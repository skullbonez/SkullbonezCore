/*
File: SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
Purpose:
  Owns runtime editor placement, selection, gizmos, and overlay tracing.

Mental model:
  Input asks for editor actions. This file performs editor math, picking,
  placement, gizmos, and line visualization.

Glossary:
  Gizmo: World-space editor axes or rotation rings used to transform selected
    models.
  Placement preflight: Capacity and asset-availability check shared by the
    "can place" query and the actual placement commit.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Preview, preflight, and placement commit must use the same object-type,
    scale, terrain, and asset rules.
  - Gizmo group indices are frame-local model indices; any capacity or deletion
    change must invalidate the captured group before applying transforms.
  - Runtime editor traces are derived from current state and must not mutate
    physics or selection ownership.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "EditorOverlayTools.h"
#include "EditorTools.h"
#include "EditorHullAssets.h"
#include "../../Assets/AssetSystem.h"
#include "../InputController.h"
#include "../RuntimeInteractionCommands.h"
#include "../RuntimePickService.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UIInput.h"
#include "../../UI/UILayout.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using Json = nlohmann::ordered_json;

namespace
{
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr float EDITOR_PLACEMENT_YAW_STEP_RADIANS = _PI / 12.0f;

// Concept: Clip-space mouse coordinates become editor rays by unprojecting two
// endpoints through the inverse view-projection matrix, then normalizing the
// world-space segment between them.
bool TransformClipPointToWorld( const Matrix4& inverseViewProjection, float x, float y, float z, Vector3& outWorld )
{
    const float worldX = inverseViewProjection.m[0] * x + inverseViewProjection.m[4] * y +
                         inverseViewProjection.m[8] * z + inverseViewProjection.m[12];
    const float worldY = inverseViewProjection.m[1] * x + inverseViewProjection.m[5] * y +
                         inverseViewProjection.m[9] * z + inverseViewProjection.m[13];
    const float worldZ = inverseViewProjection.m[2] * x + inverseViewProjection.m[6] * y +
                         inverseViewProjection.m[10] * z + inverseViewProjection.m[14];
    const float worldW = inverseViewProjection.m[3] * x + inverseViewProjection.m[7] * y +
                         inverseViewProjection.m[11] * z + inverseViewProjection.m[15];
    if ( fabsf( worldW ) < 1e-6f )
    {
        return false;
    }

    const float invW = 1.0f / worldW;
    outWorld = Vector3( worldX * invW, worldY * invW, worldZ * invW );
    return true;
}

Vector3 HullAuthoredLocalOffset( const ConvexHullShape& hull )
{
    return hull.GetPosition() + hull.GetAuthoredCenterOfMass();
}


float HullAuthoredBottomOffset( const ConvexHullShape& hull )
{
    float minY = FLT_MAX;
    const Vector3 authoredOffset = HullAuthoredLocalOffset( hull );
    for ( uint16_t i = 0; i < hull.GetVertexCount(); ++i )
    {
        minY = (std::min)( minY, authoredOffset.y + hull.GetVertex( i ).y );
    }
    return minY == FLT_MAX ? 0.0f : -minY;
}


bool EditorPositionsDiffer( const Vector3& a, const Vector3& b )
{
    return VectorMagSquared( a - b ) > 1.0e-8f;
}


bool EditorOrientationsDiffer( const Quaternion& a, const Quaternion& b )
{
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float aw = 1.0f;
    float bx = 0.0f;
    float by = 0.0f;
    float bz = 0.0f;
    float bw = 1.0f;
    a.GetComponents( ax, ay, az, aw );
    b.GetComponents( bx, by, bz, bw );

    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    const float dw = aw - bw;
    const float sx = ax + bx;
    const float sy = ay + by;
    const float sz = az + bz;
    const float sw = aw + bw;
    const float directDistanceSq = dx * dx + dy * dy + dz * dz + dw * dw;
    const float flippedDistanceSq = sx * sx + sy * sy + sz * sz + sw * sw;
    return (std::min)( directDistanceSq, flippedDistanceSq ) > 1.0e-10f;
}


float HullVerticalSize( const ConvexHullShape& hull )
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    for ( uint16_t i = 0; i < hull.GetVertexCount(); ++i )
    {
        const float y = hull.GetVertex( i ).y;
        minY = (std::min)( minY, y );
        maxY = (std::max)( maxY, y );
    }
    return minY == FLT_MAX ? 1.0f : (std::max)( 1.0f, maxY - minY );
}


constexpr float EDITOR_TEXTURE_MODE_INVERTED = -2.0f;


void ApplyEditorSpawnMaterial( GameModel& model, bool fixedObject, bool boxObject )
{
    if ( fixedObject )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, 1.0f );
    }
    else if ( boxObject )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
    }
    else
    {
        model.SetRenderTint( 0.42f, 0.50f, 1.0f, -1.0f );
    }
}


constexpr float EDITOR_PLACEMENT_SURFACE_EPSILON = 0.02f;
constexpr float EDITOR_PLACEMENT_SNAP = 2.0f;
struct EditorTreePartDefinition
{
    EditorHullAsset hullAsset;
    const char* suffix;
    float offsetX;
    float offsetY;
    float offsetZ;
    float restitution;
    SkullbonezCore::Rendering::RenderMaterialKind materialKind;
    const char* materialName;
    float colorR;
    float colorG;
    float colorB;
    float roughness;
    float specular;
    float stylization;
    bool startsFixed = false;
    bool contactReleaseOnImpact = false;
    float contactReleaseImpulseThreshold = 1.0f;
};


struct EditorTreeDefinition
{
    const char* label;
    const EditorTreePartDefinition* parts;
    int partCount;
    bool alignToTerrainNormal = false;
    bool forceFixed = false;
    bool seedAsleep = false;
};


struct EditorHousePartDefinition
{
    const char* suffix;
    float offsetX;
    float offsetY;
    float offsetZ;
    float halfX;
    float halfY;
    float halfZ;
    float restitution;
    SkullbonezCore::Rendering::RenderMaterialKind materialKind;
    const char* materialName;
    float colorR;
    float colorG;
    float colorB;
    float roughness;
    float specular;
    float stylization;
};


struct EditorHouseDefinition
{
    const char* label;
    const EditorHousePartDefinition* parts;
    int partCount;
    bool seedAsleep = true;
};


struct EditorBuildingDefinition
{
    int objectType;
    const char* assetName;
    const char* label;
};


constexpr EditorBuildingDefinition EDITOR_BUILDING_ASSETS[] = {
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP, "building.brick_house_low", "bhl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP, "building.brick_house_high", "bhh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP, "building.cute_house_low", "chl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP, "building.cute_house_high", "chh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP, "building.triple_decker_low", "tdl" },
    { SkullbonezCore::UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP, "building.triple_decker_high", "tdh" },
    { SkullbonezCore::UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP, "building.brick_wall_200", "bw200" },
};


#include "RunEditorPlacementAssets.inl"
float EditorPlacementAltitudeStepSize( int objectType,
                                       const Vector3& placementScale,
                                       const SkullbonezCore::Assets::AssetSystem& assets )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        return scale.y * 2.0f;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        return scale.x * 2.0f;
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL:
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        return scale.x * 18.5f;
    default:
    {
        if ( EditorTreeDefinitionForType( type ) )
        {
            return EditorTreeVerticalSize( type );
        }
        if ( EditorBuildingDefinitionForType( type ) )
        {
            return EditorBuildingVerticalSize( type, assets );
        }
        if ( EditorHouseDefinitionForType( type ) )
        {
            return EditorHouseVerticalSize( type );
        }
        ConvexHullShape hull;
        return TryBuildScaledEditorHullForType( type, scale, hull ) ? HullVerticalSize( hull ) : 1.0f;
    }
    }
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float EditorModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

float EditorShapeAxisExtent( const CollisionShape& shape, int axis )
{
    if ( axis < 0 || axis > 2 )
    {
        return 1.0f;
    }

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        return (std::max)( sphere->GetRadius(), 0.25f );
    }

    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& halfExtents = box->GetHalfExtents();
        if ( axis == 0 )
        {
            return (std::max)( halfExtents.x, 0.25f );
        }
        if ( axis == 1 )
        {
            return (std::max)( halfExtents.y, 0.25f );
        }
        return (std::max)( halfExtents.z, 0.25f );
    }

    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3& halfExtents = hull->GetInertiaHalfExtents();
        if ( axis == 0 )
        {
            return (std::max)( halfExtents.x, 0.25f );
        }
        if ( axis == 1 )
        {
            return (std::max)( halfExtents.y, 0.25f );
        }
        return (std::max)( halfExtents.z, 0.25f );
    }

    return 1.0f;
}


bool TryEditorScaleFactorFromShapes( const CollisionShape& startShape,
                                     const CollisionShape& currentShape,
                                     int axis,
                                     float& outFactor )
{
    const float startExtent = EditorShapeAxisExtent( startShape, axis );
    if ( startExtent <= 0.0f )
    {
        return false;
    }

    const float currentExtent = EditorShapeAxisExtent( currentShape, axis );
    outFactor = currentExtent / startExtent;
    return std::isfinite( outFactor ) && fabsf( outFactor - 1.0f ) > 1.0e-4f;
}


float EditorGizmoAxisLength( float modelRadius )
{
    return (std::max)( 14.0f, modelRadius + 12.0f );
}


float EditorGizmoRotationRadius( float modelRadius )
{
    return (std::max)( 12.0f, modelRadius + 7.0f );
}


using EditorGizmoGroupIndices = std::array<int, RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY>;

bool TryGetEditorRagdollInstancePrefixLength( const GameModel& model, std::size_t& outPrefixLength )
{
    static constexpr const char* RAGDOLL_SUFFIXES[] = { "torso",
                                                        "head",
                                                        "upper_arm_l",
                                                        "lower_arm_l",
                                                        "upper_arm_r",
                                                        "lower_arm_r",
                                                        "upper_leg_l",
                                                        "lower_leg_l",
                                                        "upper_leg_r",
                                                        "lower_leg_r" };

    const char* name = model.GetName();
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    const std::size_t nameLength = std::strlen( name );
    for ( const char* suffix : RAGDOLL_SUFFIXES )
    {
        const std::size_t suffixLength = std::strlen( suffix );
        if ( nameLength <= suffixLength + 1 )
        {
            continue;
        }

        const std::size_t suffixStart = nameLength - suffixLength;
        if ( name[suffixStart - 1] != '_' || std::strncmp( name + suffixStart, suffix, suffixLength ) != 0 )
        {
            continue;
        }

        outPrefixLength = suffixStart - 1;
        return outPrefixLength > 0;
    }
    return false;
}


bool EditorRagdollPrefixMatches( const char* a, std::size_t aLength, const char* b, std::size_t bLength )
{
    return aLength == bLength && std::strncmp( a, b, aLength ) == 0;
}


int GatherSelectedEditorTransformGroup( const std::vector<GameModel>& models,
                                        int selectedIndex,
                                        EditorGizmoGroupIndices& outIndices )
{
    outIndices.fill( -1 );
    if ( selectedIndex < 0 || selectedIndex >= static_cast<int>( models.size() ) )
    {
        return 0;
    }

    std::size_t selectedPrefixLength = 0;
    if ( !TryGetEditorRagdollInstancePrefixLength( models[static_cast<std::size_t>( selectedIndex )],
                                                   selectedPrefixLength ) )
    {
        outIndices[0] = selectedIndex;
        return 1;
    }

    const char* selectedName = models[static_cast<std::size_t>( selectedIndex )].GetName();
    int count = 0;
    for ( int i = 0; i < static_cast<int>( models.size() ) && count < static_cast<int>( outIndices.size() ); ++i )
    {
        const GameModel& candidate = models[static_cast<std::size_t>( i )];
        std::size_t prefixLength = 0;
        if ( TryGetEditorRagdollInstancePrefixLength( candidate, prefixLength ) &&
             EditorRagdollPrefixMatches( selectedName, selectedPrefixLength, candidate.GetName(), prefixLength ) )
        {
            outIndices[static_cast<std::size_t>( count )] = i;
            ++count;
        }
    }

    if ( count <= 0 )
    {
        outIndices[0] = selectedIndex;
        return 1;
    }
    return count;
}


bool TryGetEditorSelectionFrame( const std::vector<GameModel>& models,
                                 int selectedIndex,
                                 Vector3& outOrigin,
                                 float& outRadius,
                                 EditorGizmoGroupIndices* outGroupIndices = nullptr,
                                 int* outGroupCount = nullptr )
{
    EditorGizmoGroupIndices indices = {};
    const int count = GatherSelectedEditorTransformGroup( models, selectedIndex, indices );
    if ( count <= 0 )
    {
        return false;
    }

    Vector3 origin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    for ( int i = 0; i < count; ++i )
    {
        origin += models[static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] )].GetPosition();
    }
    origin /= static_cast<float>( count );

    float radius = 1.0f;
    for ( int i = 0; i < count; ++i )
    {
        const GameModel& model = models[static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] )];
        radius = (std::max)( radius, Distance( model.GetPosition(), origin ) + EditorModelRadius( model ) );
    }

    outOrigin = origin;
    outRadius = radius;
    if ( outGroupIndices )
    {
        *outGroupIndices = indices;
    }
    if ( outGroupCount )
    {
        *outGroupCount = count;
    }
    return true;
}


void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor,
                                       const std::vector<GameModel>& models,
                                       bool allowRagdollGroup )
{
    // Lifetime: Drag state stores model indices and starting transforms for the
    // current gesture only. A changed model count invalidates the group before
    // movement, scale, or rotation applies.
    editor.gizmoDragGroupCount = 0;
    editor.gizmoDragGroupIndices.fill( -1 );
    if ( editor.selectedModelIndex < 0 || editor.selectedModelIndex >= static_cast<int>( models.size() ) )
    {
        return;
    }

    EditorGizmoGroupIndices indices = {};
    int count = 1;
    indices[0] = editor.selectedModelIndex;
    if ( allowRagdollGroup )
    {
        count = GatherSelectedEditorTransformGroup( models, editor.selectedModelIndex, indices );
    }

    editor.gizmoDragGroupCount =
        std::clamp( count, 0, static_cast<int>( RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY ) );
    for ( int i = 0; i < editor.gizmoDragGroupCount; ++i )
    {
        const int index = indices[static_cast<std::size_t>( i )];
        editor.gizmoDragGroupIndices[static_cast<std::size_t>( i )] = index;
        const GameModel& model = models[static_cast<std::size_t>( index )];
        editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( i )] = model.GetPosition();
        editor.gizmoDragGroupStartOrientations[static_cast<std::size_t>( i )] = model.GetOrientation();
    }
}


int ValidCapturedEditorGizmoGroupCount( const RunEditorPlacementState& editor, int modelCount )
{
    const int count = editor.gizmoDragGroupCount;
    if ( count <= 0 || count > static_cast<int>( RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY ) )
    {
        return 0;
    }
    for ( int i = 0; i < count; ++i )
    {
        const int index = editor.gizmoDragGroupIndices[static_cast<std::size_t>( i )];
        if ( index < 0 || index >= modelCount )
        {
            return 0;
        }
    }
    return count;
}


void ResetEditorModelMotionAndWake( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                    SkullbonezCore::Physics::PhysicsEngine&,
                                    int index,
                                    GameModel& model )
{
    // Why: Direct editor transforms teleport the body. Clearing velocities and
    // waking dynamic bodies prevents stale solver momentum from immediately
    // dragging the authored pose away.
    model.SetLinearVelocity( SkullbonezCore::Math::Vector::ZERO_VECTOR );
    model.SetAngularVelocity( SkullbonezCore::Math::Vector::ZERO_VECTOR );
    if ( !model.IsFixed() )
    {
        collection.WakeModel( index );
    }
}


float ReplayVelocityLinearBaseLength( float modelRadius )
{
    return (std::max)( 10.0f, modelRadius + 7.0f );
}


float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent )
{
    const float sign = velocityComponent < 0.0f ? -1.0f : 1.0f;
    const float t = std::clamp( fabsf( velocityComponent ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
    return sign * ( ReplayVelocityLinearBaseLength( modelRadius ) + t * REPLAY_VELOCITY_EDIT_LINEAR_EXTRA );
}


float ReplayVelocityLinearUnitsPerWorld()
{
    return REPLAY_VELOCITY_EDIT_LINEAR_MAX / REPLAY_VELOCITY_EDIT_LINEAR_EXTRA;
}


float ReplayVelocityAngularBaseRadius( float modelRadius )
{
    return (std::max)( 11.0f, modelRadius + 6.0f );
}


float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent )
{
    const float t = std::clamp( fabsf( angularComponent ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
    return ReplayVelocityAngularBaseRadius( modelRadius ) + t * (std::max)( 5.0f, modelRadius * 0.85f );
}


float ReplayVelocityAxisComponent( const Vector3& value, int axis )
{
    if ( axis == 0 )
    {
        return value.x;
    }
    if ( axis == 1 )
    {
        return value.y;
    }
    return value.z;
}


void ReplayVelocitySetAxisComponent( Vector3& value, int axis, float component )
{
    if ( axis == 0 )
    {
        value.x = component;
    }
    else if ( axis == 1 )
    {
        value.y = component;
    }
    else
    {
        value.z = component;
    }
}


void ReplayVelocityAxisColor( int axis, float heat, bool hot, bool active, float& r, float& g, float& b )
{
    r = axis == 0 ? 1.0f : 0.10f;
    g = axis == 1 ? 0.95f : 0.16f;
    b = axis == 2 ? 1.0f : 0.14f;
    r = std::clamp( r + heat * 0.46f, 0.0f, 1.0f );
    g = std::clamp( g + heat * 0.22f, 0.0f, 1.0f );
    b = std::clamp( b - heat * 0.34f, 0.05f, 1.0f );
    if ( hot || active )
    {
        r = (std::min)( 1.0f, r + 0.34f );
        g = (std::min)( 1.0f, g + 0.34f );
        b = (std::min)( 1.0f, b + 0.20f );
    }
    if ( active )
    {
        r = 1.0f;
        g = 0.96f;
        b = 0.18f;
    }
}


Vector3 EditorRotationRingBasisA( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 2:
        return Vector3( 1.0f, 0.0f, 0.0f );
    default:
        return Vector3( 1.0f, 0.0f, 0.0f );
    }
}


Vector3 EditorRotationRingBasisB( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 1:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 1.0f, 0.0f );
    default:
        return Vector3( 0.0f, 1.0f, 0.0f );
    }
}


float WrapEditorAngleDelta( float delta )
{
    while ( delta > _PI )
    {
        delta -= 2.0f * _PI;
    }
    while ( delta < -_PI )
    {
        delta += 2.0f * _PI;
    }
    return delta;
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = segment * segment;
    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, toPoint * rayDirection );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segmentLenSq;
    const float d = rayDirection * w0;
    const float e = segment * w0;
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}


constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 64;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 12;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
constexpr float MOUSE_PICKUP_DEAD_ZONE = 0.04f;
constexpr float MOUSE_PICKUP_STIFFNESS = 18.0f;
constexpr float MOUSE_PICKUP_DAMPING = 1.35f;
constexpr float MOUSE_PICKUP_MAX_IMPULSE = 260.0f;

} // namespace

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
bool BeginEditorGizmoDragGesture( EditorGizmoContext context, int modelIndex, int axis, bool angular )
{
    if ( context.interaction.PointerCapture() != RuntimePointerCaptureOwner::None ||
         context.interaction.Gesture().kind != RuntimeInteractionGestureKind::None )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = mouse.x;
    gesture.startY = mouse.y;
    gesture.modelIndex = modelIndex;
    gesture.axis = axis;
    gesture.angular = angular;

    context.interaction.BeginGesture( gesture,
                                      RuntimePointerCaptureOwner::ToolGesture,
                                      InteractionExitReason::BeginGesture );
    return context.interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag;
}


void EndEditorGizmoDragGesture( EditorGizmoContext context )
{
    if ( context.interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        context.interaction.EndGesture( InteractionExitReason::EndGesture );
    }
}


void CancelEditorGizmoDragState( EditorGizmoContext context )
{
    EndEditorGizmoDragGesture( context );
    context.editor.gizmoDragActive = false;
    context.editor.gizmoDragIsRotation = false;
    context.editor.gizmoDragIsScale = false;
    context.editor.activeGizmoAxis = -1;
    context.editor.gizmoDragPlaneNormal = Math::Vector::ZERO_VECTOR;
    context.editor.gizmoDragGroupCount = 0;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore


void Run::TickEditorViewportAndPlacementScaleInput( int unhandledWheelDelta )
{
    const bool editorViewportLookNow =
        m_runtimeTools.Editor().editorModeEnabled && Input::IsRightMouseDown() && !m_UI.BlocksCameraMouse();
    if ( editorViewportLookNow != m_runtimeTools.Editor().viewportLookActive )
    {
        InputController::ResetMouseLook( m_camera );
    }
    m_runtimeTools.Editor().viewportLookActive = editorViewportLookNow;
    if ( editorViewportLookNow != ( m_runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook ) )
    {
        UpdateRuntimeInputModeAfterAction( editorViewportLookNow ? RuntimeInputAction::BeginEditorViewportLook
                                                                 : RuntimeInputAction::EndEditorViewportLook,
                                           RuntimeInputActionSource::Mouse );
    }

    const int placementWheelSteps = EditorMouseWheelSteps( unhandledWheelDelta );
    const bool placementLeftMouseNow = Input::IsLeftMouseDown();
    const bool placementYawWheel = placementWheelSteps != 0 && m_runtimeTools.Editor().editorModeEnabled &&
                                   m_runtimeTools.Editor().placementModeEnabled && Input::IsKeyDown( VK_CONTROL ) &&
                                   !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse();
    if ( placementYawWheel )
    {
        EnterInteractiveSceneRun();
        m_runtimeTools.Editor().placementYawRadians =
            WrapEditorAngleDelta( m_runtimeTools.Editor().placementYawRadians +
                                  static_cast<float>( placementWheelSteps ) * EDITOR_PLACEMENT_YAW_STEP_RADIANS );
    }
    if ( m_runtimeTools.Editor().placementScaleActive && placementLeftMouseNow &&
         !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse() )
    {
        if ( placementWheelSteps != 0 && !placementYawWheel )
        {
            EnterInteractiveSceneRun();
            m_runtimeTools.Editor().placementScaleWheelSteps += placementWheelSteps;
        }

        const POINT currentClient = Input::GetClientMouseCoordinates();
        const float dragPixelsX =
            static_cast<float>( currentClient.x - m_runtimeTools.Editor().placementScaleStartClient.x );
        const float dragPixelsY =
            static_cast<float>( currentClient.y - m_runtimeTools.Editor().placementScaleStartClient.y );
        m_runtimeTools.Editor().placementScale =
            EditorPlacementScaleFromGesture( m_runtimeTools.Editor().objectType,
                                             m_runtimeTools.Editor().placementScaleStart,
                                             dragPixelsX,
                                             dragPixelsY,
                                             m_runtimeTools.Editor().placementScaleWheelSteps );
    }
    else if ( placementWheelSteps != 0 && m_runtimeTools.Editor().editorModeEnabled &&
              m_runtimeTools.Editor().placementModeEnabled && !placementYawWheel &&
              !m_runtimeTools.Editor().viewportLookActive && !m_UI.BlocksCameraMouse() )
    {
        const int nextAltitudeSteps =
            (std::max)( 0, m_runtimeTools.Editor().placementAltitudeSteps + placementWheelSteps );
        if ( nextAltitudeSteps != m_runtimeTools.Editor().placementAltitudeSteps )
        {
            EnterInteractiveSceneRun();
            m_runtimeTools.Editor().placementAltitudeSteps = nextAltitudeSteps;
        }
    }
    ApplyCursorOwnership();
}


bool Run::TickEditorWorldClick( const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    const bool previewInspectGizmoActive = InspectGizmoInteractionActive();
    const bool previewCanUseMouseRay = !m_UI.BlocksCameraMouse() && !m_runtimeTools.Editor().viewportLookActive &&
                                       ( m_runtimeTools.Editor().editorModeEnabled || previewInspectGizmoActive );
    const bool previewNeedsMouseRay =
        previewCanUseMouseRay &&
        ( ( m_runtimeTools.Editor().editorModeEnabled && m_runtimeTools.Editor().placementModeEnabled &&
            !m_runtimeTools.Editor().placementScaleActive ) ||
          ( m_runtimeTools.Editor().selectedModelIndex >= 0 && !m_runtimeTools.Editor().gizmoDragActive &&
            !m_runtimeTools.Editor().placementModeEnabled ) );

    Vector3 previewRayOrigin;
    Vector3 previewRayDirection;
    const bool hasPreviewMouseRay =
        previewNeedsMouseRay && TryBuildMouseWorldRay( previewRayOrigin, previewRayDirection );

    const EditorInteractionPreviewResult previewResult = UpdateEditorInteractionPreview(
        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction, m_systems.terrain.get(), m_systems.assets },
        { m_UI.BlocksCameraMouse(),
          previewInspectGizmoActive,
          hasPreviewMouseRay,
          previewRayOrigin,
          previewRayDirection,
          Input::IsKeyDown( VK_CONTROL ) } );

    if ( previewResult.clearInvalidSelection )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.modelIndex = -1;
        command.selectionScope = previewResult.inspectSelectionScope ? RuntimeInteractionSelectionScope::Inspect
                                                                     : RuntimeInteractionSelectionScope::Editor;
        command.claimSelectionOwner = false;
        ExecuteRuntimeInteractionCommand( command );
        CancelEditorGizmoDragState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
    }

    const bool leftMouseNow = mouseEdges.leftDown;
    const bool leftPressed = mouseEdges.leftPressed;
    const bool leftReleased = mouseEdges.leftReleased;
    bool consumedWorldClick = false;

    if ( m_runtimeTools.Editor().placementScaleActive )
    {
        consumedWorldClick = true;
        if ( leftReleased || suppressWorldActionThisFrame )
        {
            if ( leftReleased && !suppressWorldActionThisFrame && m_runtimeTools.Editor().placementPreviewVisible )
            {
                EditorObjectPlacementContext placementContext{ m_runtimeTools.Editor(),
                                                               m_cGameModelCollection,
                                                               SceneState(),
                                                               m_cWorldEnvironment,
                                                               m_systems.terrain.get(),
                                                               m_systems.assets,
                                                               ActiveGameModelCapacity() };
                EditorObjectPlacementRequest placementRequest{ m_runtimeTools.Editor().objectType,
                                                               m_runtimeTools.Editor().placeStaticObject,
                                                               m_runtimeTools.Editor().placementTerrainPoint };
                EditorObjectPlacementResult placementResult;
                if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
                {
                    EnterInteractiveSceneRun();
                    PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
                    if ( placementResult.placed )
                    {
                        m_replayRuntime.RecordEditorPlaceEvent( placementResult.objectType,
                                                                placementResult.fixedObject,
                                                                placementResult.autoTerrainAlign,
                                                                placementResult.modelCountBefore,
                                                                placementResult.terrainPoint,
                                                                placementResult.placementScale,
                                                                placementResult.placementYawRadians );

                        RuntimeInteractionCommand command;
                        command.type = RuntimeInteractionCommandType::SetEditorSelection;
                        command.modelIndex = placementResult.modelCountAfter - 1;
                        command.claimSelectionOwner = false;
                        ExecuteRuntimeInteractionCommand( command );
                    }
                }
            }
            m_runtimeTools.Editor().placementScaleActive = false;
            m_runtimeTools.Editor().placementScaleWheelSteps = 0;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorPlacementScale,
                                               RuntimeInputActionSource::Mouse );
        }
    }

    if ( !consumedWorldClick && m_runtimeTools.Editor().gizmoDragActive )
    {
        consumedWorldClick = true;
        if ( leftMouseNow && !suppressWorldActionThisFrame )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
            {
                if ( m_runtimeTools.Editor().gizmoDragIsScale )
                {
                    ScaleSelectedEditorObjectAlongAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
                else if ( m_runtimeTools.Editor().gizmoDragIsRotation )
                {
                    RotateSelectedEditorObjectAroundAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
                else
                {
                    MoveSelectedEditorObjectAlongAxis(
                        { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                        rayOrigin,
                        rayDirection );
                }
            }
        }
        if ( leftReleased || suppressWorldActionThisFrame )
        {
            if ( leftReleased && !suppressWorldActionThisFrame && m_runtimeTools.Editor().selectedModelIndex >= 0 &&
                 m_runtimeTools.Editor().selectedModelIndex < m_cGameModelCollection.GetModelCount() )
            {
                const GameModel& model =
                    m_cGameModelCollection.GetModelAtIndex( m_runtimeTools.Editor().selectedModelIndex );
                if ( m_runtimeTools.Editor().gizmoDragIsScale )
                {
                    int scaleAxis = m_runtimeTools.Editor().activeGizmoAxis;
                    float scaleFactor = 1.0f;
                    const uint32_t changedFlags =
                        TryEditorScaleFactorFromShapes( m_runtimeTools.Editor().gizmoDragStartShape,
                                                        model.GetCollisionShape(),
                                                        scaleAxis,
                                                        scaleFactor )
                            ? REPLAY_EDITOR_TRANSFORM_SCALE
                            : 0u;
                    m_replayRuntime.RecordEditorTransformEvent( m_runtimeTools.Editor().selectedModelIndex,
                                                                changedFlags,
                                                                model,
                                                                m_cGameModelCollection.GetModelCount(),
                                                                scaleAxis,
                                                                scaleFactor );
                }
                else
                {
                    const int groupCount = ValidCapturedEditorGizmoGroupCount( m_runtimeTools.Editor(),
                                                                               m_cGameModelCollection.GetModelCount() );
                    if ( groupCount > 0 )
                    {
                        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
                        {
                            const int modelIndex =
                                m_runtimeTools.Editor().gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
                            const GameModel& groupModel = m_cGameModelCollection.GetModelAtIndex( modelIndex );
                            uint32_t changedFlags = 0;
                            changedFlags |=
                                EditorPositionsDiffer(
                                    groupModel.GetPosition(),
                                    m_runtimeTools.Editor()
                                        .gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] )
                                    ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                    : 0u;
                            changedFlags |=
                                EditorOrientationsDiffer(
                                    groupModel.GetOrientation(),
                                    m_runtimeTools.Editor()
                                        .gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )] )
                                    ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                    : 0u;
                            m_replayRuntime.RecordEditorTransformEvent( modelIndex,
                                                                        changedFlags,
                                                                        groupModel,
                                                                        m_cGameModelCollection.GetModelCount(),
                                                                        -1,
                                                                        1.0f );
                        }
                    }
                    else
                    {
                        uint32_t changedFlags = 0;
                        changedFlags |=
                            EditorPositionsDiffer( model.GetPosition(), m_runtimeTools.Editor().gizmoDragStartPosition )
                                ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                : 0u;
                        changedFlags |= EditorOrientationsDiffer( model.GetOrientation(),
                                                                  m_runtimeTools.Editor().gizmoDragStartOrientation )
                                            ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                            : 0u;
                        m_replayRuntime.RecordEditorTransformEvent( m_runtimeTools.Editor().selectedModelIndex,
                                                                    changedFlags,
                                                                    model,
                                                                    m_cGameModelCollection.GetModelCount(),
                                                                    -1,
                                                                    1.0f );
                    }
                }
            }
            CancelEditorGizmoDragState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::EndEditorGizmoDrag,
                                               RuntimeInputActionSource::Mouse );
        }
    }

    if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame )
    {
        const bool inspectGizmoActive = InspectGizmoInteractionActive();
        const bool transformGizmoActive = ( m_runtimeTools.Editor().editorModeEnabled || inspectGizmoActive ) &&
                                          !m_runtimeTools.Editor().placementModeEnabled;
        const WorldInteractionOwner transformOwner =
            inspectGizmoActive ? WorldInteractionOwner::InspectGizmo : WorldInteractionOwner::EditorGizmo;
        const InteractionExitReason transformReason =
            inspectGizmoActive ? InteractionExitReason::EnterInspect : InteractionExitReason::EnterEdit;
        const bool canCaptureGizmoGesture = m_interaction.PointerCapture() == RuntimePointerCaptureOwner::None &&
                                            m_interaction.Gesture().kind == RuntimeInteractionGestureKind::None;
        const bool editorScaleMode = transformGizmoActive && Input::IsKeyDown( VK_CONTROL );
        if ( canCaptureGizmoGesture && editorScaleMode && m_runtimeTools.Editor().selectedModelIndex >= 0 &&
             m_runtimeTools.Editor().selectedModelIndex < m_cGameModelCollection.GetModelCount() &&
             m_runtimeTools.Editor().hotGizmoAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float axisT = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryEditorAxisRayParameter( gizmoContext,
                                            m_runtimeTools.Editor().hotGizmoAxis,
                                            rayOrigin,
                                            rayDirection,
                                            axisT ) )
            {
                EnterInteractiveSceneRun();
                SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                   m_runtimeTools.Editor().selectedModelIndex,
                                                   m_runtimeTools.Editor().hotGizmoAxis,
                                                   false ) )
                {
                    return consumedWorldClick;
                }
                GameModel& model = m_cGameModelCollection.GetModelAtIndex( m_runtimeTools.Editor().selectedModelIndex );
                m_runtimeTools.Editor().gizmoDragActive = true;
                m_runtimeTools.Editor().gizmoDragIsRotation = false;
                m_runtimeTools.Editor().gizmoDragIsScale = true;
                m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotGizmoAxis;
                m_runtimeTools.Editor().gizmoDragStartAxisT = axisT;
                m_runtimeTools.Editor().gizmoDragStartShape = model.GetCollisionShape();
                m_runtimeTools.Editor().gizmoDragStartPosition = model.GetPosition();
                m_runtimeTools.Editor().gizmoDragStartOrientation = model.GetOrientation();
                CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), m_cGameModelCollection.Models(), false );
                consumedWorldClick = true;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoScale,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( canCaptureGizmoGesture && transformGizmoActive && !editorScaleMode &&
             m_runtimeTools.Editor().selectedModelIndex >= 0 && m_runtimeTools.Editor().hotRotationAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float startAngle = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                 TryEditorRotationRayAngle( gizmoContext,
                                            m_runtimeTools.Editor().hotRotationAxis,
                                            rayOrigin,
                                            rayDirection,
                                            startAngle ) )
            {
                EnterInteractiveSceneRun();
                SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                   m_runtimeTools.Editor().selectedModelIndex,
                                                   m_runtimeTools.Editor().hotRotationAxis,
                                                   true ) )
                {
                    return consumedWorldClick;
                }
                m_runtimeTools.Editor().gizmoDragActive = true;
                m_runtimeTools.Editor().gizmoDragIsRotation = true;
                m_runtimeTools.Editor().gizmoDragIsScale = false;
                m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotRotationAxis;
                m_runtimeTools.Editor().gizmoDragStartRotationAngle = startAngle;
                const std::vector<GameModel>& models = m_cGameModelCollection.Models();
                Vector3 selectionOrigin =
                    models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetPosition();
                float selectionRadius = 1.0f;
                TryGetEditorSelectionFrame( models,
                                            m_runtimeTools.Editor().selectedModelIndex,
                                            selectionOrigin,
                                            selectionRadius );
                m_runtimeTools.Editor().gizmoDragStartPosition = selectionOrigin;
                m_runtimeTools.Editor().gizmoDragStartOrientation =
                    models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetOrientation();
                CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), models, true );
                consumedWorldClick = true;
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoRotate,
                                                   RuntimeInputActionSource::Mouse );
            }
        }

        if ( !consumedWorldClick && canCaptureGizmoGesture && transformGizmoActive && !editorScaleMode &&
             m_runtimeTools.Editor().selectedModelIndex >= 0 && m_runtimeTools.Editor().hotGizmoAxis >= 0 )
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            float axisT = 0.0f;
            EditorGizmoContext gizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction };
            const std::vector<GameModel>& models = m_cGameModelCollection.Models();
            Vector3 selectionOrigin;
            float selectionRadius = 1.0f;
            const bool haveSelectionFrame = TryGetEditorSelectionFrame( models,
                                                                        m_runtimeTools.Editor().selectedModelIndex,
                                                                        selectionOrigin,
                                                                        selectionRadius );
            if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) && haveSelectionFrame )
            {
                const Vector3 planeNormal =
                    EditorAxisDragPlaneNormal( m_runtimeTools.Editor().hotGizmoAxis, rayDirection );
                if ( TryEditorAxisPlaneRayParameter( m_runtimeTools.Editor().hotGizmoAxis,
                                                     selectionOrigin,
                                                     planeNormal,
                                                     rayOrigin,
                                                     rayDirection,
                                                     axisT ) )
                {
                    EnterInteractiveSceneRun();
                    SetWorldInteractionOwnerAfterInteractionTransition( transformOwner, transformReason );
                    if ( !BeginEditorGizmoDragGesture( gizmoContext,
                                                       m_runtimeTools.Editor().selectedModelIndex,
                                                       m_runtimeTools.Editor().hotGizmoAxis,
                                                       false ) )
                    {
                        return consumedWorldClick;
                    }
                    m_runtimeTools.Editor().gizmoDragActive = true;
                    m_runtimeTools.Editor().gizmoDragIsRotation = false;
                    m_runtimeTools.Editor().gizmoDragIsScale = false;
                    m_runtimeTools.Editor().activeGizmoAxis = m_runtimeTools.Editor().hotGizmoAxis;
                    m_runtimeTools.Editor().gizmoDragStartAxisT = axisT;
                    m_runtimeTools.Editor().gizmoDragStartPosition = selectionOrigin;
                    m_runtimeTools.Editor().gizmoDragPlaneNormal = planeNormal;
                    m_runtimeTools.Editor().gizmoDragStartOrientation =
                        models[static_cast<size_t>( m_runtimeTools.Editor().selectedModelIndex )].GetOrientation();
                    CaptureEditorGizmoDragGroupState( m_runtimeTools.Editor(), models, true );
                    consumedWorldClick = true;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorGizmoTranslate,
                                                       RuntimeInputActionSource::Mouse );
                }
            }
        }

        if ( !consumedWorldClick && ( m_runtimeTools.Editor().editorModeEnabled || inspectGizmoActive ) )
        {
            if ( m_runtimeTools.Editor().placementModeEnabled )
            {
                consumedWorldClick = true;
                if ( m_runtimeTools.Editor().placementPreviewVisible )
                {
                    m_runtimeTools.Editor().placementScaleActive = true;
                    m_runtimeTools.Editor().placementScaleWheelSteps = 0;
                    m_runtimeTools.Editor().placementScaleStart =
                        EditorClampPlacementScale( m_runtimeTools.Editor().objectType,
                                                   m_runtimeTools.Editor().placementScale );
                    m_runtimeTools.Editor().placementScale = m_runtimeTools.Editor().placementScaleStart;
                    m_runtimeTools.Editor().placementScaleStartClient = Input::GetClientMouseCoordinates();
                    m_runtimeTools.Editor().placementScaleTerrainPoint = m_runtimeTools.Editor().placementTerrainPoint;
                    m_runtimeTools.Editor().placementScaleRayOrigin = m_runtimeTools.Editor().placementRayOrigin;
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::BeginEditorPlacementScale,
                                                       RuntimeInputActionSource::Mouse );
                }
            }
            else
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                RuntimePickResult result;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    RuntimePickRequest request;
                    request.purpose = RuntimePickPurpose::EditorSelection;
                    request.models = &m_cGameModelCollection.Models();
                    request.rayOrigin = rayOrigin;
                    request.rayDirection = rayDirection;
                    RuntimePickService::TryPickModel( request, result );
                }

                if ( result.modelIndex >= 0 )
                {
                    RuntimeInteractionCommand command;
                    command.type = RuntimeInteractionCommandType::SetEditorSelection;
                    command.modelIndex = result.modelIndex;
                    command.selectionScope = inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect
                                                                : RuntimeInteractionSelectionScope::Editor;
                    consumedWorldClick = ExecuteRuntimeInteractionCommand( command );
                }
                else
                {
                    RuntimeInteractionCommand command;
                    command.type = RuntimeInteractionCommandType::SetEditorSelection;
                    command.modelIndex = -1;
                    command.selectionScope = inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect
                                                                : RuntimeInteractionSelectionScope::Editor;
                    consumedWorldClick = ExecuteRuntimeInteractionCommand( command );
                }
            }
        }
    }

    return consumedWorldClick;
}


#include "RunEditorTracer.inl"
bool Run::TryBuildMouseWorldRay( Vector3& outOrigin, Vector3& outDirection, bool clampToViewport ) const
{
    if ( !m_systems.window || !m_systems.cameras )
    {
        return false;
    }

    POINT mouse = Input::GetClientMouseCoordinates();
    const int screenW = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.x ) );
    const int screenH = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.y ) );
    if ( clampToViewport )
    {
        // Invariant: Captured tool drags keep receiving mouse positions after
        // the cursor leaves the client area. Clamp those positions to the
        // nearest viewport edge so drag math remains continuous instead of
        // dropping frames and jumping when the cursor re-enters.
        mouse.x = std::clamp<LONG>( mouse.x, 0L, static_cast<LONG>( screenW - 1 ) );
        mouse.y = std::clamp<LONG>( mouse.y, 0L, static_cast<LONG>( screenH - 1 ) );
    }
    if ( mouse.x < 0 || mouse.y < 0 || mouse.x >= screenW || mouse.y >= screenH )
    {
        return false;
    }

    const float ndcX = ( static_cast<float>( mouse.x ) / static_cast<float>( screenW ) ) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ( static_cast<float>( mouse.y ) / static_cast<float>( screenH ) ) * 2.0f;

    const Vector3 eye = m_systems.cameras->GetCameraTranslation();
    const Vector3 view = m_systems.cameras->GetCameraView();
    const Vector3 up = m_systems.cameras->GetCameraUp();
    const Matrix4 viewMatrix = Matrix4::LookAt( eye, view, up );
    const Matrix4 inverseViewProjection = ( m_systems.window->GetProjectionMatrix() * viewMatrix ).Inverse();

    Vector3 rayNear;
    Vector3 rayFar;
    if ( !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 0.0f, rayNear ) ||
         !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 1.0f, rayFar ) )
    {
        return false;
    }

    Vector3 rayDirection = rayFar - rayNear;
    const float dirLenSq = VectorMagSquared( rayDirection );
    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    outOrigin = rayNear;
    outDirection = rayDirection * ( 1.0f / sqrtf( dirLenSq ) );
    return true;
}


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
bool TryGetEditorTerrainPlacement( Geometry::Terrain* terrain,
                                   const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   EditorTerrainPlacement& outPlacement )
{
    if ( !terrain )
    {
        return false;
    }

    outPlacement.rayOrigin = rayOrigin;
    outPlacement.rayDirection = rayDirection;
    // Concept: Terrain picking samples along the ray until it crosses from
    // above terrain to below terrain, then bisects the last interval for a
    // stable placement point without depending on renderer picking.
    constexpr float MAX_RAY_DISTANCE = 5000.0f;
    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = MAX_RAY_DISTANCE * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Vector3 sample = rayOrigin + rayDirection * t;
        if ( !terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outPlacement.position = Vector3( sample.x, terrainY, sample.z );
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;
            Vector3 hit = sample;
            float hitY = terrainY;
            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Vector3 mid = rayOrigin + rayDirection * midT;
                if ( !terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;
                hit = mid;
                hitY = midTerrainY;
                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }
            outPlacement.position = Vector3( hit.x, hitY, hit.z );
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}


bool TryComputeEditorObjectCenter( int objectType,
                                   const Vector3& terrainPoint,
                                   const Vector3& placementScale,
                                   const Quaternion& orientation,
                                   const Assets::AssetSystem& assets,
                                   Vector3& outCenter )
{
    const int type = std::clamp( objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    Quaternion orientationCopy = orientation;
    const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        outCenter = terrainPoint + rotation * Vector3( 0.0f, scale.y + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        return true;
    case UI::EditorTab::OBJECT_BALL:
    case UI::EditorTab::OBJECT_SPHERE:
        outCenter =
            Vector3( terrainPoint.x, terrainPoint.y + scale.x + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        outCenter = Ragdoll::DefaultPreviewCenter( terrainPoint, scale.x, orientation );
        return true;
    case UI::EditorTab::OBJECT_TREE_SMALL:
    case UI::EditorTab::OBJECT_TREE_BIG:
    case UI::EditorTab::OBJECT_TREE_CEDAR:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
    case UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
    case UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
    case UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
    case UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
    case UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
    case UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
    {
        const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
        Vector3 minV;
        Vector3 maxV;
        if ( !tree || !TryComputeEditorTreeWorldBounds( *tree, terrainPoint, rotation, minV, maxV ) )
        {
            return false;
        }
        outCenter = ( minV + maxV ) * 0.5f;
        return true;
    }
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP:
    {
        Vector3 minV;
        Vector3 maxV;
        if ( !TryComputeEditorBuildingWorldBounds( type, terrainPoint, orientation, assets, minV, maxV ) )
        {
            return false;
        }
        outCenter = ( minV + maxV ) * 0.5f;
        return true;
    }
    default:
    {
        ConvexHullShape hull;
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return false;
        }
        const Vector3 authoredOrigin =
            terrainPoint +
            rotation * Vector3( 0.0f, HullAuthoredBottomOffset( hull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        outCenter = authoredOrigin + rotation * hull.GetAuthoredCenterOfMass();
        return true;
    }
    }
}


bool TryUpdateEditorPlacementPreview( EditorPlacementPreviewContext context,
                                      int objectType,
                                      const EditorTerrainPlacement* mousePlacement )
{
    Vector3 terrainPoint;
    Vector3 rayOrigin;
    bool terrainAlreadyIncludesAltitude = false;
    if ( context.editor.placementScaleActive )
    {
        terrainPoint = context.editor.placementScaleTerrainPoint;
        rayOrigin = context.editor.placementScaleRayOrigin;
        terrainAlreadyIncludesAltitude = true;
    }
    else
    {
        if ( !mousePlacement )
        {
            return false;
        }
        terrainPoint = mousePlacement->position;
        rayOrigin = mousePlacement->rayOrigin;

        if ( context.terrain && EDITOR_PLACEMENT_SNAP > 0.0f )
        {
            const float snappedX = roundf( terrainPoint.x / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            const float snappedZ = roundf( terrainPoint.z / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            if ( context.terrain->IsInBounds( snappedX, snappedZ ) )
            {
                terrainPoint.x = snappedX;
                terrainPoint.z = snappedZ;
                terrainPoint.y = context.terrain->GetTerrainHeightAt( snappedX, snappedZ );
            }
        }
    }

    if ( !terrainAlreadyIncludesAltitude )
    {
        // Invariant: Placement altitude is applied before normal/orientation
        // lookup so preview and commit agree about the authored terrain point.
        terrainPoint.y += static_cast<float>( context.editor.placementAltitudeSteps ) *
                          EditorPlacementAltitudeStepSize( objectType, context.editor.placementScale, context.assets );
    }

    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        context.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const Quaternion placementOrientation = EditorPlacementOrientation( objectType,
                                                                        terrainNormal,
                                                                        context.editor.autoTerrainAlign,
                                                                        context.editor.placementYawRadians );

    Vector3 center;
    if ( !TryComputeEditorObjectCenter( objectType,
                                        terrainPoint,
                                        context.editor.placementScale,
                                        placementOrientation,
                                        context.assets,
                                        center ) )
    {
        return false;
    }

    context.editor.placementTerrainPoint = terrainPoint;
    context.editor.placementCenter = center;
    context.editor.placementOrientation = placementOrientation;
    context.editor.placementRayOrigin = rayOrigin;
    context.editor.placementRayHit = terrainPoint;
    return true;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore


#include "RunMousePickupTools.inl"
#include "RunEditorGizmoTools.inl"
#include "RunEditorOverlayTools.inl"
#include "RunEditorObjectPlacement.inl"

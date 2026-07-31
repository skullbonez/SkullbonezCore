/*
File: SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
Purpose:
  Owns runtime editor placement, selection, gizmos, and overlay tracing.

Summary:
  Owns runtime editor placement,
  selection, gizmos, and overlay tracing.

Glossary:
  Placement preflight: Capacity and asset-availability check shared by the
    "can place" query and the actual placement commit.
  Body-store row: Physics-owned record that receives editor wake/sleep commands
    after a model-index selection has been validated.

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
  - Agentic/Reference/engine-glossary.md
*/
#include "EditorOverlayTools.h"
#include "EditorPlacementAssets.h"
#include "EditorTools.h"
#include "EditorHullAssets.h"
#include "../Tools/RuntimeTools.h"
#include "../Input/InputRouter.h"
#include "../Camera/CameraCollection.h"
#include "../App/Window.h"
#include "../../Assets/AssetSystem.h"
#include "../Scene/SceneController.h"
#include "../Input/InputController.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/RuntimePickService.h"
#include "../Scene/SceneAuthoredSetup.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UILayout.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"
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
#include <fstream>
#include <string>
#include <utility>

using SkullbonezCore::Math::Vector::Vector3;
namespace Assets = SkullbonezCore::Assets;
namespace Environment = SkullbonezCore::Environment;
namespace Geometry = SkullbonezCore::Geometry;
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::Runtime::SceneController;
using Json = nlohmann::ordered_json;

namespace
{
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED = REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE |
                                                       REPLAY_EDITOR_TRANSFORM_SCALE;
constexpr float EDITOR_PLACEMENT_YAW_STEP_RADIANS = _PI / 12.0f;

// Concept: Clip-space mouse coordinates become editor rays by unprojecting two
// endpoints through the inverse view-projection matrix, then normalizing the
// world-space segment between them.
bool TransformClipPointToWorld( const Matrix4& inverseViewProjection, float x, float y, float z, Vector3& outWorld )
{
    const float worldX = inverseViewProjection.m[0] * x + inverseViewProjection.m[4] * y + inverseViewProjection.m[8] * z +
                         inverseViewProjection.m[12];

    const float worldY = inverseViewProjection.m[1] * x + inverseViewProjection.m[5] * y + inverseViewProjection.m[9] * z +
                         inverseViewProjection.m[13];

    const float worldZ = inverseViewProjection.m[2] * x + inverseViewProjection.m[6] * y + inverseViewProjection.m[10] * z +
                         inverseViewProjection.m[14];

    const float worldW = inverseViewProjection.m[3] * x + inverseViewProjection.m[7] * y + inverseViewProjection.m[11] * z +
                         inverseViewProjection.m[15];

    if ( fabsf( worldW ) < 1e-6f )
    {
        return false;
    }

    const float invW = 1.0f / worldW;
    outWorld = Vector3( worldX * invW, worldY * invW, worldZ * invW );
    return true;
}


bool RecordEditorTransformEventFromBodyStore( ReplayEventCommandBatch& replayEvents,
                                              SkullbonezCore::Runtime::SceneWorld& world, int modelIndex,
                                              uint32_t changedFlags, int scaleAxis, float scaleFactor )
{

    // Why: editor gizmos mutate the SceneWorld-owned authoring edge, then
    // commit into PhysicsBodyStore. Replay event bytes must come from that
    // authoritative body row so legacy model-side writeback is not required
    // before recording.
    changedFlags &= REPLAY_EDITOR_TRANSFORM_SUPPORTED;

    if ( changedFlags == 0 )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex );

    if ( !body || !body->sceneObjectId.IsValid() )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const auto hotFields = bodyStore.HotFields();

    if ( !replayEvents.Append( ReplayEventCommandOperations::BuildEditorTransform( modelIndex, changedFlags, body->sceneObjectId,
                                                                                   PhysicsBodyPosition( hotFields, bodyIndex ),
                                                                                   PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                                                   world.SceneEntityCount(), scaleAxis, scaleFactor ) ) )
    {
        SB_FATAL( "Runtime/EditorTools", "Replay editor-event batch capacity exhausted." );
    }

    return true;
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


float EditorPlacementAltitudeStepSize( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                       const Vector3& placementScale, const SkullbonezCore::Assets::AssetSystem& assets )
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
            return EditorTreeVerticalSize( diagnostics, type );
        }

        if ( EditorBuildingDefinitionForType( type ) )
        {
            return EditorBuildingVerticalSize( diagnostics, type, assets );
        }

        if ( EditorHouseDefinitionForType( type ) )
        {
            return EditorHouseVerticalSize( type );
        }

        ConvexHullShape hull;
        return TryBuildScaledEditorHullForType( diagnostics, type, scale, hull ) ? HullVerticalSize( hull ) : 1.0f;
    }
    }
}


} // namespace

namespace SkullbonezCore
{
namespace Runtime
{
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


float EditorColliderRadius( const ColliderRecord& collider )
{
    return (std::max)( collider.boundingRadius > 0.0f ? collider.boundingRadius : GetShapeBoundingRadius( collider.shape ),
                       1.0f );
}


float EditorShapeAxisExtent( const CollisionShapeReference& shape, int axis )
{

    if ( axis < 0 || axis > 2 )
    {
        return 1.0f;
    }

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape ) )
    {
        return (std::max)( sphere->GetRadius(), 0.25f );
    }

    if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &shape ) )
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

    if ( const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &shape ) )
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


bool TryEditorScaleFactorFromShapes( const CollisionShapeReference& startShape, const CollisionShapeReference& currentShape,
                                     int axis, float& outFactor )
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

// Why: editor transform grouping is scene-object metadata, not physics state.
// SceneWorld owns a dense grouping row beside model order, so gizmo grouping
// does not parse display names or read legacy physics metadata from legacy object record.
int GatherSelectedEditorTransformGroup( const SceneWorld& world, int selectedIndex, EditorGizmoGroupIndices& outIndices )
{
    outIndices.fill( -1 );
    return world.Entities().GatherGroupMemberIndices( selectedIndex, outIndices.data(),
                                                      static_cast<int>( outIndices.size() ) );
}


const PhysicsBodyRecord* TryResolveEditorBodyRecord( const PhysicsBodyStore& bodyStore, PhysicsBodyHandle bodyHandle,
                                                     int modelIndex )
{

    // Invariant: editor selection carries the handle as live physics identity.
    // The model index is a UI/grouping hint and must agree before callers read
    // the dense store row.
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );

    if ( !body || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex )
    {
        return nullptr;
    }

    return body;
}


const PhysicsBodyRecord* TryResolveEditorBodyRecord( const PhysicsBodyStore& bodyStore, int modelIndex )
{
    return TryResolveEditorBodyRecord( bodyStore, bodyStore.HandleForModelIndex( modelIndex ), modelIndex );
}


bool TryResolveEditorBodyCollider( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                   PhysicsBodyHandle bodyHandle, PhysicsColliderHandle colliderHandle, int modelIndex,
                                   const PhysicsBodyRecord*& outBody, const ColliderRecord*& outCollider )
{
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );

    if ( !body || !collider || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex ||
         colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex || collider->body != bodyHandle )
    {
        outBody = nullptr;
        outCollider = nullptr;
        return false;
    }

    outBody = body;
    outCollider = collider;
    return true;
}


bool TryGetEditorSelectionFrame( const SceneWorld& world, PhysicsBodyHandle selectedBodyHandle,
                                 PhysicsColliderHandle selectedColliderHandle, int selectedIndex, Vector3& outOrigin,
                                 float& outRadius )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();
    EditorGizmoGroupIndices indices = {};

    const int count = GatherSelectedEditorTransformGroup( world, selectedIndex, indices );

    if ( count <= 0 )
    {
        return false;
    }

    // Invariant: editor selection may group by legacy object record name metadata, but the
    // interactive frame itself must use live body/collider rows so stale
    // presentation poses do not steer hit testing or drag math.
    std::array<const ColliderRecord*, RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY> colliders = {};
    const auto hotFields = bodyStore.HotFields();
    Vector3 origin = SkullbonezCore::Math::Vector::ZERO_VECTOR;

    for ( int i = 0; i < count; ++i )
    {
        const int modelIndex = indices[static_cast<std::size_t>( i )];
        const PhysicsBodyRecord* body = nullptr;
        const ColliderRecord* collider = nullptr;
        const bool selectedMember = modelIndex == selectedIndex;
        const PhysicsBodyHandle bodyHandle = selectedMember ? selectedBodyHandle
                                                            : bodyStore.HandleForModelIndex( modelIndex );

        const PhysicsColliderHandle colliderHandle = selectedMember ? selectedColliderHandle
                                                                    : colliderStore.HandleForBodyHandle( bodyHandle );

        if ( !TryResolveEditorBodyCollider( bodyStore, colliderStore, bodyHandle, colliderHandle, modelIndex, body,
                                            collider ) )
        {
            return false;
        }

        colliders[static_cast<std::size_t>( i )] = collider;
        origin += PhysicsBodyPosition( hotFields, static_cast<std::size_t>( modelIndex ) );
    }

    origin /= static_cast<float>( count );

    float radius = 1.0f;

    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = *colliders[static_cast<std::size_t>( i )];
        const std::size_t bodyIndex = static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] );
        radius = (std::max)( radius, Distance( PhysicsBodyPosition( hotFields, bodyIndex ), origin ) +
                                         EditorColliderRadius( collider ) );
    }

    outOrigin = origin;
    outRadius = radius;
    return true;
}


bool TryTraceEditorSelectionOverlayFromStores( const SceneWorld& world, PhysicsBodyHandle selectedBodyHandle,
                                               PhysicsColliderHandle selectedColliderHandle, int selectedIndex,
                                               EditorTracer& tracer, Vector3& outOrigin, float& outRadius )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();
    EditorGizmoGroupIndices indices = {};

    const int count = GatherSelectedEditorTransformGroup( world, selectedIndex, indices );

    if ( count <= 0 )
    {
        return false;
    }

    // Invariant: overlay tracing uses bounded pointer scratch only. Do not copy
    // CollisionShape values or allocate per selected body just to draw lines.
    std::array<const ColliderRecord*, RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY> colliders = {};
    const auto hotFields = bodyStore.HotFields();
    Vector3 origin = SkullbonezCore::Math::Vector::ZERO_VECTOR;

    for ( int i = 0; i < count; ++i )
    {
        const int modelIndex = indices[static_cast<std::size_t>( i )];
        const PhysicsBodyRecord* body = nullptr;
        const ColliderRecord* collider = nullptr;
        const bool selectedMember = modelIndex == selectedIndex;
        const PhysicsBodyHandle bodyHandle = selectedMember ? selectedBodyHandle
                                                            : bodyStore.HandleForModelIndex( modelIndex );

        const PhysicsColliderHandle colliderHandle = selectedMember ? selectedColliderHandle
                                                                    : colliderStore.HandleForBodyHandle( bodyHandle );

        if ( !TryResolveEditorBodyCollider( bodyStore, colliderStore, bodyHandle, colliderHandle, modelIndex, body,
                                            collider ) )
        {
            return false;
        }

        colliders[static_cast<std::size_t>( i )] = collider;
        origin += PhysicsBodyPosition( hotFields, static_cast<std::size_t>( modelIndex ) );
    }

    origin /= static_cast<float>( count );

    // Why: selection grouping still uses model-owned editor identity, but the
    // visible overlay follows the live body/collider rows so legacy object-record
    // pose/shape caches are not required for presentation.
    float radius = 1.0f;

    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = *colliders[static_cast<std::size_t>( i )];
        const std::size_t bodyIndex = static_cast<std::size_t>( indices[static_cast<std::size_t>( i )] );
        const Vector3 position = PhysicsBodyPosition( hotFields, bodyIndex );
        radius = (std::max)( radius, Distance( position, origin ) + EditorColliderRadius( collider ) );
        tracer.AddSelectionOutline( position, PhysicsBodyOrientation( hotFields, bodyIndex ), collider.shape );
    }

    outOrigin = origin;
    outRadius = radius;
    return true;
}


void CaptureEditorGizmoDragGroupState( RunEditorPlacementState& editor, const SceneWorld& world, bool allowRagdollGroup )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();

    // Lifetime: Drag state stores model indices plus store-sourced starting
    // transforms for the current gesture only. A changed model/body topology
    // invalidates the group before movement, scale, or rotation applies.
    editor.gizmoDragGroupCount = 0;
    editor.gizmoDragGroupIndices.fill( -1 );
    const int selectedModelIndex = ResolveSelectedEditorModelIndex( editor, bodyStore );

    if ( selectedModelIndex < 0 || selectedModelIndex >= world.SceneEntityCount() )
    {
        return;
    }

    EditorGizmoGroupIndices indices = {};
    int count = 1;
    indices[0] = selectedModelIndex;

    if ( allowRagdollGroup )
    {
        count = GatherSelectedEditorTransformGroup( world, selectedModelIndex, indices );
    }

    editor.gizmoDragGroupCount = std::clamp( count, 0,
                                             static_cast<int>( RunEditorPlacementState::GIZMO_DRAG_GROUP_CAPACITY ) );

    for ( int i = 0; i < editor.gizmoDragGroupCount; ++i )
    {
        const int index = indices[static_cast<std::size_t>( i )];
        editor.gizmoDragGroupIndices[static_cast<std::size_t>( i )] = index;
        const PhysicsBodyHandle bodyHandle = index == selectedModelIndex ? editor.selectedBody
                                                                         : bodyStore.HandleForModelIndex( index );

        const PhysicsBodyRecord* body = TryResolveEditorBodyRecord( bodyStore, bodyHandle, index );

        if ( !body )
        {
            editor.gizmoDragGroupCount = 0;
            editor.gizmoDragGroupIndices.fill( -1 );
            return;
        }

        const std::size_t bodyIndex = static_cast<std::size_t>( index );
        const auto hotFields = bodyStore.HotFields();
        editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( i )] = PhysicsBodyPosition( hotFields, bodyIndex );

        editor.gizmoDragGroupStartOrientations[static_cast<std::size_t>( i )] = PhysicsBodyOrientation( hotFields,
                                                                                                        bodyIndex );
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


// Why: editor/runtime tools still speak model indices for selection and replay
// gesture identity, but command mutation can enter PhysicsEngine through the
// current body-store row once topology drift is repaired at this boundary.
void WakeEditorPhysicsBody( SceneWorld& world, int modelIndex )
{
    PhysicsEngine& physics = world.Physics();
    const int modelCount = world.SceneEntityCount();

    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return;
    }

    if ( !world.RepairPhysicsBodyAndColliderTopology() )
    {
        return;
    }

    const PhysicsBodyHandle body = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).HandleForModelIndex( modelIndex );

    if ( !body.IsValid() )
    {
        return;
    }

    physics.WakeBody( body );
}


void SeedEditorPhysicsBodyAsleep( SceneWorld& world, int modelIndex )
{
    PhysicsEngine& physics = world.Physics();
    const int modelCount = world.SceneEntityCount();

    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return;
    }

    if ( !world.RepairPhysicsBodyAndColliderTopology() )
    {
        return;
    }

    const PhysicsBodyHandle body = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).HandleForModelIndex( modelIndex );

    if ( !body.IsValid() )
    {
        return;
    }

    physics.SeedBodyAsleep( body );
}


bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, PhysicsBodyUpdateDesc update )
{
    PhysicsEngine& physics = world.Physics();

    // Why: Direct editor transforms teleport the body. Clearing velocities and
    // waking dynamic bodies prevents stale solver momentum from immediately
    // dragging the authored pose away.
    const PhysicsBodyHandle bodyHandle = world.BodyStore().HandleForModelIndex( index );
    update.body = bodyHandle;
    update.updateMask |= PHYSICS_BODY_UPDATE_VELOCITY;
    update.linearVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    update.angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;

    if ( !physics.UpdateAuthoredBody( update ) )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );

    // Why: the explicit edit just refreshed the physics row; wake eligibility
    // should now follow PhysicsBodyStore, not legacy model-side body state.

    if ( body && bodyStore.HotFields().fixed[static_cast<std::size_t>( index )] == 0u )
    {
        WakeEditorPhysicsBody( world, index );
    }

    return true;
}


bool ResetEditorModelMotionAndWake( SceneWorld& world, int index, PhysicsBodyUpdateDesc update,
                                    PhysicsColliderCreateDesc colliderDesc )
{
    PhysicsEngine& physics = world.Physics();

    // Why: scale edits change the authored descriptor sidecar and the physics
    // collider row. Commit them together so wake decisions, picks, and replay
    // recording all see one coherent body/collider state.
    const PhysicsBodyHandle bodyHandle = world.BodyStore().HandleForModelIndex( index );
    update.body = bodyHandle;
    update.updateMask |= PHYSICS_BODY_UPDATE_VELOCITY;
    update.linearVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    update.angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;

    if ( !physics.UpdateAuthoredBodyAndCollider( update, std::move( colliderDesc ) ) )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );

    if ( body && bodyStore.HotFields().fixed[static_cast<std::size_t>( index )] == 0u )
    {
        WakeEditorPhysicsBody( world, index );
    }

    return true;
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


float DistanceRayToSegmentSquared( const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = Dot( segment, segment );

    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, Dot( toPoint, rayDirection ) );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = Dot( rayDirection, rayDirection );
    const float b = Dot( rayDirection, segment );
    const float c = segmentLenSq;
    const float d = Dot( rayDirection, w0 );
    const float e = Dot( segment, w0 );
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


} // namespace Runtime
} // namespace SkullbonezCore

namespace
{
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 64;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 12;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;

} // namespace

namespace SkullbonezCore
{
namespace Runtime
{
bool BeginEditorGizmoDragGesture( SceneWorld& world, RuntimeInteractionController& interaction, int modelIndex, int axis,
                                  RuntimeGizmoDragKind gizmoKind, int clientX, int clientY )
{

    if ( interaction.PointerCapture() != RuntimePointerCaptureOwner::None ||
         interaction.Gesture().kind != RuntimeInteractionGestureKind::None )
    {
        return false;
    }

    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = clientX;
    gesture.startY = clientY;
    gesture.body = world.BodyStore().HandleForModelIndex( modelIndex );
    gesture.axis = axis;
    gesture.angular = gizmoKind == RuntimeGizmoDragKind::Rotate;
    gesture.gizmoKind = gizmoKind;

    RuntimeGestureCommand command;
    command.gesture = gesture;
    RuntimeGestureEvent event;
    return interaction.ApplyGestureCommand( command, event );
}


void EndEditorGizmoDragGesture( RuntimeInteractionController& interaction )
{

    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
    }
}


void EndEditorPlacementScaleGesture( RuntimeInteractionController& interaction )
{

    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::EditorPlacementScaleDrag )
    {
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
    }
}


void CancelEditorGizmoDragState( RunEditorPlacementState& editor, RuntimeInteractionController& interaction )
{
    EndEditorGizmoDragGesture( interaction );
    editor.gizmoDragPlaneNormal = Math::Vector::ZERO_VECTOR;
    editor.gizmoDragGroupCount = 0;
}


int ResolveSelectedEditorModelIndex( RunEditorPlacementState& editor, const PhysicsBodyStore& bodyStore )
{

    // Concept: editor selection identity is the body/collider handle pair.
    // The row cache is only a UI/editor grouping hint and is repaired from the
    // live body store before any caller treats it as a model row.

    if ( !editor.selectedBody.IsValid() )
    {
        editor.selectedModelRow.value = -1;
        return -1;
    }

    return bodyStore.ResolveModelRow( editor.selectedBody, editor.selectedModelRow );
}


int PeekSelectedEditorModelIndex( const RunEditorPlacementState& editor, const PhysicsBodyStore& bodyStore )
{
    Physics::ModelRowHint hint = editor.selectedModelRow;
    return bodyStore.ResolveModelRow( editor.selectedBody, hint );
}
} // namespace Runtime
} // namespace SkullbonezCore


EditorViewportPlacementResult RuntimeTools::RouteEditorViewportPlacement( const EditorViewportPlacementInput& input )
{
    EditorViewportPlacementResult result;
    const bool placementScaleActive = input.gesture == RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    const bool editorViewportLookNow = m_editor.editorModeEnabled && input.rightDown && !input.blocksCameraMouse;

    if ( editorViewportLookNow != m_editor.viewportLookActive )
    {
        result.resetMouseLook = true;
    }

    m_editor.viewportLookActive = editorViewportLookNow;

    if ( editorViewportLookNow != input.inputModeIsViewportLook )
    {
        result.modeAction = editorViewportLookNow ? EditorViewportModeAction::Begin : EditorViewportModeAction::End;
    }

    const int placementWheelSteps = EditorMouseWheelSteps( input.unhandledWheelDelta );
    const bool placementYawWheel = placementWheelSteps != 0 && m_editor.editorModeEnabled && m_editor.placementModeEnabled &&
                                   input.controlDown && !m_editor.viewportLookActive && !input.blocksCameraMouse;

    if ( placementYawWheel )
    {
        result.enteredInteractiveScene = true;
        m_editor.placementYawRadians = WrapEditorAngleDelta( m_editor.placementYawRadians + static_cast<float>( placementWheelSteps ) * EDITOR_PLACEMENT_YAW_STEP_RADIANS );
    }

    if ( placementScaleActive && input.leftDown && !m_editor.viewportLookActive && !input.blocksCameraMouse )
    {

        if ( placementWheelSteps != 0 && !placementYawWheel )
        {
            result.enteredInteractiveScene = true;
            m_editor.placementScaleWheelSteps += placementWheelSteps;
        }

        if ( input.hasClientPosition )
        {
            const float dragPixelsX = static_cast<float>( input.clientX - m_editor.placementScaleStartClient.x );
            const float dragPixelsY = static_cast<float>( input.clientY - m_editor.placementScaleStartClient.y );
            m_editor.placementScale = EditorPlacementScaleFromGesture( m_editor.objectType, m_editor.placementScaleStart,
                                                                       dragPixelsX, dragPixelsY,
                                                                       m_editor.placementScaleWheelSteps );
        }
    }
    else if ( placementWheelSteps != 0 && m_editor.editorModeEnabled && m_editor.placementModeEnabled &&
              !placementYawWheel && !m_editor.viewportLookActive && !input.blocksCameraMouse )
    {
        const int nextAltitudeSteps = (std::max)( 0, m_editor.placementAltitudeSteps + placementWheelSteps );

        if ( nextAltitudeSteps != m_editor.placementAltitudeSteps )
        {
            result.enteredInteractiveScene = true;
            m_editor.placementAltitudeSteps = nextAltitudeSteps;
        }
    }

    return result;
}


int RuntimeTools::RefreshEditorPointerPreview( const EditorPointerPreviewInput& input, SceneWorld& world,
                                               RuntimeInteractionController& interaction, const Assets::AssetSystem& assets )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const int selectedModelIndex = ResolveSelectedEditorModelIndex( m_editor, bodyStore );
    const EditorInteractionPreviewResult previewResult = UpdateEditorInteractionPreview( m_resultDiagnostics, m_editor,
                                                                                         world, interaction, assets,
                                                                                         { input.blocksCameraMouse,
                                                                                           input.inspectGizmoActive,
                                                                                           input.hasWorldRay,
                                                                                           input.rayOrigin,
                                                                                           input.rayDirection,
                                                                                           input.controlDown } );

    if ( previewResult.clearInvalidSelection )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.selectionScope = previewResult.inspectSelectionScope ? RuntimeInteractionSelectionScope::Inspect
                                                                     : RuntimeInteractionSelectionScope::Editor;

        command.claimSelectionOwner = false;
        ApplySelectionCommand( command, world );
        CancelEditorGizmoDragState( m_editor, interaction );
    }

    return selectedModelIndex;
}


bool RuntimeTools::PrepareEditorPointerSelection( const EditorPointerSelectionInput& input, const SceneWorld& world,
                                                  RuntimeInteractionSelectionPlan& outPlan, WorldInteractionOwner& outOwner,
                                                  InteractionExitReason& outReason )
{
    RuntimePickResult result;

    if ( input.hasWorldRay )
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::EditorSelection;
        request.bodyStore = &world.BodyStore();
        request.colliderStore = &world.Colliders();
        request.rayOrigin = input.rayOrigin;
        request.rayDirection = input.rayDirection;
        RuntimePickService::TryPickModel( request, result );
    }

    RuntimeInteractionCommand command;
    command.type = RuntimeInteractionCommandType::SetEditorSelection;
    command.body = result.body;
    command.collider = result.collider;
    command.selectionScope = input.inspectGizmoActive ? RuntimeInteractionSelectionScope::Inspect
                                                      : RuntimeInteractionSelectionScope::Editor;

    if ( !PrepareSelectionCommand( command, world, outPlan ) )
    {
        return false;
    }

    outOwner = result.modelRow.IsValid()
                   ? ( input.inspectGizmoActive ? WorldInteractionOwner::InspectGizmo : WorldInteractionOwner::EditorGizmo )
                   : WorldInteractionOwner::None;

    outReason = input.inspectGizmoActive ? InteractionExitReason::EnterInspect : InteractionExitReason::EnterEdit;
    return true;
}


EditorPlacementScalePointerResult
RuntimeTools::RouteEditorPlacementScalePointer( bool leftReleased, bool suppressWorldAction, SceneWorld& world,
                                                SceneSessionState& scene, Assets::AssetSystem& assets,
                                                int activeModelCapacity, RuntimeInteractionController& interaction )
{
    EditorPlacementScalePointerResult result;

    if ( interaction.Gesture().kind != RuntimeInteractionGestureKind::EditorPlacementScaleDrag )
    {
        return result;
    }

    result.consumed = true;

    if ( !leftReleased && !suppressWorldAction )
    {
        return result;
    }

    if ( leftReleased && !suppressWorldAction && m_editor.placementPreviewVisible )
    {
        EditorObjectPlacementRequest placementRequest { m_editor.objectType, m_editor.placeStaticObject,
                                                        m_editor.placementTerrainPoint };

        EditorObjectPlacementResult placementResult;

        if ( CanPlaceEditorObjectAtTerrainPoint( world, assets, activeModelCapacity, placementRequest ) )
        {
            result.enteredInteractiveScene = true;
            PlaceEditorObjectAtTerrainPoint( m_resultDiagnostics, m_editor, world, scene, assets, activeModelCapacity,
                                             placementRequest, placementResult );

            if ( placementResult.placed )
            {
                RecordEditorPlacementHistory( world, placementResult.modelCountBefore, placementResult.modelCountAfter );

                result.replayEvent = ReplayEventCommandOperations::BuildEditorPlace( placementResult.objectType,
                                                                                     placementResult.fixedObject,
                                                                                     placementResult.autoTerrainAlign,
                                                                                     placementResult.modelCountBefore,
                                                                                     placementResult.terrainPoint,
                                                                                     placementResult.placementScale,
                                                                                     placementResult.placementYawRadians );

                result.recordReplayEvent = true;

                RuntimeInteractionCommand command;
                command.type = RuntimeInteractionCommandType::SetEditorSelection;
                command.body = placementResult.placedBody;
                command.collider = placementResult.placedCollider;
                command.claimSelectionOwner = false;
                ApplySelectionCommand( command, world );
            }
        }
    }

    RuntimeGestureCommand endCommand;
    endCommand.action = RuntimeGestureCommandAction::End;
    endCommand.gesture.kind = RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    endCommand.reason = InteractionExitReason::EndGesture;
    RuntimeGestureEvent endEvent;
    (void)interaction.ApplyGestureCommand( endCommand, endEvent );
    m_editor.placementScaleWheelSteps = 0;
    result.endedGesture = true;
    return result;
}


EditorGizmoDragPointerResult RuntimeTools::RouteEditorGizmoDragPointer( const EditorGizmoDragPointerInput& input,
                                                                        SceneWorld& world,
                                                                        RuntimeInteractionController& interaction )
{
    EditorGizmoDragPointerResult result;
    const RuntimeInteractionGesture gesture = interaction.Gesture();

    if ( gesture.kind != RuntimeInteractionGestureKind::GizmoDrag )
    {
        return result;
    }

    result.consumed = true;

    if ( input.leftDown && !input.suppressWorldAction && input.hasWorldRay )
    {

        if ( gesture.gizmoKind == RuntimeGizmoDragKind::Scale )
        {
            ScaleSelectedEditorObjectAlongAxis( m_editor, world, interaction, input.rayOrigin, input.rayDirection );
        }
        else if ( gesture.gizmoKind == RuntimeGizmoDragKind::Rotate )
        {
            RotateSelectedEditorObjectAroundAxis( m_editor, world, interaction, input.rayOrigin, input.rayDirection );
        }
        else
        {
            MoveSelectedEditorObjectAlongAxis( m_editor, world, interaction, input.rayOrigin, input.rayDirection );
        }
    }

    if ( !input.leftReleased && !input.suppressWorldAction )
    {
        return result;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();

    if ( input.leftReleased && !input.suppressWorldAction && input.selectedModelIndex >= 0 &&
         input.selectedModelIndex < world.SceneEntityCount() )
    {

        if ( gesture.gizmoKind == RuntimeGizmoDragKind::Scale )
        {
            const int scaleAxis = gesture.axis;
            float scaleFactor = 1.0f;
            const PhysicsBodyRecord* selectedBody = nullptr;
            const ColliderRecord* selectedCollider = nullptr;

            if ( TryResolveEditorBodyCollider( bodyStore, colliderStore, m_editor.selectedBody, m_editor.selectedCollider,
                                               input.selectedModelIndex, selectedBody, selectedCollider ) )
            {
                const uint32_t changedFlags = TryEditorScaleFactorFromShapes( m_editor.gizmoDragStartShape,
                                                                              selectedCollider->shape, scaleAxis,
                                                                              scaleFactor )
                                                  ? REPLAY_EDITOR_TRANSFORM_SCALE
                                                  : 0u;

                RecordEditorTransformEventFromBodyStore( result.replayEvents, world, input.selectedModelIndex, changedFlags,
                                                         scaleAxis, scaleFactor );
            }
        }
        else
        {
            const int groupCount = ValidCapturedEditorGizmoGroupCount( m_editor, world.SceneEntityCount() );

            if ( groupCount > 0 )
            {

                for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
                {
                    const int modelIndex = m_editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
                    const PhysicsBodyRecord* groupBody = TryResolveEditorBodyRecord( bodyStore, modelIndex );

                    if ( !groupBody )
                    {
                        continue;
                    }

                    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
                    const auto hotFields = bodyStore.HotFields();
                    uint32_t changedFlags = 0;
                    changedFlags |= EditorPositionsDiffer( PhysicsBodyPosition( hotFields, bodyIndex ),
                                                           m_editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] )
                                        ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                        : 0u;

                    changedFlags |= EditorOrientationsDiffer( PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                              m_editor
                                                                  .gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )] )
                                        ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                        : 0u;

                    RecordEditorTransformEventFromBodyStore( result.replayEvents, world, modelIndex, changedFlags, -1,
                                                             1.0f );
                }
            }
            else
            {
                const PhysicsBodyRecord* selectedBody = TryResolveEditorBodyRecord( bodyStore, m_editor.selectedBody,
                                                                                    input.selectedModelIndex );

                if ( selectedBody )
                {
                    const std::size_t bodyIndex = static_cast<std::size_t>( input.selectedModelIndex );
                    const auto hotFields = bodyStore.HotFields();
                    uint32_t changedFlags = 0;
                    changedFlags |= EditorPositionsDiffer( PhysicsBodyPosition( hotFields, bodyIndex ),
                                                           m_editor.gizmoDragStartPosition )
                                        ? REPLAY_EDITOR_TRANSFORM_TRANSLATE
                                        : 0u;
                    changedFlags |= EditorOrientationsDiffer( PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                              m_editor.gizmoDragStartOrientation )
                                        ? REPLAY_EDITOR_TRANSFORM_ROTATE
                                        : 0u;
                    RecordEditorTransformEventFromBodyStore( result.replayEvents, world, input.selectedModelIndex,
                                                             changedFlags, -1, 1.0f );
                }
            }
        }
    }

    if ( input.leftReleased && !input.suppressWorldAction )
    {
        RecordEditorTransformHistory( world, gesture.gizmoKind, input.selectedModelIndex );
    }

    CancelEditorGizmoDragState( m_editor, interaction );
    result.endedGesture = true;
    return result;
}


bool RuntimeTools::PrepareEditorGizmoGesture( bool inspectGizmoActive, bool scaleMode, int selectedModelIndex,
                                              bool hasWorldRay, const Vector3& rayOrigin, const Vector3& rayDirection,
                                              int clientX, int clientY, SceneWorld& world,
                                              RuntimeInteractionController& interaction, EditorGizmoGesturePlan& outPlan )
{
    outPlan = EditorGizmoGesturePlan {};
    const bool transformActive = ( m_editor.editorModeEnabled || inspectGizmoActive ) && !m_editor.placementModeEnabled;
    const bool canCapture = interaction.PointerCapture() == RuntimePointerCaptureOwner::None &&
                            interaction.Gesture().kind == RuntimeInteractionGestureKind::None;

    if ( !transformActive || !canCapture || !hasWorldRay || selectedModelIndex < 0 )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();
    outPlan.owner = inspectGizmoActive ? WorldInteractionOwner::InspectGizmo : WorldInteractionOwner::EditorGizmo;
    outPlan.reason = inspectGizmoActive ? InteractionExitReason::EnterInspect : InteractionExitReason::EnterEdit;
    outPlan.selectedBody = m_editor.selectedBody;
    outPlan.clientX = clientX;
    outPlan.clientY = clientY;

    if ( scaleMode )
    {

        if ( selectedModelIndex >= world.SceneEntityCount() || m_editor.hotGizmoAxis < 0 )
        {
            return false;
        }

        const PhysicsBodyRecord* selectedBody = nullptr;
        const ColliderRecord* selectedCollider = nullptr;
        float axisParameter = 0.0f;

        if ( !TryEditorAxisRayParameter( m_editor, world, m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisParameter ) ||
             !TryResolveEditorBodyCollider( bodyStore, colliderStore, m_editor.selectedBody, m_editor.selectedCollider,
                                            selectedModelIndex, selectedBody, selectedCollider ) )
        {
            return false;
        }

        outPlan.kind = EditorGizmoGestureKind::Scale;
        outPlan.axis = m_editor.hotGizmoAxis;
        outPlan.axisParameter = axisParameter;
        outPlan.startShape = CopyCollisionShape( selectedCollider->shape );
        const std::size_t bodyIndex = static_cast<std::size_t>( selectedModelIndex );
        const auto hotFields = bodyStore.HotFields();
        outPlan.startPosition = PhysicsBodyPosition( hotFields, bodyIndex );
        outPlan.startOrientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        return true;
    }

    if ( m_editor.hotRotationAxis >= 0 )
    {
        float startAngle = 0.0f;
        const PhysicsBodyRecord* selectedBody = TryResolveEditorBodyRecord( bodyStore, m_editor.selectedBody,
                                                                            selectedModelIndex );

        Vector3 selectionOrigin;
        float selectionRadius = 1.0f;

        if ( TryEditorRotationRayAngle( m_editor, world, m_editor.hotRotationAxis, rayOrigin, rayDirection, startAngle ) &&
             selectedBody &&
             TryGetEditorSelectionFrame( world, m_editor.selectedBody, m_editor.selectedCollider, selectedModelIndex,
                                         selectionOrigin, selectionRadius ) )
        {
            outPlan.kind = EditorGizmoGestureKind::Rotate;
            outPlan.axis = m_editor.hotRotationAxis;
            outPlan.axisParameter = startAngle;
            outPlan.startPosition = selectionOrigin;
            outPlan.startOrientation = PhysicsBodyOrientation( bodyStore.HotFields(),
                                                               static_cast<std::size_t>( selectedModelIndex ) );

            return true;
        }
    }

    if ( m_editor.hotGizmoAxis < 0 )
    {
        return false;
    }

    const PhysicsBodyRecord* selectedBody = TryResolveEditorBodyRecord( bodyStore, m_editor.selectedBody,
                                                                        selectedModelIndex );

    Vector3 selectionOrigin;
    float selectionRadius = 1.0f;

    if ( !selectedBody || !TryGetEditorSelectionFrame( world, m_editor.selectedBody, m_editor.selectedCollider,
                                                       selectedModelIndex, selectionOrigin, selectionRadius ) )
    {
        return false;
    }

    const Vector3 planeNormal = EditorAxisDragPlaneNormal( m_editor.hotGizmoAxis, rayDirection );
    float axisParameter = 0.0f;

    if ( !TryEditorAxisPlaneRayParameter( m_editor.hotGizmoAxis, selectionOrigin, planeNormal, rayOrigin, rayDirection,
                                          axisParameter ) )
    {
        return false;
    }

    outPlan.kind = EditorGizmoGestureKind::Translate;
    outPlan.axis = m_editor.hotGizmoAxis;
    outPlan.axisParameter = axisParameter;
    outPlan.startPosition = selectionOrigin;
    outPlan.startOrientation = PhysicsBodyOrientation( bodyStore.HotFields(),
                                                       static_cast<std::size_t>( selectedModelIndex ) );

    outPlan.dragPlaneNormal = planeNormal;
    return true;
}


EditorGizmoGestureResult RuntimeTools::CommitEditorGizmoGesture( const EditorGizmoGesturePlan& plan, SceneWorld& world,
                                                                 RuntimeInteractionController& interaction )
{
    EditorGizmoGestureResult result;

    if ( plan.kind == EditorGizmoGestureKind::None )
    {
        return result;
    }

    result.attempted = true;
    const RuntimeGizmoDragKind gizmoKind = plan.kind == EditorGizmoGestureKind::Rotate  ? RuntimeGizmoDragKind::Rotate
                                           : plan.kind == EditorGizmoGestureKind::Scale ? RuntimeGizmoDragKind::Scale
                                                                                        : RuntimeGizmoDragKind::Translate;

    const int selectedModelIndex = world.BodyStore().ModelIndexForHandle( plan.selectedBody );

    if ( selectedModelIndex < 0 )
    {
        return result;
    }

    if ( !BeginEditorGizmoDragGesture( world, interaction, selectedModelIndex, plan.axis, gizmoKind, plan.clientX,
                                       plan.clientY ) )
    {
        return result;
    }

    m_editor.gizmoDragStartPosition = plan.startPosition;
    m_editor.gizmoDragStartOrientation = plan.startOrientation;

    if ( gizmoKind == RuntimeGizmoDragKind::Rotate )
    {
        m_editor.gizmoDragStartRotationAngle = plan.axisParameter;
    }
    else
    {
        m_editor.gizmoDragStartAxisT = plan.axisParameter;
    }

    if ( plan.kind == EditorGizmoGestureKind::Scale )
    {
        m_editor.gizmoDragStartShape = plan.startShape;
    }
    else if ( plan.kind == EditorGizmoGestureKind::Translate )
    {
        m_editor.gizmoDragPlaneNormal = plan.dragPlaneNormal;
    }

    CaptureEditorGizmoDragGroupState( m_editor, world, plan.kind != EditorGizmoGestureKind::Scale );
    result.consumed = true;
    result.kind = plan.kind;
    return result;
}


EditorPlacementScaleStartResult RuntimeTools::BeginEditorPlacementScalePointer( bool inspectGizmoActive,
                                                                                bool hasClientPosition, int clientX,
                                                                                int clientY,
                                                                                RuntimeInteractionController& interaction )
{
    EditorPlacementScaleStartResult result;

    if ( !( m_editor.editorModeEnabled || inspectGizmoActive ) || !m_editor.placementModeEnabled )
    {
        return result;
    }

    result.consumed = true;

    if ( !m_editor.placementPreviewVisible || !hasClientPosition )
    {
        return result;
    }

    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = clientX;
    gesture.startY = clientY;
    RuntimeGestureCommand command;
    command.gesture = gesture;
    RuntimeGestureEvent event;

    if ( !interaction.ApplyGestureCommand( command, event ) )
    {
        return result;
    }

    m_editor.placementScaleWheelSteps = 0;
    m_editor.placementScaleStart = EditorClampPlacementScale( m_editor.objectType, m_editor.placementScale );
    m_editor.placementScale = m_editor.placementScaleStart;
    m_editor.placementScaleStartClient = { clientX, clientY };
    m_editor.placementScaleTerrainPoint = m_editor.placementTerrainPoint;
    m_editor.placementScaleRayOrigin = m_editor.placementRayOrigin;
    result.beganGesture = true;
    return result;
}


EditorPointerRouteResult
InputRouter::RouteEditorPointer( const RuntimePointerEvent& pointer, bool hasWorldRay, const Vector3& rayOrigin,
                                 const Vector3& rayDirection, RunCameraMode cameraMode, bool replayInspectionActive,
                                 int activeModelCapacity, Assets::AssetSystem& assets, RuntimeTools& runtimeTools,
                                 RuntimeInteractionController& interaction, SceneController& models )
{
    SceneWorld& sceneWorld = models.Scene();
    SceneSessionState& scene = models.State();
    EditorPointerRouteResult routeResult;
    auto appendModeAction = [&routeResult]( RuntimeInputAction action )
    {

        if ( routeResult.modeActionCount >= routeResult.modeActions.size() )
        {
            SB_FATAL( "Runtime/InputRouter", "Editor pointer mode-action capacity exhausted." );
        }

        routeResult.modeActions[routeResult.modeActionCount++] = action;
    };

    const auto publishInteractionTransition = [&routeResult]( const RuntimeInteractionTransition& transition )
    {

        // Invariant: one pointer route can begin at most one editor claim. The
        // composition shell must clean it before routing another world owner.

        if ( routeResult.hasInteractionTransition )
        {
            SB_FATAL( "Runtime/InputRouter", "Editor pointer emitted multiple interaction transitions." );
        }

        routeResult.interactionTransition = transition;
        routeResult.hasInteractionTransition = true;
    };

    const PhysicsBodyStore& editorBodyStore = sceneWorld.BodyStore();

    // Invariant: preview refresh precedes active placement/gizmo teardown;
    // only an unconsumed press may begin a gizmo or scale gesture, and
    // selection is the final editor fallback.
    const bool previewInspectGizmoActive = runtimeTools.InspectGizmoInteractionActive( cameraMode, replayInspectionActive );

    const bool previewCanUseMouseRay = !pointer.uiBlocksCameraMouse && !runtimeTools.Editor().viewportLookActive &&
                                       ( runtimeTools.Editor().editorModeEnabled || previewInspectGizmoActive );

    const bool previewNeedsMouseRay = previewCanUseMouseRay &&
                                      ( ( runtimeTools.Editor().editorModeEnabled &&
                                          runtimeTools.Editor().placementModeEnabled &&
                                          interaction.Gesture().kind !=
                                              RuntimeInteractionGestureKind::EditorPlacementScaleDrag ) ||
                                        ( ResolveSelectedEditorModelIndex( runtimeTools.Editor(), editorBodyStore ) >= 0 &&
                                          interaction.Gesture().kind != RuntimeInteractionGestureKind::GizmoDrag &&
                                          !runtimeTools.Editor().placementModeEnabled ) );

    const bool hasPreviewMouseRay = previewNeedsMouseRay && hasWorldRay;
    const int selectedModelIndex = runtimeTools.RefreshEditorPointerPreview( { pointer.uiBlocksCameraMouse,
                                                                               previewInspectGizmoActive, hasPreviewMouseRay,
                                                                               pointer.controlDown, rayOrigin,
                                                                               rayDirection },
                                                                             sceneWorld, interaction, assets );

    const bool leftMouseNow = pointer.leftDown;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    bool consumedWorldClick = false;

    const EditorPlacementScalePointerResult
        placementScaleResult = runtimeTools.RouteEditorPlacementScalePointer( leftReleased, pointer.suppressWorldAction,
                                                                              sceneWorld, scene, assets, activeModelCapacity,
                                                                              interaction );

    if ( placementScaleResult.recordReplayEvent && !routeResult.replayEvents.Append( placementScaleResult.replayEvent ) )
    {
        SB_FATAL( "Runtime/InputRouter", "Replay editor-event batch capacity exhausted." );
    }

    if ( placementScaleResult.enteredInteractiveScene )
    {
        routeResult.enteredInteractiveScene = true;
    }

    if ( placementScaleResult.endedGesture )
    {
        ReleaseNativeCapture();
        appendModeAction( RuntimeInputAction::EndEditorPlacementScale );
    }

    consumedWorldClick = placementScaleResult.consumed;

    Vector3 dragRayOrigin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 dragRayDirection = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const bool hasDragWorldRay = interaction.Gesture().kind == RuntimeInteractionGestureKind::GizmoDrag && leftMouseNow &&
                                 !pointer.suppressWorldAction && hasWorldRay;

    dragRayOrigin = rayOrigin;
    dragRayDirection = rayDirection;
    const EditorGizmoDragPointerResult
        gizmoDragResult = runtimeTools.RouteEditorGizmoDragPointer( { leftMouseNow, leftReleased,
                                                                      pointer.suppressWorldAction, hasDragWorldRay,
                                                                      selectedModelIndex, dragRayOrigin, dragRayDirection },
                                                                    sceneWorld, interaction );

    for ( std::size_t replayEventIndex = 0; replayEventIndex < gizmoDragResult.replayEvents.count; ++replayEventIndex )
    {

        if ( !routeResult.replayEvents.Append( gizmoDragResult.replayEvents.commands[replayEventIndex] ) )
        {
            SB_FATAL( "Runtime/InputRouter", "Replay editor-event batch capacity exhausted." );
        }
    }

    if ( gizmoDragResult.endedGesture )
    {
        ReleaseNativeCapture();
        appendModeAction( RuntimeInputAction::EndEditorGizmoDrag );
    }

    consumedWorldClick = consumedWorldClick || gizmoDragResult.consumed;

    if ( !consumedWorldClick && leftPressed && !pointer.suppressWorldAction )
    {
        const bool inspectGizmoActive = runtimeTools.InspectGizmoInteractionActive( cameraMode, replayInspectionActive );

        EditorGizmoGesturePlan gesturePlan;

        if ( runtimeTools.PrepareEditorGizmoGesture( inspectGizmoActive, pointer.controlDown, selectedModelIndex,
                                                     hasWorldRay, rayOrigin, rayDirection, pointer.clientX, pointer.clientY,
                                                     sceneWorld, interaction, gesturePlan ) )
        {
            routeResult.enteredInteractiveScene = true;
            publishInteractionTransition( interaction.SetWorldInteractionOwnerInWorkspace( interaction.WorkspaceForOwner( gesturePlan.owner ),
                                                                                           gesturePlan.owner, gesturePlan.reason ) );

            const EditorGizmoGestureResult gestureResult = runtimeTools.CommitEditorGizmoGesture( gesturePlan, sceneWorld,
                                                                                                  interaction );

            if ( gestureResult.attempted && !gestureResult.consumed )
            {
                routeResult.consumed = consumedWorldClick;
                return routeResult;
            }

            consumedWorldClick = gestureResult.consumed;

            if ( gestureResult.consumed )
            {
                RequestNativeCapture();
            }

            if ( gestureResult.kind == EditorGizmoGestureKind::Scale )
            {
                appendModeAction( RuntimeInputAction::BeginEditorGizmoScale );
            }
            else if ( gestureResult.kind == EditorGizmoGestureKind::Rotate )
            {
                appendModeAction( RuntimeInputAction::BeginEditorGizmoRotate );
            }
            else if ( gestureResult.kind == EditorGizmoGestureKind::Translate )
            {
                appendModeAction( RuntimeInputAction::BeginEditorGizmoTranslate );
            }
        }

        if ( !consumedWorldClick && ( runtimeTools.Editor().editorModeEnabled || inspectGizmoActive ) )
        {
            const EditorPlacementScaleStartResult
                placementStart = runtimeTools.BeginEditorPlacementScalePointer( inspectGizmoActive,
                                                                                pointer.hasClientPosition, pointer.clientX,
                                                                                pointer.clientY, interaction );

            consumedWorldClick = placementStart.consumed;

            if ( placementStart.beganGesture )
            {
                RequestNativeCapture();
                appendModeAction( RuntimeInputAction::BeginEditorPlacementScale );
            }

            if ( !consumedWorldClick )
            {
                RuntimeInteractionSelectionPlan plan;
                WorldInteractionOwner selectionOwner = WorldInteractionOwner::None;
                InteractionExitReason selectionReason = InteractionExitReason::EnterEdit;

                if ( runtimeTools.PrepareEditorPointerSelection( { inspectGizmoActive, hasWorldRay, rayOrigin,
                                                                   rayDirection },
                                                                 sceneWorld, plan, selectionOwner, selectionReason ) )
                {
                    publishInteractionTransition( interaction.SetWorldInteractionOwnerInWorkspace( interaction.WorkspaceForOwner( selectionOwner ),
                                                                                                   selectionOwner, selectionReason ) );

                    RuntimeInteractionEvent event;
                    consumedWorldClick = runtimeTools.CommitSelectionCommand( plan, event );
                }
            }
        }
    }

    routeResult.consumed = consumedWorldClick;
    return routeResult;
}


bool InputRouter::TryBuildWorldRay( const Environment::CameraCollection& cameras, const Window& window, Vector3& outOrigin,
                                    Vector3& outDirection, bool clampToViewport ) const
{
    const DeviceInputFrame& deviceFrame = DeviceFrame();

    if ( !deviceFrame.hasClientPosition )
    {
        return false;
    }

    return TryBuildWorldRayAt( POINT { deviceFrame.clientX, deviceFrame.clientY }, cameras, window, outOrigin, outDirection,
                               clampToViewport );
}


bool InputRouter::TryBuildWorldRayAt( POINT mouse, const Environment::CameraCollection& cameras, const Window& window,
                                      Vector3& outOrigin, Vector3& outDirection, bool clampToViewport ) const
{
    const int screenW = (std::max)( 1, window.ClientWidth() );
    const int screenH = (std::max)( 1, window.ClientHeight() );

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

    const Vector3 eye = cameras.GetCameraTranslation();
    const Vector3 view = cameras.GetCameraView();
    const Vector3 up = cameras.GetCameraUp();
    const Matrix4 viewMatrix = Matrix4::LookAt( eye, view, up );
    const Matrix4 inverseViewProjection = ( window.GetProjectionMatrix() * viewMatrix ).Inverse();

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
namespace Runtime
{
bool TryGetEditorTerrainPlacement( Geometry::Terrain* terrain, const Vector3& rayOrigin, const Vector3& rayDirection,
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


bool TryComputeEditorObjectCenter( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int objectType,
                                   const Vector3& terrainPoint, const Vector3& placementScale, const Quaternion& orientation,
                                   const Assets::AssetSystem& assets, Vector3& outCenter )
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
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + scale.x + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );

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

        if ( !tree || !TryComputeEditorTreeWorldBounds( diagnostics, *tree, terrainPoint, rotation, minV, maxV ) )
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

        if ( !TryComputeEditorBuildingWorldBounds( diagnostics, type, terrainPoint, orientation, assets, minV, maxV ) )
        {
            return false;
        }

        outCenter = ( minV + maxV ) * 0.5f;
        return true;
    }
    default:
    {
        ConvexHullShape hull;

        if ( !TryBuildScaledEditorHullForType( diagnostics, type, scale, hull ) )
        {
            return false;
        }

        const Vector3 authoredOrigin = terrainPoint + rotation * Vector3( 0.0f,
                                                                          HullAuthoredBottomOffset( hull ) +
                                                                              EDITOR_PLACEMENT_SURFACE_EPSILON,
                                                                          0.0f );

        outCenter = authoredOrigin + rotation * hull.GetAuthoredCenterOfMass();
        return true;
    }
    }
}


bool TryUpdateEditorPlacementPreview( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, RunEditorPlacementState& editor,
                                      Geometry::Terrain* terrain, const Assets::AssetSystem& assets, bool scaleGestureActive,
                                      int objectType, const EditorTerrainPlacement* mousePlacement )
{
    Vector3 terrainPoint;
    Vector3 rayOrigin;
    bool terrainAlreadyIncludesAltitude = false;

    if ( scaleGestureActive )
    {
        terrainPoint = editor.placementScaleTerrainPoint;
        rayOrigin = editor.placementScaleRayOrigin;
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

        if ( terrain && EDITOR_PLACEMENT_SNAP > 0.0f )
        {
            const float snappedX = roundf( terrainPoint.x / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
            const float snappedZ = roundf( terrainPoint.z / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;

            if ( terrain->IsInBounds( snappedX, snappedZ ) )
            {
                terrainPoint.x = snappedX;
                terrainPoint.z = snappedZ;
                terrainPoint.y = terrain->GetTerrainHeightAt( snappedX, snappedZ );
            }
        }
    }

    if ( !terrainAlreadyIncludesAltitude )
    {

        // Invariant: Placement altitude is applied before normal/orientation
        // lookup so preview and commit agree about the authored terrain point.
        terrainPoint.y += static_cast<float>( editor.placementAltitudeSteps ) *
                          EditorPlacementAltitudeStepSize( diagnostics, objectType, editor.placementScale, assets );
    }

    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );

    if ( terrain && terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }

    const Quaternion placementOrientation = EditorPlacementOrientation( objectType, terrainNormal, editor.autoTerrainAlign,
                                                                        editor.placementYawRadians );

    Vector3 center;

    if ( !TryComputeEditorObjectCenter( diagnostics, objectType, terrainPoint, editor.placementScale, placementOrientation,
                                        assets, center ) )
    {
        return false;
    }

    editor.placementTerrainPoint = terrainPoint;
    editor.placementCenter = center;
    editor.placementOrientation = placementOrientation;
    editor.placementRayOrigin = rayOrigin;
    editor.placementRayHit = terrainPoint;
    return true;
}
} // namespace Runtime
} // namespace SkullbonezCore

/*
File: SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp
Purpose:
  Contains editor object placement preflight and commit logic.

Summary:
  Placement is the ownership boundary between UI-selected asset recipes and live
  scene model creation. This slice validates the terrain point, computes object
  transforms, and adds the requested body or compound asset to the model store.

Glossary:
  Preflight: Placement validation that checks object type, terrain point, and
    asset recipe availability without mutating the scene.
  Placement request: User-selected object type, static/dynamic mode, and target
    terrain point.

Invariants:
  - Preflight and commit must use matching geometry decisions.
  - Placement names and model order are replay-visible and must stay stable.
  - Hull sharing is published only for geometry copied from a known base asset
    and scaled once in canonical X/Y/Z order.

Related:
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "EditorPlacementAssets.h"
#include "../../Assets/AssetKeys.h"
#include "EditorTools.h"
#include "../../Assets/EditorHullAssets.h"
#include "../Scene/SceneControllerState.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneAuthoredSetup.h"
#include "../Scene/SceneController.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../Interaction/OperatorEditorObjectCatalog.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::Math::Vector::Vector3;
using Json = SkullbonezCore::Runtime::EditorPlacementJson;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr float EDITOR_TEXTURE_MODE_INVERTED = -2.0f;

void ApplyEditorSpawnMaterial( SceneEntityCreateDesc& model, bool fixedObject, bool boxObject )
{
    // Concept: editor-spawn material encodes placement mode before asset
    // recipes override it. Fixed bodies stay neutral, dynamic boxes keep the
    // legacy inverted-texture marker, and dynamic hulls use the blue editor tint.
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
} // namespace

PhysicsColliderCreateDesc MakeEditorColliderDesc( CollisionShape shape, float restitution, HullShapeIdentity hullIdentity = {} )
{
    // Why: placement commit already owns the primitive geometry selected by the
    // editor. Pass that value into physics at append time so the collider store
    // receives exact shape facts without a legacy object record readback.
    return MakeColliderCreateDesc( std::move( shape ), restitution, HashStr( "default" ), nullptr, std::move( hullIdentity ) );
}


PhysicsBodyCreateDesc MakeEditorBodyDesc( const CollisionShape& shape,
                                          const Vector3& position,
                                          const Quaternion& orientation,
                                          const Vector3& linearVelocity,
                                          const Vector3& angularVelocity,
                                          const Vector3& rotationalInertia,
                                          float mass,
                                          float restitution )
{
    return MakePhysicsBodyCreateDesc( PhysicsSceneObjectId {}, shape, position, orientation, linearVelocity, angularVelocity, rotationalInertia, mass, restitution, PhysicsBodyMotionKind::Dynamic );
}


static bool
TryResolveEditorObjectPlacementPreflight( SceneWorld& world, const Assets::AssetSystem& assets, int activeModelCapacity, EditorObjectPlacementRequest request, int& outType, bool reportErrors )
{
    // Invariant: This preflight is the single capacity and asset-count gate
    // for both CanPlace and Place. Add new multi-part object families here
    // before adding their placement branch below.
    const int modelCount = world.SceneEntityCount();
    const int type = std::clamp( request.objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const int buildingPartCount = building ? EditorBuildingPartCount( type, assets ) : 0;
    const bool isRagdollType = UI::EditorTab::IsRagdollObjectType( type );

    if ( building && buildingPartCount <= 0 )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s is missing or empty.\n", building->assetName );
        }

        return false;
    }

    const int requiredModelCount = isRagdollType ? Ragdoll::SIMPLE_PART_COUNT : ( building ? buildingPartCount : ( house ? house->partCount : ( tree ? tree->partCount : 1 ) ) );

    if ( modelCount + requiredModelCount > activeModelCapacity )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place object: model capacity reached.\n" );
        }

        return false;
    }

    outType = type;
    return true;
}


bool CanPlaceEditorObjectAtTerrainPoint( SceneWorld& world, const Assets::AssetSystem& assets, int activeModelCapacity, EditorObjectPlacementRequest request )
{
    int type = 0;
    return TryResolveEditorObjectPlacementPreflight( world, assets, activeModelCapacity, request, type, true );
}


namespace
{
// Invariant: a placement batch keeps one effective transform and scene-id source
// for all of its parts, and reports success only after its append operations finish.
// Lifetime: the owner is synchronous; its scene, recipe and diagnostic borrows
// expire when PlaceEditorObjectAtTerrainPoint returns. Native placement tests
// bind the reported object type to all ten ragdoll bodies and their saved joints.
class EditorObjectPlacementBatch
{
  public:
    EditorObjectPlacementBatch( Core::SbDiagnosticStore& diagnostics,
                                RunEditorPlacementState& editor,
                                SceneWorld& world,
                                SceneSessionState& scene,
                                const Assets::AssetSystem& assets,
                                int type,
                                EditorObjectPlacementRequest request )
        : m_diagnostics( diagnostics ), m_world( world ), m_scene( scene ), m_assets( assets ), m_type( type ), m_autoTerrainAlign( editor.autoTerrainAlign ),
          m_placementYawRadians( editor.placementYawRadians )
    {
        m_modelCount = m_world.SceneEntityCount();
        m_tree = EditorTreeDefinitionForType( m_type );
        m_house = EditorHouseDefinitionForType( m_type );
        m_building = EditorBuildingDefinitionForType( m_type );
        m_terrainPoint = request.terrainPoint;
        m_fixedObject = request.fixedObject;
        m_placementScale = EditorClampPlacementScale( m_type, editor.placementScale );
        m_serial = editor.placedObjectSerial++;
        Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
        Geometry::Terrain* terrain = m_world.Terrain().Get();

        if ( terrain && terrain->IsInBounds( m_terrainPoint.x, m_terrainPoint.z ) )
        {
            float ignoredHeight = 0.0f;
            terrain->GetTerrainHeightAndNormalAt( m_terrainPoint.x, m_terrainPoint.z, ignoredHeight, terrainNormal );
        }

        m_alignToTerrain = EditorObjectAlignsToTerrainNormal( m_type, editor.autoTerrainAlign );
        m_placementOrientation = EditorPlacementOrientation( m_type, terrainNormal, editor.autoTerrainAlign, editor.placementYawRadians );

        Quaternion placementOrientationCopy = m_placementOrientation;
        m_placementRotation = placementOrientationCopy.GetOrientationMatrix();
        m_placementFixed = m_tree && m_tree->forceFixed ? true : m_fixedObject;
        m_ragdollStartsAsleep = UI::EditorTab::IsRagdollObjectType( m_type ) && m_type != UI::EditorTab::OBJECT_RAGDOLL;
        m_modePrefix = m_placementFixed ? "static" : ( ( m_tree && m_tree->seedAsleep ) || ( m_house && m_house->seedAsleep ) || m_building || m_ragdollStartsAsleep ? "sleeping" : "dynamic" );
    }

    bool Execute( EditorObjectPlacementResult& outResult )
    {
        switch ( m_type )
        {
        case UI::EditorTab::OBJECT_BOX:
            AddBox();
            break;
        case UI::EditorTab::OBJECT_BALL:
            AddSphere( "ball", m_placementScale.x, 0.45f );
            break;
        case UI::EditorTab::OBJECT_SPHERE:
            AddSphere( "sphere", m_placementScale.x, 0.35f );
            break;
        case UI::EditorTab::OBJECT_HULL_WEDGE:
            AddHull( EditorHullAsset::WEDGE );
            break;
        case UI::EditorTab::OBJECT_HULL_TRI_PRISM:
            AddHull( EditorHullAsset::TRI_PRISM );
            break;
        case UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
            AddHull( EditorHullAsset::TAPERED_BLOCK );
            break;
        case UI::EditorTab::OBJECT_HULL_PYRAMID:
            AddHull( EditorHullAsset::PYRAMID );
            break;
        case UI::EditorTab::OBJECT_HULL_HEX_PRISM:
            AddHull( EditorHullAsset::HEX_PRISM );
            break;
        case UI::EditorTab::OBJECT_HULL_DIAMOND:
            AddHull( EditorHullAsset::DIAMOND );
            break;
        case UI::EditorTab::OBJECT_ROCK_SLAB:
            AddHull( EditorHullAsset::ROCK_SLAB_FLAT );
            break;
        case UI::EditorTab::OBJECT_ROCK_LUMP:
            AddHull( EditorHullAsset::ROCK_LUMP_LARGE );
            break;
        case UI::EditorTab::OBJECT_ROCK_SHARD:
            AddHull( EditorHullAsset::ROCK_SHARD_TALL );
            break;
        case UI::EditorTab::OBJECT_ROCK_CHIPPED:
            AddHull( EditorHullAsset::ROCK_CHIPPED_BLOCK );
            break;
        case UI::EditorTab::OBJECT_ROOT_SMALL:
            AddHull( EditorHullAsset::TREE_ROOT_SMALL );
            break;
        case UI::EditorTab::OBJECT_ROOT_LARGE:
            AddHull( EditorHullAsset::TREE_ROOT_LARGE );
            break;
        case UI::EditorTab::OBJECT_TREE_SMALL:

            if ( m_tree )
            {
                AddTree( *m_tree );
            }

            break;
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

            if ( m_tree )
            {
                AddTree( *m_tree );
            }

            break;
        case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
        case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
        case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
        case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
        case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
        case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
        case UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP:

            if ( m_building )
            {
                AddBuilding( *m_building );
            }

            break;
        case UI::EditorTab::OBJECT_RAGDOLL:
        case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        case UI::EditorTab::OBJECT_RAGDOLL_ONE_ARM_SLEEP:
        case UI::EditorTab::OBJECT_RAGDOLL_BOTH_ARMS_SLEEP:
            AddRagdoll();
            break;
        default:
            break;
        }

        if ( m_appendFailed )
        {
            outResult = EditorObjectPlacementResult {};
            return false;
        }

        m_scene.modelCount = m_world.SceneEntityCount();
        const bool placed = m_scene.modelCount > m_modelCount;
        outResult.placed = placed;
        outResult.modelCountBefore = m_modelCount;
        outResult.modelCountAfter = m_scene.modelCount;
        outResult.placedBody = m_lastPlacedBody;

        if ( placed && m_lastPlacedModelIndex >= 0 )
        {
            outResult.placedCollider = m_world.Colliders().HandleForBodyHandle( m_lastPlacedBody );
        }

        outResult.objectType = m_type;
        outResult.fixedObject = m_fixedObject;
        outResult.autoTerrainAlign = m_autoTerrainAlign;
        outResult.terrainPoint = m_terrainPoint;
        outResult.placementScale = m_placementScale;
        outResult.placementYawRadians = m_placementYawRadians;
        return placed;
    }

  private:
    bool AddModel( SceneEntityCreateDesc model, PhysicsBodyCreateDesc bodyDesc, PhysicsColliderCreateDesc colliderDesc, bool modelFixed, bool modelStartsAsleep = false )
    {
        // Lifetime: the transaction publishes the new scene, physics, and
        // render rows together before the returned handle becomes observable.
        // Physics sleep state must be seeded immediately, while the returned
        // placement result reports only the before/after count.
        if ( bodyDesc.shape.valueless_by_exception() )
        {
            SB_FATAL( "Runtime/EditorObjectPlacement", "Cannot place editor object: body collision shape is valueless before scene registration." );
        }

        bodyDesc.motionKind = modelFixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;

        if ( !model.sceneObjectId.IsValid() )
        {
            model.sceneObjectId = m_scene.AllocateSceneObjectId();
        }

        const int index = m_world.SceneEntityCount();
        const auto appendResult = m_world.TryCreateSceneEntity( std::move( model ), std::move( bodyDesc ), std::move( colliderDesc ) );

        if ( !appendResult.status.Ok() )
        {
            m_appendFailed = true;
            fprintf( stderr, "[editor] Cannot place object: %s\n", appendResult.status.ErrorMessage() );
            return false;
        }

        m_lastPlacedBody = appendResult.body;
        m_lastPlacedModelIndex = index;

        if ( !modelFixed )
        {
            if ( modelStartsAsleep )
            {
                SeedEditorPhysicsBodyAsleep( m_world, index );
            }
            else
            {
                WakeEditorPhysicsBody( m_world, index );
            }
        }

        return true;
    }

    void AddSphere( const char* label, float radius, float restitution )
    {
        const float mass = CalculateSphereMass( radius );

        const Vector3 inertia = CalculateSphereInertia( radius, mass );
        const Vector3 center( m_terrainPoint.x, m_terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON, m_terrainPoint.z );

        SceneEntityCreateDesc model;
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", m_modePrefix, label, m_serial );
        model.SetName( name );
        const BoundingSphere shape( radius, Vector3( 0.0f, 0.0f, 0.0f ) );
        AddModel( std::move( model ),
                  MakeEditorBodyDesc( shape, center, IDENTITY_QUATERNION, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), inertia, mass, restitution ),
                  MakeEditorColliderDesc( shape, restitution ),
                  m_placementFixed );
    }

    void AddBox()
    {
        const Vector3 halfExtents = m_placementScale;

        const float mass = CalculateBoxMass( halfExtents );
        Vector3 center;

        if ( !TryComputeEditorObjectCenter( m_diagnostics, m_type, m_terrainPoint, m_placementScale, m_placementOrientation, m_assets, center ) )
        {
            return;
        }

        SceneEntityCreateDesc model;
        ApplyEditorSpawnMaterial( model, m_fixedObject, true );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_box_%03d", m_modePrefix, m_serial );
        model.SetName( name );
        const Vector3 inertia = CalculateBoxInertiaForHalfExtents( halfExtents, mass );
        const BoundingBox shape( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );

        // Lifetime: materialize the variant once before descriptor creation.
        // The body helper borrows this stable value and the collider helper gets
        // its own copy, avoiding an implicit by-value conversion temporary.
        const CollisionShape bodyShape = shape;
        PhysicsBodyCreateDesc bodyDesc = MakeEditorBodyDesc( bodyShape,
                                                             center,
                                                             m_alignToTerrain ? m_placementOrientation : IDENTITY_QUATERNION,
                                                             Vector3( 0.0f, 0.0f, 0.0f ),
                                                             Vector3( 0.0f, 0.0f, 0.0f ),
                                                             inertia,
                                                             mass,
                                                             0.25f );

        PhysicsColliderCreateDesc colliderDesc = MakeEditorColliderDesc( shape, 0.25f );
        AddModel( std::move( model ), std::move( bodyDesc ), std::move( colliderDesc ), m_placementFixed );
    }

    void AddHull( EditorHullAsset asset )
    {
        const char* label = EditorHullAssetToken( asset );

        const char* path = EditorHullAssetPath( asset );

        if ( !path )
        {
            return;
        }

        ConvexHullShape hull;
        const SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( m_diagnostics, path, hull );

        if ( !hullLoad.Ok() )
        {
            fprintf( stderr, "[editor] Cannot place hull asset %s: %s\n", label, hullLoad.ErrorMessage() );
            return;
        }

        ConvexHullShape scaledHull = hull;
        scaledHull.ScaleAxis( 0, m_placementScale.x );
        scaledHull.ScaleAxis( 1, m_placementScale.y );
        scaledHull.ScaleAxis( 2, m_placementScale.z );
        const float mass = scaledHull.GetDefaultMass();
        const bool alignHull = m_alignToTerrain;
        const RotationMatrix hullRotation = alignHull ? m_placementRotation : IDENTITY_MATRIX;
        const Quaternion hullOrientation = alignHull ? m_placementOrientation : IDENTITY_QUATERNION;
        const Vector3 authoredOrigin = m_terrainPoint + hullRotation * Vector3( 0.0f, HullAuthoredBottomOffset( scaledHull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

        const Vector3 center = authoredOrigin + hullRotation * scaledHull.GetAuthoredCenterOfMass();
        SceneEntityCreateDesc model;
        SkullbonezCore::Rendering::RenderMaterial rockMaterial;

        if ( TryEditorRockMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else if ( TryEditorRootMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else
        {
            ApplyEditorSpawnMaterial( model, m_fixedObject, false );
        }

        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", m_modePrefix, label, m_serial );
        model.SetName( name );
        AddModel( std::move( model ),
                  MakeEditorBodyDesc( scaledHull, center, hullOrientation, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), scaledHull.ComputeBoxApproxInertia( mass ), mass, 0.25f ),
                  MakeEditorColliderDesc( scaledHull, 0.25f, MakeShareableHullShapeIdentity( path, m_placementScale ) ),
                  m_placementFixed );
    }

    void AddTree( const EditorTreeDefinition& treeDefinition )
    {
        PhysicsSceneObjectId treeRootObjectId;

        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];

            if ( !CachedEditorHullForAsset( m_diagnostics, part.hullAsset ) )
            {
                fprintf( stderr, "[editor] Cannot place tree: missing hull asset %s.\n", EditorHullAssetToken( part.hullAsset ) );

                return;
            }
        }

        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            const ConvexHullShape* sourceHull = CachedEditorHullForAsset( m_diagnostics, part.hullAsset );

            if ( !sourceHull )
            {
                continue;
            }

            ConvexHullShape hull = *sourceHull;
            const Vector3 localOffset( part.offsetX, part.offsetY, part.offsetZ );
            const Vector3 authoredOrigin = m_terrainPoint + m_placementRotation * ( localOffset + Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f ) );

            const Vector3 center = authoredOrigin + m_placementRotation * hull.GetAuthoredCenterOfMass();
            const float mass = hull.GetDefaultMass();
            const Vector3 inertia = hull.ComputeBoxApproxInertia( mass );
            SceneEntityCreateDesc model;
            model.SetRenderMaterial( EditorTreePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", m_modePrefix, treeDefinition.label, m_serial, part.suffix );
            model.SetName( name );
            model.sceneObjectId = m_scene.AllocateSceneObjectId();

            if ( partIndex == 0 )
            {
                treeRootObjectId = model.sceneObjectId;
            }

            const bool partFixed = treeDefinition.forceFixed || part.startsFixed || m_placementFixed;

            // Invariant: editor tree grouping is prefab metadata known before
            // append. Pass it directly instead of making the collection recover
            // group identity from display-name suffixes.
            model.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, treeRootObjectId, partIndex );
            PhysicsBodyCreateDesc bodyDesc = MakeEditorBodyDesc( hull, center, m_placementOrientation, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), inertia, mass, part.restitution );

            bodyDesc.releasesFromFixedOnContact = part.contactReleaseOnImpact;
            bodyDesc.contactReleaseImpulseThreshold = part.contactReleaseImpulseThreshold;

            if ( !AddModel( std::move( model ),
                            std::move( bodyDesc ),
                            MakeEditorColliderDesc( hull, part.restitution, MakeShareableHullShapeIdentity( EditorHullAssetPath( part.hullAsset ), Vector3( 1.0f, 1.0f, 1.0f ) ) ),
                            partFixed,
                            treeDefinition.seedAsleep && !partFixed ) )
            {
                return;
            }
        }
    }

    void AddBuilding( const EditorBuildingDefinition& buildingDefinition )
    {
        bool failed = false;

        const Vector3 base = m_terrainPoint + m_placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const bool ok = ForEachEditorBuildingPart( m_type, m_assets, [&]( const Json& part )
                                                   {
                                                       if ( failed )
                                                       {
                                                           return;
                                                       }

                                                       const float restitution = EditorJsonFloatOr( part, "restitution", 0.08f );
                                                       const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                                                       const Quaternion partOrientation = EditorBuildingPartOrientation( m_placementOrientation, part );
                                                       Quaternion partCopy = partOrientation;
                                                       const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                                                       const Vector3 authoredOrigin = base + m_placementRotation * offset;
                                                       const std::string primitiveType = EditorAssetPrimitiveType( part );
                                                       auto finishPartModel = [&]( SceneEntityCreateDesc&& model, PhysicsBodyCreateDesc bodyDesc, PhysicsColliderCreateDesc colliderDesc )
                                                       {
                                                           model.SetRenderMaterial( EditorBuildingPartMaterial( part ) );

                                                           if ( const Json* velocity = EditorJsonFindMember( part, "velocity" ) )
                                                           {
                                                               Vector3 authoredVelocity;

                                                               if ( TryReadEditorJsonVec3( *velocity, authoredVelocity ) )
                                                               {
                                                                   bodyDesc.linearVelocity = authoredVelocity;
                                                               }
                                                           }

                                                           if ( const Json* angularVelocity = EditorJsonFindMember( part, "angularVelocity" ) )
                                                           {
                                                               Vector3 authoredAngularVelocity;

                                                               if ( TryReadEditorJsonVec3( *angularVelocity, authoredAngularVelocity ) )
                                                               {
                                                                   bodyDesc.angularVelocity = authoredAngularVelocity;
                                                               }
                                                           }

                                                           char name[64];
                                                           const std::string partName = EditorJsonStringOr( part, "name", "part" );
                                                           snprintf( name, sizeof( name ), "%s_%s_%03d_%s", m_modePrefix, buildingDefinition.label, m_serial, partName.c_str() );

                                                           name[sizeof( name ) - 1] = '\0';
                                                           model.SetName( name );
                                                           const bool partFixed = m_placementFixed || EditorJsonBoolOr( part, "fixed", false );
                                                           const bool partSleeping = EditorJsonBoolOr( part, "sleeping", true );
                                                           bodyDesc.releasesFromFixedOnContact = EditorJsonBoolOr( part, "contactReleaseOnImpact", false );
                                                           bodyDesc.contactReleaseImpulseThreshold = (std::max)( 0.0f, EditorJsonFloatOr( part, "contactReleaseImpulseThre" "shold", 1.0f ) );

                                                           if ( !AddModel( std::move( model ), std::move( bodyDesc ), std::move( colliderDesc ), partFixed, partSleeping && !partFixed ) )
                                                           {
                                                               failed = true;
                                                           }
                                                       };

                                                       if ( primitiveType == "convexHull" )
                                                       {
                                                           const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                                                           const ConvexHullShape* sourceHull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( m_diagnostics, hullPath );

                                                           if ( !sourceHull )
                                                           {
                                                               failed = true;
                                                               return;
                                                           }

                                                           ConvexHullShape hull = *sourceHull;
                                                           const float mass = EditorJsonFloatOr( part, "mass", hull.GetDefaultMass() );
                                                           const Vector3 center = authoredOrigin + partRotation * hull.GetAuthoredCenterOfMass();
                                                           const Vector3 inertia = hull.ComputeBoxApproxInertia( mass );
                                                           SceneEntityCreateDesc model;
                                                           finishPartModel( std::move( model ), MakeEditorBodyDesc( hull,
                                                                                                center,
                                                                                                partOrientation,
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                inertia,
                                                                                                mass,
                                                                                                restitution ), MakeEditorColliderDesc( hull, restitution, MakeShareableHullShapeIdentity( ResolveEditorHullAssetPath( hullPath.c_str() ), Vector3( 1.0f, 1.0f, 1.0f ) ) ) );

                                                           return;
                                                       }

                                                       if ( primitiveType == "box" )
                                                       {
                                                           Vector3 halfExtents;

                                                           if ( !TryReadEditorBoxHalfExtents( part, halfExtents ) )
                                                           {
                                                               failed = true;
                                                               return;
                                                           }

                                                           const float mass = EditorJsonFloatOr( part, "mass", CalculateBoxMass( halfExtents ) );
                                                           const Vector3 inertia = CalculateBoxInertiaForHalfExtents( halfExtents, mass );
                                                           SceneEntityCreateDesc model;
                                                           const BoundingBox shape( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );
                                                           finishPartModel( std::move( model ), MakeEditorBodyDesc( shape,
                                                                                                authoredOrigin,
                                                                                                partOrientation,
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                inertia,
                                                                                                mass,
                                                                                                restitution ), MakeEditorColliderDesc( shape, restitution ) );

                                                           return;
                                                       }

                                                       if ( primitiveType == "sphere" )
                                                       {
                                                           float radius = 0.0f;

                                                           if ( !TryReadEditorSphereRadius( part, radius ) )
                                                           {
                                                               failed = true;
                                                               return;
                                                           }

                                                           const float mass = EditorJsonFloatOr( part, "mass", CalculateSphereMass( radius ) );
                                                           const Vector3 inertia = CalculateSphereInertia( radius, mass );
                                                           SceneEntityCreateDesc model;
                                                           const BoundingSphere shape( radius, Vector3( 0.0f, 0.0f, 0.0f ) );
                                                           finishPartModel( std::move( model ), MakeEditorBodyDesc( shape,
                                                                                                authoredOrigin,
                                                                                                partOrientation,
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                                inertia,
                                                                                                mass,
                                                                                                restitution ), MakeEditorColliderDesc( shape, restitution ) );

                                                           return;
                                                       }

                                                       failed = true;
                                                   } );

        if ( failed || !ok )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s.\n", buildingDefinition.assetName );
        }
    }

    void AddRagdoll()
    {
        RagdollBuildOptions options;

        char prefix[64];
        sprintf_s( prefix, sizeof( prefix ), "%s_ragdoll_%03d", m_modePrefix, m_serial );
        options.namePrefix = prefix;
        options.terrainPoint = m_terrainPoint;
        options.orientation = m_placementOrientation;
        options.scale = m_placementScale.x;
        options.fixed = m_placementFixed;
        options.startsAsleep = m_ragdollStartsAsleep && !m_placementFixed;
        options.pose = EditorRagdollPose( m_type );
        options.firstSceneObjectId = m_scene.AllocateSceneObjectIdRange( Ragdoll::SIMPLE_PART_COUNT );
        const SkullbonezCore::Core::SbResult appendResult = SceneAuthoredSetup::AppendSimpleRagdoll( m_diagnostics, m_world, options );

        if ( !appendResult.Ok() )
        {
            m_appendFailed = true;
            fprintf( stderr, "[editor] Cannot place ragdoll: %s\n", appendResult.ErrorMessage() );
        }
    }

    Core::SbDiagnosticStore& m_diagnostics;
    SceneWorld& m_world;
    SceneSessionState& m_scene;
    const Assets::AssetSystem& m_assets;
    int m_type;
    int m_modelCount;
    const EditorTreeDefinition* m_tree;
    const EditorHouseDefinition* m_house;
    const EditorBuildingDefinition* m_building;
    Vector3 m_terrainPoint;
    bool m_fixedObject;
    Vector3 m_placementScale;
    int m_serial;
    bool m_alignToTerrain;
    Quaternion m_placementOrientation;
    RotationMatrix m_placementRotation;
    bool m_placementFixed;
    bool m_ragdollStartsAsleep;
    const char* m_modePrefix;
    PhysicsBodyHandle m_lastPlacedBody;
    int m_lastPlacedModelIndex = -1;
    bool m_appendFailed = false;
    bool m_autoTerrainAlign;
    float m_placementYawRadians;
};
} // namespace

bool PlaceEditorObjectAtTerrainPoint( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                      RunEditorPlacementState& editor,
                                      SceneWorld& world,
                                      SceneSessionState& scene,
                                      const Assets::AssetSystem& assets,
                                      int activeModelCapacity,
                                      EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult )
{
    int type = 0;

    if ( !TryResolveEditorObjectPlacementPreflight( world, assets, activeModelCapacity, request, type, false ) )
    {
        outResult = EditorObjectPlacementResult {};

        return false;
    }

    EditorObjectPlacementBatch batch( diagnostics, editor, world, scene, assets, type, request );
    return batch.Execute( outResult );
}

} // namespace Runtime
} // namespace SkullbonezCore

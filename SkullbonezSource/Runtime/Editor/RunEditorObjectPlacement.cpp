/*
File: SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp
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
  Asset primitive: Single spawned collision body inside a placeable asset
    container, such as a box, sphere, or convex hull.
  Scene-object group: Scene-owned behavior metadata that keeps multi-part editor
    prefabs, such as releasable trees, tied to one stable root object id.

Invariants:
  - Preflight and commit must use matching geometry decisions.
  - Placement names and model order are replay-visible and must stay stable.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
  - SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp
*/
#include "EditorPlacementAssets.h"
#include "../../Assets/AssetKeys.h"
#include "EditorTools.h"
#include "EditorHullAssets.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneControllerState.h"
#include "../Scene/SceneRuntime.h"
#include "../Scene/SceneAuthoredSetup.h"
#include "../Scene/SceneController.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../../UI/UITabEditor.h"
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
using SkullbonezCore::Math::Vector::Vector3;
using Json = SkullbonezCore::Runtime::RunInternal::EditorPlacementJson;

namespace SkullbonezCore
{
namespace Runtime
{
namespace RunInternal
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

PhysicsColliderCreateDesc MakeEditorColliderDesc( CollisionShape shape, float restitution )
{
    // Why: placement commit already owns the primitive geometry selected by the
    // editor. Pass that value into physics at append time so the collider store
    // receives exact shape facts without a legacy object record readback.
    return MakeColliderCreateDesc( std::move( shape ), restitution, HashStr( "default" ) );
}


PhysicsBodyCreateDesc MakeEditorBodyDesc( CollisionShape shape,
                                          const Vector3& position,
                                          const Quaternion& orientation,
                                          const Vector3& linearVelocity,
                                          const Vector3& angularVelocity,
                                          const Vector3& rotationalInertia,
                                          float mass,
                                          float restitution,
                                          Geometry::Terrain* terrain )
{
    return MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{},
                                      shape,
                                      position,
                                      orientation,
                                      linearVelocity,
                                      angularVelocity,
                                      rotationalInertia,
                                      mass,
                                      restitution,
                                      PhysicsBodyMotionKind::Dynamic,
                                      terrain );
}


static bool TryResolveEditorObjectPlacementPreflight( EditorObjectPlacementContext context,
                                                      EditorObjectPlacementRequest request,
                                                      int& outType,
                                                      bool reportErrors )
{
    // Invariant: This preflight is the single capacity and asset-count gate
    // for both CanPlace and Place. Add new multi-part object families here
    // before adding their placement branch below.
    const int modelCount = context.models.Scene().SceneEntityCount();
    const int type = std::clamp( request.objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const int buildingPartCount = building ? EditorBuildingPartCount( type, context.assets ) : 0;
    const bool isRagdollType = type == UI::EditorTab::OBJECT_RAGDOLL || type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    if ( building && buildingPartCount <= 0 )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s is missing or empty.\n", building->assetName );
        }
        return false;
    }
    const int requiredModelCount =
        isRagdollType
            ? Ragdoll::SIMPLE_PART_COUNT
            : ( building ? buildingPartCount : ( house ? house->partCount : ( tree ? tree->partCount : 1 ) ) );
    if ( modelCount + requiredModelCount > context.activeModelCapacity )
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


bool CanPlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context, EditorObjectPlacementRequest request )
{
    int type = 0;
    return TryResolveEditorObjectPlacementPreflight( context, request, type, true );
}


bool PlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context,
                                      EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult )
{
    int type = 0;
    if ( !TryResolveEditorObjectPlacementPreflight( context, request, type, false ) )
    {
        outResult = EditorObjectPlacementResult{};
        return false;
    }

    const int modelCount = context.models.Scene().SceneEntityCount();
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const Vector3& terrainPoint = request.terrainPoint;
    const bool fixedObject = request.fixedObject;
    const Vector3 placementScale = EditorClampPlacementScale( type, context.editor.placementScale );
    const int serial = context.editor.placedObjectSerial++;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        context.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const bool alignToTerrain = EditorObjectAlignsToTerrainNormal( type, context.editor.autoTerrainAlign );
    const Quaternion placementOrientation = EditorPlacementOrientation( type,
                                                                        terrainNormal,
                                                                        context.editor.autoTerrainAlign,
                                                                        context.editor.placementYawRadians );
    Quaternion placementOrientationCopy = placementOrientation;
    const RotationMatrix placementRotation = placementOrientationCopy.GetOrientationMatrix();
    const bool placementFixed = tree && tree->forceFixed ? true : fixedObject;
    const bool ragdollStartsAsleep = type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    const char* modePrefix = placementFixed ? "static"
                                            : ( ( tree && tree->seedAsleep ) || ( house && house->seedAsleep ) ||
                                                        building || ragdollStartsAsleep
                                                    ? "sleeping"
                                                    : "dynamic" );
    // Invariant: placement selection preserves the last added row to keep the
    // existing multi-part object behavior while carrying store-owned identity.
    Physics::PhysicsBodyHandle lastPlacedBody;
    int lastPlacedModelIndex = -1;
    bool appendFailed = false;

    auto addModel = [&]( SceneEntityCreateDesc model,
                         PhysicsBodyCreateDesc bodyDesc,
                         PhysicsColliderCreateDesc colliderDesc,
                         bool modelFixed,
                         bool modelStartsAsleep = false ) -> bool
    {
        // Lifetime: the transaction publishes the new scene, physics, and
        // render rows together before the returned handle becomes observable.
        // Physics sleep state must be seeded immediately, while the returned
        // placement result reports only the before/after count.
        bodyDesc.motionKind = modelFixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
        if ( !model.sceneObjectId.IsValid() )
        {
            model.sceneObjectId = context.scene.AllocateSceneObjectId();
        }
        const int index = context.models.Scene().SceneEntityCount();
        const auto appendResult = context.models.Scene().TryCreateSceneEntity( std::move( model ),
                                                                               std::move( bodyDesc ),
                                                                               std::move( colliderDesc ) );
        if ( !appendResult.status.ok )
        {
            appendFailed = true;
            fprintf( stderr, "[editor] Cannot place object: %s\n", appendResult.status.error.message );
            return false;
        }
        lastPlacedBody = appendResult.body;
        lastPlacedModelIndex = index;
        if ( !modelFixed )
        {
            if ( modelStartsAsleep )
            {
                SeedEditorPhysicsBodyAsleep( context.models, context.physics, index );
            }
            else
            {
                WakeEditorPhysicsBody( context.models, context.physics, index );
            }
        }
        return true;
    };

    auto addSphere = [&]( const char* label, float radius, float restitution )
    {
        const float mass = CalculateSphereMass( radius );
        const Vector3 inertia = CalculateSphereInertia( radius, mass );
        const Vector3 center( terrainPoint.x,
                              terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON,
                              terrainPoint.z );
        SceneEntityCreateDesc model;
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        const BoundingSphere shape( radius, Vector3( 0.0f, 0.0f, 0.0f ) );
        addModel( std::move( model ),
                  MakeEditorBodyDesc( shape,
                                      center,
                                      IDENTITY_QUATERNION,
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      inertia,
                                      mass,
                                      restitution,
                                      context.terrain ),
                  MakeEditorColliderDesc( shape, restitution ),
                  placementFixed );
    };

    auto addBox = [&]()
    {
        const Vector3 halfExtents = placementScale;
        const float mass = CalculateBoxMass( halfExtents );
        Vector3 center;
        if ( !TryComputeEditorObjectCenter( type,
                                            terrainPoint,
                                            placementScale,
                                            placementOrientation,
                                            context.assets,
                                            center ) )
        {
            return;
        }
        SceneEntityCreateDesc model;
        ApplyEditorSpawnMaterial( model, fixedObject, true );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_box_%03d", modePrefix, serial );
        model.SetName( name );
        const Vector3 inertia = CalculateBoxInertiaForHalfExtents( halfExtents, mass );
        const BoundingBox shape( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );
        addModel( std::move( model ),
                  MakeEditorBodyDesc( shape,
                                      center,
                                      alignToTerrain ? placementOrientation : IDENTITY_QUATERNION,
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      inertia,
                                      mass,
                                      0.25f,
                                      context.terrain ),
                  MakeEditorColliderDesc( shape, 0.25f ),
                  placementFixed );
    };

    auto addHull = [&]( EditorHullAsset asset )
    {
        const char* label = EditorHullAssetToken( asset );
        const char* path = EditorHullAssetPath( asset );
        if ( !path )
        {
            return;
        }
        ConvexHullShape hull;
        const SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( path, hull );
        if ( !hullLoad.ok )
        {
            fprintf( stderr, "[editor] Cannot place hull asset %s: %s\n", label, hullLoad.error.message );
            return;
        }
        ConvexHullShape scaledHull = hull;
        scaledHull.ScaleAxis( 0, placementScale.x );
        scaledHull.ScaleAxis( 1, placementScale.y );
        scaledHull.ScaleAxis( 2, placementScale.z );
        const float mass = scaledHull.GetDefaultMass();
        const bool alignHull = alignToTerrain;
        const RotationMatrix hullRotation = alignHull ? placementRotation : IDENTITY_MATRIX;
        const Quaternion hullOrientation = alignHull ? placementOrientation : IDENTITY_QUATERNION;
        const Vector3 authoredOrigin =
            terrainPoint +
            hullRotation *
                Vector3( 0.0f, HullAuthoredBottomOffset( scaledHull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
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
            ApplyEditorSpawnMaterial( model, fixedObject, false );
        }
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        addModel( std::move( model ),
                  MakeEditorBodyDesc( scaledHull,
                                      center,
                                      hullOrientation,
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                      scaledHull.ComputeBoxApproxInertia( mass ),
                                      mass,
                                      0.25f,
                                      context.terrain ),
                  MakeEditorColliderDesc( scaledHull, 0.25f ),
                  placementFixed );
    };

    auto addTree = [&]( const EditorTreeDefinition& treeDefinition )
    {
        PhysicsSceneObjectId treeRootObjectId;
        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            if ( !CachedEditorHullForAsset( part.hullAsset ) )
            {
                fprintf( stderr,
                         "[editor] Cannot place tree: missing hull asset %s.\n",
                         EditorHullAssetToken( part.hullAsset ) );
                return;
            }
        }

        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            const ConvexHullShape* sourceHull = CachedEditorHullForAsset( part.hullAsset );
            if ( !sourceHull )
            {
                continue;
            }
            ConvexHullShape hull = *sourceHull;
            const Vector3 localOffset( part.offsetX, part.offsetY, part.offsetZ );
            const Vector3 authoredOrigin =
                terrainPoint +
                placementRotation * ( localOffset + Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f ) );
            const Vector3 center = authoredOrigin + placementRotation * hull.GetAuthoredCenterOfMass();
            const float mass = hull.GetDefaultMass();
            const Vector3 inertia = hull.ComputeBoxApproxInertia( mass );
            SceneEntityCreateDesc model;
            model.SetRenderMaterial( EditorTreePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, treeDefinition.label, serial, part.suffix );
            model.SetName( name );
            model.sceneObjectId = context.scene.AllocateSceneObjectId();
            if ( partIndex == 0 )
            {
                treeRootObjectId = model.sceneObjectId;
            }
            const bool partFixed = treeDefinition.forceFixed || part.startsFixed || placementFixed;
            // Invariant: editor tree grouping is prefab metadata known before
            // append. Pass it directly instead of making the collection recover
            // group identity from display-name suffixes.
            model.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, treeRootObjectId, partIndex );
            PhysicsBodyCreateDesc bodyDesc = MakeEditorBodyDesc( hull,
                                                                 center,
                                                                 placementOrientation,
                                                                 Vector3( 0.0f, 0.0f, 0.0f ),
                                                                 Vector3( 0.0f, 0.0f, 0.0f ),
                                                                 inertia,
                                                                 mass,
                                                                 part.restitution,
                                                                 context.terrain );
            bodyDesc.releasesFromFixedOnContact = part.contactReleaseOnImpact;
            bodyDesc.contactReleaseImpulseThreshold = part.contactReleaseImpulseThreshold;
            if ( !addModel( std::move( model ),
                            std::move( bodyDesc ),
                            MakeEditorColliderDesc( hull, part.restitution ),
                            partFixed,
                            treeDefinition.seedAsleep && !partFixed ) )
            {
                return;
            }
        }
    };

    auto addHouse = [&]( const EditorHouseDefinition& houseDefinition )
    {
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < houseDefinition.partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = houseDefinition.parts[partIndex];
            const Vector3 halfExtents( part.halfX, part.halfY, part.halfZ );
            const float mass = CalculateBoxMass( halfExtents );
            const Vector3 center = base + placementRotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            const Vector3 inertia = CalculateBoxInertiaForHalfExtents( halfExtents, mass );
            SceneEntityCreateDesc model;
            model.SetRenderMaterial( EditorHousePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, houseDefinition.label, serial, part.suffix );
            model.SetName( name );
            const BoundingBox shape( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );
            if ( !addModel( std::move( model ),
                            MakeEditorBodyDesc( shape,
                                                center,
                                                placementOrientation,
                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                inertia,
                                                mass,
                                                part.restitution,
                                                context.terrain ),
                            MakeEditorColliderDesc( shape, part.restitution ),
                            placementFixed,
                            houseDefinition.seedAsleep && !placementFixed ) )
            {
                return;
            }
        }
    };

    auto addBuilding = [&]( const EditorBuildingDefinition& buildingDefinition )
    {
        bool failed = false;
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const bool ok = ForEachEditorBuildingPart(
            type,
            context.assets,
            [&]( const Json& part )
            {
                if ( failed )
                {
                    return;
                }
                const float restitution = EditorJsonFloatOr( part, "restitution", 0.08f );
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( placementOrientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 authoredOrigin = base + placementRotation * offset;
                const std::string primitiveType = EditorAssetPrimitiveType( part );
                auto finishPartModel = [&]( SceneEntityCreateDesc&& model,
                                            PhysicsBodyCreateDesc bodyDesc,
                                            PhysicsColliderCreateDesc colliderDesc )
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
                    snprintf( name,
                              sizeof( name ),
                              "%s_%s_%03d_%s",
                              modePrefix,
                              buildingDefinition.label,
                              serial,
                              partName.c_str() );
                    name[sizeof( name ) - 1] = '\0';
                    model.SetName( name );
                    const bool partFixed = placementFixed || EditorJsonBoolOr( part, "fixed", false );
                    const bool partSleeping = EditorJsonBoolOr( part, "sleeping", true );
                    bodyDesc.releasesFromFixedOnContact = EditorJsonBoolOr( part, "contactReleaseOnImpact", false );
                    bodyDesc.contactReleaseImpulseThreshold =
                        (std::max)( 0.0f, EditorJsonFloatOr( part, "contactReleaseImpulseThreshold", 1.0f ) );
                    if ( !addModel( std::move( model ),
                                    std::move( bodyDesc ),
                                    std::move( colliderDesc ),
                                    partFixed,
                                    partSleeping && !partFixed ) )
                    {
                        failed = true;
                    }
                };

                if ( primitiveType == "convexHull" )
                {
                    const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                    const ConvexHullShape* sourceHull =
                        hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
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
                    finishPartModel( std::move( model ),
                                     MakeEditorBodyDesc( hull,
                                                         center,
                                                         partOrientation,
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         inertia,
                                                         mass,
                                                         restitution,
                                                         context.terrain ),
                                     MakeEditorColliderDesc( hull, restitution ) );
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
                    finishPartModel( std::move( model ),
                                     MakeEditorBodyDesc( shape,
                                                         authoredOrigin,
                                                         partOrientation,
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         inertia,
                                                         mass,
                                                         restitution,
                                                         context.terrain ),
                                     MakeEditorColliderDesc( shape, restitution ) );
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
                    finishPartModel( std::move( model ),
                                     MakeEditorBodyDesc( shape,
                                                         authoredOrigin,
                                                         partOrientation,
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                                         inertia,
                                                         mass,
                                                         restitution,
                                                         context.terrain ),
                                     MakeEditorColliderDesc( shape, restitution ) );
                    return;
                }
                failed = true;
            } );
        if ( failed || !ok )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s.\n", buildingDefinition.assetName );
        }
    };

    auto addRagdoll = [&]()
    {
        RagdollBuildOptions options;
        char prefix[64];
        sprintf_s( prefix, sizeof( prefix ), "%s_ragdoll_%03d", modePrefix, serial );
        options.namePrefix = prefix;
        options.terrainPoint = terrainPoint;
        options.orientation = placementOrientation;
        options.scale = placementScale.x;
        options.fixed = placementFixed;
        options.startsAsleep = ragdollStartsAsleep && !placementFixed;
        options.firstSceneObjectId = context.scene.AllocateSceneObjectIdRange( Ragdoll::SIMPLE_PART_COUNT );
        SceneSimpleRagdollAppendContext ragdollContext{
            context.scene,
            context.models.Scene(),
        };
        const SkullbonezCore::Core::SbResult appendResult =
            SceneAuthoredSetup::AppendSimpleRagdoll( ragdollContext, options );
        if ( !appendResult.ok )
        {
            appendFailed = true;
            fprintf( stderr, "[editor] Cannot place ragdoll: %s\n", appendResult.error.message );
        }
    };

    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        addBox();
        break;
    case UI::EditorTab::OBJECT_BALL:
        addSphere( "ball", placementScale.x, 0.45f );
        break;
    case UI::EditorTab::OBJECT_SPHERE:
        addSphere( "sphere", placementScale.x, 0.35f );
        break;
    case UI::EditorTab::OBJECT_HULL_WEDGE:
        addHull( EditorHullAsset::WEDGE );
        break;
    case UI::EditorTab::OBJECT_HULL_TRI_PRISM:
        addHull( EditorHullAsset::TRI_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        addHull( EditorHullAsset::TAPERED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_HULL_PYRAMID:
        addHull( EditorHullAsset::PYRAMID );
        break;
    case UI::EditorTab::OBJECT_HULL_HEX_PRISM:
        addHull( EditorHullAsset::HEX_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_DIAMOND:
        addHull( EditorHullAsset::DIAMOND );
        break;
    case UI::EditorTab::OBJECT_ROCK_SLAB:
        addHull( EditorHullAsset::ROCK_SLAB_FLAT );
        break;
    case UI::EditorTab::OBJECT_ROCK_LUMP:
        addHull( EditorHullAsset::ROCK_LUMP_LARGE );
        break;
    case UI::EditorTab::OBJECT_ROCK_SHARD:
        addHull( EditorHullAsset::ROCK_SHARD_TALL );
        break;
    case UI::EditorTab::OBJECT_ROCK_CHIPPED:
        addHull( EditorHullAsset::ROCK_CHIPPED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_ROOT_SMALL:
        addHull( EditorHullAsset::TREE_ROOT_SMALL );
        break;
    case UI::EditorTab::OBJECT_ROOT_LARGE:
        addHull( EditorHullAsset::TREE_ROOT_LARGE );
        break;
    case UI::EditorTab::OBJECT_TREE_SMALL:
        if ( tree )
        {
            addTree( *tree );
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
        if ( tree )
        {
            addTree( *tree );
        }
        break;
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_WALL_200_SLEEP:
        if ( building )
        {
            addBuilding( *building );
        }
        break;
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        addRagdoll();
        break;
    default:
        break;
    }

    if ( appendFailed )
    {
        outResult = EditorObjectPlacementResult{};
        return false;
    }

    context.scene.modelCount = context.models.Scene().SceneEntityCount();
    const bool placed = context.scene.modelCount > modelCount;
    outResult.placed = placed;
    outResult.modelCountBefore = modelCount;
    outResult.modelCountAfter = context.scene.modelCount;
    outResult.placedBody = lastPlacedBody;
    if ( placed && lastPlacedModelIndex >= 0 )
    {
        outResult.placedCollider = context.models.Scene().Colliders().HandleForBodyHandle( lastPlacedBody );
    }
    outResult.objectType = type;
    outResult.fixedObject = fixedObject;
    outResult.autoTerrainAlign = context.editor.autoTerrainAlign;
    outResult.terrainPoint = terrainPoint;
    outResult.placementScale = placementScale;
    outResult.placementYawRadians = context.editor.placementYawRadians;
    return placed;
}
} // namespace RunInternal
} // namespace Runtime
} // namespace SkullbonezCore

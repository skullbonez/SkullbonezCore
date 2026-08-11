/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.cpp
Purpose:
  Serializes the current scene state back into a JSON scene file.

Summary:
  The writer validates borrowed owner topology, resolves each entity through
  its stable body/collider identity, then emits non-asset objects directly and
  groups asset-backed rows by their stable asset root.

Glossary:
  Scene snapshot: JSON scene emitted from current runtime state rather than the
    original authored file.
  Cold metadata: Names, render materials, and behavior grouping that identify
    objects but do not drive physics integration.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Saved body pose, velocity, sleep, mass, inertia, and shape data come from
    PhysicsBodyStore/ColliderStore so scene saving does not depend on post-step
    legacy object record body writeback.
  - No entity or point joint is silently skipped; owner topology disagreement
    is an engine invariant failure.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneSnapshotWriter.h"
#include "../Core/SbDiagnosticStore.h"
#include "../Runtime/Scene/SceneEntityStore.h"

#include "../Core/FatalError.h"
#include "../Physics/BoundingBox.h"
#include "../Physics/BoundingSphere.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/ConvexHullShape.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/Ragdoll.h"
#include "../Runtime/Editor/EditorHullAssets.h"
#include "../Rendering/RenderMaterial.h"
#include "../Runtime/Tools/RuntimeFileWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetFromToken;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderAuthoringRecord;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::LoadPhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Runtime::RuntimeFileWriter;
using SkullbonezCore::Runtime::SceneAssetAffiliation;
using SkullbonezCore::Runtime::SceneBehaviorGroup;
using SkullbonezCore::Runtime::SceneBehaviorGroupKind;
using SkullbonezCore::Runtime::SceneEntityRecord;
using SkullbonezCore::Runtime::SceneEntityStore;

namespace
{
using Json = nlohmann::ordered_json;

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

Json Vec3Json( float x, float y, float z )
{
    return Json::array( { x, y, z } );
}

Json OrientationJson( const Quaternion& orientation )
{
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    orientation.GetComponents( qx, qy, qz, qw );
    return Json::array( { qx, qy, qz, qw } );
}

bool SceneMaterialFloatDiffers( float a, float b )
{
    return std::fabs( a - b ) > 1.0e-5f;
}

bool ShouldSaveRenderMaterial( const SkullbonezCore::Rendering::RenderMaterial& material )
{
    const SkullbonezCore::Rendering::RenderMaterial defaults = {};
    return material.name[0] != '\0' || material.kind != defaults.kind ||
           SceneMaterialFloatDiffers( material.baseColor[0], defaults.baseColor[0] ) ||
           SceneMaterialFloatDiffers( material.baseColor[1], defaults.baseColor[1] ) ||
           SceneMaterialFloatDiffers( material.baseColor[2], defaults.baseColor[2] ) ||
           SceneMaterialFloatDiffers( material.baseColor[3], defaults.baseColor[3] ) ||
           SceneMaterialFloatDiffers( material.roughness, defaults.roughness ) ||
           SceneMaterialFloatDiffers( material.metallic, defaults.metallic ) ||
           SceneMaterialFloatDiffers( material.specular, defaults.specular ) ||
           SceneMaterialFloatDiffers( material.transmission, defaults.transmission ) ||
           SceneMaterialFloatDiffers( material.stylization, defaults.stylization ) ||
           SceneMaterialFloatDiffers( material.emissiveColor[0], defaults.emissiveColor[0] ) ||
           SceneMaterialFloatDiffers( material.emissiveColor[1], defaults.emissiveColor[1] ) ||
           SceneMaterialFloatDiffers( material.emissiveColor[2], defaults.emissiveColor[2] ) ||
           SceneMaterialFloatDiffers( material.emissiveStrength, defaults.emissiveStrength ) ||
           SceneMaterialFloatDiffers( material.textureMode, defaults.textureMode ) || material.flags != defaults.flags;
}

Json RenderMaterialJson( const char* target, const SkullbonezCore::Rendering::RenderMaterial& material )
{
    Json materialJson = {
        { "target", target ? target : "" },

        // Empty is meaningful: omitting the field lets the parser replace it
        // with the material-kind spelling and mutates the live material.
        { "name", material.name },
        { "color", Vec3Json( material.baseColor[0], material.baseColor[1], material.baseColor[2] ) },
        { "alpha", material.baseColor[3] },
        { "roughness", material.roughness },
        { "metallic", material.metallic },
        { "specular", material.specular },
        { "transmission", material.transmission },
        { "stylization", material.stylization },
    };

    if ( material.kind == SkullbonezCore::Rendering::RenderMaterialKind::Textured )
    {
        materialJson["mode"] = material.textureMode;
    }
    else
    {
        materialJson["mode"] = SkullbonezCore::Rendering::RenderMaterialKindName( material.kind );
    }

    if ( material.kind == SkullbonezCore::Rendering::RenderMaterialKind::Emissive || material.emissiveStrength > 0.0f )
    {
        materialJson["emissive"] = Vec3Json( material.emissiveColor[0], material.emissiveColor[1],
                                             material.emissiveColor[2] );

        materialJson["strength"] = material.emissiveStrength;
    }

    if ( material.flags != 0 )
    {
        materialJson["flags"] = material.flags;
    }

    return materialJson;
}

const SceneBehaviorGroup& BehaviorGroupAt( const SceneWorldSaveState& scene, int entityIndex )
{
    return scene.entities.At( entityIndex ).behaviorGroup;
}

void AddSceneObjectGroupJson( Json& object, const SceneWorldSaveState& scene, int entityIndex )
{
    const SceneBehaviorGroup& group = BehaviorGroupAt( scene, entityIndex );

    if ( group.kind != SceneBehaviorGroupKind::ReleasableTree )
    {
        return;
    }

    const int rootIndex = scene.entities.FindBySceneObjectId( group.rootObjectId );

    if ( rootIndex < 0 || group.partIndex < 0 )
    {
        SB_FATAL( "Scene/SceneSnapshotWriter", "Invalid releasable-tree group at save. row=%d root_id=%u part=%d",
                  entityIndex, group.rootObjectId.value, group.partIndex );
    }

    object["objectGroup"] = {
        { "kind", "releasableTree" },
        { "root", scene.entities.At( rootIndex ).displayName },
        { "part", group.partIndex },
    };
}

struct LiveSceneRow
{
    const SceneEntityRecord& entity;
    const PhysicsBodyRecord& body;
    PhysicsBodyHotState hotState;
    const ColliderRecord& collider;

    // Lifetime: the cold row is borrowed from the same store snapshot and must
    // stay index/handle aligned with collider until this save call returns.
    const ColliderAuthoringRecord& colliderAuthoring;
};

LiveSceneRow ResolveLiveSceneRow( const SceneWorldSaveState& scene, int entityIndex )
{
    const SceneEntityRecord& entity = scene.entities.At( entityIndex );
    const PhysicsBodyRecord* body = scene.bodies.RecordForHandle( entity.body );
    const int bodyIndex = scene.bodies.ModelIndexForHandle( entity.body );
    const PhysicsColliderHandle colliderHandle = body ? scene.colliders.HandleForBodyHandle( body->handle )
                                                      : PhysicsColliderHandle {};

    const ColliderRecord* collider = scene.colliders.RecordForHandle( colliderHandle );
    const ColliderAuthoringRecord* colliderAuthoring = scene.colliders.AuthoringRecordForHandle( colliderHandle );

    if ( !body || bodyIndex < 0 || !collider || !colliderAuthoring || collider->body != body->handle ||
         body->sceneObjectId.value != entity.sceneObjectId.value ||
         collider->sceneObjectId.value != entity.sceneObjectId.value )
    {
        SB_FATAL( "Scene/SceneSnapshotWriter",
                  "Entity/body/collider identity topology diverged at save. row=%d entity_id=%u", entityIndex,
                  entity.sceneObjectId.value );
    }

    return { entity, *body, LoadPhysicsBodyHotState( scene.bodies.HotFields(), static_cast<std::size_t>( bodyIndex ) ),
             *collider, *colliderAuthoring };
}

Json BuildLiveStateJson( const SceneWorldSaveState& scene, int entityIndex )
{
    const LiveSceneRow row = ResolveLiveSceneRow( scene, entityIndex );
    const char* contactMaterial = row.colliderAuthoring.contactMaterialName[0] != '\0'
                                      ? row.colliderAuthoring.contactMaterialName
                                      : "default";

    Json state = {
        { "sceneObjectId", row.entity.sceneObjectId.value },
        { "name", row.entity.displayName },
        { "position", Vec3Json( row.hotState.position ) },
        { "velocity", Vec3Json( row.hotState.linearVelocity ) },
        { "angularVelocity", Vec3Json( row.hotState.angularVelocity ) },
        { "orientation", OrientationJson( row.hotState.orientation ) },
        { "mass", row.body.mass },
        { "restitution", row.collider.restitution },
        { "contactMaterial", contactMaterial },
        { "inertia", Vec3Json( row.body.rotationalInertia ) },
        { "fixed", row.hotState.fixed },
    };

    // Invariant: part state overrides asset-recipe defaults in both
    // directions. Explicit false values are required so a live awake/release-
    // disabled body cannot inherit stale true authoring on reparse.
    state["sleeping"] = !row.hotState.awake;

    const auto& shape = row.collider.shape;

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape ) )
    {
        state["type"] = "ballState";
        state["radius"] = sphere->GetRadius();
    }
    else if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &shape ) )
    {
        state["type"] = "boxState";
        state["halfExtents"] = Vec3Json( box->GetHalfExtents() );
    }
    else if ( const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &shape ) )
    {
        const EditorHullAsset hullAsset = EditorHullAssetFromToken( hull->GetName() );
        state["type"] = "convexHullState";
        state["hull"] = hullAsset == EditorHullAsset::UNKNOWN ? hull->GetName() : EditorHullAssetToken( hullAsset );
        state["contactReleaseOnImpact"] = row.body.releasesFromFixedOnContact;
        state["contactReleaseImpulseThreshold"] = row.body.contactReleaseImpulseThreshold;
        AddSceneObjectGroupJson( state, scene, entityIndex );
    }
    else
    {
        SB_FATAL( "Scene/SceneSnapshotWriter", "Unsupported collider shape at save. row=%d", entityIndex );
    }

    return state;
}

bool SameAssetInstance( const SceneAssetAffiliation& a, const SceneAssetAffiliation& b )
{
    return a.rootObjectId.value == b.rootObjectId.value && std::strcmp( a.libraryToken, b.libraryToken ) == 0 &&
           std::strcmp( a.assetName, b.assetName ) == 0 && std::strcmp( a.instanceName, b.instanceName ) == 0;
}
} // namespace


SkullbonezCore::Core::SbResult SceneSnapshotWriter::Save( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                          const SceneSaveRequest& request )
{
    const SceneWorldSaveState& sceneView = request.world;
    const SceneSessionSaveState& session = request.session;
    const PresentationSaveState& presentation = request.presentation;

    // Invariant: Editable scene saves emit state-form objects whose positions,
    // velocities, sleeping flags, and materials can round-trip through
    // AuthoredSceneParser without reinterpreting authored placement offsets.
    if ( sceneView.entities.Count() != sceneView.bodies.Count() ||
         sceneView.entities.Count() != sceneView.colliders.Count() )
    {
        SB_FATAL( "Scene/SceneSnapshotWriter", "Save owner counts diverged. entities=%d bodies=%d colliders=%d",
                  sceneView.entities.Count(), sceneView.bodies.Count(), sceneView.colliders.Count() );
    }

    Json scene;
    scene["format"] = "skullbonez.scene.json";
    scene["version"] = 4;
    scene["simulation"] = Json::object();
    scene["simulation"]["physics"] = session.physicsOn;
    scene["simulation"]["text"] = session.textOn;
    scene["simulation"]["world"] = {
        { "gravity", sceneView.gravity },
        { "fluidHeight", sceneView.fluidSurfaceHeight },
        { "fluidDensity", sceneView.fluidDensity },
    };

    const auto& mutualGravity = sceneView.mutualGravity;

    if ( mutualGravity.enabled )
    {
        scene["simulation"]["world"]["mutualGravity"] = {
            { "enabled", true },
            { "gravitationalConstant", mutualGravity.gravitationalConstant },
            { "softeningLength", mutualGravity.softeningLength },
            { "elasticCollisions", mutualGravity.elasticCollisions },
        };
    }

    scene["playback"] = Json::object();
    scene["playback"]["frames"] = "unlimited";
    scene["playback"]["fixedStep"] = session.fixedStep;

    if ( session.editableScene )
    {
        scene["editor"] = {
            { "editableScene", true },
        };
    }

    scene["debug"] = Json::object();
    scene["debug"]["waterHidden"] = presentation.waterHidden;
    scene["debug"]["terrainHidden"] = presentation.terrainHidden;

    if ( session.hasFlatSlope )
    {
        scene["terrain"] = {
            { "flatSlope",
              {
                  { "baseY", session.flatBaseY },
                  { "slopeX", session.flatSlopeX },
                  { "slopeZ", session.flatSlopeZ },
              } },
        };
    }

    scene["cameras"] = Json::array();
    scene["cameras"].push_back( {
        { "name", "main" },
        { "position", Vec3Json( sceneView.cameraEye ) },
        { "view", Vec3Json( sceneView.cameraView ) },
        { "up", Vec3Json( sceneView.cameraUp ) },
    } );
    scene["objects"] = Json::array();
    Json objectMaterials = Json::array();

    scene["assetLibraries"] = Json::array();
    scene["assetInstances"] = Json::array();
    std::vector<bool> emittedAssetRows( static_cast<std::size_t>( sceneView.entities.Count() ), false );

    for ( int i = 0; i < sceneView.entities.Count(); ++i )
    {
        const SceneEntityRecord& entity = sceneView.entities.At( i );

        if ( !entity.sceneObjectId.IsValid() || entity.displayName[0] == '\0' )
        {
            SB_FATAL( "Scene/SceneSnapshotWriter",
                      "Scene entity lacks durable identity or display name at save. row=%d id=%u", i,
                      entity.sceneObjectId.value );
        }

        (void)ResolveLiveSceneRow( sceneView, i );
        const SceneBehaviorGroup& behaviorGroup = BehaviorGroupAt( sceneView, i );

        if ( entity.asset.isAssetBacked && entity.asset.partIndex == 0 )
        {
            if ( entity.asset.rootObjectId.value != entity.sceneObjectId.value || entity.asset.libraryToken[0] == '\0' ||
                 entity.asset.assetName[0] == '\0' || entity.asset.instanceName[0] == '\0' ||
                 entity.asset.partName[0] == '\0' )
            {
                SB_FATAL( "Scene/SceneSnapshotWriter", "Invalid asset-root affiliation at save. row=%d", i );
            }

            std::vector<int> partRows;

            for ( int candidate = 0; candidate < sceneView.entities.Count(); ++candidate )
            {
                const SceneEntityRecord& partEntity = sceneView.entities.At( candidate );

                if ( partEntity.asset.isAssetBacked &&
                     partEntity.asset.rootObjectId.value == entity.asset.rootObjectId.value )
                {
                    if ( !SameAssetInstance( entity.asset, partEntity.asset ) )
                    {
                        SB_FATAL( "Scene/SceneSnapshotWriter",
                                  "Asset instance affiliation disagrees across parts. root_id=%u row=%d",
                                  entity.asset.rootObjectId.value, candidate );
                    }

                    partRows.push_back( candidate );
                }
            }

            std::sort( partRows.begin(), partRows.end(), [&]( int a, int b )
                       { return sceneView.entities.At( a ).asset.partIndex < sceneView.entities.At( b ).asset.partIndex; } );

            if ( partRows.empty() )
            {
                SB_FATAL( "Scene/SceneSnapshotWriter", "Asset root has no parts. root_id=%u", entity.sceneObjectId.value );
            }

            Json instance = {
                { "asset", entity.asset.assetName },
                { "name", entity.asset.instanceName },
                { "position", Vec3Json( SkullbonezCore::Math::Vector::ZERO_VECTOR ) },
                { "parts", Json::array() },
            };

            for ( std::size_t partOrdinal = 0; partOrdinal < partRows.size(); ++partOrdinal )
            {
                const int partRow = partRows[partOrdinal];
                const SceneEntityRecord& partEntity = sceneView.entities.At( partRow );

                if ( partEntity.asset.partIndex != partOrdinal || emittedAssetRows[static_cast<std::size_t>( partRow )] )
                {
                    SB_FATAL( "Scene/SceneSnapshotWriter",
                              "Asset part order is not unique and contiguous. root_id=%u expected=%zu actual=%u",
                              entity.sceneObjectId.value, partOrdinal, partEntity.asset.partIndex );
                }

                Json partState = BuildLiveStateJson( sceneView, partRow );
                partState["name"] = partEntity.asset.partName;
                partState["objectName"] = partEntity.displayName;

                // Why: ConvexHullShape retains the baked hull's diagnostic
                // name, not the authored library token/path. Asset affiliation
                // proves this row still belongs to the recipe, so the recipe's
                // exact hull field remains authoritative on reparse.
                if ( partState["type"] == "convexHullState" )
                {
                    partState.erase( "hull" );
                }

                instance["parts"].push_back( std::move( partState ) );
                emittedAssetRows[static_cast<std::size_t>( partRow )] = true;
            }

            scene["assetInstances"].push_back( std::move( instance ) );

            const bool libraryAlreadyEmitted = std::any_of( scene["assetLibraries"].begin(), scene["assetLibraries"].end(),
                                                            [&]( const Json& value )
                                                            {
                                                                return value.is_string() &&
                                                                       value.get<std::string>() == entity.asset.libraryToken;
                                                            } );

            if ( !libraryAlreadyEmitted )
            {
                scene["assetLibraries"].push_back( entity.asset.libraryToken );
            }
        }
        else if ( !entity.asset.isAssetBacked )
        {
            scene["objects"].push_back( BuildLiveStateJson( sceneView, i ) );
        }

        const auto& material = entity.renderMaterial;

        if ( behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll &&
             ( entity.asset.isAssetBacked || ShouldSaveRenderMaterial( material ) ) )
        {
            objectMaterials.push_back( RenderMaterialJson( entity.displayName, material ) );
        }
    }

    for ( int i = 0; i < sceneView.entities.Count(); ++i )
    {
        if ( sceneView.entities.At( i ).asset.isAssetBacked && !emittedAssetRows[static_cast<std::size_t>( i )] )
        {
            SB_FATAL( "Scene/SceneSnapshotWriter", "Asset-backed entity was not emitted. row=%d", i );
        }
    }

    if ( scene["assetLibraries"].empty() )
    {
        scene.erase( "assetLibraries" );
        scene.erase( "assetInstances" );
    }

    if ( !objectMaterials.empty() )
    {
        scene["objectMaterials"] = objectMaterials;
    }

    if ( sceneView.pointJointCount < 0 || ( sceneView.pointJointCount > 0 && !sceneView.pointJoints ) )
    {
        SB_FATAL( "Scene/SceneSnapshotWriter", "Invalid point-joint save view." );
    }

    if ( sceneView.pointJointCount > 0 )
    {
        scene["ragdollJoints"] = Json::array();

        for ( int jointIndex = 0; jointIndex < sceneView.pointJointCount; ++jointIndex )
        {
            const auto& joint = sceneView.pointJoints[jointIndex];
            const int bodyAIndex = joint.BodyAIndex( sceneView.bodies );
            const int bodyBIndex = joint.BodyBIndex( sceneView.bodies );

            if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= sceneView.entities.Count() ||
                 bodyBIndex >= sceneView.entities.Count() )
            {
                SB_FATAL( "Scene/SceneSnapshotWriter", "Point joint references a missing scene body. joint=%d", jointIndex );
            }

            Json jointJson = {
                { "bodyA", sceneView.entities.At( bodyAIndex ).displayName },
                { "bodyB", sceneView.entities.At( bodyBIndex ).displayName },
                { "localAnchorA", Vec3Json( joint.localAnchorA ) },
                { "localAnchorB", Vec3Json( joint.localAnchorB ) },
                { "slack", joint.slack },
                { "stiffness", joint.stiffness },
                { "damping", joint.damping },
                { "groupId", joint.groupId },
            };

            if ( joint.flags != 0 )
            {
                jointJson["flags"] = static_cast<int>( joint.flags );
            }

            scene["ragdollJoints"].push_back( jointJson );
        }
    }

    // Why: validate and serialize every owner row before opening the target so
    // a fatal topology finding cannot truncate the previous editable scene.
    const std::string serializedScene = scene.dump( 2 );
    std::ofstream output;

    if ( !request.path || request.path[0] == '\0' || !RuntimeFileWriter::OpenTextFile( request.path, output ) )
    {
        return diagnostics.Failure( "Scene/SceneSnapshotWriter", "Failed to open scene snapshot path '%s' for writing.",
                                    request.path ? request.path : "" );
    }

    output << serializedScene << '\n';

    if ( !output.good() )
    {
        return diagnostics.Failure( "Scene/SceneSnapshotWriter", "Failed while writing scene snapshot '%s'.", request.path );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

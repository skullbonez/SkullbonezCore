/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.cpp
Purpose:
  Serializes the current scene state back into a JSON scene file.

Mental model:
  Scene snapshots serialize a live, editable scene. Cold presentation metadata
  still comes from GameModel order, while live physics state is sampled from the
  physics stores that own the current simulation frame.

Glossary:
  Scene snapshot: JSON scene emitted from current runtime state rather than the
    original authored file.
  Cold metadata: Names, render materials, and collection grouping that identify
    objects but do not drive physics integration.
  Scene object group: JSON metadata that lets multi-part object grouping
    round-trip without parsing display-name suffixes at collection append time.
  Validation gate: Repository script that proves a class of changes before
    commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Saved body pose, velocity, sleep, mass, inertia, and shape data come from
    PhysicsBodyStore/ColliderStore so scene saving does not depend on post-step
    GameModel body writeback.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneSnapshotWriter.h"

#include "../Physics/BoundingBox.h"
#include "../Physics/BoundingSphere.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/ConvexHullShape.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Runtime/Editor/EditorHullAssets.h"
#include "../GameObjects/GameModelCollection.h"
#include "../Rendering/RenderMaterial.h"
#include "../Runtime/RuntimeFileWriter.h"
#include "../World/WorldEnvironment.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <variant>
#include <vector>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetFromToken;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Basics::RuntimeFileWriter;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;

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
        { "color", Vec3Json( material.baseColor[0], material.baseColor[1], material.baseColor[2] ) },
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
    if ( material.name[0] != '\0' )
    {
        materialJson["name"] = material.name;
    }
    if ( material.kind == SkullbonezCore::Rendering::RenderMaterialKind::Emissive || material.emissiveStrength > 0.0f )
    {
        materialJson["emissive"] =
            Vec3Json( material.emissiveColor[0], material.emissiveColor[1], material.emissiveColor[2] );
        materialJson["strength"] = material.emissiveStrength;
    }
    if ( material.flags != 0 )
    {
        materialJson["flags"] = material.flags;
    }
    return materialJson;
}

void AddSceneObjectGroupJson( Json& object,
                              const GameModelCollection& collection,
                              const std::vector<GameModel>& gameModels,
                              int modelIndex )
{
    if ( collection.GroupKindAt( modelIndex ) != GameModelCollectionKind::ReleasableTree )
    {
        return;
    }

    const int rootModelIndex = collection.GroupRootModelIndexAt( modelIndex );
    if ( rootModelIndex < 0 || rootModelIndex >= static_cast<int>( gameModels.size() ) )
    {
        return;
    }

    object["objectGroup"] = {
        { "kind", "releasableTree" },
        { "root", gameModels[static_cast<std::size_t>( rootModelIndex )].GetName() },
        { "part", collection.GroupPartIndexAt( modelIndex ) },
    };
}
} // namespace


bool SceneSnapshotWriter::Save( GameModelCollection& collection,
                                const char* path,
                                bool physicsOn,
                                bool textOn,
                                WorldEnvironment& worldEnv,
                                const Vector3& camEye,
                                const Vector3& camView,
                                const Vector3& camUp,
                                bool editableScene,
                                bool fixedStep,
                                bool waterHidden,
                                bool terrainHidden,
                                bool hasFlatSlope,
                                float flatBaseY,
                                float flatSlopeX,
                                float flatSlopeZ )
{
    // Invariant: Editable scene saves emit state-form objects whose positions,
    // velocities, sleeping flags, and materials can round-trip through
    // TestSceneParser without reinterpreting authored placement offsets.
    const std::vector<GameModel>& m_gameModels = collection.Models();
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsBodyStore();
    const ColliderStore& colliderStore = collection.GetColliderStore();

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( path, output ) )
    {
        return false;
    }

    Json scene;
    scene["format"] = "skullbonez.scene.json";
    scene["version"] = 1;
    scene["simulation"] = Json::object();
    scene["simulation"]["physics"] = physicsOn;
    scene["simulation"]["text"] = textOn;
    scene["simulation"]["world"] = {
        { "gravity", worldEnv.GetGravity() },
        { "fluidHeight", worldEnv.GetFluidSurfaceHeight() },
        { "fluidDensity", worldEnv.GetFluidDensity() },
    };
    scene["playback"] = Json::object();
    scene["playback"]["frames"] = "unlimited";
    scene["playback"]["fixedStep"] = fixedStep;
    if ( editableScene )
    {
        scene["editor"] = {
            { "editableScene", true },
        };
    }
    scene["debug"] = Json::object();
    scene["debug"]["waterHidden"] = waterHidden;
    scene["debug"]["terrainHidden"] = terrainHidden;
    if ( hasFlatSlope )
    {
        scene["terrain"] = {
            { "flatSlope",
              {
                  { "baseY", flatBaseY },
                  { "slopeX", flatSlopeX },
                  { "slopeZ", flatSlopeZ },
              } },
        };
    }

    scene["cameras"] = Json::array();
    scene["cameras"].push_back( {
        { "name", "main" },
        { "position", Vec3Json( camEye ) },
        { "view", Vec3Json( camView ) },
        { "up", Vec3Json( camUp ) },
    } );
    scene["objects"] = Json::array();
    Json objectMaterials = Json::array();

    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        // Concept: Shape variants map to saved scene state records. These are
        // live simulation snapshots, not original authored spawn commands.
        const char* name = m_gameModels[i].GetName();
        char safeName[64];
        if ( !name[0] )
        {
            sprintf_s( safeName, sizeof( safeName ), "_ball_%d", i );
            name = safeName;
        }

        // Invariant: the vector index is only the compatibility row key here.
        // Live physics values must come from the stores so saving does not need
        // the post-step GameModel mirror to be fresh.
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i );
        const ColliderRecord* collider = colliderStore.RecordForHandle( colliderStore.HandleForModelIndex( i ) );
        if ( !body || !collider || collider->body != body->handle )
        {
            continue;
        }

        const Vector3& pos = body->position;
        const Vector3& vel = body->linearVelocity;
        const Vector3& avel = body->angularVelocity;
        const Vector3& ri = body->rotationalInertia;
        const auto& shape = collider->shape;
        const float mass = body->mass;
        const float rest = collider->restitution;
        const bool fixed = body->isFixed;
        const bool sleeping = body->isSleeping;

        if ( std::holds_alternative<BoundingSphere>( shape ) )
        {
            const BoundingSphere& sphere = std::get<BoundingSphere>( shape );
            scene["objects"].push_back( {
                { "type", "ballState" },
                { "name", name },
                { "position", Vec3Json( pos ) },
                { "velocity", Vec3Json( vel ) },
                { "angularVelocity", Vec3Json( avel ) },
                { "orientation", OrientationJson( body->orientation ) },
                { "radius", sphere.GetRadius() },
                { "mass", mass },
                { "restitution", rest },
                { "contactMaterial", m_gameModels[i].GetContactMaterialName() },
                { "inertia", Vec3Json( ri ) },
                { "fixed", fixed },
            } );
            if ( sleeping && !fixed )
            {
                scene["objects"].back()["sleeping"] = true;
            }
        }
        else if ( std::holds_alternative<BoundingBox>( shape ) )
        {
            const BoundingBox& box = std::get<BoundingBox>( shape );
            const Vector3& halfExtents = box.GetHalfExtents();
            scene["objects"].push_back( {
                { "type", "boxState" },
                { "name", name },
                { "position", Vec3Json( pos ) },
                { "velocity", Vec3Json( vel ) },
                { "angularVelocity", Vec3Json( avel ) },
                { "orientation", OrientationJson( body->orientation ) },
                { "halfExtents", Vec3Json( halfExtents ) },
                { "mass", mass },
                { "restitution", rest },
                { "contactMaterial", m_gameModels[i].GetContactMaterialName() },
                { "inertia", Vec3Json( ri ) },
                { "fixed", fixed },
            } );
            if ( sleeping && !fixed )
            {
                scene["objects"].back()["sleeping"] = true;
            }
        }
        else if ( std::holds_alternative<ConvexHullShape>( shape ) )
        {
            const ConvexHullShape& hull = std::get<ConvexHullShape>( shape );
            const EditorHullAsset hullAsset = EditorHullAssetFromToken( hull.GetName() );
            const char* hullToken =
                hullAsset == EditorHullAsset::UNKNOWN ? hull.GetName() : EditorHullAssetToken( hullAsset );
            Json hullState = {
                { "type", "convexHullState" },
                { "name", name },
                { "hull", hullToken },
                { "position", Vec3Json( pos ) },
                { "velocity", Vec3Json( vel ) },
                { "angularVelocity", Vec3Json( avel ) },
                { "orientation", OrientationJson( body->orientation ) },
                { "mass", mass },
                { "restitution", rest },
                { "contactMaterial", m_gameModels[i].GetContactMaterialName() },
                { "inertia", Vec3Json( ri ) },
                { "fixed", fixed },
            };
            if ( body->releasesFromFixedOnContact )
            {
                hullState["contactReleaseOnImpact"] = true;
                hullState["contactReleaseImpulseThreshold"] = body->contactReleaseImpulseThreshold;
            }
            if ( sleeping && !fixed )
            {
                hullState["sleeping"] = true;
            }
            AddSceneObjectGroupJson( hullState, collection, m_gameModels, i );
            scene["objects"].push_back( hullState );
        }

        const SkullbonezCore::Rendering::RenderMaterial& material = m_gameModels[i].GetRenderMaterial();
        if ( collection.GroupKindAt( i ) != GameModelCollectionKind::SimpleRagdoll &&
             ShouldSaveRenderMaterial( material ) )
        {
            objectMaterials.push_back( RenderMaterialJson( name, material ) );
        }
    }

    if ( !objectMaterials.empty() )
    {
        scene["objectMaterials"] = objectMaterials;
    }

    const std::vector<SkullbonezCore::Physics::PointJointConstraint>& pointJoints =
        collection.GetPointJointConstraints();
    if ( !pointJoints.empty() )
    {
        scene["ragdollJoints"] = Json::array();
        for ( const SkullbonezCore::Physics::PointJointConstraint& joint : pointJoints )
        {
            const int bodyAIndex = joint.BodyAIndex( bodyStore );
            const int bodyBIndex = joint.BodyBIndex( bodyStore );
            if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= static_cast<int>( m_gameModels.size() ) ||
                 bodyBIndex >= static_cast<int>( m_gameModels.size() ) )
            {
                continue;
            }
            Json jointJson = {
                { "bodyA", m_gameModels[static_cast<size_t>( bodyAIndex )].GetName() },
                { "bodyB", m_gameModels[static_cast<size_t>( bodyBIndex )].GetName() },
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

    output << scene.dump( 2 ) << '\n';
    return output.good();
}

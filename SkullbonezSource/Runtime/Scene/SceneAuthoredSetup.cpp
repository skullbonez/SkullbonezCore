/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
Purpose:
  Applies parsed authored scenes to runtime camera, model, and validation gate storage.

Summary:
  Authored scene setup is scene lifecycle behavior. This helper keeps the
  existing model insertion order, material targeting rules, ragdoll constraint
  flags, and required-gate resolution while moving the construction algorithms
  out of Run.

Glossary:
  Required gate: Scene-authored condition that must be observed before a
    validation run can complete.
  Hull variant: One normalized resolved hull path plus exact authored scale bits.

Invariants:
  - Scene object insertion order is validation-facing and must stay stable.
  - Parsed scene object ids, not loop order, are authoritative identity.
  - Authored hull tokens resolve through the editor hull asset table for
    compatibility with saved scenes.
  - Initial hull reservation counts distinct unit-scale variants without
    allocating a second scene identity collection.
  - Gate state is initialized here but completed by frame/runtime observation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneAuthoredSetup.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../../Assets/AssetKeys.h"
#include "SceneSessionState.h"
#include "../Camera/CameraCollection.h"
#include "../Editor/EditorHullAssets.h"
#include "SceneController.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/RotationMatrix.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/Ragdoll.h"
#include "../../Scene/AuthoredScene.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::HullShapeIdentity;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::MakeShareableHullShapeIdentity;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PointJointConstraint;
using SkullbonezCore::Physics::Ragdoll;
using SkullbonezCore::Physics::RagdollBuildOptions;
using SkullbonezCore::Physics::RagdollJointDesc;
using SkullbonezCore::Physics::RagdollPartDesc;
using SkullbonezCore::Runtime::SceneController;

constexpr float SCENE_EDITOR_TEXTURE_MODE_INVERTED = -2.0f;

// Why: Scene JSON stores authored startup angles in degrees. Convert that
// authoring unit once here, then pass a quaternion into model/body setup instead
// of asking entity to cache and hand the value back.
Quaternion MakeSceneEulerQuaternion( float eulerXDeg, float eulerYDeg, float eulerZDeg )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    const float xHalf = eulerXDeg * DEG2RAD * 0.5f;
    const float yHalf = eulerYDeg * DEG2RAD * 0.5f;
    const float zHalf = eulerZDeg * DEG2RAD * 0.5f;

    // Invariant: scene Euler degrees preserve their established world-space
    // meaning across the canonical Hamilton representation change.
    const Quaternion xRotation( -sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, -sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, -sinf( zHalf ), cosf( zHalf ) );

    Quaternion orientation;
    orientation *= xRotation * yRotation * zRotation;
    orientation.Normalise();
    return orientation;
}

uint32_t SceneContactMaterialId( const char* materialName )
{
    const char* safeName = ( materialName && materialName[0] != '\0' ) ? materialName : "default";
    return HashStr( safeName );
}

PhysicsColliderCreateDesc MakeSceneColliderDesc( CollisionShape shape, float restitution, const char* materialName )
{

    // Why: authored scene setup owns the parsed shape/material facts. Importing
    // them as a collider descriptor keeps PhysicsEngine/ColliderStore authoritative
    // for row layout instead of asking SceneController to rediscover them.
    const char* safeName = ( materialName && materialName[0] != '\0' ) ? materialName : "default";
    return MakeColliderCreateDesc( std::move( shape ), restitution, SceneContactMaterialId( safeName ), safeName );
}

PhysicsBodyCreateDesc MakeSceneBodyDesc( Physics::PhysicsSceneObjectId sceneObjectId, const CollisionShape& shape,
                                         const Vector3& position, const Quaternion& orientation,
                                         const Vector3& linearVelocity, const Vector3& angularVelocity,
                                         const Vector3& rotationalInertia, float mass, float restitution, bool fixed,
                                         const char* name )
{
    return MakePhysicsBodyCreateDesc( sceneObjectId, shape, position, orientation, linearVelocity, angularVelocity,
                                      rotationalInertia, mass, restitution,
                                      fixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic, name );
}

PhysicsColliderCreateDesc MakeSceneSphereColliderDesc( float radius, float restitution, const char* materialName )
{
    return MakeSceneColliderDesc( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ) ), restitution, materialName );
}

PhysicsColliderCreateDesc MakeSceneBoxColliderDesc( const Vector3& halfExtents, float restitution, const char* materialName )
{
    return MakeSceneColliderDesc( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ), restitution, materialName );
}

HullShapeIdentity UnitHullIdentity( const char* authoredPath )
{
    return MakeShareableHullShapeIdentity( ResolveEditorHullAssetPath( authoredPath ), Vector3( 1.0f, 1.0f, 1.0f ) );
}

int CountDistinctAuthoredHullVariants( const AuthoredScene& scene )
{
    int distinct = 0;

    for ( int index = 0; index < scene.GetConvexHullCount(); ++index )
    {
        const HullShapeIdentity identity = UnitHullIdentity( scene.GetConvexHull( index ).hullPath );
        bool seen = false;

        for ( int previous = 0; previous < index; ++previous )
        {
            seen = seen || identity == UnitHullIdentity( scene.GetConvexHull( previous ).hullPath );
        }

        distinct += seen ? 0 : 1;
    }

    for ( int index = 0; index < scene.GetConvexHullStateCount(); ++index )
    {
        const HullShapeIdentity identity = UnitHullIdentity( scene.GetConvexHullState( index ).hullPath );
        bool seen = false;

        for ( int previous = 0; previous < scene.GetConvexHullCount(); ++previous )
        {
            seen = seen || identity == UnitHullIdentity( scene.GetConvexHull( previous ).hullPath );
        }

        for ( int previous = 0; previous < index; ++previous )
        {
            seen = seen || identity == UnitHullIdentity( scene.GetConvexHullState( previous ).hullPath );
        }

        distinct += seen ? 0 : 1;
    }

    return distinct;
}

PhysicsColliderCreateDesc MakeSceneHullColliderDesc( const ConvexHullShape& hull, const char* authoredPath,
                                                     float restitution, const char* materialName )
{
    PhysicsColliderCreateDesc desc = MakeSceneColliderDesc( hull, restitution, materialName );
    desc.hullIdentity = UnitHullIdentity( authoredPath );
    return desc;
}

Vector3 ScaleSceneVector( const Vector3& value, float scale )
{
    return Vector3( value.x * scale, value.y * scale, value.z * scale );
}

SkullbonezCore::Core::SbResult AppendAuthoredSimpleRagdoll( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                            SceneWorld& sceneWorld, const RagdollBuildOptions& options )
{
    const int firstBody = sceneWorld.SceneEntityCount();
    const uint32_t groupId = static_cast<uint32_t>( firstBody + 1 );
    const float scale = Ragdoll::ClampScale( options.scale );
    Quaternion orientation = options.orientation;
    const RotationMatrix rotation = orientation.GetOrientationMatrix();
    const Vector3 base = options.terrainPoint + rotation * Vector3( 0.0f, Ragdoll::SurfaceEpsilon(), 0.0f );
    const char* prefix = options.namePrefix && options.namePrefix[0] ? options.namePrefix : "ragdoll";
    const RagdollPartDesc* parts = Ragdoll::SimpleParts();
    int jointCount = 0;
    const RagdollJointDesc* joints = Ragdoll::SimpleJoints( jointCount );

    // Invariant: the caller reserves SIMPLE_PART_COUNT ids as one range so
    // ragdoll parts append with deterministic, gap-free scene identity.

    if ( !options.firstSceneObjectId.IsValid() )
    {
        return resultDiagnostics.Failure( "Runtime/SceneAuthoredSetup", "Ragdoll build requires a scene object id range." );
    }

    char partNames[Ragdoll::SIMPLE_PART_COUNT][64] = {};

    for ( int i = 0; i < Ragdoll::SIMPLE_PART_COUNT; ++i )
    {

        if ( !Ragdoll::TryBuildSimplePartName( prefix, i, partNames[i] ) )
        {

            // Lane R: preflight the longest generated names before the first
            // append so one bad prefix cannot publish a partial ragdoll.
            return resultDiagnostics.Failure( "Runtime/SceneAuthoredSetup",
                                              "Ragdoll part name exceeds the 63-character display-name limit." );
        }
    }

    // Transaction preflight: reserve every body, box payload, and joint before
    // the first part row is published. During initial scene load this is a
    // no-op against the exact whole-scene commit; editor placement extends the
    // retained backing and logical joint allowance once.
    const SkullbonezCore::Core::SbResult
        capacityCommit = sceneWorld.ReserveAdditionalPhysicsSceneCapacity( 0, Ragdoll::SIMPLE_PART_COUNT, 0, jointCount );

    if ( !capacityCommit.Ok() )
    {
        return capacityCommit;
    }

    for ( int i = 0; i < Ragdoll::SIMPLE_PART_COUNT; ++i )
    {
        const Vector3 halfExtents = ScaleSceneVector( parts[i].halfExtents, scale );
        const BoundingBox shape( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );
        const float mass = Physics::CalculateBoxMass( halfExtents );
        const Vector3 inertia = Physics::CalculateBoxInertiaForHalfExtents( halfExtents, mass );
        const Vector3 position = base + rotation * ScaleSceneVector( parts[i].localCenter, scale );
        SceneEntityCreateDesc model;
        model.SetRenderTint( parts[i].tintR, parts[i].tintG, parts[i].tintB, 1.0f );
        const char* name = partNames[i];
        model.SetName( name );

        Physics::PhysicsSceneObjectId partSceneObjectId;
        partSceneObjectId.value = options.firstSceneObjectId.value + static_cast<uint32_t>( i );
        model.sceneObjectId = partSceneObjectId;
        model.SetBehaviorGroup( SceneBehaviorGroupKind::SimpleRagdoll, options.firstSceneObjectId, i );

        // Invariant: ragdoll grouping is prefab metadata. Pass root/part facts
        // directly so the creation transaction never parses display names to recover it.
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( model ),
                                                                   MakeSceneBodyDesc( partSceneObjectId, shape, position,
                                                                                      orientation,
                                                                                      Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                      Vector3( 0.0f, 0.0f, 0.0f ), inertia,
                                                                                      mass, parts[i].restitution,
                                                                                      options.fixed, name ),
                                                                   MakeSceneColliderDesc( shape, parts[i].restitution,
                                                                                          "default" ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }
    }

    const PhysicsBodyStore& bodyStore = sceneWorld.BodyStore();

    for ( int i = 0; i < jointCount; ++i )
    {
        PhysicsPointJointCreateDesc desc;
        desc.bodyA = bodyStore.HandleForModelIndex( firstBody + joints[i].bodyA );
        desc.bodyB = bodyStore.HandleForModelIndex( firstBody + joints[i].bodyB );
        desc.localAnchorA = ScaleSceneVector( joints[i].localAnchorA, scale );
        desc.localAnchorB = ScaleSceneVector( joints[i].localAnchorB, scale );
        desc.slack = joints[i].slack * scale;
        desc.stiffness = 0.22f;
        desc.damping = 0.35f;
        desc.groupId = groupId;
        desc.flags = joints[i].flags;
        sceneWorld.Physics().CreatePointJoint( desc );
    }

    if ( options.startsAsleep && !options.fixed )
    {

        for ( int i = 0; i < Ragdoll::SIMPLE_PART_COUNT; ++i )
        {

            // Why: authored setup already resolves body handles for joints.
            // Seed sleep through the same physics boundary instead of reopening
            // the collection's model-index command wrapper.
            sceneWorld.Physics().SeedBodyAsleep( bodyStore.HandleForModelIndex( firstBody + i ) );
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult ApplySceneBehaviorGroup( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                        const SceneObjectGroupMetadata& group,
                                                        SceneEntityCreateDesc& entity )
{

    if ( group.kind == SceneObjectGroupKind::None )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( group.kind != SceneObjectGroupKind::ReleasableTree || !group.rootObjectId.IsValid() || group.partIndex < 0 )
    {

        // Lane R: authored scene metadata can become invalid when an include or
        // editor save names a group root that cannot be resolved for this hull section.
        return resultDiagnostics.Failure( "Runtime/SceneAuthoredSetup",
                                          "Invalid authored scene object group metadata: kind=%u root_id=%u part=%d.",
                                          static_cast<unsigned int>( group.kind ), group.rootObjectId.value,
                                          group.partIndex );
    }

    entity.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, group.rootObjectId, group.partIndex );
    return SkullbonezCore::Core::SbResult::Success();
}

bool SceneNameEndsWithPartSuffix( const char* name, const char* suffix )
{

    if ( !name || !suffix )
    {
        return false;
    }

    const size_t nameLength = strlen( name );
    const size_t suffixLength = strlen( suffix );

    if ( nameLength <= suffixLength || name[nameLength - suffixLength - 1] != '_' )
    {
        return false;
    }

    return strcmp( name + nameLength - suffixLength, suffix ) == 0;
}

bool TryGetSimpleRagdollPartPrefixLength( const char* name, const char* suffix, size_t& outPrefixLength )
{
    outPrefixLength = 0;

    if ( !SceneNameEndsWithPartSuffix( name, suffix ) )
    {
        return false;
    }

    outPrefixLength = strlen( name ) - strlen( suffix );
    return true;
}

bool IsSimpleRagdollNeckJointName( const char* bodyA, const char* bodyB )
{
    size_t torsoPrefixLength = 0;
    size_t headPrefixLength = 0;
    return TryGetSimpleRagdollPartPrefixLength( bodyA, "torso", torsoPrefixLength ) &&
           TryGetSimpleRagdollPartPrefixLength( bodyB, "head", headPrefixLength ) && torsoPrefixLength == headPrefixLength &&
           strncmp( bodyA, bodyB, torsoPrefixLength ) == 0;
}

bool IsBroadMaterialTarget( const char* target )
{
    return strcmp( target, "all" ) == 0 || strcmp( target, "balls" ) == 0 || strcmp( target, "boxes" ) == 0 ||
           strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0;
}

bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material, const char* displayName,
                                 bool simpleRagdollPart, ColliderShapeKind shapeKind )
{

    // Invariant: broad scene style targets must not recolor generated ragdoll
    // body parts, but a named prefix/exact target may opt one authored ragdoll
    // into a scene-local presentation material.

    if ( simpleRagdollPart && IsBroadMaterialTarget( material.target ) )
    {
        return false;
    }

    if ( strcmp( material.target, "all" ) == 0 )
    {
        return true;
    }

    if ( strcmp( material.target, "balls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Sphere;
    }

    if ( strcmp( material.target, "boxes" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::Box;
    }

    if ( strcmp( material.target, "hulls" ) == 0 || strcmp( material.target, "convex_hulls" ) == 0 )
    {
        return shapeKind == ColliderShapeKind::ConvexHull;
    }

    if ( strncmp( material.target, "prefix:", 7 ) == 0 )
    {
        const char* prefix = material.target + 7;
        return prefix[0] != '\0' && strncmp( displayName, prefix, strlen( prefix ) ) == 0;
    }

    return strcmp( material.target, displayName ) == 0;
}

bool SceneNameStartsWith( const char* name, const char* prefix )
{
    return name && strncmp( name, prefix, strlen( prefix ) ) == 0;
}

bool IsEditorPlacedSphereName( const char* name )
{
    return SceneNameStartsWith( name, "static_ball_" ) || SceneNameStartsWith( name, "dynamic_ball_" ) ||
           SceneNameStartsWith( name, "sleeping_ball_" ) || SceneNameStartsWith( name, "static_sphere_" ) ||
           SceneNameStartsWith( name, "dynamic_sphere_" ) || SceneNameStartsWith( name, "sleeping_sphere_" );
}

void ApplyEditorPlacedSphereMaterial( SceneEntityCreateDesc& model, const char* displayName )
{

    if ( IsEditorPlacedSphereName( displayName ) )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, SCENE_EDITOR_TEXTURE_MODE_INVERTED );
    }
}

int FindModelByName( const SceneEntityStore& entities, const char* name )
{
    return entities.FindByDisplayName( name );
}

void ApplyAssetAffiliation( SceneEntityCreateDesc& entity, const AuthoredScene& scene, SceneAssetPartSource source,
                            uint32_t sourceIndex )
{

    // Why: parser provenance keeps exact shape-vector indices. Resolve that
    // cold key once during creation so steady runtime rows retain durable asset
    // identity without keeping or searching the parsed AuthoredScene.

    for ( int partRow = 0; partRow < scene.GetAssetPartCount(); ++partRow )
    {
        const SceneAssetPartRef& part = scene.GetAssetPart( partRow );

        if ( part.source != source || part.sourceIndex != sourceIndex )
        {
            continue;
        }

        for ( int instanceRow = 0; instanceRow < scene.GetAssetInstanceCount(); ++instanceRow )
        {
            const SceneAssetInstanceRecord& instance = scene.GetAssetInstance( instanceRow );
            const uint32_t row = static_cast<uint32_t>( partRow );

            if ( row < instance.firstPart || row >= instance.firstPart + instance.partCount )
            {
                continue;
            }

            const SceneAssetLibraryRef& library = scene.GetAssetLibrary( static_cast<int>( instance.libraryRefIndex ) );
            entity.SetAssetAffiliation( instance.rootSceneObjectId, library.token, instance.assetName, instance.instanceName,
                                        part.partName, part.partIndex );

            return;
        }
    }
}
} // namespace


SkullbonezCore::Core::SbResult
SceneAuthoredSetup::AppendSimpleRagdoll( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, SceneWorld& sceneWorld,
                                         const RagdollBuildOptions& options )
{
    return AppendAuthoredSimpleRagdoll( resultDiagnostics, sceneWorld, options );
}


void SceneAuthoredSetup::SetUpCameras( SceneWorld& sceneWorld, const AuthoredScene& scene )
{
    bool hasFreeCamera = false;
    Vector3 firstPosition( 900.0f, 110.0f, 900.0f );
    Vector3 firstView( 313.0f, 31.0f, 282.0f );
    Vector3 firstUp( 0.0f, 1.0f, 0.0f );

    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );

        if ( i == 0 )
        {
            firstPosition = cam.m_position;
            firstView = cam.view;
            firstUp = cam.up;
        }

        hasFreeCamera = hasFreeCamera || hash == CAMERA_FREE;
        sceneWorld.Cameras().AddCamera( cam.m_position, cam.view, cam.up, hash );
    }

    if ( !hasFreeCamera )
    {
        sceneWorld.Cameras().AddCamera( firstPosition, firstView, firstUp, CAMERA_FREE );
    }

    if ( scene.IsTerrainHidden() )
    {

        // Concept: terrain-hidden authored scenes are the terrainless/space
        // lane. Keep their default wide camera bounds and never enter terrain
        // height queries while a replay or inspection camera is tweening.
        sceneWorld.Cameras().SetTerrain( nullptr );
    }
    else
    {
        sceneWorld.Cameras().SetCameraXZBounds( sceneWorld.Terrain().Get()->GetXZBounds() );
        sceneWorld.Cameras().SetTerrain( sceneWorld.Terrain().Get() );
    }

    sceneWorld.Cameras().SetLockedMode( false );
}


SkullbonezCore::Core::SbResult
SceneAuthoredSetup::SetUpSceneEntities( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                        SceneSessionState& sceneState, SceneWorld& sceneWorld,
                                        SceneAutomationGateConfiguration& automationGates, const AuthoredScene& scene )
{
    int simpleRagdollJointCount = 0;
    (void)Ragdoll::SimpleJoints( simpleRagdollJointCount );
    const int sphereCount = scene.GetBallCount() + scene.GetBallStateCount();
    const int boxCount = scene.GetBoxCount() + scene.GetBoxStateCount() +
                         scene.GetRagdollCount() * Ragdoll::SIMPLE_PART_COUNT;

    const int hullCount = scene.GetConvexHullCount() + scene.GetConvexHullStateCount();
    const int hullVariantCapacity = CountDistinctAuthoredHullVariants( scene );
    const int bodyCount = sphereCount + boxCount + hullCount;
    const int pointJointCount = scene.GetPointJointConstraintCount() + scene.GetRagdollCount() * simpleRagdollJointCount;
    const SkullbonezCore::Core::SbResult capacityCommit = sceneWorld.CommitPhysicsSceneCapacity( bodyCount, sphereCount,
                                                                                                 boxCount, hullCount,
                                                                                                 hullVariantCapacity,
                                                                                                 pointJointCount );

    if ( !capacityCommit.Ok() )
    {
        return capacityCommit;
    }

    // Invariant: Model insertion order follows scene schema sections. Runtime
    // validation, saved editable scenes, and point-joint name resolution all
    // depend on this deterministic order.
    sceneState.modelCount = bodyCount;

    sceneWorld.Physics().ClearPointJointConstraints();

    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        SceneEntityCreateDesc entity;

        entity.SetName( ball.name );
        ApplyEditorPlacedSphereMaterial( entity, ball.name );

        const bool hasInitialImpulse = !ball.isFixed &&
                                       ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f );

        const Physics::PhysicsSceneObjectId sceneObjectId = ball.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        const BoundingSphere shape( ball.m_radius, Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = sceneWorld
                                      .TryCreateSceneEntity( std::move( entity ),
                                                             MakeSceneBodyDesc( sceneObjectId, shape,
                                                                                Vector3( ball.posX, ball.posY, ball.posZ ),
                                                                                ball.hasInitOrient
                                                                                    ? MakeSceneEulerQuaternion( ball.eulerX,
                                                                                                                ball.eulerY,
                                                                                                                ball.eulerZ )
                                                                                    : Quaternion(),
                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                Vector3( ball.moment, ball.moment,
                                                                                         ball.moment ),
                                                                                ball.m_mass, ball.restitution, ball.isFixed,
                                                                                ball.name ),
                                                             MakeSceneColliderDesc( shape, ball.restitution,
                                                                                    ball.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;

        if ( hasInitialImpulse )
        {
            sceneWorld.Physics().SetPendingBodyImpulse( body, Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                                                        Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }
    }

    // ball_state entries: full dynamic state from a snapshot

    for ( int i = 0; i < scene.GetBallStateCount(); ++i )
    {
        const SceneBallState& bs = scene.GetBallState( i );

        SceneEntityCreateDesc entity;

        entity.SetName( bs.name );
        ApplyEditorPlacedSphereMaterial( entity, bs.name );
        ApplyAssetAffiliation( entity, scene, SceneAssetPartSource::BallState, static_cast<uint32_t>( i ) );

        const Physics::PhysicsSceneObjectId sceneObjectId = bs.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        const BoundingSphere shape( bs.radius, Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( entity ),
                                                                   MakeSceneBodyDesc( sceneObjectId, shape,
                                                                                      Vector3( bs.posX, bs.posY, bs.posZ ),
                                                                                      Quaternion( bs.orientX, bs.orientY,
                                                                                                  bs.orientZ, bs.orientW ),
                                                                                      Vector3( bs.velX, bs.velY, bs.velZ ),
                                                                                      Vector3( bs.angVelX, bs.angVelY,
                                                                                               bs.angVelZ ),
                                                                                      Vector3( bs.inertiaX, bs.inertiaY,
                                                                                               bs.inertiaZ ),
                                                                                      bs.mass, bs.restitution, bs.isFixed,
                                                                                      bs.name ),
                                                                   MakeSceneColliderDesc( shape, bs.restitution,
                                                                                          bs.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;

        if ( bs.isSleeping && !bs.isFixed )
        {
            sceneWorld.Physics().SeedBodyAsleep( body );
        }
    }

    // box entries: rigid box entities

    for ( int i = 0; i < scene.GetBoxCount(); ++i )
    {
        const SceneBox& box = scene.GetBox( i );

        // Box inertia: I = m/3 * (hy^2 + hz^2) etc. for half-extents
        float hx2 = box.halfX * box.halfX;
        float hy2 = box.halfY * box.halfY;
        float hz2 = box.halfZ * box.halfZ;
        float m3 = box.mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        SceneEntityCreateDesc entity;

        entity.SetName( box.name );

        const Physics::PhysicsSceneObjectId sceneObjectId = box.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        const BoundingBox shape( Vector3( box.halfX, box.halfY, box.halfZ ), Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = sceneWorld
                                      .TryCreateSceneEntity( std::move( entity ),
                                                             MakeSceneBodyDesc( sceneObjectId, shape,
                                                                                Vector3( box.posX, box.posY, box.posZ ),
                                                                                box.hasInitOrient
                                                                                    ? MakeSceneEulerQuaternion( box.eulerX,
                                                                                                                box.eulerY,
                                                                                                                box.eulerZ )
                                                                                    : Quaternion(),
                                                                                box.hasInitVelocity
                                                                                    ? Vector3( box.velX, box.velY, box.velZ )
                                                                                    : Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                Vector3( 0.0f, 0.0f, 0.0f ), inertia,
                                                                                box.mass, box.restitution, box.isFixed,
                                                                                box.name ),
                                                             MakeSceneColliderDesc( shape, box.restitution,
                                                                                    box.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }
    }

    // box_state entries: full dynamic state from an editable scene snapshot

    for ( int i = 0; i < scene.GetBoxStateCount(); ++i )
    {
        const SceneBoxState& box = scene.GetBoxState( i );

        SceneEntityCreateDesc entity;

        entity.SetName( box.name );
        ApplyAssetAffiliation( entity, scene, SceneAssetPartSource::BoxState, static_cast<uint32_t>( i ) );

        const Physics::PhysicsSceneObjectId sceneObjectId = box.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        const BoundingBox shape( Vector3( box.halfX, box.halfY, box.halfZ ), Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto
            appendResult = sceneWorld
                               .TryCreateSceneEntity( std::move( entity ),
                                                      MakeSceneBodyDesc( sceneObjectId, shape,
                                                                         Vector3( box.posX, box.posY, box.posZ ),
                                                                         Quaternion( box.orientX, box.orientY, box.orientZ,
                                                                                     box.orientW ),
                                                                         Vector3( box.velX, box.velY, box.velZ ),
                                                                         Vector3( box.angVelX, box.angVelY, box.angVelZ ),
                                                                         Vector3( box.inertiaX, box.inertiaY, box.inertiaZ ),
                                                                         box.mass, box.restitution, box.isFixed, box.name ),
                                                      MakeSceneColliderDesc( shape, box.restitution, box.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;

        if ( box.isSleeping && !box.isFixed )
        {
            sceneWorld.Physics().SeedBodyAsleep( body );
        }
    }

    // convex_hull entries: authored immutable hull assets

    for ( int i = 0; i < scene.GetConvexHullCount(); ++i )
    {
        const SceneConvexHull& hullScene = scene.GetConvexHull( i );
        ConvexHullShape hull;
        SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( resultDiagnostics,
                                                                                    ResolveEditorHullAssetPath( hullScene.hullPath ),
                                                                                    hull );

        if ( !hullLoad.Ok() )
        {
            return hullLoad;
        }

        const Vector3 inertia = hull.ComputeBoxApproxInertia( hullScene.mass );
        const Vector3 authoredPosition( hullScene.posX, hullScene.posY, hullScene.posZ );

        SceneEntityCreateDesc entity;

        entity.SetName( hullScene.name );
        ApplyAssetAffiliation( entity, scene, SceneAssetPartSource::ConvexHull, static_cast<uint32_t>( i ) );

        Quaternion hullQuaternion;

        // Invariant: asset hierarchy composition has already produced an exact
        // quaternion. Euler remains only for ordinary version-1 authored hulls.

        if ( hullScene.hasInitQuaternionOrient )
        {
            hullQuaternion = Quaternion( hullScene.orientX, hullScene.orientY, hullScene.orientZ, hullScene.orientW );
            hullQuaternion.Normalise();
        }
        else if ( hullScene.hasInitOrient )
        {
            hullQuaternion = MakeSceneEulerQuaternion( hullScene.eulerX, hullScene.eulerY, hullScene.eulerZ );
        }

        const RotationMatrix hullOrientation = hullQuaternion.GetOrientationMatrix();
        const Vector3 bodyPosition = authoredPosition + hullOrientation * hull.GetAuthoredCenterOfMass();

        // Invariant: parsed scene grouping crosses the construction edge as a
        // stable root id and stays separate from asset affiliation.
        const SkullbonezCore::Core::SbResult groupResult = ApplySceneBehaviorGroup( resultDiagnostics, hullScene.group,
                                                                                    entity );

        if ( !groupResult.Ok() )
        {
            return groupResult;
        }

        const Physics::PhysicsSceneObjectId sceneObjectId = hullScene.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        PhysicsBodyCreateDesc bodyDesc = MakeSceneBodyDesc( sceneObjectId, hull, bodyPosition, hullQuaternion,
                                                            hullScene.hasInitVelocity
                                                                ? Vector3( hullScene.velX, hullScene.velY, hullScene.velZ )
                                                                : Vector3( 0.0f, 0.0f, 0.0f ),
                                                            hullScene.hasInitAngularVelocity
                                                                ? Vector3( hullScene.angVelX, hullScene.angVelY,
                                                                           hullScene.angVelZ )
                                                                : Vector3( 0.0f, 0.0f, 0.0f ),
                                                            inertia, hullScene.mass, hullScene.restitution,
                                                            hullScene.isFixed, hullScene.name );

        bodyDesc.releasesFromFixedOnContact = hullScene.contactReleaseOnImpact;
        bodyDesc.contactReleaseImpulseThreshold = hullScene.contactReleaseImpulseThreshold;
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( entity ), bodyDesc,
                                                                   MakeSceneHullColliderDesc( hull, hullScene.hullPath,
                                                                                              hullScene.restitution,
                                                                                              hullScene.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;

        if ( hullScene.isSleeping && !hullScene.isFixed )
        {
            sceneWorld.Physics().SeedBodyAsleep( body );
        }
    }

    // Invariant: convex_hull_state entries come from editable scene snapshots.
    // The writer stores body-store position, the simulated body/COM position,
    // so do not add the authored hull COM here.

    for ( int i = 0; i < scene.GetConvexHullStateCount(); ++i )
    {
        const SceneConvexHullState& hullScene = scene.GetConvexHullState( i );
        ConvexHullShape hull;
        SkullbonezCore::Core::SbResult hullLoad = ConvexHullShape::TryLoadFromFile( resultDiagnostics,
                                                                                    ResolveEditorHullAssetPath( hullScene.hullPath ),
                                                                                    hull );

        if ( !hullLoad.Ok() )
        {
            return hullLoad;
        }

        SceneEntityCreateDesc entity;

        entity.SetName( hullScene.name );
        ApplyAssetAffiliation( entity, scene, SceneAssetPartSource::ConvexHullState, static_cast<uint32_t>( i ) );
        const SkullbonezCore::Core::SbResult groupResult = ApplySceneBehaviorGroup( resultDiagnostics, hullScene.group,
                                                                                    entity );

        if ( !groupResult.Ok() )
        {
            return groupResult;
        }

        const Physics::PhysicsSceneObjectId sceneObjectId = hullScene.sceneObjectId;
        entity.sceneObjectId = sceneObjectId;
        PhysicsBodyCreateDesc bodyDesc = MakeSceneBodyDesc( sceneObjectId, hull,
                                                            Vector3( hullScene.posX, hullScene.posY, hullScene.posZ ),
                                                            Quaternion( hullScene.orientX, hullScene.orientY,
                                                                        hullScene.orientZ, hullScene.orientW ),
                                                            Vector3( hullScene.velX, hullScene.velY, hullScene.velZ ),
                                                            Vector3( hullScene.angVelX, hullScene.angVelY,
                                                                     hullScene.angVelZ ),
                                                            Vector3( hullScene.inertiaX, hullScene.inertiaY,
                                                                     hullScene.inertiaZ ),
                                                            hullScene.mass, hullScene.restitution, hullScene.isFixed,
                                                            hullScene.name );

        bodyDesc.releasesFromFixedOnContact = hullScene.contactReleaseOnImpact;
        bodyDesc.contactReleaseImpulseThreshold = hullScene.contactReleaseImpulseThreshold;
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( entity ), bodyDesc,
                                                                   MakeSceneHullColliderDesc( hull, hullScene.hullPath,
                                                                                              hullScene.restitution,
                                                                                              hullScene.contactMaterial ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;

        if ( hullScene.isSleeping && !hullScene.isFixed )
        {
            sceneWorld.Physics().SeedBodyAsleep( body );
        }
    }

    for ( int i = 0; i < scene.GetRagdollCount(); ++i )
    {
        const SceneRagdoll& ragdollScene = scene.GetRagdoll( i );
        RagdollBuildOptions options;
        options.namePrefix = ragdollScene.name;
        options.terrainPoint = Vector3( ragdollScene.posX, ragdollScene.posY, ragdollScene.posZ );
        options.scale = ragdollScene.scale;
        options.fixed = ragdollScene.isFixed;
        options.startsAsleep = ragdollScene.startsAsleep;
        options.firstSceneObjectId = ragdollScene.firstSceneObjectId;

        if ( ragdollScene.hasInitOrient )
        {
            options.orientation = MakeSceneEulerQuaternion( ragdollScene.eulerX, ragdollScene.eulerY, ragdollScene.eulerZ );
        }

        const SkullbonezCore::Core::SbResult ragdollResult = AppendSimpleRagdoll( resultDiagnostics, sceneWorld, options );

        if ( !ragdollResult.Ok() )
        {
            return ragdollResult;
        }
    }

    const PhysicsBodyStore& bodyStore = sceneWorld.BodyStore();

    for ( int i = 0; i < scene.GetPointJointConstraintCount(); ++i )
    {
        const ScenePointJointConstraint& sceneJoint = scene.GetPointJointConstraint( i );
        PhysicsPointJointCreateDesc joint;
        const int bodyAIndex = FindModelByName( sceneWorld.Entities(), sceneJoint.bodyA );
        const int bodyBIndex = FindModelByName( sceneWorld.Entities(), sceneJoint.bodyB );

        if ( bodyAIndex < 0 || bodyBIndex < 0 )
        {
            fprintf( stderr, "[scene] ragdoll_joint could not resolve '%s' <-> '%s'\n", sceneJoint.bodyA, sceneJoint.bodyB );

            continue;
        }

        joint.bodyA = bodyStore.HandleForModelIndex( bodyAIndex );
        joint.bodyB = bodyStore.HandleForModelIndex( bodyBIndex );
        joint.localAnchorA = sceneJoint.localAnchorA;
        joint.localAnchorB = sceneJoint.localAnchorB;
        joint.slack = sceneJoint.slack;
        joint.stiffness = sceneJoint.stiffness;
        joint.damping = sceneJoint.damping;
        joint.groupId = sceneJoint.groupId;
        joint.flags = sceneJoint.flags;

        if ( IsSimpleRagdollNeckJointName( sceneJoint.bodyA, sceneJoint.bodyB ) )
        {
            joint.flags |= PointJointConstraint::FLAG_LIMIT_NECK_SWING;
        }

        sceneWorld.Physics().CreatePointJoint( joint );
    }

    for ( int materialIndex = 0; materialIndex < scene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {

        // Why: Material overrides are applied after all bodies exist so prefix
        // and exact-name targets can hit authored objects, generated ragdolls,
        // and snapshot bodies uniformly.
        const SceneObjectMaterialOverride& material = scene.GetObjectMaterialOverride( materialIndex );
        const auto colliders = sceneWorld.Colliders().Records();

        for ( int modelIndex = 0; modelIndex < sceneWorld.SceneEntityCount(); ++modelIndex )
        {
            const ColliderShapeKind shapeKind = modelIndex < static_cast<int>( colliders.size() )
                                                    ? colliders[static_cast<std::size_t>( modelIndex )].shapeKind
                                                    : ColliderShapeKind::Sphere;

            if ( SceneMaterialTargetMatches( material, sceneWorld.Entities().At( modelIndex ).displayName,
                                             sceneWorld.Entities().IsSimpleRagdollPart( modelIndex ), shapeKind ) )
            {
                sceneWorld.Entities().MutableAt( modelIndex ).renderMaterial = material.material;
            }
        }
    }

    // Invariant: runtime-created objects continue after the highest authored id,
    // even when schema v2 deliberately uses sparse/non-contiguous values.
    sceneState.ResetSceneObjectIdCursor( sceneWorld.BodyStore() );
    SetUpRequiredContacts( sceneWorld, automationGates, scene );
    SetUpRequiredBroadphaseXCells( automationGates, scene );
    return SkullbonezCore::Core::SbResult::Success();
}


void SceneAuthoredSetup::SetUpRequiredContacts( SceneWorld& sceneWorld, SceneAutomationGateConfiguration& automationGates,
                                                const AuthoredScene& scene )
{

    // Lifetime: Required contacts store body indices resolved for this load.
    // Scene reloads must rebuild them because model storage is recreated.
    automationGates.ReserveRequiredContacts( static_cast<std::size_t>( scene.GetRequiredContactCount() ) );

    for ( int i = 0; i < scene.GetRequiredContactCount(); ++i )
    {
        const SceneRequiredContact& contact = scene.GetRequiredContact( i );
        const int bodyA = FindModelByName( sceneWorld.Entities(), contact.nameA );
        const int bodyB = FindModelByName( sceneWorld.Entities(), contact.nameB );

        if ( bodyA < 0 || bodyB < 0 )
        {
            fprintf( stderr, "[scene] required_contact could not resolve '%s' <-> '%s'\n", contact.nameA, contact.nameB );
        }

        automationGates.AppendRequiredContact( contact.nameA, contact.nameB, bodyA, bodyB );
    }
}


void SceneAuthoredSetup::SetUpRequiredBroadphaseXCells( SceneAutomationGateConfiguration& automationGates,
                                                        const AuthoredScene& scene )
{
    automationGates.ReserveRequiredBroadphaseXCells( static_cast<std::size_t>( scene.GetRequiredBroadphaseXCellCount() ) );

    for ( int i = 0; i < scene.GetRequiredBroadphaseXCellCount(); ++i )
    {
        const SceneRequiredBroadphaseXCells& sceneCells = scene.GetRequiredBroadphaseXCell( i );
        automationGates.AppendRequiredBroadphaseXCells( sceneCells.minCellX, sceneCells.maxCellX, sceneCells.cellY,
                                                        sceneCells.cellZ );
    }
}

} // namespace Runtime
} // namespace SkullbonezCore

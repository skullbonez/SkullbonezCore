/*
File: SkullbonezSource/Runtime/App/StartupProbeApplication.cpp
Purpose:
  Applies command-line early-exit probes that construct runtime owners.

Summary:
  App owns early-exit probes that construct Scene and other runtime owners.
  Atlas generation and engine physics/runtime-handle validation still execute
  before the ordinary window, renderer, worker, and Run owners are constructed.

Glossary:
  Engine lifecycle smoke: Two fresh PhysicsEngine scenarios whose exact state
    hashes must match before validation may pass.
  Runtime mirror: Scene/runtime stores that must retain the same typed physics
    handles as the public physics API.

Invariants:
  - Output field names, output files, exit codes, and call positions are startup
    compatibility surfaces; new probes append named fields without changing
    the meaning of existing fields.
  - Probe state is local to one synchronous invocation and is never retained.
  - Physics probes finish before ordinary runtime ownership begins.

Related:
  - SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h
  - SkullbonezSource/Runtime/Startup/StartupCommandLine.h
  - Agentic/Reference/engine-glossary.md
*/
#include "../Startup/StartupProbeHarnesses.h"
#include "../Startup/StartupCommandLine.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Common.h"
#include "../../Core/Config.h"
#include "../../Core/LockOrderValidator.h"
#include "../../Core/WorkerPool.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsWorldForces.h"
#include "../../World/Terrain.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/Text.h"
#include "../../World/WorldEnvironment.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneAuthoredSetup.h"
#include "../Scene/SceneGeneratedSetup.h"
#include "../../Core/WindowConstants.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Physics;
namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
namespace
{
constexpr uint64_t PHYSICS_SMOKE_FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t PHYSICS_SMOKE_FNV_PRIME = 1099511628211ull;
constexpr uint64_t PHYSICS_ENGINE_LIFECYCLE_EXPECTED_HASH = 0x6B91536A6023A15Full;

uint64_t HashPhysicsSmokeU32( uint64_t hash, uint32_t value )
{
    for ( uint32_t shift = 0u; shift < 32u; shift += 8u )
    {
        hash ^= static_cast<uint8_t>( value >> shift );
        hash *= PHYSICS_SMOKE_FNV_PRIME;
    }

    return hash;
}

uint64_t HashPhysicsSmokeFloat( uint64_t hash, float value )
{
    uint32_t bits = 0u;
    std::memcpy( &bits, &value, sizeof( bits ) );
    return HashPhysicsSmokeU32( hash, bits );
}

uint64_t HashPhysicsSmokeVector( uint64_t hash, const Math::Vector::Vector3& value )
{
    hash = HashPhysicsSmokeFloat( hash, value.x );
    hash = HashPhysicsSmokeFloat( hash, value.y );
    return HashPhysicsSmokeFloat( hash, value.z );
}

uint64_t HashPhysicsSmokeHandle( uint64_t hash, PhysicsBodyHandle handle )
{
    hash = HashPhysicsSmokeU32( hash, handle.index );
    return HashPhysicsSmokeU32( hash, handle.generation );
}

uint64_t HashPhysicsSmokeHandle( uint64_t hash, PhysicsColliderHandle handle )
{
    hash = HashPhysicsSmokeU32( hash, handle.index );
    return HashPhysicsSmokeU32( hash, handle.generation );
}

uint64_t HashPhysicsSmokeHandle( uint64_t hash, PhysicsConstraintHandle handle )
{
    hash = HashPhysicsSmokeU32( hash, handle.index );
    return HashPhysicsSmokeU32( hash, handle.generation );
}

struct PhysicsEngineLifecycleScenarioResult
{
    bool passed = false;
    bool lifecycleChecksPassed = false;
    bool activationChecksPassed = false;
    bool rayCastHit = false;
    uint32_t bodyCount = 0u;
    uint32_t colliderCount = 0u;
    uint32_t broadphaseQueryCount = 0u;
    uint32_t contactCount = 0u;
    uint32_t islandStateCount = 0u;
    uint32_t stepCount = 0u;
    uint64_t deterministicHash = 0u;
    Math::Vector::Vector3 finalPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 finalLinearVelocity = Math::Vector::ZERO_VECTOR;
};

PhysicsAuthoredBodyRegistration RegisterPhysicsSmokeBody( PhysicsEngine& engine, uint32_t sceneObjectValue,
                                                          const Math::Vector::Vector3& position,
                                                          const Math::Vector::Vector3& velocity,
                                                          PhysicsBodyMotionKind motionKind )
{
    const PhysicsSceneObjectId sceneObjectId { sceneObjectValue };
    const Math::CollisionDetection::BoundingSphere shape( 1.0f, Math::Vector::ZERO_VECTOR );
    PhysicsBodyCreateDesc body = MakePhysicsBodyCreateDesc( sceneObjectId, shape, position,
                                                            Math::Orientation::IDENTITY_QUATERNION, velocity,
                                                            Math::Vector::ZERO_VECTOR,
                                                            Math::Vector::Vector3( 2.0f, 2.0f, 2.0f ), 2.0f, 0.0f,
                                                            motionKind, "physics_engine_smoke" );

    PhysicsColliderCreateDesc collider = MakeColliderCreateDesc( shape, 0.0f, HashStr( "default" ) );
    collider.sceneObjectId = sceneObjectId;
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    return engine.RegisterAuthoredBody( body, std::move( collider ) );
}

PhysicsEngineLifecycleScenarioResult RunPhysicsEngineLifecycleScenario()
{
    // Lifetime: PhysicsEngine retains the terrain view across Clear(). Declare
    // config, terrain, then the cold heap engine so reverse destruction retires
    // the borrower before both retained-view owners.
    Core::EngineConfig config;
    Geometry::Terrain terrain( 0.0f, 0.0f, 0.0f, config );
    auto engineOwner = std::make_unique<PhysicsEngine>();
    PhysicsEngine& engine = *engineOwner;
    engine.ApplyRuntimeConfig( config );
    engine.Clear();
    engine.SetTerrainView( terrain.PhysicsView() );

    {
        Core::Allocation::RuntimeAllocationScope sceneLoadScope( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( 3u, 3u, 0u, 0u, 2u );
    }

    const PhysicsAuthoredBodyRegistration fixed = RegisterPhysicsSmokeBody( engine, 71u,
                                                                            Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                                                                            Math::Vector::ZERO_VECTOR,
                                                                            PhysicsBodyMotionKind::Fixed );

    const PhysicsAuthoredBodyRegistration dynamic = RegisterPhysicsSmokeBody( engine, 72u,
                                                                              Math::Vector::Vector3( 4.0f, 2.0f, 0.0f ),
                                                                              Math::Vector::Vector3( 1.0f, 0.0f, 0.0f ),
                                                                              PhysicsBodyMotionKind::Dynamic );

    const PhysicsAuthoredBodyRegistration transient = RegisterPhysicsSmokeBody( engine, 73u,
                                                                                Math::Vector::Vector3( 12.0f, 0.0f, 0.0f ),
                                                                                Math::Vector::ZERO_VECTOR,
                                                                                PhysicsBodyMotionKind::Dynamic );

    const bool created = fixed.IsValid() && dynamic.IsValid() && transient.IsValid();

    PhysicsBodyUpdateDesc bodyUpdate;
    bodyUpdate.body = dynamic.body;
    bodyUpdate.updateMask = PHYSICS_BODY_UPDATE_POSE | PHYSICS_BODY_UPDATE_VELOCITY | PHYSICS_BODY_UPDATE_MASS;
    bodyUpdate.position = Math::Vector::Vector3( 3.0f, 2.0f, 0.0f );
    bodyUpdate.linearVelocity = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    bodyUpdate.mass = 2.0f;
    bodyUpdate.rotationalInertia = Math::Vector::Vector3( 2.0f, 2.0f, 2.0f );
    const Math::CollisionDetection::BoundingSphere updatedShape( 1.25f, Math::Vector::ZERO_VECTOR );
    const bool updatedBodyAndCollider = engine.UpdateAuthoredBodyAndCollider( bodyUpdate,
                                                                              MakeColliderCreateDesc( updatedShape, 0.2f,
                                                                                                      HashStr( "default" ) ) );

    PhysicsPointJointCreateDesc firstJointDesc;
    firstJointDesc.bodyA = fixed.body;
    firstJointDesc.bodyB = dynamic.body;
    const PhysicsConstraintHandle firstJoint = engine.CreatePointJoint( firstJointDesc );
    PhysicsPointJointCreateDesc survivorJointDesc;
    survivorJointDesc.bodyA = dynamic.body;
    survivorJointDesc.bodyB = transient.body;
    const PhysicsConstraintHandle survivorJoint = engine.CreatePointJoint( survivorJointDesc );
    PhysicsPointJointUpdateDesc jointUpdate;
    jointUpdate.constraint = survivorJoint;
    jointUpdate.updateMask = PHYSICS_POINT_JOINT_UPDATE_ANCHORS | PHYSICS_POINT_JOINT_UPDATE_SOLVER |
                             PHYSICS_POINT_JOINT_UPDATE_GROUP;

    jointUpdate.localAnchorA = Math::Vector::Vector3( 0.5f, 0.0f, 0.0f );
    jointUpdate.localAnchorB = Math::Vector::Vector3( -0.5f, 0.0f, 0.0f );
    jointUpdate.slack = 0.5f;
    jointUpdate.stiffness = 0.4f;
    jointUpdate.damping = 0.3f;
    jointUpdate.groupId = 17u;
    jointUpdate.flags = 3u;
    const bool updatedJoint = engine.UpdatePointJoint( jointUpdate );
    const bool destroyedFirstJoint = engine.DestroyConstraint( firstJoint );
    const auto& compactedJoints = PhysicsEngine::ReadPointJointConstraints( engine );
    PointJointConstraint survivorRecord;
    const bool survivorRecordCaptured = compactedJoints.size() == 1u;

    if ( survivorRecordCaptured )
    {
        survivorRecord = compactedJoints[0];
    }

    // Invariant: verify the live compacted row, not the input packet. Otherwise
    // a field-specific update regression could repeat deterministically and
    // leave the smoke hash unchanged.
    const bool survivorHandleStable = survivorRecordCaptured && survivorRecord.handle == survivorJoint &&
                                      survivorRecord.bodyA == survivorJointDesc.bodyA &&
                                      survivorRecord.bodyB == survivorJointDesc.bodyB &&
                                      survivorRecord.localAnchorA == jointUpdate.localAnchorA &&
                                      survivorRecord.localAnchorB == jointUpdate.localAnchorB &&
                                      survivorRecord.slack == jointUpdate.slack &&
                                      survivorRecord.stiffness == jointUpdate.stiffness &&
                                      survivorRecord.damping == jointUpdate.damping &&
                                      survivorRecord.groupId == jointUpdate.groupId &&
                                      survivorRecord.flags == jointUpdate.flags;

    const bool destroyedSurvivorJoint = engine.DestroyConstraint( survivorJoint );
    const bool staleConstraintRejected = !engine.UpdatePointJoint( jointUpdate ) &&
                                         !engine.DestroyConstraint( survivorJoint );

    const PhysicsConstraintHandle cascadeJoint = engine.CreatePointJoint( firstJointDesc );
    const bool destroyedFixedBody = engine.DestroyAuthoredBody( fixed.body );
    PhysicsBodyUpdateDesc staleBodyUpdate;
    staleBodyUpdate.body = fixed.body;
    staleBodyUpdate.updateMask = PHYSICS_BODY_UPDATE_POSE;
    PhysicsPointJointUpdateDesc cascadeStaleUpdate;
    cascadeStaleUpdate.constraint = cascadeJoint;
    const bool cascadeLifecycle = cascadeJoint.IsValid() && destroyedFixedBody &&
                                  !engine.UpdateAuthoredBody( staleBodyUpdate ) &&
                                  !PhysicsEngine::ReadBodies( engine ).Contains( fixed.body ) &&
                                  PhysicsEngine::ReadColliders( engine ).RecordForHandle( fixed.collider ) == nullptr &&
                                  PhysicsEngine::ReadPointJointConstraints( engine ).empty() &&
                                  !engine.UpdatePointJoint( cascadeStaleUpdate ) &&
                                  !engine.DestroyConstraint( cascadeJoint );

    PhysicsPointJointCreateDesc clearJointDesc;
    clearJointDesc.bodyA = dynamic.body;
    clearJointDesc.bodyB = transient.body;
    const PhysicsConstraintHandle clearJoint = engine.CreatePointJoint( clearJointDesc );
    PhysicsPointJointUpdateDesc clearStaleUpdate;
    clearStaleUpdate.constraint = clearJoint;
    engine.ClearPointJointConstraints();
    const bool clearLifecycle = clearJoint.IsValid() && PhysicsEngine::ReadPointJointConstraints( engine ).empty() &&
                                !engine.UpdatePointJoint( clearStaleUpdate ) && !engine.DestroyConstraint( clearJoint );

    engine.SeedBodyAsleep( dynamic.body );
    const int dynamicRow = PhysicsEngine::ReadBodies( engine ).ModelIndexForHandle( dynamic.body );
    bool seededAsleep = false;

    if ( dynamicRow >= 0 )
    {
        seededAsleep = PhysicsEngine::ReadBodies( engine ).HotFields().awake[static_cast<std::size_t>( dynamicRow )] == 0u;
    }

    engine.SetSleepEnabled( false );
    const bool disablingSleepWokeBody = dynamicRow >= 0 && PhysicsEngine::ReadBodies( engine )
                                                                   .HotFields()
                                                                   .awake[static_cast<std::size_t>( dynamicRow )] != 0u;

    engine.SetSleepEnabled( true );
    engine.SeedBodyAsleep( dynamic.body );
    engine.WakeBody( dynamic.body );
    const bool explicitWakeWorked = dynamicRow >= 0 && PhysicsEngine::ReadBodies( engine )
                                                               .HotFields()
                                                               .awake[static_cast<std::size_t>( dynamicRow )] != 0u;

    engine.ApplyBodyImpulse( dynamic.body, Math::Vector::Vector3( 2.0f, 0.0f, 0.0f ), Math::Vector::ZERO_VECTOR );
    Threading::LockOrderValidator lockOrderValidator;
    Threading::WorkerPool inlineWorkers( lockOrderValidator );
    PhysicsWorldForces forces;
    engine.Step( 0.25f, forces, inlineWorkers, PhysicsDiagnosticsCsvWriter {} );

    PhysicsEngineLifecycleScenarioResult result;
    result.stepCount = 1u;
    result.activationChecksPassed = seededAsleep && disablingSleepWokeBody && explicitWakeWorked;
    const PhysicsBodyStore& bodies = PhysicsEngine::ReadBodies( engine );
    const ColliderStore& colliders = PhysicsEngine::ReadColliders( engine );
    result.bodyCount = static_cast<uint32_t>( bodies.Count() );
    result.colliderCount = static_cast<uint32_t>( colliders.Count() );
    const int finalDynamicRow = bodies.ModelIndexForHandle( dynamic.body );

    if ( finalDynamicRow >= 0 )
    {
        const std::size_t row = static_cast<std::size_t>( finalDynamicRow );
        result.finalPosition = PhysicsBodyPosition( bodies.HotFields(), row );
        result.finalLinearVelocity = PhysicsBodyLinearVelocity( bodies.HotFields(), row );
    }

    PhysicsRayCastDesc ray;
    ray.origin = result.finalPosition - Math::Vector::Vector3( 0.0f, 0.0f, 5.0f );
    ray.direction = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    ray.maxDistance = 10.0f;
    ray.includeFixedBodies = false;
    ray.includeSleepingBodies = false;
    const PhysicsRayCastHit rayHit = engine.RayCast( ray );
    result.rayCastHit = rayHit.hit && rayHit.body == dynamic.body && rayHit.collider == dynamic.collider;

    PhysicsBroadphaseCellQueryDesc broadphase;
    const Math::Vector::Vector3 queryHalfExtents( 0.1f, 0.1f, 0.1f );
    broadphase.min = result.finalPosition - queryHalfExtents;
    broadphase.max = result.finalPosition + queryHalfExtents;
    broadphase.includeFixedBodies = false;
    broadphase.includeSleepingBodies = false;
    const PhysicsBroadphaseQueryResultView broadphaseView = engine.QueryBroadphaseCells( broadphase );
    result.broadphaseQueryCount = broadphaseView.bodyCount;
    const bool broadphaseMatched = broadphaseView.bodyCount == 1u && broadphaseView.bodies &&
                                   broadphaseView.bodies[0] == dynamic.body;

    const PhysicsDiagnosticsView diagnostics = engine.GetDiagnosticsView();
    result.contactCount = static_cast<uint32_t>( diagnostics.persistentContacts.size() );
    result.islandStateCount = static_cast<uint32_t>( diagnostics.sleepIslandVisualId.size() );

    uint64_t hash = PHYSICS_SMOKE_FNV_OFFSET_BASIS;

    for ( std::size_t row = 0; row < bodies.Records().size(); ++row )
    {
        const PhysicsBodyRecord& body = bodies.Records()[row];
        hash = HashPhysicsSmokeHandle( hash, body.handle );
        hash = HashPhysicsSmokeU32( hash, body.sceneObjectId.value );
        hash = HashPhysicsSmokeFloat( hash, body.mass );
        hash = HashPhysicsSmokeVector( hash, PhysicsBodyPosition( bodies.HotFields(), row ) );
        hash = HashPhysicsSmokeVector( hash, PhysicsBodyLinearVelocity( bodies.HotFields(), row ) );
        hash = HashPhysicsSmokeU32( hash, bodies.HotFields().awake[row] );
    }

    for ( const ColliderRecord& collider : colliders.Records() )
    {
        hash = HashPhysicsSmokeHandle( hash, collider.handle );
        hash = HashPhysicsSmokeHandle( hash, collider.body );
        hash = HashPhysicsSmokeU32( hash, collider.sceneObjectId.value );
        hash = HashPhysicsSmokeFloat( hash, collider.boundingRadius );
        hash = HashPhysicsSmokeFloat( hash, collider.restitution );
    }

    hash = HashPhysicsSmokeHandle( hash, survivorRecord.handle );
    hash = HashPhysicsSmokeHandle( hash, survivorRecord.bodyA );
    hash = HashPhysicsSmokeHandle( hash, survivorRecord.bodyB );
    hash = HashPhysicsSmokeVector( hash, survivorRecord.localAnchorA );
    hash = HashPhysicsSmokeVector( hash, survivorRecord.localAnchorB );
    hash = HashPhysicsSmokeFloat( hash, survivorRecord.slack );
    hash = HashPhysicsSmokeFloat( hash, survivorRecord.stiffness );
    hash = HashPhysicsSmokeFloat( hash, survivorRecord.damping );
    hash = HashPhysicsSmokeU32( hash, survivorRecord.groupId );
    hash = HashPhysicsSmokeU32( hash, survivorRecord.flags );
    hash = HashPhysicsSmokeHandle( hash, rayHit.body );
    hash = HashPhysicsSmokeHandle( hash, rayHit.collider );
    hash = HashPhysicsSmokeFloat( hash, rayHit.distance );
    hash = HashPhysicsSmokeU32( hash, broadphaseView.bodyCount );

    for ( uint32_t index = 0u; index < broadphaseView.bodyCount; ++index )
    {
        hash = HashPhysicsSmokeHandle( hash, broadphaseView.bodies[index] );
    }

    hash = HashPhysicsSmokeU32( hash, result.contactCount );
    hash = HashPhysicsSmokeU32( hash, result.islandStateCount );

    for ( const PersistentContact& contact : diagnostics.persistentContacts )
    {
        hash = HashPhysicsSmokeU32( hash, static_cast<uint32_t>( contact.bodyA ) );
        hash = HashPhysicsSmokeU32( hash, static_cast<uint32_t>( contact.bodyB ) );
        hash = HashPhysicsSmokeU32( hash, contact.featureId );
        hash = HashPhysicsSmokeU32( hash, static_cast<uint32_t>( contact.key ) );
        hash = HashPhysicsSmokeU32( hash, static_cast<uint32_t>( static_cast<uint64_t>( contact.key ) >> 32u ) );
        hash = HashPhysicsSmokeVector( hash, contact.normal );
        hash = HashPhysicsSmokeFloat( hash, contact.penetration );
        hash = HashPhysicsSmokeFloat( hash, contact.accN );
        hash = HashPhysicsSmokeU32( hash, contact.isTerrain ? 1u : 0u );
    }

    for ( int islandId : diagnostics.sleepIslandVisualId )
    {
        hash = HashPhysicsSmokeU32( hash, static_cast<uint32_t>( islandId ) );
    }

    result.deterministicHash = hash;
    result.lifecycleChecksPassed = created && updatedBodyAndCollider && firstJoint.IsValid() && survivorJoint.IsValid() &&
                                   updatedJoint && destroyedFirstJoint && survivorHandleStable && destroyedSurvivorJoint &&
                                   staleConstraintRejected && cascadeLifecycle && clearLifecycle &&
                                   result.activationChecksPassed && result.rayCastHit && broadphaseMatched;

    // Invariant: the physics gate pins one exact shipping-engine state, not
    // merely agreement between two runs that could repeat the same regression.
    const bool expectedState = result.bodyCount == 2u && result.colliderCount == 2u && result.broadphaseQueryCount == 1u &&
                               result.contactCount == 1u && result.islandStateCount == 2u && result.stepCount == 1u &&
                               result.finalPosition.x == 3.25f && result.finalPosition.y == 2.0f &&
                               result.finalPosition.z == 0.0f && result.finalLinearVelocity.x == 1.0f &&
                               result.finalLinearVelocity.y == 0.0f && result.finalLinearVelocity.z == 0.0f &&
                               result.deterministicHash == PHYSICS_ENGINE_LIFECYCLE_EXPECTED_HASH;

    result.passed = result.lifecycleChecksPassed && expectedState;
    return result;
}

struct PhysicsEngineLifecycleSmokeResult
{
    PhysicsEngineLifecycleScenarioResult first;
    uint64_t repeatHash = 0u;
    bool deterministic = false;
    bool passed = false;
};

PhysicsEngineLifecycleSmokeResult RunPhysicsEngineLifecycleSmokeSample()
{
    PhysicsEngineLifecycleSmokeResult result;
    result.first = RunPhysicsEngineLifecycleScenario();
    const PhysicsEngineLifecycleScenarioResult repeat = RunPhysicsEngineLifecycleScenario();
    result.repeatHash = repeat.deterministicHash;
    result.deterministic = result.first.deterministicHash == repeat.deterministicHash;
    result.passed = result.first.passed && repeat.passed && result.deterministic;
    return result;
}

struct PhysicsRuntimeHandleSmokeResult
{
    bool passed = false;
    bool handlesMatchStores = false;
    bool renderMirrorMatches = false;
    bool jointUsesHandles = false;
    bool colliderRefreshMatches = false;
    bool reorderPreservesHandleState = false;
    bool failedCreationIsAtomic = false;
    bool deletionIsAtomic = false;
    bool mutationUsesStableHandle = false;
    bool ragdollCapacityPreflight = false;
    bool generatedCapacityPreflight = false;
    int bodyCount = 0;
    int colliderCount = 0;
    int renderInstanceCount = 0;
    std::size_t pointJointCount = 0;
    PhysicsBodyHandle bodyA;
    std::string errorMessage;
};

bool RunRagdollCapacityPreflightSmoke( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
{
    auto collection = std::make_unique<SkullbonezCore::Runtime::SceneController>( diagnostics );
    int jointCount = 0;
    (void)Ragdoll::SimpleJoints( jointCount );
    RagdollBuildOptions options;
    options.namePrefix = "capacity_smoke";
    options.firstSceneObjectId = collection->State().AllocateSceneObjectIdRange( Ragdoll::SIMPLE_PART_COUNT );
    const SkullbonezCore::Core::SbResult result = SceneAuthoredSetup::AppendSimpleRagdoll( diagnostics, collection->Scene(),
                                                                                           options );

    const PhysicsEngine& physics = collection->Scene().Physics();
    return result.Ok() && collection->Scene().SceneEntityCount() == Ragdoll::SIMPLE_PART_COUNT &&
           collection->Scene().BodyStore().Count() == Ragdoll::SIMPLE_PART_COUNT &&
           collection->Scene().Colliders().BoxShapeCount() == static_cast<std::size_t>( Ragdoll::SIMPLE_PART_COUNT ) &&
           PhysicsEngine::ReadPointJointConstraints( physics ).size() == static_cast<std::size_t>( jointCount ) &&
           PhysicsEngine::ReadPointJointCapacity( physics ) == static_cast<std::size_t>( jointCount );
}

bool RunGeneratedCapacityPreflightSmoke( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
{
    auto collection = std::make_unique<SkullbonezCore::Runtime::SceneController>( diagnostics );
    Core::EngineConfig config;
    SceneSessionState& state = collection->State();
    state.rngState = 1u;
    const SkullbonezCore::Core::SbResult
        result = SceneGeneratedSetup::SetUpSceneEntities( state, config, collection->Scene(),
                                                          GeneratedObjectTypeOverride::Mixed, 32 );

    const ColliderStore& colliders = collection->Scene().Colliders();
    return result.Ok() && state.rngState == 0x72A6EE09u && collection->Scene().SceneEntityCount() == 32 &&
           colliders.SphereShapeCount() == 24u && colliders.BoxShapeCount() == 8u &&
           colliders.SphereShapeCapacity() == 24u && colliders.BoxShapeCapacity() == 8u &&
           colliders.HullShapeCapacity() == 0u;
}

PhysicsRuntimeHandleSmokeResult RunPhysicsRuntimeHandleSmokeSample( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
{
    // Why: this smoke proves runtime-created bodies keep their returned physics
    // handles aligned with body/collider stores and render snapshots without opening the
    // window or renderer. WinMain runs the normal command-line/config bootstrap
    // before this helper so collection capacity uses the same config snapshot
    // as a regular runtime launch.
    // Lifetime: SceneController owns the validation-only physics and entity
    // stores exactly as it does during a normal scene. The cold smoke keeps the
    // owner off the launcher stack and executes once before steady gameplay.
    auto collection = std::make_unique<SkullbonezCore::Runtime::SceneController>( diagnostics );
    SkullbonezCore::Physics::PhysicsEngine& physics = collection->Scene().Physics();
    SkullbonezCore::Runtime::SceneEntityStore& sceneEntities = collection->Scene().Entities();
    PhysicsRuntimeHandleSmokeResult result;
    PhysicsBodyHandle createdBodies[2];
    const SkullbonezCore::Core::SbResult capacityCommit = collection->Scene().CommitPhysicsSceneCapacity( 2, 2, 0, 0, 0, 1 );

    if ( !capacityCommit.Ok() )
    {
        result.errorMessage = capacityCommit.ErrorMessage();
        return result;
    }

    for ( int i = 0; i < 2; ++i )
    {
        SkullbonezCore::Runtime::SceneEntityCreateDesc model;
        char name[32] = {};

        sprintf_s( name, sizeof( name ), "runtime_smoke_%d", i );
        model.SetName( name );
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = static_cast<uint32_t>( i + 1 );
        model.sceneObjectId = sceneObjectId;
        const SkullbonezCore::Math::CollisionDetection::BoundingSphere
            shape( 0.75f, SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );

        const auto
            appendResult = collection->Scene()
                               .TryCreateSceneEntity( std::move( model ),
                                                      MakePhysicsBodyCreateDesc( sceneObjectId, shape,
                                                                                 SkullbonezCore::Math::Vector::
                                                                                     Vector3( static_cast<float>( i ) * 2.0f,
                                                                                              4.0f, 0.0f ),
                                                                                 SkullbonezCore::Math::Orientation::
                                                                                     IDENTITY_QUATERNION,
                                                                                 SkullbonezCore::Math::Vector::
                                                                                     Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                 SkullbonezCore::Math::Vector::
                                                                                     Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                 SkullbonezCore::Math::Vector::
                                                                                     Vector3( 1.0f, 1.0f, 1.0f ),
                                                                                 2.0f + static_cast<float>( i ), 0.0f,
                                                                                 PhysicsBodyMotionKind::Dynamic, name ),
                                                      MakeColliderCreateDesc( shape, 0.0f, HashStr( "default" ) ) );

        if ( !appendResult.status.Ok() )
        {
            result.errorMessage = appendResult.status.ErrorMessage();
            return result;
        }

        createdBodies[i] = appendResult.body;
    }

    const int entityCountBeforeFailure = sceneEntities.Count();
    const int bodyCountBeforeFailure = collection->Scene().BodyStore().Count();
    const int colliderCountBeforeFailure = collection->Scene().Colliders().Count();
    const int renderCountBeforeFailure = collection->Scene().GetRenderInstanceStore().Count();
    const uint32_t descriptorCountBeforeFailure = physics.AuthoredBodyDescriptorCount().value;
    SkullbonezCore::Runtime::SceneEntityCreateDesc duplicateEntity;
    duplicateEntity.sceneObjectId = PhysicsSceneObjectId { 1u };

    duplicateEntity.SetName( "runtime_smoke_duplicate" );
    const SkullbonezCore::Math::CollisionDetection::BoundingSphere
        duplicateShape( 0.5f, SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );

    const auto duplicateResult = collection->Scene()
                                     .TryCreateSceneEntity( std::move( duplicateEntity ),
                                                            MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { 1u },
                                                                                       duplicateShape,
                                                                                       SkullbonezCore::Math::Vector::
                                                                                           Vector3( 0.0f, 8.0f, 0.0f ),
                                                                                       SkullbonezCore::Math::Orientation::
                                                                                           IDENTITY_QUATERNION,
                                                                                       SkullbonezCore::Math::Vector::
                                                                                           Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                       SkullbonezCore::Math::Vector::
                                                                                           Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                       SkullbonezCore::Math::Vector::
                                                                                           Vector3( 1.0f, 1.0f, 1.0f ),
                                                                                       1.0f, 0.0f,
                                                                                       PhysicsBodyMotionKind::Dynamic,
                                                                                       "runtime_smoke_duplicate" ),
                                                            MakeColliderCreateDesc( duplicateShape, 0.0f,
                                                                                    HashStr( "default" ) ) );

    const bool failedCreationIsAtomic = !duplicateResult.status.Ok() && sceneEntities.Count() == entityCountBeforeFailure &&
                                        collection->Scene().BodyStore().Count() == bodyCountBeforeFailure &&
                                        collection->Scene().Colliders().Count() == colliderCountBeforeFailure &&
                                        collection->Scene().GetRenderInstanceStore().Count() == renderCountBeforeFailure &&
                                        physics.AuthoredBodyDescriptorCount().value == descriptorCountBeforeFailure;

    const PhysicsBodyHandle bodyA = createdBodies[0];
    const PhysicsBodyHandle bodyB = createdBodies[1];
    PhysicsPointJointCreateDesc jointDesc;
    jointDesc.bodyA = bodyA;
    jointDesc.bodyB = bodyB;
    jointDesc.localAnchorA = SkullbonezCore::Math::Vector::Vector3( 0.25f, 0.0f, 0.0f );
    jointDesc.localAnchorB = SkullbonezCore::Math::Vector::Vector3( -0.25f, 0.0f, 0.0f );
    const PhysicsConstraintHandle jointHandle = physics.CreatePointJoint( jointDesc );
    const PhysicsBodyStore& bodyStore = collection->Scene().BodyStore();
    const ColliderStore& colliderStore = collection->Scene().Colliders();
    const RenderInstanceStore& renderStore = collection->Scene().GetRenderInstanceStore();
    const auto& pointJoints = PhysicsEngine::ReadPointJointConstraints( collection->Scene().Physics() );

    const size_t initialColliderCount = colliderStore.Count();
    const ColliderRecord initialCollider = colliderStore.Records()[0];
    const SkullbonezCore::Math::Vector::Vector3 editedHalfExtents( 0.25f, 1.25f, 0.5f );
    constexpr float EDITED_RESTITUTION = 0.42f;
    PhysicsBodyUpdateDesc colliderUpdate;
    colliderUpdate.body = bodyA;
    bool colliderUpdateAccepted = false;

    {
        // Why: this authoring probe intentionally introduces the first box shape
        // after scene creation. Per-kind shape backing may grow only at the same
        // cold topology boundary used by real scene/editor authoring.
        Core::Allocation::RuntimeAllocationScope sceneLoadScope( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        const SkullbonezCore::Math::CollisionDetection::BoundingBox
            editedShape( editedHalfExtents, SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
        colliderUpdateAccepted = physics.UpdateAuthoredBodyAndCollider( colliderUpdate,
                                                                        MakeColliderCreateDesc( editedShape,
                                                                                                EDITED_RESTITUTION,
                                                                                                HashStr( "default" ) ) );
    }

    const ColliderStore& refreshedColliderStore = collection->Scene().Colliders();
    const ColliderRecord& refreshedCollider = refreshedColliderStore.Records()[0];
    const float expectedBoxRadius = sqrtf( 0.25f * 0.25f + 1.25f * 1.25f + 0.5f * 0.5f );

    // Invariant: same-count authoring edits must be visible through the explicit
    // collider edit commit. Store reads only auto-repair topology changes, so
    // tools and scene edits must commit before asking for collider records.
    const bool colliderRefreshMatches = colliderUpdateAccepted && initialCollider.shapeKind == ColliderShapeKind::Sphere &&
                                        refreshedCollider.shapeKind == ColliderShapeKind::Box &&
                                        fabsf( refreshedCollider.boundingRadius - expectedBoxRadius ) < 0.0001f &&
                                        fabsf( refreshedCollider.restitution - 0.42f ) < 0.0001f &&
                                        fabsf( refreshedCollider.projectedSurfaceArea -
                                               initialCollider.projectedSurfaceArea ) > 0.0001f &&
                                        fabsf( refreshedCollider.dragCoefficient - initialCollider.dragCoefficient ) >
                                            0.0001f &&
                                        refreshedCollider.handle == initialCollider.handle &&
                                        refreshedCollider.body == initialCollider.body &&
                                        refreshedColliderStore.Count() == initialColliderCount;

    const PhysicsBodyRecord* bodyARecord = bodyStore.RecordForModelIndex( 0 );
    const PhysicsBodyRecord* bodyBRecord = bodyStore.RecordForModelIndex( 1 );
    const RenderInstanceHandle renderHandleA = renderStore.HandleForModelIndex( 0 );
    const bool handlesMatchStores = bodyA.IsValid() && bodyB.IsValid() && bodyARecord && bodyBRecord &&
                                    bodyARecord->handle == bodyA && bodyBRecord->handle == bodyB &&
                                    colliderStore.HandleForBodyHandle( bodyA ).IsValid() &&
                                    colliderStore.HandleForBodyHandle( bodyB ).IsValid();

    const bool renderMirrorMatches = bodyARecord && renderStore.Count() == 2 && renderHandleA.IsValid() &&
                                     renderStore.ModelIndexForHandle( renderHandleA ) == 0 &&
                                     !renderStore.Records().empty() &&
                                     renderStore.Records()[0].sceneObjectId == bodyARecord->sceneObjectId;

    const bool jointUsesHandles = jointHandle.IsValid() && pointJoints.size() == 1 && pointJoints[0].bodyA == bodyA &&
                                  pointJoints[0].bodyB == bodyB && pointJoints[0].BodyAIndex( bodyStore ) == 0 &&
                                  pointJoints[0].BodyBIndex( bodyStore ) == 1;

    constexpr uint32_t REORDER_BODY_A_SCENE_OBJECT_ID_VALUE = 100u;
    constexpr uint32_t REORDER_BODY_B_SCENE_OBJECT_ID_VALUE = 101u;

    // Why: keep this cold isolated topology-reorder probe owner off WinMain's
    // bounded thread stack, then commit only its two required store rows.
    auto reorderBodyStore = std::make_unique<PhysicsBodyStore>();

    {
        Core::Allocation::RuntimeAllocationScope sceneLoadScope( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        reorderBodyStore->ReserveCapacity( 2u );
    }

    std::vector<PhysicsBodyCreateDesc> reorderBodyDescs;

    for ( int i = 0; i < 2; ++i )
    {
        PhysicsBodyCreateDesc desc;
        desc.sceneObjectId = MakePhysicsSceneObjectId( REORDER_BODY_A_SCENE_OBJECT_ID_VALUE + static_cast<uint32_t>( i ) );

        desc.shape = SkullbonezCore::Math::CollisionDetection::
            BoundingSphere( 0.5f, SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );

        desc.position = SkullbonezCore::Math::Vector::Vector3( static_cast<float>( i ) * 3.0f, 5.0f, 0.0f );
        desc.rotationalInertia = SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f );
        desc.mass = 3.0f + static_cast<float>( i );
        desc.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
        desc.volume = SkullbonezCore::Math::CollisionDetection::GetShapeVolume( desc.shape );
        desc.projectedSurfaceArea = SkullbonezCore::Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );

        desc.dragCoefficient = SkullbonezCore::Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );
        reorderBodyDescs.push_back( desc );
    }

    reorderBodyStore->LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t> {} );
    const PhysicsBodyHandle reorderedOriginalBody = reorderBodyStore->HandleForModelIndex( 0 );
    const uint32_t reorderBodyASceneObjectIdValue = REORDER_BODY_A_SCENE_OBJECT_ID_VALUE;
    const uint32_t reorderBodyBSceneObjectIdValue = REORDER_BODY_B_SCENE_OBJECT_ID_VALUE;
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulse( 0.0f, 2.0f, 0.0f );
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulsePoint( 0.25f, 0.0f, 0.0f );
    const bool seededReorderState = reorderBodyStore->SetPendingBodyImpulse( reorderedOriginalBody, pendingImpulse,
                                                                             pendingImpulsePoint ) &&
                                    reorderBodyStore->SeedBodyAsleep( reorderedOriginalBody );

    reorderBodyDescs[0].sceneObjectId = MakePhysicsSceneObjectId( reorderBodyBSceneObjectIdValue );
    reorderBodyDescs[1].sceneObjectId = MakePhysicsSceneObjectId( reorderBodyASceneObjectIdValue );
    reorderBodyStore->LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t> {} );
    const int reorderedBodyAIndex = reorderBodyStore->ModelIndexForHandle( reorderedOriginalBody );
    const PhysicsBodyRecord* reorderedBodyARecord = reorderedBodyAIndex >= 0
                                                        ? reorderBodyStore->RecordForModelIndex( reorderedBodyAIndex )
                                                        : nullptr;

    const auto reorderedHotFields = reorderBodyStore->HotFields();

    // Invariant: allocator-owned handles must carry physics-owned one-shot
    // state through a same-scene reorder. Otherwise the handle identity is only
    // nominally independent from model order.
    const bool reorderPreservesHandleState = seededReorderState && reorderedBodyAIndex == 1 && reorderedBodyARecord &&
                                             reorderedBodyARecord->handle == reorderedOriginalBody &&
                                             reorderedBodyARecord->hasPendingImpulse &&
                                             reorderedHotFields.awake[static_cast<std::size_t>( reorderedBodyAIndex )] ==
                                                 0u &&
                                             fabsf( reorderedBodyARecord->pendingImpulse.y - pendingImpulse.y ) < 0.0001f &&
                                             fabsf( reorderedBodyARecord->pendingImpulseWorldOffset.x -
                                                    pendingImpulsePoint.x ) < 0.0001f;

    const PhysicsBodyRecord* bodyBBeforeDelete = collection->Scene().BodyStore().RecordForHandle( bodyB );
    const int bodyBIndexBeforeDelete = collection->Scene().BodyStore().ModelIndexForHandle( bodyB );
    const PhysicsBodyHotState
        bodyBHotBeforeDelete = bodyBIndexBeforeDelete >= 0
                                   ? LoadPhysicsBodyHotState( collection->Scene().BodyStore().HotFields(),
                                                              static_cast<std::size_t>( bodyBIndexBeforeDelete ) )
                                   : PhysicsBodyHotState {};

    const SkullbonezCore::Math::Vector::Vector3 liveOnlyPosition( 42.0f, 17.0f, -3.0f );
    const PhysicsBodyRestoreState liveOnlyRestore { bodyB,
                                                    bodyBBeforeDelete ? bodyBBeforeDelete->sceneObjectId
                                                                      : PhysicsSceneObjectId {},
                                                    bodyBHotBeforeDelete.fixed,
                                                    liveOnlyPosition,
                                                    bodyBHotBeforeDelete.orientation,
                                                    bodyBHotBeforeDelete.linearVelocity,
                                                    bodyBHotBeforeDelete.angularVelocity,
                                                    bodyBBeforeDelete ? bodyBBeforeDelete->mass : 0.0f,
                                                    bodyBHotBeforeDelete.inverseMass,
                                                    bodyBBeforeDelete ? bodyBBeforeDelete->rotationalInertia
                                                                      : SkullbonezCore::Math::Vector::ZERO_VECTOR,
                                                    bodyBHotBeforeDelete.inverseRotationalInertia };

    const bool seededLiveOnlyState = bodyBBeforeDelete && bodyBIndexBeforeDelete >= 0 &&
                                     physics.RestoreReplayBodyState( liveOnlyRestore );

    const bool destroyedBodyA = collection->Scene().DestroySceneEntity( bodyA );
    const PhysicsBodyRecord* survivingBody = collection->Scene().BodyStore().RecordForHandle( bodyB );
    const int survivingBodyIndex = collection->Scene().BodyStore().ModelIndexForHandle( bodyB );
    const SkullbonezCore::Math::Vector::Vector3
        survivingPosition = survivingBodyIndex >= 0 ? PhysicsBodyPosition( collection->Scene().BodyStore().HotFields(),
                                                                           static_cast<std::size_t>( survivingBodyIndex ) )
                                                    : SkullbonezCore::Math::Vector::Vector3 {};

    const bool deletionIsAtomic = seededLiveOnlyState && destroyedBodyA &&
                                  !collection->Scene().BodyStore().Contains( bodyA ) && survivingBody &&
                                  collection->Scene().BodyStore().ModelIndexForHandle( bodyB ) == 0 &&
                                  sceneEntities.Count() == 1 && sceneEntities.At( 0 ).body == bodyB &&
                                  collection->Scene().BodyStore().Count() == 1 &&
                                  collection->Scene().Colliders().Count() == 1 &&
                                  collection->Scene().Colliders().HandleForBodyHandle( bodyB ).IsValid() &&
                                  collection->Scene().GetRenderInstanceStore().Count() == 1 &&
                                  collection->Scene().GetRenderInstanceStore().PresentationCount() == 1 &&
                                  physics.AuthoredBodyDescriptorCount().value == 1u &&
                                  PhysicsEngine::ReadPointJointConstraints( collection->Scene().Physics() ).empty() &&
                                  fabsf( survivingPosition.x - liveOnlyPosition.x ) < 0.0001f &&
                                  fabsf( survivingPosition.y - liveOnlyPosition.y ) < 0.0001f &&
                                  fabsf( survivingPosition.z - liveOnlyPosition.z ) < 0.0001f;

    PhysicsBodyUpdateDesc staleUpdate;
    staleUpdate.body = bodyA;
    staleUpdate.updateMask = PHYSICS_BODY_UPDATE_POSE;
    PhysicsBodyUpdateDesc survivingUpdate;
    survivingUpdate.body = bodyB;
    survivingUpdate.updateMask = PHYSICS_BODY_UPDATE_VELOCITY | PHYSICS_BODY_UPDATE_MASS;
    survivingUpdate.linearVelocity = SkullbonezCore::Math::Vector::Vector3( 2.0f, 3.0f, 4.0f );
    survivingUpdate.angularVelocity = SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.5f, 0.0f );
    survivingUpdate.mass = 7.0f;
    survivingUpdate.rotationalInertia = survivingBody ? survivingBody->rotationalInertia
                                                      : SkullbonezCore::Math::Vector::ZERO_VECTOR;

    const bool staleMutationRejected = !physics.UpdateAuthoredBody( staleUpdate );
    const bool survivingMutationAccepted = physics.UpdateAuthoredBody( survivingUpdate );
    survivingBody = collection->Scene().BodyStore().RecordForHandle( bodyB );
    const int mutatedBodyIndex = collection->Scene().BodyStore().ModelIndexForHandle( bodyB );
    const auto mutatedHotFields = collection->Scene().BodyStore().HotFields();
    const bool
        mutationUsesStableHandle = staleMutationRejected && survivingMutationAccepted && survivingBody &&
                                   mutatedBodyIndex >= 0 && fabsf( survivingBody->mass - 7.0f ) < 0.0001f &&
                                   fabsf( mutatedHotFields.linearVelocityX[static_cast<std::size_t>( mutatedBodyIndex )] -
                                          2.0f ) < 0.0001f &&
                                   fabsf( mutatedHotFields.angularVelocityY[static_cast<std::size_t>( mutatedBodyIndex )] -
                                          0.5f ) < 0.0001f;

    result.handlesMatchStores = handlesMatchStores;
    result.renderMirrorMatches = renderMirrorMatches;
    result.jointUsesHandles = jointUsesHandles;
    result.colliderRefreshMatches = colliderRefreshMatches;
    result.reorderPreservesHandleState = reorderPreservesHandleState;
    result.failedCreationIsAtomic = failedCreationIsAtomic;
    result.deletionIsAtomic = deletionIsAtomic;
    result.mutationUsesStableHandle = mutationUsesStableHandle;
    result.ragdollCapacityPreflight = RunRagdollCapacityPreflightSmoke( diagnostics );
    result.generatedCapacityPreflight = RunGeneratedCapacityPreflightSmoke( diagnostics );
    result.bodyCount = bodyCountBeforeFailure;
    result.colliderCount = colliderCountBeforeFailure;
    result.renderInstanceCount = renderCountBeforeFailure;
    result.pointJointCount = jointUsesHandles ? 1u : 0u;
    result.bodyA = bodyA;
    result.passed = handlesMatchStores && renderMirrorMatches && jointUsesHandles && colliderRefreshMatches &&
                    reorderPreservesHandleState && failedCreationIsAtomic && deletionIsAtomic && mutationUsesStableHandle &&
                    result.ragdollCapacityPreflight && result.generatedCapacityPreflight;

    return result;
}
} // anonymous namespace
bool HandleGenAtlas( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--gen-atlas" ) )
    {
        return false;
    }

    char outPath[MAX_PATH];
    const char* atlasArg = FindOptionValue( commandLine, "--gen-atlas" );

    if ( atlasArg && *atlasArg != '\0' )
    {
        if ( strlen( atlasArg ) >= MAX_PATH )
        {
            fprintf( stderr, "[gen-atlas] Output path is too long.\n" );
            outExitCode = 1;
            return true;
        }

        strcpy_s( outPath, atlasArg );
    }
    else
    {
        strcpy_s( outPath, "SkullbonezData/font_atlas.sdf" );
    }

    fprintf( stdout, "[gen-atlas] Generating SDF font atlas: %s\n", outPath );

    if ( SkullbonezCore::Text::Text2d::GenerateSdfAtlasToFile( "Verdana", outPath ) )
    {
        fprintf( stdout, "[gen-atlas] Done.\n" );
        outExitCode = 0;
    }
    else
    {
        fprintf( stderr, "[gen-atlas] FAILED.\n" );
        outExitCode = 1;
    }

    return true;
}
bool HandlePhysicsStandaloneSmoke( Core::SbDiagnosticStore& diagnostics, const CommandLineView& commandLine,
                                   int& outExitCode )
{
    if ( !HasOption( commandLine, "--physics-standalone-smoke" ) && !HasOption( commandLine, "--physics_standalone_smoke" ) )
    {
        return false;
    }

    // Why: this option runs before the ordinary WorkerPool, Window, renderer,
    // or Run owners. Its local inline worker proves the shipping PhysicsEngine
    // path can step and query without renderer/window services.
    const PhysicsEngineLifecycleSmokeResult engineLifecycle = RunPhysicsEngineLifecycleSmokeSample();
    PhysicsRuntimeHandleSmokeResult runtimeMirror = RunPhysicsRuntimeHandleSmokeSample( diagnostics );
    auto writeReport = [&]( FILE* stream )
    {
        if ( !stream )
        {
            return;
        }

        fprintf( stream,
                 "[physics-engine-lifecycle-smoke] bodies=%u colliders=%u steps=%u "
                 "final_position=(%.6f,%.6f,%.6f) final_velocity=(%.6f,%.6f,%.6f) "
                 "lifecycle_checks=%s activation_checks=%s ray_cast=%s broadphase=%u contacts=%u "
                 "island_states=%u deterministic=%s hash=0x%016llX repeat_hash=0x%016llX "
                 "runtime_mirror_checks=%s\n",
                 engineLifecycle.first.bodyCount, engineLifecycle.first.colliderCount, engineLifecycle.first.stepCount,
                 engineLifecycle.first.finalPosition.x, engineLifecycle.first.finalPosition.y,
                 engineLifecycle.first.finalPosition.z, engineLifecycle.first.finalLinearVelocity.x,
                 engineLifecycle.first.finalLinearVelocity.y, engineLifecycle.first.finalLinearVelocity.z,
                 engineLifecycle.first.lifecycleChecksPassed ? "pass" : "fail",
                 engineLifecycle.first.activationChecksPassed ? "pass" : "fail",
                 engineLifecycle.first.rayCastHit ? "pass" : "fail", engineLifecycle.first.broadphaseQueryCount,
                 engineLifecycle.first.contactCount, engineLifecycle.first.islandStateCount,
                 engineLifecycle.deterministic ? "pass" : "fail",
                 static_cast<unsigned long long>( engineLifecycle.first.deterministicHash ),
                 static_cast<unsigned long long>( engineLifecycle.repeatHash ), runtimeMirror.passed ? "pass" : "fail" );

        fprintf( stream,
                 "[physics-runtime-handle-smoke] bodies=%d colliders=%d render_instances=%d point_joints=%zu "
                 "handle_a=(%u,%u) store_handles=%s render_mirror=%s joint_handles=%s collider_refresh=%s "
                 "reorder_state=%s creation_atomic=%s deletion_atomic=%s mutation_handle=%s "
                 "ragdoll_capacity=%s generated_capacity=%s\n",
                 runtimeMirror.bodyCount, runtimeMirror.colliderCount, runtimeMirror.renderInstanceCount,
                 runtimeMirror.pointJointCount, runtimeMirror.bodyA.index, runtimeMirror.bodyA.generation,
                 runtimeMirror.handlesMatchStores ? "pass" : "fail", runtimeMirror.renderMirrorMatches ? "pass" : "fail",
                 runtimeMirror.jointUsesHandles ? "pass" : "fail", runtimeMirror.colliderRefreshMatches ? "pass" : "fail",
                 runtimeMirror.reorderPreservesHandleState ? "pass" : "fail",
                 runtimeMirror.failedCreationIsAtomic ? "pass" : "fail", runtimeMirror.deletionIsAtomic ? "pass" : "fail",
                 runtimeMirror.mutationUsesStableHandle ? "pass" : "fail",
                 runtimeMirror.ragdollCapacityPreflight ? "pass" : "fail",
                 runtimeMirror.generatedCapacityPreflight ? "pass" : "fail" );

        if ( !runtimeMirror.errorMessage.empty() )
        {
            fprintf( stream, "[physics-runtime-handle-smoke] error=\"%s\"\n", runtimeMirror.errorMessage.c_str() );
        }

        fflush( stream );
    };

    writeReport( stdout );
    const char* reportPath = FindOptionValue( commandLine, "--physics-standalone-smoke-log",
                                              "--physics_standalone_smoke_log" );

    if ( reportPath && !IsOptionValueMissing( reportPath ) )
    {
        FILE* reportFile = nullptr;

        if ( fopen_s( &reportFile, reportPath, "w" ) == 0 && reportFile )
        {
            writeReport( reportFile );
            fclose( reportFile );
        }
    }

    if ( !engineLifecycle.passed || !runtimeMirror.passed )
    {
        fprintf( stderr, "FAIL: physics engine lifecycle, deterministic repeat, or runtime mirror checks did not pass.\n" );

        outExitCode = 1;
        return true;
    }

    fprintf( stdout, "PASS: physics engine lifecycle and runtime handle mirror smoke matched expected state.\n" );
    outExitCode = 0;
    return true;
}
} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore

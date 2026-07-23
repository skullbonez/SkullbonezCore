//
// File: SkullbonezTests/TestDeterminism.cpp
// Purpose:
//   Lock fast PhysicsEngine determinism, replay restore, and physics invariant properties.
//
// Summary:
//   A minimal authored physics world can be seeded directly through
//   PhysicsEngine without SceneController or scene-load plumbing. Fixed-step
//   determinism means two engines with identical body/collider rows produce the
//   same byte-level kinematic state at the same tick boundaries.
//
// Glossary:
//   Micro-world: Tiny unit-test physics scene with a few authored bodies and
//     colliders, deliberately below every worker-dispatch threshold.
//   Solver snapshot: Replay-owned PhysicsWorld state captured separately from
//     body-store poses and velocities.
//   Body replay state: Store-owned body row fields restored through
//     PhysicsEngine::RestoreReplayBodyState.
//   Physics-only terrain: CPU-domain analytic slope used without renderer
//     construction or a backend test double.
//   Property check: Tolerance-based unit assertion over a physics rule that
//     should hold across implementation details, rather than a golden row match.
//   Kinetic energy: Translational plus angular motion energy used here as a
//     damping monotonicity signal.
//   Sleep gate: Solver-owned optimization state that can pause integration until
//     an explicit wake path receives motion again.
//   Terrain manifold: Contact-point report between a body and the flat test
//     terrain, sampled here as diagnostics rather than as the byte oracle.
//   Parallel gravity field: Forty-body fixture above the mutual-gravity worker
//     threshold whose exact kinematics are compared at 0, 1, and 4 workers.
//   Parallel contact field: 520-body fixture above the ordinary physics worker
//     threshold whose contact, terrain, integration, and sleep state is compared
//     at 0, 1, and 4 workers.
//   Large gravity field: 520-body fixture above the pair-scratch threshold that
//     proves the exact serial fallback ignores worker availability.
//
// Invariants:
//   - The micro-world stays serial; worker fan-out starts far above this body count.
//   - Snapshot losslessness needs both solver state and body replay state.
//   - Kinematic comparisons are byte-exact, not epsilon-based.
//   - Invariant checks use explicit tolerances because they assert physical
//     policy, not serialized replay bytes.
//   - Terrain queries are real flat-plane queries; render resources must stay unused.
//   - Worker scheduling must not change any kinematic or sleep-state byte.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsEngine.h
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsDiagnosticsSink.h"
#include "../SkullbonezSource/Gameplay/TornadoGameplay.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::LoadPhysicsBodyHotState;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyRestoreState;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsSolverSnapshot;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ReplayBodyShapeKind;
using SkullbonezCore::Runtime::ReplayFrameIndex;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
constexpr int kMicroBodyCount = 3;
constexpr int kParallelMutualGravityBodyCount = 40;
constexpr int kParallelContactBodyCount = 520;
constexpr int kLargeMutualGravityBodyCount = 520;
constexpr int kSnapshotFrame = 120;
constexpr int kReplayWindowTicks = 60;
constexpr int kReplaySampleSnapshotFrame = 30;
constexpr int kReplaySampleWindowTicks = 30;
constexpr int kTotalDeterminismTicks = 240;
constexpr int kPenetrationSettleTicks = 480;
constexpr float kDampingEnergyTolerance = 0.0001f;

struct BodyReplayState
{
    PhysicsBodyHandle handle;
    PhysicsSceneObjectId sceneObjectId;
    bool fixed = false;
    Vector3 position;
    Quaternion orientation;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Vector3 rotationalInertia;
    Vector3 inverseRotationalInertia;
};

struct MicroWorldSnapshot
{
    PhysicsSolverSnapshot solver;
    std::array<BodyReplayState, kMicroBodyCount> bodies;
};

SkullbonezCore::Core::EngineConfig MakeDeterministicConfig()
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelExternalForceFields = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;
    config.worldForces.fluidDensity = 0.0f;
    config.worldForces.gasDensity = 0.0f;
    config.worldForces.gravity = -9.8f;
    config.bodySimulation.velocityLimit = 1000.0f;
    config.broadphase.cellSize = 32.0f;
    config.physicsSleep.frames = 1000000;
    return config;
}

PhysicsWorldForces DeterministicForces()
{
    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.fluidDensity = 0.0f;
    forces.gasDensity = 0.0f;
    forces.angularDragMultiplier = 0.0f;
    return forces;
}

PhysicsWorldForces NoGravityForces()
{
    PhysicsWorldForces forces = DeterministicForces();
    forces.gravity = 0.0f;
    return forces;
}

PhysicsWorldForces MutualGravityForces( float gravitationalConstant, float softeningLength )
{
    PhysicsWorldForces forces = NoGravityForces();
    forces.mutualGravity.enabled = true;
    forces.mutualGravity.gravitationalConstant = gravitationalConstant;
    forces.mutualGravity.softeningLength = softeningLength;
    return forces;
}

PhysicsWorldForces DampingForces()
{
    PhysicsWorldForces forces = NoGravityForces();
    forces.fluidSurfaceHeight = 1000.0f;
    forces.fluidDensity = 2.0f;
    forces.angularDragMultiplier = 2.0f;
    return forces;
}

Terrain& FlatTestTerrain()
{
    // Lifetime: bodies borrow this Terrain pointer for every step. Static
    // storage keeps the borrowed terrain and config valid across repeated
    // engine resets without depending on process-global configuration.
    static SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    static Terrain terrain( 0.0f, 0.0f, 0.0f, config );
    return terrain;
}

CollisionShape MakeSphereShape( float radius )
{
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

void AddMicroBody( PhysicsEngine& engine,
                   uint32_t sceneObjectIdValue,
                   const Vector3& position,
                   const Vector3& linearVelocity )
{
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectIdValue },
                                               shape,
                                               position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               linearVelocity,
                                               Vector3( 0.03f * static_cast<float>( sceneObjectIdValue ), 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ),
                                               mass,
                                               0.0f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               &FlatTestTerrain(),
                                               "unit-determinism-body" );
    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void AddSupportedSleepBody( PhysicsEngine& engine, uint32_t sceneObjectIdValue, const Vector3& position )
{
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectIdValue },
                                               shape,
                                               position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ),
                                               mass,
                                               0.0f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               &FlatTestTerrain(),
                                               "unit-sleep-threshold-body" );
    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void SeedSupportedSleepWorld( PhysicsEngine& engine, const SkullbonezCore::Core::EngineConfig& config )
{
    // Why: the sleep-threshold test needs real terrain support but no inherited
    // angular motion from SeedMicroWorld. One quiet sphere starts exactly on the
    // flat terrain so the solver, not fixture surgery, earns the sleep state.
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    AddSupportedSleepBody( engine, 401u, Vector3( 0.0f, 1.0f, 0.0f ) );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 1 );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == 1 );
}

void SeedParallelContactSleepWorld( PhysicsEngine& engine, const SkullbonezCore::Core::EngineConfig& config )
{
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    engine.ReserveAuthoredBodyCapacity( kParallelContactBodyCount );

    // Concept: the first 256 pairs are independent contact islands, exactly
    // crossing the parallel narrowphase threshold without sharing bodies. The
    // final eight bodies stay separated so the fixture also proves a real sleep
    // transition. All 520 bodies cross the force, terrain, and integration
    // worker thresholds before the awake list shrinks.
    for ( int bodyIndex = 0; bodyIndex < kParallelContactBodyCount; ++bodyIndex )
    {
        const int pairIndex = bodyIndex / 2;
        const int pairColumn = pairIndex % 20;
        const int pairRow = pairIndex / 20;
        // Leave the final four pairs separated so quiet terrain-supported
        // bodies must enter sleep even if the resolving contact pairs have not
        // settled within this bounded test window.
        const float pairHalfSeparation = pairIndex < 256 ? 0.9f : 2.5f;
        const float pairOffset = ( bodyIndex & 1 ) == 0 ? -pairHalfSeparation : pairHalfSeparation;
        AddSupportedSleepBody( engine,
                               static_cast<uint32_t>( 1000 + bodyIndex ),
                               Vector3( static_cast<float>( pairColumn * 8 - 76 ) + pairOffset,
                                        1.0f,
                                        static_cast<float>( pairRow * 8 - 48 ) ) );
    }

    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == kParallelContactBodyCount );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == kParallelContactBodyCount );
}

TEST_CASE( "Physics collision-time diagnostics cover every bounded fixed-step event" )
{
    // Four candidate pairs per model are the PhysicsWorld reserve contract;
    // terrain can add one more event for every model in the same fixed step.
    CHECK( SkullbonezCore::Physics::PhysicsDiagnosticsSink::CollisionTimeEventCapacity() ==
           SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 5 );
}

TEST_CASE( "PhysicsEngine exposes its owned sleep policy" )
{
    // PhysicsEngine owns fixed-capacity solver scratch too large for the
    // default test-thread stack. Heap ownership also keeps repeated fixtures
    // out of the Debug executable's image-size budget.
    auto engineStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& engine = *engineStorage;
    engine.Clear();
    engine.SetSleepEnabled( true );
    CHECK( engine.IsSleepEnabled() );
    engine.SetSleepEnabled( false );
    CHECK_FALSE( engine.IsSleepEnabled() );
}

TEST_CASE( "Tornado force witness preserves exact one-step body state" )
{
    // Why: the varied-scene CSV gate does not contain tornado content. This
    // focused byte witness pins the field arithmetic and its exact force-stage
    // scheduling point before gameplay ownership moves out of Physics.
    auto engineStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& engine = *engineStorage;
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, 901u, Vector3( 100.0f, 50.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );

    SkullbonezCore::Gameplay::TornadoGameplay tornadoGameplay;
    SkullbonezCore::Gameplay::TornadoFieldConfig field;
    field.enabled = true;
    field.center = Vector3( 0.0f, 0.0f, 0.0f );
    field.radius = 200.0f;
    field.height = 100.0f;
    field.minCaptureSeconds = 1000.0f;
    field.maxDeltaVelocity = 1000.0f;
    tornadoGameplay.SetFieldConfig( field );

    LockOrderValidator lockOrderValidator;
    WorkerPool workers( lockOrderValidator );
    engine.Step( PHYSICS_FIXED_DT,
                 NoGravityForces(),
                 tornadoGameplay.BuildForceFrame( PHYSICS_FIXED_DT, 1 ),
                 workers,
                 SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );

    const PhysicsBodyHotState hot = LoadPhysicsBodyHotState( PhysicsEngine::ReadBodies( engine ).HotFields(), 0u );
    uint32_t velocityXBits = 0;
    uint32_t velocityYBits = 0;
    uint32_t velocityZBits = 0;
    uint32_t positionXBits = 0;
    uint32_t positionYBits = 0;
    uint32_t positionZBits = 0;
    std::memcpy( &velocityXBits, &hot.linearVelocity.x, sizeof( velocityXBits ) );
    std::memcpy( &velocityYBits, &hot.linearVelocity.y, sizeof( velocityYBits ) );
    std::memcpy( &velocityZBits, &hot.linearVelocity.z, sizeof( velocityZBits ) );
    std::memcpy( &positionXBits, &hot.position.x, sizeof( positionXBits ) );
    std::memcpy( &positionYBits, &hot.position.y, sizeof( positionYBits ) );
    std::memcpy( &positionZBits, &hot.position.z, sizeof( positionZBits ) );
    CHECK( velocityXBits == 3208432847u );
    CHECK( velocityYBits == 1057523849u );
    CHECK( velocityZBits == 3214464429u );
    CHECK( positionXBits == 1120402650u );
    CHECK( positionYBits == 1112016013u );
    CHECK( positionZBits == 3156411918u );
}

TEST_CASE( "Tornado execution config is published by the Gameplay force frame" )
{
    SkullbonezCore::Gameplay::TornadoGameplay gameplay;
    gameplay.SetParallelForceEvaluation( false );
    CHECK_FALSE( gameplay.BuildForceFrame( PHYSICS_FIXED_DT, 0 ).parallelEvaluation );

    // Invariant: the authored compatibility key projects into Gameplay. The
    // Physics settings snapshot must not regain content-specific execution
    // authority merely because the generic stage consumes this value.
    gameplay.SetParallelForceEvaluation( true );
    CHECK( gameplay.BuildForceFrame( PHYSICS_FIXED_DT, 0 ).parallelEvaluation );

    SkullbonezCore::Gameplay::TornadoGameplay predictionGameplay;
    predictionGameplay.SetParallelForceEvaluation( gameplay.ParallelForceEvaluation() );
    CHECK( predictionGameplay.BuildForceFrame( PHYSICS_FIXED_DT, 0 ).parallelEvaluation );
}

TEST_CASE( "Tornado owner edits and replay restore reuse bounded vortex storage" )
{
    SkullbonezCore::Gameplay::TornadoGameplay gameplay;
    const uint64_t memoryBeforeVisualReserve = gameplay.CollectMemoryBytes();
    gameplay.ReserveVisualCapacity();
    CHECK( gameplay.CollectMemoryBytes() > memoryBeforeVisualReserve + 30u * 1024u * 1024u );

    SkullbonezCore::Gameplay::TornadoSystemConfig system;
    system.enabled = true;
    system.vortices.resize( 3u );
    gameplay.SetSystemConfig( system );

    const auto* storage = gameplay.GetSystemConfig().vortices.data();
    const std::size_t capacity = gameplay.GetSystemConfig().vortices.capacity();
    gameplay.ToggleEnabled();
    gameplay.ToggleFieldVectors();
    gameplay.SetFieldRadius( 180.0f );
    gameplay.SetFieldHeight( 120.0f );
    gameplay.SetFieldInwardAcceleration( 140.0f );
    gameplay.SetFieldSwirlAcceleration( 175.0f );
    gameplay.SetFieldLiftAcceleration( 60.0f );

    CHECK( gameplay.GetSystemConfig().vortices.data() == storage );
    CHECK( gameplay.GetSystemConfig().vortices.capacity() == capacity );

    std::vector<float> captureSeconds( 3u, 0.25f );
    std::vector<float> cooldownSeconds( 3u, 0.50f );
    gameplay.SetReplayState( captureSeconds, cooldownSeconds, {}, system, 2.0f );
    CHECK( gameplay.GetSystemConfig().vortices.data() == storage );
    CHECK( gameplay.GetSystemConfig().vortices.capacity() == capacity );

    SkullbonezCore::Gameplay::TornadoSystemConfig disabledSystem;
    gameplay.SetReplayState( captureSeconds, cooldownSeconds, {}, disabledSystem, 3.0f );
    REQUIRE( gameplay.CaptureSeconds().size() == captureSeconds.size() );
    REQUIRE( gameplay.EjectCooldownSeconds().size() == cooldownSeconds.size() );
    for ( std::size_t i = 0; i < captureSeconds.size(); ++i )
    {
        CHECK( gameplay.CaptureSeconds()[i] == captureSeconds[i] );
        CHECK( gameplay.EjectCooldownSeconds()[i] == cooldownSeconds[i] );
    }
}

TEST_CASE( "Replay prediction world reset preserves reserved Gameplay snapshot storage" )
{
    SkullbonezCore::Runtime::ReplaySolverWorldSnapshot world;
    world.tornadoSystemConfig.vortices.reserve( 64u );
    world.tornadoCaptureSeconds.reserve( 1024u );
    world.tornadoEjectCooldownSeconds.reserve( 1024u );
    const auto* vortexStorage = world.tornadoSystemConfig.vortices.data();
    const auto* captureStorage = world.tornadoCaptureSeconds.data();
    const auto* cooldownStorage = world.tornadoEjectCooldownSeconds.data();
    const std::size_t vortexCapacity = world.tornadoSystemConfig.vortices.capacity();
    const std::size_t captureCapacity = world.tornadoCaptureSeconds.capacity();
    const std::size_t cooldownCapacity = world.tornadoEjectCooldownSeconds.capacity();

    world.ClearPreservingCapacity();
    CHECK( world.tornadoSystemConfig.vortices.data() == vortexStorage );
    CHECK( world.tornadoCaptureSeconds.data() == captureStorage );
    CHECK( world.tornadoEjectCooldownSeconds.data() == cooldownStorage );
    CHECK( world.tornadoSystemConfig.vortices.capacity() == vortexCapacity );
    CHECK( world.tornadoCaptureSeconds.capacity() == captureCapacity );
    CHECK( world.tornadoEjectCooldownSeconds.capacity() == cooldownCapacity );

    // Invariant: repeated cancellation invalidation remains allocation-free;
    // the first reset must not merely leave a fresh snapshot with zero reserve.
    world.ClearPreservingCapacity();
    CHECK( world.tornadoSystemConfig.vortices.data() == vortexStorage );
    CHECK( world.tornadoCaptureSeconds.data() == captureStorage );
    CHECK( world.tornadoEjectCooldownSeconds.data() == cooldownStorage );
}

void AddMutualGravityBody( PhysicsEngine& engine,
                           uint32_t sceneObjectIdValue,
                           const Vector3& position,
                           const Vector3& linearVelocity,
                           float mass,
                           float radius )
{
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectIdValue },
                                               shape,
                                               position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               linearVelocity,
                                               Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ),
                                               mass,
                                               0.0f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               &FlatTestTerrain(),
                                               "unit-mutual-gravity-body" );
    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void SeedMicroWorld( PhysicsEngine& engine )
{
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, 101u, Vector3( 100.0f, 30.0f, 100.0f ), Vector3( 1.5f, 0.0f, 0.0f ) );
    AddMicroBody( engine, 102u, Vector3( 112.0f, 40.0f, 100.0f ), Vector3( 0.0f, 0.5f, 0.0f ) );
    AddMicroBody( engine, 103u, Vector3( 124.0f, 50.0f, 100.0f ), Vector3( -1.0f, 0.0f, 0.0f ) );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == kMicroBodyCount );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == kMicroBodyCount );
}

void SeedTwoBodyGravityWorld( PhysicsEngine& engine,
                              const Vector3& leftPosition,
                              const Vector3& rightPosition,
                              const Vector3& leftVelocity,
                              const Vector3& rightVelocity,
                              float mass,
                              float radius )
{
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.ReserveAuthoredBodyCapacity( 2 );
    AddMutualGravityBody( engine, 201u, leftPosition, leftVelocity, mass, radius );
    AddMutualGravityBody( engine, 202u, rightPosition, rightVelocity, mass, radius );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 2 );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == 2 );
}

void StepMicroWorldWith( PhysicsEngine& engine, int ticks, const PhysicsWorldForces& forces, int workerThreadCount = 0 )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    if ( workerThreadCount > 0 )
    {
        workerPool.Initialise( workerThreadCount );
    }
    for ( int tick = 0; tick < ticks; ++tick )
    {
        engine.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );
    }
}

void StepMicroWorld( PhysicsEngine& engine, int ticks )
{
    const PhysicsWorldForces forces = DeterministicForces();
    StepMicroWorldWith( engine, ticks, forces );
}

void CheckEngineKinematicsEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs );

void CheckMutualGravityFieldExactAcrossWorkerCounts( int bodyCount, int ticks )
{
    // Lifetime: these large fixed-store engines are cold test owners. Heap
    // ownership keeps their storage out of the executable image while all
    // three worlds remain alive for the exact comparison below.
    auto serial = std::make_unique<PhysicsEngine>();
    auto oneWorker = std::make_unique<PhysicsEngine>();
    auto fourWorkers = std::make_unique<PhysicsEngine>();

    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.physicsExecution.parallel = true;
    config.physicsExecution.parallelMutualGravity = true;
    config.worldForces.gravity = 0.0f;

    auto seedField = [&config, bodyCount]( PhysicsEngine& engine )
    {
        engine.Clear();
        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        engine.ReserveAuthoredBodyCapacity( bodyCount );
        for ( int index = 0; index < bodyCount; ++index )
        {
            const int column = index % 8;
            const int row = index / 8;
            AddMutualGravityBody( engine,
                                  static_cast<uint32_t>( 300u + index ),
                                  Vector3( static_cast<float>( column * 20 - 70 ),
                                           static_cast<float>( 100 + row * 17 ),
                                           static_cast<float>( ( index * 13 ) % 29 - 14 ) ),
                                  Vector3( static_cast<float>( ( index % 5 ) - 2 ) * 0.03f,
                                           static_cast<float>( ( index % 3 ) - 1 ) * 0.02f,
                                           static_cast<float>( ( index % 7 ) - 3 ) * 0.01f ),
                                  1.0f + static_cast<float>( index % 11 ) * 0.25f,
                                  0.5f );
        }
        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == bodyCount );
    };

    seedField( *serial );
    seedField( *oneWorker );
    seedField( *fourWorkers );
    const PhysicsWorldForces forces = MutualGravityForces( 45.0f, 0.35f );

    StepMicroWorldWith( *serial, ticks, forces, 0 );
    StepMicroWorldWith( *oneWorker, ticks, forces, 1 );
    StepMicroWorldWith( *fourWorkers, ticks, forces, 4 );

    CheckEngineKinematicsEqual( *serial, *oneWorker );
    CheckEngineKinematicsEqual( *serial, *fourWorkers );
}

const PhysicsBodyRecord& RequireBodyRecord( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record =
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( modelIndex );
    REQUIRE( record != nullptr );
    return *record;
}

PhysicsBodyHotState RequireBodyHotState( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine );
    REQUIRE( modelIndex >= 0 );
    REQUIRE( modelIndex < bodyStore.Count() );
    return LoadPhysicsBodyHotState( bodyStore.HotFields(), static_cast<std::size_t>( modelIndex ) );
}

PhysicsBodyHandle RequireBodyHandle( const PhysicsEngine& engine, int modelIndex )
{
    return RequireBodyRecord( engine, modelIndex ).handle;
}

float VectorMagnitudeSquared( const Vector3& value )
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

float BodyKineticEnergy( const PhysicsBodyRecord& record, const PhysicsBodyHotState& hotState )
{
    const float translational = 0.5f * record.mass * VectorMagnitudeSquared( hotState.linearVelocity );
    const float angular =
        0.5f * ( record.rotationalInertia.x * hotState.angularVelocity.x * hotState.angularVelocity.x +
                 record.rotationalInertia.y * hotState.angularVelocity.y * hotState.angularVelocity.y +
                 record.rotationalInertia.z * hotState.angularVelocity.z * hotState.angularVelocity.z );
    return translational + angular;
}

float TotalKineticEnergy( const PhysicsEngine& engine )
{
    float energy = 0.0f;
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count(); ++i )
    {
        energy += BodyKineticEnergy( RequireBodyRecord( engine, i ), RequireBodyHotState( engine, i ) );
    }
    return energy;
}

bool DiagnosticsSleepStateAt( const PhysicsEngine& engine, int modelIndex )
{
    const auto sleepStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( engine );
    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    return bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
}

void CheckTerrainPenetrationWithinTolerance( const PhysicsEngine& engine,
                                             const SkullbonezCore::Core::EngineConfig& config )
{
    // Concept: this is the fast invariant partner to byte-exact CSV baselines.
    // It does not care about exact impulse history, only that settled body rows
    // and terrain manifolds stay inside the configured contact envelope.
    const float maxAllowedPenetration = config.terrainContact.threshold + config.bodySimulation.contactEpsilon;
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count(); ++i )
    {
        const PhysicsBodyHotState hotState = RequireBodyHotState( engine, i );
        const float groundClearance = hotState.position.y - hotState.boundingRadius;
        CHECK( groundClearance >= -maxAllowedPenetration );
    }

    const auto diagnostics = engine.GetDiagnosticsView();
    for ( const SkullbonezCore::Physics::TerrainContactManifold& manifold : diagnostics.terrainContactManifolds )
    {
        for ( uint8_t i = 0; i < manifold.pointCount; ++i )
        {
            CHECK( manifold.points[i].penetration <= maxAllowedPenetration );
        }
    }
}

BodyReplayState CaptureBodyReplayState( const PhysicsBodyRecord& record, const PhysicsBodyHotState& hotState )
{
    BodyReplayState state;
    state.handle = record.handle;
    state.sceneObjectId = record.sceneObjectId;
    state.fixed = hotState.fixed;
    state.position = hotState.position;
    state.orientation = hotState.orientation;
    state.linearVelocity = hotState.linearVelocity;
    state.angularVelocity = hotState.angularVelocity;
    state.mass = record.mass;
    state.inverseMass = hotState.inverseMass;
    state.rotationalInertia = record.rotationalInertia;
    state.inverseRotationalInertia = hotState.inverseRotationalInertia;
    return state;
}

void HashBytesForReplayTest( uint64_t& hash, const void* data, std::size_t byteCount )
{
    const uint8_t* bytes = static_cast<const uint8_t*>( data );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash ^= static_cast<uint64_t>( bytes[i] );
        hash *= 1099511628211ull;
    }
}

template <typename T> void HashValueForReplayTest( uint64_t& hash, const T& value )
{
    HashBytesForReplayTest( hash, &value, sizeof( T ) );
}

template <typename T> void HashVectorForReplayTest( uint64_t& hash, const std::vector<T>& values )
{
    const std::size_t count = values.size();
    HashValueForReplayTest( hash, count );
    for ( const T& value : values )
    {
        HashValueForReplayTest( hash, value );
    }
}

void HashSolverBodyForReplayTest( uint64_t& hash, const ReplaySolverBodySample& body )
{
    HashValueForReplayTest( hash, body.id.value );
    HashValueForReplayTest( hash, body.modelRow.value );
    HashValueForReplayTest( hash, body.shapeKind );
    HashValueForReplayTest( hash, body.position );
    HashValueForReplayTest( hash, body.linearVelocity );
    HashValueForReplayTest( hash, body.angularVelocity );
    HashBytesForReplayTest( hash, body.orientation, sizeof( body.orientation ) );
    HashValueForReplayTest( hash, body.mass );
    HashValueForReplayTest( hash, body.inverseMass );
    HashValueForReplayTest( hash, body.rotationalInertia );
    HashValueForReplayTest( hash, body.inverseRotationalInertia );
    HashValueForReplayTest( hash, body.fixed );
    HashValueForReplayTest( hash, body.sleeping );
    HashValueForReplayTest( hash, body.sleepSupported );
    HashValueForReplayTest( hash, body.sleepInhibited );
    HashValueForReplayTest( hash, body.collisionContact );
    HashValueForReplayTest( hash, body.sleepIslandVisualId );
    HashValueForReplayTest( hash, body.contactCount );
    HashValueForReplayTest( hash, body.maxPenetration );
    HashValueForReplayTest( hash, body.normalImpulseSum );
}

uint64_t HashReplaySampleForTest( const ReplaySolverFrameSample& sample )
{
    // Concept: this is a unit-level solver replay hash. Production replay hashes
    // are owned by ReplaySolverRecorder, which needs SceneController. This
    // micro-world fixture hashes the same retained solver sample it restores so
    // the unit test can prove snapshot restore reaches an identical hash without
    // launching Run or rebuilding presentation owners.
    uint64_t hash = 1469598103934665603ull;
    HashValueForReplayTest( hash, sample.frameIndex );
    HashValueForReplayTest( hash, sample.sceneFrame );
    HashValueForReplayTest( hash, sample.simulationSeconds );
    HashValueForReplayTest( hash, sample.physicsDt );
    HashValueForReplayTest( hash, sample.world.gravity );
    HashValueForReplayTest( hash, sample.world.fluidHeight );
    HashValueForReplayTest( hash, sample.world.fluidDensity );
    HashValueForReplayTest( hash, sample.world.fixedStep );
    HashValueForReplayTest( hash, sample.world.scenePhysicsEnabled );
    HashValueForReplayTest( hash, sample.world.sceneTextEnabled );
    HashValueForReplayTest( hash, sample.worldSnapshot.physics.version );
    HashValueForReplayTest( hash, sample.worldSnapshot.physics.modelCount );
    HashValueForReplayTest( hash, sample.worldSnapshot.physics.sleepEnabled );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.timeRemaining );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.sleepState );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.sleepCounter );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.collisionVisualContacts );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.sleepIslandParent );
    HashVectorForReplayTest( hash, sample.worldSnapshot.physics.sleepIslandRank );
    HashValueForReplayTest( hash, sample.contactCount );
    HashValueForReplayTest( hash, sample.pipelineRecordCount );
    const std::size_t bodyCount = sample.bodies.size();
    HashValueForReplayTest( hash, bodyCount );
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        HashSolverBodyForReplayTest( hash, body );
    }
    return hash != 0ull ? hash : 1ull;
}

MicroWorldSnapshot CaptureMicroWorldSnapshot( const PhysicsEngine& engine )
{
    MicroWorldSnapshot snapshot;
    engine.CaptureReplaySolverSnapshot( snapshot.solver, MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) );
    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        const PhysicsBodyRecord* record =
            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( i );
        REQUIRE( record != nullptr );
        snapshot.bodies[static_cast<std::size_t>( i )] =
            CaptureBodyReplayState( *record, RequireBodyHotState( engine, i ) );
    }
    return snapshot;
}

ReplaySolverBodySample CaptureMicroWorldReplayBodySample( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record =
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( modelIndex );
    REQUIRE( record != nullptr );

    ReplaySolverBodySample body;
    const PhysicsBodyHotState hotState = RequireBodyHotState( engine, modelIndex );
    body.id = record->sceneObjectId;
    body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( modelIndex );
    body.shapeKind = ReplayBodyShapeKind::Sphere;
    body.position = hotState.position;
    body.linearVelocity = hotState.linearVelocity;
    body.angularVelocity = hotState.angularVelocity;
    hotState.orientation.GetComponents( body.orientation[0],
                                        body.orientation[1],
                                        body.orientation[2],
                                        body.orientation[3] );
    body.mass = record->mass;
    body.inverseMass = hotState.inverseMass;
    body.rotationalInertia = record->rotationalInertia;
    body.inverseRotationalInertia = hotState.inverseRotationalInertia;
    body.fixed = hotState.fixed;

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const auto sleepStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( engine );
    const auto sleepSupportedStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepSupportedStates( engine );
    const auto sleepInhibitedStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepInhibitedStates( engine );
    const std::vector<uint8_t>& collisionContacts =
        SkullbonezCore::Physics::PhysicsEngine::ReadCollisionVisualContacts( engine );
    const auto sleepIslandIds = SkullbonezCore::Physics::PhysicsEngine::ReadSleepIslandVisualIds( engine );
    body.sleeping = bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
    body.sleepSupported = bodyIndex < sleepSupportedStates.size() && sleepSupportedStates[bodyIndex] != 0;
    body.sleepInhibited = bodyIndex < sleepInhibitedStates.size() && sleepInhibitedStates[bodyIndex] != 0;
    body.collisionContact = bodyIndex < collisionContacts.size() && collisionContacts[bodyIndex] != 0;
    body.sleepIslandVisualId = bodyIndex < sleepIslandIds.size() ? sleepIslandIds[bodyIndex] : 0;
    return body;
}

ReplaySolverFrameSample CaptureMicroWorldReplaySample( const PhysicsEngine& engine, ReplayFrameIndex frameIndex )
{
    // Concept: the replay solver sample is the record under test. This fixture
    // builds the body+world payload that restore consumes without depending on
    // Run, SceneController, cameras, or renderer-owned presentation state.
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.sceneFrame = static_cast<int>( frameIndex );
    sample.simulationSeconds = static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    sample.physicsDt = PHYSICS_FIXED_DT;
    sample.world.gravity = DeterministicForces().gravity;
    sample.world.fluidHeight = DeterministicForces().fluidSurfaceHeight;
    sample.world.fluidDensity = DeterministicForces().fluidDensity;
    sample.world.fixedStep = true;
    sample.world.scenePhysicsEnabled = true;
    sample.world.sceneTextEnabled = true;
    sample.contactCount =
        static_cast<uint16_t>( SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( engine ).size() );
    sample.pipelineRecordCount =
        static_cast<uint16_t>( SkullbonezCore::Physics::PhysicsEngine::ReadPipelineTrace( engine ).size() );
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot.physics,
                                        MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) );

    sample.bodies.reserve( kMicroBodyCount );
    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        sample.bodies.push_back( CaptureMicroWorldReplayBodySample( engine, i ) );
    }
    sample.solverHash = HashReplaySampleForTest( sample );
    sample.presentationHash = sample.solverHash;
    return sample;
}

void RestoreMicroWorldSnapshot( PhysicsEngine& engine, const MicroWorldSnapshot& snapshot )
{
    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot.solver,
                                                 MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) ) );
    for ( const BodyReplayState& state : snapshot.bodies )
    {
        REQUIRE( engine.RestoreReplayBodyState( PhysicsBodyRestoreState{ state.handle,
                                                                         state.sceneObjectId,
                                                                         state.fixed,
                                                                         state.position,
                                                                         state.orientation,
                                                                         state.linearVelocity,
                                                                         state.angularVelocity,
                                                                         state.mass,
                                                                         state.inverseMass,
                                                                         state.rotationalInertia,
                                                                         state.inverseRotationalInertia } ) );
    }
}

void RestoreMicroWorldReplaySample( PhysicsEngine& engine, const ReplaySolverFrameSample& sample )
{
    // Why: replay restore applies solver cache first, then body rows. The test
    // mirrors that order so a future mismatch points at the same boundary Run uses.
    REQUIRE( sample.worldSnapshot.physics.modelCount == static_cast<int>( sample.bodies.size() ) );
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        sample.worldSnapshot.physics,
        MakePhysicsBodyCountFromNonNegativeInt( static_cast<int>( sample.bodies.size() ) ) ) );
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        const Quaternion orientation( body.orientation[0],
                                      body.orientation[1],
                                      body.orientation[2],
                                      body.orientation[3] );
        const PhysicsBodyRecord* record =
            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( body.modelRow.value );
        REQUIRE( record != nullptr );
        REQUIRE( engine.RestoreReplayBodyState( PhysicsBodyRestoreState{ record->handle,
                                                                         body.id,
                                                                         body.fixed,
                                                                         body.position,
                                                                         orientation,
                                                                         body.linearVelocity,
                                                                         body.angularVelocity,
                                                                         body.mass,
                                                                         body.inverseMass,
                                                                         body.rotationalInertia,
                                                                         body.inverseRotationalInertia } ) );
    }
}

void CheckVectorBytesEqual( const Vector3& lhs, const Vector3& rhs )
{
    CHECK( std::memcmp( &lhs, &rhs, sizeof( Vector3 ) ) == 0 );
}

void CheckQuaternionBytesEqual( const Quaternion& lhs, const Quaternion& rhs )
{
    float left[4] = {};
    float right[4] = {};
    lhs.GetComponents( left[0], left[1], left[2], left[3] );
    rhs.GetComponents( right[0], right[1], right[2], right[3] );
    CHECK( std::memcmp( left, right, sizeof( left ) ) == 0 );
}

template <typename T> void CheckVectorContentsEqual( const std::vector<T>& lhs, const std::vector<T>& rhs )
{
    REQUIRE( lhs.size() == rhs.size() );
    for ( std::size_t i = 0; i < lhs.size(); ++i )
    {
        CHECK( lhs[i] == rhs[i] );
    }
}

void CheckReplayBodySamplesEqual( const ReplaySolverBodySample& lhs, const ReplaySolverBodySample& rhs )
{
    CHECK( lhs.id.value == rhs.id.value );
    CHECK( lhs.modelRow.value == rhs.modelRow.value );
    CHECK( lhs.shapeKind == rhs.shapeKind );
    CheckVectorBytesEqual( lhs.position, rhs.position );
    CheckVectorBytesEqual( lhs.linearVelocity, rhs.linearVelocity );
    CheckVectorBytesEqual( lhs.angularVelocity, rhs.angularVelocity );
    CHECK( std::memcmp( lhs.orientation, rhs.orientation, sizeof( lhs.orientation ) ) == 0 );
    CHECK( std::memcmp( &lhs.mass, &rhs.mass, sizeof( lhs.mass ) ) == 0 );
    CHECK( std::memcmp( &lhs.inverseMass, &rhs.inverseMass, sizeof( lhs.inverseMass ) ) == 0 );
    CheckVectorBytesEqual( lhs.rotationalInertia, rhs.rotationalInertia );
    CheckVectorBytesEqual( lhs.inverseRotationalInertia, rhs.inverseRotationalInertia );
    CHECK( lhs.fixed == rhs.fixed );
    CHECK( lhs.sleeping == rhs.sleeping );
    CHECK( lhs.sleepSupported == rhs.sleepSupported );
    CHECK( lhs.sleepInhibited == rhs.sleepInhibited );
    CHECK( lhs.collisionContact == rhs.collisionContact );
    CHECK( lhs.sleepIslandVisualId == rhs.sleepIslandVisualId );
    CHECK( lhs.contactCount == rhs.contactCount );
    CHECK( std::memcmp( &lhs.maxPenetration, &rhs.maxPenetration, sizeof( lhs.maxPenetration ) ) == 0 );
    CHECK( std::memcmp( &lhs.normalImpulseSum, &rhs.normalImpulseSum, sizeof( lhs.normalImpulseSum ) ) == 0 );
}

void CheckReplaySamplesEqual( const ReplaySolverFrameSample& lhs, const ReplaySolverFrameSample& rhs )
{
    // Invariant: the recaptured future frame must match at the replay-sample
    // boundary, not merely at live PhysicsBodyStore kinematics.
    CHECK( lhs.frameIndex == rhs.frameIndex );
    CHECK( lhs.sceneFrame == rhs.sceneFrame );
    CHECK( std::memcmp( &lhs.simulationSeconds, &rhs.simulationSeconds, sizeof( lhs.simulationSeconds ) ) == 0 );
    CHECK( std::memcmp( &lhs.physicsDt, &rhs.physicsDt, sizeof( lhs.physicsDt ) ) == 0 );
    CHECK( std::memcmp( &lhs.world.gravity, &rhs.world.gravity, sizeof( lhs.world.gravity ) ) == 0 );
    CHECK( std::memcmp( &lhs.world.fluidHeight, &rhs.world.fluidHeight, sizeof( lhs.world.fluidHeight ) ) == 0 );
    CHECK( std::memcmp( &lhs.world.fluidDensity, &rhs.world.fluidDensity, sizeof( lhs.world.fluidDensity ) ) == 0 );
    CHECK( lhs.world.fixedStep == rhs.world.fixedStep );
    CHECK( lhs.world.scenePhysicsEnabled == rhs.world.scenePhysicsEnabled );
    CHECK( lhs.world.sceneTextEnabled == rhs.world.sceneTextEnabled );
    CHECK( lhs.worldSnapshot.physics.version == rhs.worldSnapshot.physics.version );
    CHECK( lhs.worldSnapshot.physics.modelCount == rhs.worldSnapshot.physics.modelCount );
    CHECK( lhs.worldSnapshot.physics.sleepEnabled == rhs.worldSnapshot.physics.sleepEnabled );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.timeRemaining, rhs.worldSnapshot.physics.timeRemaining );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.sleepState, rhs.worldSnapshot.physics.sleepState );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.sleepCounter, rhs.worldSnapshot.physics.sleepCounter );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.collisionVisualContacts,
                              rhs.worldSnapshot.physics.collisionVisualContacts );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.sleepIslandParent,
                              rhs.worldSnapshot.physics.sleepIslandParent );
    CheckVectorContentsEqual( lhs.worldSnapshot.physics.sleepIslandRank, rhs.worldSnapshot.physics.sleepIslandRank );
    CHECK( lhs.contactCount == rhs.contactCount );
    CHECK( lhs.pipelineRecordCount == rhs.pipelineRecordCount );
    CHECK( lhs.solverHash != 0u );
    CHECK( lhs.solverHash == rhs.solverHash );
    CHECK( lhs.presentationHash == rhs.presentationHash );
    REQUIRE( lhs.bodies.size() == rhs.bodies.size() );
    for ( std::size_t i = 0; i < lhs.bodies.size(); ++i )
    {
        CheckReplayBodySamplesEqual( lhs.bodies[i], rhs.bodies[i] );
    }
}

void CheckEngineKinematicsEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).Count() ==
             SkullbonezCore::Physics::PhysicsEngine::ReadBodies( rhs ).Count() );
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).Count(); ++i )
    {
        const PhysicsBodyRecord* left =
            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).RecordForModelIndex( i );
        const PhysicsBodyRecord* right =
            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( rhs ).RecordForModelIndex( i );
        REQUIRE( left != nullptr );
        REQUIRE( right != nullptr );
        const PhysicsBodyHotState leftHot = RequireBodyHotState( lhs, i );
        const PhysicsBodyHotState rightHot = RequireBodyHotState( rhs, i );
        CheckVectorBytesEqual( leftHot.position, rightHot.position );
        CheckQuaternionBytesEqual( leftHot.orientation, rightHot.orientation );
        CheckVectorBytesEqual( leftHot.linearVelocity, rightHot.linearVelocity );
        CheckVectorBytesEqual( leftHot.angularVelocity, rightHot.angularVelocity );
    }
}

void CheckEngineWorkerDeterministicStateEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    CheckEngineKinematicsEqual( lhs, rhs );

    const auto checkRowsEqual = []( const auto& left, const auto& right )
    {
        REQUIRE( left.size() == right.size() );
        for ( std::size_t index = 0; index < left.size(); ++index )
        {
            CHECK( left[index] == right[index] );
        }
    };

    // Invariant: multithreaded determinism includes cold sleep ownership and
    // diagnostics, not only visible poses. A schedule-dependent transition can
    // leave kinematics equal for one tick while changing future awake work.
    checkRowsEqual( SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( lhs ),
                    SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( rhs ) );
    checkRowsEqual( SkullbonezCore::Physics::PhysicsEngine::ReadSleepSupportedStates( lhs ),
                    SkullbonezCore::Physics::PhysicsEngine::ReadSleepSupportedStates( rhs ) );
    checkRowsEqual( SkullbonezCore::Physics::PhysicsEngine::ReadSleepInhibitedStates( lhs ),
                    SkullbonezCore::Physics::PhysicsEngine::ReadSleepInhibitedStates( rhs ) );
    checkRowsEqual( SkullbonezCore::Physics::PhysicsEngine::ReadSleepIslandVisualIds( lhs ),
                    SkullbonezCore::Physics::PhysicsEngine::ReadSleepIslandVisualIds( rhs ) );
}
} // namespace


TEST_CASE( "PhysicsEngine determinism: micro-world matches at fixed tick intervals" )
{
    auto firstStorage = std::make_unique<PhysicsEngine>();
    auto secondStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& first = *firstStorage;
    PhysicsEngine& second = *secondStorage;
    SeedMicroWorld( first );
    SeedMicroWorld( second );

    for ( int tick = 60; tick <= kTotalDeterminismTicks; tick += 60 )
    {
        StepMicroWorld( first, 60 );
        StepMicroWorld( second, 60 );
        CheckEngineKinematicsEqual( first, second );
    }
}


TEST_CASE( "PhysicsEngine multithreaded determinism: contact and sleep pipeline is exact across worker counts" )
{
    // Lifetime: PhysicsEngine owns large fixed-capacity stores, so these cold
    // test fixtures live on the heap while all three worker variants coexist.
    auto serial = std::make_unique<PhysicsEngine>();
    auto oneWorker = std::make_unique<PhysicsEngine>();
    auto fourWorkers = std::make_unique<PhysicsEngine>();

    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.physicsExecution.parallel = true;
    config.physicsExecution.parallelApplyForces = true;
    config.physicsExecution.parallelNarrowphase = true;
    config.physicsExecution.parallelTerrainDetect = true;
    config.physicsExecution.parallelIntegrate = true;
    config.broadphase.cellSize = 4.0f;
    config.physicsSleep.frames = 3;
    config.physicsSleep.linearSpeed = 0.25f;
    config.physicsSleep.angularSpeed = 0.25f;

    SeedParallelContactSleepWorld( *serial, config );
    SeedParallelContactSleepWorld( *oneWorker, config );
    SeedParallelContactSleepWorld( *fourWorkers, config );
    const PhysicsWorldForces forces = DeterministicForces();

    StepMicroWorldWith( *serial, 1, forces, 0 );
    StepMicroWorldWith( *oneWorker, 1, forces, 1 );
    StepMicroWorldWith( *fourWorkers, 1, forces, 4 );
    // Invariant: the fixture must keep the parallel-narrowphase threshold
    // active. A geometry/filter drift to 255 pairs would otherwise let every
    // worker-count comparison pass through the serial fallback.
    CHECK( oneWorker->GetDiagnosticsView().candidatePairs.size() == 256u );
    CHECK( fourWorkers->GetDiagnosticsView().candidatePairs.size() == 256u );
    CheckEngineWorkerDeterministicStateEqual( *serial, *oneWorker );
    CheckEngineWorkerDeterministicStateEqual( *serial, *fourWorkers );

    StepMicroWorldWith( *serial, 30, forces, 0 );
    StepMicroWorldWith( *oneWorker, 30, forces, 1 );
    StepMicroWorldWith( *fourWorkers, 30, forces, 4 );
    CheckEngineWorkerDeterministicStateEqual( *serial, *oneWorker );
    CheckEngineWorkerDeterministicStateEqual( *serial, *fourWorkers );

    const auto sleepStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( *serial );
    CHECK( std::any_of( sleepStates.begin(), sleepStates.end(), []( uint8_t sleeping ) { return sleeping != 0; } ) );
}


TEST_CASE( "Tornado external-force lane is byte-exact across serial and parallel body partitions" )
{
    constexpr int bodyCount = kParallelContactBodyCount;
    auto serial = std::make_unique<PhysicsEngine>();
    auto parallel = std::make_unique<PhysicsEngine>();
    EngineConfig config = MakeDeterministicConfig();
    config.physicsExecution.parallel = true;
    serial->ApplyRuntimeConfig( config );
    parallel->ApplyRuntimeConfig( config );
    serial->SetSleepEnabled( false );
    parallel->SetSleepEnabled( false );
    for ( int index = 0; index < bodyCount; ++index )
    {
        const Vector3 position( -95.0f + static_cast<float>( index % 20 ) * 10.0f,
                                20.0f + static_cast<float>( index / 20 ) * 5.0f,
                                -40.0f + static_cast<float>( index % 9 ) * 10.0f );
        const Vector3 zeroVelocity( 0.0f, 0.0f, 0.0f );
        AddMicroBody( *serial, 2000u + static_cast<uint32_t>( index ), position, zeroVelocity );
        AddMicroBody( *parallel, 2000u + static_cast<uint32_t>( index ), position, zeroVelocity );
    }

    SkullbonezCore::Gameplay::TornadoFieldConfig field;
    field.enabled = true;
    field.center = Vector3( 0.0f, 0.0f, 0.0f );
    field.radius = 500.0f;
    field.height = 500.0f;
    field.minCaptureSeconds = 1000.0f;
    field.maxDeltaVelocity = 1000.0f;
    SkullbonezCore::Gameplay::TornadoGameplay serialGameplay;
    SkullbonezCore::Gameplay::TornadoGameplay parallelGameplay;
    serialGameplay.SetFieldConfig( field );
    parallelGameplay.SetFieldConfig( field );
    serialGameplay.SetParallelForceEvaluation( false );
    parallelGameplay.SetParallelForceEvaluation( true );

    LockOrderValidator serialLockOrder;
    LockOrderValidator parallelLockOrder;
    WorkerPool serialWorkers( serialLockOrder );
    WorkerPool parallelWorkers( parallelLockOrder );
    serialWorkers.Initialise( 4 );
    parallelWorkers.Initialise( 4 );
    serial->Step( PHYSICS_FIXED_DT,
                  NoGravityForces(),
                  serialGameplay.BuildForceFrame( PHYSICS_FIXED_DT, bodyCount ),
                  serialWorkers,
                  SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );
    parallel->Step( PHYSICS_FIXED_DT,
                    NoGravityForces(),
                    parallelGameplay.BuildForceFrame( PHYSICS_FIXED_DT, bodyCount ),
                    parallelWorkers,
                    SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );

    CheckEngineKinematicsEqual( *serial, *parallel );
    CheckVectorContentsEqual( serialGameplay.CaptureSeconds(), parallelGameplay.CaptureSeconds() );
    CheckVectorContentsEqual( serialGameplay.EjectCooldownSeconds(), parallelGameplay.EjectCooldownSeconds() );
}


TEST_CASE( "PhysicsEngine mutual gravity: pair force is antisymmetric" )
{
    auto pairWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& pairWorld = *pairWorldStorage;
    SeedTwoBodyGravityWorld( pairWorld,
                             Vector3( -12.0f, 80.0f, 0.0f ),
                             Vector3( 12.0f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             5.0f,
                             0.5f );

    const PhysicsWorldForces forces = MutualGravityForces( 120.0f, 0.25f );

    StepMicroWorldWith( pairWorld, 1, forces );
    const PhysicsBodyHotState left = RequireBodyHotState( pairWorld, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( pairWorld, 1 );
    CHECK( left.linearVelocity.x > 0.0f );
    CHECK( right.linearVelocity.x < 0.0f );
    CHECK( left.linearVelocity.x == doctest::Approx( -right.linearVelocity.x ).epsilon( 0.0001 ) );
    CHECK( left.linearVelocity.y == doctest::Approx( right.linearVelocity.y ).epsilon( 0.0001 ) );
    CHECK( left.linearVelocity.z == doctest::Approx( right.linearVelocity.z ).epsilon( 0.0001 ) );
}


TEST_CASE( "PhysicsEngine mutual gravity: softening keeps near pairs finite" )
{
    auto closeWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& closeWorld = *closeWorldStorage;
    SeedTwoBodyGravityWorld( closeWorld,
                             Vector3( 0.0f, 80.0f, 0.0f ),
                             Vector3( 0.03f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             3.0f,
                             0.01f );

    const PhysicsWorldForces forces = MutualGravityForces( 1000.0f, 5.0f );

    StepMicroWorldWith( closeWorld, 1, forces );
    const PhysicsBodyHotState left = RequireBodyHotState( closeWorld, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( closeWorld, 1 );
    CHECK( std::isfinite( left.linearVelocity.x ) );
    CHECK( std::isfinite( right.linearVelocity.x ) );
    CHECK( fabsf( left.linearVelocity.x ) < 1.0f );
    CHECK( fabsf( right.linearVelocity.x ) < 1.0f );
}


TEST_CASE( "PhysicsEngine mutual gravity: equal-mass two-body orbit stays bounded" )
{
    auto orbitWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& orbitWorld = *orbitWorldStorage;
    const float orbitRadius = 20.0f;
    const float separation = orbitRadius * 2.0f;
    const float mass = 20.0f;
    const float gravitationalConstant = 20.0f;
    const float softeningLength = 0.1f;
    const float softenedDistanceSq = separation * separation + softeningLength * softeningLength;
    const float acceleration =
        gravitationalConstant * mass * separation / ( softenedDistanceSq * sqrtf( softenedDistanceSq ) );
    const float orbitalSpeed = sqrtf( acceleration * orbitRadius );

    SeedTwoBodyGravityWorld( orbitWorld,
                             Vector3( -orbitRadius, 80.0f, 0.0f ),
                             Vector3( orbitRadius, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, -orbitalSpeed ),
                             Vector3( 0.0f, 0.0f, orbitalSpeed ),
                             mass,
                             0.5f );

    const PhysicsWorldForces forces = MutualGravityForces( gravitationalConstant, softeningLength );

    StepMicroWorldWith( orbitWorld, 300, forces );
    const PhysicsBodyHotState left = RequireBodyHotState( orbitWorld, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( orbitWorld, 1 );
    const Vector3 barycenter = ( left.position + right.position ) * 0.5f;
    const float finalSeparation = sqrtf( VectorMagnitudeSquared( right.position - left.position ) );
    CHECK( barycenter.x == doctest::Approx( 0.0f ).epsilon( 0.001 ) );
    CHECK( barycenter.y == doctest::Approx( 80.0f ).epsilon( 0.001 ) );
    CHECK( barycenter.z == doctest::Approx( 0.0f ).epsilon( 0.001 ) );
    CHECK( finalSeparation == doctest::Approx( separation ).epsilon( 0.10 ) );
}


TEST_CASE( "PhysicsEngine mutual gravity: chaotic triple is deterministic" )
{
    auto firstStorage = std::make_unique<PhysicsEngine>();
    auto secondStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& first = *firstStorage;
    PhysicsEngine& second = *secondStorage;
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;

    auto seedTriple = [&config]( PhysicsEngine& engine )
    {
        engine.Clear();
        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        engine.ReserveAuthoredBodyCapacity( 3 );
        AddMutualGravityBody( engine,
                              301u,
                              Vector3( -18.0f, 90.0f, 0.0f ),
                              Vector3( 0.8f, 0.0f, -1.0f ),
                              12.0f,
                              0.45f );
        AddMutualGravityBody( engine, 302u, Vector3( 16.0f, 90.0f, 4.0f ), Vector3( -0.4f, 0.0f, 1.1f ), 16.0f, 0.45f );
        AddMutualGravityBody( engine,
                              303u,
                              Vector3( 2.0f, 90.0f, 24.0f ),
                              Vector3( -0.2f, 0.0f, -0.8f ),
                              10.0f,
                              0.45f );
        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 3 );
        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == 3 );
    };

    seedTriple( first );
    seedTriple( second );
    const PhysicsWorldForces forces = MutualGravityForces( 45.0f, 0.35f );
    StepMicroWorldWith( first, 240, forces );
    StepMicroWorldWith( second, 240, forces );
    CheckEngineKinematicsEqual( first, second );
}


TEST_CASE( "PhysicsEngine mutual gravity: parallel pair build is exact across worker counts" )
{
    CheckMutualGravityFieldExactAcrossWorkerCounts( kParallelMutualGravityBodyCount, 20 );
}


TEST_CASE( "PhysicsEngine mutual gravity: large fields use an exact serial fallback" )
{
    CheckMutualGravityFieldExactAcrossWorkerCounts( kLargeMutualGravityBodyCount, 1 );
}


TEST_CASE( "PhysicsEngine mutual gravity: elastic space collision preserves closing speed" )
{
    auto collisionWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& collisionWorld = *collisionWorldStorage;
    const float mass = 2.0f;
    const float radius = 1.0f;
    const float speed = 4.0f;
    SeedTwoBodyGravityWorld( collisionWorld,
                             Vector3( -0.9f, 80.0f, 0.0f ),
                             Vector3( 0.9f, 80.0f, 0.0f ),
                             Vector3( speed, 0.0f, 0.0f ),
                             Vector3( -speed, 0.0f, 0.0f ),
                             mass,
                             radius );

    PhysicsWorldForces forces = MutualGravityForces( 0.001f, 1.0f );
    REQUIRE( forces.mutualGravity.elasticCollisions );

    const float initialEnergy = TotalKineticEnergy( collisionWorld );
    StepMicroWorldWith( collisionWorld, 1, forces );
    const PhysicsBodyHotState left = RequireBodyHotState( collisionWorld, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( collisionWorld, 1 );
    const float finalEnergy = TotalKineticEnergy( collisionWorld );

    CHECK( left.linearVelocity.x < -speed * 0.98f );
    CHECK( right.linearVelocity.x > speed * 0.98f );
    CHECK( finalEnergy == doctest::Approx( initialEnergy ).epsilon( 0.01 ) );
}


TEST_CASE( "PhysicsEngine invariants: settled bodies stay within terrain penetration tolerance" )
{
    auto settledStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& settled = *settledStorage;
    SeedMicroWorld( settled );

    const SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    const PhysicsWorldForces forces = DeterministicForces();
    StepMicroWorldWith( settled, kPenetrationSettleTicks, forces );

    CheckTerrainPenetrationWithinTolerance( settled, config );
}


TEST_CASE( "PhysicsEngine invariants: fluid damping does not add kinetic energy" )
{
    auto dampedStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& damped = *dampedStorage;
    SeedMicroWorld( damped );

    const PhysicsWorldForces forces = DampingForces();

    const float initialEnergy = TotalKineticEnergy( damped );
    REQUIRE( initialEnergy > 0.0f );

    float previousEnergy = initialEnergy;
    for ( int tick = 0; tick < 12; ++tick )
    {
        StepMicroWorldWith( damped, 1, forces );
        const float currentEnergy = TotalKineticEnergy( damped );
        CHECK( currentEnergy <= previousEnergy + kDampingEnergyTolerance );
        previousEnergy = currentEnergy;
    }
    CHECK( previousEnergy < initialEnergy );
}


TEST_CASE( "PhysicsEngine invariants: authored velocity wakes a sleeping body" )
{
    auto sleepWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& sleepWorld = *sleepWorldStorage;
    SeedMicroWorld( sleepWorld );
    sleepWorld.SetSleepEnabled( true );

    const PhysicsWorldForces forces = NoGravityForces();

    const PhysicsBodyHandle body = RequireBodyHandle( sleepWorld, 0 );
    sleepWorld.SeedBodyAsleep( body );
    CHECK_FALSE( RequireBodyHotState( sleepWorld, 0 ).awake );
    CHECK( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    const Vector3 positionBeforeWake = RequireBodyHotState( sleepWorld, 0 ).position;
    REQUIRE( sleepWorld.SetBodyVelocity( body, Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), true ) );
    CHECK( RequireBodyHotState( sleepWorld, 0 ).awake );
    CHECK_FALSE( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    StepMicroWorldWith( sleepWorld, 1, forces );
    CHECK( RequireBodyHotState( sleepWorld, 0 ).position.x > positionBeforeWake.x );
}


TEST_CASE( "PhysicsEngine sleep policy: quiet supported body sleeps after threshold frames" )
{
    auto sleepWorldStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& sleepWorld = *sleepWorldStorage;

    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.physicsSleep.frames = 3;
    config.physicsSleep.linearSpeed = 0.25f;
    config.physicsSleep.angularSpeed = 0.25f;
    const PhysicsWorldForces forces = DeterministicForces();

    SeedSupportedSleepWorld( sleepWorld, config );
    StepMicroWorldWith( sleepWorld, config.physicsSleep.frames + 24, forces );

    CHECK_FALSE( RequireBodyHotState( sleepWorld, 0 ).awake );
    CHECK( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    const PhysicsBodyHandle body = RequireBodyHandle( sleepWorld, 0 );
    const Vector3 positionBeforeWake = RequireBodyHotState( sleepWorld, 0 ).position;
    sleepWorld.ApplyBodyImpulse( body, Vector3( 12.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    CHECK( RequireBodyHotState( sleepWorld, 0 ).awake );
    CHECK_FALSE( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    StepMicroWorldWith( sleepWorld, 1, forces );
    CHECK( RequireBodyHotState( sleepWorld, 0 ).position.x > positionBeforeWake.x );
}


TEST_CASE( "PhysicsEngine determinism: solver snapshot plus body state restores losslessly" )
{
    auto interruptedStorage = std::make_unique<PhysicsEngine>();
    auto restoredStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& interrupted = *interruptedStorage;
    PhysicsEngine& restored = *restoredStorage;
    SeedMicroWorld( interrupted );
    SeedMicroWorld( restored );

    StepMicroWorld( interrupted, kSnapshotFrame );
    StepMicroWorld( restored, kSnapshotFrame );
    const MicroWorldSnapshot snapshot = CaptureMicroWorldSnapshot( restored );

    StepMicroWorld( interrupted, kReplayWindowTicks );
    StepMicroWorld( restored, kReplayWindowTicks );
    RestoreMicroWorldSnapshot( restored, snapshot );
    StepMicroWorld( restored, kReplayWindowTicks );

    CheckEngineKinematicsEqual( interrupted, restored );
}


TEST_CASE( "Replay solver sample restore: recorded frame reproduces future frame" )
{
    auto expectedStorage = std::make_unique<PhysicsEngine>();
    auto restoredStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& expected = *expectedStorage;
    PhysicsEngine& restored = *restoredStorage;
    SeedMicroWorld( expected );
    SeedMicroWorld( restored );

    StepMicroWorld( expected, kReplaySampleSnapshotFrame );
    StepMicroWorld( restored, kReplaySampleSnapshotFrame );
    const ReplaySolverFrameSample restorePoint =
        CaptureMicroWorldReplaySample( restored, static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame ) );

    StepMicroWorld( expected, kReplaySampleWindowTicks );
    const ReplaySolverFrameSample expectedFuture = CaptureMicroWorldReplaySample(
        expected,
        static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame + kReplaySampleWindowTicks ) );

    StepMicroWorld( restored, kReplaySampleWindowTicks );
    RestoreMicroWorldReplaySample( restored, restorePoint );
    StepMicroWorld( restored, kReplaySampleWindowTicks );
    const ReplaySolverFrameSample restoredFuture = CaptureMicroWorldReplaySample(
        restored,
        static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame + kReplaySampleWindowTicks ) );

    CheckReplaySamplesEqual( expectedFuture, restoredFuture );
}

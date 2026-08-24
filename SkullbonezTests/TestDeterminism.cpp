//
// File: SkullbonezTests/TestDeterminism.cpp
// Purpose:
//   Lock fast PhysicsEngine determinism and physics invariant properties.
//
// Summary:
//   A minimal authored physics world can be seeded directly through
//   PhysicsEngine without SceneController or scene-load plumbing. Fixed-step
//   determinism means two engines with identical body/collider rows produce the
//   same byte-level kinematic, sleep, contact, and motion-classification state
//   at the same tick boundaries.
//
// Glossary:
//   Micro-world: Tiny unit-test physics scene with a few authored bodies and
//     colliders, deliberately below every worker-dispatch threshold.
//   Solver snapshot: Physics-owned PhysicsWorld state captured separately from
//     body-store poses and velocities.
//   Body replay state: Store-owned body row fields restored through
//     PhysicsEngine::RestoreReplayBodyState.
//   Physics-only terrain: CPU-domain analytic slope used without renderer
//     construction or a backend test double.
//   Terrain fixture: Per-test owner that destroys its heap PhysicsEngine before
//     the analytic terrain and config backing the engine's retained view.
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
//     threshold whose contact, terrain, joint, integration, and sleep state is
//     compared at 0, 1, and 4 workers.
//   Large gravity field: 520-body fixture above the pair-scratch threshold that
//     proves the exact serial fallback ignores worker availability.
//
// Invariants:
//   - The micro-world stays serial; worker fan-out starts far above this body count.
//   - Snapshot losslessness needs both solver state and body replay state.
//   - Replay restore commits body rows before solver state, then proves retained
//     motion hysteresis by advancing a velocity inside the policy band.
//   - Kinematic comparisons are byte-exact, not epsilon-based.
//   - Invariant checks use explicit tolerances because they assert physical
//     policy, not serialized replay bytes.
//   - Terrain queries are real flat-plane queries; render resources must stay unused.
//   - Every terrain-bearing engine is owned by a per-test fixture; no borrowed
//     terrain view survives its terrain or construction config.
//   - Reconstructing alternating flat/deep fixtures yields the same exact hash
//     for each terrain, independent of the fixture that ran immediately before.
//   - Worker scheduling must not change kinematics, ordered collision work,
//     point-joint rows, or replay-restorable solver state.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsEngine.h
//   - SkullbonezSource/Physics/PhysicsMotionEligibility.h
//   - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
//   - SkullbonezTests/TestReplayDeterminism.cpp
//   - SkullbonezTests/TestReplaySolverHashWitness.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"
#include "TestReplaySolverHashWitness.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsMotionEligibility.h"
#include "../SkullbonezSource/Physics/PhysicsDiagnosticsSink.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"
#include "../SkullbonezSource/Gameplay/TornadoGameplay.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
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
using SkullbonezCore::Physics::PhysicsTerrainView;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore resultDiagnostics;

void ReserveTestPhysicsCapacity( PhysicsEngine& engine, std::size_t capacity, std::size_t pointJointCapacity = 0u )
{
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    engine.ReserveAuthoredBodyCapacity( capacity, 0u, 0u, 0u, pointJointCapacity );
}

constexpr int kMicroBodyCount = 3;
constexpr int kParallelMutualGravityBodyCount = 40;
constexpr int kParallelContactBodyCount = 520;
constexpr int kLargeMutualGravityBodyCount = 520;
constexpr int kSnapshotFrame = 120;
constexpr int kReplayWindowTicks = 60;
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

// Invariant: PhysicsEngine retains the terrain view across Clear(), so member
// declaration order must remain config, terrain, then heap engine. Reverse
// destruction then retires the borrower before either retained-view owner.
// The determinism tests exercise this owner through every flat/deep engine.
class DeterminismTerrainFixture final
{
  public:
    explicit DeterminismTerrainFixture( float terrainBaseY )
        : m_config( MakeDeterministicConfig() ), m_terrain( terrainBaseY, 0.0f, 0.0f, m_config ),
          m_engine( std::make_unique<PhysicsEngine>() )
    {
        m_engine->SetTerrainView( m_terrain.PhysicsView() );
    }

    DeterminismTerrainFixture( const DeterminismTerrainFixture& ) = delete;
    DeterminismTerrainFixture& operator=( const DeterminismTerrainFixture& ) = delete;
    DeterminismTerrainFixture( DeterminismTerrainFixture&& ) = delete;
    DeterminismTerrainFixture& operator=( DeterminismTerrainFixture&& ) = delete;

    PhysicsEngine& Engine() noexcept
    {
        return *m_engine;
    }

    PhysicsTerrainView TerrainView() const noexcept
    {
        return m_terrain.PhysicsView();
    }

  private:
    EngineConfig m_config;
    Terrain m_terrain;
    std::unique_ptr<PhysicsEngine> m_engine;
};

constexpr float kFlatTerrainBaseY = 0.0f;
constexpr float kDeepSpaceTerrainBaseY = -100000.0f;

CollisionShape MakeSphereShape( float radius )
{
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

void AddPromotionFixtureBody( PhysicsEngine& engine, PhysicsTerrainView terrainView, uint32_t sceneObjectIdValue,
                              const CollisionShape& shape, const Vector3& position, const Vector3& linearVelocity,
                              const Vector3& rotationalInertia, float mass, float restitution,
                              PhysicsBodyMotionKind motionKind, const char* diagnosticName )
{
    engine.SetTerrainView( terrainView );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { sceneObjectIdValue }, shape, position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION, linearVelocity,
                                               Vector3( 0.0f, 0.0f, 0.0f ), rotationalInertia, mass, restitution,
                                               motionKind, diagnosticName );
    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, restitution, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
        SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void AddMicroBody( PhysicsEngine& engine, PhysicsTerrainView terrainView, uint32_t sceneObjectIdValue,
                   const Vector3& position, const Vector3& linearVelocity )
{
    ReserveTestPhysicsCapacity( engine, kMicroBodyCount );
    engine.SetTerrainView( terrainView );
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { sceneObjectIdValue }, shape, position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION, linearVelocity,
                                               Vector3( 0.03f * static_cast<float>( sceneObjectIdValue ), 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ), mass, 0.0f,
                                               PhysicsBodyMotionKind::Dynamic, "unit-determinism-body" );

    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void AddSupportedSleepBody( PhysicsEngine& engine, PhysicsTerrainView terrainView, uint32_t sceneObjectIdValue,
                            const Vector3& position, std::size_t pointJointCapacity = 0u )
{
    ReserveTestPhysicsCapacity( engine, kParallelContactBodyCount, pointJointCapacity );
    engine.SetTerrainView( terrainView );
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { sceneObjectIdValue }, shape, position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ), mass, 0.0f,
                                               PhysicsBodyMotionKind::Dynamic, "unit-sleep-threshold-body" );

    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void SeedSupportedSleepWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView,
                              const SkullbonezCore::Core::EngineConfig& config )
{

    // Why: the sleep-threshold test needs real terrain support but no inherited
    // angular motion from SeedMicroWorld. One quiet sphere starts exactly on the
    // flat terrain so the solver, not fixture surgery, earns the sleep state.
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    AddSupportedSleepBody( engine, terrainView, 401u, Vector3( 0.0f, 1.0f, 0.0f ) );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 1 );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == 1 );
}

void SeedParallelContactSleepWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView,
                                    const SkullbonezCore::Core::EngineConfig& config )
{
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    ReserveTestPhysicsCapacity( engine, kParallelContactBodyCount );

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
        AddSupportedSleepBody( engine, terrainView, static_cast<uint32_t>( 1000 + bodyIndex ),
                               Vector3( static_cast<float>( pairColumn * 8 - 76 ) + pairOffset, 1.0f,
                                        static_cast<float>( pairRow * 8 - 48 ) ),
                               2u );
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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, fixture.TerrainView(), 901u, Vector3( 100.0f, 50.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );

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
    engine.Step( PHYSICS_FIXED_DT, NoGravityForces(), tornadoGameplay.BuildForceFrame( PHYSICS_FIXED_DT, 1 ), workers,
                 SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

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

void AddMutualGravityBody( PhysicsEngine& engine, PhysicsTerrainView terrainView, uint32_t sceneObjectIdValue,
                           const Vector3& position, const Vector3& linearVelocity, float mass, float radius,
                           PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic )
{
    engine.SetTerrainView( terrainView );

    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { sceneObjectIdValue }, shape, position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION, linearVelocity,
                                               Vector3( 0.0f, 0.0f, 0.0f ), Vector3( inertia, inertia, inertia ), mass, 0.0f,
                                               motionKind, "unit-mutual-gravity-body" );

    bodyDesc.angularVelocityLimit = 1000.0f;
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
}

void SeedAuthoredSolarWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView,
                             const SkullbonezCore::Runtime::AuthoredScene& scene, bool earthGravityEnabled )
{
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    ReserveTestPhysicsCapacity( engine, scene.GetBallStateCount() );

    for ( int index = 0; index < scene.GetBallStateCount(); ++index )
    {
        const SkullbonezCore::Runtime::SceneBallState& body = scene.GetBallState( index );
        const bool removeEarthGravity = !earthGravityEnabled && std::strcmp( body.name, "earth" ) == 0;
        AddMutualGravityBody( engine, terrainView,
                              body.sceneObjectId.value != 0u ? body.sceneObjectId.value
                                                             : static_cast<uint32_t>( 1000 + index ),
                              Vector3( body.posX, body.posY, body.posZ ), Vector3( body.velX, body.velY, body.velZ ),
                              removeEarthGravity ? 0.001f : body.mass, body.radius,
                              body.isFixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic );
    }

    REQUIRE( PhysicsEngine::ReadBodies( engine ).Count() == scene.GetBallStateCount() );
}

void SeedMicroWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView )
{
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, terrainView, 101u, Vector3( 100.0f, 30.0f, 100.0f ), Vector3( 1.5f, 0.0f, 0.0f ) );
    AddMicroBody( engine, terrainView, 102u, Vector3( 112.0f, 40.0f, 100.0f ), Vector3( 0.0f, 0.5f, 0.0f ) );
    AddMicroBody( engine, terrainView, 103u, Vector3( 124.0f, 50.0f, 100.0f ), Vector3( -1.0f, 0.0f, 0.0f ) );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == kMicroBodyCount );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == kMicroBodyCount );
}

void SeedTwoBodyGravityWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView, const Vector3& leftPosition,
                              const Vector3& rightPosition, const Vector3& leftVelocity, const Vector3& rightVelocity,
                              float mass, float radius )
{
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    ReserveTestPhysicsCapacity( engine, 2 );
    AddMutualGravityBody( engine, terrainView, 201u, leftPosition, leftVelocity, mass, radius );
    AddMutualGravityBody( engine, terrainView, 202u, rightPosition, rightVelocity, mass, radius );
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
        engine.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
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

    // Lifetime: each cold fixture owns an independent analytic terrain and
    // heap engine. Equality cannot pass through shared mutable terrain state.
    DeterminismTerrainFixture serialFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture oneWorkerFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture fourWorkersFixture( kFlatTerrainBaseY );
    PhysicsEngine& serial = serialFixture.Engine();
    PhysicsEngine& oneWorker = oneWorkerFixture.Engine();
    PhysicsEngine& fourWorkers = fourWorkersFixture.Engine();

    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.physicsExecution.parallel = true;
    config.physicsExecution.parallelMutualGravity = true;
    config.worldForces.gravity = 0.0f;

    auto seedField = [&config, bodyCount]( PhysicsEngine& engine, PhysicsTerrainView terrainView )
    {
        engine.Clear();

        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        ReserveTestPhysicsCapacity( engine, bodyCount );

        for ( int index = 0; index < bodyCount; ++index )
        {
            const int column = index % 8;
            const int row = index / 8;
            // Why: the bounded parallel case deliberately places adjacent
            // fixed bodies across chunked rows. Their skipped pairs create
            // holes in worker slices and exercise canonical compaction, while
            // the 520-body case remains the all-dynamic serial-fallback proof.
            const bool createReceiverGap = bodyCount == kParallelMutualGravityBodyCount && index % 9 < 2;
            AddMutualGravityBody( engine, terrainView, static_cast<uint32_t>( 300u + index ),
                                  Vector3( static_cast<float>( column * 20 - 70 ), static_cast<float>( 100 + row * 17 ),
                                           static_cast<float>( ( index * 13 ) % 29 - 14 ) ),
                                  Vector3( static_cast<float>( ( index % 5 ) - 2 ) * 0.03f,
                                           static_cast<float>( ( index % 3 ) - 1 ) * 0.02f,
                                           static_cast<float>( ( index % 7 ) - 3 ) * 0.01f ),
                                  1.0f + static_cast<float>( index % 11 ) * 0.25f, 0.5f,
                                  createReceiverGap ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic );
        }

        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == bodyCount );
    };

    seedField( serial, serialFixture.TerrainView() );
    seedField( oneWorker, oneWorkerFixture.TerrainView() );
    seedField( fourWorkers, fourWorkersFixture.TerrainView() );
    const PhysicsWorldForces forces = MutualGravityForces( 45.0f, 0.35f );

    StepMicroWorldWith( serial, ticks, forces, 0 );
    StepMicroWorldWith( oneWorker, ticks, forces, 1 );
    StepMicroWorldWith( fourWorkers, ticks, forces, 4 );

    CheckEngineKinematicsEqual( serial, oneWorker );
    CheckEngineKinematicsEqual( serial, fourWorkers );
}

const PhysicsBodyRecord& RequireBodyRecord( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( modelIndex );
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

void SeedParallelDeterminismPointJoints( PhysicsEngine& engine )
{
    constexpr std::pair<int, int> jointBodies[] = { { 512, 513 }, { 514, 515 } };

    for ( const auto [bodyA, bodyB] : jointBodies )
    {
        SkullbonezCore::Physics::PhysicsPointJointCreateDesc joint;
        joint.bodyA = RequireBodyHandle( engine, bodyA );
        joint.bodyB = RequireBodyHandle( engine, bodyB );
        joint.slack = 5.0f;
        REQUIRE( engine.CreatePointJoint( joint ).IsValid() );
    }
}

float VectorMagnitudeSquared( const Vector3& value )
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

float BodyKineticEnergy( const PhysicsBodyRecord& record, const PhysicsBodyHotState& hotState )
{
    const float translational = 0.5f * record.mass * VectorMagnitudeSquared( hotState.linearVelocity );
    const float angular = 0.5f * ( record.rotationalInertia.x * hotState.angularVelocity.x * hotState.angularVelocity.x +
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

bool DiagnosticsHasCandidatePair( const PhysicsEngine& engine, int bodyA, int bodyB )
{
    if ( bodyA > bodyB )
    {
        std::swap( bodyA, bodyB );
    }

    const auto pairs = engine.GetDiagnosticsView().candidatePairs;
    return std::find( pairs.begin(), pairs.end(), std::make_pair( bodyA, bodyB ) ) != pairs.end();
}

bool DiagnosticsHasPipelineStageForPair( const PhysicsEngine& engine,
                                         SkullbonezCore::Physics::PhysicsPipelineStage stage, int bodyA, int bodyB )
{
    for ( const SkullbonezCore::Physics::PhysicsPipelineRecord& record :
          engine.GetDiagnosticsView().physicsPipelineTrace )
    {
        const bool samePair = ( record.bodyA == bodyA && record.bodyB == bodyB ) ||
                              ( record.bodyA == bodyB && record.bodyB == bodyA );

        if ( samePair && record.stage == stage )
        {
            return true;
        }
    }

    return false;
}

void CheckTerrainPenetrationWithinTolerance( const PhysicsEngine& engine, const SkullbonezCore::Core::EngineConfig& config )
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

void HashPhysicsBytesForTest( uint64_t& hash, const void* data, std::size_t byteCount )
{
    const uint8_t* bytes = static_cast<const uint8_t*>( data );

    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash ^= static_cast<uint64_t>( bytes[i] );
        hash *= 1099511628211ull;
    }
}

template <typename T> void HashPhysicsValueForTest( uint64_t& hash, const T& value )
{
    HashPhysicsBytesForTest( hash, &value, sizeof( T ) );
}

uint64_t HashMicroWorldPhysicsState( const PhysicsEngine& engine )
{
    uint64_t hash = 1469598103934665603ull;
    const int bodyCount = PhysicsEngine::ReadBodies( engine ).Count();
    HashPhysicsValueForTest( hash, bodyCount );

    for ( int modelIndex = 0; modelIndex < bodyCount; ++modelIndex )
    {
        const PhysicsBodyHotState hotState = RequireBodyHotState( engine, modelIndex );
        float orientation[4] = {};
        hotState.orientation.GetComponents( orientation[0], orientation[1], orientation[2], orientation[3] );
        HashPhysicsValueForTest( hash, hotState.position );
        HashPhysicsBytesForTest( hash, orientation, sizeof( orientation ) );
        HashPhysicsValueForTest( hash, hotState.linearVelocity );
        HashPhysicsValueForTest( hash, hotState.angularVelocity );
        HashPhysicsValueForTest( hash, hotState.awake );
    }
    return hash != 0ull ? hash : 1ull;
}

MicroWorldSnapshot CaptureMicroWorldSnapshot( const PhysicsEngine& engine )
{
    MicroWorldSnapshot snapshot;
    engine.CaptureReplaySolverSnapshot( snapshot.solver, MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) );

    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( i );
        REQUIRE( record != nullptr );
        snapshot.bodies[static_cast<std::size_t>( i )] = CaptureBodyReplayState( *record, RequireBodyHotState( engine, i ) );
    }

    return snapshot;
}

void RestoreMicroWorldSnapshot( PhysicsEngine& engine, const MicroWorldSnapshot& snapshot )
{
    for ( const BodyReplayState& state : snapshot.bodies )
    {
        REQUIRE( engine.RestoreReplayBodyState( PhysicsBodyRestoreState { state.handle, state.sceneObjectId, state.fixed, state.position, state.orientation,
                                                                          state.linearVelocity, state.angularVelocity, state.mass, state.inverseMass,
                                                                          state.rotationalInertia, state.inverseRotationalInertia } ) );
    }

    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot.solver,
                                                 MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) ) );
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

void CheckEngineKinematicsEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).Count() ==
             SkullbonezCore::Physics::PhysicsEngine::ReadBodies( rhs ).Count() );

    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).Count(); ++i )
    {
        const PhysicsBodyRecord* left = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( lhs ).RecordForModelIndex( i );
        const PhysicsBodyRecord* right = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( rhs ).RecordForModelIndex( i );
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

void CheckMotionEligibilityBytesEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    const auto left = lhs.GetDiagnosticsView().motionEligibilityState;
    const auto right = rhs.GetDiagnosticsView().motionEligibilityState;
    REQUIRE( left.size() == right.size() );

    if ( !left.empty() )
    {
        CHECK( std::memcmp( left.data(), right.data(), left.size() ) == 0 );
    }
}

void CheckPhysicsPipelineTraceBytesEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    const auto left = lhs.GetDiagnosticsView().physicsPipelineTrace;
    const auto right = rhs.GetDiagnosticsView().physicsPipelineTrace;
    REQUIRE( left.size() == right.size() );

    for ( std::size_t index = 0; index < left.size(); ++index )
    {
        // Hazard: PhysicsPipelineRecord can contain compiler padding. Replay
        // equality owns every payload field, not indeterminate struct bytes.
        const SkullbonezCore::Physics::PhysicsPipelineRecord& leftRecord = left[index];
        const SkullbonezCore::Physics::PhysicsPipelineRecord& rightRecord = right[index];
        CHECK( leftRecord.stage == rightRecord.stage );
        CHECK( leftRecord.bodyA == rightRecord.bodyA );
        CHECK( leftRecord.bodyB == rightRecord.bodyB );
        CHECK( leftRecord.iteration == rightRecord.iteration );
        CHECK( leftRecord.featureId == rightRecord.featureId );
        CheckVectorBytesEqual( leftRecord.point, rightRecord.point );
        CheckVectorBytesEqual( leftRecord.normal, rightRecord.normal );
        CHECK( std::memcmp( &leftRecord.scalarA, &rightRecord.scalarA, sizeof( leftRecord.scalarA ) ) == 0 );
        CHECK( std::memcmp( &leftRecord.scalarB, &rightRecord.scalarB, sizeof( leftRecord.scalarB ) ) == 0 );
        CHECK( std::memcmp( &leftRecord.scalarC, &rightRecord.scalarC, sizeof( leftRecord.scalarC ) ) == 0 );
    }
}

void CheckContactIdentityOrderEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    const auto left = lhs.GetDiagnosticsView().persistentContacts;
    const auto right = rhs.GetDiagnosticsView().persistentContacts;
    REQUIRE( left.size() == right.size() );

    for ( std::size_t index = 0; index < left.size(); ++index )
    {
        CHECK( left[index].bodyA == right[index].bodyA );
        CHECK( left[index].bodyB == right[index].bodyB );
        CHECK( left[index].featureId == right[index].featureId );
        CHECK( left[index].key == right[index].key );
    }
}

void CheckTerrainManifoldOrderEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    const auto left = lhs.GetDiagnosticsView().terrainContactManifolds;
    const auto right = rhs.GetDiagnosticsView().terrainContactManifolds;
    REQUIRE( left.size() == right.size() );

    for ( std::size_t manifoldIndex = 0; manifoldIndex < left.size(); ++manifoldIndex )
    {
        const auto& leftManifold = left[manifoldIndex];
        const auto& rightManifold = right[manifoldIndex];
        CHECK( leftManifold.bodyA == rightManifold.bodyA );
        CHECK( leftManifold.bodyB == rightManifold.bodyB );
        CheckVectorBytesEqual( leftManifold.normal, rightManifold.normal );
        CheckVectorBytesEqual( leftManifold.tangent1, rightManifold.tangent1 );
        CheckVectorBytesEqual( leftManifold.tangent2, rightManifold.tangent2 );
        REQUIRE( leftManifold.pointCount == rightManifold.pointCount );

        for ( uint8_t pointIndex = 0; pointIndex < leftManifold.pointCount; ++pointIndex )
        {
            const auto& leftPoint = leftManifold.points[pointIndex];
            const auto& rightPoint = rightManifold.points[pointIndex];
            CheckVectorBytesEqual( leftPoint.point, rightPoint.point );
            CheckVectorBytesEqual( leftPoint.rA, rightPoint.rA );
            CHECK( std::memcmp( &leftPoint.penetration, &rightPoint.penetration,
                                sizeof( leftPoint.penetration ) ) == 0 );
            CHECK( leftPoint.featureId == rightPoint.featureId );
        }

        CHECK( std::memcmp( &leftManifold.timeOfImpact, &rightManifold.timeOfImpact,
                            sizeof( leftManifold.timeOfImpact ) ) == 0 );
        CHECK( leftManifold.sweptHit == rightManifold.sweptHit );
        CHECK( leftManifold.supportsRestingPolicy == rightManifold.supportsRestingPolicy );
        CHECK( leftManifold.allowsTangentFriction == rightManifold.allowsTangentFriction );
        CHECK( leftManifold.inhibitsSleep == rightManifold.inhibitsSleep );
        CHECK( leftManifold.terrainCellId == rightManifold.terrainCellId );
        CHECK( leftManifold.materialId == rightManifold.materialId );
    }
}

void CheckPointJointOrderEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    const auto& left = PhysicsEngine::ReadPointJointConstraints( lhs );
    const auto& right = PhysicsEngine::ReadPointJointConstraints( rhs );
    REQUIRE( left.size() == right.size() );

    for ( std::size_t index = 0; index < left.size(); ++index )
    {
        const auto& leftJoint = left[index];
        const auto& rightJoint = right[index];
        CHECK( leftJoint.handle == rightJoint.handle );
        CHECK( leftJoint.bodyA == rightJoint.bodyA );
        CHECK( leftJoint.bodyB == rightJoint.bodyB );
        CheckVectorBytesEqual( leftJoint.localAnchorA, rightJoint.localAnchorA );
        CheckVectorBytesEqual( leftJoint.localAnchorB, rightJoint.localAnchorB );
        CHECK( std::memcmp( &leftJoint.slack, &rightJoint.slack, sizeof( leftJoint.slack ) ) == 0 );
        CHECK( std::memcmp( &leftJoint.stiffness, &rightJoint.stiffness, sizeof( leftJoint.stiffness ) ) == 0 );
        CHECK( std::memcmp( &leftJoint.damping, &rightJoint.damping, sizeof( leftJoint.damping ) ) == 0 );
        CHECK( std::memcmp( &leftJoint.accumulatedImpulse, &rightJoint.accumulatedImpulse,
                            sizeof( leftJoint.accumulatedImpulse ) ) == 0 );
        CHECK( leftJoint.groupId == rightJoint.groupId );
        CHECK( leftJoint.flags == rightJoint.flags );
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

    const auto leftDiagnostics = lhs.GetDiagnosticsView();
    const auto rightDiagnostics = rhs.GetDiagnosticsView();
    checkRowsEqual( leftDiagnostics.candidatePairs, rightDiagnostics.candidatePairs );
    checkRowsEqual( leftDiagnostics.collisionCellKeys, rightDiagnostics.collisionCellKeys );
    checkRowsEqual( leftDiagnostics.motionEligibilityState, rightDiagnostics.motionEligibilityState );
    checkRowsEqual( leftDiagnostics.linearTravelSquared, rightDiagnostics.linearTravelSquared );
    checkRowsEqual( leftDiagnostics.angularTravelSquared, rightDiagnostics.angularTravelSquared );
    CHECK( leftDiagnostics.motionEligibilityStats.policyVersion ==
           rightDiagnostics.motionEligibilityStats.policyVersion );
    CHECK( leftDiagnostics.motionEligibilityStats.evaluatedBodies ==
           rightDiagnostics.motionEligibilityStats.evaluatedBodies );
    CHECK( leftDiagnostics.motionEligibilityStats.discreteBodies ==
           rightDiagnostics.motionEligibilityStats.discreteBodies );
    CHECK( leftDiagnostics.motionEligibilityStats.promotedBodies ==
           rightDiagnostics.motionEligibilityStats.promotedBodies );
    CHECK( leftDiagnostics.motionEligibilityStats.angularExpandedBodies ==
           rightDiagnostics.motionEligibilityStats.angularExpandedBodies );

    // Invariant: worker equality owns ordered collision work and the complete
    // replay-restorable solver record, not only poses and summary counters.
    // The MSVC Runtime lane adds the production Replay hash assertion through
    // its test-only witness; portable builds retain every renderer-free case
    // without acquiring Runtime authority. All comparisons remain byte-exact
    // and affect determinism validation rather than a Physics golden baseline.
    CheckContactIdentityOrderEqual( lhs, rhs );
    CheckTerrainManifoldOrderEqual( lhs, rhs );
    CheckPointJointOrderEqual( lhs, rhs );
    CheckPhysicsPipelineTraceBytesEqual( lhs, rhs );
#if !defined( SKULLBONEZ_PORTABLE_CPU )
    SkullbonezTests::CheckProductionReplaySolverHashEqual( lhs, rhs );
#endif
}
} // namespace


TEST_CASE( "PhysicsEngine determinism: micro-world matches at fixed tick intervals" )
{
    DeterminismTerrainFixture firstFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture secondFixture( kFlatTerrainBaseY );
    PhysicsEngine& first = firstFixture.Engine();
    PhysicsEngine& second = secondFixture.Engine();
    SeedMicroWorld( first, firstFixture.TerrainView() );
    SeedMicroWorld( second, secondFixture.TerrainView() );

    for ( int tick = 60; tick <= kTotalDeterminismTicks; tick += 60 )
    {
        StepMicroWorld( first, 60 );
        StepMicroWorld( second, 60 );
        CheckEngineKinematicsEqual( first, second );
    }
}


TEST_CASE( "Physics motion promotion: ordinary slow micro-world body-ticks remain Discrete" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    SeedMicroWorld( engine, fixture.TerrainView() );
    constexpr int measuredTicks = 60;
    int evaluatedBodyTicks = 0;
    int discreteBodyTicks = 0;
    int promotedBodyTicks = 0;

    for ( int tick = 0; tick < measuredTicks; ++tick )
    {
        StepMicroWorldWith( engine, 1, NoGravityForces() );
        const auto& stats = engine.GetDiagnosticsView().motionEligibilityStats;
        evaluatedBodyTicks += stats.evaluatedBodies;
        discreteBodyTicks += stats.discreteBodies;
        promotedBodyTicks += stats.promotedBodies;
    }

    CHECK( evaluatedBodyTicks == measuredTicks * kMicroBodyCount );
    CHECK( discreteBodyTicks == measuredTicks * kMicroBodyCount );
    CHECK( promotedBodyTicks == 0 );
    CHECK( discreteBodyTicks > evaluatedBodyTicks - discreteBodyTicks );
}


TEST_CASE( "Physics motion promotion: fixed-step force integration promotes in the same tick" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = -2400.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    ReserveTestPhysicsCapacity( engine, 1u );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3101u, MakeSphereShape( 1.0f ),
                             Vector3( 0.0f, 10.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.4f, 0.4f, 0.4f ), 1.0f, 0.0f,
                             PhysicsBodyMotionKind::Dynamic, "force-promoted-sphere" );

    PhysicsWorldForces forces = NoGravityForces();
    forces.gravity = config.worldForces.gravity;
    StepMicroWorldWith( engine, 1, forces );

    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == 1u );
    CHECK( ( diagnostics.motionEligibilityState[0] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( diagnostics.motionEligibilityStats.promotedBodies == 1 );
}


TEST_CASE( "Physics motion promotion: authoritative 200-box striker velocity promotes without scene identity" )
{
    SkullbonezCore::Runtime::AuthoredScene scene;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadAuthoredScene(
        resultDiagnostics, "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json", scene ) );
    const SkullbonezCore::Runtime::SceneBallState* striker = nullptr;

    for ( int ballIndex = 0; ballIndex < scene.GetBallStateCount(); ++ballIndex )
    {
        const auto& ball = scene.GetBallState( ballIndex );

        if ( std::strcmp( ball.name, "prediction_striker_ball" ) == 0 )
        {
            striker = &ball;
            break;
        }
    }

    REQUIRE( striker != nullptr );
    const Vector3 authoredVelocity( striker->velX, striker->velY, striker->velZ );
    CHECK( sqrtf( VectorMagnitudeSquared( authoredVelocity ) ) * PHYSICS_FIXED_DT >
           SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );

    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    ReserveTestPhysicsCapacity( engine, 1u );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3201u, MakeSphereShape( striker->radius ),
                             Vector3( striker->posX, striker->posY, striker->posZ ), authoredVelocity,
                             Vector3( striker->inertiaX, striker->inertiaY, striker->inertiaZ ), striker->mass,
                             striker->restitution, PhysicsBodyMotionKind::Dynamic, "generic-fast-sphere" );

    StepMicroWorldWith( engine, 1, NoGravityForces() );
    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == 1u );
    CHECK( ( diagnostics.motionEligibilityState[0] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( diagnostics.motionEligibilityStats.promotedBodies == 1 );
}


TEST_CASE( "PhysicsEngine terrain fixtures reconstruct without cross-instance state" )
{
    const auto runColdFixture = []( float terrainBaseY )
    {
        DeterminismTerrainFixture fixture( terrainBaseY );
        PhysicsEngine& engine = fixture.Engine();
        engine.Clear();
        engine.ApplyRuntimeConfig( MakeDeterministicConfig() );
        engine.SetSleepEnabled( false );
        AddMicroBody( engine, fixture.TerrainView(), 501u, Vector3( 0.0f, 2.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ) );
        AddMicroBody( engine, fixture.TerrainView(), 502u, Vector3( 4.0f, 4.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ) );
        AddMicroBody( engine, fixture.TerrainView(), 503u, Vector3( 8.0f, 6.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ) );
        StepMicroWorld( engine, kSnapshotFrame );
        return HashMicroWorldPhysicsState( engine );
    };

    // Invariant: every lambda return destroys its engine, terrain, and config
    // before the next terrain is constructed. The sequence exercises flat and
    // deep after both predecessor kinds, then exact hashes prove that no prior
    // fixture contributes retained mutable state.
    const std::array<uint64_t, 7> reconstructionHashes = {
        runColdFixture( kFlatTerrainBaseY ),
        runColdFixture( kDeepSpaceTerrainBaseY ),
        runColdFixture( kFlatTerrainBaseY ),
        runColdFixture( kDeepSpaceTerrainBaseY ),
        runColdFixture( kDeepSpaceTerrainBaseY ),
        runColdFixture( kFlatTerrainBaseY ),
        runColdFixture( kFlatTerrainBaseY ),
    };

    CHECK( reconstructionHashes[0] != 0u );
    CHECK( reconstructionHashes[1] != 0u );
    CHECK( reconstructionHashes[0] == reconstructionHashes[2] );
    CHECK( reconstructionHashes[0] == reconstructionHashes[5] );
    CHECK( reconstructionHashes[0] == reconstructionHashes[6] );
    CHECK( reconstructionHashes[1] == reconstructionHashes[3] );
    CHECK( reconstructionHashes[1] == reconstructionHashes[4] );
    CHECK( reconstructionHashes[0] != reconstructionHashes[1] );
}


TEST_CASE( "PhysicsEngine multithreaded determinism: contact and sleep pipeline is exact across worker counts" )
{

    // Lifetime: each fixture keeps the heap engine before its independent
    // terrain owner in reverse destruction order.
    DeterminismTerrainFixture serialFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture oneWorkerFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture fourWorkersFixture( kFlatTerrainBaseY );
    PhysicsEngine& serial = serialFixture.Engine();
    PhysicsEngine& oneWorker = oneWorkerFixture.Engine();
    PhysicsEngine& fourWorkers = fourWorkersFixture.Engine();

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

    SeedParallelContactSleepWorld( serial, serialFixture.TerrainView(), config );
    SeedParallelContactSleepWorld( oneWorker, oneWorkerFixture.TerrainView(), config );
    SeedParallelContactSleepWorld( fourWorkers, fourWorkersFixture.TerrainView(), config );
    SeedParallelDeterminismPointJoints( serial );
    SeedParallelDeterminismPointJoints( oneWorker );
    SeedParallelDeterminismPointJoints( fourWorkers );

    const Vector3 promotedVelocity( 13.0f, 0.0f, 0.0f );
    const Vector3 zeroAngularVelocity( 0.0f, 0.0f, 0.0f );
    REQUIRE( serial.SetBodyVelocity( RequireBodyHandle( serial, 0 ), promotedVelocity, zeroAngularVelocity, true ) );
    REQUIRE(
        oneWorker.SetBodyVelocity( RequireBodyHandle( oneWorker, 0 ), promotedVelocity, zeroAngularVelocity, true ) );
    REQUIRE( fourWorkers.SetBodyVelocity( RequireBodyHandle( fourWorkers, 0 ), promotedVelocity, zeroAngularVelocity,
                                         true ) );
    const PhysicsWorldForces forces = DeterministicForces();

    StepMicroWorldWith( serial, 1, forces, 0 );
    StepMicroWorldWith( oneWorker, 1, forces, 1 );
    StepMicroWorldWith( fourWorkers, 1, forces, 4 );

    // Invariant: the fixture must keep the parallel-narrowphase threshold
    // active. A geometry/filter drift to 255 pairs would otherwise let every
    // worker-count comparison pass through the serial fallback.
    const auto serialDiagnostics = serial.GetDiagnosticsView();
    CHECK( oneWorker.GetDiagnosticsView().candidatePairs.size() == 256u );
    CHECK( fourWorkers.GetDiagnosticsView().candidatePairs.size() == 256u );
    CHECK_FALSE( serialDiagnostics.persistentContacts.empty() );
    CHECK_FALSE( serialDiagnostics.terrainContactManifolds.empty() );
    CHECK_FALSE( serialDiagnostics.physicsPipelineTrace.empty() );
    CHECK( PhysicsEngine::ReadPointJointConstraints( serial ).size() == 2u );
    CHECK( serialDiagnostics.motionEligibilityStats.promotedBodies >= 1 );
    CHECK( ( serialDiagnostics.motionEligibilityState[0] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CheckEngineWorkerDeterministicStateEqual( serial, oneWorker );
    CheckEngineWorkerDeterministicStateEqual( serial, fourWorkers );

    StepMicroWorldWith( serial, 30, forces, 0 );
    StepMicroWorldWith( oneWorker, 30, forces, 1 );
    StepMicroWorldWith( fourWorkers, 30, forces, 4 );
    CheckEngineWorkerDeterministicStateEqual( serial, oneWorker );
    CheckEngineWorkerDeterministicStateEqual( serial, fourWorkers );

    const auto sleepStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( serial );
    CHECK( std::any_of( sleepStates.begin(), sleepStates.end(), []( uint8_t sleeping ) { return sleeping != 0; } ) );
}


TEST_CASE( "Tornado external-force lane is byte-exact across serial and parallel body partitions" )
{
    constexpr int bodyCount = kParallelContactBodyCount;
    DeterminismTerrainFixture serialFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture parallelFixture( kFlatTerrainBaseY );
    PhysicsEngine& serial = serialFixture.Engine();
    PhysicsEngine& parallel = parallelFixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.physicsExecution.parallel = true;
    serial.ApplyRuntimeConfig( config );
    parallel.ApplyRuntimeConfig( config );
    serial.SetSleepEnabled( false );
    parallel.SetSleepEnabled( false );
    ReserveTestPhysicsCapacity( serial, bodyCount );
    ReserveTestPhysicsCapacity( parallel, bodyCount );

    for ( int index = 0; index < bodyCount; ++index )
    {
        const Vector3 position( -95.0f + static_cast<float>( index % 20 ) * 10.0f,
                                20.0f + static_cast<float>( index / 20 ) * 5.0f,
                                -40.0f + static_cast<float>( index % 9 ) * 10.0f );
        const Vector3 zeroVelocity( 0.0f, 0.0f, 0.0f );
        AddMicroBody( serial, serialFixture.TerrainView(), 2000u + static_cast<uint32_t>( index ), position, zeroVelocity );
        AddMicroBody( parallel, parallelFixture.TerrainView(), 2000u + static_cast<uint32_t>( index ), position,
                      zeroVelocity );
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
    serial.Step( PHYSICS_FIXED_DT, NoGravityForces(), serialGameplay.BuildForceFrame( PHYSICS_FIXED_DT, bodyCount ),
                 serialWorkers, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    parallel.Step( PHYSICS_FIXED_DT, NoGravityForces(), parallelGameplay.BuildForceFrame( PHYSICS_FIXED_DT, bodyCount ),
                   parallelWorkers, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    // Invariant: every authored spin in this 520-body workload crosses the
    // absolute angular threshold. Persistent grid cells must therefore be
    // established before transient coverage is admitted in either execution mode.
    CHECK( serial.GetDiagnosticsView().motionEligibilityStats.angularExpandedBodies == bodyCount );
    CHECK( parallel.GetDiagnosticsView().motionEligibilityStats.angularExpandedBodies == bodyCount );
    const auto exercisedAngularCoverage = []( const PhysicsEngine& engine )
    {
        const auto& grid = engine.GetDiagnosticsView().spatialGrid;
        return grid.GetMaintenanceStats().sweptOverlayCellsAdded > 0 || grid.GetSweptFallbackBodyCount() > 0u;
    };
    CHECK( exercisedAngularCoverage( serial ) );
    CHECK( exercisedAngularCoverage( parallel ) );
    CheckEngineKinematicsEqual( serial, parallel );
    CheckVectorContentsEqual( serialGameplay.CaptureSeconds(), parallelGameplay.CaptureSeconds() );
    CheckVectorContentsEqual( serialGameplay.EjectCooldownSeconds(), parallelGameplay.EjectCooldownSeconds() );
}


TEST_CASE( "Physics motion promotion: opposing individually Discrete balls retain relative Swept TOI" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    config.bodySimulation.contactEpsilon = 0.001f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.SetPipelineTraceFullRecordConsumerActive( true );
    ReserveTestPhysicsCapacity( engine, 2u );

    constexpr float radius = 0.1f;
    constexpr float mass = 1.0f;
    constexpr float speed = 9.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape sphere = MakeSphereShape( radius );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3001u, sphere, Vector3( -0.17f, 0.0f, 0.0f ),
                             Vector3( speed, 0.0f, 0.0f ), Vector3( inertia, inertia, inertia ), mass, 1.0f,
                             PhysicsBodyMotionKind::Dynamic, "opposing-discrete-left" );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3002u, sphere, Vector3( 0.17f, 0.0f, 0.0f ),
                             Vector3( -speed, 0.0f, 0.0f ), Vector3( inertia, inertia, inertia ), mass, 1.0f,
                             PhysicsBodyMotionKind::Dynamic, "opposing-discrete-right" );

    StepMicroWorldWith( engine, 1, NoGravityForces() );
    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == 2u );
    CHECK( diagnostics.motionEligibilityState[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( diagnostics.motionEligibilityState[1] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( DiagnosticsHasCandidatePair( engine, 0, 1 ) );
    CHECK( DiagnosticsHasPipelineStageForPair( engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit,
                                               0, 1 ) );

    const PhysicsBodyHotState left = RequireBodyHotState( engine, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( engine, 1 );
    CHECK( left.position.x < right.position.x );
    CHECK( left.linearVelocity.x < 0.0f );
    CHECK( right.linearVelocity.x > 0.0f );
}


TEST_CASE( "Physics motion promotion: sub-threshold opposing tiny balls retain geometry-selected Swept TOI" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    config.bodySimulation.contactEpsilon = 0.001f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.SetPipelineTraceFullRecordConsumerActive( true );
    ReserveTestPhysicsCapacity( engine, 2u );

    constexpr float radius = 0.01f;
    constexpr float mass = 1.0f;
    constexpr float speed = 4.8f;
    constexpr float initialCenterSeparation = 0.05f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape sphere = MakeSphereShape( radius );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3011u, sphere,
                             Vector3( initialCenterSeparation * -0.5f, 0.0f, 0.0f ), Vector3( speed, 0.0f, 0.0f ),
                             Vector3( inertia, inertia, inertia ), mass, 1.0f, PhysicsBodyMotionKind::Dynamic,
                             "tiny-opposing-discrete-left" );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3012u, sphere,
                             Vector3( initialCenterSeparation * 0.5f, 0.0f, 0.0f ), Vector3( -speed, 0.0f, 0.0f ),
                             Vector3( inertia, inertia, inertia ), mass, 1.0f, PhysicsBodyMotionKind::Dynamic,
                             "tiny-opposing-discrete-right" );

    const float perBodyTravel = speed * PHYSICS_FIXED_DT;
    CHECK( perBodyTravel == doctest::Approx( 0.04f ) );
    CHECK( perBodyTravel * 2.0f == doctest::Approx( 0.08f ) );
    CHECK( perBodyTravel < SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
    CHECK( perBodyTravel * 2.0f < SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
    CHECK( perBodyTravel * 2.0f > initialCenterSeparation + radius * 2.0f );

    StepMicroWorldWith( engine, 1, NoGravityForces() );
    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == 2u );
    CHECK( diagnostics.motionEligibilityState[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( diagnostics.motionEligibilityState[1] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( DiagnosticsHasCandidatePair( engine, 0, 1 ) );
    CHECK( DiagnosticsHasPipelineStageForPair( engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit,
                                               0, 1 ) );

    const PhysicsBodyHotState left = RequireBodyHotState( engine, 0 );
    const PhysicsBodyHotState right = RequireBodyHotState( engine, 1 );
    CHECK( left.position.x < right.position.x );
    CHECK( left.linearVelocity.x < 0.0f );
    CHECK( right.linearVelocity.x > 0.0f );
}


TEST_CASE( "Physics motion promotion: resting static-friction pair stays Discrete" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.physicsMaterial.objectFrictionCoeff = 1.0f;
    config.bodySimulation.contactEpsilon = 0.001f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.SetPipelineTraceFullRecordConsumerActive( true );
    ReserveTestPhysicsCapacity( engine, 2u );

    constexpr float dynamicHalfExtent = 0.5f;
    constexpr float initialTangentialSpeed = 0.05f;
    const CollisionShape dynamicBox = CollisionShape(
        BoundingBox( Vector3( dynamicHalfExtent, dynamicHalfExtent, dynamicHalfExtent ), Vector3( 0.0f, 0.0f, 0.0f ) ) );
    const CollisionShape supportBox =
        CollisionShape( BoundingBox( Vector3( 1.0f, dynamicHalfExtent, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3021u, dynamicBox, Vector3( 0.0f, 0.99f, 0.0f ),
                             Vector3( initialTangentialSpeed, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                             PhysicsBodyMotionKind::Dynamic, "resting-friction-box" );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3022u, supportBox, Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                             PhysicsBodyMotionKind::Fixed, "resting-friction-support" );

    const PhysicsBodyHotState supportBefore = RequireBodyHotState( engine, 1 );

    // Invariant: exact current contact belongs to the persistent manifold and
    // static-friction solve; pair-level CCD must not emit a synthetic sweep.
    for ( int tick = 0; tick < 4; ++tick )
    {
        StepMicroWorldWith( engine, 1, DeterministicForces() );
        const auto diagnostics = engine.GetDiagnosticsView();
        REQUIRE( diagnostics.motionEligibilityState.size() == 2u );
        CHECK( diagnostics.motionEligibilityState[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
        CHECK( DiagnosticsHasCandidatePair( engine, 0, 1 ) );
        CHECK( DiagnosticsHasPipelineStageForPair( engine, SkullbonezCore::Physics::PhysicsPipelineStage::ManifoldRow,
                                                   0, 1 ) );
        CHECK_FALSE( DiagnosticsHasPipelineStageForPair( engine,
                                                         SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 0,
                                                         1 ) );
        CHECK_FALSE( DiagnosticsHasPipelineStageForPair( engine,
                                                         SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectMiss, 0,
                                                         1 ) );
    }

    const PhysicsBodyHotState dynamicAfter = RequireBodyHotState( engine, 0 );
    const PhysicsBodyHotState supportAfter = RequireBodyHotState( engine, 1 );
    CHECK( fabsf( dynamicAfter.linearVelocity.x ) < initialTangentialSpeed );
    CHECK( dynamicAfter.position.y >= 0.99f - config.bodySimulation.contactEpsilon );
    CheckVectorBytesEqual( supportBefore.position, supportAfter.position );
    CheckVectorBytesEqual( supportBefore.linearVelocity, supportAfter.linearVelocity );
}


TEST_CASE( "Physics motion promotion: tilted terrain box keeps support long enough to sleep while Discrete" )
{
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = -32.0f;
    config.physicsSleep.frames = 30;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    ReserveTestPhysicsCapacity( engine, 1u );

    const Vector3 halfExtents( 1.15f, 1.45f, 2.45f );
    const CollisionShape shape = CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
    const SkullbonezCore::Math::Orientation::Quaternion orientation( -0.010159f, -0.006545f, -0.000461f,
                                                                     0.999927f );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { 3031u }, shape,
                                               Vector3( 592.0f, 1.5004f, 472.5f ), orientation,
                                               Vector3( 0.0f, -0.0001f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( 32.42f, 29.3f, 13.7f ), 12.0f, 0.08f,
                                               PhysicsBodyMotionKind::Dynamic, "tilted-discrete-terrain-box" );
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.08f, 0u, "unit" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        REQUIRE( engine.RegisterAuthoredBody( bodyDesc, colliderDesc ).IsValid() );
    }

    bool observedSupport = false;
    int supportDropouts = 0;

    for ( int tick = 0; tick < config.physicsSleep.frames + 30; ++tick )
    {
        StepMicroWorldWith( engine, 1, DeterministicForces() );
        const auto diagnostics = engine.GetDiagnosticsView();
        REQUIRE( diagnostics.motionEligibilityState.size() == 1u );
        CHECK( diagnostics.motionEligibilityState[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );

        if ( !DiagnosticsSleepStateAt( engine, 0 ) )
        {
            if ( diagnostics.sleepSupportedThisFrame[0] != 0u )
            {
                observedSupport = true;
            }
            else if ( observedSupport )
            {
                ++supportDropouts;
            }
        }
    }

    // Invariant: a clamp to terrain is a collision consequence, not a support
    // substitute. A quiet Discrete body must retain solver support continuously
    // enough for the 30-frame sleep policy to complete.
    CHECK( observedSupport );
    CHECK( supportDropouts == 0 );
    CHECK( DiagnosticsSleepStateAt( engine, 0 ) );
}


TEST_CASE( "Physics motion promotion: collision-promoted target uses Swept TOI against thin wall next tick" )
{
    DeterminismTerrainFixture fixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& engine = fixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    config.bodySimulation.contactEpsilon = 0.001f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.SetPipelineTraceFullRecordConsumerActive( true );
    ReserveTestPhysicsCapacity( engine, 3u );

    constexpr float ballRadius = 0.1f;
    constexpr float ballMass = 1.0f;
    constexpr float wallX = 0.55f;
    constexpr float wallHalfThickness = 0.005f;
    const float ballInertia = 0.4f * ballMass * ballRadius * ballRadius;
    const CollisionShape ballShape = MakeSphereShape( ballRadius );
    const CollisionShape wallShape = CollisionShape(
        BoundingBox( Vector3( wallHalfThickness, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3101u, ballShape, Vector3( -1.1f, 0.0f, 0.0f ),
                             Vector3( 120.0f, 0.0f, 0.0f ), Vector3( ballInertia, ballInertia, ballInertia ), ballMass,
                             1.0f, PhysicsBodyMotionKind::Dynamic, "promotion-striker" );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3102u, ballShape, Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ), Vector3( ballInertia, ballInertia, ballInertia ), ballMass,
                             1.0f, PhysicsBodyMotionKind::Dynamic, "promotion-target" );
    AddPromotionFixtureBody( engine, fixture.TerrainView(), 3103u, wallShape, Vector3( wallX, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                             PhysicsBodyMotionKind::Fixed, "promotion-thin-wall" );

    StepMicroWorldWith( engine, 1, NoGravityForces() );
    const auto firstTick = engine.GetDiagnosticsView();
    REQUIRE( firstTick.motionEligibilityState.size() == 3u );
    REQUIRE( firstTick.linearTravelSquared.size() == 3u );
    CHECK( ( firstTick.motionEligibilityState[0] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( firstTick.motionEligibilityState[1] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( firstTick.linearTravelSquared[1] == doctest::Approx( 0.0f ) );
    CHECK( DiagnosticsHasCandidatePair( engine, 1, 2 ) );
    CHECK( DiagnosticsHasPipelineStageForPair( engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit,
                                               0, 1 ) );
    CHECK_FALSE( DiagnosticsHasPipelineStageForPair(
        engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 1, 2 ) );
    CHECK_FALSE( DiagnosticsHasPipelineStageForPair(
        engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectMiss, 1, 2 ) );

    const PhysicsBodyHotState targetAfterImpact = RequireBodyHotState( engine, 1 );
    const PhysicsBodyHotState wallBeforeSecondTick = RequireBodyHotState( engine, 2 );
    const float wallNearContactCenter = wallX - wallHalfThickness - ballRadius;
    const float wallFarClearanceCenter = wallX + wallHalfThickness + ballRadius;
    CHECK( targetAfterImpact.position.x < wallNearContactCenter );
    CHECK( targetAfterImpact.linearVelocity.x * PHYSICS_FIXED_DT >
           SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
    const float unimpededEndX = targetAfterImpact.position.x + targetAfterImpact.linearVelocity.x * PHYSICS_FIXED_DT;
    CHECK( unimpededEndX > wallFarClearanceCenter );

    StepMicroWorldWith( engine, 1, NoGravityForces() );
    const auto secondTick = engine.GetDiagnosticsView();
    REQUIRE( secondTick.motionEligibilityState.size() == 3u );
    REQUIRE( secondTick.linearTravelSquared.size() == 3u );
    CHECK( ( secondTick.motionEligibilityState[1] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( secondTick.linearTravelSquared[1] >
           SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK *
               SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
    CHECK( DiagnosticsHasPipelineStageForPair( engine, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit,
                                               1, 2 ) );

    const PhysicsBodyHotState targetAfterWall = RequireBodyHotState( engine, 1 );
    const PhysicsBodyHotState wallAfterSecondTick = RequireBodyHotState( engine, 2 );
    CHECK( targetAfterWall.position.x <= wallNearContactCenter + config.bodySimulation.contactEpsilon * 2.0f );
    CheckVectorBytesEqual( wallBeforeSecondTick.position, wallAfterSecondTick.position );
}


TEST_CASE( "PhysicsEngine mutual gravity: pair force is antisymmetric" )
{
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& pairWorld = fixture.Engine();
    SeedTwoBodyGravityWorld( pairWorld, fixture.TerrainView(), Vector3( -12.0f, 80.0f, 0.0f ), Vector3( 12.0f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 5.0f, 0.5f );

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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& closeWorld = fixture.Engine();
    SeedTwoBodyGravityWorld( closeWorld, fixture.TerrainView(), Vector3( 0.0f, 80.0f, 0.0f ), Vector3( 0.03f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 3.0f, 0.01f );

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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& orbitWorld = fixture.Engine();
    const float orbitRadius = 20.0f;
    const float separation = orbitRadius * 2.0f;
    const float mass = 20.0f;
    const float gravitationalConstant = 20.0f;
    const float softeningLength = 0.1f;
    const float softenedDistanceSq = separation * separation + softeningLength * softeningLength;
    const float acceleration = gravitationalConstant * mass * separation /
                               ( softenedDistanceSq * sqrtf( softenedDistanceSq ) );

    const float orbitalSpeed = sqrtf( acceleration * orbitRadius );

    SeedTwoBodyGravityWorld( orbitWorld, fixture.TerrainView(), Vector3( -orbitRadius, 80.0f, 0.0f ),
                             Vector3( orbitRadius, 80.0f, 0.0f ), Vector3( 0.0f, 0.0f, -orbitalSpeed ),
                             Vector3( 0.0f, 0.0f, orbitalSpeed ), mass, 0.5f );

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


TEST_CASE( "PhysicsEngine solar assist: same-state 120-second forecast matches live and depends on Earth gravity" )
{
    SkullbonezCore::Runtime::AuthoredScene scene;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadAuthoredScene(
        resultDiagnostics, "SkullbonezData/scenes/solar_system_mars_slingshot.scene.json", scene ) );

    DeterminismTerrainFixture liveFixture( kDeepSpaceTerrainBaseY );
    DeterminismTerrainFixture forecastFixture( kDeepSpaceTerrainBaseY );
    DeterminismTerrainFixture noEarthGravityFixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& live = liveFixture.Engine();
    PhysicsEngine& forecast = forecastFixture.Engine();
    PhysicsEngine& noEarthGravity = noEarthGravityFixture.Engine();
    SeedAuthoredSolarWorld( live, liveFixture.TerrainView(), scene, true );
    SeedAuthoredSolarWorld( forecast, forecastFixture.TerrainView(), scene, true );
    SeedAuthoredSolarWorld( noEarthGravity, noEarthGravityFixture.TerrainView(), scene, false );

    int earthIndex = -1;
    int marsIndex = -1;
    int rocketIndex = -1;

    for ( int index = 0; index < scene.GetBallStateCount(); ++index )
    {
        const char* name = scene.GetBallState( index ).name;
        earthIndex = std::strcmp( name, "earth" ) == 0 ? index : earthIndex;
        marsIndex = std::strcmp( name, "mars" ) == 0 ? index : marsIndex;
        rocketIndex = std::strcmp( name, "rocket" ) == 0 ? index : rocketIndex;
    }

    REQUIRE( earthIndex >= 0 );
    REQUIRE( marsIndex >= 0 );
    REQUIRE( rocketIndex >= 0 );

    struct MoonOrbitProbe
    {
        const char* moonName;
        const char* parentName;
        int moonIndex = -1;
        int parentIndex = -1;
        float initialDistance = 0.0f;
        float maximumDistance = 0.0f;
    };
    std::array<MoonOrbitProbe, 22> moonOrbits = {
        MoonOrbitProbe { "moon", "earth" },       MoonOrbitProbe { "phobos", "mars" },
        MoonOrbitProbe { "deimos", "mars" },      MoonOrbitProbe { "io", "jupiter" },
        MoonOrbitProbe { "europa", "jupiter" },   MoonOrbitProbe { "ganymede", "jupiter" },
        MoonOrbitProbe { "callisto", "jupiter" }, MoonOrbitProbe { "mimas", "saturn" },
        MoonOrbitProbe { "enceladus", "saturn" }, MoonOrbitProbe { "tethys", "saturn" },
        MoonOrbitProbe { "dione", "saturn" },     MoonOrbitProbe { "rhea", "saturn" },
        MoonOrbitProbe { "titan", "saturn" },     MoonOrbitProbe { "iapetus", "saturn" },
        MoonOrbitProbe { "miranda", "uranus" },   MoonOrbitProbe { "ariel", "uranus" },
        MoonOrbitProbe { "umbriel", "uranus" },   MoonOrbitProbe { "titania", "uranus" },
        MoonOrbitProbe { "oberon", "uranus" },    MoonOrbitProbe { "proteus", "neptune" },
        MoonOrbitProbe { "triton", "neptune" },   MoonOrbitProbe { "nereid", "neptune" },
    };

    for ( MoonOrbitProbe& orbit : moonOrbits )
    {

        for ( int index = 0; index < scene.GetBallStateCount(); ++index )
        {
            const char* name = scene.GetBallState( index ).name;
            orbit.moonIndex = std::strcmp( name, orbit.moonName ) == 0 ? index : orbit.moonIndex;
            orbit.parentIndex = std::strcmp( name, orbit.parentName ) == 0 ? index : orbit.parentIndex;
        }

        REQUIRE( orbit.moonIndex >= 0 );
        REQUIRE( orbit.parentIndex >= 0 );
        orbit.initialDistance = sqrtf( VectorMagnitudeSquared( RequireBodyHotState( live, orbit.moonIndex ).position -
                                                               RequireBodyHotState( live, orbit.parentIndex ).position ) );

        orbit.maximumDistance = orbit.initialDistance;
    }

    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    const PhysicsWorldForces forces = MutualGravityForces( 1.0f, 0.5f );
    float closestEarth = 1.0e9f;
    float closestMars = 1.0e9f;
    float closestMarsWithoutEarthGravity = 1.0e9f;
    float maximumRocketRadius = 0.0f;
    float maximumBodyRadius = 0.0f;
    constexpr int kPredictionTicks = 120 * 120;

    for ( int tick = 0; tick < kPredictionTicks; ++tick )
    {
        live.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
        forecast.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
        noEarthGravity.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

        const PhysicsBodyHotState rocket = RequireBodyHotState( live, rocketIndex );
        const PhysicsBodyHotState earth = RequireBodyHotState( live, earthIndex );
        const PhysicsBodyHotState mars = RequireBodyHotState( live, marsIndex );
        const PhysicsBodyHotState noEarthRocket = RequireBodyHotState( noEarthGravity, rocketIndex );
        const PhysicsBodyHotState noEarthMars = RequireBodyHotState( noEarthGravity, marsIndex );
        closestEarth = (std::min)( closestEarth, sqrtf( VectorMagnitudeSquared( rocket.position - earth.position ) ) );
        closestMars = (std::min)( closestMars, sqrtf( VectorMagnitudeSquared( rocket.position - mars.position ) ) );
        closestMarsWithoutEarthGravity = (std::min)( closestMarsWithoutEarthGravity,
                                                     sqrtf( VectorMagnitudeSquared( noEarthRocket.position -
                                                                                    noEarthMars.position ) ) );

        maximumRocketRadius = (std::max)( maximumRocketRadius, sqrtf( VectorMagnitudeSquared( rocket.position ) ) );

        for ( int bodyIndex = 0; bodyIndex < scene.GetBallStateCount(); ++bodyIndex )
        {
            maximumBodyRadius = (std::max)( maximumBodyRadius, sqrtf( VectorMagnitudeSquared( RequireBodyHotState( live, bodyIndex ).position ) ) );
        }

        for ( MoonOrbitProbe& orbit : moonOrbits )
        {
            const float parentRelativeDistance = sqrtf( VectorMagnitudeSquared( RequireBodyHotState( live, orbit.moonIndex ).position -
                                                                                RequireBodyHotState( live, orbit.parentIndex ).position ) );

            orbit.maximumDistance = (std::max)( orbit.maximumDistance, parentRelativeDistance );
        }
    }

    // Same initial snapshot plus the same fixed-step forces is the prediction
    // contract. Exact equality catches any future split between live and forecast stepping.
    CheckEngineKinematicsEqual( live, forecast );
    CHECK( closestEarth > 1.3f );
    CHECK( closestEarth < 2.6f );
    CHECK( closestMars > 0.7f );
    CHECK( closestMars < 2.0f );
    CHECK( closestMarsWithoutEarthGravity > 10.0f );
    CHECK( closestMarsWithoutEarthGravity > closestMars * 8.0f );
    CHECK( maximumRocketRadius < 180.0f );
    CHECK( maximumBodyRadius < 450.0f );
    const PhysicsBodyHotState finalRocket = RequireBodyHotState( live, rocketIndex );
    CHECK( finalRocket.position.x == doctest::Approx( 18.169813f ).epsilon( 0.00001 ) );
    CHECK( finalRocket.position.y == doctest::Approx( 89.437309f ).epsilon( 0.00001 ) );
    CHECK( finalRocket.position.z == doctest::Approx( 0.0f ).epsilon( 0.00001 ) );
    CHECK( sqrtf( VectorMagnitudeSquared( finalRocket.position ) ) < 110.0f );

    for ( const MoonOrbitProbe& orbit : moonOrbits )
    {
        CAPTURE( orbit.moonName );
        CAPTURE( orbit.parentName );
        CAPTURE( orbit.initialDistance );
        CAPTURE( orbit.maximumDistance );
        CHECK( orbit.maximumDistance < orbit.initialDistance * 1.5f );
    }
}


TEST_CASE( "PhysicsEngine mutual gravity: chaotic triple is deterministic" )
{
    DeterminismTerrainFixture firstFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture secondFixture( kFlatTerrainBaseY );
    PhysicsEngine& first = firstFixture.Engine();
    PhysicsEngine& second = secondFixture.Engine();
    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;

    auto seedTriple = [&config]( PhysicsEngine& engine, PhysicsTerrainView terrainView )
    {
        engine.Clear();

        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        ReserveTestPhysicsCapacity( engine, 3 );
        AddMutualGravityBody( engine, terrainView, 301u, Vector3( -18.0f, 90.0f, 0.0f ), Vector3( 0.8f, 0.0f, -1.0f ), 12.0f,
                              0.45f );

        AddMutualGravityBody( engine, terrainView, 302u, Vector3( 16.0f, 90.0f, 4.0f ), Vector3( -0.4f, 0.0f, 1.1f ), 16.0f,
                              0.45f );

        AddMutualGravityBody( engine, terrainView, 303u, Vector3( 2.0f, 90.0f, 24.0f ), Vector3( -0.2f, 0.0f, -0.8f ), 10.0f,
                              0.45f );

        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 3 );
        REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Count() == 3 );
    };

    seedTriple( first, firstFixture.TerrainView() );
    seedTriple( second, secondFixture.TerrainView() );
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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& collisionWorld = fixture.Engine();
    const float mass = 2.0f;
    const float radius = 1.0f;
    const float speed = 4.0f;
    SeedTwoBodyGravityWorld( collisionWorld, fixture.TerrainView(), Vector3( -0.9f, 80.0f, 0.0f ),
                             Vector3( 0.9f, 80.0f, 0.0f ), Vector3( speed, 0.0f, 0.0f ), Vector3( -speed, 0.0f, 0.0f ), mass,
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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& settled = fixture.Engine();
    SeedMicroWorld( settled, fixture.TerrainView() );

    const SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    const PhysicsWorldForces forces = DeterministicForces();
    StepMicroWorldWith( settled, kPenetrationSettleTicks, forces );

    CheckTerrainPenetrationWithinTolerance( settled, config );
}


TEST_CASE( "PhysicsEngine invariants: fluid damping does not add kinetic energy" )
{
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& damped = fixture.Engine();
    SeedMicroWorld( damped, fixture.TerrainView() );

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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& sleepWorld = fixture.Engine();
    SeedMicroWorld( sleepWorld, fixture.TerrainView() );
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
    DeterminismTerrainFixture fixture( kFlatTerrainBaseY );
    PhysicsEngine& sleepWorld = fixture.Engine();

    SkullbonezCore::Core::EngineConfig config = MakeDeterministicConfig();
    config.physicsSleep.frames = 3;
    config.physicsSleep.linearSpeed = 0.25f;
    config.physicsSleep.angularSpeed = 0.25f;
    const PhysicsWorldForces forces = DeterministicForces();

    SeedSupportedSleepWorld( sleepWorld, fixture.TerrainView(), config );
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
    DeterminismTerrainFixture interruptedFixture( kFlatTerrainBaseY );
    DeterminismTerrainFixture restoredFixture( kFlatTerrainBaseY );
    PhysicsEngine& interrupted = interruptedFixture.Engine();
    PhysicsEngine& restored = restoredFixture.Engine();
    SeedMicroWorld( interrupted, interruptedFixture.TerrainView() );
    SeedMicroWorld( restored, restoredFixture.TerrainView() );

    StepMicroWorld( interrupted, kSnapshotFrame );
    StepMicroWorld( restored, kSnapshotFrame );
    const MicroWorldSnapshot snapshot = CaptureMicroWorldSnapshot( restored );

    StepMicroWorld( interrupted, kReplayWindowTicks );
    StepMicroWorld( restored, kReplayWindowTicks );
    RestoreMicroWorldSnapshot( restored, snapshot );
    StepMicroWorld( restored, kReplayWindowTicks );

    CheckEngineKinematicsEqual( interrupted, restored );
}

TEST_CASE( "PhysicsEngine replay restore preserves promoted impact and demotion paths byte-exactly" )
{
    DeterminismTerrainFixture referenceFixture( kDeepSpaceTerrainBaseY );
    DeterminismTerrainFixture restoredFixture( kDeepSpaceTerrainBaseY );
    PhysicsEngine& reference = referenceFixture.Engine();
    PhysicsEngine& restored = restoredFixture.Engine();
    EngineConfig config = MakeDeterministicConfig();
    config.worldForces.gravity = 0.0f;
    config.bodySimulation.contactEpsilon = 0.001f;
    const PhysicsWorldForces forces = NoGravityForces();
    constexpr float radius = 0.1f;
    constexpr float mass = 1.0f;
    constexpr float wallHalfThickness = 0.005f;
    constexpr float primeTravel = 0.11f;
    constexpr float retainedTravel = ( SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK +
                                       SkullbonezCore::Physics::PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK ) *
                                     0.5f;
    constexpr float wallX = primeTravel + retainedTravel * 0.5f + radius + wallHalfThickness;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape sphere = MakeSphereShape( radius );
    const CollisionShape wall = CollisionShape(
        BoundingBox( Vector3( wallHalfThickness, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) );
    const auto seedWorld = [&]( PhysicsEngine& engine, PhysicsTerrainView terrainView, uint32_t idBase )
    {
        engine.Clear();
        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        engine.SetPipelineTraceFullRecordConsumerActive( true );
        ReserveTestPhysicsCapacity( engine, 2u );
        AddPromotionFixtureBody( engine, terrainView, idBase, sphere, Vector3( 0.0f, 0.0f, 0.0f ),
                                 Vector3( primeTravel / PHYSICS_FIXED_DT, 0.0f, 0.0f ), Vector3( inertia, inertia, inertia ),
                                 mass, 0.0f, PhysicsBodyMotionKind::Dynamic, "replay-promoted-ball" );
        AddPromotionFixtureBody( engine, terrainView, idBase + 1u, wall, Vector3( wallX, 0.0f, 0.0f ),
                                 Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                                 PhysicsBodyMotionKind::Fixed, "replay-thin-wall" );
    };
    seedWorld( reference, referenceFixture.TerrainView(), 991u );
    seedWorld( restored, restoredFixture.TerrainView(), 991u );

    StepMicroWorldWith( reference, 1, forces );
    StepMicroWorldWith( restored, 1, forces );
    CheckEngineKinematicsEqual( reference, restored );
    CheckMotionEligibilityBytesEqual( reference, restored );
    REQUIRE( reference.GetDiagnosticsView().motionEligibilityState.size() == 2u );
    REQUIRE( ( reference.GetDiagnosticsView().motionEligibilityState[0] &
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );

    const Vector3 retainedVelocity( retainedTravel / PHYSICS_FIXED_DT, 0.0f, 0.0f );
    CHECK( retainedTravel < SkullbonezCore::Physics::PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
    CHECK( retainedTravel > SkullbonezCore::Physics::PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK );
    REQUIRE( reference.SetBodyVelocity( RequireBodyHandle( reference, 0 ), retainedVelocity, Vector3( 0.0f, 0.0f, 0.0f ),
                                        true ) );
    REQUIRE( restored.SetBodyVelocity( RequireBodyHandle( restored, 0 ), retainedVelocity,
                                       Vector3( 0.0f, 0.0f, 0.0f ), true ) );

    std::array<BodyReplayState, 2> bodyStates;

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        const PhysicsBodyRecord* record = PhysicsEngine::ReadBodies( restored ).RecordForModelIndex( bodyIndex );
        REQUIRE( record != nullptr );
        bodyStates[static_cast<std::size_t>( bodyIndex )] = CaptureBodyReplayState( *record,
                                                                                    RequireBodyHotState( restored,
                                                                                                         bodyIndex ) );
    }

    PhysicsSolverSnapshot solverSnapshot;
    restored.CaptureReplaySolverSnapshot( solverSnapshot, MakePhysicsBodyCountFromNonNegativeInt( 2 ) );
    REQUIRE( solverSnapshot.motionEligibilityState.size() == 2u );
    REQUIRE( ( solverSnapshot.motionEligibilityState[0] &
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );

    StepMicroWorldWith( restored, 1, forces );
    REQUIRE( DiagnosticsHasPipelineStageForPair( restored, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 0,
                                                 1 ) );
    StepMicroWorldWith( restored, 1, forces );
    REQUIRE( restored.GetDiagnosticsView().motionEligibilityState.size() == 2u );
    REQUIRE( ( restored.GetDiagnosticsView().motionEligibilityState[0] &
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );

    // Invariant: the production replay transaction preflights retained solver
    // state before body mutation and commits that hidden state after body rows.
    REQUIRE( restored.CanRestoreReplaySolverSnapshot( solverSnapshot, MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );

    for ( const BodyReplayState& state : bodyStates )
    {
        REQUIRE( restored.RestoreReplayBodyState( PhysicsBodyRestoreState {
            state.handle, state.sceneObjectId, state.fixed, state.position, state.orientation, state.linearVelocity,
            state.angularVelocity, state.mass, state.inverseMass, state.rotationalInertia,
            state.inverseRotationalInertia } ) );
    }

    REQUIRE( restored.RestoreReplaySolverSnapshot( solverSnapshot, MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );
    CheckEngineKinematicsEqual( reference, restored );
    CheckMotionEligibilityBytesEqual( reference, restored );
    CheckPhysicsPipelineTraceBytesEqual( reference, restored );
    REQUIRE( ( restored.GetDiagnosticsView().motionEligibilityState[0] &
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );

    const PhysicsBodyHotState wallBeforeImpact = RequireBodyHotState( reference, 1 );
    StepMicroWorldWith( reference, 1, forces );
    StepMicroWorldWith( restored, 1, forces );
    CheckMotionEligibilityBytesEqual( reference, restored );
    CheckPhysicsPipelineTraceBytesEqual( reference, restored );
    CheckEngineKinematicsEqual( reference, restored );
    REQUIRE( reference.GetDiagnosticsView().motionEligibilityState.size() == 2u );
    CHECK( ( reference.GetDiagnosticsView().motionEligibilityState[0] &
             SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    REQUIRE( DiagnosticsHasPipelineStageForPair( reference, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 0,
                                                 1 ) );
    REQUIRE( DiagnosticsHasPipelineStageForPair( restored, SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 0,
                                                 1 ) );
    const PhysicsBodyHotState ballAfterImpact = RequireBodyHotState( reference, 0 );
    const PhysicsBodyHotState wallAfterImpact = RequireBodyHotState( reference, 1 );
    const float wallNearContactCenter = wallX - wallHalfThickness - radius;
    CHECK( ballAfterImpact.position.x <= wallNearContactCenter + config.bodySimulation.contactEpsilon * 2.0f );
    CHECK( fabsf( ballAfterImpact.linearVelocity.x ) * PHYSICS_FIXED_DT <=
           SkullbonezCore::Physics::PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK );
    CheckVectorBytesEqual( wallBeforeImpact.position, wallAfterImpact.position );
    CheckVectorBytesEqual( wallBeforeImpact.linearVelocity, wallAfterImpact.linearVelocity );

    StepMicroWorldWith( reference, 1, forces );
    StepMicroWorldWith( restored, 1, forces );
    CheckMotionEligibilityBytesEqual( reference, restored );
    CheckPhysicsPipelineTraceBytesEqual( reference, restored );
    CheckEngineKinematicsEqual( reference, restored );
    REQUIRE( reference.GetDiagnosticsView().motionEligibilityState.size() == 2u );
    CHECK( reference.GetDiagnosticsView().motionEligibilityState[0] ==
           SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK_FALSE( DiagnosticsHasPipelineStageForPair( reference,
                                                     SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit, 0, 1 ) );
    CHECK_FALSE( DiagnosticsHasPipelineStageForPair( reference,
                                                     SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectMiss, 0,
                                                     1 ) );
}

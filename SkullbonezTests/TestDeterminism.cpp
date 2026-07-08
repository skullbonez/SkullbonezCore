//
// File: SkullbonezTests/TestDeterminism.cpp
// Purpose:
//   Lock fast PhysicsEngine determinism, replay restore, and physics invariant properties.
//
// Mental model:
//   A minimal authored physics world can be seeded directly through
//   PhysicsEngine without GameModelCollection or scene-load plumbing. Fixed-step
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
//   Null render resource factory: Inert backend capability needed only to
//     satisfy the Terrain constructor signature in this focused harness.
//   Property check: Tolerance-based unit assertion over a physics rule that
//     should hold across implementation details, rather than a golden row match.
//   Kinetic energy: Translational plus angular motion energy used here as a
//     damping monotonicity signal.
//   Sleep gate: Solver-owned optimization state that can pause integration until
//     an explicit wake path receives motion again.
//   Terrain manifold: Contact-point report between a body and the flat test
//     terrain, sampled here as diagnostics rather than as the byte oracle.
//
// Invariants:
//   - The micro-world stays serial; worker fan-out starts far above this body count.
//   - Snapshot losslessness needs both solver state and body replay state.
//   - Kinematic comparisons are byte-exact, not epsilon-based.
//   - Invariant checks use explicit tolerances because they assert physical
//     policy, not serialized replay bytes.
//   - Terrain queries are real flat-plane queries; render resources must stay unused.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsEngine.h
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
//   - engine-cleanup-plans/05-behavioral-test-coverage.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Rendering/IRenderResourceFactory.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using SkullbonezCore::Basics::EngineConfig;
using SkullbonezCore::Basics::ReplayBodyShapeKind;
using SkullbonezCore::Basics::ReplayFrameIndex;
using SkullbonezCore::Basics::ReplaySolverBodySample;
using SkullbonezCore::Basics::ReplaySolverFrameSample;
using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
constexpr int kMicroBodyCount = 3;
constexpr int kSnapshotFrame = 120;
constexpr int kReplayWindowTicks = 60;
constexpr int kReplaySampleSnapshotFrame = 30;
constexpr int kReplaySampleWindowTicks = 30;
constexpr int kTotalDeterminismTicks = 240;
constexpr int kPenetrationSettleTicks = 480;
constexpr float kDampingEnergyTolerance = 0.0001f;

// Why: Terrain is still part of the real physics step, but the unit test only
// needs its collision plane. Resource methods return inert handles because any
// render call here would mean the fixture crossed out of its physics boundary.
class NullRenderResourceFactory final : public SkullbonezCore::Rendering::IRenderResourceFactory
{
  public:
    std::unique_ptr<SkullbonezCore::Rendering::IShader> CreateShader( const char* ) override
    {
        return nullptr;
    }

    std::unique_ptr<SkullbonezCore::Rendering::IMesh> CreateMesh( const float*, int, bool, bool ) override
    {
        return nullptr;
    }

    std::unique_ptr<SkullbonezCore::Rendering::IFramebuffer>
    CreateFramebuffer( int, int, SkullbonezCore::Rendering::FramebufferColorFormat ) override
    {
        return nullptr;
    }

    uint32_t CreateTexture2D( const uint8_t*, int, int, int, bool, bool ) override
    {
        return 0u;
    }

    void DeleteTexture( uint32_t ) override
    {
    }

    uint32_t CreateDynamicVB( const int*, int, int ) override
    {
        return 0u;
    }

    void DestroyDynamicVB( uint32_t ) override
    {
    }

    uint32_t CreateInstancedMesh( const float*,
                                  int,
                                  int,
                                  int,
                                  int,
                                  int,
                                  const int*,
                                  int,
                                  const int*,
                                  int ) override
    {
        return 0u;
    }

    void DestroyInstancedMesh( uint32_t ) override
    {
    }
};

struct BodyReplayState
{
    PhysicsBodyHandle handle;
    uint32_t replayBodyId = 0;
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
    ReplaySolverWorldSnapshot solver;
    std::array<BodyReplayState, kMicroBodyCount> bodies;
};

EngineConfig MakeDeterministicConfig()
{
    EngineConfig config;
    config.physicsParallel = false;
    config.physicsParallelApplyForces = false;
    config.physicsParallelTornadoField = false;
    config.physicsParallelNarrowphase = false;
    config.physicsParallelTerrainDetect = false;
    config.physicsParallelIntegrate = false;
    config.fluidDensity = 0.0f;
    config.gasDensity = 0.0f;
    config.gravity = -9.8f;
    config.velocityLimit = 1000.0f;
    config.broadphaseCell = 32.0f;
    config.physicsSleepFrames = 1000000;
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
    static EngineConfig config = MakeDeterministicConfig();
    static SkullbonezCore::Assets::AssetSystem assets;
    static NullRenderResourceFactory resources;
    static Terrain terrain( 0.0f, 0.0f, 0.0f, config, assets, resources );
    return terrain;
}

CollisionShape MakeSphereShape( float radius )
{
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

void AddMicroBody( PhysicsEngine& engine,
                   uint32_t sceneObjectId,
                   const Vector3& position,
                   const Vector3& linearVelocity )
{
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectId },
                                               shape,
                                               position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               linearVelocity,
                                               Vector3( 0.03f * static_cast<float>( sceneObjectId ), 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ),
                                               mass,
                                               0.0f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               &FlatTestTerrain(),
                                               "unit-determinism-body" );
    bodyDesc.angularVelocityLimit = 1000.0f;
    const PhysicsBodyHandle body = engine.RegisterAuthoredBody( bodyDesc );
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.body = body;
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    (void)engine.RegisterAuthoredCollider( colliderDesc );
}

void SeedMicroWorld( PhysicsEngine& engine )
{
    EngineConfig config = MakeDeterministicConfig();
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, 101u, Vector3( 100.0f, 30.0f, 100.0f ), Vector3( 1.5f, 0.0f, 0.0f ) );
    AddMicroBody( engine, 102u, Vector3( 112.0f, 40.0f, 100.0f ), Vector3( 0.0f, 0.5f, 0.0f ) );
    AddMicroBody( engine, 103u, Vector3( 124.0f, 50.0f, 100.0f ), Vector3( -1.0f, 0.0f, 0.0f ) );
    REQUIRE( engine.BodyStore().Count() == kMicroBodyCount );
    REQUIRE( engine.Colliders().Count() == kMicroBodyCount );
}

void StepMicroWorldWith( PhysicsEngine& engine,
                         int ticks,
                         const EngineConfig& config,
                         const PhysicsWorldForces& forces )
{
    WorkerPool workerPool;
    for ( int tick = 0; tick < ticks; ++tick )
    {
        engine.Step( PHYSICS_FIXED_DT,
                     config,
                     forces,
                     workerPool,
                     nullptr,
                     0,
                     SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );
    }
}

void StepMicroWorld( PhysicsEngine& engine, int ticks )
{
    const EngineConfig config = MakeDeterministicConfig();
    const PhysicsWorldForces forces = DeterministicForces();
    StepMicroWorldWith( engine, ticks, config, forces );
}

const PhysicsBodyRecord& RequireBodyRecord( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record = engine.BodyStore().RecordForModelIndex( modelIndex );
    REQUIRE( record != nullptr );
    return *record;
}

PhysicsBodyHandle RequireBodyHandle( const PhysicsEngine& engine, int modelIndex )
{
    return RequireBodyRecord( engine, modelIndex ).handle;
}

float VectorMagnitudeSquared( const Vector3& value )
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

float BodyKineticEnergy( const PhysicsBodyRecord& record )
{
    const float translational = 0.5f * record.mass * VectorMagnitudeSquared( record.linearVelocity );
    const float angular = 0.5f *
        ( record.rotationalInertia.x * record.angularVelocity.x * record.angularVelocity.x +
          record.rotationalInertia.y * record.angularVelocity.y * record.angularVelocity.y +
          record.rotationalInertia.z * record.angularVelocity.z * record.angularVelocity.z );
    return translational + angular;
}

float TotalKineticEnergy( const PhysicsEngine& engine )
{
    float energy = 0.0f;
    for ( int i = 0; i < engine.BodyStore().Count(); ++i )
    {
        energy += BodyKineticEnergy( RequireBodyRecord( engine, i ) );
    }
    return energy;
}

bool DiagnosticsSleepStateAt( const PhysicsEngine& engine, int modelIndex )
{
    const std::vector<uint8_t>& sleepStates = engine.GetSleepStates();
    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    return bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
}

void CheckTerrainPenetrationWithinTolerance( const PhysicsEngine& engine, const EngineConfig& config )
{
    // Concept: this is the fast invariant partner to byte-exact CSV baselines.
    // It does not care about exact impulse history, only that settled body rows
    // and terrain manifolds stay inside the configured contact envelope.
    const float maxAllowedPenetration = config.terrainContactThreshold + config.contactEpsilon;
    for ( int i = 0; i < engine.BodyStore().Count(); ++i )
    {
        const PhysicsBodyRecord& record = RequireBodyRecord( engine, i );
        const float groundClearance = record.position.y - record.boundingRadius;
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

BodyReplayState CaptureBodyReplayState( const PhysicsBodyRecord& record )
{
    BodyReplayState state;
    state.handle = record.handle;
    state.replayBodyId = record.replayBodyId;
    state.fixed = record.isFixed;
    state.position = record.position;
    state.orientation = record.orientation;
    state.linearVelocity = record.linearVelocity;
    state.angularVelocity = record.angularVelocity;
    state.mass = record.mass;
    state.inverseMass = record.invMass;
    state.rotationalInertia = record.rotationalInertia;
    state.inverseRotationalInertia = record.invRotationalInertia;
    return state;
}

MicroWorldSnapshot CaptureMicroWorldSnapshot( const PhysicsEngine& engine )
{
    MicroWorldSnapshot snapshot;
    engine.CaptureReplaySolverSnapshot( snapshot.solver, kMicroBodyCount );
    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        const PhysicsBodyRecord* record = engine.BodyStore().RecordForModelIndex( i );
        REQUIRE( record != nullptr );
        snapshot.bodies[static_cast<std::size_t>( i )] = CaptureBodyReplayState( *record );
    }
    return snapshot;
}

ReplaySolverBodySample CaptureMicroWorldReplayBodySample( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record = engine.BodyStore().RecordForModelIndex( modelIndex );
    REQUIRE( record != nullptr );

    ReplaySolverBodySample body;
    body.id.value = record->replayBodyId;
    body.modelIndex = modelIndex;
    body.shapeKind = ReplayBodyShapeKind::Sphere;
    body.position = record->position;
    body.linearVelocity = record->linearVelocity;
    body.angularVelocity = record->angularVelocity;
    record->orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
    body.mass = record->mass;
    body.inverseMass = record->invMass;
    body.rotationalInertia = record->rotationalInertia;
    body.inverseRotationalInertia = record->invRotationalInertia;
    body.fixed = record->isFixed;

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const std::vector<uint8_t>& sleepStates = engine.GetSleepStates();
    const std::vector<uint8_t>& sleepSupportedStates = engine.GetSleepSupportedStates();
    const std::vector<uint8_t>& sleepInhibitedStates = engine.GetSleepInhibitedStates();
    const std::vector<uint8_t>& collisionContacts = engine.GetCollisionVisualContacts();
    const std::vector<int>& sleepIslandIds = engine.GetSleepIslandVisualIds();
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
    // Run, GameModelCollection, cameras, or renderer-owned presentation state.
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
    sample.contactCount = static_cast<uint16_t>( engine.GetPhysicsDebugContacts().size() );
    sample.pipelineRecordCount = static_cast<uint16_t>( engine.GetPhysicsPipelineTrace().size() );
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot, kMicroBodyCount );

    sample.bodies.reserve( kMicroBodyCount );
    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        sample.bodies.push_back( CaptureMicroWorldReplayBodySample( engine, i ) );
    }
    return sample;
}

void RestoreMicroWorldSnapshot( PhysicsEngine& engine, const MicroWorldSnapshot& snapshot )
{
    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot.solver, kMicroBodyCount ) );
    for ( const BodyReplayState& state : snapshot.bodies )
    {
        REQUIRE( engine.RestoreReplayBodyState( state.handle,
                                                state.replayBodyId,
                                                state.fixed,
                                                state.position,
                                                state.orientation,
                                                state.linearVelocity,
                                                state.angularVelocity,
                                                state.mass,
                                                state.inverseMass,
                                                state.rotationalInertia,
                                                state.inverseRotationalInertia ) );
    }
}

void RestoreMicroWorldReplaySample( PhysicsEngine& engine, const ReplaySolverFrameSample& sample )
{
    // Why: replay restore applies solver cache first, then body rows. The test
    // mirrors that order so a future mismatch points at the same boundary Run uses.
    REQUIRE( sample.worldSnapshot.modelCount == static_cast<int>( sample.bodies.size() ) );
    REQUIRE( engine.RestoreReplaySolverSnapshot( sample.worldSnapshot, static_cast<int>( sample.bodies.size() ) ) );
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        const Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        const PhysicsBodyRecord* record = engine.BodyStore().RecordForModelIndex( body.modelIndex );
        REQUIRE( record != nullptr );
        REQUIRE( engine.RestoreReplayBodyState( record->handle,
                                                body.id.value,
                                                body.fixed,
                                                body.position,
                                                orientation,
                                                body.linearVelocity,
                                                body.angularVelocity,
                                                body.mass,
                                                body.inverseMass,
                                                body.rotationalInertia,
                                                body.inverseRotationalInertia ) );
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

template <typename T>
void CheckVectorContentsEqual( const std::vector<T>& lhs, const std::vector<T>& rhs )
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
    CHECK( lhs.modelIndex == rhs.modelIndex );
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
    CHECK( lhs.worldSnapshot.version == rhs.worldSnapshot.version );
    CHECK( lhs.worldSnapshot.modelCount == rhs.worldSnapshot.modelCount );
    CHECK( lhs.worldSnapshot.sleepEnabled == rhs.worldSnapshot.sleepEnabled );
    CheckVectorContentsEqual( lhs.worldSnapshot.timeRemaining, rhs.worldSnapshot.timeRemaining );
    CheckVectorContentsEqual( lhs.worldSnapshot.sleepState, rhs.worldSnapshot.sleepState );
    CheckVectorContentsEqual( lhs.worldSnapshot.sleepCounter, rhs.worldSnapshot.sleepCounter );
    CheckVectorContentsEqual( lhs.worldSnapshot.collisionVisualContacts, rhs.worldSnapshot.collisionVisualContacts );
    CheckVectorContentsEqual( lhs.worldSnapshot.sleepIslandParent, rhs.worldSnapshot.sleepIslandParent );
    CheckVectorContentsEqual( lhs.worldSnapshot.sleepIslandRank, rhs.worldSnapshot.sleepIslandRank );
    CHECK( lhs.contactCount == rhs.contactCount );
    CHECK( lhs.pipelineRecordCount == rhs.pipelineRecordCount );
    REQUIRE( lhs.bodies.size() == rhs.bodies.size() );
    for ( std::size_t i = 0; i < lhs.bodies.size(); ++i )
    {
        CheckReplayBodySamplesEqual( lhs.bodies[i], rhs.bodies[i] );
    }
}

void CheckEngineKinematicsEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    REQUIRE( lhs.BodyStore().Count() == rhs.BodyStore().Count() );
    for ( int i = 0; i < lhs.BodyStore().Count(); ++i )
    {
        const PhysicsBodyRecord* left = lhs.BodyStore().RecordForModelIndex( i );
        const PhysicsBodyRecord* right = rhs.BodyStore().RecordForModelIndex( i );
        REQUIRE( left != nullptr );
        REQUIRE( right != nullptr );
        CheckVectorBytesEqual( left->position, right->position );
        CheckQuaternionBytesEqual( left->orientation, right->orientation );
        CheckVectorBytesEqual( left->linearVelocity, right->linearVelocity );
        CheckVectorBytesEqual( left->angularVelocity, right->angularVelocity );
    }
}
} // namespace


TEST_CASE( "PhysicsEngine determinism: micro-world matches at fixed tick intervals" )
{
    static PhysicsEngine first;
    static PhysicsEngine second;
    SeedMicroWorld( first );
    SeedMicroWorld( second );

    for ( int tick = 60; tick <= kTotalDeterminismTicks; tick += 60 )
    {
        StepMicroWorld( first, 60 );
        StepMicroWorld( second, 60 );
        CheckEngineKinematicsEqual( first, second );
    }
}


TEST_CASE( "PhysicsEngine invariants: settled bodies stay within terrain penetration tolerance" )
{
    static PhysicsEngine settled;
    SeedMicroWorld( settled );

    const EngineConfig config = MakeDeterministicConfig();
    const PhysicsWorldForces forces = DeterministicForces();
    StepMicroWorldWith( settled, kPenetrationSettleTicks, config, forces );

    CheckTerrainPenetrationWithinTolerance( settled, config );
}


TEST_CASE( "PhysicsEngine invariants: fluid damping does not add kinetic energy" )
{
    static PhysicsEngine damped;
    SeedMicroWorld( damped );

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    config.fluidDensity = 2.0f;
    config.fluidAngularDragMultiplier = 2.0f;
    const PhysicsWorldForces forces = DampingForces();

    const float initialEnergy = TotalKineticEnergy( damped );
    REQUIRE( initialEnergy > 0.0f );

    float previousEnergy = initialEnergy;
    for ( int tick = 0; tick < 12; ++tick )
    {
        StepMicroWorldWith( damped, 1, config, forces );
        const float currentEnergy = TotalKineticEnergy( damped );
        CHECK( currentEnergy <= previousEnergy + kDampingEnergyTolerance );
        previousEnergy = currentEnergy;
    }
    CHECK( previousEnergy < initialEnergy );
}


TEST_CASE( "PhysicsEngine invariants: authored velocity wakes a sleeping body" )
{
    static PhysicsEngine sleepWorld;
    SeedMicroWorld( sleepWorld );
    sleepWorld.SetSleepEnabled( true );

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    const PhysicsWorldForces forces = NoGravityForces();

    const PhysicsBodyHandle body = RequireBodyHandle( sleepWorld, 0 );
    sleepWorld.SeedBodyAsleep( body );
    CHECK( RequireBodyRecord( sleepWorld, 0 ).isSleeping );
    CHECK( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    const Vector3 positionBeforeWake = RequireBodyRecord( sleepWorld, 0 ).position;
    REQUIRE( sleepWorld.SetBodyVelocity( body,
                                         Vector3( 2.0f, 0.0f, 0.0f ),
                                         Vector3( 0.0f, 0.0f, 0.0f ),
                                         true ) );
    CHECK_FALSE( RequireBodyRecord( sleepWorld, 0 ).isSleeping );
    CHECK_FALSE( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    StepMicroWorldWith( sleepWorld, 1, config, forces );
    CHECK( RequireBodyRecord( sleepWorld, 0 ).position.x > positionBeforeWake.x );
}


TEST_CASE( "PhysicsEngine determinism: solver snapshot plus body state restores losslessly" )
{
    static PhysicsEngine interrupted;
    static PhysicsEngine restored;
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
    static PhysicsEngine expected;
    static PhysicsEngine restored;
    SeedMicroWorld( expected );
    SeedMicroWorld( restored );

    StepMicroWorld( expected, kReplaySampleSnapshotFrame );
    StepMicroWorld( restored, kReplaySampleSnapshotFrame );
    const ReplaySolverFrameSample restorePoint =
        CaptureMicroWorldReplaySample( restored, static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame ) );

    StepMicroWorld( expected, kReplaySampleWindowTicks );
    const ReplaySolverFrameSample expectedFuture =
        CaptureMicroWorldReplaySample( expected,
                                       static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame +
                                                                      kReplaySampleWindowTicks ) );

    StepMicroWorld( restored, kReplaySampleWindowTicks );
    RestoreMicroWorldReplaySample( restored, restorePoint );
    StepMicroWorld( restored, kReplaySampleWindowTicks );
    const ReplaySolverFrameSample restoredFuture =
        CaptureMicroWorldReplaySample( restored,
                                       static_cast<ReplayFrameIndex>( kReplaySampleSnapshotFrame +
                                                                      kReplaySampleWindowTicks ) );

    CheckReplaySamplesEqual( expectedFuture, restoredFuture );
}

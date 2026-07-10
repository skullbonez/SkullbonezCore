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
//   - Agentic/Plans/TODO/behavioral-test-depth.md
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
#include "../SkullbonezSource/Physics/PhysicsEngineStoreQueries.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Rendering/IRenderResourceFactory.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestRenderResourceDoubles.h"

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
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
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
    static EngineConfig config = MakeDeterministicConfig();
    static SkullbonezCore::Assets::AssetSystem assets;
    static SkullbonezTests::NullRenderResourceFactory resources;
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

void AddSupportedSleepBody( PhysicsEngine& engine, uint32_t sceneObjectId, const Vector3& position )
{
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectId },
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
    const PhysicsBodyHandle body = engine.RegisterAuthoredBody( bodyDesc );
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.body = body;
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    (void)engine.RegisterAuthoredCollider( colliderDesc );
}

void SeedSupportedSleepWorld( PhysicsEngine& engine, const EngineConfig& config )
{
    // Why: the sleep-threshold test needs real terrain support but no inherited
    // angular motion from SeedMicroWorld. One quiet sphere starts exactly on the
    // flat terrain so the solver, not fixture surgery, earns the sleep state.
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    AddSupportedSleepBody( engine, 401u, Vector3( 0.0f, 1.0f, 0.0f ) );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count() == 1 );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::Colliders( engine ).Count() == 1 );
}

void AddMutualGravityBody( PhysicsEngine& engine,
                           uint32_t sceneObjectId,
                           const Vector3& position,
                           const Vector3& linearVelocity,
                           float mass,
                           float radius )
{
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectId },
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
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count() == kMicroBodyCount );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::Colliders( engine ).Count() == kMicroBodyCount );
}

void SeedTwoBodyGravityWorld( PhysicsEngine& engine,
                              const Vector3& leftPosition,
                              const Vector3& rightPosition,
                              const Vector3& leftVelocity,
                              const Vector3& rightVelocity,
                              float mass,
                              float radius )
{
    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    engine.ReserveAuthoredBodyCapacity( 2 );
    AddMutualGravityBody( engine, 201u, leftPosition, leftVelocity, mass, radius );
    AddMutualGravityBody( engine, 202u, rightPosition, rightVelocity, mass, radius );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count() == 2 );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::Colliders( engine ).Count() == 2 );
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
    const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).RecordForModelIndex( modelIndex );
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
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count(); ++i )
    {
        energy += BodyKineticEnergy( RequireBodyRecord( engine, i ) );
    }
    return energy;
}

bool DiagnosticsSleepStateAt( const PhysicsEngine& engine, int modelIndex )
{
    const std::vector<uint8_t>& sleepStates = SkullbonezCore::Physics::PhysicsEngineStoreQueries::SleepStates( engine );
    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    return bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
}

void CheckTerrainPenetrationWithinTolerance( const PhysicsEngine& engine, const EngineConfig& config )
{
    // Concept: this is the fast invariant partner to byte-exact CSV baselines.
    // It does not care about exact impulse history, only that settled body rows
    // and terrain manifolds stay inside the configured contact envelope.
    const float maxAllowedPenetration = config.terrainContactThreshold + config.contactEpsilon;
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count(); ++i )
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

void HashBytesForReplayTest( uint64_t& hash, const void* data, std::size_t byteCount )
{
    const uint8_t* bytes = static_cast<const uint8_t*>( data );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash ^= static_cast<uint64_t>( bytes[i] );
        hash *= 1099511628211ull;
    }
}

template <typename T>
void HashValueForReplayTest( uint64_t& hash, const T& value )
{
    HashBytesForReplayTest( hash, &value, sizeof( T ) );
}

template <typename T>
void HashVectorForReplayTest( uint64_t& hash, const std::vector<T>& values )
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
    // are owned by ReplaySolverRecorder, which needs GameModelCollection. This
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
    HashValueForReplayTest( hash, sample.worldSnapshot.version );
    HashValueForReplayTest( hash, sample.worldSnapshot.modelCount );
    HashValueForReplayTest( hash, sample.worldSnapshot.sleepEnabled );
    HashVectorForReplayTest( hash, sample.worldSnapshot.timeRemaining );
    HashVectorForReplayTest( hash, sample.worldSnapshot.sleepState );
    HashVectorForReplayTest( hash, sample.worldSnapshot.sleepCounter );
    HashVectorForReplayTest( hash, sample.worldSnapshot.collisionVisualContacts );
    HashVectorForReplayTest( hash, sample.worldSnapshot.sleepIslandParent );
    HashVectorForReplayTest( hash, sample.worldSnapshot.sleepIslandRank );
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
        const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).RecordForModelIndex( i );
        REQUIRE( record != nullptr );
        snapshot.bodies[static_cast<std::size_t>( i )] = CaptureBodyReplayState( *record );
    }
    return snapshot;
}

ReplaySolverBodySample CaptureMicroWorldReplayBodySample( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).RecordForModelIndex( modelIndex );
    REQUIRE( record != nullptr );

    ReplaySolverBodySample body;
    body.id.value = record->replayBodyId;
    body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( modelIndex );
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
    const std::vector<uint8_t>& sleepStates = SkullbonezCore::Physics::PhysicsEngineStoreQueries::SleepStates( engine );
    const std::vector<uint8_t>& sleepSupportedStates = SkullbonezCore::Physics::PhysicsEngineStoreQueries::SleepSupportedStates( engine );
    const std::vector<uint8_t>& sleepInhibitedStates = SkullbonezCore::Physics::PhysicsEngineStoreQueries::SleepInhibitedStates( engine );
    const std::vector<uint8_t>& collisionContacts = SkullbonezCore::Physics::PhysicsEngineStoreQueries::CollisionVisualContacts( engine );
    const std::vector<int>& sleepIslandIds = SkullbonezCore::Physics::PhysicsEngineStoreQueries::SleepIslandVisualIds( engine );
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
    sample.contactCount = static_cast<uint16_t>( SkullbonezCore::Physics::PhysicsEngineStoreQueries::DebugContacts( engine ).size() );
    sample.pipelineRecordCount = static_cast<uint16_t>( SkullbonezCore::Physics::PhysicsEngineStoreQueries::PipelineTrace( engine ).size() );
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot, MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) );

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
    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot.solver, MakePhysicsBodyCountFromNonNegativeInt( kMicroBodyCount ) ) );
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
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        sample.worldSnapshot,
        MakePhysicsBodyCountFromNonNegativeInt( static_cast<int>( sample.bodies.size() ) ) ) );
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        const Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        const PhysicsBodyRecord* record = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).RecordForModelIndex( body.modelRow.value );
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
    REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( lhs ).Count() == SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( rhs ).Count() );
    for ( int i = 0; i < SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( lhs ).Count(); ++i )
    {
        const PhysicsBodyRecord* left = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( lhs ).RecordForModelIndex( i );
        const PhysicsBodyRecord* right = SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( rhs ).RecordForModelIndex( i );
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


TEST_CASE( "PhysicsEngine mutual gravity: pair force is antisymmetric" )
{
    static PhysicsEngine pairWorld;
    SeedTwoBodyGravityWorld( pairWorld,
                             Vector3( -12.0f, 80.0f, 0.0f ),
                             Vector3( 12.0f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             5.0f,
                             0.5f );

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    const PhysicsWorldForces forces = MutualGravityForces( 120.0f, 0.25f );

    StepMicroWorldWith( pairWorld, 1, config, forces );
    const PhysicsBodyRecord& left = RequireBodyRecord( pairWorld, 0 );
    const PhysicsBodyRecord& right = RequireBodyRecord( pairWorld, 1 );
    CHECK( left.linearVelocity.x > 0.0f );
    CHECK( right.linearVelocity.x < 0.0f );
    CHECK( left.linearVelocity.x == doctest::Approx( -right.linearVelocity.x ).epsilon( 0.0001 ) );
    CHECK( left.linearVelocity.y == doctest::Approx( right.linearVelocity.y ).epsilon( 0.0001 ) );
    CHECK( left.linearVelocity.z == doctest::Approx( right.linearVelocity.z ).epsilon( 0.0001 ) );
}


TEST_CASE( "PhysicsEngine mutual gravity: softening keeps near pairs finite" )
{
    static PhysicsEngine closeWorld;
    SeedTwoBodyGravityWorld( closeWorld,
                             Vector3( 0.0f, 80.0f, 0.0f ),
                             Vector3( 0.03f, 80.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             Vector3( 0.0f, 0.0f, 0.0f ),
                             3.0f,
                             0.01f );

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    const PhysicsWorldForces forces = MutualGravityForces( 1000.0f, 5.0f );

    StepMicroWorldWith( closeWorld, 1, config, forces );
    const PhysicsBodyRecord& left = RequireBodyRecord( closeWorld, 0 );
    const PhysicsBodyRecord& right = RequireBodyRecord( closeWorld, 1 );
    CHECK( std::isfinite( left.linearVelocity.x ) );
    CHECK( std::isfinite( right.linearVelocity.x ) );
    CHECK( fabsf( left.linearVelocity.x ) < 1.0f );
    CHECK( fabsf( right.linearVelocity.x ) < 1.0f );
}


TEST_CASE( "PhysicsEngine mutual gravity: equal-mass two-body orbit stays bounded" )
{
    static PhysicsEngine orbitWorld;
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

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    const PhysicsWorldForces forces = MutualGravityForces( gravitationalConstant, softeningLength );

    StepMicroWorldWith( orbitWorld, 300, config, forces );
    const PhysicsBodyRecord& left = RequireBodyRecord( orbitWorld, 0 );
    const PhysicsBodyRecord& right = RequireBodyRecord( orbitWorld, 1 );
    const Vector3 barycenter = ( left.position + right.position ) * 0.5f;
    const float finalSeparation = sqrtf( VectorMagnitudeSquared( right.position - left.position ) );
    CHECK( barycenter.x == doctest::Approx( 0.0f ).epsilon( 0.001 ) );
    CHECK( barycenter.y == doctest::Approx( 80.0f ).epsilon( 0.001 ) );
    CHECK( barycenter.z == doctest::Approx( 0.0f ).epsilon( 0.001 ) );
    CHECK( finalSeparation == doctest::Approx( separation ).epsilon( 0.10 ) );
}


TEST_CASE( "PhysicsEngine mutual gravity: chaotic triple is deterministic" )
{
    static PhysicsEngine first;
    static PhysicsEngine second;
    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;

    auto seedTriple = [&config]( PhysicsEngine& engine )
    {
        engine.Clear();
        engine.ApplyRuntimeConfig( config );
        engine.SetSleepEnabled( false );
        engine.ReserveAuthoredBodyCapacity( 3 );
        AddMutualGravityBody(
            engine, 301u, Vector3( -18.0f, 90.0f, 0.0f ), Vector3( 0.8f, 0.0f, -1.0f ), 12.0f, 0.45f );
        AddMutualGravityBody( engine, 302u, Vector3( 16.0f, 90.0f, 4.0f ), Vector3( -0.4f, 0.0f, 1.1f ), 16.0f, 0.45f );
        AddMutualGravityBody(
            engine, 303u, Vector3( 2.0f, 90.0f, 24.0f ), Vector3( -0.2f, 0.0f, -0.8f ), 10.0f, 0.45f );
        REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( engine ).Count() == 3 );
        REQUIRE( SkullbonezCore::Physics::PhysicsEngineStoreQueries::Colliders( engine ).Count() == 3 );
    };

    seedTriple( first );
    seedTriple( second );
    const PhysicsWorldForces forces = MutualGravityForces( 45.0f, 0.35f );
    StepMicroWorldWith( first, 240, config, forces );
    StepMicroWorldWith( second, 240, config, forces );
    CheckEngineKinematicsEqual( first, second );
}


TEST_CASE( "PhysicsEngine mutual gravity: elastic space collision preserves closing speed" )
{
    static PhysicsEngine collisionWorld;
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

    EngineConfig config = MakeDeterministicConfig();
    config.gravity = 0.0f;
    PhysicsWorldForces forces = MutualGravityForces( 0.001f, 1.0f );
    REQUIRE( forces.mutualGravity.elasticCollisions );

    const float initialEnergy = TotalKineticEnergy( collisionWorld );
    StepMicroWorldWith( collisionWorld, 1, config, forces );
    const PhysicsBodyRecord& left = RequireBodyRecord( collisionWorld, 0 );
    const PhysicsBodyRecord& right = RequireBodyRecord( collisionWorld, 1 );
    const float finalEnergy = TotalKineticEnergy( collisionWorld );

    CHECK( left.linearVelocity.x < -speed * 0.98f );
    CHECK( right.linearVelocity.x > speed * 0.98f );
    CHECK( finalEnergy == doctest::Approx( initialEnergy ).epsilon( 0.01 ) );
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


TEST_CASE( "PhysicsEngine sleep policy: quiet supported body sleeps after threshold frames" )
{
    static PhysicsEngine sleepWorld;

    EngineConfig config = MakeDeterministicConfig();
    config.physicsSleepFrames = 3;
    config.physicsSleepLinearSpeed = 0.25f;
    config.physicsSleepAngularSpeed = 0.25f;
    const PhysicsWorldForces forces = DeterministicForces();

    SeedSupportedSleepWorld( sleepWorld, config );
    StepMicroWorldWith( sleepWorld, config.physicsSleepFrames + 24, config, forces );

    CHECK( RequireBodyRecord( sleepWorld, 0 ).isSleeping );
    CHECK( DiagnosticsSleepStateAt( sleepWorld, 0 ) );

    const PhysicsBodyHandle body = RequireBodyHandle( sleepWorld, 0 );
    const Vector3 positionBeforeWake = RequireBodyRecord( sleepWorld, 0 ).position;
    sleepWorld.ApplyBodyImpulse( body, Vector3( 12.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
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

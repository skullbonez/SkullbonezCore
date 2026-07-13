//
// File: SkullbonezTests/TestReplayPredictionFidelity.cpp
// Purpose:
//   Prove that a replay-snapshot-seeded PhysicsEngine copy produces the same
//   byte-exact body future as the live engine at every subsequent fixed tick.
//
// Summary:
//   Forward prediction copies the live physics facade, restores captured body
//   rows and hidden solver state, then advances privately. This test performs
//   that exact seed sequence and compares every body field after each tick.
//
// Glossary:
//   Solver snapshot: Hidden sleep, contact-cache, island, and diagnostics state
//     needed in addition to public body poses and velocities.
//   Seed tick: Fixed-step boundary where the live engine is copied/restored.
//   Sleep transition: Tick where an awake supported body becomes sleeping.
//
// Invariants:
//   - Comparisons are byte-exact; tolerance would hide prediction divergence.
//   - The fixture contains retained body/terrain contact state and a sleeping
//     body at the seed, then observes another body cross a sleep transition.
//   - PhysicsEngine instances use static storage because their bounded solver
//     scratch is intentionally too large for the test-thread stack.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
//   - SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Rendering/IRenderResourceFactory.h"
#include "../SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestRenderResourceDoubles.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ReplaySolverWorldSnapshot;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
constexpr int kBodyCount = 3;
constexpr int kPreSleepTicks = 8;
constexpr int kWarmupTicks = 16;
constexpr int kFutureTicks = 64;

struct ReplayBodySeed
{
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

SkullbonezCore::Core::EngineConfig FidelityConfig()
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelTornadoField = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;
    config.worldForces.gravity = -9.8f;
    config.worldForces.fluidDensity = 0.0f;
    config.worldForces.gasDensity = 0.0f;
    config.bodySimulation.velocityLimit = 1000.0f;
    config.broadphase.cellSize = 16.0f;
    config.physicsSleep.frames = 4;
    config.physicsSleep.linearSpeed = 0.25f;
    config.physicsSleep.angularSpeed = 0.25f;
    return config;
}

PhysicsWorldForces FidelityForces()
{
    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.fluidDensity = 0.0f;
    forces.gasDensity = 0.0f;
    forces.angularDragMultiplier = 0.0f;
    return forces;
}

Terrain& FidelityTerrain()
{
    static SkullbonezCore::Core::EngineConfig config = FidelityConfig();
    static SkullbonezCore::Assets::AssetSystem assets;
    static SkullbonezTests::NullRenderResourceFactory resources;
    static Terrain terrain( 0.0f, 0.0f, 0.0f, config, assets, resources );
    return terrain;
}

void AddFidelityBody( PhysicsEngine& engine, uint32_t id, float x, float height, PhysicsBodyMotionKind motionKind )
{
    constexpr float radius = 1.0f;
    constexpr float mass = 2.0f;
    constexpr float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
    auto body = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ id },
                                           shape,
                                           Vector3( x, height, 0.0f ),
                                           SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                           Vector3( 0.0f, 0.0f, 0.0f ),
                                           Vector3( 0.0f, 0.0f, 0.0f ),
                                           Vector3( inertia, inertia, inertia ),
                                           mass,
                                           0.0f,
                                           motionKind,
                                           &FidelityTerrain(),
                                           "unit-replay-fidelity-body" );
    body.angularVelocityLimit = 1000.0f;
    auto collider = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    collider.sceneObjectId = body.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( body, collider ).IsValid() );
}

void SeedFidelityWorld( PhysicsEngine& engine, const SkullbonezCore::Core::EngineConfig& config )
{
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( true );
    AddFidelityBody( engine, 701u, 0.0f, 1.0f, PhysicsBodyMotionKind::Dynamic );
    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() == 1 );
}

void StepFidelityWorld( PhysicsEngine& engine,
                        const SkullbonezCore::Core::EngineConfig& config,
                        const PhysicsWorldForces& forces,
                        WorkerPool& workerPool )
{
    engine.Step( PHYSICS_FIXED_DT,
                 config,
                 forces,
                 workerPool,
                 nullptr,
                 0,
                 SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );
}

ReplayBodySeed CaptureBodySeed( const PhysicsBodyRecord& body )
{
    ReplayBodySeed seed;
    seed.replayBodyId = body.replayBodyId;
    seed.fixed = body.isFixed;
    seed.position = body.position;
    seed.orientation = body.orientation;
    seed.linearVelocity = body.linearVelocity;
    seed.angularVelocity = body.angularVelocity;
    seed.mass = body.mass;
    seed.inverseMass = body.invMass;
    seed.rotationalInertia = body.rotationalInertia;
    seed.inverseRotationalInertia = body.invRotationalInertia;
    return seed;
}

bool CompareBodyField( int tick,
                       int bodyIndex,
                       const char* field,
                       const void* left,
                       const void* right,
                       std::size_t bytes )
{
    if ( std::memcmp( left, right, bytes ) == 0 )
    {
        return true;
    }
    const std::string message =
        "first divergence tick=" + std::to_string( tick ) + " body=" + std::to_string( bodyIndex ) + " field=" + field;
    CHECK_MESSAGE( false, message );
    return false;
}

bool CompareBodyState( int tick, int bodyIndex, const PhysicsBodyRecord& live, const PhysicsBodyRecord& predicted )
{
#define CHECK_REPLAY_FIELD( field )                                                                                    \
    if ( !CompareBodyField( tick, bodyIndex, #field, &live.field, &predicted.field, sizeof( live.field ) ) )           \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    CHECK_REPLAY_FIELD( position );
    CHECK_REPLAY_FIELD( orientation );
    CHECK_REPLAY_FIELD( linearVelocity );
    CHECK_REPLAY_FIELD( angularVelocity );
    CHECK_REPLAY_FIELD( mass );
    CHECK_REPLAY_FIELD( invMass );
    CHECK_REPLAY_FIELD( rotationalInertia );
    CHECK_REPLAY_FIELD( invRotationalInertia );
    CHECK_REPLAY_FIELD( isFixed );
    CHECK_REPLAY_FIELD( isSleeping );
#undef CHECK_REPLAY_FIELD
    return true;
}
} // namespace

TEST_CASE( "Replay prediction snapshot seed reproduces every future fixed tick" )
{
    static PhysicsEngine live;
    static PhysicsEngine predicted;
    SkullbonezCore::Core::EngineConfig config = FidelityConfig();
    const PhysicsWorldForces forces = FidelityForces();
    WorkerPool workerPool;
    SeedFidelityWorld( live, config );

    // Why: pre-sleep an isolated terrain-supported body, then introduce a
    // fixed/dynamic sphere pair. At the seed the first body is sleeping while
    // the pair has only just established a persistent contact and will cross a
    // sleep transition during the compared future.
    for ( int tick = 0; tick < kPreSleepTicks; ++tick )
    {
        StepFidelityWorld( live, config, forces, workerPool );
    }
    config.physicsSleep.frames = 24;
    live.ApplyRuntimeConfig( config );
    AddFidelityBody( live, 702u, 4.0f, 1.0f, PhysicsBodyMotionKind::Fixed );
    AddFidelityBody( live, 703u, 4.0f, 3.05f, PhysicsBodyMotionKind::Dynamic );
    for ( int tick = 0; tick < kWarmupTicks; ++tick )
    {
        StepFidelityWorld( live, config, forces, workerPool );
    }

    REQUIRE( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( live ).Count() == kBodyCount );
    const auto seedSleepStates = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( live );
    REQUIRE( seedSleepStates.size() == kBodyCount );
    REQUIRE( seedSleepStates[0] != 0 );
    REQUIRE( seedSleepStates[2] == 0 );

    ReplaySolverWorldSnapshot solverSeed;
    live.CaptureReplaySolverSnapshot( solverSeed, MakePhysicsBodyCountFromNonNegativeInt( kBodyCount ) );
    const bool hasRetainedContactState = !solverSeed.persistentContacts.empty() ||
                                         !solverSeed.persistentContactCache.empty() ||
                                         !solverSeed.debugContacts.empty();
    const PhysicsBodyRecord* contactBody =
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( live ).RecordForModelIndex( 2 );
    INFO( "persistent=" << solverSeed.persistentContacts.size() << " cache=" << solverSeed.persistentContactCache.size()
                        << " debug=" << solverSeed.debugContacts.size()
                        << " y=" << ( contactBody ? contactBody->position.y : -999.0f ) );
    REQUIRE( hasRetainedContactState );

    std::array<ReplayBodySeed, kBodyCount> bodySeeds;
    for ( int i = 0; i < kBodyCount; ++i )
    {
        const PhysicsBodyRecord* body =
            SkullbonezCore::Physics::PhysicsEngine::ReadBodies( live ).RecordForModelIndex( i );
        REQUIRE( body != nullptr );
        bodySeeds[static_cast<std::size_t>( i )] = CaptureBodySeed( *body );
    }

    predicted = live;
    // Why: advance the copied engine before restore so hidden state cannot
    // remain accidentally correct merely because copy assignment preserved it.
    // A snapshot field omitted from capture/restore must now surface when the
    // two futures are compared.
    StepFidelityWorld( predicted, config, forces, workerPool );
    for ( int i = 0; i < kBodyCount; ++i )
    {
        const ReplayBodySeed& seed = bodySeeds[static_cast<std::size_t>( i )];
        const auto handle = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( predicted ).HandleForModelIndex( i );
        REQUIRE( predicted.RestoreReplayBodyState( handle,
                                                   seed.replayBodyId,
                                                   seed.fixed,
                                                   seed.position,
                                                   seed.orientation,
                                                   seed.linearVelocity,
                                                   seed.angularVelocity,
                                                   seed.mass,
                                                   seed.inverseMass,
                                                   seed.rotationalInertia,
                                                   seed.inverseRotationalInertia ) );
    }
    REQUIRE(
        predicted.RestoreReplaySolverSnapshot( solverSeed, MakePhysicsBodyCountFromNonNegativeInt( kBodyCount ) ) );

    bool observedTargetAwakeToSleepTransition = false;
    std::array<uint8_t, kBodyCount> previousSleep = { seedSleepStates[0], seedSleepStates[1] };
    for ( int tick = 1; tick <= kFutureTicks; ++tick )
    {
        StepFidelityWorld( live, config, forces, workerPool );
        StepFidelityWorld( predicted, config, forces, workerPool );

        const auto liveSleep = SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( live );
        for ( int i = 0; i < kBodyCount; ++i )
        {
            const PhysicsBodyRecord* liveBody =
                SkullbonezCore::Physics::PhysicsEngine::ReadBodies( live ).RecordForModelIndex( i );
            const PhysicsBodyRecord* predictedBody =
                SkullbonezCore::Physics::PhysicsEngine::ReadBodies( predicted ).RecordForModelIndex( i );
            REQUIRE( liveBody != nullptr );
            REQUIRE( predictedBody != nullptr );
            REQUIRE( CompareBodyState( tick, i, *liveBody, *predictedBody ) );
            observedTargetAwakeToSleepTransition =
                observedTargetAwakeToSleepTransition || ( i == 2 && previousSleep[static_cast<std::size_t>( i )] == 0 &&
                                                          liveSleep[static_cast<std::size_t>( i )] != 0 );
            previousSleep[static_cast<std::size_t>( i )] = liveSleep[static_cast<std::size_t>( i )];
        }
    }
    CHECK( observedTargetAwakeToSleepTransition );
}

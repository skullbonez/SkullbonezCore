//
// File: SkullbonezTests/TestReplayDeterminism.cpp
// Purpose:
//   Lock Replay-owned solver sample restore against a live PhysicsEngine world.
//
// Summary:
//   Replay records Physics-owned solver state beside detached body rows. This
//   test restores that combined record and proves the recaptured future sample
//   is byte-exact through Replay's production fingerprint without constructing
//   SceneController or presentation owners.
//
// Glossary:
//   Replay restore point: Solver snapshot and body rows captured at one fixed
//     frame, then reapplied after the live engine has advanced beyond it.
//   Replay future: Recaptured sample reached by replaying the same fixed ticks
//     from a restore point.
//
// Invariants:
//   - Solver state is restored before body rows, matching Runtime replay order.
//   - The terrain and config outlive every engine view retained by the fixture.
//   - Sample and kinematic comparisons are byte-exact, not epsilon-based.
//
// Related:
//   - SkullbonezTests/TestDeterminism.cpp
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
//   - SkullbonezSource/Physics/PhysicsEngine.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsMotionEligibility.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::LoadPhysicsBodyHotState;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyRestoreState;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsTerrainView;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ReplayBodyShapeKind;
using SkullbonezCore::Runtime::ReplayFrameIndex;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
constexpr int kBodyCount = 3;
constexpr int kSnapshotFrame = 30;
constexpr int kReplayWindowTicks = 30;

SkullbonezCore::Core::EngineConfig MakeReplayDeterminismConfig()
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

PhysicsWorldForces ReplayDeterminismForces()
{
    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.fluidDensity = 0.0f;
    forces.gasDensity = 0.0f;
    forces.angularDragMultiplier = 0.0f;
    return forces;
}

// Lifetime: PhysicsEngine retains the terrain view, so reverse member
// destruction retires the engine before its terrain and construction config.
class ReplayDeterminismFixture final
{
  public:
    ReplayDeterminismFixture()
        : m_config( MakeReplayDeterminismConfig() ), m_terrain( 0.0f, 0.0f, 0.0f, m_config ),
          m_engine( std::make_unique<PhysicsEngine>() )
    {
        m_engine->SetTerrainView( m_terrain.PhysicsView() );
    }

    PhysicsEngine& Engine() noexcept
    {
        return *m_engine;
    }

    PhysicsTerrainView TerrainView() const noexcept
    {
        return m_terrain.PhysicsView();
    }

  private:
    SkullbonezCore::Core::EngineConfig m_config;
    Terrain m_terrain;
    std::unique_ptr<PhysicsEngine> m_engine;
};

PhysicsBodyHotState RequireHotState( const PhysicsEngine& engine, int modelIndex )
{
    const PhysicsBodyStore& bodies = PhysicsEngine::ReadBodies( engine );
    REQUIRE( modelIndex >= 0 );
    REQUIRE( modelIndex < bodies.Count() );
    return LoadPhysicsBodyHotState( bodies.HotFields(), static_cast<std::size_t>( modelIndex ) );
}

void SeedReplayWorld( PhysicsEngine& engine, PhysicsTerrainView terrainView )
{
    engine.Clear();
    engine.ApplyRuntimeConfig( MakeReplayDeterminismConfig() );
    engine.SetSleepEnabled( false );
    engine.SetTerrainView( terrainView );

    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
        SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    engine.ReserveAuthoredBodyCapacity( kBodyCount );

    for ( int modelIndex = 0; modelIndex < kBodyCount; ++modelIndex )
    {
        const float radius = 1.0f;
        const float mass = 2.0f;
        const float inertia = 0.4f * mass * radius * radius;
        const CollisionShape shape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
        const uint32_t sceneObjectId = static_cast<uint32_t>( 101 + modelIndex );
        auto body = MakePhysicsBodyCreateDesc(
            PhysicsSceneObjectId { sceneObjectId }, shape,
            Vector3( 100.0f + static_cast<float>( modelIndex * 12 ), 30.0f + static_cast<float>( modelIndex * 10 ),
                     100.0f ),
            SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
            Vector3( 1.5f - static_cast<float>( modelIndex ) * 1.25f,
                     static_cast<float>( modelIndex ) * 0.25f, 0.0f ),
            Vector3( 0.03f * static_cast<float>( sceneObjectId ), 0.0f, 0.0f ),
            Vector3( inertia, inertia, inertia ), mass, 0.0f, PhysicsBodyMotionKind::Dynamic,
            "unit-replay-determinism-body" );
        body.angularVelocityLimit = 1000.0f;
        auto collider = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
        collider.sceneObjectId = body.sceneObjectId;
        REQUIRE( engine.RegisterAuthoredBody( body, collider ).IsValid() );
    }
}

void StepReplayWorld( PhysicsEngine& engine, int ticks )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    const PhysicsWorldForces forces = ReplayDeterminismForces();
    for ( int tick = 0; tick < ticks; ++tick )
    {
        engine.Step( PHYSICS_FIXED_DT, forces, workerPool,
                     SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
    }
}

ReplaySolverFrameSample CaptureReplaySample( const PhysicsEngine& engine, ReplayFrameIndex frameIndex )
{
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.sceneFrame = static_cast<int>( frameIndex );
    sample.simulationSeconds = static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    sample.physicsDt = PHYSICS_FIXED_DT;
    sample.world.gravity = ReplayDeterminismForces().gravity;
    sample.world.fluidHeight = ReplayDeterminismForces().fluidSurfaceHeight;
    sample.world.fluidDensity = ReplayDeterminismForces().fluidDensity;
    sample.world.fixedStep = true;
    sample.world.scenePhysicsEnabled = true;
    sample.world.sceneTextEnabled = true;
    sample.contactCount = static_cast<uint16_t>( PhysicsEngine::ReadDebugContacts( engine ).size() );
    sample.pipelineRecordCount = static_cast<uint16_t>( PhysicsEngine::ReadPipelineRecordCount( engine ) );
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot.physics,
                                        MakePhysicsBodyCountFromNonNegativeInt( kBodyCount ) );
    sample.bodies.reserve( kBodyCount );

    for ( int modelIndex = 0; modelIndex < kBodyCount; ++modelIndex )
    {
        const PhysicsBodyRecord* record = PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( modelIndex );
        REQUIRE( record != nullptr );
        const PhysicsBodyHotState hot = RequireHotState( engine, modelIndex );
        ReplaySolverBodySample body;
        body.id = record->sceneObjectId;
        body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( modelIndex );
        body.shapeKind = ReplayBodyShapeKind::Sphere;
        body.position = hot.position;
        body.linearVelocity = hot.linearVelocity;
        body.angularVelocity = hot.angularVelocity;
        hot.orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2],
                                       body.orientation[3] );
        body.mass = record->mass;
        body.inverseMass = hot.inverseMass;
        body.rotationalInertia = record->rotationalInertia;
        body.inverseRotationalInertia = hot.inverseRotationalInertia;
        body.fixed = hot.fixed;

        const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
        const auto sleepStates = PhysicsEngine::ReadSleepStates( engine );
        const auto sleepSupportedStates = PhysicsEngine::ReadSleepSupportedStates( engine );
        const auto sleepInhibitedStates = PhysicsEngine::ReadSleepInhibitedStates( engine );
        const auto collisionContacts = PhysicsEngine::ReadCollisionVisualContacts( engine );
        const auto sleepIslandIds = PhysicsEngine::ReadSleepIslandVisualIds( engine );
        body.sleeping = bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
        body.sleepSupported = bodyIndex < sleepSupportedStates.size() && sleepSupportedStates[bodyIndex] != 0;
        body.sleepInhibited = bodyIndex < sleepInhibitedStates.size() && sleepInhibitedStates[bodyIndex] != 0;
        body.collisionContact = bodyIndex < collisionContacts.size() && collisionContacts[bodyIndex] != 0;
        body.sleepIslandVisualId = bodyIndex < sleepIslandIds.size() ? sleepIslandIds[bodyIndex] : 0;
        sample.bodies.push_back( body );
    }

    // Invariant: the unit oracle uses the production Replay hash so promotion
    // bits, contacts, joints, solver rows, and cell keys cannot bypass it.
    sample.solverHash = SkullbonezCore::Runtime::ReplaySolverHashForSample( sample );
    sample.presentationHash = sample.solverHash;
    return sample;
}

void RestoreReplaySample( PhysicsEngine& engine, const ReplaySolverFrameSample& sample )
{
    // Invariant: replay restores solver caches before body rows so the next
    // fixed tick consumes a coherent historical world.
    REQUIRE( sample.worldSnapshot.physics.modelCount == static_cast<int>( sample.bodies.size() ) );
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        sample.worldSnapshot.physics,
        MakePhysicsBodyCountFromNonNegativeInt( static_cast<int>( sample.bodies.size() ) ) ) );

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        const PhysicsBodyRecord* record = PhysicsEngine::ReadBodies( engine ).RecordForModelIndex( body.modelRow.value );
        REQUIRE( record != nullptr );
        const Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2],
                                      body.orientation[3] );
        REQUIRE( engine.RestoreReplayBodyState( PhysicsBodyRestoreState {
            record->handle, body.id, body.fixed, body.position, orientation, body.linearVelocity,
            body.angularVelocity, body.mass, body.inverseMass, body.rotationalInertia,
            body.inverseRotationalInertia } ) );
    }
}

template <typename T> void CheckVectorExact( const std::vector<T>& left, const std::vector<T>& right )
{
    REQUIRE( left.size() == right.size() );
    for ( std::size_t index = 0; index < left.size(); ++index )
    {
        CHECK( left[index] == right[index] );
    }
}

void CheckVectorBytesExact( const Vector3& left, const Vector3& right )
{
    CHECK( std::memcmp( &left, &right, sizeof( Vector3 ) ) == 0 );
}

void CheckReplayBodyExact( const ReplaySolverBodySample& left, const ReplaySolverBodySample& right )
{
    CHECK( left.id.value == right.id.value );
    CHECK( left.modelRow.value == right.modelRow.value );
    CHECK( left.shapeKind == right.shapeKind );
    CheckVectorBytesExact( left.position, right.position );
    CheckVectorBytesExact( left.linearVelocity, right.linearVelocity );
    CheckVectorBytesExact( left.angularVelocity, right.angularVelocity );
    CHECK( std::memcmp( left.orientation, right.orientation, sizeof( left.orientation ) ) == 0 );
    CHECK( std::memcmp( &left.mass, &right.mass, sizeof( left.mass ) ) == 0 );
    CHECK( std::memcmp( &left.inverseMass, &right.inverseMass, sizeof( left.inverseMass ) ) == 0 );
    CheckVectorBytesExact( left.rotationalInertia, right.rotationalInertia );
    CheckVectorBytesExact( left.inverseRotationalInertia, right.inverseRotationalInertia );
    CHECK( left.fixed == right.fixed );
    CHECK( left.sleeping == right.sleeping );
    CHECK( left.sleepSupported == right.sleepSupported );
    CHECK( left.sleepInhibited == right.sleepInhibited );
    CHECK( left.collisionContact == right.collisionContact );
    CHECK( left.sleepIslandVisualId == right.sleepIslandVisualId );
    CHECK( left.contactCount == right.contactCount );
    CHECK( std::memcmp( &left.maxPenetration, &right.maxPenetration, sizeof( left.maxPenetration ) ) == 0 );
    CHECK( std::memcmp( &left.normalImpulseSum, &right.normalImpulseSum,
                        sizeof( left.normalImpulseSum ) ) == 0 );
}

void CheckReplaySamplesExact( const ReplaySolverFrameSample& expected, const ReplaySolverFrameSample& actual )
{
    // Invariant: the recaptured future frame matches at the complete Replay
    // sample boundary, not merely at live PhysicsBodyStore kinematics.
    CHECK( expected.frameIndex == actual.frameIndex );
    CHECK( expected.sceneFrame == actual.sceneFrame );
    CHECK( std::memcmp( &expected.simulationSeconds, &actual.simulationSeconds,
                        sizeof( expected.simulationSeconds ) ) == 0 );
    CHECK( std::memcmp( &expected.physicsDt, &actual.physicsDt, sizeof( expected.physicsDt ) ) == 0 );
    CHECK( std::memcmp( &expected.world.gravity, &actual.world.gravity,
                        sizeof( expected.world.gravity ) ) == 0 );
    CHECK( std::memcmp( &expected.world.fluidHeight, &actual.world.fluidHeight,
                        sizeof( expected.world.fluidHeight ) ) == 0 );
    CHECK( std::memcmp( &expected.world.fluidDensity, &actual.world.fluidDensity,
                        sizeof( expected.world.fluidDensity ) ) == 0 );
    CHECK( expected.world.fixedStep == actual.world.fixedStep );
    CHECK( expected.world.scenePhysicsEnabled == actual.world.scenePhysicsEnabled );
    CHECK( expected.world.sceneTextEnabled == actual.world.sceneTextEnabled );
    CHECK( expected.worldSnapshot.physics.version == actual.worldSnapshot.physics.version );
    CHECK( expected.worldSnapshot.physics.modelCount == actual.worldSnapshot.physics.modelCount );
    CHECK( expected.worldSnapshot.physics.sleepEnabled == actual.worldSnapshot.physics.sleepEnabled );
    CHECK( std::memcmp( &expected.worldSnapshot.tornadoSystemElapsedSeconds,
                        &actual.worldSnapshot.tornadoSystemElapsedSeconds,
                        sizeof( expected.worldSnapshot.tornadoSystemElapsedSeconds ) ) == 0 );
    CheckVectorExact( expected.worldSnapshot.physics.timeRemaining, actual.worldSnapshot.physics.timeRemaining );
    CheckVectorExact( expected.worldSnapshot.physics.sleepState, actual.worldSnapshot.physics.sleepState );
    CheckVectorExact( expected.worldSnapshot.physics.sleepCounter, actual.worldSnapshot.physics.sleepCounter );
    CheckVectorExact( expected.worldSnapshot.physics.sleepPoseAnchorPosition,
                      actual.worldSnapshot.physics.sleepPoseAnchorPosition );
    CheckVectorExact( expected.worldSnapshot.physics.sleepPoseAnchorOrientation,
                      actual.worldSnapshot.physics.sleepPoseAnchorOrientation );
    CheckVectorExact( expected.worldSnapshot.physics.sleepPoseAnchorValid,
                      actual.worldSnapshot.physics.sleepPoseAnchorValid );
    CheckVectorExact( expected.worldSnapshot.physics.collisionVisualContacts,
                      actual.worldSnapshot.physics.collisionVisualContacts );
    CheckVectorExact( expected.worldSnapshot.physics.sleepIslandParent,
                      actual.worldSnapshot.physics.sleepIslandParent );
    CheckVectorExact( expected.worldSnapshot.physics.sleepIslandRank,
                      actual.worldSnapshot.physics.sleepIslandRank );
    CHECK( expected.contactCount == actual.contactCount );
    CHECK( expected.pipelineRecordCount == actual.pipelineRecordCount );
    CHECK( expected.solverHash != 0u );
    CHECK( expected.solverHash == actual.solverHash );
    CHECK( expected.presentationHash == actual.presentationHash );
    REQUIRE( expected.bodies.size() == actual.bodies.size() );
    for ( std::size_t index = 0; index < expected.bodies.size(); ++index )
    {
        CheckReplayBodyExact( expected.bodies[index], actual.bodies[index] );
    }
}
} // namespace


TEST_CASE( "Replay solver sample restore: recorded frame reproduces future frame" )
{
    ReplayDeterminismFixture expectedFixture;
    ReplayDeterminismFixture restoredFixture;
    PhysicsEngine& expected = expectedFixture.Engine();
    PhysicsEngine& restored = restoredFixture.Engine();
    SeedReplayWorld( expected, expectedFixture.TerrainView() );
    SeedReplayWorld( restored, restoredFixture.TerrainView() );

    StepReplayWorld( expected, kSnapshotFrame );
    StepReplayWorld( restored, kSnapshotFrame );
    const ReplaySolverFrameSample restorePoint = CaptureReplaySample( restored, kSnapshotFrame );

    StepReplayWorld( expected, kReplayWindowTicks );
    const ReplaySolverFrameSample expectedFuture =
        CaptureReplaySample( expected, kSnapshotFrame + kReplayWindowTicks );

    // Hazard: the previous unit-only hash omitted motion eligibility and could
    // accept a replay that restored a different Discrete/Swept future.
    ReplaySolverFrameSample promotionMutation = expectedFuture;
    REQUIRE_FALSE( promotionMutation.worldSnapshot.physics.motionEligibilityState.empty() );
    promotionMutation.worldSnapshot.physics.motionEligibilityState[0] ^=
        SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted;
    CHECK( SkullbonezCore::Runtime::ReplaySolverHashForSample( promotionMutation ) != expectedFuture.solverHash );

    StepReplayWorld( restored, kReplayWindowTicks );
    RestoreReplaySample( restored, restorePoint );
    StepReplayWorld( restored, kReplayWindowTicks );
    const ReplaySolverFrameSample restoredFuture =
        CaptureReplaySample( restored, kSnapshotFrame + kReplayWindowTicks );

    CheckReplaySamplesExact( expectedFuture, restoredFuture );
}

/*
File: TestContinuousPredictionProducer.cpp
Purpose:
  Proves continuous private-physics production without bounded-state leakage.

Summary:
  A one-body authoritative engine is fingerprinted before and after a continuous
  forecast runs beyond 120 seconds and through three rolling-window wraps. The
  test also pins flat warmed reserve counts, complete boundary rows, unlimited
  per-submit progress, and an untouched bounded PREDICT publication owner.

Invariants:
  - The live solver fingerprint is computed through Replay's production hash.
  - The producer test uses the production 120-second row capacity.
  - A zero-thread WorkerPool exercises the same typed submission inline, making
    the slice completion deterministic without changing its wall-clock stop.

Related:
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h
  - Agentic/Reference/runtime-reference.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/LockOrderValidator.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Gameplay/TornadoGameplay.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/CollisionShape.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

namespace
{
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakeModelRowHint;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ContinuousPredictionProducer;
using SkullbonezCore::Runtime::ContinuousPredictionProducerView;
using SkullbonezCore::Runtime::ContinuousPredictionTickObserver;
using SkullbonezCore::Runtime::ContinuousPredictionWindowRowCapacity;
using SkullbonezCore::Runtime::ReplayBodyShapeKind;
using SkullbonezCore::Runtime::ReplayPredictionPublication;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Runtime::ReplaySolverHashForSample;

class CompleteTickObserver final : public ContinuousPredictionTickObserver
{
  public:
    void ObserveCompleteContinuousPredictionTick( const PhysicsBodyStore& bodies,
                                                  std::span<const SkullbonezCore::Physics::PersistentContact>,
                                                  std::uint64_t absoluteTick ) noexcept override
    {
        ++completeTickCount;
        lastAbsoluteTick = absoluteTick;
        lastBodyCount = bodies.Count();
    }

    void ObserveInvalidContinuousPredictionPublication( std::uint64_t absoluteTick ) noexcept override
    {
        invalidPublicationTick = absoluteTick;
    }

    std::uint64_t completeTickCount = 0u;
    std::uint64_t lastAbsoluteTick = 0u;
    std::uint64_t invalidPublicationTick = 0u;
    int lastBodyCount = 0;
};

void PopulateContinuousLiveEngine( PhysicsEngine& engine )
{
    const CollisionShape shape = BoundingSphere( 0.5f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const auto body = MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( 9101u ), shape, Vector3( 12.0f, 4.0f, -3.0f ),
                                                 IDENTITY_QUATERNION, Vector3( 1.0f, 0.0f, 0.0f ),
                                                 Vector3( 0.0f, 0.1f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), 2.0f, 0.0f,
                                                 PhysicsBodyMotionKind::Dynamic, "continuous-producer-body" );
    auto collider = MakeColliderCreateDesc( shape, body.restitution, 0u, "continuous-producer-body" );
    collider.sceneObjectId = body.sceneObjectId;

    RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
    engine.ReserveAuthoredBodyCapacity( 1u, 1u );
    REQUIRE( engine.RegisterAuthoredBody( body, collider ).IsValid() );
}

ReplaySolverFrameSample CaptureLiveSolverSample( const PhysicsEngine& engine )
{
    ReplaySolverFrameSample sample;
    const PhysicsBodyStore& bodies = PhysicsEngine::ReadBodies( engine );
    const auto hot = bodies.HotFields();
    const int bodyCount = bodies.Count();
    sample.physicsDt = PHYSICS_FIXED_DT;
    sample.world.fixedStep = true;
    engine.CaptureReplaySolverSnapshot( sample.worldSnapshot.physics, MakePhysicsBodyCountFromNonNegativeInt( bodyCount ) );
    sample.contactCount = static_cast<std::uint16_t>( PhysicsEngine::ReadDebugContacts( engine ).size() );
    sample.pipelineRecordCount = static_cast<std::uint16_t>( PhysicsEngine::ReadPipelineRecordCount( engine ) );
    sample.bodies.reserve( static_cast<std::size_t>( bodyCount ) );

    for ( int row = 0; row < bodyCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );
        const auto* record = bodies.RecordForModelIndex( row );
        REQUIRE( record != nullptr );
        ReplaySolverBodySample body;
        body.id = record->sceneObjectId;
        body.modelRow = MakeModelRowHint( row );
        body.shapeKind = ReplayBodyShapeKind::Sphere;
        body.position = PhysicsBodyPosition( hot, index );
        body.linearVelocity = SkullbonezCore::Physics::PhysicsBodyLinearVelocity( hot, index );
        body.angularVelocity = SkullbonezCore::Physics::PhysicsBodyAngularVelocity( hot, index );
        const auto orientation = SkullbonezCore::Physics::PhysicsBodyOrientation( hot, index );
        orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        body.mass = record->mass;
        body.inverseMass = hot.inverseMass[index];
        body.rotationalInertia = record->rotationalInertia;
        body.inverseRotationalInertia = SkullbonezCore::Physics::PhysicsBodyInverseInertia( hot, index );
        body.fixed = hot.fixed[index] != 0u;
        const auto sleeping = PhysicsEngine::ReadSleepStates( engine );
        const auto sleepSupported = PhysicsEngine::ReadSleepSupportedStates( engine );
        const auto sleepInhibited = PhysicsEngine::ReadSleepInhibitedStates( engine );
        const auto collision = PhysicsEngine::ReadCollisionVisualContacts( engine );
        const auto island = PhysicsEngine::ReadSleepIslandVisualIds( engine );
        body.sleeping = index < sleeping.size() && sleeping[index] != 0u;
        body.sleepSupported = index < sleepSupported.size() && sleepSupported[index] != 0u;
        body.sleepInhibited = index < sleepInhibited.size() && sleepInhibited[index] != 0u;
        body.collisionContact = index < collision.size() && collision[index] != 0u;
        body.sleepIslandVisualId = index < island.size() ? island[index] : 0;
        sample.bodies.push_back( body );
    }

    sample.solverHash = ReplaySolverHashForSample( sample );
    return sample;
}

bool AdvanceUntilTick( ContinuousPredictionProducer& producer, std::uint64_t targetTick,
                       std::uint64_t& outLargestSubmitAdvance )
{
    outLargestSubmitAdvance = 0u;
    // Hazard: an iteration-count spin can exhaust itself before Windows gives
    // the one-thread worker a timeslice. A wall-clock deadline still bounds a
    // real stall while remaining independent of scheduler quantum length.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );

    while ( std::chrono::steady_clock::now() < deadline )
    {
        const std::uint64_t before = producer.View().newestAbsoluteTick;

        if ( before >= targetTick )
        {
            return true;
        }

        (void)producer.AdvanceFrame( std::chrono::steady_clock::now() );
        const ContinuousPredictionProducerView after = producer.View();

        if ( !after.workerInFlight )
        {
            outLargestSubmitAdvance = (std::max)( outLargestSubmitAdvance, after.newestAbsoluteTick - before );
        }

        if ( after.failed )
        {
            return false;
        }

        std::this_thread::sleep_for( std::chrono::microseconds( 50 ) );
    }

    return false;
}

bool ReadSingleBodyPositionAtTick( const ContinuousPredictionProducerView& view, std::uint64_t absoluteTick,
                                   Vector3& outPosition )
{
    if ( absoluteTick < view.samples.OldestAbsoluteTick() || absoluteTick > view.samples.NewestAbsoluteTick() )
    {
        return false;
    }

    std::array<Vector3, 1> positions;
    std::uint64_t resolvedTick = 0u;
    const std::size_t logicalRow = static_cast<std::size_t>( absoluteTick - view.samples.OldestAbsoluteTick() );
    return view.samples.TryReadRow( logicalRow, positions, resolvedTick ) && resolvedTick == absoluteTick
               ? ( outPosition = positions[0], true )
               : false;
}
} // namespace

TEST_CASE( "Continuous prediction producer: three wraps preserve live and bounded prediction state" )
{
    SkullbonezCore::Core::EngineConfig config;
    SkullbonezCore::Geometry::Terrain terrain( -100000.0f, 0.0f, 0.0f, config );
    PhysicsEngine liveEngine;
    liveEngine.SetTerrainView( terrain.PhysicsView() );
    PopulateContinuousLiveEngine( liveEngine );
    liveEngine.ApplyRuntimeConfig( config );
    SkullbonezCore::Gameplay::TornadoGameplay liveTornado;
    liveTornado.ReserveBodyCapacity( 1 );
    PhysicsWorldForces forces;
    SkullbonezCore::Threading::LockOrderValidator lockOrderValidator;
    SkullbonezCore::Threading::WorkerPool workerPool( lockOrderValidator );

    const std::uint64_t liveHashBefore = CaptureLiveSolverSample( liveEngine ).solverHash;
    REQUIRE( liveHashBefore != 0u );

    ReplayPredictionPublication boundedPublication;
    boundedPublication.PublishSlot( 6u, 10u );
    REQUIRE( boundedPublication.PublishedCount( 10u ) == 7u );
    REQUIRE_FALSE( boundedPublication.WorkerFailed() );

    ContinuousPredictionProducer producer;
    CompleteTickObserver observer;
    const std::size_t rowCapacity = ContinuousPredictionWindowRowCapacity();
    REQUIRE( rowCapacity == 14401u );
    REQUIRE( producer.Begin( liveEngine, liveTornado, config, forces, workerPool, ContinuousPredictionWindowRowCapacity(),
                             &observer ) );

    RuntimeReserveOwnerStatsView warmedStats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( SkullbonezCore::Runtime::REPLAY_PREDICTION_RESERVE_OWNER,
                                                            warmedStats ) );
    const std::size_t warmedBytes = producer.View().retainedBytes;
    const std::uint64_t targetTick = static_cast<std::uint64_t>( rowCapacity ) * 3u + 17u;
    std::uint64_t largestSubmitAdvance = 0u;
    REQUIRE( AdvanceUntilTick( producer, targetTick, largestSubmitAdvance ) );

    const ContinuousPredictionProducerView view = producer.View();
    REQUIRE( view.active );
    REQUIRE_FALSE( view.failed );
    CHECK( view.newestAbsoluteTick >= targetTick );
    CHECK( view.simulatedSeconds > 120.0 );
    CHECK( largestSubmitAdvance > 8u );
    CHECK( view.samples.RowCount() == rowCapacity );
    CHECK( view.samples.NewestAbsoluteTick() == view.newestAbsoluteTick );
    CHECK( view.samples.OldestAbsoluteTick() == view.newestAbsoluteTick - rowCapacity + 1u );
    CHECK( view.retainedBytes == warmedBytes );
    CHECK( view.measuredTicksPerMillisecond > 0.0 );
    CHECK( observer.completeTickCount == view.newestAbsoluteTick );
    CHECK( observer.lastAbsoluteTick == view.newestAbsoluteTick );
    CHECK( observer.lastBodyCount == 1 );
    CHECK( observer.invalidPublicationTick == 0u );

    std::array<Vector3, 1> oldestPosition;
    std::array<Vector3, 1> newestPosition;
    std::uint64_t oldestTick = 0u;
    std::uint64_t newestTick = 0u;
    REQUIRE( view.samples.TryReadRow( 0u, oldestPosition, oldestTick ) );
    REQUIRE( view.samples.TryReadRow( rowCapacity - 1u, newestPosition, newestTick ) );
    CHECK( oldestTick == view.samples.OldestAbsoluteTick() );
    CHECK( newestTick == view.samples.NewestAbsoluteTick() );
    CHECK( oldestPosition[0].x > 12.0f );
    CHECK( newestPosition[0].x > oldestPosition[0].x );
    CHECK( newestPosition[0].y == doctest::Approx( 4.0f ) );
    CHECK( newestPosition[0].z == doctest::Approx( -3.0f ) );

    RuntimeReserveOwnerStatsView afterWrapStats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( SkullbonezCore::Runtime::REPLAY_PREDICTION_RESERVE_OWNER,
                                                            afterWrapStats ) );
    CHECK( afterWrapStats.replayGrowths == warmedStats.replayGrowths );
    CHECK( boundedPublication.PublishedCount( 10u ) == 7u );
    CHECK_FALSE( boundedPublication.WorkerFailed() );
    CHECK( CaptureLiveSolverSample( liveEngine ).solverHash == liveHashBefore );

    producer.Stop();
    const ContinuousPredictionProducerView stopped = producer.View();
    CHECK_FALSE( stopped.active );
    CHECK( stopped.samples.Empty() );
    CHECK( stopped.retainedBytes == warmedBytes );

    const std::uint64_t growthsBeforeReseed = afterWrapStats.replayGrowths;
    REQUIRE( producer.Begin( liveEngine, liveTornado, config, forces, workerPool ) );
    const auto expiredFrameStart = std::chrono::steady_clock::now() - std::chrono::milliseconds( 10 );
    CHECK_FALSE( producer.AdvanceFrame( expiredFrameStart ) );
    CHECK( producer.View().newestAbsoluteTick == 0u );
    constexpr std::uint64_t WORKER_COUNT_WITNESS_TICK = 1024u;
    std::uint64_t inlineLargestSubmitAdvance = 0u;
    REQUIRE( AdvanceUntilTick( producer, WORKER_COUNT_WITNESS_TICK, inlineLargestSubmitAdvance ) );
    Vector3 inlinePosition;
    REQUIRE( ReadSingleBodyPositionAtTick( producer.View(), WORKER_COUNT_WITNESS_TICK, inlinePosition ) );
    RuntimeReserveOwnerStatsView reseedStats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( SkullbonezCore::Runtime::REPLAY_PREDICTION_RESERVE_OWNER,
                                                            reseedStats ) );
    CHECK( reseedStats.replayGrowths == growthsBeforeReseed );
    producer.Stop();

    workerPool.Initialise( 1 );
    REQUIRE( producer.Begin( liveEngine, liveTornado, config, forces, workerPool ) );
    std::uint64_t threadedLargestSubmitAdvance = 0u;
    REQUIRE( AdvanceUntilTick( producer, WORKER_COUNT_WITNESS_TICK, threadedLargestSubmitAdvance ) );
    Vector3 threadedPosition;
    REQUIRE( ReadSingleBodyPositionAtTick( producer.View(), WORKER_COUNT_WITNESS_TICK, threadedPosition ) );
    CHECK( threadedPosition.x == inlinePosition.x );
    CHECK( threadedPosition.y == inlinePosition.y );
    CHECK( threadedPosition.z == inlinePosition.z );
    producer.Stop();

    REQUIRE( producer.Begin( liveEngine, liveTornado, config, forces, workerPool ) );
    REQUIRE( producer.AdvanceFrame( std::chrono::steady_clock::now() ) );
    producer.Stop();
    CHECK_FALSE( producer.View().active );
    CHECK( producer.View().samples.Empty() );
    workerPool.Shutdown();
}

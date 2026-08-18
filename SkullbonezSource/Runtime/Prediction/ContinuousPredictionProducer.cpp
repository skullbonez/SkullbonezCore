/*
File: ContinuousPredictionProducer.cpp
Purpose:
  Implements private-engine seeding and unlimited-target continuous slices.

Summary:
  Begin captures live body/solver values synchronously, restores them into a
  reserve-owned private engine, and publishes tick zero. Each later submission
  advances and publishes indivisible fixed ticks until its own five-millisecond
  clock expires, while the frame side retains a separate admission check.

Invariants:
  - No worker code receives or mutates the authoritative Physics engine.
  - Every published tick contains exactly one position for every seeded body.
  - Capacity is prepared before the worker becomes active and retained on stop.
  - Cancellation is observed between indivisible ticks and joined before reset.

Related:
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp
  - SkullbonezTests/TestContinuousPredictionProducer.cpp
  - Agentic/Reference/runtime-reference.md
*/
#include "ContinuousPredictionProducer.h"

#include "ReplayPredictionReserve.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/Config.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace SkullbonezCore::Runtime
{
using namespace ReplayPredictionReserveOperations;

namespace
{
double ContinuousPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start ) noexcept
{
    return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
}

bool ContinuousPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start,
                                        double budgetMilliseconds ) noexcept
{
    return budgetMilliseconds > 0.0 && ContinuousPredictionElapsedMilliseconds( start ) >= budgetMilliseconds;
}
} // namespace

std::size_t ContinuousPredictionWindowRowCapacity() noexcept
{
    return static_cast<std::size_t>( std::ceil( CONTINUOUS_PREDICTION_WINDOW_SECONDS / static_cast<double>( PHYSICS_FIXED_DT ) ) ) +
           1u;
}

void ContinuousPredictionWorkerTask::Configure( ContinuousPredictionProducer& producer,
                                                Threading::WorkerPool& workerPool ) noexcept
{
    WaitForIdle();
    m_producer = &producer;
    m_workerPool = &workerPool;
}

bool ContinuousPredictionWorkerTask::Submit() noexcept
{
    if ( !m_producer || !m_workerPool )
    {
        return false;
    }

    bool expected = false;

    if ( !m_inFlight.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) )
    {
        return false;
    }

    m_workerPool->SubmitNoAlloc( *this );
    return true;
}

void ContinuousPredictionWorkerTask::WaitForIdle() const noexcept
{
    while ( m_inFlight.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }
}

void ContinuousPredictionWorkerTask::ExecuteWorkerTask() noexcept
{
    if ( m_producer && m_workerPool )
    {
        m_producer->RunWorkerSlice( *m_workerPool );
    }

    m_inFlight.store( false, std::memory_order_release );
}

ContinuousPredictionProducer::~ContinuousPredictionProducer()
{
    Stop();
}

bool ContinuousPredictionProducer::CaptureSeed( const Physics::PhysicsEngine& liveEngine )
{
    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( liveEngine );
    const auto records = bodyStore.Records();
    const auto hot = bodyStore.HotFields();
    m_modelCount = bodyStore.Count();

    if ( m_modelCount <= 0 || records.size() < static_cast<std::size_t>( m_modelCount ) ||
         hot.positionX.size() < static_cast<std::size_t>( m_modelCount ) )
    {
        return false;
    }

    m_bodySeeds.clear();

    if ( !ReserveReplayPredictionVector( m_bodySeeds, static_cast<std::size_t>( m_modelCount ), 0,
                                         "ContinuousPredictionProducer::bodySeeds" ) )
    {
        return false;
    }

    m_bodySeeds.resize( static_cast<std::size_t>( m_modelCount ) );

    for ( int row = 0; row < m_modelCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );
        const Physics::PhysicsBodyRecord& record = records[index];
        ContinuousPredictionBodySeed& seed = m_bodySeeds[index];
        seed.id = record.sceneObjectId;
        seed.modelRow.value = row;
        seed.position = Physics::PhysicsBodyPosition( hot, index );
        seed.orientation = Physics::PhysicsBodyOrientation( hot, index );
        seed.linearVelocity = Physics::PhysicsBodyLinearVelocity( hot, index );
        seed.angularVelocity = Physics::PhysicsBodyAngularVelocity( hot, index );
        seed.mass = record.mass;
        seed.inverseMass = hot.inverseMass[index];
        seed.rotationalInertia = record.rotationalInertia;
        seed.inverseRotationalInertia = Physics::PhysicsBodyInverseInertia( hot, index );
        seed.fixed = hot.fixed[index] != 0u;
    }

    liveEngine.CaptureReplaySolverSnapshot( m_solverSnapshot,
                                            Physics::MakePhysicsBodyCountFromNonNegativeInt( m_modelCount ) );
    return true;
}

bool ContinuousPredictionProducer::SeedPrivateEngine( const Physics::PhysicsEngine& liveEngine,
                                                      const Core::EngineConfig& config,
                                                      const Physics::PhysicsWorldForces& worldForces )
{
    int reservedBytes = 0;

    if ( !SeedReplayPredictionEngineStorage( m_engine, liveEngine, m_engineReserveBytes, reservedBytes ) || !m_engine )
    {
        return false;
    }

    m_engineReserveBytes = reservedBytes;
    m_engine->BindProfiler( m_profiler );
    m_engine->ApplyRuntimeConfig( config );
    m_worldForces = worldForces;

    const Physics::PhysicsBodyStore& privateBodies = Physics::PhysicsEngine::ReadBodies( *m_engine );

    if ( privateBodies.Count() != m_modelCount )
    {
        return false;
    }

    for ( const ContinuousPredictionBodySeed& seed : m_bodySeeds )
    {
        const Physics::PhysicsBodyHandle handle = privateBodies.HandleForModelIndex( seed.modelRow.value );
        const Physics::PhysicsBodyRecord* record = privateBodies.RecordForHandle( handle );

        if ( !record || record->sceneObjectId != seed.id ||
             !m_engine->RestoreReplayBodyState( { handle, seed.id, seed.fixed, seed.position, seed.orientation,
                                                  seed.linearVelocity, seed.angularVelocity, seed.mass, seed.inverseMass,
                                                  seed.rotationalInertia, seed.inverseRotationalInertia } ) )
        {
            return false;
        }
    }

    return m_engine->RestoreReplaySolverSnapshot( m_solverSnapshot,
                                                  Physics::MakePhysicsBodyCountFromNonNegativeInt( m_modelCount ) );
}

bool ContinuousPredictionProducer::Begin( const Physics::PhysicsEngine& liveEngine,
                                          const Gameplay::TornadoGameplay& liveTornado, const Core::EngineConfig& config,
                                          const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                                          std::size_t rowCapacity )
{
    if ( m_active.load( std::memory_order_acquire ) || m_workerTask.InFlight() )
    {
        return false;
    }

    m_samples.ResetAfterJoin();
    m_failed.store( false, std::memory_order_relaxed );
    m_cancelRequested.store( false, std::memory_order_relaxed );
    m_newestAbsoluteTick.store( 0u, std::memory_order_relaxed );
    m_measuredTicksPerMillisecond.store( 0.0, std::memory_order_relaxed );

    Core::Allocation::RuntimeAllocationScope replayAllocationScope( Core::Allocation::RuntimeAllocationPhase::Replay );
    Core::Allocation::RuntimeReserveOwnerScope ownerScope( ReplayPredictionReserveOwner() );

    if ( !CaptureSeed( liveEngine ) || !m_samples.Prepare( rowCapacity, static_cast<std::size_t>( m_modelCount ) ) ||
         !SeedPrivateEngine( liveEngine, config, worldForces ) )
    {
        MarkFailed();
        return false;
    }

    m_tornadoGameplay.SetReplayState( liveTornado.CaptureSeconds(), liveTornado.EjectCooldownSeconds(),
                                      liveTornado.GetFieldConfig(), liveTornado.GetSystemConfig(),
                                      liveTornado.GetSystemElapsedSeconds() );
    m_tornadoGameplay.SetParallelForceEvaluation( liveTornado.ParallelForceEvaluation() );
    m_tornadoGameplay.ReserveBodyCapacity( m_modelCount );

    if ( !m_samples.Start( 0u ) || !CapturePositionRow( 0u ) )
    {
        MarkFailed();
        return false;
    }

    m_workerTask.Configure( *this, workerPool );
    m_active.store( true, std::memory_order_release );
    return true;
}

bool ContinuousPredictionProducer::AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart,
                                                 double frameBudgetMilliseconds ) noexcept
{
    if ( !m_active.load( std::memory_order_acquire ) || m_failed.load( std::memory_order_acquire ) ||
         m_cancelRequested.load( std::memory_order_acquire ) ||
         ContinuousPredictionBudgetExpired( frameBudgetStart, frameBudgetMilliseconds ) )
    {
        return false;
    }

    return m_workerTask.Submit();
}

void ContinuousPredictionProducer::Stop() noexcept
{
    m_cancelRequested.store( true, std::memory_order_release );
    m_samples.RequestCancellation();
    m_workerTask.WaitForIdle();
    m_active.store( false, std::memory_order_release );
    m_samples.ResetAfterJoin();
}

ContinuousPredictionProducerView ContinuousPredictionProducer::View() const noexcept
{
    ContinuousPredictionProducerView view;
    view.samples = m_samples.AcquireSnapshot();
    view.newestAbsoluteTick = m_newestAbsoluteTick.load( std::memory_order_acquire );
    view.simulatedSeconds = static_cast<double>( view.newestAbsoluteTick ) * static_cast<double>( PHYSICS_FIXED_DT );
    view.measuredTicksPerMillisecond = m_measuredTicksPerMillisecond.load( std::memory_order_acquire );
    view.retainedBytes = m_samples.RetainedBytes() + static_cast<std::size_t>( (std::max)( 0, m_engineReserveBytes ) ) +
                         m_bodySeeds.capacity() * sizeof( ContinuousPredictionBodySeed );
    view.active = m_active.load( std::memory_order_acquire );
    view.workerInFlight = m_workerTask.InFlight();
    view.failed = m_failed.load( std::memory_order_acquire ) || m_samples.Failed();
    return view;
}

bool ContinuousPredictionProducer::CapturePositionRow( std::uint64_t absoluteTick ) noexcept
{
    if ( !m_engine || !m_samples.BeginRow( absoluteTick ) )
    {
        return false;
    }

    const Physics::PhysicsBodyStore& bodies = Physics::PhysicsEngine::ReadBodies( *m_engine );
    const auto hot = bodies.HotFields();

    if ( bodies.Count() != m_modelCount || hot.positionX.size() < static_cast<std::size_t>( m_modelCount ) )
    {
        return false;
    }

    for ( int row = 0; row < m_modelCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );

        if ( !m_samples.WriteBodyPosition( index, Physics::PhysicsBodyPosition( hot, index ) ) )
        {
            return false;
        }
    }

    return m_samples.PublishRow();
}

void ContinuousPredictionProducer::RunWorkerSlice( Threading::WorkerPool& workerPool ) noexcept
{
    if ( !m_engine || !m_active.load( std::memory_order_acquire ) )
    {
        return;
    }

    const auto sliceStart = std::chrono::steady_clock::now();
    std::uint64_t completedTicks = 0u;

    while ( !m_cancelRequested.load( std::memory_order_acquire ) )
    {
        const std::uint64_t currentTick = m_newestAbsoluteTick.load( std::memory_order_relaxed );

        if ( currentTick == ( std::numeric_limits<std::uint64_t>::max )() - 1u )
        {
            MarkFailed();
            break;
        }

        Core::Allocation::RuntimeAllocationScope replayAllocationScope( Core::Allocation::RuntimeAllocationPhase::Replay );
        const Physics::ExternalForceFrameInput
            externalForces = m_tornadoGameplay.BuildForceFrame( PHYSICS_FIXED_DT,
                                                                Physics::PhysicsEngine::ReadBodies( *m_engine ).Count() );
        m_engine->Step( PHYSICS_FIXED_DT, m_worldForces, externalForces, workerPool,
                        Physics::PhysicsDiagnosticsCsvWriter {} );

        const std::uint64_t nextTick = currentTick + 1u;

        if ( !CapturePositionRow( nextTick ) )
        {
            if ( !m_cancelRequested.load( std::memory_order_relaxed ) )
            {
                MarkFailed();
            }

            break;
        }

        m_newestAbsoluteTick.store( nextTick, std::memory_order_release );
        ++completedTicks;

        // Invariant: one Physics tick and its complete row are indivisible.
        // There is deliberately no finite target or deterministic eight-tick cap.
        if ( ContinuousPredictionElapsedMilliseconds( sliceStart ) >= CONTINUOUS_PREDICTION_WORKER_BUDGET_MILLISECONDS )
        {
            break;
        }
    }

    const double elapsedMilliseconds = ContinuousPredictionElapsedMilliseconds( sliceStart );

    if ( completedTicks > 0u && elapsedMilliseconds > 0.0 && std::isfinite( elapsedMilliseconds ) )
    {
        const double sample = static_cast<double>( completedTicks ) / elapsedMilliseconds;
        const double previous = m_measuredTicksPerMillisecond.load( std::memory_order_relaxed );
        m_measuredTicksPerMillisecond.store( previous > 0.0 ? previous + ( sample - previous ) * 0.25 : sample,
                                             std::memory_order_release );
    }
}

void ContinuousPredictionProducer::MarkFailed() noexcept
{
    m_failed.store( true, std::memory_order_release );
    m_cancelRequested.store( true, std::memory_order_release );
    m_samples.RequestCancellation();
}
} // namespace SkullbonezCore::Runtime

/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.cpp
Purpose:
  Owns replay prediction worker lifetime, cancellation, and prefix promotion.

Summary:
  The frame owner submits typed simulation slices through one schedule. Every
  destructive transition joins an in-flight slice before touching build state.

Glossary:
  Promotion: Freezing the acquire-visible build prefix as committed prediction.

Invariants:
  - Cancellation and destruction wait for the schedule to become idle.
  - Promotion joins the worker before swapping the visible prefix into ownership.

Related:
  - ReplayPredictionScheduling.h
  - ReplayPredictionPublicationOperations.h
*/
#include "ReplayPrediction.h"
#include "ReplayPredictionPublicationOperations.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;

void ReplayPredictionSimulationSlice::operator()( int beginTickIndex, int endTickIndex ) const
{
    // Lifetime: CancelPredictionJob waits for the enclosing AmortizedTask before
    // any of these replay-owned borrows can be cleared or replaced.
    if ( prediction && config && workerPool )
    {
        prediction->RunWorkerRange( *config, *workerPool, modelCount, beginTickIndex, endTickIndex );
    }
}

RunReplayPredictionState::~RunReplayPredictionState()
{
    // Hazard: WorkerPool tasks capture this replay state by reference. Destruct
    // only after the in-flight slice has dropped ownership of build scratch.
    build.schedule.WaitForIdle();
}


void ReplayPrediction::WaitForJobIdle()
{
    m_state.build.schedule.WaitForIdle();
}

bool ReplayPrediction::PromoteBuildPrefixToCommitted()
{
    if ( !m_state.BuildPrefixShouldBePresented() )
    {
        return false;
    }
    WaitForJobIdle();
    const std::size_t promotedFrameCount = m_state.PublishedBuildFrameCount();
    if ( promotedFrameCount < 2u || promotedFrameCount > m_state.build.buildFrames.size() )
    {
        return false;
    }

    // Hazard: this is the Play-button ownership transfer. The worker has
    // released buildFrames before the visible prefix becomes committed state.
    m_state.build.schedule.Reset();
    m_state.build.building = false;
    m_state.build.complete = true;
    m_state.simulation.frames.swap( m_state.build.buildFrames );
    m_state.simulation.frames.resize( promotedFrameCount );
    m_state.ResetBuildFramePublication();
    if ( !RebuildReplayPredictionCommittedRootTrajectory( m_state ) )
    {
        return false;
    }
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionTornadoGameplay.Clear();
    m_state.simulation.predictionWorld.ClearPreservingCapacity();
    return true;
}

void ReplayPrediction::CancelJob( bool clearSamples )
{
    WaitForJobIdle();
    m_state.build.schedule.Reset();
    m_state.build.building = false;
    m_state.build.complete = false;
    m_state.build.buildMode = ReplayPredictionBuildMode::Undecided;
    m_state.build.pendingLatestRestart = false;
    m_state.simulation.targetModelRow.value = -1;
    m_state.build.nextTick = 1;
    m_state.build.targetTickCount = 0;
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionTornadoGameplay.Clear();
    m_state.simulation.predictionWorld.ClearPreservingCapacity();
    // Runtime allocation policy: cancellation invalidates publication but keeps
    // the double-buffered frame payloads warm for the next replay rebuild.
    m_state.ResetBuildFramePublication();
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    if ( clearSamples )
    {
        m_state.build.supersededRestartCount = 0;
        m_state.build.latestRestartBeginCount = 0;
        m_state.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
        m_state.simulation.probeElapsedMs = 0.0;
        m_state.simulation.probeTicksCompleted = 0;
        m_state.simulation.calibratedModelCount = -1;
        m_state.simulation.frames.clear();
        m_state.trajectoryStore.Clear();
        ClearFutureNodeCache();
    }
}

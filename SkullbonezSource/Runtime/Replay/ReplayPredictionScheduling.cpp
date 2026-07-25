/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.cpp
Purpose:
  Owns replay prediction worker lifetime, cancellation, prefix promotion,
  steady-clock budgets, and the monotonic presentation reveal cursor.

Summary:
  The frame owner submits typed simulation slices through one schedule. Every
  destructive transition joins an in-flight slice before touching build state.

Glossary:
  Promotion: Freezing the acquire-visible build prefix as committed prediction.
  Reveal cursor: Monotonic prediction frame made visible by wall-clock pacing.

Invariants:
  - Cancellation and destruction wait for the schedule to become idle.
  - Promotion joins the worker before swapping the visible prefix into ownership.
  - Budget and reveal callers share one implementation so extracted publication
    units cannot drift from prediction orchestration.

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
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <chrono>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;

namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations
{
double ReplayPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start )
{
    return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
}

bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds )
{
    return budgetMilliseconds > 0.0 && ReplayPredictionElapsedMilliseconds( start ) >= budgetMilliseconds;
}

// Why: prediction diagnostics need the exact pass that lost work. Keeping the
// counter beside the single budget predicate prevents extracted publication
// units from changing either time units or accounting order.
bool ReplayPredictionBudgetExpiredForPass( ReplayPredictionUpdateResult& result,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass pass,
                                           const std::chrono::steady_clock::time_point& start,
                                           double budgetMilliseconds )
{
    if ( !ReplayPredictionBudgetExpired( start, budgetMilliseconds ) )
    {
        return false;
    }

    const std::size_t passIndex = static_cast<std::size_t>( pass );
    if ( passIndex < result.budgetExpiries.size() )
    {
        ++result.budgetExpiries[passIndex];
    }

    return true;
}

double ReplayPredictionRemainingMilliseconds( const std::chrono::steady_clock::time_point& start,
                                              double budgetMilliseconds )
{
    if ( budgetMilliseconds <= 0.0 )
    {
        return 0.0;
    }

    return (std::max)( 0.0, budgetMilliseconds - ReplayPredictionElapsedMilliseconds( start ) );
}

double ReplayPredictionRevealSecondsPerSecond( const RunReplayPredictionState& prediction )
{
    // Why: authored shot-list data is allowed to be imperfect. Non-positive
    // rates fall back to real-time pacing instead of freezing the reveal cursor
    // or dividing by zero while the prediction build catches up.
    return prediction.revealClock.secondsPerSecond > 0.0 ? prediction.revealClock.secondsPerSecond : 1.0;
}

// Concept: reveal cursor - the wall-clock playhead of the causal-unfold animation.
//
// Every prediction draw pass clamps to the frame this returns, so the pace of
// the visible tree comes from real time, not from how fast the build job
// happened to finish. While the job is still building, the cursor also clamps
// to the populated prefix and re-anchors at that edge, so a slow build paces
// the unfold without banking reveal debt.
// Invariant: the cursor is monotonic per prediction. It plays 0 -> horizon once
// and then holds, so every revealed line and causal box stays on screen.
ReplayFrameIndex ReplayPredictionRevealFrameIndex( RunReplayPredictionState& prediction,
                                                   ReplayFrameIndex lastAvailableFrame )
{
    if ( prediction.revealClock.deterministicFrameEnabled )
    {
        prediction.revealClock.presentedFrame = (std::min)( lastAvailableFrame,
                                                            prediction.revealClock.deterministicFrame );

        return prediction.revealClock.presentedFrame;
    }

    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        // Why: instant mode presents the completed future at once. The causal
        // unfold clock remains an amortized-mode presentation affordance.
        prediction.revealClock.presentedFrame = lastAvailableFrame;
        return prediction.revealClock.presentedFrame;
    }

    const auto now = std::chrono::steady_clock::now();
    if ( !prediction.revealClock.anchorValid )
    {
        prediction.revealClock.anchor = now;
        prediction.revealClock.anchorValid = true;
        prediction.revealClock.presentedFrame = 0;
        return prediction.revealClock.presentedFrame;
    }

    const double availableSeconds = static_cast<double>( lastAvailableFrame ) * ::PHYSICS_FIXED_DT;
    const double elapsedSeconds = (std::max)( 0.0,
                                              std::chrono::duration<double>( now - prediction.revealClock.anchor )
                                                  .count() );

    const double revealSecondsPerSecond = ReplayPredictionRevealSecondsPerSecond( prediction );
    double revealSeconds = elapsedSeconds * revealSecondsPerSecond;
    if ( prediction.build.building && revealSeconds > availableSeconds )
    {
        revealSeconds = availableSeconds;
        prediction.revealClock.anchor = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                  std::chrono::duration<double>( availableSeconds /
                                                                                 revealSecondsPerSecond ) );
    }

    const double revealFrame = revealSeconds / static_cast<double>( ::PHYSICS_FIXED_DT );
    prediction.revealClock.presentedFrame = (std::min)( lastAvailableFrame,
                                                        static_cast<ReplayFrameIndex>( revealFrame ) );

    return prediction.revealClock.presentedFrame;
}

std::size_t ReplayPredictionBuildPresentationFrameCountForRefresh( RunReplayPredictionState& prediction,
                                                                   Physics::PhysicsSceneObjectId requestedTargetId )
{
    if ( requestedTargetId.value == 0 || prediction.simulation.targetId.value != requestedTargetId.value ||
         prediction.simulation.frames.size() < 2u )
    {
        return 2u;
    }

    // Why: auto-refresh should replace the old future only after the rebuilding
    // prefix catches the causal story the user can already see.
    const ReplayFrameIndex lastCommittedFrame = prediction.simulation.frames.back().frameIndex;
    const ReplayFrameIndex revealFrame = ReplayPredictionRevealFrameIndex( prediction, lastCommittedFrame );
    return (std::max)( std::size_t { 2u }, static_cast<std::size_t>( revealFrame ) + 1u );
}
} // namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations

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
    m_state.build.liveVelocityEditRefreshPending = false;
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
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState {};
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

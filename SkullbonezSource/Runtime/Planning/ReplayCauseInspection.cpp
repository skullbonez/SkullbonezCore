/*
File: ReplayCauseInspection.cpp
Purpose:
  Resolves causal-row eligibility and advances the Planning-owned inspection transition.

Summary:
  Recorded rows are valid only inside the solver recorder's current bounded
  window. Prediction rows require an exact frame match in the published frame
  bank; neither path guesses, clamps, or reconstructs a missing frame. The
  transition owner derives a fixed cubic curve from total wall-clock elapsed,
  coalesces its discrete frame requests, rejects stale completion tokens, and
  publishes pause/return actions for App to apply through concrete owners.

Glossary:
  In-flight generation: Exact transport token currently awaiting Replay restore
    completion; a newer selected generation may wait behind it.
  Aftermath: Live follow mode entered from paused detail by Space.

Invariants:
  - Recorder statistics describe a contiguous half-open frame range.
  - Prediction matching is exact because publication may replace the frame bank.
  - Only the current generation may reveal detail or complete a return.
  - Forward and reverse transport round symmetrically and reach the exact target
    only at eased progress 1, independent of render cadence.
  - Planning publishes pause actions but never mutates Replay or camera owners directly.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
*/
#include "ReplayCauseInspection.h"

#include "../Prediction/ReplayPredictionView.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr const char* REPLAY_FRAME_EXPIRED_FEEDBACK = "Replay frame expired";
constexpr double REPLAY_CAUSE_TRANSITION_SECONDS = 1.5;
} // namespace

float EvaluateReplayCauseTransitionProgress( double elapsedSeconds ) noexcept
{
    const double u = std::clamp( elapsedSeconds / REPLAY_CAUSE_TRANSITION_SECONDS, 0.0, 1.0 );
    const double remaining = 1.0 - u;
    return static_cast<float>( 1.0 - remaining * remaining * remaining );
}

ReplayFrameIndex EvaluateReplayCauseTransitionFrame( ReplayFrameIndex sourceFrame, ReplayFrameIndex targetFrame,
                                                     float easedProgress ) noexcept
{
    const double progress = std::clamp( static_cast<double>( easedProgress ), 0.0, 1.0 );

    if ( progress >= 1.0 )
    {
        return targetFrame;
    }

    if ( sourceFrame <= targetFrame )
    {
        const ReplayFrameIndex distance = targetFrame - sourceFrame;
        return sourceFrame + static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
    }

    const ReplayFrameIndex distance = sourceFrame - targetFrame;
    return sourceFrame - static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
}

bool ReplayCauseSeekResult::CanTransport() const noexcept
{
    return availability == ReplayCauseSeekAvailability::Available;
}

const char* ReplayCauseSeekResult::Feedback() const noexcept
{
    return CanTransport() ? "" : REPLAY_FRAME_EXPIRED_FEEDBACK;
}

ReplayCauseSeekResult EvaluateReplayCauseSeek( const RunReplayCauseTreeRow& row, const ReplayRecorderStats& solverStats,
                                               std::span<const RunReplayPredictionFrame> predictionFrames ) noexcept
{
    ReplayCauseSeekResult result;
    result.frame = row.firstFrame;
    result.source = row.prediction ? ReplayCauseSeekSource::Prediction : ReplayCauseSeekSource::SolverHistory;

    if ( row.prediction )
    {
        const auto match = std::find_if( predictionFrames.begin(), predictionFrames.end(),
                                         [&]( const auto& frame ) { return frame.frameIndex == row.firstFrame; } );

        if ( match != predictionFrames.end() )
        {
            result.availability = ReplayCauseSeekAvailability::Available;
        }

        return result;
    }

    const ReplayFrameIndex retainedCount = static_cast<ReplayFrameIndex>( solverStats.sampleCount );
    const ReplayFrameIndex oldestFrame = solverStats.nextFrameIndex > retainedCount
                                             ? solverStats.nextFrameIndex - retainedCount
                                             : 0;

    if ( solverStats.sampleCount > 0 && row.firstFrame >= oldestFrame && row.firstFrame < solverStats.nextFrameIndex )
    {
        result.availability = ReplayCauseSeekAvailability::Available;
    }

    return result;
}

bool ReplayCauseInspection::Select( int rowIndex, const ReplayCauseSeekResult& seek, ReplayFrameIndex presentedFrame,
                                    bool simulationAlreadyPaused, double nowSeconds ) noexcept
{
    if ( rowIndex < 0 || !seek.CanTransport() )
    {
        return false;
    }

    // Invariant: zero is reserved for "no request" at the App boundary. A
    // wrap restarts at one so even a process-lifetime session keeps that seam.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive )
    {
        m_state.ownsPause = !simulationAlreadyPaused;
    }
    else
    {
        // Direct retargeting from aftermath reacquires only the pause that this
        // inspection itself caused. A pre-existing operator pause stays external.
        m_state.ownsPause = m_state.ownsPause || !simulationAlreadyPaused;
    }

    m_state.mode = ReplayCauseInspectionMode::Transporting;
    m_state.sourceFrame = presentedFrame;
    m_state.targetFrame = seek.frame;
    m_state.presentedFrame = presentedFrame;
    m_state.seekSource = seek.source;
    m_state.selectedRow = rowIndex;
    m_state.detailVisible = false;
    m_state.transportPending = false;
    m_state.easedProgress = 0.0f;
    m_startedAtSeconds = nowSeconds;
    m_pendingFrame = presentedFrame;
    return true;
}

void ReplayCauseInspection::Advance( double nowSeconds ) noexcept
{
    if ( m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return;
    }

    // Invariant: total elapsed time, not prior progress, owns the curve. A
    // backwards host clock clamps to the source rather than reversing a turn.
    m_state.easedProgress = EvaluateReplayCauseTransitionProgress( nowSeconds - m_startedAtSeconds );
    const ReplayFrameIndex requested = EvaluateReplayCauseTransitionFrame( m_state.sourceFrame, m_state.targetFrame,
                                                                           m_state.easedProgress );

    if ( requested != m_state.presentedFrame && ( !m_state.transportInFlight || requested != m_inFlightFrame ) )
    {
        // Coalescing replaces an obsolete not-yet-issued intermediate frame.
        m_pendingFrame = requested;
        m_state.transportPending = true;
    }

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportInFlight &&
         !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::TakeTransportRequest( ReplayCauseTransportRequest& outRequest ) noexcept
{
    if ( !m_state.transportPending || m_state.transportInFlight || m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return false;
    }

    outRequest.generation = m_state.generation;
    outRequest.sourceFrame = m_state.sourceFrame;
    outRequest.targetFrame = m_pendingFrame;
    outRequest.source = m_state.seekSource;
    m_state.transportPending = false;
    m_state.transportInFlight = true;
    m_inFlightGeneration = outRequest.generation;
    m_inFlightFrame = outRequest.targetFrame;
    m_state.transportFrame = outRequest.targetFrame;
    return true;
}

void ReplayCauseInspection::CompleteTransport( uint64_t generation, bool succeeded ) noexcept
{
    if ( generation == 0 || generation != m_inFlightGeneration )
    {
        return;
    }

    m_state.transportInFlight = false;
    m_inFlightGeneration = 0;

    // Hazard: a newer row may have been selected while this restore was in
    // flight. Its pending generation owns the next mutation; the old completion
    // must neither expose detail nor unwind the inspection pause.
    if ( generation != m_state.generation )
    {
        return;
    }

    if ( !succeeded )
    {
        m_state.mode = ReplayCauseInspectionMode::Returning;
        m_state.detailVisible = false;
        return;
    }

    m_state.presentedFrame = m_inFlightFrame;

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::BeginAftermath( bool& outReleasePause ) noexcept
{
    outReleasePause = false;

    if ( m_state.mode != ReplayCauseInspectionMode::DetailPaused )
    {
        return false;
    }

    m_state.mode = ReplayCauseInspectionMode::AftermathFollow;
    m_state.detailVisible = false;
    outReleasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    return true;
}

ReplayCauseExitAction ReplayCauseInspection::BeginReturn() noexcept
{
    ReplayCauseExitAction action;

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive || m_state.returnIssued )
    {
        return action;
    }

    action.apply = true;
    action.releasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    m_state.detailVisible = false;
    m_state.transportPending = false;
    m_state.mode = ReplayCauseInspectionMode::Returning;
    m_state.returnIssued = true;

    // Invalidate a synchronous completion that arrives after cancellation.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    return action;
}

void ReplayCauseInspection::CompleteReturn() noexcept
{
    if ( m_state.mode == ReplayCauseInspectionMode::Returning && !m_state.transportInFlight )
    {
        Reset();
    }
}

void ReplayCauseInspection::Reset() noexcept
{
    m_state = ReplayCauseInspectionView {};
    m_startedAtSeconds = 0.0;
    m_pendingFrame = 0;
    m_inFlightFrame = 0;
    m_inFlightGeneration = 0;
}

ReplayCauseInspectionView ReplayCauseInspection::View() const noexcept
{
    return m_state;
}
} // namespace SkullbonezCore::Runtime

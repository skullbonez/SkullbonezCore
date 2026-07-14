/*
File: SkullbonezSource/Runtime/Replay/ReplayScrubber.h
Purpose:
  Owns scrub cursor state and defines live-restore command values.

Summary:
  ReplayScrubber is the mutable cursor/track authority. ReplayRuntime sequences
  cross-owner prediction cancellation, physics restore, and camera reactions.

Glossary:
  Live edge: The newest retained replay sample.

Invariants:
  - Restore sample pointers are borrowed for the applying frame only.
  - Prediction cancellation completes before a live restore mutates authority.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
inline constexpr float REPLAY_SCRUBBER_LIVE_THRESHOLD = 0.995f;
inline constexpr float REPLAY_SCRUBBER_PRESENT_EPSILON = 0.0035f;

inline bool ReplayTimelineHasFuture( float presentT ) noexcept
{
    return presentT < REPLAY_SCRUBBER_LIVE_THRESHOLD;
}

inline bool ReplayAtPresentTrackPosition( float position, float presentT ) noexcept
{
    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return position >= REPLAY_SCRUBBER_LIVE_THRESHOLD;
    }
    return std::fabs( position - presentT ) <= REPLAY_SCRUBBER_PRESENT_EPSILON;
}

inline bool ReplayTrackPositionIsFuture( float position, float presentT ) noexcept
{
    return ReplayTimelineHasFuture( presentT ) && position > presentT + REPLAY_SCRUBBER_PRESENT_EPSILON;
}

inline float ReplaySolverNormalizedFromTrack( float position, float presentT ) noexcept
{
    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return std::clamp( position, 0.0f, 1.0f );
    }
    return std::clamp( position / (std::max)( presentT, 0.0001f ), 0.0f, 1.0f );
}

inline float ReplayPredictionNormalizedFromTrack( float position, float presentT ) noexcept
{
    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return 0.0f;
    }
    return std::clamp( ( position - presentT ) / ( 1.0f - presentT ), 0.0f, 1.0f );
}

enum class RunReplayTrack
{
    Presentation,
    Solver
};

struct RunReplayScrubberState
{
    bool visible = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool pauseRestoreFlyMode = false;
    bool pauseRestoreLauncherMode = false;
    bool restoreWasDown = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    float position = 1.0f;                                 // 0 = oldest retained sample, 1 = live edge.
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    double fadeUpdatedAt = 0.0;                            // Last scrubber opacity update in runtime seconds.
    float visibleAlpha = 0.0f;                             // 0 = hidden, 1 = fully faded in.
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

// Value-only publication for input, render, and validation consumers. Mutating
// this copy cannot move the retained replay cursor or alter restore/fade state.
struct ReplayScrubberView
{
    bool visible = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    float position = 1.0f;
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    float visibleAlpha = 0.0f;
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

struct ReplayScrubberInputFrame
{
    bool leftPressed = false;
    bool leftReleased = false;
    bool restorePressed = false;
};

struct ReplayScrubberUnavailableResult
{
    bool exitInspectionCamera = false;
};

struct RunReplayV2TargetRestoreResult
{
    std::size_t checkpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t hashCount = 0;
    std::size_t eventsApplied = 0;
    std::size_t bodyCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex checkpointFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    uint32_t eventCursor = 0;
    uint32_t branchId = 0;
    uint32_t parentBranchId = 0;
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    bool generatedTopologyRebuilt = false;
    bool madeLiveBranch = false;
    bool enterInteractiveRequested = false;
};

enum class ReplayLiveRestoreKind : uint8_t
{
    None,
    V2ArtifactTarget,
    SolverSample
};

struct ReplayLiveRestoreRequest
{
    ReplayLiveRestoreKind kind = ReplayLiveRestoreKind::None;
    const ReplaySolverFrameSample* solverSample = nullptr; // Borrowed until the workspace command is applied this frame.
    ReplayFrameIndex requestedFrame = 0;
    bool makeLiveBranch = false;
    bool enterInteractive = false;
    RunReplayTrack messageTrack = RunReplayTrack::Solver;
    double now = 0.0;
    char path[260] = {};
};

class ReplayScrubber
{
  public:
    ReplayScrubberView View() const noexcept
    {
        ReplayScrubberView view;
        view.visible = m_state.visible;
        view.historicalSamplePaused = m_state.historicalSamplePaused;
        view.liveAdvanceHeld = m_state.liveAdvanceHeld;
        view.restoreConsumedThisFrame = m_state.restoreConsumedThisFrame;
        view.activeTrack = m_state.activeTrack;
        view.saveMessageTrack = m_state.saveMessageTrack;
        view.position = m_state.position;
        view.presentationPosition = m_state.presentationPosition;
        view.solverPosition = m_state.solverPosition;
        view.mouseX = m_state.mouseX;
        view.mouseY = m_state.mouseY;
        view.visibleUntil = m_state.visibleUntil;
        view.visibleAlpha = m_state.visibleAlpha;
        view.saveMessageUntil = m_state.saveMessageUntil;
        strcpy_s( view.saveMessage, sizeof( view.saveMessage ), m_state.saveMessage );
        return view;
    }

    RunReplayScrubberState& State() noexcept
    {
        return m_state;
    }
    const RunReplayScrubberState& State() const noexcept
    {
        return m_state;
    }

    float TrackPosition( RunReplayTrack track ) const noexcept
    {
        return track == RunReplayTrack::Solver ? m_state.solverPosition : m_state.presentationPosition;
    }

    void SetTrackPosition( RunReplayTrack track, float position ) noexcept
    {
        const float clamped = std::clamp( position, 0.0f, 1.0f );
        if ( track == RunReplayTrack::Solver )
        {
            m_state.solverPosition = clamped;
        }
        else
        {
            m_state.presentationPosition = clamped;
        }
        if ( m_state.activeTrack == track )
        {
            m_state.position = clamped;
        }
    }

    void SyncActiveTrackPosition() noexcept
    {
        m_state.position = TrackPosition( m_state.activeTrack );
    }

    void SetAllTrackPositions( float position ) noexcept
    {
        const float clamped = std::clamp( position, 0.0f, 1.0f );
        m_state.presentationPosition = clamped;
        m_state.solverPosition = clamped;
        m_state.position = clamped;
    }

    bool ResetState( bool inspectionCameraActive ) noexcept
    {
        const bool shouldExitInspectionCamera = inspectionCameraActive && !m_state.liveAdvanceHeld;
        const bool restoreWasDown = m_state.restoreWasDown;
        const bool restoreConsumedThisFrame = m_state.restoreConsumedThisFrame;
        const bool liveAdvanceHeld = m_state.liveAdvanceHeld;
        const bool pauseRestoreFlyMode = m_state.pauseRestoreFlyMode;
        const bool pauseRestoreLauncherMode = m_state.pauseRestoreLauncherMode;
        m_state = RunReplayScrubberState{};
        m_state.restoreWasDown = restoreWasDown;
        m_state.restoreConsumedThisFrame = restoreConsumedThisFrame;
        m_state.liveAdvanceHeld = liveAdvanceHeld;
        m_state.pauseRestoreFlyMode = pauseRestoreFlyMode;
        m_state.pauseRestoreLauncherMode = pauseRestoreLauncherMode;
        return shouldExitInspectionCamera;
    }

    ReplayScrubberInputFrame BeginInputFrame( bool leftPressed, bool leftReleased, bool restoreDown ) noexcept
    {
        ReplayScrubberInputFrame frame;
        m_state.restoreConsumedThisFrame = false;
        frame.leftPressed = leftPressed;
        frame.leftReleased = leftReleased;
        frame.restorePressed = restoreDown && !m_state.restoreWasDown;
        m_state.restoreWasDown = restoreDown;
        return frame;
    }

    ReplayScrubberUnavailableResult ResetUnavailableSurface( bool loadedPresentation,
                                                             bool inspectionCameraActive ) noexcept
    {
        ReplayScrubberUnavailableResult result;
        if ( !loadedPresentation )
        {
            result.exitInspectionCamera = ResetState( inspectionCameraActive );
        }
        m_state.fadeUpdatedAt = 0.0;
        m_state.visibleAlpha = 0.0f;
        return result;
    }

    bool SetLiveAdvanceHeld( bool held ) noexcept
    {
        if ( m_state.liveAdvanceHeld == held )
        {
            return false;
        }
        m_state.liveAdvanceHeld = held;
        return true;
    }

    bool LiveAdvanceHeld() const noexcept
    {
        return m_state.liveAdvanceHeld;
    }

    // Publishes explicit visibility policy without giving automation or UI
    // callers mutable access to the scrubber's retained cursor state.
    void SetVisible( bool visible, double now, double holdSeconds ) noexcept
    {
        m_state.visible = visible;
        if ( visible )
        {
            m_state.visibleUntil = now + (std::max)( 0.0, holdSeconds );
        }
    }

  private:
    RunReplayScrubberState m_state;
};

} // namespace Runtime
} // namespace SkullbonezCore

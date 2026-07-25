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
  - Retained cursor state never escapes by mutable reference; consumers receive
    value snapshots and issue owner commands.
  - Restore sample pointers are borrowed for the applying frame only.
  - Prediction cancellation completes before a live restore mutates authority.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"
#include "ReplayTimelinePackets.h"
#include "../Interaction/RuntimeInteractionController.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
// Semantic scrubber actions are owner vocabulary, not screen-layout policy.
// The layout publishes action ids; ReplayScrubber resolves one action from the
// current pointer/frame values before the composition root applies cross-owner
// effects.
enum class ReplayScrubberAction : uint32_t
{
    None,
    RestoreBranch,
    TogglePause,
    ToggleVelocityEdit,
    TogglePrediction,
    SetPredictionHorizon,
    ToggleRagdollVisuals,
    TogglePastPath,
    Save,
    Load,
    Scrub
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

struct ReplayScrubberPointerFrame
{
    ReplayRecorderStats solverStats;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    double now = 0.0;
    int mouseX = 0;
    int mouseY = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    bool leftPressed = false;
    bool leftReleased = false;
    bool restoreDown = false;
    bool hasClientPosition = false;
    bool uiBlocksMouse = false;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool loadedPresentation = false;
    bool pathTargetAvailable = false;
    bool predictionTimelineAvailable = false;
    bool currentPresentationAvailable = false;
    bool currentSolverAvailable = false;
    bool scenePhysicsEnabled = false;
    bool inspectionCameraActive = false;
};

struct ReplayScrubberPointerDecision
{
    ReplayScrubberAction action = ReplayScrubberAction::None;
    RunReplayTrack track = RunReplayTrack::Solver;
    float horizonX = 0.0f;
    float horizonY = 0.0f;
    float horizonWidth = 0.0f;
    float horizonHeight = 0.0f;
    bool surfaceAvailable = false;
    bool cancelToolDrag = false;
    bool exitInspectionCamera = false;
    bool consumesMouse = false;
    bool leftReleased = false;
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

// Lifetime: sample pointers and the path are borrowed only while the scrubber
// builds the frame-local restore command. The command copies durable path data.
struct ReplayScrubberRestoreSources
{
    bool hasLoadedPresentation = false;
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const char* loadedPresentationPath = nullptr;
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
        m_state = RunReplayScrubberState {};
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

    ReplayScrubberUnavailableResult
    ResetUnavailableSurface( bool loadedPresentation, bool inspectionCameraActive ) noexcept
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

    ReplayScrubberPointerDecision ResolvePointerAction( const ReplayScrubberPointerFrame& frame );

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

    void HideSurface() noexcept
    {
        m_state.visible = false;
        m_state.visibleAlpha = 0.0f;
        m_state.fadeUpdatedAt = 0.0;
    }

    void KeepVisible( double now, double holdSeconds ) noexcept
    {
        m_state.visibleUntil = now + (std::max)( 0.0, holdSeconds );
        m_state.visible = true;
    }

    void SetHistoricalSamplePaused( bool paused ) noexcept
    {
        m_state.historicalSamplePaused = paused;
    }

    void SelectTrack( RunReplayTrack track ) noexcept
    {
        m_state.activeTrack = track;
        SyncActiveTrackPosition();
    }

    void SetPointer( int mouseX, int mouseY ) noexcept
    {
        m_state.mouseX = mouseX;
        m_state.mouseY = mouseY;
    }

    void ArmLoadedPresentation( float normalized, double now, double holdSeconds ) noexcept
    {
        // Invariant: a loaded artifact owns the presentation cursor while the
        // live solver cursor stays parked at its present edge.
        SelectTrack( RunReplayTrack::Presentation );
        SetTrackPosition( RunReplayTrack::Presentation, normalized );
        m_state.solverPosition = 1.0f;
        m_state.historicalSamplePaused = true;
        KeepVisible( now, holdSeconds );
    }

    // Concept: cold file/save feedback is copied into the bounded retained UI
    // buffer; no dialog or file-path lifetime crosses the call.
    void PublishFeedback( RunReplayTrack track, const char* message, double now, double durationSeconds ) noexcept
    {
        m_state.saveMessageTrack = track;
        strncpy_s( m_state.saveMessage, sizeof( m_state.saveMessage ), message ? message : "", _TRUNCATE );
        m_state.saveMessageUntil = now + (std::max)( 0.0, durationSeconds );
    }

    // Concept: fade integration belongs to the retained cursor owner because
    // the previous update timestamp is part of scrubber presentation state.
    // Invariant: visibility remains published through the fade-out tail and
    // frame stalls contribute at most 250 ms to one opacity step.
    void UpdateVisibilityFade(
        bool targetVisible,
        double now,
        double fadeInSeconds,
        double fadeOutSeconds,
        float visibleEpsilon
    ) noexcept
    {
        if ( m_state.fadeUpdatedAt <= 0.0 || now < m_state.fadeUpdatedAt )
        {
            m_state.fadeUpdatedAt = now;
        }
        const double deltaSeconds = std::clamp( now - m_state.fadeUpdatedAt, 0.0, 0.25 );
        m_state.fadeUpdatedAt = now;
        const double fadeSeconds = targetVisible ? fadeInSeconds : fadeOutSeconds;
        const float alphaStep = fadeSeconds > 0.0 ? static_cast<float>( deltaSeconds / fadeSeconds ) : 1.0f;
        m_state.visibleAlpha =
            std::clamp( m_state.visibleAlpha + ( targetVisible ? alphaStep : -alphaStep ), 0.0f, 1.0f );
        m_state.visible = targetVisible || m_state.visibleAlpha > visibleEpsilon;
    }

    bool BuildRestoreRequest(
        const ReplayScrubberRestoreSources& sources,
        double now,
        ReplayLiveRestoreRequest& outRequest,
        char* outReason = nullptr,
        std::size_t reasonSize = 0
    );
    void CompleteRestore(
        const ReplayLiveRestoreRequest& request,
        bool restored,
        const RunReplayV2TargetRestoreResult& v2Result,
        const char* reason,
        RunReplayV2TargetRestoreResult* outV2Result = nullptr,
        char* outReason = nullptr,
        std::size_t reasonSize = 0
    );

  private:
    static void WriteRestoreReason( char* outReason, std::size_t reasonSize, const char* reason );
    void PublishRestoreResult( double now, bool restored, RunReplayTrack messageTrack );
    RunReplayScrubberState m_state;
};

} // namespace Runtime
} // namespace SkullbonezCore

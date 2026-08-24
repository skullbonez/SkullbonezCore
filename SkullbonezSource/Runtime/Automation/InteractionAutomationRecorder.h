/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
Purpose:
  Owns the bounded, full-device-frame interaction recording artifact.

Summary:
  F8 arms this owner, App publishes the scene/replay sidecars at the next clean
  frame boundary, and the recorder then retains one complete input value per
  runtime turn. The manifest is the final atomic publication and therefore
  never names a sidecar that was only partially written.

Glossary:
  Recording turn: Monotonic input turn independent of scene-frame resets.
  Pending turn: The just-routed device frame held until the next lifecycle
    boundary proves that it did not trigger a scene replacement.
  Interaction baseline: Detached camera/tool/UI values not owned by scene JSON.

Invariants:
  - InputRouter remains the only retained input owner; App copies its current
    device state into one Automation-owned input sample after routing.
  - F8 control turns are never appended to the tape.
  - Capacity grows only in one-minute chunks through the named Diagnostics
    reserve owner and never beyond the configured 1..60 minute maximum.
  - A published manifest is complete and digest-bound to every sidecar.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
namespace Runtime
{
struct InteractionAutomationInputSample
{
    std::array<uint64_t, 4> keyWords = {};
    int clientX = 0;
    int clientY = 0;
    long rawMouseX = 0;
    long rawMouseY = 0;
    int wheelDelta = 0;
    bool hasClientPosition = false;
    bool appFocused = true;
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};

struct RecordedInputFrame
{
    uint64_t turn = 0u;
    double deltaSeconds = 0.0;
    std::array<uint64_t, 4> keyWords = {};
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    long rawMouseX = 0;
    long rawMouseY = 0;
    int wheelDelta = 0;
    bool hasPointer = false;
    bool appFocused = true;
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
    char semanticAnchor[64] = {};
};

// Value-only state that scene serialization does not own. Integer enum values
// are validated by the production manifest reader before they are applied.
struct InteractionRecordingBaseline
{
    int cameraMode = 0;
    int worldInteractionOwner = 0;
    int activeUiTab = 0;
    int developmentUiSurface = 0;
    bool uiVisible = true;
    bool uiMinimized = false;
    bool editorModeEnabled = false;
    bool editorPlacementModeEnabled = false;
    bool editorPlaceStatic = false;
    bool editorTerrainAlign = false;
    int editorObjectType = 0;
    bool replayActive = false;
    bool replayScrubPaused = false;
    bool replayLiveAdvanceHeld = false;
    bool replayPredictionEnabled = false;
    int replayTrack = 1;
    float replayPresentationTrackPosition = 1.0f;
    float replaySolverTrackPosition = 1.0f;
    int replayCauseInspectionMode = 0;
    int replayCauseSelectedRow = -1;
    int replayCauseActiveTab = 0;
    int replayCauseSelectedDetailContactRow = -1;
    int replayCauseSolverDetailFirstRow = 0;
    int replayCauseRawRecordFirstRow = 0;
    int replayCauseIterationsFirstRow = 0;
    uint64_t replayCauseSourceFrame = 0u;
    uint64_t replayCauseTargetFrame = 0u;
    uint64_t replayCausePresentedFrame = 0u;
    bool replayCauseDetailVisible = false;
    bool replayCauseOwnsPause = false;
    bool replayCauseTransportPending = false;
    bool replayCauseTransportInFlight = false;
    bool replayCauseReturnIssued = false;
    float replayCauseEasedProgress = 0.0f;
    float replayCauseDrawerProgress = 0.0f;
    char editorSelectionName[64] = {};
    char replayPathTargetName[64] = {};
};

enum class InteractionRecordingState : uint8_t
{
    Idle,
    Armed,
    Recording,
    Saving,
    Saved,
    Failed
};

struct InteractionRecordingStatusView
{
    InteractionRecordingState state = InteractionRecordingState::Idle;
    double elapsedSeconds = 0.0;
    int maximumMinutes = 1;
    std::size_t frameCount = 0u;
    std::size_t frameCapacity = 0u;
    const char* stopReason = "";
    const char* failure = "";
};

class InteractionAutomationRecorder
{
  public:
    static constexpr std::size_t FRAMES_PER_MINUTE = 14'400u;
    static constexpr int MAX_RECORDING_MINUTES = 60;

    // Arms an explicit path or a new timestamped TestOutput recording. The
    // caller must publish sidecars before BeginAtBoundary.
    Core::SbResult Arm( Core::SbDiagnosticStore& diagnostics, const char* requestedManifestPath, int maximumMinutes );

    // Captures detached non-scene state at the first clean pre-input boundary.
    Core::SbResult BeginAtBoundary( Core::SbDiagnosticStore& diagnostics, int sourceWidth, int sourceHeight,
                                    uint64_t sceneGeneration, const InteractionRecordingBaseline& baseline,
                                    bool replaySidecarWritten );

    // Commits the previous routed turn only while the same scene generation remains active.
    Core::SbResult AdvanceBoundary( Core::SbDiagnosticStore& diagnostics, uint64_t sceneGeneration );

    // Copies the routed device frame; publication is deferred until AdvanceBoundary.
    void CapturePendingTurn( double deltaSeconds, int sourceWidth, int sourceHeight,
                             const InteractionAutomationInputSample& frame, const char* semanticAnchor = nullptr );

    // Saves the valid prefix and publishes the manifest last. Pending input is
    // included only when the caller has already proved it is not a control turn.
    Core::SbResult StopAndSave( Core::SbDiagnosticStore& diagnostics, const char* reason, bool commitPendingTurn = false );

    // Ends recording with a visible recoverable failure and no published manifest.
    Core::SbResult Abort( Core::SbDiagnosticStore& diagnostics, const char* message );
    void Reset();

    bool IsArmed() const noexcept
    {
        return m_state == InteractionRecordingState::Armed;
    }
    bool IsRecording() const noexcept
    {
        return m_state == InteractionRecordingState::Recording;
    }
    bool IsActive() const noexcept
    {
        return IsArmed() || IsRecording() || m_state == InteractionRecordingState::Saving;
    }
    const char* ManifestPath() const noexcept
    {
        return m_manifestPath.c_str();
    }
    const char* ScenePath() const noexcept
    {
        return m_scenePath.c_str();
    }
    const char* ReplayPath() const noexcept
    {
        return m_replayPath.c_str();
    }
    const std::vector<RecordedInputFrame>& Frames() const noexcept
    {
        return m_frames;
    }
    InteractionRecordingStatusView Status() const noexcept;

  private:
    bool PreparePaths( const char* requestedManifestPath );
    bool EnsureFrameCapacity( Core::SbDiagnosticStore& diagnostics, std::size_t requiredCapacity );
    Core::SbResult CommitPendingTurn( Core::SbDiagnosticStore& diagnostics );
    Core::SbResult SaveManifestAtomically( Core::SbDiagnosticStore& diagnostics );
    void Fail( const char* message );

    InteractionRecordingState m_state = InteractionRecordingState::Idle;
    InteractionRecordingBaseline m_baseline;
    std::vector<RecordedInputFrame> m_frames;
    RecordedInputFrame m_pendingTurn;
    std::string m_manifestPath;
    std::string m_scenePath;
    std::string m_replayPath;
    std::string m_stopReason;
    std::string m_failure;
    uint64_t m_sceneGeneration = 0u;
    double m_elapsedSeconds = 0.0;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    int m_maximumMinutes = 1;
    bool m_hasPendingTurn = false;
    bool m_replaySidecarWritten = false;
};
} // namespace Runtime
} // namespace SkullbonezCore

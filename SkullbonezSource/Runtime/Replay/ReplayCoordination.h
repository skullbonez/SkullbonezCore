/*
File: SkullbonezSource/Runtime/Replay/ReplayCoordination.h
Purpose:
  Defines frame-scoped commands and results exchanged at replay owner boundaries.

Summary:
  Replay owners retain state; the composition root receives short-lived values
  and publishes immutable evidence views. Explicit borrowed references remain
  synchronous and none of these packets is retained.

Glossary:
  Workspace tick: One input-frame pass through replay scrub, authoring, and path tools.
  Timeline reset: Scene-boundary command that restarts retained replay history.
  Startup workflow: Cold command-line replay load or validation operation.
  Published view: Read-only, frame-scoped evidence that exposes no mutation
    path back into a replay owner.

Invariants:
  - Borrowed owner references remain valid only for the consuming call.
  - Values publish cross-owner effects; they do not provide callbacks into Run.
  - Automation and input views contain no mutable replay owner reference.
  - Transport host values are synchronous borrows and retain no Run authority.
  - Scene reset facts contain no mutable scene or physics authority.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayAuthoring.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "ReplayTimeline.h"
#include "../../Core/PlatformWin32.h"
#include "../../Core/Common.h"
#include "ReplayProbeState.h"

#include <span>
#include <type_traits>
#include <variant>

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Physics
{
class PhysicsEngine;
}
namespace UI
{
struct RunSceneUIOverrideState;
}
namespace Runtime
{
class InputRouter;
class RuntimeInteractionController;
class SceneController;
class SceneEntityStore;
class ReplayRestoreTransaction;
struct CameraControlState;
struct RunMousePickupState;
struct SceneSessionState;

namespace ReplayInteractionOperations
{
// Ends replay-owned tool capture without borrowing the replay composition root.
// Gesture and native-capture authority remain with their concrete input owners.
void CancelToolGesture( RuntimeInteractionController& interaction );
void CancelToolDragState( RuntimeInteractionController& interaction, InputRouter& inputRouter );
} // namespace ReplayInteractionOperations

// Value-only facts sampled by the input turn. Mutable domain owners are passed
// explicitly to the synchronous composition operation instead of being hidden
// in this packet.
struct ReplayWorkspaceFrameInput
{
    HWND window = nullptr;
    bool uiBlocksMouse = false;

    // Invariant: GameUI pointer tools must not sample or reset replay state
    // while the mutually exclusive ImGui development surface owns input.
    bool gameUiPointerSurfaceActive = true;
    int wheelDelta = 0;
    ReplayPathPickInput pointerRay;
    RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
    RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
    bool attachedFollow = false;
    bool directorGrabbed = false;
    bool editorModeEnabled = false;
    bool scenePhysicsEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool spaceDown = false;
    int screenWidth = 0;
    int screenHeight = 0;
    float cameraMouseRadiansPerPixel = 0.0f; // Cached config sample; replay never reopens device/config ownership.
    double now = 0.0;
    int requestedCauseRow = -1; // Frame-local typed input; production pointer hit-testing publishes the same value.
};

struct ReplayWorkspaceOutput
{
    ReplayLiveRestoreRequest restoreRequest;
    bool consumesMouse = false;

    // Why: focused cause-filter text must block later runtime key bindings in
    // the same frame without giving Replay retained access to InputRouter.
    bool consumesKeyboard = false;
    bool enterInteractive = false;

    // Zero denotes ordinary replay transport. Planning uses a non-zero token to
    // match a causal restore completion without adding state to ReplayScrubber.
    uint64_t planningTransitionToken = 0;

    // Cold native-file selection remains at TickWorkspace, after the scrubber
    // has completed its pointer and visibility phase.
    bool loadPresentationRequested = false;
};

// Semantic transport actions are independent of the GameUI overlay and the
// ImGui presentation. ReplayRuntime translates these value commands into the
// existing timeline, scrubber, prediction, authoring, and cold-I/O owners.
enum class ReplayTransportAction : uint8_t
{
    SetRecordingEnabled,
    JumpToStart,
    JumpToEnd,
    TogglePlayPause,
    StepBackward,
    StepForward,
    SetRevealSpeed,
    Scrub,
    TogglePrediction,
    SetPredictionDetailMode,
    SetPredictionHorizon,
    RestoreBranch,
    Save,
    Load,
    ReturnToLive,
    SelectCauseRow
};

struct ReplaySetRecordingEnabledCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::SetRecordingEnabled;
    bool enabled = false;
};

struct ReplayJumpToStartCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::JumpToStart;
};
struct ReplayJumpToEndCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::JumpToEnd;
};
struct ReplayTogglePlayPauseCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::TogglePlayPause;
};
struct ReplayStepBackwardCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::StepBackward;
};
struct ReplayStepForwardCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::StepForward;
};
struct ReplaySetRevealSpeedCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::SetRevealSpeed;
    float rate = 0.0f;
};
struct ReplayScrubCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::Scrub;
    float normalized = 0.0f;
};
struct ReplayTogglePredictionCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::TogglePrediction;
};
struct ReplaySetPredictionDetailModeCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::SetPredictionDetailMode;
    bool highDetail = false;
};
struct ReplaySetPredictionHorizonCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::SetPredictionHorizon;
    float seconds = 0.0f;
};
struct ReplayRestoreBranchCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::RestoreBranch;
};
struct ReplaySaveCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::Save;
};
struct ReplayLoadCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::Load;
};
struct ReplayReturnToLiveCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::ReturnToLive;
};
struct ReplaySelectCauseRowCommand
{
    static constexpr ReplayTransportAction action = ReplayTransportAction::SelectCauseRow;
    int rowIndex = -1;
};

// Invariant: each transport alternative carries only the payload accepted by
// that action. The dispatcher cannot observe a stale scalar, row, or toggle
// value left behind by a different UI command.
using ReplayTransportCommand = std::variant<ReplaySetRecordingEnabledCommand, ReplayJumpToStartCommand,
                                            ReplayJumpToEndCommand, ReplayTogglePlayPauseCommand, ReplayStepBackwardCommand,
                                            ReplayStepForwardCommand, ReplaySetRevealSpeedCommand, ReplayScrubCommand,
                                            ReplayTogglePredictionCommand, ReplaySetPredictionDetailModeCommand,
                                            ReplaySetPredictionHorizonCommand, ReplayRestoreBranchCommand, ReplaySaveCommand,
                                            ReplayLoadCommand, ReplayReturnToLiveCommand, ReplaySelectCauseRowCommand>;

inline ReplayTransportAction ReplayTransportCommandAction( const ReplayTransportCommand& command ) noexcept
{
    return std::visit(
        []( const auto& value ) noexcept
        {
            using Command = std::remove_cvref_t<decltype( value )>;
            return Command::action;
        },
        command );
}

struct ReplayTransportLoadResult
{
    bool activateLoadedPresentation = false;
};

// Concept: generic input routing consumes one immutable replay publication.
// It contains only decisions already owned by replay; no recorder, prediction,
// presentation, or authoring storage is reachable through this value.
struct ReplayInputView
{
    bool activeInteraction = false;
    bool inspectionActive = false;
    bool inspectionCameraActive = false;
    bool restoreConsumedThisFrame = false;
    bool scrubPaused = false;
    bool liveAdvanceHeld = false;
    bool velocityEditEnabled = false;
    bool predictionEnabled = false;
    bool captureEnabled = false;
    bool hasPathTarget = false;
    bool hasCameraFocus = false;
    RunCameraMode restoreCameraMode = RunCameraMode::Demo;
    int pathTargetModelRow = -1;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    float presentationTrackPosition = 1.0f;
    float solverTrackPosition = 1.0f;
    float solverPresentTrackPosition = 1.0f;
    float predictionRevealProgress = 0.0f;
    bool predictionRevealAvailable = false;
};

// Value command emitted when generic input leaves replay ownership. Replay
// decides whether durable replay state needs clearing; camera/input owners are
// explicit synchronous operands on the consuming operation.
struct ReplayInteractionExitInput
{
    bool leavingReplayWorkspace = false;
    bool previousOwnerWasReplay = false;
    RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
    bool attachedFollow = false;
    bool directorGrabbed = false;
};

struct ReplayLiveRestoreOutcome
{
    bool requested = false;
    bool restored = false;
    bool enterInteractive = false;
    RunReplayV2TargetRestoreResult v2Result;
    char reason[160] = {};
};

struct ReplayStartupLoadRequest
{
    const char* path = nullptr;
    bool validationProbe = false;
};

#ifdef _DEBUG
struct ReplayStartupRestoreFileProbeRequests
{
    const char* checkpointPath = nullptr;
    const char* targetPath = nullptr;
    const char* branchPath = nullptr;
    const char* failurePath = nullptr;
};

struct ReplayStartupNormalizedProbeRequest
{
    bool enabled = false;
    float normalized = 0.25f;
};

struct ReplayStartupSaveProbeRequest
{
    bool enabled = false;
    const char* path = nullptr;
};
#endif

struct ReplayStartupRequest
{
    ReplayStartupLoadRequest load;
#ifdef _DEBUG
    ReplayStartupRestoreFileProbeRequests restoreFiles;
    ReplayStartupNormalizedProbeRequest scrub;
    ReplayStartupNormalizedProbeRequest restore;
    ReplayStartupSaveProbeRequest save;
#endif
};

// Detached application work emitted after Replay has accepted a startup
// artifact decision. App supplies the concrete camera/input owners only while
// applying the command; Replay never stores a host capability.
enum class ReplayStartupApplicationAction : uint8_t
{
    None,
    ActivateLoadedPresentation
};

struct ReplayStartupResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    ReplayStartupApplicationAction applicationAction = ReplayStartupApplicationAction::None;
    double applicationTimeSeconds = 0.0;
    float presentationTrackPosition = 0.25f;
    bool skipExecute = false;
};

// Value-only terminal publication sampled after replay flushes its cold hash
// logs. Run may print these facts but cannot reopen timeline owner state.
struct ReplayShutdownReport
{
    ReplayRecorderStats presentation;
    ReplayRecorderStats solver;
};

// Cold startup configures timeline capacity and resets the retained cursor as
// one replay operation. The camera reaction is returned as a value so Run does
// not receive a raw scrubber mutation API.
struct ReplayRecordingActivationResult
{
    ReplayRecordingConfigResult configuration;
    bool exitInspectionCamera = false;
};

struct ReplaySceneTimelineResetInput
{
    const char* sceneLabel = nullptr;
    bool preserveBranchMetadata = false;
    bool preserveReplayInspection = false;
    bool preserveReplaySourceTimeline = false; // Intermediate causal restore retains later exact-frame targets.
    bool isSceneMode = false;
    int modelCount = 0;
    int solverBallCount = 0;
    int solverBoxCount = 0;
    uint32_t rngSeed = 0;
    int sceneObjectCapacity = 0;
    uint32_t generatedObjectTypeOverride = 0;
    bool hasUiModelCountOverride = false;
    bool hasUiSolverCountOverride = false;
};

namespace ReplayTimelineOperations
{
inline bool SceneTimelineResetClearsBranch( const ReplaySceneTimelineResetInput& input ) noexcept
{
    return !input.preserveBranchMetadata;
}

inline bool SceneTimelineRecordsGeneratedConfig( const ReplaySceneTimelineResetInput& input ) noexcept
{
    return !( input.isSceneMode && input.solverBallCount <= 0 && input.solverBoxCount <= 0 );
}

inline uint32_t SceneTimelineGeneratedConfigFlags( const ReplaySceneTimelineResetInput& input ) noexcept
{
    uint32_t flags = 0;
    flags |= ( input.solverBallCount > 0 || input.solverBoxCount > 0 ) ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS : 0u;
    flags |= input.hasUiModelCountOverride ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
    flags |= input.hasUiSolverCountOverride ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS : 0u;
    flags |= ( input.generatedObjectTypeOverride << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
             REPLAY_GENERATED_SCENE_OVERRIDE_MASK;
    return flags;
}

ReplaySceneTimelineResetInput DescribeReplaySceneTimeline( const SceneController& sceneController,
                                                           const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                           const SceneSessionState& scene, int sceneObjectCapacity,
                                                           uint32_t generatedObjectTypeOverride );
} // namespace ReplayTimelineOperations

struct ReplaySceneTimelineResetResult
{
    bool exitInspectionCamera = false;
    bool timelineStarted = false;
};

struct ReplayKeyboardVelocityEditInput
{
    bool altDown = false;
    bool toggleAllowed = true; // False records the key edge while the editor owns Alt.
    bool velocityEditOwnsWorld = false;
    double now = 0.0;
};

enum class ReplayKeyboardVelocityEditCameraAction
{
    None,
    EnterInspection,
    ExitInspection
};

struct ReplayKeyboardVelocityEditResult
{
    bool cancelToolDrag = false;
    bool enterInteractive = false;
    ReplayKeyboardVelocityEditCameraAction cameraAction = ReplayKeyboardVelocityEditCameraAction::None;
    bool setWorldOwner = false;
    ReplayWorldOwnerRequest worldOwner = ReplayWorldOwnerRequest::None;
};

#ifdef _DEBUG
struct ReplayProbeTickResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool enterInteractive = false;

    // Value boundary: Run owns lifecycle submission after the probe succeeds.
    bool resetCurrentScene = false;
};
#endif
} // namespace Runtime
} // namespace SkullbonezCore

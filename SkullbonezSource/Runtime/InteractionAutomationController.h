/*
File: SkullbonezSource/Runtime/InteractionAutomationController.h
Purpose:
  Defines the CLI interaction automation script and report state.

Mental model:
  Interaction automation is a validation harness, not gameplay state. Scripts
  describe frame-indexed input/runtime commands, while reports capture the
  bounded evidence that validation scripts read after the run exits.

Glossary:
  Automation action: One scheduled command from an interaction script.
  Assertion report: Machine-readable proof row for one expected runtime state.
  Input override: Scripted mouse/key snapshot forwarded through the normal
    runtime input bridge for a bounded frame window.

Invariants:
  - Automation state is active only for CLI validation launches.
  - Report vectors are validation artifacts, not hot-path gameplay storage.
  - Input fields describe the current injected frame and must be cleared through
    the automation owner after actions complete or fail.

Related:
  - SkullbonezSource/Runtime/InteractionAutomationController.cpp
  - SkullbonezSource/Runtime/Input.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "DemoDirector.h"
#include "RuntimeCameraMode.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
}
namespace UI
{
class InGameUI;
}
namespace Basics
{
class AttachedCameraController;
class CaptureController;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
class Window;
class EngineConfig;
struct RunCameraState;
struct RunTimerState;
enum class RunInteractionAutomationActionType
{
    LoadShotList,
    DirectorPlay,
    DirectorAdvance,
    DirectorGrab,
    DirectorRelease,
    SetPhaseStyle,
    SetCameraPose,
    SetCameraMode,
    LoseFocus,
    MoveMouse,
    ClickObject,
    ClickReplayControl,
    ScrubReplaySolverTrack,
    SetReplayPredictionEnabled,
    SetReplayPredictionHorizonSeconds,
    SetReplayPathTarget,
    NudgeReplayPathTargetVelocity,
    ShowReplayScrubber,
    PressKey,
    AssertState,
    Screenshot
};

enum class RunInteractionAutomationButton
{
    Left,
    Right
};

enum class RunInteractionAutomationAssertKind
{
    SelectedObject,
    Owner,
    CameraMode,
    DirectorGrabbed,
    DirectorPhaseIndex,
    DirectorPhaseName,
    DirectorPhaseStylePath,
    ReplayPredictionEnabled,
    ReplayPathTarget,
    ReplayPastTrajectoryFullRebuildCountMax,
    ReplayPastTrajectoryIncrementalTrimCountMin,
    ReplayPastTrajectoryPublishedPointCountMin,
    PredictionPathVisible,
    PredictionFullHorizonComplete,
    PredictionBuildMode,
    PredictionSupersededRestartCountMin,
    PredictionBaselineVisible,
    PredictionDivergenceMin,
    ReplaySolverTrackAtPresent,
    PredictionScrubFrameActive,
    PredictionTargetDisplacementMin,
    LiveSolverHashStableAcrossPrediction,
    PredictionTrajectoryFingerprintReady,
    GizmoVisible,
    MousePickupActive,
    PointerCapture,
    NativeCaptureRequested,
    CursorVisibleRequested,
    UiBlocksMouse,
    LauncherRayActive,
    ReplayActiveTrack,
    ReplayHistoricalSamplePaused,
    MemoryOverlayEnabled
};

struct RunInteractionAutomationAction
{
    int frame = 0;
    RunInteractionAutomationActionType type = RunInteractionAutomationActionType::AssertState;
    RunInteractionAutomationButton button = RunInteractionAutomationButton::Left;
    RunInteractionAutomationAssertKind assertKind = RunInteractionAutomationAssertKind::SelectedObject;
    RunCameraMode cameraMode = RunCameraMode::Inspect;
    DemoCameraPose cameraPose;
    int keyVirtualKey = 0;
    int holdFrames = 1;
    bool boolValue = false;
    float numberValue = 0.0f;
    Math::Vector::Vector3 vectorValue = Math::Vector::ZERO_VECTOR; // Generic vector payload for replay proof actions.
    char text[128] = {};
    char path[260] = {};
    POINT mouse = {};
    bool hasMouse = false;
    bool processed = false;
};

struct RunInteractionAutomationReportAction
{
    int frame = 0;
    char type[64] = {};
    char target[128] = {};
    POINT mouse = {};
    bool hasMouse = false;
    bool consumed = false;
    char detail[256] = {};
};

struct RunInteractionAutomationReportAssertion
{
    int frame = 0;
    char name[64] = {};
    char expected[128] = {};
    char actual[128] = {};
    bool passed = false;
};

struct InteractionAutomationController
{
    bool enabled = false;
    bool scriptLoaded = false;
    bool failed = false;
    bool finished = false;
    bool reportWritten = false;
    char scriptPath[260] = {};
    char reportPath[260] = {};
    char failure[512] = {};
    std::vector<RunInteractionAutomationAction> actions;
    std::vector<RunInteractionAutomationReportAction> actionReports;
    std::vector<RunInteractionAutomationReportAssertion> assertionReports;
    std::vector<std::string> screenshots;
    POINT mouseClientPosition = {};
    bool hasMouseClientPosition = false;
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    int keyVirtualKey = 0;
    bool keyDown = false;
    int releaseLeftFrame = -1;
    int releaseRightFrame = -1;
    int releaseKeyFrame = -1;
    int unfocusedInputFrames = 0;
};

struct InteractionAutomationFrameResult
{
    bool requestQuit = false;
    SbResult status = SbResult::Success();
};

SbResult ConfigureInteractionAutomation( InteractionAutomationController& state,
                                         const char* scriptPath,
                                         const char* reportPath );
SbResult InteractionAutomationResult( const InteractionAutomationController& state );
void ClearInteractionAutomationInput( InteractionAutomationController& state );
InteractionAutomationFrameResult TickInteractionAutomationBeforeInput( InteractionAutomationController& state,
                                                                       Window* window,
                                                                       const EngineConfig& config,
                                                                       SceneController& scene,
                                                                       RunTimerState& timers,
                                                                       ReplayRuntime& replayRuntime,
                                                                       RunCameraState& camera,
                                                                       InputRouter& inputRouter,
                                                                       RuntimeInteractionController& interaction,
                                                                       RuntimeTools& runtimeTools,
                                                                       AttachedCameraController& attachedCamera,
                                                                       UI::InGameUI& ui );
InteractionAutomationFrameResult
TickInteractionAutomationAfterRender( InteractionAutomationController& state,
                                      SceneController& scene,
                                      RuntimeTools& runtimeTools,
                                      ReplayRuntime& replayRuntime,
                                      RuntimeInteractionController& interaction,
                                      InputRouter& inputRouter,
                                      RunCameraState& camera,
                                      UI::InGameUI& ui,
                                      CaptureController& capture,
                                      Rendering::IRenderCaptureBackend& captureBackend );
SbResult WriteInteractionAutomationReport( InteractionAutomationController& state,
                                           const SceneController& scene,
                                           const RuntimeTools& runtimeTools,
                                           const ReplayRuntime& replayRuntime,
                                           const RuntimeInteractionController& interaction,
                                           const RunCameraState& camera,
                                           const UI::InGameUI& ui );
} // namespace Basics
} // namespace SkullbonezCore

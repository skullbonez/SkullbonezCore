/*
File: SkullbonezSource/Runtime/InteractionAutomationController.h
Purpose:
  Defines the CLI interaction automation action sequencer.

Summary:
  Interaction automation is a validation harness, not gameplay state. Scripts
  describe frame-indexed input/runtime commands. Concrete input and report
  owners hold synthetic device state and bounded evidence outside the sequencer.

Glossary:
  Automation action: One scheduled command from an interaction script.
  Assertion report: Machine-readable proof row for one expected runtime state.
  Input override: Scripted mouse/key snapshot forwarded through the normal
    runtime input bridge for a bounded frame window.
  Development UI command: Fixed presentation or native-window request emitted
    by the sequencer and applied synchronously by this automation owner.

Invariants:
  - Automation state is active only for CLI validation launches.
  - Report vectors are owned by `InteractionAutomationReportWriter`, not by the
    action sequencer or hot-path gameplay storage.
  - Synthetic input is cleared through `InteractionAutomationInputDriver` after
    actions complete or fail.
  - Development UI commands are fixed-capacity and select at most one surface.
  - Editor and window owners are borrowed only while one command batch is
    applied; automation stores neither owner after the synchronous call.
  - Process-wide development-surface selection remains a typed request for Run.

Related:
  - SkullbonezSource/Runtime/InteractionAutomationController.cpp
  - SkullbonezSource/Runtime/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/InteractionAutomationReportWriter.h
  - SkullbonezSource/Runtime/Input.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/PlatformWin32.h"

#include "RuntimeFrameViews.h"
#include "InteractionAutomationInputDriver.h"
#include "InteractionAutomationReportWriter.h"
#include "RunLaunchOptions.h"

#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "DemoDirector.h"
#include "RuntimeCameraMode.h"
#include "Replay/ReplayCoordination.h"
#include "Replay/ReplayVisualPacketFingerprint.h"
#include "../UI/OperatorEditorExchange.h"

#include <string>
#include <array>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core
namespace Rendering
{
class Dx12BackbufferCapture;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
namespace DevelopmentTools
{
class ImGuiEditorOwner;
}
class AttachedCameraController;
class CaptureController;
class InputRouter;
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
class Window;
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
    ClickPoint,
    ClickReplayControl,
    ScrubReplaySolverTrack,
    ScrubEditorReplayTrack,
    SetReplayPredictionEnabled,
    SetReplayPredictionHorizonSeconds,
    BeginReplayVisualFidelityCapture,
    SetReplayPathTarget,
    NudgeReplayPathTargetVelocity,
    ShowReplayScrubber,
    PressKey,
    CaptureEditorSelectionState,
    LoadScene,
    SetDevelopmentUiSurface,
    SetImGuiPanelVisible,
    ResetImGuiLayout,
    FocusImGuiPanel,
    SetImGuiDpiScale,
    ResizeWindow,
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
    MemoryOverlayEnabled,
    EditorUndoDepth,
    EditorRedoDepth,
    EditorSelectionExists,
    EditorSelectionHasTerrain,
    EditorSelectionMatchesCapture,
    DevelopmentUiSurface,
    ImGuiVisible,
    LegacyReplayPresentationActive,
    ImGuiPanelMask,
    ImGuiLayoutResetCountMin,
    ImGuiFocusCountMin,
    ImGuiDpiScale,
    ImGuiDescriptorHighWaterMax,
    ImGuiViewportRecreationsMin,
    ImGuiPreferencesRecovered
};

enum class InteractionAutomationDevelopmentUiCommandType : uint8_t
{
    SelectSurface,
    SetPanelVisible,
    ResetLayout,
    FocusPanel,
    SetDpiScale,
    ResizeWindow
};

struct InteractionAutomationDevelopmentUiCommand
{
    InteractionAutomationDevelopmentUiCommandType type = InteractionAutomationDevelopmentUiCommandType::ResetLayout;
    char target[64] = {};
    bool boolValue = false;
    float numberValue = 0.0f;
    int width = 0;
    int height = 0;
};

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
struct InteractionAutomationDevelopmentUiApplyResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool selectSurface = false;
    DevelopmentUiMode surface = DevelopmentUiMode::Legacy;
};
#endif

// Immutable after-render evidence used by automation assertions. It exposes
// resource counters and visibility facts, never mutable editor or renderer state.
struct InteractionAutomationDevelopmentUiView
{
    bool available = false;
    bool selectedImGui = false;
    bool legacyVisible = false;
    bool imguiVisible = false;
    // Exact authority consumed by the completed late replay-render pass. This
    // may differ from next-frame selection during an ImGui-to-Legacy swap.
    bool legacyReplayPresentationActive = false;
    uint32_t panelVisibilityMask = 0u;
    uint32_t layoutResetCount = 0u;
    uint32_t automationFocusCount = 0u;
    float appliedDpiScale = 0.0f;
    uint32_t rendererDescriptorHighWater = 0u;
    uint32_t gameViewportRecreations = 0u;
    bool preferencesRecovered = false;
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

struct InteractionAutomationFrameResult;

struct InteractionAutomationController
{
    bool enabled = false;
    bool scriptLoaded = false;
    bool finished = false;
    char scriptPath[260] = {};
    std::vector<RunInteractionAutomationAction> actions;
    InteractionAutomationRunStatus status;
    InteractionAutomationInputDriver inputDriver;
    InteractionAutomationReportWriter reportWriter;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Applies the bounded editor/window commands for one automation turn. Run
    // retains only the returned process-surface selection and failure boundary.
    InteractionAutomationDevelopmentUiApplyResult
    ApplyDevelopmentUiCommands( const InteractionAutomationFrameResult& frame,
                                Window& window,
                                DevelopmentTools::ImGuiEditorOwner& editor ) const;
#endif
};

struct InteractionAutomationFrameResult
{
    static constexpr std::size_t DEVELOPMENT_UI_COMMAND_CAPACITY = 8u;

    bool requestQuit = false;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    // Value-only replay mutations are applied once by the frame composition
    // boundary after automation has finished producing its synthetic input.
    ReplayFrameIntent replayIntent;
    // Value-only editor automation joins the same bounded command arbitration
    // used by a real ImGui widget; the sequencer never reaches into replay state.
    bool hasOperatorEditorReplayCommand = false;
    UI::OperatorEditorReplayCommand operatorEditorReplayCommand;
    bool applyCameraMode = false;
    RunCameraMode cameraMode = RunCameraMode::Demo;
    bool setWorldInteractionOwner = false;
    WorldInteractionOwner worldInteractionOwner = WorldInteractionOwner::None;
    InteractionExitReason worldInteractionReason = InteractionExitReason::EnterReplay;
    std::array<InteractionAutomationDevelopmentUiCommand, DEVELOPMENT_UI_COMMAND_CAPACITY> developmentUiCommands = {};
    std::size_t developmentUiCommandCount = 0u;
};

SkullbonezCore::Core::SbResult ConfigureInteractionAutomation( InteractionAutomationController& state,
                                                               const char* scriptPath,
                                                               const char* reportPath );
SkullbonezCore::Core::SbResult InteractionAutomationResult( const InteractionAutomationController& state );
void ClearInteractionAutomationInput( InteractionAutomationController& state );
InteractionAutomationFrameResult TickInteractionAutomationBeforeInput( InteractionAutomationController& state,
                                                                       RuntimeFrameHostView& host,
                                                                       RuntimeFrameInteractionView& interactionOwners,
                                                                       RuntimeFrameSceneView& sceneOwners,
                                                                       const ReplayAutomationView& replayView );
InteractionAutomationFrameResult
TickInteractionAutomationAfterRender( InteractionAutomationController& state,
                                      RuntimeFrameInteractionView& interactionOwners,
                                      RuntimeFrameSceneView& sceneOwners,
                                      const ReplayAutomationView& replayView,
                                      const InteractionAutomationDevelopmentUiView& developmentUiView,
                                      CaptureController& capture,
                                      Rendering::Dx12BackbufferCapture& backbufferCapture );
bool InteractionAutomationWillCaptureAfterRender( const InteractionAutomationController& state, int frame );
} // namespace Runtime
} // namespace SkullbonezCore

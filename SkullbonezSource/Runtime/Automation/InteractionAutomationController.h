/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
Purpose:
  Defines legacy interaction-script sequencing and recorded-manifest playback.

Summary:
  Legacy scripts remain scene-frame-indexed command sequences. Recorded
  manifests instead restore detached owner baselines and publish one complete
  input frame per private recording turn using captured timing. Concrete runtime
  owners still apply all requests; the controller retains progress and bounded
  report evidence, never gameplay authority.

Glossary:
  Automation action: One scheduled command from an interaction script.
  Assertion report: Machine-readable proof row for one expected runtime state.
  Velocity preview assertion: Held/released-drag proof over the Prediction-owned
    target and delta-v value, without advancing simulation.
  Input override: Scripted mouse/key snapshot forwarded through the normal
    runtime input bridge for a bounded frame window.
  Forecast presentation assertion: Post-render proof over detached rolling-ring
    metrics such as coherent heads, chronological ribbons, and wrap progress.

Invariants:
  - Automation state is active only for CLI validation launches.
  - Recorded playback timing and completion use the recording-turn clock, not
    scene-frame numbers or wall time.
  - A recorded manifest and every referenced sidecar pass structural, safe-path,
    and SHA-256 validation before its first input turn is published.
  - Report vectors are owned by `InteractionAutomationReportWriter`, not by the
    action sequencer or hot-path gameplay storage.
  - Synthetic input is cleared through `InteractionAutomationInputDriver` after
    actions complete or fail.
  - Development UI commands are fixed-capacity and select at most one surface.
  - Replay intercept assertions consume a copied value snapshot and cannot
    retarget or advance the retained closest-approach scan.
  - Prediction cause-row assertions count only typed rows already published by
    the normal cause-tree owner; they do not reconstruct solver evidence.
  - Continuous forecast actions cross the normal typed command queue; the
    sequencer stores no Planning owner reference or worker-ring span.
  - Editor and window owners are borrowed only while one command batch is
    applied; automation stores neither owner after the synchronous call.
  - Process-wide development-surface selection remains a typed request for Run.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h
  - SkullbonezSource/Runtime/Input/Input.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "../RuntimeFrameViews.h"
#include "../App/ReplayRuntimePackets.h"
#include "InteractionAutomationInputDriver.h"
#include "InteractionAutomationRecorder.h"
#include "InteractionAutomationReportWriter.h"
#include "../App/RunLaunchOptions.h"

#include "../../Core/Common.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"
#include "../Direction/DemoDirector.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Replay/ReplayCoordination.h"
#include "../../UI/OperatorEditorExchange.h"

#include <string>
#include <string_view>
#include <array>
#include <fstream>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class SbDiagnosticStore;
} // namespace Core
namespace Rendering
{
class Dx12BackbufferCapture;
struct RenderSceneSnapshot;
} // namespace Rendering
namespace UI
{
class InGameUI;
}
namespace Runtime
{
namespace DevelopmentTools
{
class ImGuiEditorOwner;
struct ImGuiEditorStatus;
} // namespace DevelopmentTools
class AttachedCameraController;
class CaptureController;
struct ContinuousOrbitalForecastView;
class InputRouter;
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
class Window;
struct CameraControlState;
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
    ScrollPoint,
    ClickObject,
    ClickPoint,
    ClickReplayControl,
    ScrubReplaySolverTrack,
    SelectReplayCauseRow,
    ScrubEditorReplayTrack,
    SetContinuousForecastCommand,
    SetReplayPredictionEnabled,
    SetReplayPredictionHorizonSeconds,
    BeginReplayVisualFidelityCapture,
    SetReplayPathTarget,
    SetReplayInterceptTarget,
    SetReplayTripPlannerCommand,
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
    ReplayInterceptContact,
    ReplayInterceptMissMax,
    ReplayInterceptEtaMin,
    ReplayInterceptEtaMax,
    ReplayTripPlannerState,
    ReplayTripPlannerIterationMax,
    ReplayTripPlannerMissMax,
    ReplayTripPlannerMissesImprove,
    ReplayPorkchopComplete,
    ReplayPorkchopMinimumDeltaVMax,
    ReplayPorkchopMinimumDepartureDelayMax,
    ReplayPorkchopMinimumTimeOfFlightMin,
    ReplayPorkchopMinimumTimeOfFlightMax,
    ReplayPorkchopRefreshMillisecondsMax,
    ReplayPorkchopMaximumFrameMillisecondsMax,
    ReplayPorkchopSweepAgeSecondsMax,
    ReplayPorkchopSelected,
    ReplayTripPlannerTimeOfFlightMin,
    ReplayTripPlannerTimeOfFlightMax,
    ReplayPastTrajectoryFullRebuildCountMax,
    ReplayPastTrajectoryIncrementalTrimCountMin,
    ReplayPastTrajectoryPublishedPointCountMin,
    PredictionPathVisible,
    PredictionVelocityPreviewActive,
    PredictionVelocityPreviewAwaitingReplacement,
    PredictionVelocityPreviewDeltaMin,
    PredictionPresentedGenerationMin,
    PredictionPresentedRootVelocityDeltaMin,
    PredictionFullHorizonComplete,
    PredictionBuildMode,
    PredictionSupersededRestartCountMin,
    PredictionSupersededRestartCountMax,
    PredictionBaselineVisible,
    PredictionDivergenceMin,
    ReplaySolverTrackAtPresent,
    PredictionScrubFrameActive,
    PredictionTargetDisplacementMin,
    PredictionTargetLastNear,
    LiveSolverHashStableAcrossPrediction,
    PredictionEvidenceConsumerBalanced,
    PredictionEvidencePipelineRowsMin,
    PredictionEvidenceCurrentCapacityMax,
    PredictionDetailMode,
    PredictionCauseDetailVisible,
    PredictionCauseWindowAvailable,
    PredictionEvidenceCapacityReleased,
    PredictionEvidenceMemoryReconciled,
    PredictionCauseManifoldRowsMin,
    PredictionCauseManifoldRowsMax,
    PredictionCauseSolverRowsMin,
    PredictionCauseSolverRowsMax,
    PredictionCauseSyntheticRowsMin,
    PredictionCauseSyntheticRowsMax,
    PredictionTrajectoryFingerprintReady,
    PredictionAppearanceInvalidationCountMin,
    ContinuousForecastActive,
    ContinuousForecastPreWrap,
    ContinuousForecastWindowWrapped,
    ContinuousForecastPresentationCoherent,
    ContinuousForecastAbsoluteTickMin,
    ContinuousForecastOldestTickMin,
    ContinuousForecastRibbonSegmentsMin,
    ContinuousForecastHeadMarkerCount,
    ShadowPassExecuted,
    TerrainShadowValid,
    ObjectShadowValid,
    ReflectionPassExecuted,
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
    GameUiReplayPresentationActive,
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
    DevelopmentUiMode surface = DevelopmentUiMode::GameUI;
};
#endif

// Immutable after-render evidence used by automation assertions. It exposes
// resource counters and visibility facts, never mutable editor or renderer state.
struct InteractionAutomationDevelopmentUiView
{
    bool available = false;
    bool selectedImGui = false;
    bool gameUiVisible = false;
    bool imguiVisible = false;

    // Exact authority consumed by the completed late replay-render pass. This
    // may differ from next-frame selection during an ImGui-to-GameUI swap.
    bool gameUiReplayPresentationActive = false;
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
    int integerValue = 0;
    int holdFrames = 1;
    bool boolValue = false;
    float numberValue = 0.0f;
    ReplayTripPlannerCommandKind tripPlannerCommand = ReplayTripPlannerCommandKind::None;
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
    explicit InteractionAutomationController( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
        : resultDiagnostics( resultDiagnostics ), reportWriter( resultDiagnostics )
    {
    }

    // Lifetime: Run owns this store for the controller's complete process
    // lifetime. Automation uses it only for recoverable error publication and child-owner
    // construction; it never replaces the App-owned diagnostic authority.
    SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics;
    bool enabled = false;
    bool scriptLoaded = false;
    bool finished = false;
    bool recordedManifest = false;
    bool recordedBaselineApplied = false;
    bool recordedFramePublished = false;
    uint64_t recordedTurn = 0u;
    uint64_t traceTurn = 0u;
    double recordedDeltaSeconds = 0.0;
    char scriptPath[260] = {};
    char tracePath[260] = {};
    std::ofstream traceOutput;
    std::vector<RunInteractionAutomationAction> actions;
    std::vector<RecordedInputFrame> recordedFrames;
    InteractionRecordingBaseline recordedBaseline;
    InteractionAutomationRunStatus status;
    InteractionAutomationInputDriver inputDriver;
    InteractionAutomationReportWriter reportWriter;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Applies the bounded editor/window commands for one automation turn. Run
    // retains only the returned process-surface selection and failure boundary.
    InteractionAutomationDevelopmentUiApplyResult
    ApplyDevelopmentUiCommands( const InteractionAutomationFrameResult& frame, Window& window,
                                DevelopmentTools::ImGuiEditorOwner& editor ) const;

    // Interprets the automation-owned replay command and submits it through
    // the same bounded queue used by real editor widgets.
    SkullbonezCore::Core::SbResult SubmitOperatorEditorReplayCommand( const InteractionAutomationFrameResult& frame,
                                                                      UI::OperatorEditorCommandQueues& commands ) const;
    SkullbonezCore::Core::SbResult SubmitOperatorEditorForecastCommand( const InteractionAutomationFrameResult& frame,
                                                                        UI::OperatorEditorCommandQueues& commands ) const;

    // Projects copied editor facts into the exact after-render assertion view;
    // no editor owner or mutable renderer state crosses this value boundary.
    InteractionAutomationDevelopmentUiView BuildDevelopmentUiView( const DevelopmentTools::ImGuiEditorStatus& editor,
                                                                   bool gameUiVisible,
                                                                   bool gameUiReplayPresentationActive ) const;
#endif
};

struct InteractionAutomationFrameResult
{
    static constexpr std::size_t DEVELOPMENT_UI_COMMAND_CAPACITY = 8u;

    bool requestQuit = false;
    bool hasRecordedDeltaSeconds = false;
    double recordedDeltaSeconds = 0.0;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();

    // Value-only replay mutations are applied once by the frame composition
    // boundary after automation has finished producing its synthetic input.
    ReplayFrameIntent replayIntent;
    int requestedReplayCauseRow = -1;

    // Value-only editor automation joins the same bounded command arbitration
    // used by a real ImGui widget; the sequencer never reaches into replay state.
    bool hasOperatorEditorReplayCommand = false;
    UI::OperatorEditorReplayCommand operatorEditorReplayCommand;
    bool hasOperatorEditorForecastCommand = false;
    UI::OperatorEditorForecastCommand operatorEditorForecastCommand;
    bool applyCameraMode = false;
    RunCameraMode cameraMode = RunCameraMode::Demo;
    bool setWorldInteractionOwner = false;
    WorldInteractionOwner worldInteractionOwner = WorldInteractionOwner::None;
    InteractionExitReason worldInteractionReason = InteractionExitReason::EnterReplay;
    bool restoreRecordedReplayBaseline = false;
    bool recordedReplayScrubPaused = false;
    bool recordedReplayLiveAdvanceHeld = false;
    RunReplayTrack recordedReplayTrack = RunReplayTrack::Solver;
    float recordedReplayPresentationTrackPosition = 1.0f;
    float recordedReplaySolverTrackPosition = 1.0f;
    bool restoreRecordedReplayCauseBaseline = false;
    InteractionRecordingBaseline recordedReplayCauseBaseline;
    std::array<InteractionAutomationDevelopmentUiCommand, DEVELOPMENT_UI_COMMAND_CAPACITY> developmentUiCommands = {};
    std::size_t developmentUiCommandCount = 0u;
};

SkullbonezCore::Core::SbResult ConfigureInteractionAutomation( InteractionAutomationController& state,
                                                               const char* scriptPath, const char* reportPath,
                                                               const char* tracePath );

// Converts script function-key labels to the Win32 value held by the input
// driver. Keeping this value-only mapping inline lets CPU tests pin the same
// branch used by the full script parser without linking a live Window graph.
inline bool TryParseInteractionAutomationVirtualKey( const char* value, int& outVirtualKey )
{
    const std::string_view key = value ? value : "";

    if ( key.size() >= 2 && ( key[0] == 'F' || key[0] == 'f' ) )
    {
        if ( key.size() == 2 && key[1] >= '1' && key[1] <= '9' )
        {
            outVirtualKey = VK_F1 + ( key[1] - '1' );
            return true;
        }

        if ( key == "F10" || key == "f10" )
        {
            outVirtualKey = VK_F10;
            return true;
        }

        if ( key == "F11" || key == "f11" )
        {
            outVirtualKey = VK_F11;
            return true;
        }

        if ( key == "F12" || key == "f12" )
        {
            outVirtualKey = VK_F12;
            return true;
        }
    }

    return false;
}
SkullbonezCore::Core::SbResult InteractionAutomationResult( const InteractionAutomationController& state );
void ClearInteractionAutomationInput( InteractionAutomationController& state );
InteractionAutomationFrameResult TickInteractionAutomationBeforeInput( InteractionAutomationController& state, Window& window, const SkullbonezCore::Core::EngineConfig& config,
                                                                       SceneController& scene, RunTimerState& timers, CameraControlState& camera, InputRouter& inputRouter,
                                                                       RuntimeInteractionController& interaction, RuntimeTools& runtimeTools, SkullbonezCore::UI::InGameUI& ui,
                                                                       const ReplayAutomationView& replayView, const Rendering::RenderSceneSnapshot& renderSnapshot );
InteractionAutomationFrameResult TickInteractionAutomationAfterRender( InteractionAutomationController& state, RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                                                                       InputRouter& inputRouter, CameraControlState& camera, SkullbonezCore::UI::InGameUI& ui, SceneController& scene,
                                                                       const ReplayAutomationView& replayView, const InteractionAutomationDevelopmentUiView& developmentUiView,
                                                                       const ContinuousOrbitalForecastView& forecastView, const Rendering::RenderSceneSnapshot& renderSnapshot,
                                                                       CaptureController& capture, Rendering::Dx12BackbufferCapture& backbufferCapture );
bool InteractionAutomationWillCaptureAfterRender( const InteractionAutomationController& state, int frame );
} // namespace Runtime
} // namespace SkullbonezCore

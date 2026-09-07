/*
Purpose:
  Defines legacy interaction-script sequencing and recorded-manifest playback.

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
  - Replay intercept assertions consume a copied value snapshot and cannot
    retarget or advance the retained closest-approach scan.
  - Prediction cause-row assertions count only typed rows already published by
    the normal cause-tree owner; they do not reconstruct solver evidence.
  - Continuous forecast actions cross the normal typed command queue; the
    sequencer stores no Planning owner reference or worker-ring span.
  - App applies editor and window commands synchronously; automation stores
    neither owner while retaining script progress or report evidence.
  - Recorded-cursor trace evidence is one overwritten detached frame value; it
    does not retain an input, UI, renderer, or native pointer owner.
  - Process-wide development-surface selection remains a typed request for Run.
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "../RuntimeFrameViews.h"
#include "ReplayAutomationView.h"
#include "InteractionAutomationInputDriver.h"
#include "InteractionAutomationRecorder.h"
#include "InteractionAutomationReportWriter.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Tools/RuntimeFileWriter.h"

#include "../../Core/Common.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"
#include "../Camera/DemoDirector.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Replay/ReplayCoordination.h"
#include "../Interaction/OperatorEditorExchange.h"

#include <string>
#include <string_view>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class SbDiagnosticStore;
} // namespace Core
namespace UI
{
}
namespace Runtime
{
struct RuntimeFrameMetricsSnapshot;
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
    PredictionCausalGeometrySubmitted,
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
    GameUiReplayPresentationActive,
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
    char directorShotListPath[DemoDirectorPlaybackState::SHOT_LIST_PATH_BYTES] = {};
    POINT mouse = {};
    bool hasMouse = false;
    bool processed = false;
};

inline bool TryRetainInteractionShotListPath( RunInteractionAutomationAction& action, std::string_view path ) noexcept
{
    if ( path.empty() || path.size() >= sizeof( action.directorShotListPath ) ||
         path.find( '\0' ) != std::string_view::npos )
    {
        return false;
    }

    // Invariant: admission completes before the action field changes, so the
    // director never receives a truncated automation path.
    std::memcpy( action.directorShotListPath, path.data(), path.size() );
    action.directorShotListPath[path.size()] = '\0';
    return true;
}

inline bool RecordedInteractionRequiresFreeRunningPresent( bool automationEnabled, bool recordedManifest ) noexcept
{
    // Why: a recorded turn already carries its own elapsed time. Coupling one
    // turn to one display refresh stretches high-rate captures during playback.
    return automationEnabled && recordedManifest;
}

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
    RecordedCursorPresentationObservation recordedCursorPresentation;
    // Automation joins the same bounded command queues as native controls.
    Core::SbResult SubmitOperatorEditorReplayCommand( const InteractionAutomationFrameResult& frame,
                                                      UI::OperatorEditorCommandQueues& commands ) const;
    Core::SbResult SubmitOperatorEditorForecastCommand( const InteractionAutomationFrameResult& frame,
                                                        UI::OperatorEditorCommandQueues& commands ) const;
};

struct InteractionAutomationFrameResult
{

    bool requestQuit = false;
    bool hasRecordedDeltaSeconds = false;
    double recordedDeltaSeconds = 0.0;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();

    // Lifetime: Automation publishes raw recorded-turn facts here; Run filters
    // one stack copy after normal input routing and retains nothing between frames.
    RecordedCursorFrame recordedCursor;

    // Value-only replay mutations are applied once by the frame composition
    // boundary after automation has finished producing its synthetic input.
    ReplayFrameIntent replayIntent;
    int requestedReplayCauseRow = -1;

    // Value-only editor automation joins the same bounded command arbitration
    bool hasOperatorEditorReplayCommand = false;
    UI::OperatorEditorReplayCommand operatorEditorReplayCommand;
    bool hasOperatorEditorForecastCommand = false;
    UI::OperatorEditorForecastCommand operatorEditorForecastCommand;
    bool applyCameraMode = false;
    RunCameraMode cameraMode = RunCameraMode::Demo;
    bool restoreRecordedSceneCameraBaseline = false;
    bool recordedSceneMode = true;
    int recordedDemoSelectedCamera = -1;
    float recordedDemoCameraCycleSeconds = 0.0f;
    bool applyDirectorCameraPose = false;
    DemoCameraPose directorCameraPose;
    bool setWorldInteractionOwner = false;
    int worldInteractionOwner = 0;
    int worldInteractionReason = 0;
    bool restoreRecordedReplayBaseline = false;
    bool recordedReplayScrubPaused = false;
    bool recordedReplayLiveAdvanceHeld = false;
    RunReplayTrack recordedReplayTrack = RunReplayTrack::Solver;
    float recordedReplayPresentationTrackPosition = 1.0f;
    float recordedReplaySolverTrackPosition = 1.0f;
    bool restoreRecordedReplayCauseBaseline = false;
    InteractionRecordingBaseline recordedReplayCauseBaseline;
};

// Resolves the exact policy used by ConfigureInteractionAutomation before it
// opens the trace. Report suppression also protects Run's later failure-write.
inline const char* ApplyInteractionAutomationOutputPathPolicy( const char* scriptPath, const char* reportPath,
                                                               const char* tracePath,
                                                               InteractionAutomationReportWriter& reportWriter )
{
    if ( RuntimeFileWriter::PathsResolveToSameFile( scriptPath, reportPath ) )
    {
        reportWriter.SuppressUnsafeOutput();
        return "interaction report path resolves to interaction script path";
    }

    if ( tracePath && tracePath[0] != '\0' && RuntimeFileWriter::PathsResolveToSameFile( scriptPath, tracePath ) )
    {
        return "interaction trace path resolves to interaction script path";
    }

    return nullptr;
}

// Performs the exact copy/open sequence used by ConfigureInteractionAutomation.
// Identity policy runs on the original caller strings before a fixed buffer or
// truncating stream can lose the evidence needed for the decision.
inline const char* PrepareInteractionAutomationOutputPaths( const char* scriptPath, const char* reportPath,
                                                            const char* tracePath, char* scriptDestination,
                                                            std::size_t scriptCapacity, char* traceDestination,
                                                            std::size_t traceCapacity, std::ofstream& traceOutput,
                                                            InteractionAutomationReportWriter& reportWriter )
{
    const char* policyFailure = ApplyInteractionAutomationOutputPathPolicy( scriptPath, reportPath, tracePath,
                                                                            reportWriter );

    if ( policyFailure )
    {
        return policyFailure;
    }

    if ( !scriptPath || scriptPath[0] == '\0' || std::strlen( scriptPath ) >= scriptCapacity ||
         strcpy_s( scriptDestination, scriptCapacity, scriptPath ) != 0 )
    {
        return "interaction script path exceeds supported length";
    }

    if ( tracePath && tracePath[0] != '\0' )
    {
        if ( std::strlen( tracePath ) >= traceCapacity || strcpy_s( traceDestination, traceCapacity, tracePath ) != 0 )
        {
            return "interaction trace path exceeds supported length";
        }

        if ( !RuntimeFileWriter::OpenTextFile( traceDestination, traceOutput ) )
        {
            return "interaction turn trace could not be opened";
        }
    }

    return nullptr;
}

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
bool InteractionAutomationWillCaptureAfterRender( const InteractionAutomationController& state, int frame );
} // namespace Runtime
} // namespace SkullbonezCore

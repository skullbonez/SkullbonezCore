/*
File: SkullbonezSource/Runtime/InteractionAutomationController.h
Purpose:
  Defines the CLI interaction automation script and report state.

Summary:
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

#include "../Core/PlatformWin32.h"

#include "RuntimeFrameViews.h"

#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "DemoDirector.h"
#include "RuntimeCameraMode.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core
namespace Rendering
{
class IRenderCaptureBackend;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class AttachedCameraController;
class CaptureController;
class InputRouter;
class ReplayRuntime;
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
    SetReplayPredictionEnabled,
    SetReplayPredictionHorizonSeconds,
    BeginReplayVisualFidelityCapture,
    SetReplayPathTarget,
    NudgeReplayPathTargetVelocity,
    ShowReplayScrubber,
    PressKey,
    CaptureEditorSelectionState,
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
    EditorSelectionMatchesCapture
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

struct ReplayVisualFidelityReportTick
{
    int sceneFrame = 0;
    uint64_t revealFrame = 0;
    uint64_t ordinaryLineHash = 0;
    uint64_t priorityLineHash = 0;
    uint64_t priorityLineCanonicalHash = 0;
    uint64_t ordinaryRibbonHash = 0;
    uint64_t priorityRibbonHash = 0;
    uint64_t priorityRibbonCanonicalHash = 0;
    uint64_t vertexHash = 0;
    uint64_t ordinaryVertexHash = 0;
    uint64_t ordinaryLineBytes = 0;
    uint64_t priorityLineBytes = 0;
    uint64_t ordinaryRibbonBytes = 0;
    uint64_t priorityRibbonBytes = 0;
    uint64_t vertexBytes = 0;
    uint64_t ordinaryVertexBytes = 0;
    uint32_t ordinaryLineVertexCount = 0;
    uint32_t priorityLineVertexCount = 0;
    uint32_t ordinaryRibbonSegmentCount = 0;
    uint32_t priorityRibbonSegmentCount = 0;
    uint32_t vertexCount = 0;
    uint32_t ordinaryVertexCount = 0;
    uint32_t segmentCount = 0;
};

struct ReplayCausalProofTick
{
    // Concept: this is the stable causal envelope beside V0's exact submitted
    // geometry. Counts may only grow as the fixed reveal cursor advances.
    uint64_t revealFrame = 0;
    uint64_t activeTopologyHash = 0;
    uint32_t activeNodeCount = 0;
    uint32_t revealedRecordCount = 0;
    uint32_t revealedPointCount = 0;
    uint32_t revealedSegmentCount = 0;
    uint32_t entryMarkerCount = 0;
    uint32_t restMarkerCount = 0;
    uint32_t horizonMarkerCount = 0;
    uint32_t ghostRequestCount = 0;
};

struct ReplayCausalTopologyNodeReport
{
    // PhysicsSceneObjectId-backed replay ids preserve the parent/depth chain in
    // report JSON without borrowing mutable runtime topology storage.
    uint32_t id = 0;
    uint32_t parentId = 0;
    uint64_t firstFrame = 0;
    int depth = 0;
    bool contactDerived = false;
};

struct ReplayPredictedLiveReportTick
{
    // Invariant: the two hashes summarize the same ordered body presentation
    // fields, but the runtime also compares each float bit and reports the first
    // typed field before appending this row.
    uint64_t offset = 0;
    uint64_t predictedFrame = 0;
    uint64_t liveFrame = 0;
    uint64_t predictedHash = 0;
    uint64_t liveHash = 0;
    uint32_t bodyCount = 0;
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
    std::vector<ReplayVisualFidelityReportTick> replayVisualFidelityTicks;
    // Validation-only retained evidence. These vectors live only for a bounded
    // CLI automation process and never become replay/runtime business state.
    std::vector<ReplayCausalProofTick> replayCausalProofTicks;
    std::vector<ReplayCausalTopologyNodeReport> replayCausalTopology;
    std::vector<ReplayPredictedLiveReportTick> replayPredictedLiveTicks;
    int replayVisualFidelityStartFrame = -1;
    bool replayVisualFidelityCaptureEnabled = false;
    bool replayCausalLiveReadyToPlay = false;
    bool replayCausalLivePausePrimed = false;
    bool replayCausalLivePlayInjected = false;
    bool replayCausalLiveComplete = false;
    uint64_t replayCausalSourceFrame = 0;
    uint64_t replayCausalNextOffset = 0;
    uint64_t replayVisualFidelityTrajectoryHash = 0;
    uint64_t replayVisualFidelityTrajectoryRecordCount = 0;
    uint64_t replayVisualFidelityTrajectoryPointCount = 0;
    bool replayVisualFidelityTrajectoryCaptured = false;
    int replayCausalControlFrame = -1;
    POINT mouseClientPosition = {};
    bool hasMouseClientPosition = false;
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    int keyVirtualKey = 0;
    bool keyDown = false;
    bool controlDown = false;
    int releaseLeftFrame = -1;
    int releaseRightFrame = -1;
    int releaseKeyFrame = -1;
    int unfocusedInputFrames = 0;
    uint64_t editorSelectionCaptureFingerprints[2] = {};
    bool editorSelectionCaptureValid[2] = {};
};

struct InteractionAutomationFrameResult
{
    bool requestQuit = false;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
};

SkullbonezCore::Core::SbResult ConfigureInteractionAutomation( InteractionAutomationController& state,
                                                               const char* scriptPath,
                                                               const char* reportPath );
SkullbonezCore::Core::SbResult InteractionAutomationResult( const InteractionAutomationController& state );
void ClearInteractionAutomationInput( InteractionAutomationController& state );
InteractionAutomationFrameResult TickInteractionAutomationBeforeInput( InteractionAutomationController& state,
                                                                       RuntimeFrameHostView& host,
                                                                       RuntimeFrameInteractionView& interactionOwners,
                                                                       RuntimeFrameSceneView& sceneOwners );
InteractionAutomationFrameResult
TickInteractionAutomationAfterRender( InteractionAutomationController& state,
                                      RuntimeFrameInteractionView& interactionOwners,
                                      RuntimeFrameSceneView& sceneOwners,
                                      CaptureController& capture,
                                      Rendering::IRenderCaptureBackend& captureBackend );
bool InteractionAutomationWillCaptureAfterRender( const InteractionAutomationController& state, int frame );
SkullbonezCore::Core::SbResult WriteInteractionAutomationReport( InteractionAutomationController& state,
                                                                 const SceneController& scene,
                                                                 const RuntimeTools& runtimeTools,
                                                                 const ReplayRuntime& replayRuntime,
                                                                 const RuntimeInteractionController& interaction,
                                                                 const RunCameraState& camera,
                                                                 const UI::InGameUI& ui );
} // namespace Runtime
} // namespace SkullbonezCore

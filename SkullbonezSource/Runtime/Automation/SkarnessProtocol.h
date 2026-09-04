#pragma once

#if defined( SKULLBONEZ_SKARNESS )

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore::Runtime
{
inline constexpr uint32_t SKARNESS_SCHEMA_VERSION = 1u;

enum class SkarnessCommandType : uint8_t
{
    CaptureScreenshot,
    SceneLoad,
    SceneReset,
    SceneLoadDemo,
    SceneObjectList,
    SceneObjectResolve,
    SceneObjectSelect,
    SceneObjectClearSelection,
    ReplaySetRecordingEnabled,
    ReplaySetRetentionSeconds,
    ReplaySetMemoryBudgetMiB,
    ReplayJumpToStart,
    ReplayJumpToEnd,
    ReplaySetPlaybackPaused,
    ReplayStepBackward,
    ReplayStepForward,
    ReplaySetRevealSpeed,
    ReplayScrub,
    ReplaySeekFrame,
    ReplaySetPredictionEnabled,
    ReplaySetPredictionDetailMode,
    ReplaySetPredictionHorizon,
    ReplaySetVelocityEditEnabled,
    ReplaySetRagdollVisualsEnabled,
    ReplaySetPastPathVisible,
    ReplaySetGuideArcsEnabled,
    ReplaySetPathColorMode,
    ReplaySetInterceptTarget,
    ReplayVelocityPreview,
    ReplayVelocityCommit,
    ReplayVelocityCancel,
    PredictionRevealReset,
    PredictionRevealAdvance,
    ReplayRestoreBranch,
    ReplaySave,
    ReplayLoad,
    ReplayReturnToLive,
    ReplaySelectCauseRow,
    ReplaySelectCause,
    ReplaySetCauseInspectorOpen,
    ReplaySetCauseFilterText,
    ReplaySetCauseFilter,
    ReplaySetCauseInspectorTab,
    ReplayReturnFromCause,
    ReplayCopyCauseRecord,
    ReplaySetPorkchopVisible,
    ReplaySelectPorkchopCell,
    ReplaySetTripTimeOfFlight,
    ReplayTripPlan,
    ReplayTripCommit,
    ReplayTripCancel,
    PredictionForecastStart,
    PredictionForecastReset,
    PredictionForecastStop,
    PredictionSelectTarget,
    CameraOrbitInspection
};

struct SkarnessCommand
{
    std::string requestId;
    SkarnessCommandType type = SkarnessCommandType::ReplayJumpToStart;
    std::string text;
    std::string secondText;
    double number = 0.0;
    double secondNumber = 0.0;
    double thirdNumber = 0.0;
    double fourthNumber = 0.0;
    double fifthNumber = 0.0;
    double sixthNumber = 0.0;
    uint64_t unsignedInteger = 0;
    uint64_t secondUnsignedInteger = 0;
    uint64_t thirdUnsignedInteger = 0;
    uint64_t fourthUnsignedInteger = 0;
    uint64_t fifthUnsignedInteger = 0;
    uint64_t sixthUnsignedInteger = 0;
    int integer = 0;
    int secondInteger = 0;
    bool enabled = false;
};

struct SkarnessSceneObjectResult
{
    uint64_t sceneObjectId = 0;
    int modelRow = -1;
    std::string name;
};

// Detached typed result values are serialized only by Automation after the
// sampled frame is durable. App never assembles protocol JSON.
struct SkarnessCommandResult
{
    std::vector<SkarnessSceneObjectResult> objects;
    std::string valueName;
    std::string textValue;
    double numberValue = 0.0;
    uint64_t unsignedValue = 0;
    int integerValue = 0;
    bool boolValue = false;
    bool hasTextValue = false;
    bool hasNumberValue = false;
    bool hasUnsignedValue = false;
    bool hasIntegerValue = false;
    bool hasBoolValue = false;
};

// App fills one detached completion while applying a command. The transport
// remains the sole owner of lifecycle serialization and request bookkeeping.
struct SkarnessCommandApplication
{
    SkarnessCommandResult result;
    const char* reason = nullptr;
    bool handled = false;
    bool applied = true;
    bool deferred = false;
};

enum class SkarnessCapabilityAvailability : uint8_t
{
    Always,
    AutomatedInputOnly
};

struct SkarnessCapability
{
    const char* name = nullptr;
    const char* owner = nullptr;
    const char* arguments = nullptr;
    SkarnessCapabilityAvailability availability = SkarnessCapabilityAvailability::Always;
};

// This catalog is the one discoverable protocol inventory. Player controls,
// parsers, and mechanical coverage tests join on these stable command names.
inline constexpr std::array SKARNESS_CAPABILITIES = {
    SkarnessCapability { "capabilities.get", "Automation", "{}" },
    SkarnessCapability { "session.stop", "Automation", "{}" },
    SkarnessCapability { "capture.screenshot", "Capture", "{path:string}" },
    SkarnessCapability { "scene.load", "Scene", "{name:string}|{path:string}" },
    SkarnessCapability { "scene.reset", "Scene", "{}" },
    SkarnessCapability { "scene.load_demo", "Scene", "{}" },
    SkarnessCapability { "scene.object.list", "Scene", "{}" },
    SkarnessCapability { "scene.object.resolve", "Scene", "{name:string}|{sceneObjectId:uint64}" },
    SkarnessCapability { "scene.object.select", "Interaction",
                         "{scope:inspect|editor,name:string}|{scope:inspect|editor,sceneObjectId:uint64}" },
    SkarnessCapability { "scene.object.clear_selection", "Interaction", "{scope:inspect|editor}" },
    SkarnessCapability { "run.pause", "Automation", "{}" },
    SkarnessCapability { "run.resume", "Automation", "{}" },
    SkarnessCapability { "run.step", "Automation", "{count:int[1..100000]}" },
    SkarnessCapability { "run.step_frames", "Automation", "{count:int[1..100000]}" },
    SkarnessCapability { "run.until", "Automation", "{condition:enum,maxTicks:int}|{condition:enum,maxFrames:int}" },
    SkarnessCapability { "replay.set_recording_enabled", "Replay", "{enabled:bool}" },
    SkarnessCapability { "replay.set_retention_seconds", "Replay", "{seconds:int}" },
    SkarnessCapability { "replay.set_memory_budget_mib", "Replay", "{mib:int}" },
    SkarnessCapability { "replay.jump_to_start", "Replay", "{}" },
    SkarnessCapability { "replay.jump_to_end", "Replay", "{}" },
    SkarnessCapability { "replay.set_playback_paused", "Replay", "{paused:bool}" },
    SkarnessCapability { "replay.step_backward", "Replay", "{}" },
    SkarnessCapability { "replay.step_forward", "Replay", "{}" },
    SkarnessCapability { "replay.set_reveal_speed", "Prediction", "{rate:number}" },
    SkarnessCapability { "replay.scrub", "Replay", "{normalized:number[0..1]}" },
    SkarnessCapability { "replay.seek_frame", "Replay", "{frame:uint64}" },
    SkarnessCapability { "replay.set_prediction_enabled", "Prediction", "{enabled:bool}" },
    SkarnessCapability { "replay.set_prediction_detail", "Prediction", "{highDetail:bool}" },
    SkarnessCapability { "replay.set_prediction_horizon", "Prediction", "{seconds:number}" },
    SkarnessCapability { "replay.set_velocity_edit_enabled", "Replay", "{enabled:bool}" },
    SkarnessCapability { "replay.set_ragdoll_visuals_enabled", "Prediction", "{enabled:bool}" },
    SkarnessCapability { "replay.set_past_path_visible", "Replay", "{visible:bool}" },
    SkarnessCapability { "replay.set_guide_arcs_enabled", "Planning", "{enabled:bool}" },
    SkarnessCapability { "replay.set_path_color_mode", "Replay", "{mode:lane|velocity|time|object|causal}" },
    SkarnessCapability { "replay.set_intercept_target", "Planning", "{name:string}|{sceneObjectId:uint64}" },
    SkarnessCapability { "replay.velocity_preview", "Replay",
                         "{linear:[number,number,number],angular:[number,number,number]}" },
    SkarnessCapability { "replay.velocity_commit", "Replay", "{}" },
    SkarnessCapability { "replay.velocity_cancel", "Replay", "{}" },
    SkarnessCapability { "prediction.reveal_reset", "Prediction", "{}" },
    SkarnessCapability { "prediction.reveal_advance", "Prediction", "{frames:int}" },
    SkarnessCapability { "replay.restore_branch", "Replay", "{}" },
    SkarnessCapability { "replay.save", "Replay", "{path:string}" },
    SkarnessCapability { "replay.load", "Replay", "{path:string}" },
    SkarnessCapability { "replay.return_to_live", "Replay", "{}" },
    SkarnessCapability { "replay.select_cause_row", "Replay", "{row:int}" },
    SkarnessCapability { "replay.select_cause", "Planning",
                         "{row:int,sceneObjectId:uint64,frame:uint64,generation:uint64,bankEpoch:uint64,topologyVersion:"
                         "uint64,publicationVersion:uint64}" },
    SkarnessCapability { "replay.set_cause_inspector_open", "Planning", "{open:bool}" },
    SkarnessCapability { "replay.set_cause_filter_text", "Replay", "{text:string}" },
    SkarnessCapability { "replay.set_cause_filter", "Replay", "{filter:all|prediction|contacts}" },
    SkarnessCapability { "replay.set_cause_inspector_tab", "Planning", "{tab:summary|raw|iterations}" },
    SkarnessCapability { "replay.close_cause_detail", "Planning", "{}" },
    SkarnessCapability { "replay.return_from_cause", "Planning", "{}" },
    SkarnessCapability { "replay.copy_cause_record", "Planning", "{}" },
    SkarnessCapability { "replay.set_porkchop_visible", "Planning", "{visible:bool}" },
    SkarnessCapability { "replay.select_porkchop_cell", "Planning", "{cell:int}" },
    SkarnessCapability { "replay.set_trip_time_of_flight", "Planning", "{seconds:number}" },
    SkarnessCapability { "replay.trip_plan", "Planning", "{}" },
    SkarnessCapability { "replay.trip_commit", "Planning", "{}" },
    SkarnessCapability { "replay.trip_cancel", "Planning", "{}" },
    SkarnessCapability { "prediction.forecast_start", "Planning", "{}" },
    SkarnessCapability { "prediction.forecast_reset", "Planning", "{}" },
    SkarnessCapability { "prediction.forecast_stop", "Planning", "{}" },
    SkarnessCapability { "prediction.select_target", "Replay", "{name:string}|{sceneObjectId:uint64}" },
    SkarnessCapability { "replay.set_path_target", "Replay", "{name:string}|{sceneObjectId:uint64}" },
    SkarnessCapability { "camera.orbit_inspection", "Camera", "{yawRadians:number,pitchRadians:number}" },
    SkarnessCapability { "state.subscribe", "Automation", "{topics:[string],detail:summary|normal|full}" },
    SkarnessCapability { "input.pointer_drag", "Input", "{button:left|right|middle,x:int,y:int,deltaX:int,deltaY:int}",
                         SkarnessCapabilityAvailability::AutomatedInputOnly },
};

struct SkarnessProceedPolicy
{
    bool pauseLocked = false;
    bool stepRequested = false;
};

enum class SkarnessPointerButton : uint8_t
{
    Left,
    Right,
    Middle
};

// Detached synthetic device sample. Input still owns edge creation, pointer
// capture, mouse-look activation, and camera gesture routing.
struct SkarnessPointerInputFrame
{
    int clientX = 0;
    int clientY = 0;
    long rawMouseX = 0;
    long rawMouseY = 0;
    SkarnessPointerButton button = SkarnessPointerButton::Right;
    bool buttonDown = false;
};

// Detached after-render facts. The transport can serialize this value but
// cannot reach Replay, Scene, Prediction, or renderer owners.
struct SkarnessFrameState
{
    uint64_t sceneGeneration = 0;
    int sceneFrame = 0;
    char scenePath[512] = {};
    int sceneObjectCount = 0;
    int physicsBodyCount = 0;
    int sceneLifecycleEvent = 0;
    bool sceneReady = false;
    bool sceneMode = false;
    double simulationSeconds = 0.0;
    bool paused = true;
    bool replayCaptureEnabled = false;
    bool replayScrubPaused = false;
    bool replayPlaybackPaused = false;
    bool predictionEnabled = false;
    bool predictionBuilding = false;
    bool predictionComplete = false;
    bool predictionDirty = false;
    bool predictionRestartPending = false;
    bool predictionGenerationPermitted = false;
    bool predictionHighDetail = false;
    bool velocityEditEnabled = false;
    bool ragdollVisualsEnabled = false;
    bool pastPathVisible = false;
    bool hasPathTarget = false;
    uint64_t pathTargetId = 0;
    int pathTargetModelRow = -1;
    float predictionHorizonSeconds = 0.0f;
    float predictionRevealProgress = 0.0f;
    uint32_t predictionGeneration = 0;
    uint64_t predictionSourceTargetId = 0;
    uint64_t predictionSourceFrame = 0;
    uint64_t predictionSourceSolverHash = 0;
    uint32_t committedPredictionFrames = 0;
    uint32_t predictionBuildPublishedFrames = 0;
    bool predictionWorkerFailed = false;
    bool predictionEvidenceCapacityTruncated = false;
    uint64_t predictionEvidenceFirstTruncatedFrame = 0;
    uint64_t predictionEvidenceEmptyBuildCommitCount = 0;
    uint32_t predictionEvidenceBuildFrames = 0;
    uint32_t predictionEvidenceCommittedFrames = 0;
    uint32_t incompleteContactFrameCount = 0;
    uint64_t publishedPredictionTargetId = 0;
    uint32_t publishedPredictionFrames = 0;
    uint32_t trajectoryRecordCount = 0;
    uint32_t selectedPastRootPointCount = 0;
    uint32_t selectedFutureRootPointCount = 0;
    uint32_t contactChildIncomingCount = 0;
    uint32_t contactChildOutgoingCount = 0;
    uint32_t childOutgoingPreEntryPointCount = 0;
    uint32_t retainedEntryMarkerCount = 0;
    uint32_t retainedEndMarkerCount = 0;
    uint32_t drawnCollisionWireframeCount = 0;
    uint32_t drawnEndingWireframeCount = 0;
    uint32_t collisionWireframePathMismatchCount = 0;
    uint32_t endingWireframePathMismatchCount = 0;
    uint32_t futureNodeCount = 0;
    uint32_t retainedLineFloatCount = 0;
    uint32_t retainedRibbonVertexFloatCount = 0;
    uint32_t causeTreeRowCount = 0;
    uint64_t causeTreeRowBuildCount = 0;
    uint64_t causeTreeRowCacheHitCount = 0;
    bool causeWindowAvailable = false;
    bool causeInspectorOpen = false;
    float causeInspectorDrawerProgress = 0.0f;
    int selectedCauseRow = -1;
    int causeInspectionMode = 0;
    float causeTransitionProgress = 0.0f;
    uint64_t selectedCauseFrame = 0;
    uint64_t causeSourceFrame = 0;
    uint64_t causeTargetFrame = 0;
    uint64_t causePresentedFrame = 0;
    int causeSeekSource = 0;
    uint64_t presentedReplayFrame = 0;
    int presentedReplayFrameSource = 0;
    bool inspectionCameraActive = false;
    int inspectionCameraFocusKind = 0;
    bool inspectionFocusFadeActive = false;
    uint32_t inspectionFocusObjectCount = 0;
    uint64_t selectedCausePrimaryId = 0;
    uint64_t selectedCauseCounterpartId = 0;
    uint32_t causeContactPointCount = 0;
    uint32_t submittedCauseContactPointCount = 0;
    uint32_t submittedCauseContactBodyCount = 0;
    uint64_t inspectionPathFocusPrimaryId = 0;
    uint64_t inspectionPathFocusCounterpartId = 0;
    uint32_t inspectionFocusedPathRangeCount = 0;
    uint32_t inspectionContextPathRangeCount = 0;
    uint32_t inspectionFocusedPathSegmentCount = 0;
    uint32_t inspectionContextPathSegmentCount = 0;
    uint32_t inspectionPathOpacityMismatchCount = 0;
    bool inspectionPathFocusActive = false;
    uint64_t inspectionBodyMarkerId = 0;
    float inspectionBodyMarkerX = 0.0f;
    float inspectionBodyMarkerY = 0.0f;
    float inspectionBodyMarkerZ = 0.0f;
    bool inspectionBodyMarkerSubmitted = false;
    float inspectionPivotX = 0.0f;
    float inspectionPivotY = 0.0f;
    float inspectionPivotZ = 0.0f;
    uint32_t selectedCameraHash = 0;
    bool cameraTweenActive = false;
    float cameraTweenProgress = 0.0f;
    float cameraPrimaryEyeX = 0.0f;
    float cameraPrimaryEyeY = 0.0f;
    float cameraPrimaryEyeZ = 0.0f;
    float cameraPrimaryViewX = 0.0f;
    float cameraPrimaryViewY = 0.0f;
    float cameraPrimaryViewZ = 0.0f;
    float cameraPrimaryUpX = 0.0f;
    float cameraPrimaryUpY = 1.0f;
    float cameraPrimaryUpZ = 0.0f;
    float cameraRenderEyeX = 0.0f;
    float cameraRenderEyeY = 0.0f;
    float cameraRenderEyeZ = 0.0f;
    float cameraRenderViewX = 0.0f;
    float cameraRenderViewY = 0.0f;
    float cameraRenderViewZ = 0.0f;
    float cameraRenderUpX = 0.0f;
    float cameraRenderUpY = 1.0f;
    float cameraRenderUpZ = 0.0f;
    float cameraRenderRollRadians = 0.0f;
    bool retainedPathGeometrySaturated = false;
    bool visualPacketHasGeometry = false;
    bool trajectorySubmitted = false;
    uint32_t submittedSegmentCount = 0;
    uint32_t submittedVertexCount = 0;
    uint64_t submittedPredictionTargetId = 0;
    uint64_t submittedPredictionSourceFrame = 0;
    uint32_t submittedPredictionTopologyVersion = 0;
    uint64_t submittedGeometryHash = 0;
    uint64_t submittedGeometryBytes = 0;
    uint32_t publishedPredictionTopologyVersion = 0;
    bool submittedFutureTreeReady = false;
};
} // namespace SkullbonezCore::Runtime

#endif

#pragma once

#if defined( SKULLBONEZ_SKARNESS )

#include <cstdint>
#include <string>

namespace SkullbonezCore::Runtime
{
inline constexpr uint32_t SKARNESS_SCHEMA_VERSION = 1u;

enum class SkarnessCommandType : uint8_t
{
    CaptureScreenshot,
    SceneLoad,
    SceneReset,
    SceneLoadDemo,
    ReplaySetRecordingEnabled,
    ReplayJumpToStart,
    ReplayJumpToEnd,
    ReplaySetPlaybackPaused,
    ReplayStepBackward,
    ReplayStepForward,
    ReplaySetRevealSpeed,
    ReplayScrub,
    ReplaySetPredictionEnabled,
    ReplaySetPredictionDetailMode,
    ReplaySetPredictionHorizon,
    ReplaySetVelocityEditEnabled,
    ReplaySetRagdollVisualsEnabled,
    ReplaySetPastPathVisible,
    ReplayRestoreBranch,
    ReplaySave,
    ReplayLoad,
    ReplayReturnToLive,
    ReplaySelectCauseRow,
    ReplaySetCauseInspectorOpen,
    PredictionSelectTarget,
    CameraOrbitInspection
};

struct SkarnessCommand
{
    std::string requestId;
    SkarnessCommandType type = SkarnessCommandType::ReplayJumpToStart;
    std::string text;
    double number = 0.0;
    double secondNumber = 0.0;
    uint64_t unsignedInteger = 0;
    int integer = 0;
    bool enabled = false;
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

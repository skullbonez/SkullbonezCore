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
    PredictionSelectTarget
};

struct SkarnessCommand
{
    std::string requestId;
    SkarnessCommandType type = SkarnessCommandType::ReplayJumpToStart;
    std::string text;
    double number = 0.0;
    uint64_t unsignedInteger = 0;
    int integer = 0;
    bool enabled = false;
};

struct SkarnessProceedPolicy
{
    bool pauseLocked = false;
    bool stepRequested = false;
};

// Detached after-render facts. The transport can serialize this value but
// cannot reach Replay, Scene, Prediction, or renderer owners.
struct SkarnessFrameState
{
    uint64_t sceneGeneration = 0;
    int sceneFrame = 0;
    double simulationSeconds = 0.0;
    bool paused = true;
    bool replayCaptureEnabled = false;
    bool replayScrubPaused = false;
    bool replayPlaybackPaused = false;
    bool predictionEnabled = false;
    bool predictionBuilding = false;
    bool predictionComplete = false;
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
    uint64_t publishedPredictionTargetId = 0;
    uint32_t publishedPredictionFrames = 0;
    uint32_t trajectoryRecordCount = 0;
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
    bool retainedPathGeometrySaturated = false;
    bool visualPacketHasGeometry = false;
    bool trajectorySubmitted = false;
    uint32_t submittedSegmentCount = 0;
    uint32_t submittedVertexCount = 0;
    bool submittedFutureTreeReady = false;
};
} // namespace SkullbonezCore::Runtime

#endif

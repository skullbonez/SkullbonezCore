/*
File: SkullbonezSource/Runtime/Replay/ReplayCoordination.h
Purpose:
  Defines frame-scoped commands and results exchanged at replay owner boundaries.

Summary:
  Replay owners retain state; the composition root receives short-lived values
  and borrowed references describing one operation. None of these packets is
  stored after the synchronous call that consumes it.

Glossary:
  Workspace tick: One input-frame pass through replay scrub, authoring, and path tools.
  Timeline reset: Scene-boundary command that restarts retained replay history.
  Startup workflow: Cold command-line replay load or validation operation.

Invariants:
  - Borrowed owner references remain valid only for the consuming call.
  - Values publish cross-owner effects; they do not provide callbacks into Run.
  - Scene reset facts contain no mutable scene or physics authority.

Related:
  - ReplayRuntime.h
  - ReplayRuntimeOwnerViews.h
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
namespace Runtime
{
class InputRouter;
class RuntimeInteractionController;
class SceneController;
class SceneEntityStore;
struct ReplayArtifactTopologyOwners;
struct ReplayRestoreTransaction;
struct ReplayStartupLoadInput;
struct RunCameraState;
struct RunMousePickupState;
struct RunSceneState;

struct ReplayWorkspaceInput
{
    HWND window = nullptr;
    bool uiBlocksMouse = false;
    int wheelDelta = 0;
    ReplayPathPickInput pointerRay;
    InputRouter& inputRouter;
    RuntimeInteractionController& interaction;
    Physics::PhysicsEngine& physics;
    const SceneEntityStore& entities;
    std::span<const Rendering::RenderInstancePresentationRecord> presentation;
    Environment::CameraCollection* cameras = nullptr;
    Geometry::Terrain* terrain = nullptr;
    RunCameraState& camera;
    RunMousePickupState& mousePickup;
    RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
    RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
    bool attachedFollow = false;
    bool directorGrabbed = false;
    bool editorModeEnabled = false;
    bool scenePhysicsEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    int screenWidth = 0;
    int screenHeight = 0;
    double now = 0.0;
};

struct ReplayWorkspaceOutput
{
    ReplayLiveRestoreRequest restoreRequest;
    bool consumesMouse = false;
    bool enterInteractive = false;
};

struct ReplayLiveRestoreOutcome
{
    bool requested = false;
    bool restored = false;
    bool enterInteractive = false;
    RunReplayV2TargetRestoreResult v2Result;
    char reason[160] = {};
};

struct ReplayStartupRequest
{
    const char* loadPath = nullptr;
    bool loadProbe = false;
#ifdef _DEBUG
    const char* checkpointProbePath = nullptr;
    const char* targetProbePath = nullptr;
    const char* branchProbePath = nullptr;
    const char* failureProbePath = nullptr;
    bool scrubProbe = false;
    float scrubProbeNormalized = 0.25f;
    bool restoreProbe = false;
    float restoreProbeNormalized = 0.25f;
    bool saveProbe = false;
    const char* saveProbePath = nullptr;
#endif
};

struct ReplayStartupResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool skipExecute = false;
};

// Concept: external automation publishes replay intent as values. The replay
// composition boundary applies each requested transition through the owning
// scrubber or prediction API; no caller receives a mutable owner reference.
struct ReplayFrameIntent
{
    bool setScrubberVisibility = false;
    bool scrubberVisible = false;
    double scrubberNow = 0.0;
    double scrubberHoldSeconds = 0.0;
    bool setPredictionEnabled = false;
    bool predictionEnabled = false;
    bool setPredictionHorizon = false;
    float predictionHorizonSeconds = 0.0f;
    bool prepareVelocityMutationBaseline = false;
    bool commitVelocityMutation = false;
    bool clearVelocityEditInputState = false;
    bool queryDeterministicRevealReady = false;
    bool armDeterministicReveal = false;
    ReplayFrameIndex revealFrame = 0;
    bool resetPresentedRevealFrame = false;
};

struct ReplayFrameIntentResult
{
    bool velocityMutationBaselinePrepared = false;
    bool deterministicRevealReady = false;
};

struct ReplayRecordingConfigResult
{
    ReplayRecorderConfig presentationConfig;
    ReplayRecorderConfig solverConfig;
    ReplayRecorderStats presentationStats;
    ReplayRecorderStats solverStats;
    ReplayEventRecorderStats eventStats;
};

struct ReplaySceneTimelineResetInput
{
    const char* sceneLabel = nullptr;
    bool preserveBranchMetadata = false;
    bool isSceneMode = false;
    int modelCount = 0;
    int solverBallCount = 0;
    int solverBoxCount = 0;
    uint32_t rngSeed = 0;
    int gameModelCapacity = 0;
    uint32_t generatedObjectTypeOverride = 0;
    bool hasUiModelCountOverride = false;
    bool hasUiSolverCountOverride = false;
};

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
    flags |=
        ( input.solverBallCount > 0 || input.solverBoxCount > 0 ) ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS : 0u;
    flags |= input.hasUiModelCountOverride ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
    flags |= input.hasUiSolverCountOverride ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS : 0u;
    flags |= ( input.generatedObjectTypeOverride << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
             REPLAY_GENERATED_SCENE_OVERRIDE_MASK;
    return flags;
}

ReplaySceneTimelineResetInput DescribeReplaySceneTimeline( const SceneController& sceneController,
                                                           const RunSceneState& scene,
                                                           int gameModelCapacity,
                                                           uint32_t generatedObjectTypeOverride );

struct ReplaySceneTimelineResetResult
{
    bool exitInspectionCamera = false;
    bool timelineStarted = false;
};

struct ReplaySceneTimelineResetOwners
{
    InputRouter& inputRouter;
    RuntimeInteractionController& interaction;
    Environment::CameraCollection* cameras = nullptr;
    Geometry::Terrain* terrain = nullptr;
    RunCameraState& camera;
    RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
    bool attachedFollow = false;
    bool directorGrabbed = false;
};

struct ReplayKeyboardVelocityEditInput
{
    bool altDown = false;
    bool toggleAllowed = true; // False records the key edge while the editor owns Alt.
    WorldInteractionOwner currentWorldOwner = WorldInteractionOwner::None;
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
    WorldInteractionOwner worldOwner = WorldInteractionOwner::None;
};

#ifdef _DEBUG
struct ReplayProbeTickResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool enterInteractive = false;
};
#endif
} // namespace Runtime
} // namespace SkullbonezCore

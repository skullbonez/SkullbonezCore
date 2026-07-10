/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h
Purpose:
  Defines scene reset preserve/restore policy outside Run.

Mental model:
  A normal Reset button rebuilds simulation instances, but it should preserve
  operator-owned runtime controls for the current scene. Scene changes and
  Reset To Defaults skip this policy so authored scene data becomes authority.

Glossary:
  Reset snapshot: Copy of operator-owned runtime settings preserved across
    same-scene reset.
  UI override: Scene-tab value that should survive an interactive reset.
  Operator-owned state: Live runtime choice made after scene load.

Invariants:
  - Snapshot fields must mirror restore logic one-for-one.
  - Context references are borrowed only for capture/restore duration.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneControllerState.h"
#include "../RunCameraState.h"
#include "../RunDebugState.h"
#include "../RunRuntimeSettings.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace Physics
{
class PhysicsDebugVisualizer;
}
namespace Basics
{
struct RunSceneState;

// Captures the part of a live run that belongs to the operator's current scene
// configuration rather than the simulation instance.
struct SceneRuntimeResetSnapshot
{
    RunRuntimeSettings runtimeSettings; // Live runtime toggles changed while operating the current scene
    RunDebugState
        debug;                          // Debug overlays/visualizers, including the C-key physics debug mode and associated alpha/linger knobs
    bool isScenePhysics =
        true;                           // Live scene simulation toggle; reset should rebuild the run, not silently re-enable physics
    bool isSceneText = true;            // Live text/HUD toggle from the scene controls
    bool isFixedStep = false;           // Live stepping mode; resetting the simulation should not change how it advances
    bool isExitOnComplete = false;      // Interactive reset preserves the user's automation/hold choice
    bool isInteractiveRun = false;      // Once a user owns the scene, a reset should not go back to CLI auto-quit behavior
    int targetFrameCount = -1;          // Live frame-count control from the UI
    float timeScale = 1.0f;             // Live time-scale control from the UI/scene controls
    float worldGravity = 0.0f;          // Live world/environment sliders
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    bool hasCinematicRenderingOverride = false;
    bool isCinematicRenderingEnabled = false;
    bool hasCinematicExposure = false;
    float cinematicExposure = 1.0f;
    bool hasCinematicGamma = false;
    float cinematicGamma = 2.2f;
    uint64_t cinematicOverrideMask = 0;
    uint64_t uiCinematicOverrideMask = 0;
    CinematicRenderConfig cinematicRender;
    float uiTimeScaleOverride =
        0.0f;                           // UI overrides feed object setup during reload, so they must survive before the scene rebuilds
    int uiModelCountOverride = -1;
    int uiSolverBallCountOverride = -1;
    int uiSolverBoxCountOverride = -1;
    int trackBallIndex = -1;            // Scene-tab camera tracking controls
    float trackHeight = 300.0f;
    float autoCycleInterval = -1.0f;
    float autoCycleAccum = 0.0f;
    int autoCycleShotsTaken = 0;
};

struct SceneRuntimeResetContext
{
    RunRuntimeSettings& runtimeSettings;
    RunDebugState& debug;
    RunSceneState& scene;
    RunSceneUIOverrideState& uiOverrides;
    RunCameraState& camera;
    Environment::WorldEnvironment& worldEnvironment;
    Physics::PhysicsDebugVisualizer& physicsDebugVisualizer;
};

SceneRuntimeResetSnapshot CaptureSceneRuntimeResetSnapshot( const SceneRuntimeResetContext& context );
void RestoreSceneRuntimeResetSnapshot( SceneRuntimeResetContext& context,
                                       const SceneRuntimeResetSnapshot& snapshot,
                                       bool suppressExitOnComplete );
void ClearSceneRuntimeUIOverrides( SceneRuntimeResetContext& context );

} // namespace Basics
} // namespace SkullbonezCore

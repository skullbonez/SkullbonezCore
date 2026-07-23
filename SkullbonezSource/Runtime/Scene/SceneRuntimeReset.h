/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h
Purpose:
  Defines scene reset preserve/restore policy outside Run.

Summary:
  A normal Reset button rebuilds simulation instances, but it should preserve
  operator-owned runtime controls for the current scene. Scene changes and
  Reset To Defaults skip this policy so authored scene data becomes authority.

Glossary:
  Reset snapshot: Value-only copy of owner state preserved across same-scene
    reset.
  UI override: Scene-tab value that should survive an interactive reset.
  Operator-owned state: Live runtime choice made after scene load.

Invariants:
  - Snapshot fields must mirror restore logic one-for-one.
  - Concrete owner references are borrowed only for capture/restore duration;
    no multi-domain reset context is retained.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "../CameraControlState.h"
#include "../OverlayDebugState.h"
#include "../Render/RenderPresentationSettings.h"
#include "../../Gameplay/TornadoField.h"
#include "../../Gameplay/TornadoVisualPass.h"
#include "../../Core/Config.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace Runtime
{
class SceneController;
class RuntimeRenderer;
struct SceneSessionState;

// Captures the part of a live run that belongs to the operator's current scene
// configuration rather than the simulation instance.
struct SceneRuntimeResetSnapshot
{
    RenderPresentationSettings renderPresentation; // Renderer-owned values restored after the new scene is populated.
    bool physicsSleepEnabled = true;
    Gameplay::TornadoFieldConfig tornadoField;
    Gameplay::TornadoSystemConfig tornadoSystem;
    Gameplay::TornadoVisualSettings tornadoVisual;
    OverlayDebugState
        debug;                                     // Debug overlays/visualizers, including the C-key physics debug mode and associated alpha/linger knobs
    bool isScenePhysics =
        true;                                      // Live scene simulation toggle; reset should rebuild the run, not silently re-enable physics
    bool isSceneText = true;                       // Live text/HUD toggle from the scene controls
    bool isFixedStep = false;                      // Live stepping mode; resetting the simulation should not change how it advances
    bool isExitOnComplete = false;                 // Interactive reset preserves the user's automation/hold choice
    bool isInteractiveRun = false;                 // Once a user owns the scene, a reset should not go back to CLI auto-quit behavior
    int targetFrameCount = -1;                     // Live frame-count control from the UI
    float timeScale = 1.0f;                        // Live time-scale control from the UI/scene controls
    float worldGravity = 0.0f;                     // Live world/environment sliders
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
    SkullbonezCore::Core::CinematicRenderConfig cinematicRender;
    float uiTimeScaleOverride =
        0.0f;                                      // UI overrides feed object setup during reload, so they must survive before the scene rebuilds
    int uiModelCountOverride = -1;
    int uiSolverBallCountOverride = -1;
    int uiSolverBoxCountOverride = -1;
    Physics::ModelRowHint trackBallRow;            // Scene-tab camera tracking cache
    float trackHeight = 300.0f;
    float autoCycleInterval = -1.0f;
    float autoCycleAccum = 0.0f;
    int autoCycleShotsTaken = 0;
};

SceneRuntimeResetSnapshot
CaptureSceneRuntimeResetSnapshot( const SceneController& controller,
                                  const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                  const RuntimeRenderer& renderer,
                                  const OverlayDebugState& debug,
                                  const CameraControlState& camera );
void RestoreSceneRuntimeResetSnapshot( SceneController& controller,
                                       SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                       RuntimeRenderer& renderer,
                                       OverlayDebugState& debug,
                                       CameraControlState& camera,
                                       const SceneRuntimeResetSnapshot& snapshot,
                                       bool suppressExitOnComplete );
void ClearSceneRuntimeUIOverrides( SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides );

} // namespace Runtime
} // namespace SkullbonezCore

/*
File: SkullbonezSource/Runtime/Scene/SceneResetPreservation.h
Purpose:
  Defines scene reset preserve/restore policy outside Run.

Summary:
  A normal Reset button rebuilds simulation instances, but it should preserve
  operator-owned runtime controls for the current scene. Scene changes and
  Reset To Defaults skip this policy so authored scene data becomes authority.

Invariants:
  - Snapshot fields must mirror restore logic one-for-one.
  - Concrete owner references are borrowed only for capture/restore duration;
    no multi-domain reset context is retained.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneSessionState.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneLoadPresentation.h"
#include "SceneRenderPolicy.h"
#include "../Camera/CameraControlState.h"
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
struct SceneSessionState;

// Captures the part of a live run that belongs to the operator's current scene
// configuration rather than the simulation instance.
struct SceneResetPreservationSnapshot
{
    SceneRenderPolicyState renderPolicy;  // Detached render policy restored by App after population.
    bool physicsSleepEnabled = true;
    Gameplay::TornadoFieldConfig tornadoField;
    Gameplay::TornadoSystemConfig tornadoSystem;
    Gameplay::TornadoVisualSettings tornadoVisual;
    ScenePresentationValues presentation; // Scene-authored render/debug values restored by App after population.
    bool isScenePhysics = true;           // Live scene simulation toggle; reset should rebuild the run, not silently re-enable

    // Preserved scene/session policy and presentation values.
    bool isSceneText = true;              // Live text/HUD toggle from the scene controls
    bool isFixedStep = false;             // Scene/capture lockstep request; effective pacing resolves after reset
    bool isExitOnComplete = false;        // Interactive reset preserves the user's automation/hold choice
    bool isInteractiveRun = false;        // Once a user owns the scene, a reset should not go back to CLI auto-quit behavior
    int targetFrameCount = -1;            // Live frame-count control from the UI
    float timeScale = 1.0f;               // Live time-scale control from the UI/scene controls
    float worldGravity = 0.0f;            // Live world/environment sliders
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
    float uiTimeScaleOverride = 0.0f;     // UI overrides feed object setup during reload, so they must survive before the

    // scene rebuilds
    int uiModelCountOverride = -1;
    int uiSolverBallCountOverride = -1;
    int uiSolverBoxCountOverride = -1;
    Physics::ModelRowHint trackBallRow;   // Scene-tab camera tracking cache
    float trackHeight = 300.0f;
    float autoCycleInterval = -1.0f;
    float autoCycleAccum = 0.0f;
    int autoCycleShotsTaken = 0;
};

} // namespace Runtime
} // namespace SkullbonezCore

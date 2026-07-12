/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp
Purpose:
  Implements scene reset preserve/restore policy outside Run.

Mental model:
  Reset preservation belongs to scene loading. SceneController supplies its
  owned state directly while the remaining value owners are explicit function
  arguments; no mutable multi-domain context is retained or stored.

Glossary:
  Reset snapshot: Value-only copy of owner state preserved across a same-scene
    reset transaction.
  Operator-owned state: UI/debug/camera/runtime choices made during the current
    run rather than authored scene defaults.
  Suppress exit: Interactive-run flag that prevents automation from quitting.

Invariants:
  - Capture and restore must stay field-complete for every preserved setting.
  - Restore clamps camera tracking to the rebuilt model count.
  - Reset-to-defaults clears UI overrides instead of restoring them.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeReset.h"
#include "SceneController.h"
#include "SceneRuntime.h"
#include "../Debug/PhysicsDebugVisualizer.h"
#include "../Render/RuntimeRenderer.h"
#include "../../World/WorldEnvironment.h"

namespace SkullbonezCore
{
namespace Basics
{
SceneRuntimeResetSnapshot CaptureSceneRuntimeResetSnapshot( const SceneController& controller,
                                                            const RuntimeRenderer& renderer,
                                                            const RunDebugState& debug,
                                                            const RunCameraState& camera )
{
    SceneRuntimeResetSnapshot snapshot;
    const RunSceneState& scene = controller.State();
    const RunSceneUIOverrideState& uiOverrides = controller.UIOverrides();
    // Invariant: Capture every field restored below. Adding a new preserved
    // runtime knob requires updating both sides of this snapshot contract.
    snapshot.renderPresentation = renderer.PresentationSettings();
    snapshot.physicsSleepEnabled = controller.Physics().IsSleepEnabled();
    snapshot.tornadoField = controller.Physics().GetTornadoFieldConfig();
    snapshot.tornadoSystem = controller.Physics().GetTornadoSystemConfig();
    snapshot.debug = debug;
    snapshot.isScenePhysics = scene.isScenePhysics;
    snapshot.isSceneText = scene.isSceneText;
    snapshot.isFixedStep = scene.isFixedStep;
    snapshot.isExitOnComplete = scene.isExitOnComplete;
    snapshot.isInteractiveRun = scene.isInteractiveRun;
    snapshot.targetFrameCount = scene.targetFrameCount;
    snapshot.timeScale = scene.timeScale;
    snapshot.worldGravity = controller.World().GetGravity();
    snapshot.worldFluidHeight = controller.World().GetFluidSurfaceHeight();
    snapshot.worldFluidDensity = controller.World().GetFluidDensity();
    snapshot.hasCinematicRenderingOverride = scene.hasCinematicRenderingOverride;
    snapshot.isCinematicRenderingEnabled = scene.isCinematicRenderingEnabled;
    snapshot.hasCinematicExposure = scene.hasCinematicExposure;
    snapshot.cinematicExposure = scene.cinematicExposure;
    snapshot.hasCinematicGamma = scene.hasCinematicGamma;
    snapshot.cinematicGamma = scene.cinematicGamma;
    snapshot.cinematicOverrideMask = scene.cinematicOverrideMask;
    snapshot.uiCinematicOverrideMask = scene.uiCinematicOverrideMask;
    snapshot.cinematicRender = scene.cinematicRender;
    snapshot.uiTimeScaleOverride = uiOverrides.timeScaleOverride;
    snapshot.uiModelCountOverride = uiOverrides.modelCountOverride;
    snapshot.uiSolverBallCountOverride = uiOverrides.solverBallCountOverride;
    snapshot.uiSolverBoxCountOverride = uiOverrides.solverBoxCountOverride;
    snapshot.trackBallRow = camera.trackBallRow;
    snapshot.trackHeight = camera.trackHeight;
    snapshot.autoCycleInterval = camera.autoCycleInterval;
    snapshot.autoCycleAccum = camera.autoCycleAccum;
    snapshot.autoCycleShotsTaken = camera.autoCycleShotsTaken;
    return snapshot;
}


void RestoreSceneRuntimeResetSnapshot( SceneController& controller,
                                       RuntimeRenderer& renderer,
                                       RunDebugState& debug,
                                       RunCameraState& camera,
                                       Physics::PhysicsDebugVisualizer& physicsDebugVisualizer,
                                       const SceneRuntimeResetSnapshot& snapshot,
                                       bool suppressExitOnComplete )
{
    RunSceneState& scene = controller.State();
    RunSceneUIOverrideState& uiOverrides = controller.UIOverrides();
    // Why: Interactive resets preserve the user's run-control choices, but
    // suppressing exit also forces automation-safe non-exit behavior.
    renderer.RestorePresentationSettings( snapshot.renderPresentation );
    controller.Physics().SetSleepEnabled( snapshot.physicsSleepEnabled );
    controller.Physics().SetTornadoFieldConfig( snapshot.tornadoField );
    controller.Physics().SetTornadoSystemConfig( snapshot.tornadoSystem );
    debug = snapshot.debug;
    scene.isScenePhysics = snapshot.isScenePhysics;
    scene.isSceneText = snapshot.isSceneText;
    scene.timeScale = snapshot.timeScale;
    scene.isFixedStep = snapshot.isFixedStep;
    scene.isInteractiveRun = snapshot.isInteractiveRun || suppressExitOnComplete;
    scene.isExitOnComplete = scene.isInteractiveRun ? false : snapshot.isExitOnComplete;
    scene.targetFrameCount = snapshot.targetFrameCount;
    scene.hasCinematicRenderingOverride = snapshot.hasCinematicRenderingOverride;
    scene.isCinematicRenderingEnabled = snapshot.isCinematicRenderingEnabled;
    scene.hasCinematicExposure = snapshot.hasCinematicExposure;
    scene.cinematicExposure = snapshot.cinematicExposure;
    scene.hasCinematicGamma = snapshot.hasCinematicGamma;
    scene.cinematicOverrideMask = snapshot.cinematicOverrideMask;
    scene.uiCinematicOverrideMask = snapshot.uiCinematicOverrideMask;
    scene.cinematicRender = snapshot.cinematicRender;
    uiOverrides.timeScaleOverride = snapshot.uiTimeScaleOverride;
    uiOverrides.modelCountOverride = snapshot.uiModelCountOverride;
    uiOverrides.solverBallCountOverride = snapshot.uiSolverBallCountOverride;
    uiOverrides.solverBoxCountOverride = snapshot.uiSolverBoxCountOverride;
    camera.trackHeight = snapshot.trackHeight;
    camera.trackBallRow.value = ( snapshot.trackBallRow.IsValid() && snapshot.trackBallRow.value < scene.modelCount )
                                    ? snapshot.trackBallRow.value
                                    : -1;
    camera.autoCycleInterval = snapshot.autoCycleInterval;
    camera.autoCycleAccum = snapshot.autoCycleAccum;
    camera.autoCycleShotsTaken = snapshot.autoCycleShotsTaken;
    physicsDebugVisualizer.SetFlags( debug.physicsDebugFlags );
    physicsDebugVisualizer.SetContactLingerSeconds( debug.physicsDebugContactLinger );
}


void ClearSceneRuntimeUIOverrides( SceneController& controller )
{
    // Concept: Reset-to-defaults hands authority back to authored scene data by
    // clearing UI-generated setup overrides.
    controller.UIOverrides().timeScaleOverride = 0.0f;
    controller.UIOverrides().modelCountOverride = -1;
    controller.UIOverrides().solverBallCountOverride = -1;
    controller.UIOverrides().solverBoxCountOverride = -1;
}

} // namespace Basics
} // namespace SkullbonezCore

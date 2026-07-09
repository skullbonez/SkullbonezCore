/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp
Purpose:
  Implements scene reset preserve/restore policy outside Run.

Mental model:
  Reset preservation belongs to scene loading, but it still borrows state that
  has not moved out of Run yet. Keep the mutation boundary explicit in
  SceneRuntimeResetContext until the remaining load phases are extracted.

Glossary:
  Reset snapshot: Copy of operator-owned runtime settings preserved across a
    same-scene reset.
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
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "SceneRuntimeReset.h"
#include "SceneRuntime.h"
#include "../Debug/PhysicsDebugVisualizer.h"
#include "../../World/WorldEnvironment.h"

namespace SkullbonezCore
{
namespace Basics
{
SceneRuntimeResetSnapshot CaptureSceneRuntimeResetSnapshot( const SceneRuntimeResetContext& context )
{
    SceneRuntimeResetSnapshot snapshot;
    // Invariant: Capture every field restored below. Adding a new preserved
    // runtime knob requires updating both sides of this snapshot contract.
    snapshot.runtimeSettings = context.runtimeSettings;
    snapshot.debug = context.debug;
    snapshot.isScenePhysics = context.scene.isScenePhysics;
    snapshot.isSceneText = context.scene.isSceneText;
    snapshot.isFixedStep = context.scene.isFixedStep;
    snapshot.isExitOnComplete = context.scene.isExitOnComplete;
    snapshot.isInteractiveRun = context.scene.isInteractiveRun;
    snapshot.targetFrameCount = context.scene.targetFrameCount;
    snapshot.timeScale = context.scene.timeScale;
    snapshot.worldGravity = context.worldEnvironment.GetGravity();
    snapshot.worldFluidHeight = context.worldEnvironment.GetFluidSurfaceHeight();
    snapshot.worldFluidDensity = context.worldEnvironment.GetFluidDensity();
    snapshot.hasCinematicRenderingOverride = context.scene.hasCinematicRenderingOverride;
    snapshot.isCinematicRenderingEnabled = context.scene.isCinematicRenderingEnabled;
    snapshot.hasCinematicExposure = context.scene.hasCinematicExposure;
    snapshot.cinematicExposure = context.scene.cinematicExposure;
    snapshot.hasCinematicGamma = context.scene.hasCinematicGamma;
    snapshot.cinematicGamma = context.scene.cinematicGamma;
    snapshot.cinematicOverrideMask = context.scene.cinematicOverrideMask;
    snapshot.uiCinematicOverrideMask = context.scene.uiCinematicOverrideMask;
    snapshot.cinematicRender = context.scene.cinematicRender;
    snapshot.uiTimeScaleOverride = context.uiOverrides.timeScaleOverride;
    snapshot.uiModelCountOverride = context.uiOverrides.modelCountOverride;
    snapshot.uiSolverBallCountOverride = context.uiOverrides.solverBallCountOverride;
    snapshot.uiSolverBoxCountOverride = context.uiOverrides.solverBoxCountOverride;
    snapshot.trackBallIndex = context.camera.trackBallIndex;
    snapshot.trackHeight = context.camera.trackHeight;
    snapshot.autoCycleInterval = context.camera.autoCycleInterval;
    snapshot.autoCycleAccum = context.camera.autoCycleAccum;
    snapshot.autoCycleShotsTaken = context.camera.autoCycleShotsTaken;
    return snapshot;
}


void RestoreSceneRuntimeResetSnapshot( SceneRuntimeResetContext& context,
                                       const SceneRuntimeResetSnapshot& snapshot,
                                       bool suppressExitOnComplete )
{
    // Why: Interactive resets preserve the user's run-control choices, but
    // suppressing exit also forces automation-safe non-exit behavior.
    context.runtimeSettings = snapshot.runtimeSettings;
    context.debug = snapshot.debug;
    context.scene.isScenePhysics = snapshot.isScenePhysics;
    context.scene.isSceneText = snapshot.isSceneText;
    context.scene.timeScale = snapshot.timeScale;
    context.scene.isFixedStep = snapshot.isFixedStep;
    context.scene.isInteractiveRun = snapshot.isInteractiveRun || suppressExitOnComplete;
    context.scene.isExitOnComplete = context.scene.isInteractiveRun ? false : snapshot.isExitOnComplete;
    context.scene.targetFrameCount = snapshot.targetFrameCount;
    context.scene.hasCinematicRenderingOverride = snapshot.hasCinematicRenderingOverride;
    context.scene.isCinematicRenderingEnabled = snapshot.isCinematicRenderingEnabled;
    context.scene.hasCinematicExposure = snapshot.hasCinematicExposure;
    context.scene.cinematicExposure = snapshot.cinematicExposure;
    context.scene.hasCinematicGamma = snapshot.hasCinematicGamma;
    context.scene.cinematicGamma = snapshot.cinematicGamma;
    context.scene.cinematicOverrideMask = snapshot.cinematicOverrideMask;
    context.scene.uiCinematicOverrideMask = snapshot.uiCinematicOverrideMask;
    context.scene.cinematicRender = snapshot.cinematicRender;
    context.uiOverrides.timeScaleOverride = snapshot.uiTimeScaleOverride;
    context.uiOverrides.modelCountOverride = snapshot.uiModelCountOverride;
    context.uiOverrides.solverBallCountOverride = snapshot.uiSolverBallCountOverride;
    context.uiOverrides.solverBoxCountOverride = snapshot.uiSolverBoxCountOverride;
    context.camera.trackHeight = snapshot.trackHeight;
    context.camera.trackBallIndex =
        ( snapshot.trackBallIndex >= 0 && snapshot.trackBallIndex < context.scene.modelCount ) ? snapshot.trackBallIndex
                                                                                               : -1;
    context.camera.autoCycleInterval = snapshot.autoCycleInterval;
    context.camera.autoCycleAccum = snapshot.autoCycleAccum;
    context.camera.autoCycleShotsTaken = snapshot.autoCycleShotsTaken;
    context.physicsDebugVisualizer.SetFlags( context.debug.physicsDebugFlags );
    context.physicsDebugVisualizer.SetContactLingerSeconds( context.debug.physicsDebugContactLinger );
}


void ClearSceneRuntimeUIOverrides( SceneRuntimeResetContext& context )
{
    // Concept: Reset-to-defaults hands authority back to authored scene data by
    // clearing UI-generated setup overrides.
    context.uiOverrides.timeScaleOverride = 0.0f;
    context.uiOverrides.modelCountOverride = -1;
    context.uiOverrides.solverBallCountOverride = -1;
    context.uiOverrides.solverBoxCountOverride = -1;
}

} // namespace Basics
} // namespace SkullbonezCore

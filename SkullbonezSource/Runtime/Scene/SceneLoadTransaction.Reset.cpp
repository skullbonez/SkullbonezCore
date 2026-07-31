/*
File: SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp
Purpose:
  Implements scene reset preserve/restore policy outside Run.

Summary:
  Reset preservation belongs to scene loading. SceneController supplies its
  owned state directly while the remaining value owners are explicit function
  arguments; no mutable multi-domain context is retained or stored.

Glossary:
  Suppress exit: Interactive-run flag that prevents automation from quitting.

Invariants:
  - Capture and restore must stay field-complete for every preserved setting.
  - Restore clamps camera tracking to the rebuilt model count.
  - Reset-to-defaults clears UI overrides instead of restoring them.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneLoadTransaction.h"
#include "SceneResetPreservation.h"
#include "SceneController.h"
#include "SceneSessionState.h"
#include "../Render/RuntimeRenderer.h"
#include "../../World/WorldEnvironment.h"

namespace SkullbonezCore
{
namespace Runtime
{
SceneResetPreservationSnapshot SceneLoadTransaction::CaptureResetSnapshot( const SceneController& controller, const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                                           const RuntimeRenderer& renderer, const OverlayDebugState& debug, const CameraControlState& camera )
{
    SceneResetPreservationSnapshot snapshot;
    const SceneSessionState& scene = controller.State();

    // Invariant: Capture every field restored below. Adding a new preserved
    // runtime knob requires updating both sides of this snapshot contract.
    snapshot.renderPresentation = renderer.PresentationSettings();
    snapshot.physicsSleepEnabled = controller.Scene().Physics().IsSleepEnabled();
    snapshot.tornadoField = controller.Scene().Tornado().GetFieldConfig();
    snapshot.tornadoSystem = controller.Scene().Tornado().GetSystemConfig();
    snapshot.tornadoVisual = controller.Scene().Tornado().VisualSettings();
    snapshot.debug = debug;
    snapshot.isScenePhysics = scene.isScenePhysics;
    snapshot.isSceneText = scene.isSceneText;
    snapshot.isFixedStep = scene.isFixedStep;
    snapshot.isExitOnComplete = scene.isExitOnComplete;
    snapshot.isInteractiveRun = scene.isInteractiveRun;
    snapshot.targetFrameCount = scene.targetFrameCount;
    snapshot.timeScale = scene.timeScale;
    snapshot.worldGravity = controller.Scene().Environment().GetGravity();
    snapshot.worldFluidHeight = controller.Scene().Environment().GetFluidSurfaceHeight();
    snapshot.worldFluidDensity = controller.Scene().Environment().GetFluidDensity();
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


void SceneLoadTransaction::RestoreResetSnapshot( SceneController& controller,
                                                 SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                 RuntimeRenderer& renderer, OverlayDebugState& debug,
                                                 CameraControlState& camera, const SceneResetPreservationSnapshot& snapshot,
                                                 bool suppressExitOnComplete )
{
    SceneSessionState& scene = controller.State();

    // Why: Interactive resets preserve the user's run-control choices, but
    // suppressing exit also forces automation-safe non-exit behavior.
    renderer.RestorePresentationSettings( snapshot.renderPresentation );
    controller.Scene().Physics().SetSleepEnabled( snapshot.physicsSleepEnabled );
    controller.Scene().Tornado().SetFieldConfig( snapshot.tornadoField );
    controller.Scene().Tornado().SetSystemConfig( snapshot.tornadoSystem );
    controller.Scene().Tornado().SetVisualSettings( snapshot.tornadoVisual );
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
}


void SceneLoadTransaction::ClearUiOverrides( SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides )
{

    // Concept: Reset-to-defaults hands authority back to authored scene data by
    // clearing UI-generated setup overrides.
    uiOverrides.timeScaleOverride = 0.0f;
    uiOverrides.modelCountOverride = -1;
    uiOverrides.solverBallCountOverride = -1;
    uiOverrides.solverBoxCountOverride = -1;
}

} // namespace Runtime
} // namespace SkullbonezCore

# Naming N2 Comment-Style Audit

Date: 2026-07-18
Scope: every source-bearing file changed by the N2 render-instance and capacity-vocabulary rename
Guide: `Agentic/Reference/comment-style-guide.md`

## Result

79 checked, 0 deferred, 0 unchecked.

Every checked C++ or tool source retains the complete learning-header structure:
`Purpose`, `Summary` or `Mental model`, `Glossary`, `Invariants`, and
`Related`. Dense owners retain nearby `Concept:`, `Why:`, `Invariant:`,
`Lifetime:`, and `Hazard:` comments where their existing logic needs them.

N2 changes no algorithm, storage size, unit, ownership lifetime, allocation
phase, replay ordering, physics arithmetic, render command, or error lane. The
body diff consists of:

- the ratified `RenderInstanceRenderer` file/type/namespace rename;
- `GameModel` capacity identifiers becoming scene-object identifiers across
  the same fixed stores and value records;
- one operator UI heading and one capacity diagnostic adopting scene-object
  vocabulary;
- two legacy generated-camera identities expressed as their exact existing
  FNV-1a values (`0x76EECD4F` and `0x77EECEE2`) so source vocabulary changes
  without changing lookup identity; and
- repository formatting/alignment caused only by the longer identifiers.

No new dense or risky code path therefore needs a new local teaching comment.
The camera constants received an explicit invariant explaining why their
numeric identities may not change.

## Checked Inventory

- [x] `SkullbonezSource/Assets/AssetKeys.h`
- [x] `SkullbonezSource/Core/Config.cpp`
- [x] `SkullbonezSource/Core/Config.h`
- [x] `SkullbonezSource/Maths/Frustum.h`
- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/PhysicsApi.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`
- [x] `SkullbonezSource/Physics/PhysicsScene.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
- [x] `SkullbonezSource/Physics/TornadoGameplay.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.h`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Runtime/Audio/ContactAudioService.cpp`
- [x] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayEventCommand.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayIdentity.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionArchive.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.Probes.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/RunCameraState.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunStartupState.h`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneCapacity.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp`
- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UILayout.cpp`
- [x] `SkullbonezSource/UI/UILayout.h`
- [x] `SkullbonezSource/UI/UITabControls.cpp`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.h`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `SkullbonezTests/TestPersistentContactSolver.cpp`
- [x] `SkullbonezTests/TestReplayRecorder.cpp`
- [x] `SkullbonezTests/TestSolverBroadphaseStage.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`
- [x] `tools/validate_project_filters.py`

The inventory comes from `git diff HEAD --name-only --diff-filter=ACMR`, not
`rg`, so the tracked ignored `Runtime/Debug/CollisionVisualizer.cpp` row is
included. That file was the sole stale identifier found by the first focused
build and is checked above.

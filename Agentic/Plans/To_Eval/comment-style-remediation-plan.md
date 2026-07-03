# Comment Style Remediation Plan

This plan tracks the repository-wide pass to bring source comments up to the
Skullbonez comment standard in `Agentic/Reference/comment-style-guide.md`.

## Current Baseline

Audit date: 2026-06-27.

Metric used for the baseline: comment-bearing nonblank lines divided by
nonblank lines. This is only a triage signal; checklist completion depends on
human-quality inspection against the guide, not a percentage target.

| Subsystem | Files | Baseline comment coverage | Learning-header coverage |
|-----------|------:|--------------------------:|-------------------------:|
| Runtime / Editor | 8 | 2.8% | 87.5% |
| Runtime / Tools | 2 | 3.0% | 0.0% |
| Runtime / Replay | 14 | 3.7% | 71.4% |
| Scene | 5 | 4.7% | 100.0% |
| Runtime / Diagnostics | 2 | 6.6% | 50.0% |
| Assets | 4 | 7.6% | 100.0% |
| UI | 52 | 8.7% | 96.2% |
| Runtime / Scene | 23 | 2.4% | 100.0% |
| Runtime / Core | 50 | 10.4% | 86.0% |
| Core | 21 | 14.8% | 100.0% |
| Physics | 40 | 17.1% | 85.0% |
| GameObjects | 8 | 17.5% | 100.0% |
| Physics / Debug | 6 | 19.1% | 100.0% |
| Rendering | 25 | 21.1% | 100.0% |
| World | 7 | 24.5% | 100.0% |
| Rendering / DX12 | 25 | 26.3% | 100.0% |
| Runtime / Render Host | 6 | 29.1% | 0.0% |
| Maths | 11 | 41.8% | 100.0% |
| Shaders / HLSL | 21 | 36.7% | 90.5% |
| Tools / Validation and Scripts | 66 | 6.1% | 83.3% |
| Agentic / Test Harnesses | 3 | 5.2% | 100.0% |

## Completion Rules

- Work subsystem by subsystem. Do not mix a comment remediation pass with
  behavior-changing refactors.
- Before a subsystem pass, rerun the scoped `git ls-files` inventory and compare
  it against this checklist. Add any newly tracked files before editing.
- Tick a file only after it has been inspected against
  `Agentic/Skills/comment-style-audit/skill.md`.
- A learning header alone is not enough. Dense or risky bodies need local
  concept, reason, invariant, lifetime, hazard, unit, and validation comments.
- Leave intentionally deferred files unchecked and add `(Deferred: reason)` on
  the same line.
- Final handoff for each subsystem must include checked count, deferred count,
  unchecked files, and validation status.

## Completeness Validation

Use `git ls-files` for inventory. Do not use `rg` as the source of truth because
tracked files under ignored directory names, such as `SkullbonezSource/Physics/Debug`,
can be missed.

PowerShell inventory shape:

```powershell
git ls-files SkullbonezSource SkullbonezData/shaders tools Agentic/Tests |
  Where-Object { $_ -match '\.(cpp|c|h|hpp|inl|hlsl|py|bat|ps1|cs)$' }
```

For a single subsystem, scope the first path argument to that subsystem folder.
The count returned by the command must match the count in the relevant checklist
heading before the pass starts and before it is reported complete.

Comment-only source changes require no repository validation once the final diff
is confirmed to contain only comments/docs. If any code behavior changes, stop
and use the validation map in `AGENTS.md`.

## Recommended Order

1. Runtime / Replay
2. Runtime / Editor
3. Runtime / Tools
4. Runtime / Diagnostics
5. Scene and Runtime / Scene
6. UI
7. Runtime / Core
8. Physics and Physics / Debug
9. Rendering / DX12 and Rendering
10. Core, GameObjects, Assets, World, Maths
11. Shaders / HLSL
12. Tools / Validation and Scripts
13. Agentic / Test Harnesses

## Runtime / Replay (14 files)

- [x] `SkullbonezSource/Runtime/Replay/ReplayExporter.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayExporter.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`

## Runtime / Editor (8 files)

- [x] `SkullbonezSource/Runtime/Editor/EditorHullAssets.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.h`
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.h`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`

## Runtime / Tools (2 files)

- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`

## Scene (5 files)

- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.h`
- [x] `SkullbonezSource/Scene/TestScene.cpp`
- [x] `SkullbonezSource/Scene/TestScene.h`
- [x] `SkullbonezSource/Scene/TestSceneParser.cpp`

## Runtime / Diagnostics (2 files)

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`

## Assets (4 files)

- [x] `SkullbonezSource/Assets/AssetSystem.cpp`
- [x] `SkullbonezSource/Assets/AssetSystem.h`
- [x] `SkullbonezSource/Assets/TextureCollection.cpp`
- [x] `SkullbonezSource/Assets/TextureCollection.h`

## UI (52 files)

- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UIBackdropBlur.cpp`
- [x] `SkullbonezSource/UI/UIBackdropBlur.h`
- [x] `SkullbonezSource/UI/UIButton.cpp`
- [x] `SkullbonezSource/UI/UIButton.h`
- [x] `SkullbonezSource/UI/UICache.cpp`
- [x] `SkullbonezSource/UI/UICache.h`
- [x] `SkullbonezSource/UI/UICheckBox.cpp`
- [x] `SkullbonezSource/UI/UICheckBox.h`
- [x] `SkullbonezSource/UI/UIComboBox.cpp`
- [x] `SkullbonezSource/UI/UIComboBox.h`
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezSource/UI/UIDraw.cpp`
- [x] `SkullbonezSource/UI/UIDraw.h`
- [x] `SkullbonezSource/UI/UIDrawList.cpp`
- [x] `SkullbonezSource/UI/UIDrawList.h`
- [x] `SkullbonezSource/UI/UIDrawWidgets.cpp`
- [x] `SkullbonezSource/UI/UIDrawWidgets.h`
- [x] `SkullbonezSource/UI/UIIconButton.cpp`
- [x] `SkullbonezSource/UI/UIIconButton.h`
- [x] `SkullbonezSource/UI/UIInput.cpp`
- [x] `SkullbonezSource/UI/UIInput.h`
- [x] `SkullbonezSource/UI/UILayout.cpp`
- [x] `SkullbonezSource/UI/UILayout.h`
- [x] `SkullbonezSource/UI/UIScrollBar.cpp`
- [x] `SkullbonezSource/UI/UIScrollBar.h`
- [x] `SkullbonezSource/UI/UISlider.cpp`
- [x] `SkullbonezSource/UI/UISlider.h`
- [x] `SkullbonezSource/UI/UIState.h`
- [x] `SkullbonezSource/UI/UIStyle.cpp`
- [x] `SkullbonezSource/UI/UIStyle.h`
- [x] `SkullbonezSource/UI/UITabBar.cpp`
- [x] `SkullbonezSource/UI/UITabBar.h`
- [x] `SkullbonezSource/UI/UITabCinematic.cpp`
- [x] `SkullbonezSource/UI/UITabCinematic.h`
- [x] `SkullbonezSource/UI/UITabControls.cpp`
- [x] `SkullbonezSource/UI/UITabControls.h`
- [x] `SkullbonezSource/UI/UITabEditor.cpp`
- [x] `SkullbonezSource/UI/UITabEditor.h`
- [x] `SkullbonezSource/UI/UITabOptions.cpp`
- [x] `SkullbonezSource/UI/UITabOptions.h`
- [x] `SkullbonezSource/UI/UITabPhysics.cpp`
- [x] `SkullbonezSource/UI/UITabPhysics.h`
- [x] `SkullbonezSource/UI/UITabProfiler.cpp`
- [x] `SkullbonezSource/UI/UITabProfiler.h`
- [x] `SkullbonezSource/UI/UITabScene.cpp`
- [x] `SkullbonezSource/UI/UITabScene.h`
- [x] `SkullbonezSource/UI/UITabSky.cpp`
- [x] `SkullbonezSource/UI/UITabSky.h`
- [x] `SkullbonezSource/UI/UIWindowChrome.cpp`
- [x] `SkullbonezSource/UI/UIWindowChrome.h`

## Runtime / Scene (23 files)

- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h`

## Runtime / Core (50 files)

- [x] `SkullbonezSource/Runtime/Camera.cpp`
- [x] `SkullbonezSource/Runtime/Camera.h`
- [x] `SkullbonezSource/Runtime/CameraCollection.cpp`
- [x] `SkullbonezSource/Runtime/CameraCollection.h`
- [x] `SkullbonezSource/Runtime/CaptureController.cpp`
- [x] `SkullbonezSource/Runtime/CaptureController.h`
- [x] `SkullbonezSource/Runtime/CaptureSystem.cpp`
- [x] `SkullbonezSource/Runtime/CaptureSystem.h`
- [x] `SkullbonezSource/Runtime/DiagnosticsController.cpp`
- [x] `SkullbonezSource/Runtime/DiagnosticsController.h`
- [x] `SkullbonezSource/Runtime/EngineContext.cpp`
- [x] `SkullbonezSource/Runtime/EngineContext.h`
- [x] `SkullbonezSource/Runtime/Init.cpp`
- [x] `SkullbonezSource/Runtime/Input.cpp`
- [x] `SkullbonezSource/Runtime/Input.h`
- [x] `SkullbonezSource/Runtime/InputController.cpp`
- [x] `SkullbonezSource/Runtime/InputController.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunCapture.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunInput.cpp`
- [x] `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`
- [x] `SkullbonezSource/Runtime/RunInternal.h`
- [x] `SkullbonezSource/Runtime/RunLiveStyle.cpp`
- [x] `SkullbonezSource/Runtime/RunPasses.cpp`
- [x] `SkullbonezSource/Runtime/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/RunReplayProbeState.h`
- [x] `SkullbonezSource/Runtime/RunState.h`
- [x] `SkullbonezSource/Runtime/RunStress.cpp`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeCameraMode.h`
- [x] `SkullbonezSource/Runtime/RuntimeCommandQueue.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeCommandQueue.h`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.h`
- [x] `SkullbonezSource/Runtime/RuntimeFileWriter.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeFileWriter.h`
- [x] `SkullbonezSource/Runtime/RuntimeInteractionController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeInteractionController.h`
- [x] `SkullbonezSource/Runtime/RuntimePickService.cpp`
- [x] `SkullbonezSource/Runtime/RuntimePickService.h`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.h`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.h`
- [x] `SkullbonezSource/Runtime/SimulationController.cpp`
- [x] `SkullbonezSource/Runtime/SimulationController.h`
- [x] `SkullbonezSource/Runtime/Window.cpp`
- [x] `SkullbonezSource/Runtime/Window.h`

## Core (21 files)

- [x] `SkullbonezSource/Core/AmortizedTask.cpp`
- [x] `SkullbonezSource/Core/AmortizedTask.h`
- [x] `SkullbonezSource/Core/Common.h`
- [x] `SkullbonezSource/Core/Config.cpp`
- [x] `SkullbonezSource/Core/Config.h`
- [x] `SkullbonezSource/Core/Fence.h`
- [x] `SkullbonezSource/Core/LockOrderValidator.cpp`
- [x] `SkullbonezSource/Core/LockOrderValidator.h`
- [x] `SkullbonezSource/Core/Log.cpp`
- [x] `SkullbonezSource/Core/Log.h`
- [x] `SkullbonezSource/Core/MainMemoryStats.h`
- [x] `SkullbonezSource/Core/PlatformProfiler.cpp`
- [x] `SkullbonezSource/Core/PlatformProfiler.h`
- [x] `SkullbonezSource/Core/Profiler.cpp`
- [x] `SkullbonezSource/Core/Profiler.h`
- [x] `SkullbonezSource/Core/SkullScope.cpp`
- [x] `SkullbonezSource/Core/SkullScope.h`
- [x] `SkullbonezSource/Core/Timer.cpp`
- [x] `SkullbonezSource/Core/Timer.h`
- [x] `SkullbonezSource/Core/WorkerPool.cpp`
- [x] `SkullbonezSource/Core/WorkerPool.h`

## Physics (40 files)

- [x] `SkullbonezSource/Physics/BoundingBox.cpp`
- [x] `SkullbonezSource/Physics/BoundingBox.h`
- [x] `SkullbonezSource/Physics/BoundingSphere.cpp`
- [x] `SkullbonezSource/Physics/BoundingSphere.h`
- [x] `SkullbonezSource/Physics/ColliderStore.cpp`
- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/CollisionShape.h`
- [x] `SkullbonezSource/Physics/ContactSolverCommon.h`
- [x] `SkullbonezSource/Physics/ConvexHullShape.cpp`
- [x] `SkullbonezSource/Physics/ConvexHullShape.h`
- [x] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [x] `SkullbonezSource/Physics/ObjectContactManifold.h`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsHandles.h`
- [x] `SkullbonezSource/Physics/PhysicsMass.h`
- [x] `SkullbonezSource/Physics/PhysicsScene.cpp`
- [x] `SkullbonezSource/Physics/PhysicsScene.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/Ragdoll.cpp`
- [x] `SkullbonezSource/Physics/Ragdoll.h`
- [x] `SkullbonezSource/Physics/ResponseInformation.h`
- [x] `SkullbonezSource/Physics/RigidBody.cpp`
- [x] `SkullbonezSource/Physics/RigidBody.h`
- [x] `SkullbonezSource/Physics/SimulationSystem.cpp`
- [x] `SkullbonezSource/Physics/SimulationSystem.h`
- [x] `SkullbonezSource/Physics/SleepIslandSystem.cpp`
- [x] `SkullbonezSource/Physics/SleepIslandSystem.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/TornadoField.cpp`
- [x] `SkullbonezSource/Physics/TornadoField.h`

## GameObjects (8 files)

- [x] `SkullbonezSource/GameObjects/GameModel.cpp`
- [x] `SkullbonezSource/GameObjects/GameModel.h`
- [x] `SkullbonezSource/GameObjects/GameModelCollection.cpp`
- [x] `SkullbonezSource/GameObjects/GameModelCollection.h`
- [x] `SkullbonezSource/GameObjects/GameModelSoACache.cpp`
- [x] `SkullbonezSource/GameObjects/GameModelSoACache.h`
- [x] `SkullbonezSource/GameObjects/GameModelStreams.cpp`
- [x] `SkullbonezSource/GameObjects/GameModelStreams.h`

## Physics / Debug (6 files)

- [x] `SkullbonezSource/Physics/Debug/BroadphaseVisualizer.cpp`
- [x] `SkullbonezSource/Physics/Debug/BroadphaseVisualizer.h`
- [x] `SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp`
- [x] `SkullbonezSource/Physics/Debug/CollisionVisualizer.h`
- [x] `SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp`
- [x] `SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h`

## Rendering (25 files)

- [x] `SkullbonezSource/Rendering/DrawCallTrace.cpp`
- [x] `SkullbonezSource/Rendering/DrawCallTrace.h`
- [x] `SkullbonezSource/Rendering/GameModelRenderer.cpp`
- [x] `SkullbonezSource/Rendering/GameModelRenderer.h`
- [x] `SkullbonezSource/Rendering/Helper.cpp`
- [x] `SkullbonezSource/Rendering/Helper.h`
- [x] `SkullbonezSource/Rendering/IFramebuffer.h`
- [x] `SkullbonezSource/Rendering/IMesh.h`
- [x] `SkullbonezSource/Rendering/IRenderBackend.cpp`
- [x] `SkullbonezSource/Rendering/IRenderBackend.h`
- [x] `SkullbonezSource/Rendering/IShader.h`
- [x] `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h`
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp`
- [x] `SkullbonezSource/Rendering/RenderGraph.h`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Rendering/RenderMaterial.h`
- [x] `SkullbonezSource/Rendering/RenderPipeline.cpp`
- [x] `SkullbonezSource/Rendering/RenderPipeline.h`
- [x] `SkullbonezSource/Rendering/RenderSceneSnapshot.h`
- [x] `SkullbonezSource/Rendering/RenderSceneView.h` (deleted 2026-07-04 with the one-implementation render-view artifact)
- [x] `SkullbonezSource/Rendering/ShaderContracts.h`
- [x] `SkullbonezSource/Rendering/Shadow.h`
- [x] `SkullbonezSource/Rendering/Text.cpp`
- [x] `SkullbonezSource/Rendering/Text.h`

## World (7 files)

- [x] `SkullbonezSource/World/SkyBox.cpp`
- [x] `SkullbonezSource/World/SkyBox.h`
- [x] `SkullbonezSource/World/Terrain.cpp`
- [x] `SkullbonezSource/World/Terrain.h`
- [x] `SkullbonezSource/World/TerrainSupportClassifier.h`
- [x] `SkullbonezSource/World/WorldEnvironment.cpp`
- [x] `SkullbonezSource/World/WorldEnvironment.h`

## Rendering / DX12 (25 files)

- [x] `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/BLASDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`
- [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/SBTDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/SBTDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/TLASDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/TLASDX12.h`

## Runtime / Render Host (6 files)

- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`

## Maths (11 files)

- [x] `SkullbonezSource/Maths/GeometricMath.cpp`
- [x] `SkullbonezSource/Maths/GeometricMath.h`
- [x] `SkullbonezSource/Maths/GeometricStructures.h`
- [x] `SkullbonezSource/Maths/Matrix4.cpp`
- [x] `SkullbonezSource/Maths/Matrix4.h`
- [x] `SkullbonezSource/Maths/Quaternion.cpp`
- [x] `SkullbonezSource/Maths/Quaternion.h`
- [x] `SkullbonezSource/Maths/RotationMatrix.cpp`
- [x] `SkullbonezSource/Maths/RotationMatrix.h`
- [x] `SkullbonezSource/Maths/Vector3.cpp`
- [x] `SkullbonezSource/Maths/Vector3.h`

## Shaders / HLSL (21 files)

- [x] `SkullbonezData/shaders/UIBackdropBlur.hlsl`
- [x] `SkullbonezData/shaders/collision_visualizer.hlsl`
- [x] `SkullbonezData/shaders/generate_mips.hlsl`
- [x] `SkullbonezData/shaders/grid_line.hlsl`
- [x] `SkullbonezData/shaders/launcher_laser.hlsl`
- [x] `SkullbonezData/shaders/lit_textured.hlsl`
- [x] `SkullbonezData/shaders/lit_textured_instanced.hlsl`
- [x] `SkullbonezData/shaders/post_tonemap.hlsl`
- [x] `SkullbonezData/shaders/post_volumetric_light.hlsl`
- [x] `SkullbonezData/shaders/reflect.rt.hlsl`
- [x] `SkullbonezData/shaders/shadow_depth.hlsl`
- [x] `SkullbonezData/shaders/shadow_depth_instanced.hlsl`
- [x] `SkullbonezData/shaders/sky_atmosphere.hlsl`
- [x] `SkullbonezData/shaders/solid_color.hlsl`
- [x] `SkullbonezData/shaders/solid_color_batch.hlsl`
- [x] `SkullbonezData/shaders/text.hlsl`
- [x] `SkullbonezData/shaders/tornado_fx.hlsl`
- [x] `SkullbonezData/shaders/ui_render_target_preview.hlsl`
- [x] `SkullbonezData/shaders/unlit_textured.hlsl`
- [x] `SkullbonezData/shaders/water_calm.hlsl`
- [x] `SkullbonezData/shaders/water_ocean.hlsl`

## Tools / Validation and Scripts (66 files)

- [x] `tools/agent_validate.bat`
- [x] `tools/align_header_inline_comments.py`
- [x] `tools/archive_validation_artifacts.bat`
- [x] `tools/archive_validation_artifacts.py`
- [x] `tools/bake_hulls.bat`
- [x] `tools/bake_hulls.py`
- [x] `tools/capture_ui_screenshot.bat`
- [x] `tools/check_dx12_baselines.py`
- [x] `tools/check_dx12_validation.bat`
- [x] `tools/check_physics_known_issue_regression.py`
- [x] `tools/check_physics_query_regression.py`
- [x] `tools/check_physics_regression.py`
- [x] `tools/check_replay_scrub_regression.py`
- [x] `tools/check_replay_v2_artifact.py`
- [x] `tools/check_runtime_boundaries.py`
- [x] `tools/check_shooting_reaction.py`
- [x] `tools/check_ui_blur.py`
- [x] `tools/export_screenshot_png.py`
- [x] `tools/find_clang_format.bat`
- [x] `tools/find_git.bat`
- [x] `tools/find_msbuild.bat`
- [x] `tools/find_python.bat`
- [x] `tools/format_fix.bat`
- [x] `tools/loc_count.bat`
- [x] `tools/physics_query.bat`
- [x] `tools/physics_query.py`
- [x] `tools/refresh_hulls.bat`
- [x] `tools/replay_query.bat`
- [x] `tools/replay_query.py`
- [x] `tools/style_harness.bat`
- [x] `tools/style_harness.ps1`
- [x] `tools/update_baselines.bat`
- [x] `tools/update_baselines.py`
- [x] `tools/validate_build.bat`
- [x] `tools/validate_concepts.bat`
- [x] `tools/validate_concepts.py`
- [x] `tools/validate_deep.bat`
- [x] `tools/validate_demo_stress.bat`
- [x] `tools/validate_dx12_arch_tests.bat`
- [x] `tools/validate_dx12_renderer.bat`
- [x] `tools/validate_fast.bat`
- [x] `tools/validate_format.bat`
- [x] `tools/validate_full.bat`
- [x] `tools/validate_interaction_clicks.bat`
- [x] `tools/validate_perf.bat`
- [x] `tools/validate_physics.bat`
- [x] `tools/validate_physics_deep.bat`
- [x] `tools/validate_physics_query.bat`
- [x] `tools/validate_project_filters.bat`
- [x] `tools/validate_project_filters.py`
- [x] `tools/validate_ready_builds.bat`
- [x] `tools/validate_renderers.bat`
- [x] `tools/validate_replay_scrub.bat`
- [x] `tools/validate_replay_v2_artifact.bat`
- [x] `tools/validate_runtime_boundaries.bat`
- [x] `tools/validate_runtime_interaction_policy.bat`
- [x] `tools/validate_scene_loads.bat`
- [x] `tools/validate_scene_loads.py`
- [x] `tools/validate_scene_parser_tests.bat`
- [x] `tools/validate_select.bat`
- [x] `tools/validate_shaders.bat`
- [x] `tools/validate_shaders.py`
- [x] `tools/validate_ui.bat`
- [x] `tools/validate_ui_stress.bat`
- [x] `tools/watch_demo_stress.bat`
- [x] `tools/watch_ui_stress.bat`

## Agentic / Test Harnesses (3 files)

- [x] `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- [x] `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp`
- [x] `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp`

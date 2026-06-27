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
| Runtime / Scene | 15 | 9.6% | 33.3% |
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

- [ ] `SkullbonezSource/Runtime/Replay/ReplayExporter.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayExporter.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRecorder.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h`
- [ ] `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`

## Runtime / Editor (8 files)

- [ ] `SkullbonezSource/Runtime/Editor/EditorHullAssets.h`
- [ ] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`
- [ ] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [ ] `SkullbonezSource/Runtime/Editor/EditorTools.h`
- [ ] `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`
- [ ] `SkullbonezSource/Runtime/Editor/LauncherLaser.h`
- [ ] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [ ] `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`

## Runtime / Tools (2 files)

- [ ] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [ ] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`

## Scene (5 files)

- [ ] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [ ] `SkullbonezSource/Scene/SceneSnapshotWriter.h`
- [ ] `SkullbonezSource/Scene/TestScene.cpp`
- [ ] `SkullbonezSource/Scene/TestScene.h`
- [ ] `SkullbonezSource/Scene/TestSceneParser.cpp`

## Runtime / Diagnostics (2 files)

- [ ] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [ ] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`

## Assets (4 files)

- [ ] `SkullbonezSource/Assets/AssetSystem.cpp`
- [ ] `SkullbonezSource/Assets/AssetSystem.h`
- [ ] `SkullbonezSource/Assets/TextureCollection.cpp`
- [ ] `SkullbonezSource/Assets/TextureCollection.h`

## UI (52 files)

- [ ] `SkullbonezSource/UI/UI.cpp`
- [ ] `SkullbonezSource/UI/UI.h`
- [ ] `SkullbonezSource/UI/UIBackdropBlur.cpp`
- [ ] `SkullbonezSource/UI/UIBackdropBlur.h`
- [ ] `SkullbonezSource/UI/UIButton.cpp`
- [ ] `SkullbonezSource/UI/UIButton.h`
- [ ] `SkullbonezSource/UI/UICache.cpp`
- [ ] `SkullbonezSource/UI/UICache.h`
- [ ] `SkullbonezSource/UI/UICheckBox.cpp`
- [ ] `SkullbonezSource/UI/UICheckBox.h`
- [ ] `SkullbonezSource/UI/UIComboBox.cpp`
- [ ] `SkullbonezSource/UI/UIComboBox.h`
- [ ] `SkullbonezSource/UI/UICommands.h`
- [ ] `SkullbonezSource/UI/UIDraw.cpp`
- [ ] `SkullbonezSource/UI/UIDraw.h`
- [ ] `SkullbonezSource/UI/UIDrawList.cpp`
- [ ] `SkullbonezSource/UI/UIDrawList.h`
- [ ] `SkullbonezSource/UI/UIDrawWidgets.cpp`
- [ ] `SkullbonezSource/UI/UIDrawWidgets.h`
- [ ] `SkullbonezSource/UI/UIIconButton.cpp`
- [ ] `SkullbonezSource/UI/UIIconButton.h`
- [ ] `SkullbonezSource/UI/UIInput.cpp`
- [ ] `SkullbonezSource/UI/UIInput.h`
- [ ] `SkullbonezSource/UI/UILayout.cpp`
- [ ] `SkullbonezSource/UI/UILayout.h`
- [ ] `SkullbonezSource/UI/UIScrollBar.cpp`
- [ ] `SkullbonezSource/UI/UIScrollBar.h`
- [ ] `SkullbonezSource/UI/UISlider.cpp`
- [ ] `SkullbonezSource/UI/UISlider.h`
- [ ] `SkullbonezSource/UI/UIState.h`
- [ ] `SkullbonezSource/UI/UIStyle.cpp`
- [ ] `SkullbonezSource/UI/UIStyle.h`
- [ ] `SkullbonezSource/UI/UITabBar.cpp`
- [ ] `SkullbonezSource/UI/UITabBar.h`
- [ ] `SkullbonezSource/UI/UITabCinematic.cpp`
- [ ] `SkullbonezSource/UI/UITabCinematic.h`
- [ ] `SkullbonezSource/UI/UITabControls.cpp`
- [ ] `SkullbonezSource/UI/UITabControls.h`
- [ ] `SkullbonezSource/UI/UITabEditor.cpp`
- [ ] `SkullbonezSource/UI/UITabEditor.h`
- [ ] `SkullbonezSource/UI/UITabOptions.cpp`
- [ ] `SkullbonezSource/UI/UITabOptions.h`
- [ ] `SkullbonezSource/UI/UITabPhysics.cpp`
- [ ] `SkullbonezSource/UI/UITabPhysics.h`
- [ ] `SkullbonezSource/UI/UITabProfiler.cpp`
- [ ] `SkullbonezSource/UI/UITabProfiler.h`
- [ ] `SkullbonezSource/UI/UITabScene.cpp`
- [ ] `SkullbonezSource/UI/UITabScene.h`
- [ ] `SkullbonezSource/UI/UITabSky.cpp`
- [ ] `SkullbonezSource/UI/UITabSky.h`
- [ ] `SkullbonezSource/UI/UIWindowChrome.cpp`
- [ ] `SkullbonezSource/UI/UIWindowChrome.h`

## Runtime / Scene (15 files)

- [ ] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntime.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp`
- [ ] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h`

## Runtime / Core (50 files)

- [ ] `SkullbonezSource/Runtime/Camera.cpp`
- [ ] `SkullbonezSource/Runtime/Camera.h`
- [ ] `SkullbonezSource/Runtime/CameraCollection.cpp`
- [ ] `SkullbonezSource/Runtime/CameraCollection.h`
- [ ] `SkullbonezSource/Runtime/CaptureController.cpp`
- [ ] `SkullbonezSource/Runtime/CaptureController.h`
- [ ] `SkullbonezSource/Runtime/CaptureSystem.cpp`
- [ ] `SkullbonezSource/Runtime/CaptureSystem.h`
- [ ] `SkullbonezSource/Runtime/DiagnosticsController.cpp`
- [ ] `SkullbonezSource/Runtime/DiagnosticsController.h`
- [ ] `SkullbonezSource/Runtime/EngineContext.cpp`
- [ ] `SkullbonezSource/Runtime/EngineContext.h`
- [ ] `SkullbonezSource/Runtime/Init.cpp`
- [ ] `SkullbonezSource/Runtime/Input.cpp`
- [ ] `SkullbonezSource/Runtime/Input.h`
- [ ] `SkullbonezSource/Runtime/InputController.cpp`
- [ ] `SkullbonezSource/Runtime/InputController.h`
- [ ] `SkullbonezSource/Runtime/Run.cpp`
- [ ] `SkullbonezSource/Runtime/Run.h`
- [ ] `SkullbonezSource/Runtime/RunCapture.cpp`
- [ ] `SkullbonezSource/Runtime/RunFrame.cpp`
- [ ] `SkullbonezSource/Runtime/RunInput.cpp`
- [ ] `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`
- [ ] `SkullbonezSource/Runtime/RunInternal.h`
- [ ] `SkullbonezSource/Runtime/RunLiveStyle.cpp`
- [ ] `SkullbonezSource/Runtime/RunPasses.cpp`
- [ ] `SkullbonezSource/Runtime/RunRender.cpp`
- [ ] `SkullbonezSource/Runtime/RunReplayProbeState.h`
- [ ] `SkullbonezSource/Runtime/RunState.h`
- [ ] `SkullbonezSource/Runtime/RunStress.cpp`
- [ ] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeCameraMode.h`
- [ ] `SkullbonezSource/Runtime/RuntimeCommandQueue.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeCommandQueue.h`
- [ ] `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeDiagnostics.h`
- [ ] `SkullbonezSource/Runtime/RuntimeFileWriter.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeFileWriter.h`
- [ ] `SkullbonezSource/Runtime/RuntimeInteractionController.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeInteractionController.h`
- [ ] `SkullbonezSource/Runtime/RuntimePickService.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimePickService.h`
- [ ] `SkullbonezSource/Runtime/RuntimeTuning.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeTuning.h`
- [ ] `SkullbonezSource/Runtime/RuntimeViewModel.cpp`
- [ ] `SkullbonezSource/Runtime/RuntimeViewModel.h`
- [ ] `SkullbonezSource/Runtime/SimulationController.cpp`
- [ ] `SkullbonezSource/Runtime/SimulationController.h`
- [ ] `SkullbonezSource/Runtime/Window.cpp`
- [ ] `SkullbonezSource/Runtime/Window.h`

## Core (21 files)

- [ ] `SkullbonezSource/Core/AmortizedTask.cpp`
- [ ] `SkullbonezSource/Core/AmortizedTask.h`
- [ ] `SkullbonezSource/Core/Common.h`
- [ ] `SkullbonezSource/Core/Config.cpp`
- [ ] `SkullbonezSource/Core/Config.h`
- [ ] `SkullbonezSource/Core/Fence.h`
- [ ] `SkullbonezSource/Core/LockOrderValidator.cpp`
- [ ] `SkullbonezSource/Core/LockOrderValidator.h`
- [ ] `SkullbonezSource/Core/Log.cpp`
- [ ] `SkullbonezSource/Core/Log.h`
- [ ] `SkullbonezSource/Core/MainMemoryStats.h`
- [ ] `SkullbonezSource/Core/PlatformProfiler.cpp`
- [ ] `SkullbonezSource/Core/PlatformProfiler.h`
- [ ] `SkullbonezSource/Core/Profiler.cpp`
- [ ] `SkullbonezSource/Core/Profiler.h`
- [ ] `SkullbonezSource/Core/SkullScope.cpp`
- [ ] `SkullbonezSource/Core/SkullScope.h`
- [ ] `SkullbonezSource/Core/Timer.cpp`
- [ ] `SkullbonezSource/Core/Timer.h`
- [ ] `SkullbonezSource/Core/WorkerPool.cpp`
- [ ] `SkullbonezSource/Core/WorkerPool.h`

## Physics (40 files)

- [ ] `SkullbonezSource/Physics/BoundingBox.cpp`
- [ ] `SkullbonezSource/Physics/BoundingBox.h`
- [ ] `SkullbonezSource/Physics/BoundingSphere.cpp`
- [ ] `SkullbonezSource/Physics/BoundingSphere.h`
- [ ] `SkullbonezSource/Physics/ColliderStore.cpp`
- [ ] `SkullbonezSource/Physics/ColliderStore.h`
- [ ] `SkullbonezSource/Physics/CollisionShape.h`
- [ ] `SkullbonezSource/Physics/ContactSolverCommon.h`
- [ ] `SkullbonezSource/Physics/ConvexHullShape.cpp`
- [ ] `SkullbonezSource/Physics/ConvexHullShape.h`
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.h`
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [ ] `SkullbonezSource/Physics/PhysicsApi.h`
- [ ] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [ ] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`
- [ ] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsEngine.h`
- [ ] `SkullbonezSource/Physics/PhysicsHandles.h`
- [ ] `SkullbonezSource/Physics/PhysicsMass.h`
- [ ] `SkullbonezSource/Physics/PhysicsScene.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsScene.h`
- [ ] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsWorld.h`
- [ ] `SkullbonezSource/Physics/Ragdoll.cpp`
- [ ] `SkullbonezSource/Physics/Ragdoll.h`
- [ ] `SkullbonezSource/Physics/ResponseInformation.h`
- [ ] `SkullbonezSource/Physics/RigidBody.cpp`
- [ ] `SkullbonezSource/Physics/RigidBody.h`
- [ ] `SkullbonezSource/Physics/SimulationSystem.cpp`
- [ ] `SkullbonezSource/Physics/SimulationSystem.h`
- [ ] `SkullbonezSource/Physics/SleepIslandSystem.cpp`
- [ ] `SkullbonezSource/Physics/SleepIslandSystem.h`
- [ ] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [ ] `SkullbonezSource/Physics/SpatialGrid.h`
- [ ] `SkullbonezSource/Physics/TornadoField.cpp`
- [ ] `SkullbonezSource/Physics/TornadoField.h`

## GameObjects (8 files)

- [ ] `SkullbonezSource/GameObjects/GameModel.cpp`
- [ ] `SkullbonezSource/GameObjects/GameModel.h`
- [ ] `SkullbonezSource/GameObjects/GameModelCollection.cpp`
- [ ] `SkullbonezSource/GameObjects/GameModelCollection.h`
- [ ] `SkullbonezSource/GameObjects/GameModelSoACache.cpp`
- [ ] `SkullbonezSource/GameObjects/GameModelSoACache.h`
- [ ] `SkullbonezSource/GameObjects/GameModelStreams.cpp`
- [ ] `SkullbonezSource/GameObjects/GameModelStreams.h`

## Physics / Debug (6 files)

- [ ] `SkullbonezSource/Physics/Debug/BroadphaseVisualizer.cpp`
- [ ] `SkullbonezSource/Physics/Debug/BroadphaseVisualizer.h`
- [ ] `SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp`
- [ ] `SkullbonezSource/Physics/Debug/CollisionVisualizer.h`
- [ ] `SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp`
- [ ] `SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h`

## Rendering (25 files)

- [ ] `SkullbonezSource/Rendering/DrawCallTrace.cpp`
- [ ] `SkullbonezSource/Rendering/DrawCallTrace.h`
- [ ] `SkullbonezSource/Rendering/GameModelRenderer.cpp`
- [ ] `SkullbonezSource/Rendering/GameModelRenderer.h`
- [ ] `SkullbonezSource/Rendering/Helper.cpp`
- [ ] `SkullbonezSource/Rendering/Helper.h`
- [ ] `SkullbonezSource/Rendering/IFramebuffer.h`
- [ ] `SkullbonezSource/Rendering/IMesh.h`
- [ ] `SkullbonezSource/Rendering/IRenderBackend.cpp`
- [ ] `SkullbonezSource/Rendering/IRenderBackend.h`
- [ ] `SkullbonezSource/Rendering/IShader.h`
- [ ] `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h`
- [ ] `SkullbonezSource/Rendering/RenderGraph.cpp`
- [ ] `SkullbonezSource/Rendering/RenderGraph.h`
- [ ] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [ ] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [ ] `SkullbonezSource/Rendering/RenderMaterial.h`
- [ ] `SkullbonezSource/Rendering/RenderPipeline.cpp`
- [ ] `SkullbonezSource/Rendering/RenderPipeline.h`
- [ ] `SkullbonezSource/Rendering/RenderSceneSnapshot.h`
- [ ] `SkullbonezSource/Rendering/RenderSceneView.h`
- [ ] `SkullbonezSource/Rendering/ShaderContracts.h`
- [ ] `SkullbonezSource/Rendering/Shadow.h`
- [ ] `SkullbonezSource/Rendering/Text.cpp`
- [ ] `SkullbonezSource/Rendering/Text.h`

## World (7 files)

- [ ] `SkullbonezSource/World/SkyBox.cpp`
- [ ] `SkullbonezSource/World/SkyBox.h`
- [ ] `SkullbonezSource/World/Terrain.cpp`
- [ ] `SkullbonezSource/World/Terrain.h`
- [ ] `SkullbonezSource/World/TerrainSupportClassifier.h`
- [ ] `SkullbonezSource/World/WorldEnvironment.cpp`
- [ ] `SkullbonezSource/World/WorldEnvironment.h`

## Rendering / DX12 (25 files)

- [ ] `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/BLASDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`
- [ ] `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/FramebufferDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/MeshDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/SBTDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/SBTDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- [ ] `SkullbonezSource/Rendering/DX12/TLASDX12.cpp`
- [ ] `SkullbonezSource/Rendering/DX12/TLASDX12.h`

## Runtime / Render Host (6 files)

- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h`
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`

## Maths (11 files)

- [ ] `SkullbonezSource/Maths/GeometricMath.cpp`
- [ ] `SkullbonezSource/Maths/GeometricMath.h`
- [ ] `SkullbonezSource/Maths/GeometricStructures.h`
- [ ] `SkullbonezSource/Maths/Matrix4.cpp`
- [ ] `SkullbonezSource/Maths/Matrix4.h`
- [ ] `SkullbonezSource/Maths/Quaternion.cpp`
- [ ] `SkullbonezSource/Maths/Quaternion.h`
- [ ] `SkullbonezSource/Maths/RotationMatrix.cpp`
- [ ] `SkullbonezSource/Maths/RotationMatrix.h`
- [ ] `SkullbonezSource/Maths/Vector3.cpp`
- [ ] `SkullbonezSource/Maths/Vector3.h`

## Shaders / HLSL (21 files)

- [ ] `SkullbonezData/shaders/UIBackdropBlur.hlsl`
- [ ] `SkullbonezData/shaders/collision_visualizer.hlsl`
- [ ] `SkullbonezData/shaders/generate_mips.hlsl`
- [ ] `SkullbonezData/shaders/grid_line.hlsl`
- [ ] `SkullbonezData/shaders/launcher_laser.hlsl`
- [ ] `SkullbonezData/shaders/lit_textured.hlsl`
- [ ] `SkullbonezData/shaders/lit_textured_instanced.hlsl`
- [ ] `SkullbonezData/shaders/post_tonemap.hlsl`
- [ ] `SkullbonezData/shaders/post_volumetric_light.hlsl`
- [ ] `SkullbonezData/shaders/reflect.rt.hlsl`
- [ ] `SkullbonezData/shaders/shadow_depth.hlsl`
- [ ] `SkullbonezData/shaders/shadow_depth_instanced.hlsl`
- [ ] `SkullbonezData/shaders/sky_atmosphere.hlsl`
- [ ] `SkullbonezData/shaders/solid_color.hlsl`
- [ ] `SkullbonezData/shaders/solid_color_batch.hlsl`
- [ ] `SkullbonezData/shaders/text.hlsl`
- [ ] `SkullbonezData/shaders/tornado_fx.hlsl`
- [ ] `SkullbonezData/shaders/ui_render_target_preview.hlsl`
- [ ] `SkullbonezData/shaders/unlit_textured.hlsl`
- [ ] `SkullbonezData/shaders/water_calm.hlsl`
- [ ] `SkullbonezData/shaders/water_ocean.hlsl`

## Tools / Validation and Scripts (66 files)

- [ ] `tools/agent_validate.bat`
- [ ] `tools/align_header_inline_comments.py`
- [ ] `tools/archive_validation_artifacts.bat`
- [ ] `tools/archive_validation_artifacts.py`
- [ ] `tools/bake_hulls.bat`
- [ ] `tools/bake_hulls.py`
- [ ] `tools/capture_ui_screenshot.bat`
- [ ] `tools/check_dx12_baselines.py`
- [ ] `tools/check_dx12_validation.bat`
- [ ] `tools/check_physics_known_issue_regression.py`
- [ ] `tools/check_physics_query_regression.py`
- [ ] `tools/check_physics_regression.py`
- [ ] `tools/check_replay_scrub_regression.py`
- [ ] `tools/check_replay_v2_artifact.py`
- [ ] `tools/check_runtime_boundaries.py`
- [ ] `tools/check_shooting_reaction.py`
- [ ] `tools/check_ui_blur.py`
- [ ] `tools/export_screenshot_png.py`
- [ ] `tools/find_clang_format.bat`
- [ ] `tools/find_git.bat`
- [ ] `tools/find_msbuild.bat`
- [ ] `tools/find_python.bat`
- [ ] `tools/format_fix.bat`
- [ ] `tools/loc_count.bat`
- [ ] `tools/physics_query.bat`
- [ ] `tools/physics_query.py`
- [ ] `tools/refresh_hulls.bat`
- [ ] `tools/replay_query.bat`
- [ ] `tools/replay_query.py`
- [ ] `tools/style_harness.bat`
- [ ] `tools/style_harness.ps1`
- [ ] `tools/update_baselines.bat`
- [ ] `tools/update_baselines.py`
- [ ] `tools/validate_build.bat`
- [ ] `tools/validate_concepts.bat`
- [ ] `tools/validate_concepts.py`
- [ ] `tools/validate_deep.bat`
- [ ] `tools/validate_demo_stress.bat`
- [ ] `tools/validate_dx12_arch_tests.bat`
- [ ] `tools/validate_dx12_renderer.bat`
- [ ] `tools/validate_fast.bat`
- [ ] `tools/validate_format.bat`
- [ ] `tools/validate_full.bat`
- [ ] `tools/validate_interaction_clicks.bat`
- [ ] `tools/validate_perf.bat`
- [ ] `tools/validate_physics.bat`
- [ ] `tools/validate_physics_deep.bat`
- [ ] `tools/validate_physics_query.bat`
- [ ] `tools/validate_project_filters.bat`
- [ ] `tools/validate_project_filters.py`
- [ ] `tools/validate_ready_builds.bat`
- [ ] `tools/validate_renderers.bat`
- [ ] `tools/validate_replay_scrub.bat`
- [ ] `tools/validate_replay_v2_artifact.bat`
- [ ] `tools/validate_runtime_boundaries.bat`
- [ ] `tools/validate_runtime_interaction_policy.bat`
- [ ] `tools/validate_scene_loads.bat`
- [ ] `tools/validate_scene_loads.py`
- [ ] `tools/validate_scene_parser_tests.bat`
- [ ] `tools/validate_select.bat`
- [ ] `tools/validate_shaders.bat`
- [ ] `tools/validate_shaders.py`
- [ ] `tools/validate_ui.bat`
- [ ] `tools/validate_ui_stress.bat`
- [ ] `tools/watch_demo_stress.bat`
- [ ] `tools/watch_ui_stress.bat`

## Agentic / Test Harnesses (3 files)

- [ ] `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- [ ] `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp`
- [ ] `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp`

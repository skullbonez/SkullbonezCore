# Engine Glossary Consolidation ? GC0 Inventory

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/engine-glossary-consolidation.md`
Branch: `nightrunner-30th-JUL-26`

## Outcome

GC0 establishes the complete tracked C++ source vocabulary census and the
per-file checklist that GC2 and GC3 must reconcile. The source list comes
from `git ls-files SkullbonezSource`, filtered only to `.cpp`, `.h`, `.hpp`,
`.inl`, and `.hlsl`; no `rg` result is used as the scope authority.

The plan's provisional denominator of 576 counted every tracked path under
`SkullbonezSource`, including `SkullbonezSource/AGENTS.md`. The corrected
source-bearing denominator is 575. This is a census correction, not a scope
reduction.

## Census

| Measure | Current result |
|---|---:|
| Tracked source-bearing files | 575 |
| Files with a `Glossary:` block | 570 |
| Files without a `Glossary:` block | 5 |
| Parsed term definitions | 2172 |
| Unique terms | 1285 |
| Multi-file shared terms | 321 |
| Single-file local terms | 964 |
| Shared terms with wording drift | 264 |
| Shared terms already word-for-word identical | 57 |

Files without a glossary remain in the checklist and require ordinary
comment-style inspection; GC0 does not infer that a glossary is mandatory
when the file has no genuinely local vocabulary:

- `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.h`
- `SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h`
- `SkullbonezSource/Runtime/RuntimeFrameViews.h`

## Classification And Canonical-Wording Ruling

The owner split rule is applied literally: all 321 terms defined in more
than one tracked source file are shared vocabulary. No repeated term was
approved as a site-local homonym. All 964 terms defined in exactly one file
remain local. This is a classification, not a size budget; later inventories
must report whatever the current tree contains.

`Model row hint` and `Published prefix`, called out by the plan for explicit
judgment, are both shared. Their canonical wordings are:

- **Model row hint:** Caller-owned cached dense-row guess that must be repaired
  or invalidated against stable identity before use.
- **Published prefix:** Contiguous completed rows that a reader may consume
  after the owning publication boundary.

For drifted terms, GC0 adjudicates one existing wording by an
occurrence-weighted token-medoid rule: choose the definition most central to
all current variants, then prefer repeated wording, a header source, and a
concise complete sentence. Fifteen cross-cutting terms receive explicit owner
wording instead. GC1 must use the canonical wording below or record an
explicit superseding wording and reason in its evidence report.

Concrete drift examples show why consolidation is required: `Lane R result`
has 21 wordings across 25 files, `Lifecycle generation` has ten wordings
across ten files, `DXR (DirectX Raytracing)` has five wordings across 18
files, and `Narrowphase` has four wordings across 30 files.

## Complete Shared-Term Inventory

Every row is classified shared. `Variants > 1` is the complete drift ledger;
the canonical wording is the GC0 adjudication that GC1 consumes.

| Term | Sites | Variants | Canonical wording | Canonical source | Defining files |
|---|---:|---:|---|---|---|
| Draw command | 46 | 1 | Lightweight record describing a UI shape or text batch to render later in the frame. | SkullbonezSource/UI/UI.cpp | `SkullbonezSource/UI/UI.cpp`<br>`SkullbonezSource/UI/UI.h`<br>`SkullbonezSource/UI/UIBackdropBlur.cpp`<br>`SkullbonezSource/UI/UIBackdropBlur.h`<br>`SkullbonezSource/UI/UIButton.cpp`<br>`SkullbonezSource/UI/UIButton.h`<br>`SkullbonezSource/UI/UICache.cpp`<br>`SkullbonezSource/UI/UICache.h`<br>`SkullbonezSource/UI/UICheckBox.cpp`<br>`SkullbonezSource/UI/UICheckBox.h`<br>`SkullbonezSource/UI/UIComboBox.cpp`<br>`SkullbonezSource/UI/UIComboBox.h`<br>`SkullbonezSource/UI/UICommands.h`<br>`SkullbonezSource/UI/UIDraw.cpp`<br>`SkullbonezSource/UI/UIDraw.h`<br>`SkullbonezSource/UI/UIDrawList.cpp`<br>`SkullbonezSource/UI/UIDrawList.h`<br>`SkullbonezSource/UI/UIDrawWidgets.cpp`<br>`SkullbonezSource/UI/UIDrawWidgets.h`<br>`SkullbonezSource/UI/UIIconButton.cpp`<br>`SkullbonezSource/UI/UIIconButton.h`<br>`SkullbonezSource/UI/UILayout.cpp`<br>`SkullbonezSource/UI/UILayout.h`<br>`SkullbonezSource/UI/UIScrollBar.cpp`<br>`SkullbonezSource/UI/UIScrollBar.h`<br>`SkullbonezSource/UI/UISlider.cpp`<br>`SkullbonezSource/UI/UISlider.h`<br>`SkullbonezSource/UI/UIState.h`<br>`SkullbonezSource/UI/UIStyle.cpp`<br>`SkullbonezSource/UI/UIStyle.h`<br>`SkullbonezSource/UI/UITabBar.cpp`<br>`SkullbonezSource/UI/UITabBar.h`<br>`SkullbonezSource/UI/UITabCinematic.cpp`<br>`SkullbonezSource/UI/UITabCinematic.h`<br>`SkullbonezSource/UI/UITabControls.cpp`<br>`SkullbonezSource/UI/UITabControls.h`<br>`SkullbonezSource/UI/UITabOptions.cpp`<br>`SkullbonezSource/UI/UITabOptions.h`<br>`SkullbonezSource/UI/UITabPhysics.cpp`<br>`SkullbonezSource/UI/UITabPhysics.h`<br>`SkullbonezSource/UI/UITabProfiler.cpp`<br>`SkullbonezSource/UI/UITabProfiler.h`<br>`SkullbonezSource/UI/UITabScene.cpp`<br>`SkullbonezSource/UI/UITabScene.h`<br>`SkullbonezSource/UI/UIWindowChrome.cpp`<br>`SkullbonezSource/UI/UIWindowChrome.h` |
| Hit box | 44 | 1 | Screen-space rectangle used to decide whether mouse input targets a widget. | SkullbonezSource/UI/UI.cpp | `SkullbonezSource/UI/UI.cpp`<br>`SkullbonezSource/UI/UI.h`<br>`SkullbonezSource/UI/UIButton.cpp`<br>`SkullbonezSource/UI/UIButton.h`<br>`SkullbonezSource/UI/UICache.cpp`<br>`SkullbonezSource/UI/UICache.h`<br>`SkullbonezSource/UI/UICheckBox.cpp`<br>`SkullbonezSource/UI/UICheckBox.h`<br>`SkullbonezSource/UI/UIComboBox.cpp`<br>`SkullbonezSource/UI/UIComboBox.h`<br>`SkullbonezSource/UI/UICommands.h`<br>`SkullbonezSource/UI/UIDraw.cpp`<br>`SkullbonezSource/UI/UIDraw.h`<br>`SkullbonezSource/UI/UIDrawList.cpp`<br>`SkullbonezSource/UI/UIDrawList.h`<br>`SkullbonezSource/UI/UIDrawWidgets.cpp`<br>`SkullbonezSource/UI/UIDrawWidgets.h`<br>`SkullbonezSource/UI/UIIconButton.cpp`<br>`SkullbonezSource/UI/UIIconButton.h`<br>`SkullbonezSource/UI/UILayout.cpp`<br>`SkullbonezSource/UI/UILayout.h`<br>`SkullbonezSource/UI/UIScrollBar.cpp`<br>`SkullbonezSource/UI/UIScrollBar.h`<br>`SkullbonezSource/UI/UISlider.cpp`<br>`SkullbonezSource/UI/UISlider.h`<br>`SkullbonezSource/UI/UIState.h`<br>`SkullbonezSource/UI/UIStyle.cpp`<br>`SkullbonezSource/UI/UIStyle.h`<br>`SkullbonezSource/UI/UITabBar.cpp`<br>`SkullbonezSource/UI/UITabBar.h`<br>`SkullbonezSource/UI/UITabCinematic.cpp`<br>`SkullbonezSource/UI/UITabCinematic.h`<br>`SkullbonezSource/UI/UITabControls.cpp`<br>`SkullbonezSource/UI/UITabControls.h`<br>`SkullbonezSource/UI/UITabOptions.cpp`<br>`SkullbonezSource/UI/UITabOptions.h`<br>`SkullbonezSource/UI/UITabPhysics.cpp`<br>`SkullbonezSource/UI/UITabPhysics.h`<br>`SkullbonezSource/UI/UITabProfiler.cpp`<br>`SkullbonezSource/UI/UITabProfiler.h`<br>`SkullbonezSource/UI/UITabScene.cpp`<br>`SkullbonezSource/UI/UITabScene.h`<br>`SkullbonezSource/UI/UIWindowChrome.cpp`<br>`SkullbonezSource/UI/UIWindowChrome.h` |
| Back buffer | 32 | 1 | Swap-chain image that will be presented to the window. | SkullbonezSource/Assets/TextureCollection.cpp | `SkullbonezSource/Assets/TextureCollection.cpp`<br>`SkullbonezSource/Assets/TextureCollection.h`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.h`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.h`<br>`SkullbonezSource/Rendering/DX12/MeshDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/MeshDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.h`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.h`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.h`<br>`SkullbonezSource/Rendering/PrimitiveMeshBuilder.h`<br>`SkullbonezSource/Rendering/RenderGraph.cpp`<br>`SkullbonezSource/Rendering/RenderGraph.h`<br>`SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceRenderer.h`<br>`SkullbonezSource/Rendering/Shadow.h`<br>`SkullbonezSource/Rendering/Text.cpp`<br>`SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/Runtime/Capture/CaptureSystem.cpp`<br>`SkullbonezSource/Runtime/Capture/CaptureSystem.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`<br>`SkullbonezSource/World/SkyBox.cpp`<br>`SkullbonezSource/World/SkyBox.h` |
| Descriptor | 32 | 2 | Small binding record that tells the GPU or output-merger how to interpret a resource. | GC0 owner adjudication | `SkullbonezSource/Assets/TextureCollection.cpp`<br>`SkullbonezSource/Assets/TextureCollection.h`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.h`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.h`<br>`SkullbonezSource/Rendering/DX12/MeshDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/MeshDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.h`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.h`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.h`<br>`SkullbonezSource/Rendering/PrimitiveMeshBuilder.h`<br>`SkullbonezSource/Rendering/RenderGraph.cpp`<br>`SkullbonezSource/Rendering/RenderGraph.h`<br>`SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceRenderer.h`<br>`SkullbonezSource/Rendering/Shadow.h`<br>`SkullbonezSource/Rendering/Text.cpp`<br>`SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/Runtime/Capture/CaptureSystem.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`<br>`SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.cpp`<br>`SkullbonezSource/World/SkyBox.cpp`<br>`SkullbonezSource/World/SkyBox.h` |
| Broadphase | 30 | 3 | Cheap collision pass that finds object pairs worth testing more precisely. | GC0 owner adjudication | `SkullbonezSource/Physics/BoundingBox.cpp`<br>`SkullbonezSource/Physics/BoundingBox.h`<br>`SkullbonezSource/Physics/BoundingSphere.cpp`<br>`SkullbonezSource/Physics/BoundingSphere.h`<br>`SkullbonezSource/Physics/CollisionShape.h`<br>`SkullbonezSource/Physics/ContactSolverCommon.h`<br>`SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PersistentContactSolver.h`<br>`SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h`<br>`SkullbonezSource/Physics/PhysicsDebugData.h`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/SleepIslandSystem.cpp`<br>`SkullbonezSource/Physics/SleepIslandSystem.h`<br>`SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`<br>`SkullbonezSource/Physics/TerrainSupportClassifier.h`<br>`SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h`<br>`SkullbonezSource/World/Terrain.h`<br>`SkullbonezSource/World/WorldEnvironment.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| Narrowphase | 30 | 4 | Precise collision pass that computes contact points, normals, and penetration. | GC0 owner adjudication | `SkullbonezSource/Physics/BoundingBox.cpp`<br>`SkullbonezSource/Physics/BoundingBox.h`<br>`SkullbonezSource/Physics/BoundingSphere.cpp`<br>`SkullbonezSource/Physics/BoundingSphere.h`<br>`SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/CollisionShape.h`<br>`SkullbonezSource/Physics/ContactSolverCommon.h`<br>`SkullbonezSource/Physics/ConvexHullShape.h`<br>`SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PersistentContactSolver.h`<br>`SkullbonezSource/Physics/PhysicsDebugData.h`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/SleepIslandSystem.cpp`<br>`SkullbonezSource/Physics/SleepIslandSystem.h`<br>`SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Physics/TerrainSupportClassifier.h`<br>`SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h`<br>`SkullbonezSource/World/Terrain.h`<br>`SkullbonezSource/World/WorldEnvironment.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| Manifold | 26 | 2 | Set of contact points and normals describing one colliding pair. | SkullbonezSource/Physics/BoundingBox.cpp | `SkullbonezSource/Physics/BoundingBox.cpp`<br>`SkullbonezSource/Physics/BoundingBox.h`<br>`SkullbonezSource/Physics/BoundingSphere.cpp`<br>`SkullbonezSource/Physics/BoundingSphere.h`<br>`SkullbonezSource/Physics/CollisionShape.h`<br>`SkullbonezSource/Physics/ContactSolverCommon.h`<br>`SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PersistentContactSolver.h`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/SleepIslandSystem.cpp`<br>`SkullbonezSource/Physics/SleepIslandSystem.h`<br>`SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Physics/TerrainContactManifold.h`<br>`SkullbonezSource/Physics/TerrainSupportClassifier.h`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h`<br>`SkullbonezSource/World/Terrain.h`<br>`SkullbonezSource/World/WorldEnvironment.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| Lane R result | 25 | 21 | Recoverable owner/message result for external input or environment failure, reported without exceptions or fatal termination. | GC0 owner adjudication | `SkullbonezSource/Assets/TextureCollection.cpp`<br>`SkullbonezSource/Core/Timer.cpp`<br>`SkullbonezSource/Core/Timer.h`<br>`SkullbonezSource/Rendering/Text.cpp`<br>`SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/Runtime/App/Init.cpp`<br>`SkullbonezSource/Runtime/App/InputFrame.cpp`<br>`SkullbonezSource/Runtime/App/Run.cpp`<br>`SkullbonezSource/Runtime/App/Run.h`<br>`SkullbonezSource/Runtime/App/RunFrame.cpp`<br>`SkullbonezSource/Runtime/App/Window.cpp`<br>`SkullbonezSource/Runtime/App/Window.h`<br>`SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`<br>`SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp`<br>`SkullbonezSource/Runtime/Direction/LiveStyleController.cpp`<br>`SkullbonezSource/Runtime/Input/Input.cpp`<br>`SkullbonezSource/Runtime/Input/Input.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneController.Style.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`<br>`SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h` |
| DXR (DirectX Raytracing) | 18 | 5 | DX12 API used for hardware ray traversal and reflection dispatch. | SkullbonezSource/Rendering/DX12/BLASDX12.cpp | `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.h`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.h`<br>`SkullbonezSource/Rendering/PrimitiveBatchRenderer.h`<br>`SkullbonezSource/Rendering/RenderSceneSnapshot.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h`<br>`SkullbonezSource/UI/UICommands.h`<br>`SkullbonezSource/World/Terrain.h`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| Lane R | 14 | 10 | Recoverable error-handling lane for external input or environment failure, represented by an owner/message result. | GC0 owner adjudication | `SkullbonezSource/Core/SbResult.h`<br>`SkullbonezSource/Physics/ConvexHullShape.h`<br>`SkullbonezSource/Runtime/App/ApplicationExitState.h`<br>`SkullbonezSource/Runtime/Direction/DemoDirector.cpp`<br>`SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParser.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserSchema.h` |
| SkullScope | 14 | 5 | Structured Physics diagnostic capture and query surface used by validation and tooling. | GC0 owner adjudication | `SkullbonezSource/Core/Config.h`<br>`SkullbonezSource/Core/Log.cpp`<br>`SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`<br>`SkullbonezSource/Physics/Diagnostics/SkullScope.h`<br>`SkullbonezSource/Physics/PhysicsDebugData.h`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsModel.h`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.h`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h` |
| SRV (Shader Resource View) | 13 | 2 | Descriptor row used when shaders read textures or buffers. | SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp | `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h`<br>`SkullbonezSource/Rendering/ShaderContracts.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` |
| Scene object id | 12 | 11 | Stable per-scene physics identity used to correlate one body across dense-row movement, Replay, and diagnostics. | GC0 owner adjudication | `SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/PhysicsBodyStore.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.h`<br>`SkullbonezSource/Physics/PhysicsHandles.h`<br>`SkullbonezSource/Rendering/RenderInstanceStore.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.h`<br>`SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h` |
| UAV (Unordered Access View) | 12 | 3 | Descriptor row used when compute or raytracing shaders write textures or buffers. | SkullbonezSource/Rendering/DX12/BLASDX12.cpp | `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/RenderGraph.h`<br>`SkullbonezSource/Rendering/RenderRaytracingTypes.h` |
| BLAS (Bottom-Level Acceleration Structure) | 11 | 3 | Raytracing spatial index for one mesh's triangles. | SkullbonezSource/Rendering/DX12/BLASDX12.cpp | `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/PrimitiveBatchRenderer.h`<br>`SkullbonezSource/Rendering/RenderRaytracingTypes.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`<br>`SkullbonezSource/World/Terrain.h` |
| PSO (Pipeline State Object) | 11 | 3 | Precompiled bundle of shaders and fixed render state that DX12 binds before drawing or dispatching. | SkullbonezSource/Rendering/DX12/MeshDX12.h | `SkullbonezSource/Rendering/DX12/MeshDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.h` |
| Lane F | 10 | 8 | Fatal invariant lane for should-never-happen owned engine state; it records diagnostics and does not return. | GC0 owner adjudication | `SkullbonezSource/Core/AmortizedTask.h`<br>`SkullbonezSource/Core/FatalError.cpp`<br>`SkullbonezSource/Core/FatalError.h`<br>`SkullbonezSource/Core/Log.h`<br>`SkullbonezSource/Core/Profiler.cpp`<br>`SkullbonezSource/Core/WorkerPool.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp`<br>`SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h` |
| Lifecycle generation | 10 | 10 | Monotonic identity for one accepted scene-load attempt, independent of scene index or successful activation. | GC0 owner adjudication | `SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/App/RunTimerState.h`<br>`SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h`<br>`SkullbonezSource/Runtime/Camera/AttachedCameraController.h`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h`<br>`SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneController.h`<br>`SkullbonezSource/Runtime/Scene/SceneLifecycle.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| TLAS (Top-Level Acceleration Structure) | 10 | 3 | Raytracing spatial index for scene instances that point at BLAS geometry. | SkullbonezSource/Rendering/DX12/BLASDX12.cpp | `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/BLASDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/TLASDX12.h`<br>`SkullbonezSource/Rendering/RenderRaytracingTypes.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` |
| DSV (Depth Stencil View) | 9 | 4 | Descriptor row used when the GPU reads or writes depth/stencil data for depth testing. | SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp | `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h` |
| RTV (Render Target View) | 9 | 3 | Descriptor row used when the GPU writes color pixels into a texture or back buffer. | SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp | `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/FramebufferDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h` |
| Body store | 7 | 5 | Physics-owned live body records used for pose and velocity authority while legacy object-record mirrors are retired. | SkullbonezSource/Runtime/App/ReplayRuntime.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`<br>`SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Collider store | 7 | 5 | Physics-owned shape, material, and radius records paired with body handles. | SkullbonezSource/Runtime/App/ReplayRuntime.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`<br>`SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| FBO (Framebuffer Object) | 7 | 4 | Engine shorthand for an off-screen render target exposed through the renderer abstraction. | SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp | `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Runtime/App/Run.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h`<br>`SkullbonezSource/UI/UICommands.h` |
| Model row hint | 7 | 7 | Caller-owned cached dense-row guess that must be repaired or invalidated against stable identity before use. | GC0 owner adjudication | `SkullbonezSource/Physics/PhysicsBodyStore.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.h`<br>`SkullbonezSource/Physics/PhysicsHandles.h`<br>`SkullbonezSource/Runtime/Interaction/RuntimePickService.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| OBB (Oriented Bounding Box) | 7 | 2 | Box with rotation, used for exact object-space collision tests. | SkullbonezSource/Physics/BoundingBox.cpp | `SkullbonezSource/Physics/BoundingBox.cpp`<br>`SkullbonezSource/Physics/BoundingBox.h`<br>`SkullbonezSource/Physics/BoundingSphere.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/TerrainContactManifold.cpp` |
| Physics material | 7 | 4 | Runtime policy for collider friction and sphere drag. | SkullbonezSource/Physics/ColliderStore.cpp | `SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/PhysicsEngine.cpp`<br>`SkullbonezSource/Physics/PhysicsEngine.h`<br>`SkullbonezSource/Physics/PhysicsObjectPolicy.cpp`<br>`SkullbonezSource/Physics/PhysicsObjectPolicy.h`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Published prefix | 7 | 7 | Contiguous completed rows that a reader may consume after the owning publication boundary. | GC0 owner adjudication | `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h`<br>`SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`<br>`SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h` |
| Asset system | 6 | 6 | Runtime-owned registry that resolves editor asset-library names without querying process-global state. | SkullbonezSource/Runtime/Editor/EditorTools.h | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`<br>`SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTools.h`<br>`SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h`<br>`SkullbonezSource/Scene/AuthoredScene.h` |
| CSV (Comma-Separated Values) | 6 | 2 | Text table format used for byte-exact physics regression output. | SkullbonezSource/Core/Log.cpp | `SkullbonezSource/Core/Log.cpp`<br>`SkullbonezSource/Core/Profiler.h`<br>`SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h`<br>`SkullbonezSource/Scene/AuthoredScene.h` |
| Schema domain | 6 | 2 | Cohesive authored section translated without creating another scene owner or intermediate model. | SkullbonezSource/Scene/AuthoredSceneParser.cpp | `SkullbonezSource/Scene/AuthoredSceneParser.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp`<br>`SkullbonezSource/Scene/AuthoredSceneParserSchema.h` |
| Snapshot | 6 | 6 | Detached value copy that remains safe after the producing owner advances or mutates. | GC0 owner adjudication | `SkullbonezSource/Rendering/DrawCallTrace.h`<br>`SkullbonezSource/Rendering/RenderPipeline.cpp`<br>`SkullbonezSource/Rendering/RenderPipeline.h`<br>`SkullbonezSource/Rendering/RenderSceneSnapshot.h`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h`<br>`SkullbonezSource/Scene/SceneSnapshotWriter.h` |
| Upload arena | 6 | 6 | Frame-scoped CPU-visible staging storage reusable only after the covering GPU fence completes. | GC0 owner adjudication | `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`<br>`SkullbonezSource/Rendering/DX12/MeshDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` |
| AABB (Axis-Aligned Bounding Box) | 5 | 3 | Box aligned to world axes, often used for cheap broadphase overlap tests. | SkullbonezSource/Physics/BoundingBox.h | `SkullbonezSource/Physics/BoundingBox.h`<br>`SkullbonezSource/Physics/PhysicsApi.h`<br>`SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp` |
| CBV (Constant Buffer View) | 5 | 2 | Descriptor row used when shaders read a packed block of constants. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderDX12.h` |
| COM (Component Object Model) | 5 | 2 | Windows interface lifetime model used by DX12 through reference-counted objects. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`<br>`SkullbonezSource/Runtime/App/Init.cpp` |
| Control surface | 5 | 5 | Fixed-capacity per-frame table shared by scrubber hit testing and, in later phases, drawing. | SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`<br>`SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlaySurface.h` |
| Convex hull | 5 | 5 | Collision shape made from a closed convex set of authored points. | SkullbonezSource/Physics/ColliderStore.h | `SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/ConvexHullShape.cpp`<br>`SkullbonezSource/Physics/ConvexHullShape.h`<br>`SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`<br>`SkullbonezSource/Rendering/Shadow.h` |
| Covering fence | 5 | 4 | Queue counter proving all earlier GPU references are finished. | SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp`<br>`SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp`<br>`SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h`<br>`SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`<br>`SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` |
| Fixed-tree release | 5 | 4 | Store-owned command that turns authored fixed props into dynamic bodies and wakes same-tree parts after an accepted impulse. | SkullbonezSource/Physics/PhysicsEngine.cpp | `SkullbonezSource/Physics/PhysicsBodyStore.h`<br>`SkullbonezSource/Physics/PhysicsEngine.cpp`<br>`SkullbonezSource/Physics/PhysicsEngine.h`<br>`SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| HUD (Heads-Up Display) | 5 | 4 | On-screen diagnostics and control overlay. | SkullbonezSource/Rendering/Text.h | `SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/Runtime/App/Run.h`<br>`SkullbonezSource/Runtime/Render/UiTextPass.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayPresentation.h`<br>`SkullbonezSource/Runtime/Replay/ReplayPresentationPackets.h` |
| Physics body handle | 5 | 5 | Generational id for the picked body-store row. | SkullbonezSource/Runtime/Editor/MousePickupTools.cpp | `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimePickService.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Recording epoch | 5 | 4 | One reusable command-list lifetime from successful Reset to Close. | SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp | `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`<br>`SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` |
| Render command context | 5 | 4 | Renderer capability borrowed only while drawing a collision-visualizer frame. | SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp | `SkullbonezSource/Assets/TextureCollection.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| Scrubber | 5 | 5 | Timeline control for seeking retained replay frames and future prediction frames. | SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlaySurface.h` |
| Solver sample | 5 | 5 | Physics-facing state retained for rollback and diagnostics. | SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` |
| UI (User Interface) | 5 | 4 | Runtime controls and overlays drawn over the 3D scene. | SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` |
| Underwater sleep lock | 5 | 1 | Sleep policy that keeps fully submerged balls dormant so buoyancy jitter does not repeatedly wake them. | SkullbonezSource/Physics/BuoyancySystem.h | `SkullbonezSource/Physics/BuoyancySystem.h`<br>`SkullbonezSource/Physics/PhysicsBodyStore.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.h`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h` |
| Velocity drag preview | 5 | 5 | First-order selected-path estimate retained until the release-triggered authoritative generation commits. | SkullbonezSource/Runtime/Prediction/ReplayPrediction.h | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` |
| Awake index list | 4 | 4 | Ascending dense body rows owned by the sleep controller and borrowed by work-producing stages for one sequenced fixed-step interval. | SkullbonezSource/Physics/PhysicsWorld.cpp | `SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/Stages/PhysicsForceStage.h`<br>`SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` |
| Convergence trace | 4 | 4 | Bounded per-iteration attribution for the solver's squared-impulse stopping metric. | SkullbonezSource/Physics/PhysicsDiagnosticsView.h | `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsView.h`<br>`SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Feature ID | 4 | 3 | Deterministic contact key used to match rows across frames for warm starting. | SkullbonezSource/Physics/ObjectContactManifold.h | `SkullbonezSource/Physics/ObjectContactManifold.h`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/TerrainContactManifold.h`<br>`SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` |
| Fence | 4 | 4 | GPU/CPU synchronization counter used to prove submitted command work has completed before memory is reused. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.h | `SkullbonezSource/Core/Fence.h`<br>`SkullbonezSource/Core/WorkerPool.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` |
| Geometry owner | 4 | 2 | Renderer owner borrowed while creating or destroying debug vertex and instance buffers. | SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp | `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| HDR (High Dynamic Range) | 4 | 4 | Floating-point scene color that can hold values brighter than display white until tonemapping resolves it. | SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp | `SkullbonezSource/Rendering/RenderResourceTypes.h`<br>`SkullbonezSource/Rendering/RenderSceneSnapshot.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` |
| Point joint | 4 | 2 | Constraint that keeps two local anchor points close together without yet modelling a full hinge, cone, or motor. | SkullbonezSource/Physics/PhysicsWorld.cpp | `SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/Ragdoll.cpp`<br>`SkullbonezSource/Physics/Ragdoll.h` |
| Presentation sample | 4 | 4 | Render-facing pose/state captured from a frame. | SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` |
| Private working set | 4 | 1 | Resident process pages not shared with other processes; matching it requires a page-level OS query. | SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h` |
| Probe failure | 4 | 3 | CLI validation failure reported as bounded result/report data so automation exits nonzero without throwing through the frame loop. | SkullbonezSource/Runtime/App/Run.cpp | `SkullbonezSource/Runtime/App/Run.cpp`<br>`SkullbonezSource/Runtime/App/Run.h`<br>`SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayProbeState.h` |
| Resource builder | 4 | 2 | Cold renderer owner borrowed only while compiling the laser shader. | SkullbonezSource/Runtime/Editor/LauncherLaser.cpp | `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| SBT (Shader Binding Table) | 4 | 2 | DXR table that maps ray records to ray-generation, miss, and hit shaders. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/SBTDX12.h` |
| Scene browser | 4 | 4 | UI-facing list of scene files discovered on disk. | SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp | `SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp`<br>`SkullbonezSource/UI/UISceneNavigationModel.h` |
| Scene queue | 4 | 3 | Ordered list of authored scene paths, where an empty path means the generated demo scene. | SkullbonezSource/Runtime/Scene/SceneSessionState.cpp | `SkullbonezSource/Runtime/Scene/SceneController.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneController.h`<br>`SkullbonezSource/Runtime/Scene/SceneSessionState.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneSessionState.h` |
| Shadow caster stream | 4 | 4 | Owner-prepared opaque bin selecting one primitive submission path without inspecting material or asset content here. | SkullbonezSource/Rendering/RenderInstanceRenderer.cpp | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.h`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Sleep | 4 | 4 | Optimization that stops simulating stable bodies until something wakes them. | SkullbonezSource/Physics/PhysicsBodyStore.cpp | `SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.h`<br>`SkullbonezSource/Physics/PhysicsEngine.cpp` |
| Sticky failure | 4 | 4 | First active command-path failure retained until a new device initialization establishes a fresh command-list lifetime. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp | `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` |
| Worker pool | 4 | 1 | Persistent thread group that runs bounded jobs outside the main thread. | SkullbonezSource/Core/AmortizedTask.cpp | `SkullbonezSource/Core/AmortizedTask.cpp`<br>`SkullbonezSource/Core/AmortizedTask.h`<br>`SkullbonezSource/Core/WorkerPool.cpp`<br>`SkullbonezSource/Core/WorkerPool.h` |
| Accumulator | 3 | 2 | Stored fractional tick state that carries time across frames. | SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp | `SkullbonezSource/Runtime/Input/Input.cpp`<br>`SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp`<br>`SkullbonezSource/Runtime/Simulation/SimulationSystem.h` |
| All-body trajectory | 3 | 3 | Mutual-gravity path record retained for every body, independent of the contact-derived future tree. | SkullbonezSource/Runtime/Prediction/ReplayPrediction.h | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h` |
| Authoring row | 3 | 2 | Cold scene round-trip text paired with one hot collider row. | SkullbonezSource/Physics/ColliderStore.cpp | `SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/UI/UIRenderAuthoringCatalog.h` |
| Body | 3 | 3 | Simulated object state such as pose, velocity, mass, and sleep flag. | SkullbonezSource/Physics/PhysicsApi.h | `SkullbonezSource/Physics/PhysicsApi.h`<br>`SkullbonezSource/Physics/PhysicsBodyStore.cpp`<br>`SkullbonezSource/Physics/PhysicsBodyStore.h` |
| Body simulation limit | 3 | 3 | Scalar cap enforced by a body before solver rows see velocity state. | SkullbonezSource/Physics/PhysicsObjectPolicy.h | `SkullbonezSource/Physics/PhysicsObjectPolicy.cpp`<br>`SkullbonezSource/Physics/PhysicsObjectPolicy.h`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Candidate pair | 3 | 3 | Broadphase-selected body pair awaiting narrowphase testing. | SkullbonezSource/Physics/PhysicsDiagnosticsView.h | `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`<br>`SkullbonezSource/Physics/SolverBroadphaseStage.h`<br>`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Capacity row | 3 | 3 | Fixed registry storage carrying one store's live sizing telemetry without building a heap-backed report. | SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp`<br>`SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h`<br>`SkullbonezSource/UI/UI.h` |
| Capacity session | 3 | 3 | One loaded scene's live-usage window, ending immediately before its store rows are cleared or replaced. | SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp`<br>`SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` |
| Cause tree | 3 | 3 | Contact, solver-row, and predicted-motion graph explaining replay body influence. | SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp` |
| Cause window | 3 | 3 | Resizable replay inspection panel that lists body/contact rows. | SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` |
| CCD (Continuous Collision Detection) | 3 | 3 | Swept collision test that asks whether objects hit during a tick, not only where they end the tick. | SkullbonezSource/Physics/PhysicsWorld.cpp | `SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Physics/TerrainContactManifold.cpp` |
| CLI (Command-Line Interface) | 3 | 2 | Text arguments or scripts used to launch validation and tooling paths. | SkullbonezSource/Runtime/App/Run.h | `SkullbonezSource/Runtime/App/Run.h`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` |
| Collider | 3 | 3 | Shape metadata used to decide what precise collision test applies. | SkullbonezSource/Physics/ColliderStore.cpp | `SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/PhysicsApi.h` |
| Collider descriptor | 3 | 3 | Value packet carrying parsed shape and contact material facts into the physics collider store. | SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Contact policy | 3 | 3 | Terrain and contact thresholds owned by PhysicsEngine so existing and newly added models receive the same physics policy. | SkullbonezSource/Runtime/Scene/SceneWorld.cpp | `SkullbonezSource/Physics/PhysicsObjectPolicy.cpp`<br>`SkullbonezSource/Physics/PhysicsObjectPolicy.h`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Contact row | 3 | 3 | Solver constraint row used to apply impulses at a contact point. | SkullbonezSource/Physics/PhysicsDebugData.h | `SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PhysicsDebugData.h`<br>`SkullbonezSource/Physics/PhysicsWorld.cpp` |
| DRED (Device Removed Extended Data) | 3 | 1 | DX12 diagnostic report for GPU device loss, breadcrumbs, and page-fault clues. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` |
| Engine module | 3 | 1 | A source file with one focused responsibility inside the SkullbonezCore runtime. | SkullbonezSource/Maths/GeometricMath.cpp | `SkullbonezSource/Maths/GeometricMath.cpp`<br>`SkullbonezSource/Maths/GeometricMath.h`<br>`SkullbonezSource/Maths/GeometricStructures.h` |
| Fluid surface | 3 | 2 | World-space Y plane where the fluid medium begins. | SkullbonezSource/Physics/BuoyancySystem.cpp | `SkullbonezSource/Physics/BuoyancySystem.cpp`<br>`SkullbonezSource/Physics/PhysicsWorldForces.h`<br>`SkullbonezSource/World/FluidSurfaceAdjustment.h` |
| Flyout | 3 | 2 | Secondary variant row anchored to one palette entry. | SkullbonezSource/UI/UIEditorMiniPalette.cpp | `SkullbonezSource/UI/UIEditorMiniPalette.cpp`<br>`SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.h` |
| FNV-1a | 3 | 3 | Small deterministic hash used only to prove each surface implementation consumed the same frame values; it is not durable identity or serialization. | SkullbonezSource/UI/OperatorEditorExchange.cpp | `SkullbonezSource/Assets/AssetKeys.h`<br>`SkullbonezSource/Core/StringHash.h`<br>`SkullbonezSource/UI/OperatorEditorExchange.cpp` |
| Future node | 3 | 3 | Causal topology row naming the predicted body, parent, activation frame, contact evidence, and depth that make a child path visible. | SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayPathPackets.h`<br>`SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Generation | 3 | 3 | Reuse identity carried with a slot or handle so stale references can be rejected. | GC0 owner adjudication | `SkullbonezSource/Core/SbDiagnosticStore.h`<br>`SkullbonezSource/Physics/PhysicsHandles.h`<br>`SkullbonezSource/Rendering/DX12/Dx12TextureRegistry.h` |
| Gizmo | 3 | 3 | World-space editor axes or rotation rings used to transform selected models. | SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp | `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Hull identity | 3 | 2 | Cold normalized authored path plus exact canonical scale bits. | SkullbonezSource/Physics/ColliderStore.cpp | `SkullbonezSource/Physics/ColliderStore.cpp`<br>`SkullbonezSource/Physics/ColliderStore.h`<br>`SkullbonezSource/Physics/PhysicsApi.h` |
| HWND (Window Handle) | 3 | 1 | Win32 identifier for the native application window. | SkullbonezSource/Runtime/App/Window.cpp | `SkullbonezSource/Runtime/App/Window.cpp`<br>`SkullbonezSource/Runtime/App/Window.h`<br>`SkullbonezSource/Runtime/Input/Input.h` |
| Input turn result | 3 | 3 | Value-only process request emitted after semantic actions are interpreted; Run applies process-wide policy without rescanning input. | SkullbonezSource/Runtime/App/InputFrame.h | `SkullbonezSource/Runtime/App/InputFrame.h`<br>`SkullbonezSource/Runtime/App/InputFrameExecution.cpp`<br>`SkullbonezSource/Runtime/App/RunFrame.cpp` |
| JSON (JavaScript Object Notation) | 3 | 3 | Text metadata format used inside the manifest chunk. | SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Marker epoch | 3 | 3 | Core identity generation advanced when the registry resets. | SkullbonezSource/Rendering/RenderGpuTimingOwner.h | `SkullbonezSource/Core/Profiler.cpp`<br>`SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp`<br>`SkullbonezSource/Rendering/RenderGpuTimingOwner.h` |
| Mini palette | 3 | 1 | Compact editor placement surface shown while UI is minimized. | SkullbonezSource/UI/UIEditorMiniPalette.cpp | `SkullbonezSource/UI/UIEditorMiniPalette.cpp`<br>`SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.h` |
| PGS (Projected Gauss-Seidel) | 3 | 1 | Iterative constraint-solver method used for bounded contact impulses. | SkullbonezSource/Core/Config.h | `SkullbonezSource/Core/Config.h`<br>`SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/PersistentContactSolver.cpp` |
| Phase cursor | 3 | 3 | Value that permits only the adjacent OC0 phase walk. | SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| PIX | 3 | 1 | Microsoft GPU debugger/profiler that can read engine markers and DX12 object names. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` |
| Platform profiler GPU stack | 3 | 3 | Fixed nesting state that must be closed before any command list is submitted. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` |
| Projection | 3 | 3 | Conversion from the common queue back into established narrow UI command structs consumed by concrete runtime owners. | SkullbonezSource/UI/OperatorEditorExchange.cpp | `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h`<br>`SkullbonezSource/Scene/AuthoredTornadoConfig.h`<br>`SkullbonezSource/UI/OperatorEditorExchange.cpp` |
| Publication | 3 | 3 | Owner-produced save value; SceneWorld's publication borrows its stores only for the duration of this operation. | SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp | `SkullbonezSource/Core/SbDiagnosticStore.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp` |
| Replay target marker | 3 | 3 | Debug overlay outline/ring drawn around a replay body from live body/collider store values. | SkullbonezSource/Runtime/Tools/RuntimeTools.h | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Reset snapshot | 3 | 3 | Value-only copy of owner state preserved across same-scene reset. | SkullbonezSource/Runtime/Scene/SceneResetPreservation.h | `SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneResetPreservation.h` |
| Ribbon | 3 | 3 | Thin render strip used for the laser core and glow. | SkullbonezSource/Runtime/Editor/LauncherLaser.h | `SkullbonezSource/Gameplay/TornadoVisualPass.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| Ring buffer | 3 | 3 | Fixed-size history where new launcher/raycast entries overwrite the oldest slots. | SkullbonezSource/Runtime/Tools/RuntimeTools.h | `SkullbonezSource/Core/Profiler.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Scene object group | 3 | 3 | Parsed metadata that ties multi-part authored objects, such as releasable trees, to a single root scene object. | SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h`<br>`SkullbonezSource/Scene/SceneSnapshotWriter.cpp` |
| SDF (Signed Distance Field) | 3 | 2 | Texture representation used for crisp scalable text rendering. | SkullbonezSource/Rendering/Text.cpp | `SkullbonezSource/Rendering/Text.cpp`<br>`SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp` |
| Semantic action | 3 | 3 | Fixed ordered input event derived from sampled key edges, independent of the platform's live hardware state. | SkullbonezSource/Runtime/App/InputFrameExecution.cpp | `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`<br>`SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`<br>`SkullbonezSource/Runtime/Input/InputRouter.h` |
| Shared editor view | 3 | 3 | Frame-owned storage passed to the operator-editor composer and then consumed by the selected development frontend. | SkullbonezSource/Runtime/App/RunFrame.cpp | `SkullbonezSource/Runtime/App/RunFrame.cpp`<br>`SkullbonezSource/Runtime/Render/UiTextPass.cpp`<br>`SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp` |
| Sleep island | 3 | 2 | Connected body group that may deactivate only as a unit. | SkullbonezSource/Physics/PhysicsWorld.cpp | `SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h`<br>`SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Step policy | 3 | 3 | Once-per-solve normalized view of authored contact bounds used by both object and terrain rows. | SkullbonezSource/Physics/PersistentContactSolver.cpp | `SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PersistentContactSolver.h`<br>`SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` |
| UIRect | 3 | 1 | Pixel-space rectangle shared by hit testing and drawing. | SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h | `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` |
| Velocity edit | 3 | 3 | Replay tool that displays and edits linear/angular velocity on the current path target. | SkullbonezSource/Runtime/App/ReplayRuntime.h | `SkullbonezSource/Physics/PhysicsEngine.cpp`<br>`SkullbonezSource/Runtime/App/ReplayRuntime.cpp`<br>`SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Virtual key | 3 | 3 | Win32 integer key code sampled in DeviceInputFrame. | SkullbonezSource/Runtime/Input/InputController.Bindings.h | `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp`<br>`SkullbonezSource/Runtime/Input/InputController.Bindings.h`<br>`SkullbonezSource/UI/UIInput.cpp` |
| Widget view | 3 | 3 | Short-lived typed references to owner-held controls whose bounds are shared by input hit testing and drawing. | SkullbonezSource/UI/UIWindowInteractionOwner.h | `SkullbonezSource/UI/UI.cpp`<br>`SkullbonezSource/UI/UIWindowInteractionOwner.cpp`<br>`SkullbonezSource/UI/UIWindowInteractionOwner.h` |
| Win32 | 3 | 3 | Windows desktop API used for the app window, messages, and process integration. | SkullbonezSource/Runtime/Input/Input.h | `SkullbonezSource/Runtime/Input/Input.cpp`<br>`SkullbonezSource/Runtime/Input/Input.h`<br>`SkullbonezSource/Runtime/Input/InputRouter.h` |
| ABI (Application Binary Interface) | 2 | 2 | The compiled binding contract between C++ root parameters, shader registers, and draw-time texture slots. | SkullbonezSource/Rendering/ShaderContracts.h | `SkullbonezSource/Rendering/ShaderContracts.h`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Acceptance ledger | 2 | 2 | Detached facts describing commands accepted this frame. | SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h | `SkullbonezSource/Runtime/App/InputFrame.cpp`<br>`SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h` |
| Active cell | 2 | 2 | Occupied persistent or swept-overlay grid cell in the latest committed physics step. | SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h | `SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp` |
| Active rotation | 2 | 1 | Rotation that moves a vector in a fixed world basis. | SkullbonezSource/Maths/Matrix4.cpp | `SkullbonezSource/Maths/Matrix4.cpp`<br>`SkullbonezSource/Maths/Quaternion.cpp` |
| Active vortex | 2 | 2 | An authored vortex after spawn, growth, shrink, drift, and pair-repulsion have been evaluated at the current gameplay time. | SkullbonezSource/Gameplay/TornadoField.h | `SkullbonezSource/Gameplay/TornadoField.h`<br>`SkullbonezSource/Gameplay/TornadoGameplay.cpp` |
| Amortized build | 2 | 2 | Bounded worker slices spread prediction work across frames. | SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` |
| Antiparallel normal | 2 | 2 | A terrain normal pointing exactly opposite world up. | SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h | `SkullbonezSource/Maths/Matrix4.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h` |
| Artifact | 2 | 1 | File written by runtime tools, diagnostics, captures, or saves. | SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp | `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h` |
| Artifact path | 2 | 2 | Validation-facing output path that must stay stable. | SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` |
| Asset affiliation | 2 | 2 | Library/asset/instance/part provenance kept separately from behavior grouping. | SkullbonezSource/Runtime/Scene/SceneEntityStore.h | `SkullbonezSource/Runtime/Scene/SceneEntityStore.h`<br>`SkullbonezSource/Scene/SceneSnapshotWriter.cpp` |
| Asset primitive | 2 | 2 | Single spawned collision body inside a placeable asset container, such as a box, sphere, or convex hull. | SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp | `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp` |
| Authored hull | 2 | 2 | Baked convex hull asset used for editor-placeable collision geometry and preview outlines. | SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h | `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h` |
| Authored path colour | 2 | 2 | Scene material base colour reused by orbital guide and predicted trajectory ribbons. | SkullbonezSource/Runtime/App/ReplayRuntime.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` |
| Authored scene | 2 | 2 | Parsed `.scene.json` data that explicitly drives runtime setup. | SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h` |
| Auto-cycle | 2 | 2 | Screenshot automation that advances capture targets over time. | SkullbonezSource/Runtime/Capture/CaptureController.h | `SkullbonezSource/Runtime/Capture/CaptureController.cpp`<br>`SkullbonezSource/Runtime/Capture/CaptureController.h` |
| Automation scene | 2 | 2 | Scene with screenshot/perf/exit behavior that should keep the UI hidden unless explicitly authored otherwise. | SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h | `SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp` |
| Awake slot | 2 | 2 | Dispatch position mapped to one ascending dynamic body index. | SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp` |
| Backdrop | 2 | 2 | Translucent panel drawn before chrome to separate controls from the world view. | SkullbonezSource/UI/UIBackdropBlur.h | `SkullbonezSource/UI/UIBackdropBlur.cpp`<br>`SkullbonezSource/UI/UIBackdropBlur.h` |
| Billboard | 2 | 1 | Camera-facing quad built from a world-space segment and view direction. | SkullbonezSource/Runtime/Editor/LauncherLaser.cpp | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| Body record | 2 | 1 | Physics-owned snapshot of pose, velocity, mass, and inertia used by the joint solver. | SkullbonezSource/Physics/Ragdoll.cpp | `SkullbonezSource/Physics/Ragdoll.cpp`<br>`SkullbonezSource/Physics/Ragdoll.h` |
| Buoyancy | 2 | 2 | Upward force from displaced fluid volume; depends on gravity, fluid density, and submerged volume. | SkullbonezSource/World/WorldEnvironment.h | `SkullbonezSource/World/WorldEnvironment.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| Callback bridge | 2 | 2 | The process-local state that lets Win32 callbacks enqueue mouse data until the frame loop consumes it. | SkullbonezSource/Runtime/Input/Input.h | `SkullbonezSource/Runtime/App/Window.cpp`<br>`SkullbonezSource/Runtime/Input/Input.h` |
| Candidate | 2 | 2 | Absolute live-world linear velocity requested for the ship. | SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp | `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp`<br>`SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp` |
| Canonical pair order | 2 | 2 | Ascending normalized `(minIndex, maxIndex)` order, independent of cell-bucket discovery history. | SkullbonezSource/Physics/SpatialGrid.h | `SkullbonezSource/Physics/SpatialGrid.h`<br>`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` |
| Canonical publisher | 2 | 2 | The single claimed list instance allowed to mutate one conceptual owner's capacity row; copies and same-name clones remain silent. | SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h`<br>`SkullbonezSource/Physics/PhysicsFixedList.h` |
| Capacity bytes | 2 | 2 | Vector storage already reserved for records or point arrays. | SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp | `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`<br>`SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp` |
| Capacity snapshot | 2 | 2 | Fixed value rows copied from the allocator registry only while the Memory tab is visible. | SkullbonezSource/Runtime/Render/UiTextPass.cpp | `SkullbonezSource/Runtime/Render/UiTextPass.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.cpp` |
| Capture owner | 2 | 2 | Concrete DX12 component that supplies screenshot readback. | SkullbonezSource/Runtime/Capture/CaptureSystem.h | `SkullbonezSource/Runtime/Capture/CaptureSystem.cpp`<br>`SkullbonezSource/Runtime/Capture/CaptureSystem.h` |
| Capture result | 2 | 1 | Value outcome folded into the fixed accepted-request batch. | SkullbonezSource/Runtime/Capture/CaptureController.cpp | `SkullbonezSource/Runtime/Capture/CaptureController.cpp`<br>`SkullbonezSource/Runtime/Capture/CaptureController.h` |
| Capture state | 2 | 2 | Window drag, resize, slider, and native-mouse ownership that can span multiple frames. | SkullbonezSource/UI/UIWindowInteractionOwner.h | `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`<br>`SkullbonezSource/UI/UIWindowInteractionOwner.h` |
| Cause row | 2 | 2 | One body, contact, solver, or prediction explanation in the replay causality tree. | SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h | `SkullbonezSource/Runtime/Replay/ReplayAuthoring.h`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h` |
| Cinematic deck | 2 | 2 | A queue of concept/cinematic scenes cycled as one authored visual look set. | SkullbonezSource/Runtime/Scene/SceneSessionState.h | `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneSessionState.h` |
| Cinematic override | 2 | 1 | Bitmask-selected render fields layered over defaults. | SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h | `SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` |
| Cold flush | 2 | 2 | Submit/wait/reset retry allowed outside steady gameplay when an upload reservation does not fit. | SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h | `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` |
| Collider authoring row | 2 | 2 | Cold material text paired with the live collider row for exact scene round trips. | SkullbonezSource/Scene/SceneSnapshotWriter.cpp | `SkullbonezSource/Physics/PhysicsEngine.cpp`<br>`SkullbonezSource/Scene/SceneSnapshotWriter.cpp` |
| Commit count | 2 | 2 | Number of fixed physics ticks the runtime owner must execute after the scheduler has updated accumulator state. | SkullbonezSource/Runtime/Simulation/SimulationSystem.h | `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp`<br>`SkullbonezSource/Runtime/Simulation/SimulationSystem.h` |
| Compiled transition | 2 | 2 | Render-graph state edge assigned to a specific pass and resource before callbacks record live commands. | SkullbonezSource/Rendering/RenderCommandTypes.h | `SkullbonezSource/Rendering/RenderCommandTypes.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` |
| Contact body view | 2 | 2 | Pose-only body input used by narrowphase so the manifold builder does not need to borrow unrelated owner storage. | SkullbonezSource/Physics/ObjectContactManifold.h | `SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h` |
| Contact highlight | 2 | 2 | Render-only feedback alpha for red fixed-body hits. | SkullbonezSource/Rendering/RenderInstanceStore.h | `SkullbonezSource/Rendering/RenderInstanceStore.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Contact release | 2 | 2 | Editor/authored behavior that lets fixed decoration become dynamic after a large impact. | SkullbonezSource/Runtime/Editor/EditorHullAssets.h | `SkullbonezSource/Runtime/Editor/EditorHullAssets.h`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` |
| Contact sweep | 2 | 2 | Conservative object/object time-of-impact query used before exact manifold generation and solver response. | SkullbonezSource/Physics/ObjectContactManifold.h | `SkullbonezSource/Physics/ObjectContactManifold.cpp`<br>`SkullbonezSource/Physics/ObjectContactManifold.h` |
| Content signature | 2 | 1 | Hash of UI-visible values used to invalidate cached draws. | SkullbonezSource/UI/UIFrameComposition.cpp | `SkullbonezSource/UI/UIFrameComposition.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.h` |
| Cross-scene pause lock | 2 | 2 | Scene-owned fact that forces step-held physics even when the active camera or tool would normally keep simulation running. | SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |
| Debug build | 2 | 1 | Configuration where validation asserts are active. | SkullbonezSource/Core/LockOrderValidator.cpp | `SkullbonezSource/Core/LockOrderValidator.cpp`<br>`SkullbonezSource/Core/LockOrderValidator.h` |
| Dense row | 2 | 2 | Compact store array index used by hot simulation scans. | SkullbonezSource/Physics/PhysicsHandles.h | `SkullbonezSource/Physics/PhysicsHandles.h`<br>`SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp` |
| Descriptor heap | 2 | 2 | DX12 table of descriptor rows; shader-visible heaps can be indexed by GPU commands. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.h | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` |
| Development tool owner | 2 | 2 | A thread-local, hard-capped ImGui or Tracy scope that is permitted only when the shared development capability is compiled. | SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp`<br>`SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` |
| Development UI command | 2 | 2 | Fixed presentation or native-window request emitted by the sequencer and applied synchronously by this automation owner. | SkullbonezSource/Runtime/Automation/InteractionAutomationController.h | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`<br>`SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Diagnostic-name table | 2 | 2 | Fixed pointer table whose pointed-to scene names remain owned by stable scene metadata. | SkullbonezSource/Physics/PhysicsDiagnosticsSink.h | `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`<br>`SkullbonezSource/Physics/PhysicsDiagnosticsSink.h` |
| Diagnostics artifact | 2 | 1 | File produced for validation, profiling, or analysis. | SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h` |
| Diagnostics view | 2 | 2 | Synchronous spans and references into one PhysicsEngine. | SkullbonezSource/Physics/PhysicsDiagnosticsView.h | `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`<br>`SkullbonezSource/Physics/PhysicsEngine.h` |
| Director playback | 2 | 2 | Runtime camera mode that applies authored shot-list poses plus optional phase styles and prediction reveal pacing. | SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h | `SkullbonezSource/Runtime/Camera/CameraControlState.h`<br>`SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h` |
| DTO (Data Transfer Object) | 2 | 2 | Plain value record passed across a subsystem boundary so the receiver can serialize data without owning the source. | SkullbonezSource/Physics/PhysicsDiagnosticsModel.h | `SkullbonezSource/Physics/PhysicsDiagnosticsModel.h`<br>`SkullbonezSource/Scene/AuthoredTornadoConfig.h` |
| Durable artifact | 2 | 2 | Saved replay payload reloaded to prove report facts survive the writer/reader boundary. | SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp`<br>`SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h` |
| Early-exit probe | 2 | 2 | Bounded validation or generation mode that does not enter the application run loop. | SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h | `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`<br>`SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h` |
| Editor command | 2 | 1 | Intent emitted by a widget and applied later by runtime code. | SkullbonezSource/UI/UITabEditor.cpp | `SkullbonezSource/UI/UITabEditor.cpp`<br>`SkullbonezSource/UI/UITabEditor.h` |
| Event cursor | 2 | 2 | Monotonic sequence marker stored on checkpoints so restore can resume timeline events without replaying old side effects. | SkullbonezSource/Runtime/App/ReplayValidation.cpp | `SkullbonezSource/Runtime/App/ReplayValidation.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp` |
| Fault injection | 2 | 2 | Debug-only synthetic failure used to prove that queue work stops before the first unsafe submission. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h`<br>`SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` |
| Fixed-step | 2 | 1 | Deterministic mode that advances physics by one fixed delta per requested tick instead of wall-clock time. | SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp | `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp`<br>`SkullbonezSource/Runtime/Simulation/SimulationSystem.h` |
| Fluid surface adjustment | 2 | 2 | Typed signed velocity issued by input in world units. | SkullbonezSource/World/WorldEnvironment.h | `SkullbonezSource/World/WorldEnvironment.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| FNV (Fowler-Noll-Vo) | 2 | 2 | Small string hash used here to identify stable scope paths without storing dynamic lookup tables. | SkullbonezSource/Rendering/DrawCallTrace.cpp | `SkullbonezSource/Rendering/DrawCallTrace.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Focus mask | 2 | 1 | Dense frame-local rows faded around the selected path family. | SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h` |
| Force frame | 2 | 2 | Ordered cylindrical field values plus per-body timer spans borrowed by Physics for exactly one fixed tick. | SkullbonezSource/Gameplay/TornadoGameplay.h | `SkullbonezSource/Gameplay/TornadoGameplay.cpp`<br>`SkullbonezSource/Gameplay/TornadoGameplay.h` |
| Fork-join | 2 | 1 | Pattern where the main thread splits work, workers run chunks, and the main thread waits before merging results. | SkullbonezSource/Core/WorkerPool.cpp | `SkullbonezSource/Core/WorkerPool.cpp`<br>`SkullbonezSource/Core/WorkerPool.h` |
| Frame publication | 2 | 1 | One-time projection of owner-backed rows and values for synchronous render-pass consumption during the current frame. | SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp | `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp`<br>`SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h` |
| Freshness manifest | 2 | 1 | Checked-in JSON map from compiler inputs to baked bytes. | SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp | `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.h` |
| Generated scene | 2 | 2 | Runtime-created demo scene with deterministic cameras and model placement. | SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` |
| Gesture | 2 | 1 | Active pointer operation that owns capture until it ends. | SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |
| Ghost request | 2 | 2 | Typed predicted pose and material treatment consumed by the ordinary object-shape renderer. | SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Glyph advance | 2 | 1 | Horizontal distance added after laying out one character. | SkullbonezSource/UI/UIFontMetrics.cpp | `SkullbonezSource/UI/UIFontMetrics.cpp`<br>`SkullbonezSource/UI/UIFontMetrics.h` |
| GPU (Graphics Processing Unit) | 2 | 2 | Hardware device that owns renderer resources such as meshes, shaders, textures, and reflection targets. | SkullbonezSource/World/WorldEnvironment.h | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| GPU drain | 2 | 2 | Ordered close, submit, fence wait, and command-list reopen that must finish before a runtime owner destroys resources. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h` |
| GPU timing sample | 2 | 2 | Completed renderer measurement submitted as a value keyed by the Core-owned marker hash. | SkullbonezSource/Core/Profiler.h | `SkullbonezSource/Core/Profiler.h`<br>`SkullbonezSource/Rendering/RenderGpuTimingOwner.h` |
| Graphics stress | 2 | 2 | Deterministic fuzzer that mutates render settings, UI state, and scene loads to reproduce DX12 lifetime or resource bugs. | SkullbonezSource/Runtime/Capture/GraphicsStressController.h | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h`<br>`SkullbonezSource/Runtime/Capture/GraphicsStressController.h` |
| HDC (Handle to Device Context) | 2 | 1 | Win32 drawing context associated with the window. | SkullbonezSource/Runtime/App/Window.cpp | `SkullbonezSource/Runtime/App/Window.cpp`<br>`SkullbonezSource/Runtime/App/Window.h` |
| Heat | 2 | 1 | Per-cell collision count used only to darken the debug color. | SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp | `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h` |
| Hitch event | 2 | 2 | A fixed-step request whose whole-tick demand exceeds the per-frame catch-up cap; excess whole ticks are intentionally discarded. | SkullbonezSource/Runtime/Simulation/SimulationSystem.h | `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp`<br>`SkullbonezSource/Runtime/Simulation/SimulationSystem.h` |
| Hold mode | 2 | 1 | Press-duration gesture that opens tree or ragdoll variants. | SkullbonezSource/UI/UIEditorMiniPalette.cpp | `SkullbonezSource/UI/UIEditorMiniPalette.cpp`<br>`SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp` |
| Hot body fields | 2 | 1 | Physics-owned arrays holding fixed/sleep/velocity state for the current tick. | SkullbonezSource/Physics/SleepIslandSystem.cpp | `SkullbonezSource/Physics/SleepIslandSystem.cpp`<br>`SkullbonezSource/Physics/SleepIslandSystem.h` |
| Hot control | 2 | 2 | Pointer control when that row is enabled. | SkullbonezSource/Runtime/UI/RuntimeUiSurface.h | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp`<br>`SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` |
| Hot reload | 2 | 2 | Explicit developer action that reruns the offline bake, then asks live shader owners to adopt hash-verified bytes transactionally. | SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.h | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`<br>`SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.h` |
| Hull scale | 2 | 1 | Per-axis size multiplier for convex hull editor assets. | SkullbonezSource/Runtime/Editor/EditorTools.cpp | `SkullbonezSource/Runtime/Editor/EditorTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTools.h` |
| Input edge | 2 | 1 | Transition from not pressed to pressed, used for one-shot commands. | SkullbonezSource/Runtime/Input/InputController.cpp | `SkullbonezSource/Runtime/Input/InputController.cpp`<br>`SkullbonezSource/Runtime/Input/InputController.h` |
| Input turn | 2 | 2 | Ordered frame interval that samples hardware, offers actions to UI/tools/replay, and commits accepted capture/default/scene requests. | SkullbonezSource/Runtime/App/InputFrame.h | `SkullbonezSource/Runtime/App/InputFrame.h`<br>`SkullbonezSource/Runtime/App/InputFrameExecution.cpp` |
| Instant build | 2 | 2 | One worker submission that completes the remaining horizon. | SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` |
| Interaction owner | 2 | 2 | Concrete owner of persistent UI controls and cross-frame pointer/capture state; it emits typed command values rather than mutating runtime subsystems. | SkullbonezSource/UI/UI.h | `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`<br>`SkullbonezSource/UI/UI.h` |
| Interaction signature | 2 | 1 | Hash of pointer/focus state used to invalidate hit data. | SkullbonezSource/UI/UIFrameComposition.cpp | `SkullbonezSource/UI/UIFrameComposition.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.h` |
| Intercept assertion | 2 | 2 | Lane-P proof over the replay-owned closest-approach snapshot; it observes distance, ETA, and contact without owning the scan. | SkullbonezSource/Runtime/Automation/InteractionAutomationController.h | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`<br>`SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Live edge | 2 | 2 | The newest retained replay sample. | SkullbonezSource/Runtime/Replay/ReplayScrubber.h | `SkullbonezSource/Runtime/Replay/ReplayScrubber.h`<br>`SkullbonezSource/Runtime/Replay/ReplayTimelinePackets.h` |
| Live graph | 2 | 2 | Production callback schedule accumulated across the frame. | SkullbonezSource/Rendering/RenderPipeline.h | `SkullbonezSource/Rendering/RenderPipeline.cpp`<br>`SkullbonezSource/Rendering/RenderPipeline.h` |
| Live style | 2 | 2 | Control-folder protocol that applies style JSON and requests a screenshot without restarting the process. | SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h`<br>`SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h` |
| Load preparation | 2 | 2 | Failure-safe phase before teardown and object population. | SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h | `SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp` |
| Load request | 2 | 2 | Accepted navigation result containing an optional scene load and whether the runtime should become interactive first. | SkullbonezSource/Runtime/Scene/SceneLoadRequest.h | `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h`<br>`SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp` |
| Material intent | 2 | 2 | Renderer-neutral description of surface style and texture selection. | SkullbonezSource/Rendering/RenderInstanceStore.h | `SkullbonezSource/Rendering/RenderInstanceStore.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Material table | 2 | 2 | Fixed t4 texture storing default material response values by material kind for the current object shader. | SkullbonezSource/Rendering/RenderMaterial.h | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp`<br>`SkullbonezSource/Rendering/RenderMaterial.h` |
| Memory waterline | 2 | 2 | Compact F6 overlay that tracks known engine memory and pinned reserve-growth events without polling process memory. | SkullbonezSource/UI/UITabMemory.h | `SkullbonezSource/UI/UITabMemory.cpp`<br>`SkullbonezSource/UI/UITabMemory.h` |
| Model capacity | 2 | 2 | Active object capacity limit. | SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| Model frame view | 2 | 2 | Borrowed render, physics, debug, and policy facts whose lifetime ends before the next frame begins. | SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h | `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h` |
| Mutual-gravity pair scratch | 2 | 2 | Preallocated triangular force table whose unique slots let workers compute pairs without racing or regrouping additions. | SkullbonezSource/Physics/PhysicsWorld.h | `SkullbonezSource/Physics/PhysicsWorld.cpp`<br>`SkullbonezSource/Physics/PhysicsWorld.h` |
| Numbered path | 2 | 1 | Prefix plus sequence number chosen to avoid overwriting an existing artifact. | SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp | `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h` |
| Operator-owned state | 2 | 2 | Live runtime choice made after scene load. | SkullbonezSource/Runtime/Scene/SceneResetPreservation.h | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneResetPreservation.h` |
| Orthogonal basis | 2 | 2 | Three perpendicular unit axes; its transpose is also its inverse. | SkullbonezSource/Maths/RotationMatrix.h | `SkullbonezSource/Maths/RotationMatrix.cpp`<br>`SkullbonezSource/Maths/RotationMatrix.h` |
| Overlay state view | 2 | 2 | Read-only replay publication borrowed for one late pass. | SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h | `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| Overlay viewport | 2 | 1 | Coupled pixel width and height used by overlay layout; the render-command target remains an explicit synchronous borrow. | SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h | `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h`<br>`SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| Override mask | 2 | 2 | Bitfield that records which optional JSON fields were authored so unspecified values keep engine.cfg defaults. | SkullbonezSource/Scene/AuthoredScene.h | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp`<br>`SkullbonezSource/Scene/AuthoredScene.h` |
| Owner | 2 | 1 | The tool or subsystem currently allowed to consume world input. | SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |
| Owner event | 2 | 2 | Stable wire-coded record of accepted owner work. | SkullbonezSource/Runtime/Replay/ReplayEventCommand.h | `SkullbonezSource/Runtime/Replay/ReplayEventCommand.h`<br>`SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp` |
| Owner view | 2 | 2 | Three synchronous const store references plus Gameplay byte values projected by SceneWorld. | SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h | `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderHost.h` |
| Pair island | 2 | 1 | Candidate pairs connected through shared body indices. | SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp` |
| Pair-source cell | 2 | 2 | Current-generation cell reached by an awake body; dormant membership remains resident even when the cell is not visited this step. | SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Pair-source stamp | 2 | 2 | Frame generation marking a cell reached by an awake body; production candidate collection skips unstamped sleep-only cells. | SkullbonezSource/Physics/SpatialGrid.h | `SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h` |
| Parent directory | 2 | 1 | Folder portion of a requested output path. | SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp | `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h` |
| Pending awake queue | 2 | 2 | Fixed-capacity worker publication rows folded into the sorted owner list at sequencer barriers. | SkullbonezSource/Physics/Stages/PhysicsSleepController.h | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Perf log | 2 | 1 | CSV-style runtime performance artifact written during runs. | SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h` |
| Persistent contact | 2 | 2 | Solver row retained long enough to warm-start a matching contact feature on the next fixed tick. | SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h | `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`<br>`SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Persistent membership | 2 | 2 | Cell occupancy retained across fixed steps until a body's integer cell range changes. | SkullbonezSource/Physics/SpatialGrid.h | `SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/SpatialGrid.h` |
| Persistent tail | 2 | 1 | Fixed suffix excluded from ordinary frame resets so retained GPU geometry can reuse cold-created upload memory across frames. | SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp | `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` |
| Physics diagnostic command | 2 | 1 | One-frame key or UI request that changes debug presentation state, not simulation state. | SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` |
| Physics-debug override | 2 | 1 | Visualization-only startup request that must not alter solver state. | SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp | `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`<br>`SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h` |
| Pick purpose | 2 | 2 | The tool-specific policy for interpreting a mouse ray. | SkullbonezSource/Runtime/Interaction/RuntimePickService.h | `SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimePickService.h` |
| Pipeline cursor | 2 | 2 | Selected physics pipeline stage rendered by the debug pass. | SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp` |
| Placement gesture | 2 | 2 | Mouse drag and wheel input used to size an object before placement commits. | SkullbonezSource/Runtime/Editor/EditorTools.h | `SkullbonezSource/Runtime/Editor/EditorTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTools.h` |
| Placement recipe | 2 | 2 | Typed editor data that describes a tree, house, building, or hull-backed primitive selected from the editor tab. | SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h | `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h` |
| POD (Plain Old Data) | 2 | 2 | Simple value type with no ownership or behavior. | SkullbonezSource/Core/MainMemoryStats.h | `SkullbonezSource/Core/MainMemoryStats.h`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Pointer arbitration | 2 | 2 | Ordered phase cursor that gives the first consuming world-pointer stage exclusive ownership. | SkullbonezSource/Runtime/Input/InputRouter.h | `SkullbonezSource/Runtime/Input/InputRouter.cpp`<br>`SkullbonezSource/Runtime/Input/InputRouter.h` |
| Pool slot | 2 | 2 | Compiler-assigned alias bucket for non-overlapping transient lifetimes with matching descriptor needs. | SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h | `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h`<br>`SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h` |
| Post-step output | 2 | 2 | Bounded physics facts borrowed synchronously by presentation. | SkullbonezSource/Runtime/Scene/SceneWorld.h | `SkullbonezSource/Runtime/Scene/SceneController.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.h` |
| Prefix digest | 2 | 2 | Digest rebuilt from every currently published trajectory point. | SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h | `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` |
| Prepared prefix | 2 | 2 | Published rows whose topology and trajectories were brought into coherence by the frame thread for one render pass. | SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h` |
| Presentation alpha | 2 | 2 | Bounded leftover accumulator fraction used only to display between the previous and current completed physics poses. | SkullbonezSource/Runtime/Simulation/SimulationSystem.h | `SkullbonezSource/Runtime/Simulation/SimulationSystem.h`<br>`SkullbonezSource/Runtime/UI/RuntimeViewModel.h` |
| Presentation state | 2 | 2 | Operator-selected overlay, water, terrain, and physics debug policy sampled into render values each frame. | SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h | `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`<br>`SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h` |
| Presentation track | 2 | 2 | Body poses, camera, and world display fields used for smooth visual scrubbing. | SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h | `SkullbonezSource/Runtime/App/ReplayRuntime.h`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Presented generation | 2 | 2 | Replacement prefix prepared by the frame thread and therefore safe to compare with the retained prediction. | SkullbonezSource/Runtime/Automation/InteractionAutomationController.h | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`<br>`SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Preview catalog | 2 | 2 | Renderer-owned frame snapshot that maps the UI's stable catalog index to one current texture handle and its presentation metadata. | SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.cpp` |
| Proceed policy | 2 | 2 | Value packet that freezes the sampled step edge and cross-scene pause decision for one frame. | SkullbonezSource/Runtime/Scene/SceneController.h | `SkullbonezSource/Runtime/Scene/SceneController.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneController.h` |
| Profiler connection snapshot | 2 | 2 | Three fixed booleans copied from the Tracy owner without a process scan, socket probe, string construction, or growth. | SkullbonezSource/Runtime/Render/UiTextPass.cpp | `SkullbonezSource/Runtime/Render/UiTextPass.cpp`<br>`SkullbonezSource/UI/UIFrameComposition.cpp` |
| Ragdoll part | 2 | 2 | One model body in the generated simple ragdoll assembly. | SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` |
| RayT | 2 | 2 | Distance along the supplied pick ray to the first shape hit. | SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h`<br>`SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp` |
| Readback buffer | 2 | 2 | CPU-readable landing resource for a GPU texture copy. | SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h`<br>`SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` |
| Record version | 2 | 2 | Monotonic identity for a replaced record; readers can detect replacement without comparing point arrays. | SkullbonezSource/Runtime/Prediction/TrajectoryStore.h | `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`<br>`SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h` |
| Render diagnostics | 2 | 1 | Renderer capability borrowed to name child draw-trace scopes without reopening global renderer access. | SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp | `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.h` |
| Render instance | 2 | 2 | CPU-side record describing one model's draw transform and material intent. | SkullbonezSource/Rendering/RenderInstanceStore.h | `SkullbonezSource/Rendering/RenderInstanceStore.cpp`<br>`SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Render pass | 2 | 2 | A named slice of frame rendering with explicit inputs, outputs, and GPU resource ownership. | SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` |
| Render pose | 2 | 1 | The eye/view/up triple actually used for the current frame; it can differ from the selected camera slot while a tween is active. | SkullbonezSource/Runtime/Camera/CameraCollection.cpp | `SkullbonezSource/Runtime/Camera/CameraCollection.cpp`<br>`SkullbonezSource/Runtime/Camera/CameraCollection.h` |
| Replay probe | 2 | 2 | Debug-only command-line workflow that validates one replay behavior and reports a machine-readable Lane P result. | SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp | `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp`<br>`SkullbonezSource/Runtime/App/ReplayValidation.cpp` |
| Replay ribbon | 2 | 2 | Screen-space-width overlay stroke generated from replay path segments, with an analytic edge and optional selected-path halo. | SkullbonezSource/Runtime/Tools/RuntimeTools.h | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Replay transfer | 2 | 2 | Deterministic copy between owned sleep rows and the solver snapshot. | SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp` |
| Replay visual sample | 2 | 2 | Compact snapshot of tool visuals restored while replay scrubbing so debug feedback follows recorded frames. | SkullbonezSource/Runtime/Tools/RuntimeTools.h | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Request batch | 2 | 2 | Ordered fixed-capacity copy drained at one frame checkpoint. | SkullbonezSource/Runtime/Scene/SceneRequestQueue.h | `SkullbonezSource/Runtime/Scene/SceneController.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneRequestQueue.h` |
| Required contact | 2 | 2 | Named body pair that must touch before automation completes. | SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`<br>`SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h` |
| Resource context | 2 | 2 | Creation/rebuild-only render factory bundle used by EnsureGpuResources methods, not by draw methods. | SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.h` |
| Resource state | 2 | 2 | DX12 usage mode for a resource, such as render target, shader read, copy source, or present. | SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp | `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` |
| Restitution | 2 | 2 | Bounce response copied from collider material data into contact views for diagnostics and future solver inputs. | SkullbonezSource/Physics/PhysicsApi.h | `SkullbonezSource/Physics/PersistentContactSolver.cpp`<br>`SkullbonezSource/Physics/PhysicsApi.h` |
| Retained draw stream | 2 | 2 | Fixed-capacity UI command/text storage reused by this pass instead of growing or consuming large nested stack frames. | SkullbonezSource/Runtime/Render/UiTextPass.cpp | `SkullbonezSource/Runtime/Render/UiTextPass.cpp`<br>`SkullbonezSource/UI/UI.cpp` |
| Retained ribbon chunk | 2 | 2 | Fixed compact segment slice appended by prediction; its physical handle is stable while packet commands sort it canonically. | SkullbonezSource/Runtime/Tools/RuntimeTools.h | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Retention window | 2 | 2 | Maximum authored duration requested for retained past samples. | SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h | `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` |
| Retirement quarantine | 2 | 2 | Fixed queue holding resources or descriptor rows until a covering fence completes. | SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h | `SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp`<br>`SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h` |
| Reveal cursor | 2 | 2 | Monotonic presentation frame reached by the prediction clock. | SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h | `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp`<br>`SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` |
| Root signature | 2 | 2 | DX12 binding contract that declares which descriptor tables and constants shaders may access. | SkullbonezSource/Rendering/DX12/RenderBackendDX12.h | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`<br>`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` |
| Run-value directive | 2 | 2 | Value-bearing Run, replay, UI-stress, or graphics-stress option whose result belongs to the launch-policy packet. | SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h | `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`<br>`SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h` |
| RVIS | 2 | 2 | Ordered packet identity, typed counts, and exact render-buffer rows. | SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| RVPD | 2 | 2 | Bounded typed prediction state used by non-presenting round-trip checks. | SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Save publication | 2 | 2 | Detached owner-produced value containing that owner's persisted fields; the world publication borrows stable stores synchronously. | SkullbonezSource/Runtime/Scene/SceneSaveOperations.h | `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h`<br>`SkullbonezSource/Scene/SceneSnapshotWriter.h` |
| Scale lock | 2 | 2 | Rule that keeps authored multi-part tree/root proportions stable. | SkullbonezSource/Runtime/Editor/EditorTools.h | `SkullbonezSource/Runtime/Editor/EditorTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTools.h` |
| Scene capacity | 2 | 2 | Maximum number of model/body/collider rows the runtime can address in one loaded scene. | SkullbonezSource/Core/SceneCapacity.h | `SkullbonezSource/Core/SceneCapacity.h`<br>`SkullbonezSource/Physics/PhysicsSceneVectorReserve.h` |
| Scene entity | 2 | 2 | Durable scene-owned identity, display, material, and asset row committed beside the live physics body. | SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`<br>`SkullbonezSource/Runtime/Scene/SceneEntityStore.h` |
| Scene request | 2 | 2 | Deferred load, reset, create, or defaults-save owner intent. | SkullbonezSource/Runtime/Scene/SceneController.h | `SkullbonezSource/Runtime/Scene/SceneController.h`<br>`SkullbonezSource/Runtime/Scene/SceneRequestQueue.h` |
| Scene session | 2 | 2 | Current scene state plus controller-owned queue navigation data. | SkullbonezSource/Runtime/Scene/SceneController.h | `SkullbonezSource/Runtime/Scene/SceneController.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneController.h` |
| Scene-object group | 2 | 2 | Scene-owned behavior metadata that keeps multi-part editor prefabs, such as releasable trees, tied to one stable root object id. | SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp | `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Screenshot request | 2 | 1 | Runtime state describing when and where to capture pixels. | SkullbonezSource/Runtime/Capture/CaptureController.cpp | `SkullbonezSource/Runtime/Capture/CaptureController.cpp`<br>`SkullbonezSource/Runtime/Capture/CaptureController.h` |
| SEH (Structured Exception Handling) | 2 | 2 | Windows process exception mechanism used to capture access violations and similar faults. | SkullbonezSource/Runtime/Startup/StartupCrashLogging.h | `SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp`<br>`SkullbonezSource/Runtime/Startup/StartupCrashLogging.h` |
| Shader handle | 2 | 2 | Runtime id that resolves to renderer-owned shader state. | SkullbonezSource/Runtime/Editor/LauncherLaser.h | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.h` |
| Sky feature | 2 | 2 | Toggle for sky, clouds, god rays, or volumetric lighting. | SkullbonezSource/UI/UITabSky.h | `SkullbonezSource/UI/UITabSky.cpp`<br>`SkullbonezSource/UI/UITabSky.h` |
| Sky slider | 2 | 2 | Focused cinematic parameter slider owned by this tab. | SkullbonezSource/UI/UITabSky.h | `SkullbonezSource/UI/UITabSky.cpp`<br>`SkullbonezSource/UI/UITabSky.h` |
| Slack | 2 | 2 | Allowed anchor separation before the solver applies correction. | SkullbonezSource/Physics/Ragdoll.h | `SkullbonezSource/Physics/Ragdoll.cpp`<br>`SkullbonezSource/Physics/Ragdoll.h` |
| Solver object | 2 | 2 | Exact-count validation object used by deterministic physics scenes. | SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` |
| Solver snapshot | 2 | 2 | Physics state retained at a tick boundary for deterministic restore and diagnostic comparison. | SkullbonezSource/Physics/PhysicsSolverSnapshot.h | `SkullbonezSource/Physics/PhysicsSolverSnapshot.h`<br>`SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` |
| Sphere cap | 2 | 1 | Portion of a sphere below the fluid surface; its analytic volume gives a deterministic submerged fraction without sampling. | SkullbonezSource/Physics/BuoyancySystem.cpp | `SkullbonezSource/Physics/BuoyancySystem.cpp`<br>`SkullbonezSource/Physics/BuoyancySystem.h` |
| Style scene | 2 | 2 | Authored scene used as material/cinematic source data. | SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h | `SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h`<br>`SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` |
| Submission | 2 | 2 | Conversion of selected replay values into bounded draw commands. | SkullbonezSource/Runtime/Replay/ReplayPresentationSubmission.h | `SkullbonezSource/Runtime/Prediction/ReplayCauseFocusSubmission.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayPresentationSubmission.h` |
| Submitted-frame mark | 2 | 2 | One Tracy frame boundary emitted only after DX12 Present succeeds. | SkullbonezSource/Core/TracyClientOwner.h | `SkullbonezSource/Core/TracyClientOwner.h`<br>`SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Support edge budget | 2 | 1 | Fixed four-edges-per-body storage ceiling shared by contact and point-joint producers. | SkullbonezSource/Physics/SleepIslandSystem.cpp | `SkullbonezSource/Physics/SleepIslandSystem.cpp`<br>`SkullbonezSource/Physics/SleepIslandSystem.h` |
| Surface | 2 | 2 | Presentation boundary or ordered control surface exposed for one UI or operator domain. | GC0 owner adjudication | `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h`<br>`SkullbonezSource/UI/OperatorEditorExchange.h` |
| Swept overlay | 2 | 2 | One-step grid coverage of a body's start-to-end path that cannot pollute its persistent current-position membership. | SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp | `SkullbonezSource/Physics/SpatialGrid.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` |
| Terrain sweep | 2 | 2 | Continuous collision query against the terrain plane under a body. | SkullbonezSource/Physics/TerrainContactManifold.h | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`<br>`SkullbonezSource/Physics/TerrainContactManifold.h` |
| Topology drift | 2 | 2 | Temporary mismatch between editor model count and physics store rows after scene/editor construction or deletion. | SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp | `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp`<br>`SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Trajectory lane | 2 | 2 | Named path category such as past root, future root, child incoming/outgoing, retained trail, or baseline root. | SkullbonezSource/Runtime/Prediction/TrajectoryStore.h | `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`<br>`SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h` |
| Transport command | 2 | 2 | Presentation-independent record, scrub, prediction, or artifact intent translated by ReplayRuntime into existing replay owners. | SkullbonezSource/Runtime/Replay/ReplayCoordination.h | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayCoordination.h` |
| Tween | 2 | 1 | Time-based interpolation between camera poses for non-jarring cuts. | SkullbonezSource/Runtime/Camera/CameraCollection.cpp | `SkullbonezSource/Runtime/Camera/CameraCollection.cpp`<br>`SkullbonezSource/Runtime/Camera/CameraCollection.h` |
| UI (user interface) | 2 | 2 | Interactive engine controls evaluated between the input router's pre-UI and after-UI phases. | SkullbonezSource/Runtime/Input/InputRouter.h | `SkullbonezSource/Runtime/Input/InputRouter.cpp`<br>`SkullbonezSource/Runtime/Input/InputRouter.h` |
| UI options | 2 | 1 | Optional `ui` block parsed from a `.scene.json` file. | SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h | `SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp` |
| UI override | 2 | 2 | Live Scene/Run-tab value that survives an interactive reset and feeds the next generated-scene rebuild. | SkullbonezSource/UI/UISceneNavigationModel.h | `SkullbonezSource/Runtime/Scene/SceneResetPreservation.h`<br>`SkullbonezSource/UI/UISceneNavigationModel.h` |
| UI stress | 2 | 2 | Deterministic diagnostics input churn driven by scene data. | SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h | `SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h`<br>`SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp` |
| Underwater lock | 2 | 2 | Policy keeping a fully submerged sleeping ball dormant. | SkullbonezSource/Physics/Stages/PhysicsSleepController.h | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Uniform scale | 2 | 1 | One shared size value applied to all axes. | SkullbonezSource/Runtime/Editor/EditorTools.cpp | `SkullbonezSource/Runtime/Editor/EditorTools.cpp`<br>`SkullbonezSource/Runtime/Editor/EditorTools.h` |
| Upload category | 2 | 2 | Caller-owned reason for consuming frame upload bytes, used only for attribution and never for allocation priority. | SkullbonezSource/Rendering/RenderDiagnosticsTypes.h | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h`<br>`SkullbonezSource/UI/UIRenderDiagnostics.h` |
| UV (Texture Coordinates) | 2 | 2 | Two-dimensional texture/sample coordinates used by water shaders when perturbing reflection lookup. | SkullbonezSource/World/WorldEnvironment.h | `SkullbonezSource/Rendering/Text.h`<br>`SkullbonezSource/World/WorldEnvironment.h` |
| View model | 2 | 1 | Read-only presentation snapshot assembled from runtime owners. | SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp | `SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp`<br>`SkullbonezSource/Runtime/UI/RuntimeViewModel.h` |
| Visual-state hash | 2 | 2 | Digest of presentation-bearing typed values, excluding process-local allocation and budget telemetry. | SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h | `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp`<br>`SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` |
| Wake fan-out | 2 | 1 | Expansion through visual, point-joint, and resting-contact islands. | SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`<br>`SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` |
| Warmup frame | 2 | 2 | Completed frame intentionally excluded from profiler stats and perf CSV rows while a scene/pass settles. | SkullbonezSource/Core/Profiler.h | `SkullbonezSource/Core/Profiler.cpp`<br>`SkullbonezSource/Core/Profiler.h` |
| WndProc | 2 | 2 | Win32 window callback that receives mouse wheel and raw mouse packets before the frame boundary captures input. | SkullbonezSource/Runtime/Input/Input.cpp | `SkullbonezSource/Runtime/App/Window.cpp`<br>`SkullbonezSource/Runtime/Input/Input.cpp` |
| Workspace | 2 | 1 | Coarse runtime mode such as live, inspect, edit, or replay. | SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp`<br>`SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |

## Complete Local-Term Inventory

Each term below occurs in exactly one tracked source file and therefore stays
file-local under the owner split rule.

| Term | Defining file |
|---|---|
| ConfigSetting | `SkullbonezSource/Core/Config.cpp` |
| Configuration registry | `SkullbonezSource/Core/Config.cpp` |
| Mutual-gravity worker toggle | `SkullbonezSource/Core/Config.cpp` |
| ABBA cycle | `SkullbonezSource/Core/LockOrderValidator.cpp` |
| Absolute bar | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` |
| Accepted capture | `SkullbonezSource/Runtime/Capture/CaptureController.h` |
| Accepted event | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp` |
| Action id | `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` |
| Action identity | `SkullbonezSource/UI/OperatorEditorExchange.cpp` |
| Action status | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| Active bytes | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| Active capacity | `SkullbonezSource/Core/SceneCapacity.h` |
| Active slider | `SkullbonezSource/UI/UITabSky.h` |
| Active-byte cap | `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h` |
| Adjustment velocity | `SkullbonezSource/World/FluidSurfaceAdjustment.h` |
| Adoption | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp` |
| Affected-body trail | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h` |
| Afterimage | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` |
| Aggregate snapshot | `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h` |
| Alias spelling | `SkullbonezSource/Runtime/Startup/StartupCommandLine.h` |
| Aliasing period | `SkullbonezSource/Maths/MathsCommon.h` |
| Alignment axis | `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp` |
| All-body bank | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp` |
| All-body path | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| All-body paths | `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` |
| Allocation guard | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h` |
| Allocation guard mode | `SkullbonezSource/Runtime/App/RunLaunchOptions.h` |
| Allocation size | `SkullbonezSource/UI/UITabMemory.cpp` |
| Amortized work | `SkullbonezSource/Core/AmortizedTask.h` |
| Analytic slope | `SkullbonezSource/Physics/PhysicsTerrainView.h` |
| Angular drag multiplier | `SkullbonezSource/Physics/PhysicsWorldForces.h` |
| Architecture log | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` |
| Arg-min | `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp` |
| Artifact document | `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` |
| Assertion report | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Asset part state | `SkullbonezSource/Scene/SceneSnapshotWriter.h` |
| Asset provenance | `SkullbonezSource/Scene/AuthoredScene.h` |
| Asset-system borrow | `SkullbonezSource/Assets/AssetSystem.h` |
| Atomic cursor | `SkullbonezSource/Core/AmortizedTask.cpp` |
| Attach | `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` |
| Attach return pose | `SkullbonezSource/Runtime/App/InputFrame.cpp` |
| Attach target | `SkullbonezSource/Runtime/Camera/AttachedCameraController.h` |
| Attached camera target | `SkullbonezSource/Runtime/App/Run.h` |
| Attached target | `SkullbonezSource/Runtime/App/RunRender.cpp` |
| Authored projection | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` |
| Authoring boundary | `SkullbonezSource/Runtime/Direction/DemoDirector.cpp` |
| Auto-cycle screenshot | `SkullbonezSource/Runtime/Camera/CameraControlState.h` |
| Automation action | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Automation command | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Automation override | `SkullbonezSource/Runtime/Input/Input.h` |
| Automation report | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp` |
| Awaiting state | `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp` |
| Awake list position | `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` |
| B | `SkullbonezSource/Physics/PhysicsStageCapacity.h` |
| Backend epoch | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h` |
| Backend handle | `SkullbonezSource/Assets/TextureCollection.cpp` |
| Backend teardown | `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h` |
| Backend-neutral | `SkullbonezSource/Rendering/RenderMaterial.h` |
| Backend-owned resource | `SkullbonezSource/Runtime/Render/RuntimeRenderer.h` |
| Backing map | `SkullbonezSource/Core/TracyClientOwner.cpp` |
| Baked font | `SkullbonezSource/UI/UIFontMetrics.h` |
| Bar overlay | `SkullbonezSource/UI/UIProfilerOverlayPresenter.h` |
| Barrier | `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp` |
| Basis fallback | `SkullbonezSource/Runtime/Camera/Camera.cpp` |
| Batched frame payload | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h` |
| Baumgarte bias | `SkullbonezSource/Physics/ObjectContactManifold.h` |
| Behavior group | `SkullbonezSource/Runtime/Scene/SceneEntityStore.h` |
| Benign preference | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Binding view | `SkullbonezSource/Runtime/Input/InputController.Bindings.h` |
| BLAS | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` |
| Blend recipe | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Blend start pose | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| BMP (Bitmap) | `SkullbonezSource/Runtime/Capture/CaptureSystem.cpp` |
| BODY | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Body descriptor | `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Body-store row | `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` |
| Boundary count | `SkullbonezSource/Physics/PhysicsHandles.h` |
| Bounded dispatch | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` |
| Bounds radius | `SkullbonezSource/Rendering/RenderInstanceStore.h` |
| BRAN | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Branch | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` |
| Branch provenance chunk | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Branch restore | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp` |
| Broadphase filter | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` |
| Broadphase query | `SkullbonezSource/Physics/PhysicsApi.h` |
| Broadphase span | `SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h` |
| Browser scene | `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp` |
| Budget | `SkullbonezSource/Core/AmortizedTask.cpp` |
| Budget pass | `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` |
| Build-lane option | `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp` |
| Build-root prefix | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h` |
| Building part visitor | `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h` |
| Buoyancy row | `SkullbonezSource/Physics/BuoyancySystem.cpp` |
| Byte view | `SkullbonezSource/Core/ByteView.h` |
| Cached PSO blob | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h` |
| Callback payload | `SkullbonezSource/Gameplay/TornadoVisualPass.cpp` |
| Camera delta | `SkullbonezSource/Runtime/Input/InputController.h` |
| Camera key | `SkullbonezSource/Assets/AssetKeys.h` |
| Camera owner | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h` |
| Camera pose | `SkullbonezSource/Runtime/Direction/DemoDirector.h` |
| Camera-lighting sample | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` |
| Candidate generation | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h` |
| Candidate mutation | `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h` |
| Canonical hash | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp` |
| Canonical owner identity | `SkullbonezSource/Physics/PhysicsEngine.ReplayPredictionCloneScope.h` |
| Canonical packet | `SkullbonezSource/UI/OperatorEditorExchange.h` |
| Capacity | `SkullbonezSource/Runtime/Replay/ReplayIdentity.h` |
| Capacity accounting | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp` |
| Capacity cap | `SkullbonezSource/Physics/PhysicsFixedList.h` |
| Capacity reason | `SkullbonezSource/Physics/PhysicsFixedList.h` |
| Capacity span | `SkullbonezSource/UI/UITabMemory.cpp` |
| Capacity table | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` |
| Capture backend | `SkullbonezSource/UI/UIBackdropBlur.cpp` |
| Capture configuration | `SkullbonezSource/Core/TracyClientOwner.cpp` |
| Capture controller | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` |
| Capture intent | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h` |
| Capture timer | `SkullbonezSource/Gameplay/TornadoGameplay.h` |
| Caster value | `SkullbonezSource/Rendering/Shadow.h` |
| Catch-up cap | `SkullbonezSource/Physics/PhysicsTimestep.h` |
| Causal proof | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h` |
| Causal topology | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp` |
| Causality detail | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Causality publication | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Cause focus | `SkullbonezSource/Runtime/Prediction/ReplayCauseFocusSubmission.cpp` |
| Cause tree row | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` |
| Cbuffer (Constant Buffer) | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` |
| CCD refinement | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp` |
| Center of buoyancy | `SkullbonezSource/World/WorldEnvironment.cpp` |
| Central parameter | `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.cpp` |
| Checkpoint summary | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Chunk | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Cinematic baseline | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h` |
| Cinematic command | `SkullbonezSource/UI/UITabSky.cpp` |
| Cinematic config | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` |
| Cinematic defaults | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h` |
| Claimed launch | `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h` |
| Clean cursor | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` |
| Clip space | `SkullbonezSource/Maths/Matrix4.h` |
| Clip-space depth | `SkullbonezSource/Maths/Matrix4.cpp` |
| Clone scope | `SkullbonezSource/Physics/PhysicsEngine.ReplayPredictionCloneScope.h` |
| Closest approach | `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h` |
| Cold construction | `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h` |
| Cold detail | `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp` |
| Cold metadata | `SkullbonezSource/Scene/SceneSnapshotWriter.cpp` |
| Cold path | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h` |
| Cold refresh | `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h` |
| Cold setup | `SkullbonezSource/UI/UIFontMetrics.cpp` |
| Cold start | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp` |
| Collider shape reference | `SkullbonezSource/Physics/ObjectContactManifold.cpp` |
| ColliderStore | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Collision visual | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` |
| Collision-cell key | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Command | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` |
| Command batch | `SkullbonezSource/Runtime/Replay/ReplayEventCommand.h` |
| Command owner | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Command side | `SkullbonezSource/Runtime/Editor/EditorHistory.cpp` |
| Command struct | `SkullbonezSource/UI/UICommands.h` |
| Command-line view | `SkullbonezSource/Runtime/Startup/StartupCommandLine.h` |
| Commit | `SkullbonezSource/Runtime/Scene/SceneEntityStore.h` |
| Compact toolbar | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp` |
| Completed sample | `SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp` |
| Component-wise | `SkullbonezSource/Maths/Vector3.h` |
| Composite main lane | `SkullbonezSource/Core/TracyClientOwner.h` |
| Composition boundary | `SkullbonezSource/Runtime/App/InputFrame.h` |
| Config rewrite | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp` |
| Conflict | `SkullbonezSource/UI/OperatorEditorExchange.h` |
| Connection snapshot | `SkullbonezSource/Core/TracyClientOwner.cpp` |
| Consequence batch | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Conservative epsilon | `SkullbonezSource/Maths/Frustum.h` |
| Constraint | `SkullbonezSource/Physics/PhysicsApi.h` |
| Consumer receipt | `SkullbonezSource/Runtime/Scene/SceneLifecycle.h` |
| Contact | `SkullbonezSource/Physics/PhysicsApi.h` |
| Contact cache | `SkullbonezSource/Physics/PhysicsSolverSnapshot.h` |
| Contact flash alpha | `SkullbonezSource/Rendering/RenderMaterial.h` |
| Contact linger | `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp` |
| Contact patch | `SkullbonezSource/Physics/TerrainContactManifold.cpp` |
| Contact presentation output | `SkullbonezSource/Runtime/App/ReplayValidation.cpp` |
| Contact skin | `SkullbonezSource/Physics/SolverBroadphaseStage.h` |
| Content envelope | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Context exit release | `SkullbonezSource/Runtime/Input/InputRouter.cpp` |
| Context mask | `SkullbonezSource/Runtime/Input/InputController.Bindings.h` |
| Context predicate | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| Context row | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h` |
| Continuation | `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h` |
| Control directory | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` |
| Control folder | `SkullbonezSource/Runtime/Direction/LiveStyleController.h` |
| Control id | `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` |
| Correction | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h` |
| Count-only trace | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` |
| Credible support | `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` |
| Cross product | `SkullbonezSource/Maths/Vector3.h` |
| Current access | `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` |
| Data root | `SkullbonezSource/Core/WindowConstants.h` |
| Debug line command | `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp` |
| Debug line row | `SkullbonezSource/Gameplay/TornadoGameplay.cpp` |
| Debug visualizers | `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h` |
| Debug-only binding | `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp` |
| Debug/broadphase bytes | `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` |
| Decorated marker | `SkullbonezSource/Core/PlatformProfiler.h` |
| Default horizon | `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h` |
| Degenerate plane | `SkullbonezSource/Maths/Frustum.cpp` |
| Delivered action | `SkullbonezSource/Runtime/Input/InputRouter.cpp` |
| Delta-v | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` |
| Demo | `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` |
| Dense topology | `SkullbonezSource/Runtime/Scene/SceneWorld.h` |
| Dense-row hint | `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h` |
| Density | `SkullbonezSource/Physics/PhysicsMass.h` |
| Departure delay | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` |
| Dependent PSO | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp` |
| Descriptor refresh | `SkullbonezSource/Physics/PhysicsEngine.h` |
| Detached scene camera | `SkullbonezSource/Runtime/Camera/CameraControlState.h` |
| Detached status | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h` |
| Detail panel | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Detection candidate | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` |
| Determinism | `SkullbonezSource/Physics/PhysicsEngine.cpp` |
| Determinism envelope | `SkullbonezSource/Core/FloatingPointContract.h` |
| Deterministic merge | `SkullbonezSource/Core/WorkerPool.h` |
| Deterministic order | `SkullbonezSource/Physics/PhysicsApi.h` |
| Deterministic slot | `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp` |
| Deterministic topology | `SkullbonezSource/Physics/ConvexHullShape.cpp` |
| Development tool permission | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h` |
| Development UI apply result | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Development UI mode | `SkullbonezSource/Runtime/App/RunLaunchOptions.h` |
| Development-tools capability | `SkullbonezSource/Core/Allocation/DevelopmentToolsCapability.h` |
| Development-UI heap | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h` |
| Device epoch | `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h` |
| Device snapshot | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| DFS (Depth-First Search) | `SkullbonezSource/Core/LockOrderValidator.cpp` |
| Diagnostic snapshot | `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h` |
| Diagnostic token | `SkullbonezSource/Core/SbResult.h` |
| Diagnostics controller | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` |
| Diagonal split | `SkullbonezSource/Physics/PhysicsTerrainView.cpp` |
| Directive row | `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp` |
| Director | `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` |
| Director advance | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| Director shot action | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp` |
| Disjoint set | `SkullbonezSource/Physics/DisjointSet.h` |
| Dispatch pass | `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp` |
| Dock shell | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Domain clamp | `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h` |
| Domain config | `SkullbonezSource/Core/Config.h` |
| Domain view | `SkullbonezSource/UI/OperatorEditorExchange.h` |
| Dot product | `SkullbonezSource/Maths/Vector3.h` |
| DPI style epoch | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Drag changed | `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h` |
| Drag coefficient | `SkullbonezSource/World/WorldEnvironment.h` |
| Drag group | `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp` |
| Draw call | `SkullbonezSource/Rendering/DrawCallTrace.h` |
| Draw list | `SkullbonezSource/Runtime/Render/UiDrawSubmission.h` |
| Draw quota | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` |
| Draw trace | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h` |
| Draw-call trace | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| Due predictor | `SkullbonezSource/Runtime/Capture/CaptureSystem.h` |
| Duplicate | `SkullbonezSource/UI/OperatorEditorExchange.h` |
| Dust band | `SkullbonezSource/Gameplay/TornadoVisualPass.cpp` |
| DX11/OpenGL | `SkullbonezSource/Runtime/App/Run.h` |
| DX12 (DirectX 12) | `SkullbonezSource/Maths/Matrix4.h` |
| DXGI (DirectX Graphics Infrastructure) adapter memory | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| DXIL container reflection | `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp` |
| DXR | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` |
| Dynamic vertex buffer | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Eccentric anomaly | `SkullbonezSource/Maths/OrbitalMechanics.h` |
| ECUR | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Edge | `SkullbonezSource/Physics/ConvexHullShape.h` |
| Editable replacement | `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h` |
| Editor arbitration | `SkullbonezSource/Runtime/App/InputFrame.h` |
| Editor commands | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Editor frame input | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Eject cooldown | `SkullbonezSource/Gameplay/TornadoGameplay.h` |
| Elastic collision | `SkullbonezSource/Physics/PhysicsWorldForces.h` |
| Embedded vector fallback | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Engine lifecycle smoke | `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp` |
| Engine log | `SkullbonezSource/Core/Log.h` |
| engine.cfg | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp` |
| Euler decomposition | `SkullbonezSource/Maths/Quaternion.h` |
| Event | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` |
| Event capacity | `SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h` |
| Event sample | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Evidence row | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h` |
| EVNT | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Exact hash | `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` |
| Exhaustion rule | `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` |
| Exit latch | `SkullbonezSource/Runtime/App/ApplicationExitState.cpp` |
| Exit summary | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` |
| Exposure | `SkullbonezSource/Physics/Stages/ExternalForceStage.h` |
| Extension registration | `SkullbonezSource/Gameplay/TornadoVisualPass.h` |
| Extension scope | `SkullbonezSource/Rendering/WorldRenderExtension.h` |
| External command state | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h` |
| External field | `SkullbonezSource/Physics/Stages/ExternalForceStage.h` |
| External owner zone | `SkullbonezSource/Core/Profiler.h` |
| Face | `SkullbonezSource/Physics/ConvexHullShape.h` |
| Face mesh | `SkullbonezSource/World/SkyBox.h` |
| Face plane | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp` |
| Failed cell | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` |
| Failure precedence | `SkullbonezSource/Runtime/App/ApplicationExitState.cpp` |
| Fast-sweep augmentation | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Fatal invariant | `SkullbonezSource/Core/FatalError.h` |
| FIFO (First In, First Out) | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| First difference | `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Fixed timestep | `SkullbonezSource/Physics/PhysicsTimestep.h` |
| Fixed-capacity list | `SkullbonezSource/Physics/PhysicsFixedList.h` |
| Fixed-step edge | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Flat-slope classification | `SkullbonezSource/Runtime/Scene/SceneTerrain.h` |
| Fluid density | `SkullbonezSource/Physics/PhysicsWorldForces.h` |
| Fluid force settings | `SkullbonezSource/World/WorldEnvironment.h` |
| Fluid-surface command | `SkullbonezSource/Runtime/Input/InputRouter.cpp` |
| Focus cancellation | `SkullbonezSource/Runtime/Input/InputRouter.cpp` |
| Focus override | `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.cpp` |
| Focus resynchronization | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| Focus row | `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp` |
| Follow-up action | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp` |
| Following request | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h` |
| Font upload | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp` |
| Footprint | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp` |
| Foreign pointer | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| FOV (Field of View) | `SkullbonezSource/Rendering/Text.h` |
| FP contraction | `SkullbonezSource/Core/FloatingPointContract.h` |
| Frame checkpoint | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h` |
| Frame gate | `SkullbonezSource/Runtime/Capture/CaptureController.h` |
| Frame index | `SkullbonezSource/Runtime/Replay/ReplayIdentity.h` |
| Frame phase result | `SkullbonezSource/Runtime/App/Run.h` |
| Frame policy | `SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h` |
| Frame view | `SkullbonezSource/UI/UIProfilerOverlayPresenter.h` |
| Frame-active visual | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` |
| Frame-local prediction draw | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` |
| Frame-local ribbon | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` |
| Framebuffer | `SkullbonezSource/Rendering/RenderResourceTypes.h` |
| Friction | `SkullbonezSource/Physics/PersistentContactSolver.cpp` |
| Frustum plane | `SkullbonezSource/Maths/Frustum.h` |
| Full-record consumer | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` |
| Game viewport | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h` |
| Game viewport copy | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h` |
| Game viewport rect | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Gas density | `SkullbonezSource/Physics/PhysicsWorldForces.h` |
| Gate configuration | `SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h` |
| Generated control transaction | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| Generated object type override | `SkullbonezSource/Runtime/App/RunLaunchOptions.h` |
| Generated override | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp` |
| Generated UI command | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| Generated-demo camera | `SkullbonezSource/Runtime/Camera/CameraControlState.cpp` |
| Gesture body | `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp` |
| Gesture command | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` |
| Ghost arc | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h` |
| Gizmo drag group | `SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| GPU resource | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` |
| GPU timer | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| GPU VA (GPU Virtual Address) | `SkullbonezSource/Rendering/RenderRaytracingTypes.h` |
| Grab | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| Grab offset | `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp` |
| Graph transient | `SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h` |
| Gravitational parameter | `SkullbonezSource/Maths/OrbitalMechanics.h` |
| Grid bucket | `SkullbonezSource/Physics/PhysicsStageCapacity.h` |
| Grid maintenance | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Growth owner | `SkullbonezSource/Runtime/App/ReplayReserveInventory.h` |
| Growth request | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp` |
| Guide ring | `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h` |
| Half-extents | `SkullbonezSource/Physics/BoundingBox.h` |
| Half-space | `SkullbonezSource/Maths/Frustum.h` |
| Half-space clipping | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h` |
| Handle | `SkullbonezSource/Physics/PhysicsHandles.h` |
| Hard cap | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp` |
| HASH | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Hash log | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` |
| Hash-log lock | `SkullbonezSource/Runtime/Replay/ReplayTimeline.h` |
| Heavy capture | `SkullbonezSource/Core/TracyClientOwner.h` |
| Heliocentric state | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp` |
| High water | `SkullbonezSource/UI/UIRenderDiagnostics.h` |
| High-resolution counter | `SkullbonezSource/Core/Timer.h` |
| High-water | `SkullbonezSource/Core/SbDiagnosticStore.h` |
| High-water capacity | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` |
| Histogram option | `SkullbonezSource/UI/UITabProfilerHistogram.cpp` |
| History cursor | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h` |
| Hot axis | `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp` |
| Hot SoA (Structure of Arrays) fields | `SkullbonezSource/Physics/PhysicsBodyStore.h` |
| Hot zone | `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` |
| HRAWINPUT | `SkullbonezSource/Runtime/Input/Input.h` |
| Hull asset | `SkullbonezSource/Runtime/Editor/EditorHullAssets.h` |
| Hull token | `SkullbonezSource/Runtime/Editor/EditorHullAssets.h` |
| Hull variant | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` |
| Identity quaternion | `SkullbonezSource/Maths/Quaternion.h` |
| Immediate submitter | `SkullbonezSource/Runtime/Render/UiDrawSubmission.h` |
| Immutable digest reuse | `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` |
| Immutable projection | `SkullbonezSource/Physics/PhysicsEngine.h` |
| In-flight chunk | `SkullbonezSource/Core/AmortizedTask.h` |
| INDX | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Inertia tensor | `SkullbonezSource/Physics/PhysicsMass.h` |
| Input bridge | `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.cpp` |
| Input event buffer | `SkullbonezSource/Runtime/Input/Input.h` |
| Input override | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Input window bridge | `SkullbonezSource/Runtime/Input/Input.cpp` |
| Inspect | `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` |
| Inspection camera | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp` |
| Instance buffer | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.h` |
| Instance payload | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` |
| Instanced mesh | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Intent | `SkullbonezSource/Runtime/App/ReplayRuntimePackets.h` |
| Interactive scene run | `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h` |
| Intrusive back-link | `SkullbonezSource/Physics/SpatialGrid.cpp` |
| Inverse inertia | `SkullbonezSource/Physics/PhysicsMass.h` |
| Inverse mass | `SkullbonezSource/Physics/PhysicsBodyStore.h` |
| K | `SkullbonezSource/Physics/PhysicsStageCapacity.h` |
| Lambert solution | `SkullbonezSource/Maths/OrbitalMechanics.h` |
| Lambert solve | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp` |
| Lane F fatal | `SkullbonezSource/Scene/AuthoredScene.cpp` |
| Lane P | `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp` |
| Large-scene fallback | `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` |
| Last phase/frame | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` |
| Late UI pass | `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp` |
| Latest-wins restart | `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` |
| Launch normalization | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` |
| Launch override | `SkullbonezSource/Runtime/App/RunLaunchOptions.h` |
| Launch policy | `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp` |
| Launch resolution | `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h` |
| Launch token | `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp` |
| Launcher ray | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` |
| Launcher tuning command | `SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Launcher visual sample | `SkullbonezSource/Runtime/Replay/ReplayToolPackets.h` |
| Layering boundary | `SkullbonezSource/Maths/GeometricMath.h` |
| Layout version | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Leaf | `SkullbonezSource/Rendering/DrawCallTrace.cpp` |
| Lean SDK surface | `SkullbonezSource/Core/PlatformWin32.h` |
| Lease | `SkullbonezSource/Core/SbResult.h` |
| Lease churn | `SkullbonezSource/Core/SbResult.cpp` |
| Legacy hash | `SkullbonezSource/Assets/TextureCollection.cpp` |
| Legacy tint bridge | `SkullbonezSource/Rendering/RenderMaterial.h` |
| Lifecycle activation | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| Lifecycle packet | `SkullbonezSource/Runtime/Scene/SceneSessionState.h` |
| Lifecycle phase | `SkullbonezSource/Runtime/Scene/SceneLifecycle.h` |
| Linger cache | `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` |
| Live advance | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp` |
| Live backup | `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h` |
| Live count | `SkullbonezSource/Physics/PhysicsFixedList.h` |
| Live entry | `SkullbonezSource/Core/SbResult.cpp` |
| Live input | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp` |
| Live restore | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp` |
| Live shader registry | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h` |
| Live visual clock | `SkullbonezSource/Gameplay/TornadoVisualPass.h` |
| Load decision | `SkullbonezSource/Runtime/Scene/SceneController.Navigation.cpp` |
| Load navigation snapshot | `SkullbonezSource/Runtime/Scene/SceneControllerState.h` |
| Load phase | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h` |
| Load-only save | `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h` |
| Loaded presentation | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` |
| Lock graph | `SkullbonezSource/Core/LockOrderValidator.h` |
| Locked orbit | `SkullbonezSource/Runtime/Camera/Camera.h` |
| Logical asset name | `SkullbonezSource/Assets/AssetSystem.cpp` |
| Logical binding | `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h` |
| Look-at target | `SkullbonezSource/Runtime/Camera/Camera.cpp` |
| LZ4 owner hooks | `SkullbonezSource/Core/TracyClientOwner.cpp` |
| Main memory | `SkullbonezSource/UI/UITabMemory.h` |
| MANI | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Manifest identity | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h` |
| Manifold commit | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp` |
| Manipulator | `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` |
| Manual camera | `SkullbonezSource/Runtime/Camera/CameraControlState.cpp` |
| Manual lifetime | `SkullbonezSource/Core/TracyClientOwner.h` |
| Manual profiler lifetime | `SkullbonezSource/Runtime/App/Init.cpp` |
| Mapped pointer | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` |
| Marker domain | `SkullbonezSource/Core/PlatformProfiler.h` |
| Marker identity | `SkullbonezSource/Core/StringHash.h` |
| Marker tree | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` |
| Material instance payload | `SkullbonezSource/Rendering/RenderMaterial.h` |
| Material kind | `SkullbonezSource/Rendering/RenderMaterial.h` |
| Material override | `SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` |
| Materialization | `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` |
| Maths prelude | `SkullbonezSource/Maths/MathsCommon.h` |
| Maximum horizon | `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h` |
| Memory log interval | `SkullbonezSource/Runtime/Capture/GraphicsStressController.h` |
| Message exit code | `SkullbonezSource/Runtime/App/ApplicationExitState.h` |
| Metadata bytes | `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` |
| Model-order reduction | `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` |
| Mouse look | `SkullbonezSource/Runtime/Input/InputController.cpp` |
| Mouse pickup | `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp` |
| Mouse-look memory | `SkullbonezSource/Runtime/Camera/CameraControlState.h` |
| Mutual gravity | `SkullbonezSource/Physics/PhysicsWorldForces.h` |
| Name registration | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` |
| Narrowphase island | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` |
| Navigation policy | `SkullbonezSource/Runtime/Scene/SceneControllerState.h` |
| NDJSON (Newline-Delimited JSON) | `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h` |
| Neck swing limit | `SkullbonezSource/Physics/Ragdoll.cpp` |
| Normalized bar | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` |
| Normalized path | `SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp` |
| Normalized vector | `SkullbonezSource/Maths/Vector3.h` |
| Numbered artifact | `SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp` |
| Object representation | `SkullbonezSource/Core/ByteView.h` |
| Object type override | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` |
| Offline DXC bake | `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp` |
| Offline projection | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h` |
| Offline reconstruction | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h` |
| On-demand client | `SkullbonezSource/Core/TracyClientOwner.cpp` |
| Operator camera mode | `SkullbonezSource/Runtime/Camera/CameraControlState.h` |
| Orbit wheel | `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp` |
| Orbital elements | `SkullbonezSource/Maths/OrbitalMechanics.h` |
| Ordinary defaults | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h` |
| Ordinary render config | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` |
| Output row | `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h` |
| Overflow | `SkullbonezSource/Rendering/DrawCallTrace.cpp` |
| Overflow shift | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` |
| Overlay command | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h` |
| Overlay mode | `SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h` |
| Overlay trace | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp` |
| Owned failure | `SkullbonezSource/Runtime/App/ApplicationExitState.h` |
| Owner boundary | `SkullbonezSource/Physics/PhysicsEngine.h` |
| Owner budget | `SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp` |
| Owner scope | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h` |
| P | `SkullbonezSource/Physics/PhysicsStageCapacity.h` |
| Pair event | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` |
| Pair repulsion | `SkullbonezSource/Gameplay/TornadoField.cpp` |
| Pair table | `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` |
| Pair-build worker | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp` |
| Pair-order commit | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` |
| Pair-order slot | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` |
| Paired capture | `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp` |
| Parallel mutual gravity | `SkullbonezSource/Core/Config.h` |
| Parsed arguments | `SkullbonezSource/Runtime/Startup/StartupCommandLine.h` |
| Parser failure scope | `SkullbonezSource/Scene/AuthoredSceneParserSchema.h` |
| Part display name | `SkullbonezSource/Physics/Ragdoll.h` |
| Pass | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` |
| Pass order | `SkullbonezSource/Runtime/Render/RuntimeRenderer.h` |
| Pass resource | `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h` |
| Pass resources | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` |
| Past trajectory view | `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` |
| Path color mode | `SkullbonezSource/Runtime/Replay/ReplayPresentation.h` |
| Path compression | `SkullbonezSource/Physics/DisjointSet.h` |
| Path target | `SkullbonezSource/Runtime/Replay/ReplayPresentation.h` |
| Path visualizer | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` |
| Peak utilisation | `SkullbonezSource/UI/UITabMemory.cpp` |
| Pending capture | `SkullbonezSource/Runtime/Direction/LiveStyleController.h` |
| Pending impulse | `SkullbonezSource/Physics/PhysicsEngine.cpp` |
| Percentage-closer filtering (PCF) | `SkullbonezSource/Rendering/Shadow.h` |
| Perf pass | `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp` |
| Perifocal frame | `SkullbonezSource/Maths/OrbitalMechanics.cpp` |
| Permanent-development exception | `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp` |
| Phase | `SkullbonezSource/Runtime/Direction/DemoDirector.h` |
| Phase pose | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| Phase scope | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h` |
| Phase style | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| Phase transaction | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Physics advance | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |
| Physics debug flag | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp` |
| Physics debug flags | `SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h` |
| Physics terrain cell | `SkullbonezSource/World/Terrain.cpp` |
| Physics-world bytes | `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` |
| PhysicsBodyStore | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| PhysicsEngine | `SkullbonezSource/Physics/PhysicsWorld.cpp` |
| Pick ray | `SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp` |
| Pick transform | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h` |
| Pipeline capacity | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` |
| Pipeline record | `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` |
| Pipeline sync | `SkullbonezSource/Runtime/Render/RenderPresentationSettings.h` |
| Pipeline trace | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` |
| Pitch cap | `SkullbonezSource/Runtime/Camera/Camera.cpp` |
| Placement ghost | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Placement mode | `SkullbonezSource/UI/UITabEditor.cpp` |
| Placement preflight | `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` |
| Placement request | `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp` |
| Planning surface | `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h` |
| Planning target | `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h` |
| Platform message | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h` |
| Platform profiler marker | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| Playback state | `SkullbonezSource/Runtime/Direction/DemoDirector.h` |
| Pointer control | `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` |
| Pointer override | `SkullbonezSource/UI/UIInput.h` |
| Policy row | `SkullbonezSource/Runtime/App/ReplayReserveInventory.h` |
| Policy violation | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` |
| Population mode | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` |
| Population result | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` |
| Porkchop cell | `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp` |
| Porkchop grid | `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h` |
| Porkchop plot | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` |
| Pose command | `SkullbonezSource/Runtime/Camera/AttachedCameraController.h` |
| Pose history | `SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Post | `SkullbonezSource/World/Terrain.cpp` |
| Post-UI snapshot | `SkullbonezSource/Runtime/App/InputFrameExecution.cpp` |
| Pre-UI facts | `SkullbonezSource/Runtime/App/InputFrameExecution.cpp` |
| Prediction archive | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h` |
| Prediction cache | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Prediction frame | `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` |
| Prediction physics tick | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` |
| Prediction prefix | `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` |
| Prediction slice | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` |
| Prediction target | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp` |
| Prediction worker | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` |
| Prefab descriptor | `SkullbonezSource/Physics/Ragdoll.cpp` |
| Preference migration | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Preference text | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp` |
| Prefix | `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h` |
| Preflight | `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp` |
| Prelude | `SkullbonezSource/Core/Common.h` |
| Prepared commit | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` |
| PRES | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Present marker | `SkullbonezSource/Runtime/Replay/ReplayTimelinePackets.h` |
| Presentation | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h` |
| Presentation cache | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp` |
| Presentation capture | `SkullbonezSource/Runtime/Scene/SceneWorld.h` |
| Presentation edit | `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h` |
| Presentation layer | `SkullbonezSource/Runtime/UI/RuntimeViewModel.h` |
| Presentation packet | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h` |
| Presentation pin | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Presentation pose | `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp` |
| Presentation record | `SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Presentation selection | `SkullbonezSource/Runtime/Replay/ReplayPresentationPackets.h` |
| Presentation storage | `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp` |
| Presentation view | `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` |
| Preview | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp` |
| Preview identity | `SkullbonezSource/Runtime/Render/UiDrawSubmission.h` |
| Preview lines | `SkullbonezSource/Physics/Ragdoll.h` |
| Preview snapshot | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h` |
| Preview state | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h` |
| Preview/commit edit | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` |
| Primary camera | `SkullbonezSource/Runtime/Camera/CameraCollection.h` |
| Primary region | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Primitive recipe | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h` |
| Prior level | `SkullbonezSource/UI/UIInput.cpp` |
| Process-end capacity table | `SkullbonezSource/Runtime/App/Run.cpp` |
| Profile build | `SkullbonezSource/Core/FatalError.cpp` |
| Profiler frame snapshot | `SkullbonezSource/UI/UITabProfiler.h` |
| Profiler thread label | `SkullbonezSource/Core/WorkerPool.cpp` |
| Prograde | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp` |
| Projection adapter | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` |
| Projection coordinate | `SkullbonezSource/UI/UIProfilerOverlayPresenter.h` |
| Projection frustum | `SkullbonezSource/Runtime/App/Window.h` |
| Promotion | `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp` |
| Property preview | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| PSO cache counters | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| Publication token | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h` |
| Published build prefix | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Published generation | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp` |
| Published view | `SkullbonezSource/Runtime/Replay/ReplayCoordination.h` |
| QPC (QueryPerformanceCounter) | `SkullbonezSource/Core/Profiler.h` |
| Quad | `SkullbonezSource/World/Terrain.cpp` |
| Quad posting | `SkullbonezSource/Physics/PhysicsTerrainView.cpp` |
| Quarantine | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h` |
| Quaternion | `SkullbonezSource/Maths/Quaternion.h` |
| Query heap | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` |
| Quiet window | `SkullbonezSource/Runtime/Capture/RuntimeStressController.h` |
| Quiet-frame counter | `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` |
| Ragdoll eyes | `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp` |
| Range | `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h` |
| Rank | `SkullbonezSource/Physics/DisjointSet.h` |
| Raster binding ABI | `SkullbonezSource/Rendering/RenderRasterBindingContract.h` |
| Raster bucket | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| RAW (Raw Heightmap) | `SkullbonezSource/World/Terrain.h` |
| Ray cast | `SkullbonezSource/Physics/PhysicsApi.h` |
| Ray interval | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp` |
| Rebuild action | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` |
| Receive predicate | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp` |
| Recipe identity | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h` |
| Reconciled memory | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` |
| Reconciled total | `SkullbonezSource/Core/MainMemoryStats.h` |
| Record | `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h` |
| Record cursor | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h` |
| Record range | `SkullbonezSource/Rendering/RenderGpuTimingOwner.h` |
| Recorder eviction | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Recorder reserve owner | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Recording gate | `SkullbonezSource/Runtime/Replay/ReplayTimeline.h` |
| Recreate recipe | `SkullbonezSource/Runtime/Editor/EditorHistory.cpp` |
| Recreation transaction | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` |
| Redo suffix | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` |
| Reduction | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp` |
| Reentrancy guard | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| Reflection contract | `SkullbonezSource/Rendering/ShaderReflectionContracts.h` |
| Refresh deadline | `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h` |
| Registration | `SkullbonezSource/Rendering/WorldRenderExtension.h` |
| Relative camera | `SkullbonezSource/Runtime/Camera/CameraCollection.h` |
| Release frame | `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h` |
| Relevant link | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h` |
| Remaining-time integration | `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` |
| Render defaults | `SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp` |
| Render frame view | `SkullbonezSource/Runtime/App/RunRender.cpp` |
| Render GPU timing owner | `SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp` |
| Render instance store | `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` |
| Render memory snapshot | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| Render pose override | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Render record | `SkullbonezSource/Core/Profiler.cpp` |
| Render relief | `SkullbonezSource/World/Terrain.cpp` |
| Render resource context | `SkullbonezSource/Assets/TextureCollection.cpp` |
| Render setup owners | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` |
| Renderer binding | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h` |
| RenderSceneSnapshot | `SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Repeat cooldown | `SkullbonezSource/Physics/Stages/ExternalForceStage.h` |
| Replay allocation scope | `SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp` |
| Replay family | `SkullbonezSource/Runtime/App/ReplayRuntimePackets.h` |
| Replay future marker | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Replay growth | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h` |
| Replay memory policy | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Replay overlay | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| Replay owner | `SkullbonezSource/Physics/PhysicsSceneVectorReserve.h` |
| Replay policy | `SkullbonezSource/UI/UITabMemory.h` |
| Replay prediction seed | `SkullbonezSource/Physics/PhysicsEngine.h` |
| Replay prediction working set | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h` |
| Replay reserve owner | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` |
| Replay velocity body view | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp` |
| Replay visual packet | `SkullbonezSource/Runtime/App/RunRender.cpp` |
| Report fact | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp` |
| Repro snapshot | `SkullbonezSource/Runtime/Editor/LauncherTools.cpp` |
| Request arbitration | `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp` |
| Request ring | `SkullbonezSource/Runtime/Capture/CaptureController.cpp` |
| Required broadphase cells | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h` |
| Required gate | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` |
| Required scene contact | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` |
| Reserve growth event | `SkullbonezSource/UI/UITabMemory.h` |
| Reserve owner | `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h` |
| Resize frame owner | `SkullbonezSource/Runtime/App/Window.h` |
| Resize lifecycle | `SkullbonezSource/Runtime/App/Window.cpp` |
| Resource | `SkullbonezSource/Rendering/ShaderContracts.h` |
| Resource barrier | `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h` |
| Rest-applied row | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` |
| Resting footprint | `SkullbonezSource/Physics/PersistentContactSolver.cpp` |
| Resting policy | `SkullbonezSource/Physics/TerrainContactManifold.h` |
| Restore flag | `SkullbonezSource/Runtime/App/ReplayValidation.Internal.h` |
| Restore operands | `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h` |
| Restore probe | `SkullbonezSource/Runtime/Replay/ReplayProbeState.h` |
| Resume | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h` |
| Retained backing | `SkullbonezSource/Physics/PhysicsSceneVectorReserve.h` |
| Retained geometry chunk | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Retained marker | `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Retained path | `SkullbonezSource/Runtime/Replay/ReplayPathPackets.h` |
| Retained prediction list | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| Retained range chunk | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` |
| Retained trail | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h` |
| Retention | `SkullbonezSource/Runtime/Replay/ReplayTimeline.h` |
| Reveal pacing | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h` |
| Reveal rate | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` |
| Reveal threshold | `SkullbonezSource/Runtime/Direction/DemoDirector.h` |
| RGBA (Red, Green, Blue, Alpha) | `SkullbonezSource/Rendering/Text.h` |
| Ring cursor | `SkullbonezSource/Runtime/Replay/ReplayToolPackets.h` |
| RNG (Random Number Generator) | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp` |
| Rolling timing value | `SkullbonezSource/Runtime/App/RunTimerState.h` |
| Root | `SkullbonezSource/Physics/DisjointSet.h` |
| Root parameter | `SkullbonezSource/Rendering/RenderRasterBindingContract.h` |
| Round trip | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp` |
| Route phase | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| Row view | `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp` |
| Run camera command | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` |
| Runtime command | `SkullbonezSource/Runtime/Input/InputController.cpp` |
| Runtime input action | `SkullbonezSource/Runtime/Input/InputController.Bindings.h` |
| Runtime input event | `SkullbonezSource/Runtime/Input/InputController.h` |
| Runtime mirror | `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp` |
| Runtime mode badge | `SkullbonezSource/Runtime/Render/UiTextPass.cpp` |
| Runtime reactions | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h` |
| Runtime renderer option | `SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h` |
| Runtime reserve owner | `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h` |
| Runtime settings snapshot | `SkullbonezSource/Physics/PhysicsRuntimeSettings.h` |
| RuntimeRenderer | `SkullbonezSource/Runtime/Render/RuntimeRenderer.h` |
| RVIS (Replay Visual Instance State) | `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp` |
| RVPD (Replay Visual Prediction Data) | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp` |
| Safeguarded Newton | `SkullbonezSource/Maths/OrbitalMechanics.cpp` |
| Same-state oracle | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` |
| Sample capacity | `SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h` |
| Sample ring | `SkullbonezSource/UI/UITabProfilerHistogram.cpp` |
| Sampler register | `SkullbonezSource/Rendering/RenderRasterBindingContract.h` |
| Sanitized filename | `SkullbonezSource/Runtime/Scene/SceneController.Creation.cpp` |
| Save probe | `SkullbonezSource/Runtime/Replay/ReplayProbeState.h` |
| Scalar codec | `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp` |
| Scalar state | `SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp` |
| Scan key | `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp` |
| Scan window | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h` |
| Scene browser path | `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h` |
| Scene collection | `SkullbonezSource/Scene/AuthoredScene.cpp` |
| Scene energy sample | `SkullbonezSource/Runtime/App/RunTimerState.h` |
| Scene gate | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h` |
| Scene gate tracker | `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` |
| Scene interval | `SkullbonezSource/Runtime/Capture/GraphicsStressController.h` |
| Scene navigation model | `SkullbonezSource/UI/UI.h` |
| Scene pause badge | `SkullbonezSource/Runtime/Render/UiTextPass.cpp` |
| Scene queue action | `SkullbonezSource/Runtime/Scene/SceneController.Creation.cpp` |
| Scene request batch | `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp` |
| Scene snapshot | `SkullbonezSource/Scene/SceneSnapshotWriter.cpp` |
| Scene UI request | `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h` |
| Scene world | `SkullbonezSource/Runtime/Scene/SceneController.h` |
| Scene-load plan | `SkullbonezSource/Runtime/Capture/RuntimeStressController.h` |
| Scene-run state | `SkullbonezSource/Runtime/Scene/SceneSessionState.cpp` |
| Schema version | `SkullbonezSource/Scene/AuthoredScene.cpp` |
| SCHK | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| Scratch flags | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Scrub probe | `SkullbonezSource/Runtime/Replay/ReplayProbeState.h` |
| Section | `SkullbonezSource/UI/UIRenderAuthoringCatalog.h` |
| Seed | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h` |
| Seed sleep | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp` |
| Selected development surface | `SkullbonezSource/Runtime/App/InputFrameExecution.cpp` |
| Selection body | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` |
| Selection outline | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Selection scope | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` |
| Semantic hash | `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` |
| Sequencer gap | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp` |
| Shader base name | `SkullbonezSource/Assets/AssetSystem.cpp` |
| Shader-visible descriptor heap | `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` |
| Shader-visible heap | `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp` |
| Shadow frame data | `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h` |
| Shape kind | `SkullbonezSource/Rendering/RenderInstanceStore.h` |
| Shape reference | `SkullbonezSource/Physics/CollisionShape.h` |
| Shared editor exchange | `SkullbonezSource/UI/UICommands.h` |
| Shortest nlerp | `SkullbonezSource/Maths/Quaternion.h` |
| Shot list | `SkullbonezSource/Runtime/Direction/DemoDirector.h` |
| Shot-list phase | `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h` |
| Side-channel log | `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp` |
| Signal | `SkullbonezSource/Core/Fence.h` |
| Signed distance | `SkullbonezSource/Maths/Frustum.cpp` |
| Simulation tick | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| Slab test | `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp` |
| Sleep group | `SkullbonezSource/Runtime/Debug/CollisionVisualizer.h` |
| Sleep state | `SkullbonezSource/Physics/PhysicsSolverSnapshot.h` |
| Sleep-only pair | `SkullbonezSource/Physics/SolverBroadphaseStage.h` |
| Sleep-pruned pair | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` |
| Slot count | `SkullbonezSource/Core/SceneCapacity.h` |
| Slot run | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` |
| Snapshot payload | `SkullbonezSource/Runtime/UI/RuntimeViewModel.h` |
| SoA (Structure of Arrays) | `SkullbonezSource/Physics/PhysicsWorld.cpp` |
| Solver | `SkullbonezSource/Physics/PhysicsEngine.cpp` |
| Solver checkpoint chunk | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` |
| Solver delta frame | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Solver track | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Source asset | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` |
| Split fraction | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp` |
| SRV slot | `SkullbonezSource/Rendering/RenderRasterBindingContract.h` |
| Stable identity | `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` |
| Stable pointer view | `SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp` |
| Stable resolution | `SkullbonezSource/Runtime/Editor/EditorHistory.cpp` |
| Staging heap | `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp` |
| Stale identity | `SkullbonezSource/Core/SbResult.cpp` |
| Stamp boundary | `SkullbonezSource/Physics/PhysicsRuntimeSettings.h` |
| Standard capture | `SkullbonezSource/Core/TracyClientOwner.h` |
| Starter scene | `SkullbonezSource/Runtime/Scene/SceneController.Creation.cpp` |
| Startup capacity | `SkullbonezSource/Runtime/App/RunStartupState.h` |
| Startup owner | `SkullbonezSource/Runtime/App/Init.cpp` |
| Startup workflow | `SkullbonezSource/Runtime/Replay/ReplayCoordination.h` |
| State hash | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` |
| State invalidation | `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp` |
| Static row | `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h` |
| Steady gameplay | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h` |
| Step emission | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` |
| Store | `SkullbonezSource/Physics/PhysicsEngine.cpp` |
| Store view | `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp` |
| Strength envelope | `SkullbonezSource/Gameplay/TornadoField.cpp` |
| Strongest field | `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp` |
| Stumpff functions | `SkullbonezSource/Maths/OrbitalMechanics.cpp` |
| Style stamp | `SkullbonezSource/Runtime/Direction/LiveStyleController.h` |
| Submersion snapshot | `SkullbonezSource/Physics/BuoyancySystem.h` |
| Submission barrier | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp` |
| Submission probe | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h` |
| Submission stream | `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Submission view | `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h` |
| Submitted work | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` |
| Support edge | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Support mapping | `SkullbonezSource/Physics/ConvexHullShape.cpp` |
| Suppress exit | `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp` |
| Suppression | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` |
| Surface selection | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Surface view | `SkullbonezSource/Rendering/WorldRenderExtension.h` |
| Swept segment | `SkullbonezSource/Physics/SolverBroadphaseStage.h` |
| Symbol displacement | `SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp` |
| Symbolized stack | `SkullbonezSource/Runtime/Startup/StartupCrashLogging.h` |
| Synthetic device frame | `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h` |
| Synthetic failure | `SkullbonezSource/Runtime/App/ApplicationExitState.cpp` |
| Target snapshot | `SkullbonezSource/Runtime/Camera/AttachedCameraController.h` |
| Target transaction | `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h` |
| Task Manager metric | `SkullbonezSource/Core/MainMemoryStats.h` |
| Terminal drain | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h` |
| Terminate handler | `SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp` |
| Terrain cell | `SkullbonezSource/Physics/PhysicsTerrainView.h` |
| Terrain contact probe | `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` |
| Terrain post | `SkullbonezSource/World/Terrain.h` |
| Terrain replacement | `SkullbonezSource/Runtime/Scene/SceneTerrain.h` |
| Terrain view | `SkullbonezSource/Physics/PhysicsTerrainView.h` |
| Text-only mode | `SkullbonezSource/Runtime/Render/UiTextPass.cpp` |
| Texture key | `SkullbonezSource/Assets/AssetKeys.h` |
| Thread phase | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| Thread token | `SkullbonezSource/Core/SbResult.cpp` |
| Time of flight | `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` |
| Timeline reset | `SkullbonezSource/Runtime/Replay/ReplayCoordination.h` |
| Timer startup boundary | `SkullbonezSource/Runtime/App/RunTimerState.h` |
| Timestamp pair | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h` |
| TOF | `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h` |
| Tombstone | `SkullbonezSource/Rendering/DX12/Dx12TextureRegistry.h` |
| Tool command | `SkullbonezSource/UI/OperatorEditorExchange.h` |
| Tool connection badge | `SkullbonezSource/UI/UITabProfiler.cpp` |
| Tool overlay trace | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h` |
| Tool state | `SkullbonezSource/Runtime/Tools/RuntimeTools.h` |
| Tool surface | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h` |
| Tool-owner scope | `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h` |
| Topology fingerprint | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` |
| Topology lookup | `SkullbonezSource/Runtime/App/ReplayValidation.Internal.h` |
| Topology publication | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h` |
| Topology restore | `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h` |
| Topology version | `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h` |
| Tornado command | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` |
| Trace cap | `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` |
| Trace connection generation | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| Trace scope | `SkullbonezSource/Rendering/DrawCallTrace.h` |
| Trace state | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h` |
| Tracer | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` |
| Track | `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` |
| TrackedMutex | `SkullbonezSource/Core/LockOrderValidator.h` |
| Trajectory record | `SkullbonezSource/Runtime/App/ReplayRuntime.h` |
| Transfer angle | `SkullbonezSource/Maths/OrbitalMechanics.cpp` |
| Transform item | `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h` |
| Transient | `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.cpp` |
| Transient row | `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h` |
| Transient triangle style | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Transition cleanup | `SkullbonezSource/Runtime/Input/InputRouter.h` |
| Transition request | `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp` |
| Translation unit | `SkullbonezSource/Core/FloatingPointContract.h` |
| Trivially copyable | `SkullbonezSource/Maths/Vector3.h` |
| UI command | `SkullbonezSource/Runtime/Scene/SceneController.Navigation.cpp` |
| UI frame data | `SkullbonezSource/Runtime/Render/UiTextPass.cpp` |
| UI frame result | `SkullbonezSource/Runtime/App/InputFrame.cpp` |
| UI text facts | `SkullbonezSource/Runtime/App/RunFrame.cpp` |
| UI text pass | `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` |
| Uncertain result | `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp` |
| Uniform | `SkullbonezSource/Rendering/ShaderContracts.h` |
| Unit domain | `SkullbonezSource/Maths/MathsCommon.h` |
| Unit-domain pole | `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp` |
| V2 target restore | `SkullbonezSource/Runtime/App/ReplayValidation.cpp` |
| VB (Vertex Buffer) | `SkullbonezSource/Rendering/Text.h` |
| VBO (Vertex Buffer Object) | `SkullbonezSource/World/Terrain.h` |
| Velocity gizmo | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp` |
| Velocity preview assertion | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` |
| Velocity preview request | `SkullbonezSource/Runtime/Replay/ReplayAuthoring.h` |
| Vertex layout | `SkullbonezSource/Rendering/ShaderContracts.h` |
| View | `SkullbonezSource/Physics/PhysicsApi.h` |
| View vector | `SkullbonezSource/Runtime/Camera/Camera.h` |
| Viewport | `SkullbonezSource/Rendering/RenderCommandTypes.h` |
| Viewport mapping | `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` |
| Violation | `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` |
| Virtual key word | `SkullbonezSource/UI/UIInput.h` |
| Visibility counters | `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` |
| Visibility view | `SkullbonezSource/UI/UIRenderDiagnostics.h` |
| Visual body metadata | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Visual candidates | `SkullbonezSource/Gameplay/TornadoVisualPass.h` |
| Visual delta frame | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Visual island id | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` |
| Visual packet | `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` |
| Visual projection | `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp` |
| Visual registration | `SkullbonezSource/Gameplay/TornadoGameplay.h` |
| Visualization coordinate | `SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h` |
| Wait | `SkullbonezSource/Core/Fence.h` |
| Wake access | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Wake event | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp` |
| Warm hit | `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp` |
| Warm start | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` |
| Warm starting | `SkullbonezSource/Physics/PersistentContactSolver.cpp` |
| Warmup | `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h` |
| Water mesh build settings | `SkullbonezSource/World/WorldEnvironment.h` |
| Water render style | `SkullbonezSource/World/WorldEnvironment.h` |
| Wet sample | `SkullbonezSource/World/WorldEnvironment.cpp` |
| Widget state | `SkullbonezSource/UI/UITabEditor.h` |
| Win32 handle | `SkullbonezSource/Core/PlatformWin32.h` |
| Window class name | `SkullbonezSource/Core/WindowConstants.h` |
| Wire code | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| WM_INPUT | `SkullbonezSource/Runtime/Input/Input.h` |
| Worker override | `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` |
| Worker-thread policy | `SkullbonezSource/Runtime/App/RunStartupState.h` |
| WorkerPool | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` |
| Working set | `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h` |
| Workspace tick | `SkullbonezSource/Runtime/Replay/ReplayCoordination.h` |
| World click | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp` |
| World extension | `SkullbonezSource/Runtime/Render/RuntimeRenderer.h` |
| World owner | `SkullbonezSource/Runtime/Scene/SceneWorld.h` |
| World ray | `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp` |
| World translation | `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.cpp` |
| World-axis delta | `SkullbonezSource/Maths/Quaternion.cpp` |
| World-settings command | `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` |
| X-macro field list | `SkullbonezSource/Physics/PhysicsWorld.cpp` |
| XZ bounds | `SkullbonezSource/Runtime/Camera/Camera.h` |

## Checklist

The source-of-truth checklist is
`Agentic/Plans/engine-glossary-consolidation-comment-checklist.md`.
It contains exactly 575 unchecked rows, one for every path in the corrected
tracked source-bearing inventory. GC2 may check a row only after inspecting
the file against the updated guide; GC3 must rerun the same `git ls-files`
scope and reconcile every row.

## Validation

GC0 changes documentation only. No repository validation script is required.
The inventory generator reconciled 575 unique tracked source paths, 575
unique checklist rows, and 1,285 unique term rows before writing this report.

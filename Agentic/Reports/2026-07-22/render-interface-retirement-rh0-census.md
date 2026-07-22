# Render Interface Retirement RH0 Census

Date: 2026-07-22
Branch: `nightrunner`
Scope: the ten rendering interfaces named by
`Agentic/Plans/TODO/render-interface-retirement.md`

## Method

The census used the current CodeGraph index first, then confirmed declarations,
inheritance, typed fields/parameters, and member calls against current source
with targeted `rg` reads. "Implementer" below distinguishes production
implementers from test doubles; this corrects the registration claim that every
interface had exactly one implementation without qualification.

## Implementers And Replacement Policy

| Interface | Production implementer | Test implementer / mock | Null or headless production path | Concrete target |
|---|---|---|---|---|
| `IRenderDeviceLifecycle` | `RenderBackendDX12` | None. `Dx12ArchUnitTests` has compile-time result-contract assertions only. | None | `Dx12RenderDevice` for extent/vsync/device state and `Dx12FrameOwner` for submission/drain; `RenderBackendDX12` remains construction/shutdown composition only and is not published as a runtime capability. |
| `IRenderResourceFactory` | `RenderBackendDX12` | `NullRenderResourceFactory` in `TestRenderResourceDoubles.h` | None | `Dx12TextureOwner` and `Dx12GeometryOwner` where those are sufficient; a concrete DX12 cold resource builder owns shader/static-mesh/framebuffer construction. CPU tests move to CPU-domain builders/value fixtures rather than a renamed renderer mock. |
| `IRenderCommandContext` | `RenderBackendDX12` | None | None | `Dx12FrameOwner`/graph executor for frame and graph commands, `Dx12TextureOwner` for texture binding, and `Dx12GeometryOwner` for dynamic/instanced submission. No union command facade is introduced. |
| `IRenderDiagnostics` | `RenderBackendDX12`, delegating state to `Dx12Diagnostics` | None | None | `Dx12Diagnostics` for counters, trace, visibility, timers, and profiler markers; renderer name/capability/memory data cross as value snapshots. |
| `IRenderCaptureBackend` | `RenderBackendDX12`, delegating storage to `Dx12BackbufferCapture` | `UnsupportedCaptureBackend` and `FailingCaptureBackend` in `TestOwnerRequestQueues.cpp` | None | A concrete `Dx12BackbufferCapture` owner with the required frame/extent operation. Queue/error tests move to a value `CaptureReadbackResult` policy seam; no capture interface replacement. |
| `IRenderRayTracing` | `RenderBackendDX12`, delegating state to `Dx12RaytracingOwner` | None | None | `Dx12RaytracingOwner`. Setup, frame dispatch, and UI consumers borrow only that owner. |
| `IRenderShaderDevelopment` | `RenderBackendDX12`, delegating state to `Dx12ShaderDevelopment` | None | None | `Dx12ShaderDevelopment` plus the concrete lifecycle drain at the cold F9 transaction. The unused interface-level enabled query is deleted. |
| `IShader` | `ShaderDX12` | `NullShader` in `TestRenderResourceDoubles.h` | None | `ShaderDX12`. Tests that currently need `NullShader` move to CPU-side style/geometry values or concrete-owner architecture coverage. |
| `IMesh` | `MeshDX12` | `NullMesh` in `TestRenderResourceDoubles.h` | None | `MeshDX12`. Terrain/world CPU tests stop manufacturing a fake GPU mesh and inspect their CPU geometry/state products instead. |
| `IFramebuffer` | `FramebufferDX12` | None; the null resource factory returns no framebuffer | None | `FramebufferDX12`. |

There is no production null, mock, software, or headless renderer. Startup always
constructs DX12 in `Runtime/Init.cpp`. Text-only policy still owns an initialized
DX12 backend; `RuntimeRenderer::Render` skips world submission at the call site,
while the frame graph and late UI/capture ordering remain explicit.

## Consumer And Used-Member Census

Files grouped in one row use the same subset or merely transport that exact
borrow to the named downstream owner. Declaration headers are included because
they are part of the reachable surface that RH5 must compare.

### Cold interfaces

| Interface | Consumer files | Actually used subset | Target borrow |
|---|---|---|---|
| `IRenderCaptureBackend` | `Runtime/CaptureSystem.{h,cpp}` | `SupportsBackbufferCapture`, `CaptureBackbuffer` | concrete capture owner |
| | `Runtime/CaptureController.{h,cpp}`, `LiveStyleController.{h,cpp}`, `InteractionAutomationController.{h,cpp}`, `RuntimeValidationHarness.{h,cpp}` | transport the same two-operation capture request into `CaptureSystem` | concrete capture owner |
| | `Runtime/InputFrameExecution.cpp`, `Runtime/Render/RuntimeRenderHost.{h,cpp}` | nullable/startup-bound storage and required-borrow check only | concrete capture owner pointer; skip/fatal policy stays at call site |
| | `SkullbonezTests/TestOwnerRequestQueues.cpp` | two test implementations exercise unsupported and failed readback results | value-result policy seam; both implementations deleted |
| `IRenderShaderDevelopment` | `Runtime/InputFrameExecution.cpp`, `Runtime/Render/RuntimeRenderHost.h` | `ReloadShadersFromSource`; `ShaderHotReloadEnabled` has no non-backend caller | `Dx12ShaderDevelopment` plus concrete drain owner |
| `IRenderRayTracing` | `Runtime/Render/RuntimeRenderPasses.{h,cpp}` | `BuildTLAS`, `DispatchReflectionRays`, `GetReflectionUAVTexture` | `Dx12RaytracingOwner` |
| | `Runtime/Render/RuntimeRenderer.{h,cpp}` | `InitDXR`, `GetInstancedMeshStaticVBVA`, `GetInstancedMeshStaticStride`, `GetReflectionUAVTexture` | `Dx12RaytracingOwner` |
| | `Runtime/Render/RuntimeRenderInputs.h`, `Runtime/UiTextPass.cpp`, `Runtime/Scene/RunScene.cpp` | transport/query `GetReflectionUAVTexture` only | value texture handle for UI; concrete owner only at setup/draw coordinator |
| | backend shutdown only | `ShutdownDXR` | retained inside concrete raytracing owner lifecycle |

### Resource and diagnostics interfaces

| Interface | Consumer files | Actually used subset | Target borrow |
|---|---|---|---|
| `IRenderResourceFactory` | `World/{Terrain,SkyBox,WorldEnvironment}.{h,cpp}`, `Assets/AssetSystem.{h,cpp}` | `CreateShader`, `CreateMesh` | concrete cold DX12 resource builder |
| | `Assets/TextureCollection.{h,cpp}`, `Runtime/RuntimeStressController.cpp` | `CreateTexture2D`, `DeleteTexture` | `Dx12TextureOwner` |
| | `Rendering/Text.{h,cpp}` | `CreateShader`, `CreateTexture2D`, `DeleteTexture`, `CreateDynamicVB`, `DestroyDynamicVB` | cold resource builder + texture/geometry owners |
| | `Rendering/PrimitiveBatchRenderer.{h,cpp}` | `CreateShader`, `CreateTexture2D`, `DeleteTexture`, `CreateDynamicVB`, `DestroyDynamicVB`, `CreateInstancedMesh`, `DestroyInstancedMesh` | cold resource builder + texture/geometry owners |
| | `UI/UI.{h,cpp}`, `UI/UIFrameComposition.{h,cpp}` | reset transport; `CreateShader`, `CreateDynamicVB`, `DestroyDynamicVB` for previews | cold resource builder + geometry owner |
| | `Runtime/Editor/LauncherLaser.{h,cpp}` | `CreateShader`, `CreateDynamicVB`, `DestroyDynamicVB` | cold resource builder + geometry owner |
| | `Runtime/Render/RuntimeRenderer.{h,cpp}`, `RuntimeRenderInputs.h`, `RuntimeRenderPasses.{h,cpp}`, `RuntimeRenderHost.h`, `Runtime/RunFrame.cpp`, `Runtime/Scene/RunScene.cpp` | transport/release plus `CreateShader`, `CreateDynamicVB`, `DestroyDynamicVB`, `CreateFramebuffer` | explicit cold builder/texture/geometry borrows; no union field |
| | `SkullbonezTests/TestRenderResourceDoubles.h`, `TestCamera.cpp`, `TestCoverageFloorContracts.cpp`, `TestDeterminism.cpp`, `TestTerrain.cpp` | fake shader/mesh/texture creation for CPU tests | CPU-domain builders/value fixtures; renderer doubles deleted |
| `IRenderDiagnostics` | `Rendering/RenderInstanceRenderer.{h,cpp}`, `PrimitiveBatchRenderer.h`, `WorldRenderExtension.h`, `DrawCallTrace.{h,cpp}` | `RecordDrawCall`, `GetFrameDrawCallCount`, `RecordVisibility`, trace push/pop | `Dx12Diagnostics` |
| | `Rendering/RenderGpuTimingOwner.h`, `ProfilerImplementation.cpp` | capability check, frame draw count/memory snapshot, GPU timer begin/end/invalidate/read, platform-profiler begin/end/marker | `Dx12Diagnostics` plus value capability/memory snapshots |
| | `Runtime/Diagnostics/DiagnosticsRuntime.{h,cpp}`, `Runtime/RuntimeStressController.{h,cpp}`, `Runtime/RuntimeValidationHarness.h` | renderer name, capabilities, memory snapshot | value snapshots |
| | `Runtime/UiTextPass.cpp`, `Runtime/UI/OperatorEditorFrameComposer.{h,cpp}` | renderer name, draw count/trace/visibility, memory snapshot | `Dx12Diagnostics` + value metadata/memory snapshot |
| | `Runtime/{Run.cpp,RunFrame.cpp,InputFrameExecution.cpp}`, `Runtime/Render/RuntimeRenderer.{h,cpp}`, `RuntimeRenderInputs.h`, `RuntimeRenderPasses.{h,cpp}`, `RuntimeRenderHost.h`, `Runtime/Scene/RunScene.cpp`, `UI/UI.{h,cpp}`, `UI/UIFrameComposition.{h,cpp}` | reset/transport and capability/name queries | concrete diagnostics owner where mutation occurs; values everywhere else |
| `IShader` | `World/{Terrain,SkyBox,WorldEnvironment}.{h,cpp}`, `Rendering/{PrimitiveBatchRenderer,Text}.{h,cpp}`, `Rendering/Shadow.h`, `UI/UI.{h,cpp}`, `UI/UIFrameComposition.{h,cpp}`, `Runtime/Editor/LauncherLaser.{h,cpp}`, `Runtime/Render/RuntimeRenderPasses.cpp`, `RuntimeRenderResources.h` | `Use`, named scalar/vector/matrix setters, and `SetConstantBufferBytes` where typed blocks are used | owned `ShaderDX12` / borrowed `ShaderDX12&` |
| | `SkullbonezTests/TestRenderResourceDoubles.h` and its five consumers listed above | all no-op virtual methods | deleted with CPU-domain fixture migration |
| `IMesh` | `World/{Terrain,SkyBox,WorldEnvironment}.{h,cpp}` | `Draw`; SkyBox also `PrecompileRasterState` | owned `MeshDX12` / borrowed `MeshDX12&` |
| | `Runtime/Render/RuntimeRenderer.cpp`, `Terrain.h` | `GetVertexCount`, `GetStride`, `GetVertexBufferGPUVA` for DXR setup | `MeshDX12` |
| | `SkullbonezTests/TestRenderResourceDoubles.h` and its five consumers | no-op draw plus vertex/stride metadata | deleted with CPU geometry fixture migration |
| `IFramebuffer` | `Runtime/Render/RuntimeRenderResources.h`, `RuntimeRenderPasses.{h,cpp}`, `RuntimeRenderer.cpp` | `Bind`, `Unbind`, color/depth handles, format, width/height, `ResetResources` | owned `FramebufferDX12` / borrowed `FramebufferDX12&` |

### Command and lifecycle interfaces

| Interface | Consumer files | Actually used subset | Target borrow |
|---|---|---|---|
| `IRenderCommandContext` | `World/{Terrain,WorldEnvironment}.{h,cpp}`, `Assets/TextureCollection.{h,cpp}`, `Rendering/Shadow.h` | `BindTexture` (and pass-local clear where shadow callback owns it) | `Dx12TextureOwner` plus frame owner for clear |
| | `Gameplay/TornadoVisualPass.cpp`, `Rendering/{PrimitiveBatchRenderer,Text}.{h,cpp}`, `ProfilerOverlayPresenter.h`, `ProfilerImplementation.cpp`, `UI/UIFrameComposition.{h,cpp}`, `Runtime/Editor/{LauncherLaser,RunEditorTracer}.{h,cpp}`, `Runtime/Tools/RuntimeTools.h`, `Runtime/Replay/ReplayOverlayRenderer.{h,cpp}` | dynamic/instanced upload and draw, colored lines/triangles, texture bind | `Dx12GeometryOwner` + `Dx12TextureOwner` |
| | `Runtime/Render/RuntimeRenderer.{h,cpp}` | materialize/resolve graph resources, execute transitions, resolve backbuffer token | `Dx12FrameOwner` + concrete graph transient pool |
| | `Runtime/Render/RuntimeRenderPasses.{h,cpp}` | viewport, clear, texture bind, graph target begin/end, dynamic fullscreen draw, debug lines | explicit frame/texture/geometry owners |
| | `Runtime/Render/RuntimeRenderInputs.h`, `RuntimeRenderHost.h`, `Runtime/RunFrame.cpp`, `UI/UI.{h,cpp}`, `UI/UIDraw.{h,cpp}`, `Rendering/WorldRenderExtension.h`, `Runtime/UiTextPass.cpp` | transport only to the subsets above | split concrete borrows in their context values |
| `IRenderDeviceLifecycle` | `Runtime/Init.cpp` | `Init`, terminal `Shutdown` | `RenderBackendDX12` composition root only |
| | `Runtime/Window.{h,cpp}` | `Resize` | concrete device/frame lifecycle owner |
| | `Runtime/RunFrame.cpp` | `Finish`, `Present` | concrete frame/device owners |
| | `Runtime/OperatorCommandApplier.{h,cpp}`, `Runtime/RuntimeStressController.cpp` | `SetVsyncEnabled` | `Dx12RenderDevice` |
| | `Runtime/Render/RuntimeRenderer.{h,cpp}`, `RuntimeRenderPasses.{h,cpp}` | resource drain, width/height, vsync update | `Dx12FrameOwner` + `Dx12RenderDevice` values |
| | `Runtime/Scene/{RunScene,SceneRuntimeLoad,SceneRuntimeGeneratedControls}.{h,cpp}` | `FlushGPU`; RunScene also samples/restores vsync | `Dx12FrameOwner` + `Dx12RenderDevice` |
| | `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp` | static result-type assertions for `FlushGPU`/`DrainForResourceRelease` | concrete owner assertions in RH4; no mock migration |

## RH0 Decisions

1. The four test-double families are not exceptions. They are deleted in RH1
   (capture) or RH2 (resource/shader/mesh) with the interface they implement.
2. A test-only inheritance replacement, callback pack, or function-pointer
   service is forbidden. Capture tests consume value readback results; terrain,
   camera, coverage, and determinism tests consume CPU-domain builders/values.
3. `RuntimeRenderBackendView` does not become a concrete backend mega-facade.
   It is reduced by phase to explicit concrete owner borrows and value snapshots.
4. Text-only execution is skip-at-call-site, not a null backend policy.
5. The exception table remains empty. No interface or virtual method has a
   justified survival row.

## Baseline For RH5

- Ten interface classes exist.
- Production implementers: one per interface.
- Additional test implementations: capture 2, resource factory 1, shader 1,
  mesh 1, framebuffer 0.
- `RuntimeRenderBackendView`: seven interface pointers plus the existing
  development-only concrete ImGui owner.
- The final census must show zero interface classes, zero test implementations,
  no consumer access wider than the subsets above, and no production null or
  headless backend introduced.

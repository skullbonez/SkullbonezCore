# Carmack Global Service Lifetime Plan

Date: 2026-06-28
Status: In progress
Impact area: runtime ownership, rendering, assets, input/window, scene system,
tooling, tests
Validation note: plan-only edits require no validation. PR-bound implementation
should choose the narrowest gate from `AGENTS.md`; broad lifetime changes usually
require `tools\validate_full.bat`.
Batching policy: do not run heavy repository validation for every small Carmack
slice. Cheap checks such as `git diff --check`, formatting, focused static
guardrails, or targeted builds may run per slice when useful. Heavy gates
(`tools\validate_full.bat`, `tools\validate_dx12_renderer.bat`,
`tools\validate_physics.bat`, `tools\validate_perf.bat`, and deep/stress gates)
should run after a batch of up to 10 completed slices, before plan completion,
or before PR handoff, whichever comes first. Run a heavy gate earlier only when
the slice changes risky behavior, baselines, resource/barrier lifetime,
deterministic physics output, or leaves uncertainty that cheap checks cannot
answer.

## Completed Slices

- [x] 2026-06-29: Reconciled the remaining global-owner surface and documented
  the lifecycle of the tolerated process-static owners. Added local lifetime or
  invariant comments around `EngineConfig::Instance()`, `WorkerPool::Instance()`
  and its worker thread-local identity flags, `LockOrderValidator` debug state,
  platform-profiler toggles/depth tracking, and the
  `DiagnosticsRuntime::RuntimeProfiler()` borrow boundary. The reviewed
  remaining live globals are isolated to startup/shutdown samples, backend
  facade compatibility, core diagnostic/service owner internals, and Win32
  callback bridges; normal runtime/render/asset/scene paths do not acquire
  services through globals. Comment-style audit inspected
  `Config.cpp`, `WorkerPool.cpp`, `LockOrderValidator.cpp`,
  `PlatformProfiler.cpp`, and `DiagnosticsRuntime.cpp`. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_final_runtime_boundaries.log`.
  `git diff --check` passed except for the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. Heavy validation and rubber-duck
  review remain deferred until the plan completion batch.
- [x] 2026-06-29: Routed explicit runtime/UI profiler reads through the
  diagnostics boundary. `DiagnosticsRuntime::RuntimeProfiler()` is now the
  single runtime-facing profiler borrow; `Run.cpp`, `RunFrame.cpp`,
  `RunUiTextPass.cpp`, `RuntimeDiagnostics.cpp`, and `UITabProfiler.cpp` no
  longer call `Profiler::Instance()` directly. `InGameUIFrameData` carries a
  nullable borrowed profiler pointer for profiler-tab marker rows, while cached
  draw-call trace rows still render when the pointer is unavailable. The
  boundary ratchet now allows `Profiler::Instance()` only in `Core/Profiler.*`
  and `Runtime/Diagnostics/DiagnosticsRuntime.cpp`, lowering counted
  `Profiler::Instance()` surface from 27 to 15. Comment audit inspected the
  touched runtime diagnostics, UI profiler, UI frame-data, runtime frame/render,
  and guardrail files. Rubber-duck review intentionally deferred until this plan
  is complete. Evidence: static source search found no `Profiler::Instance()`
  in `Runtime` or `UI` outside `DiagnosticsRuntime.cpp`; scoped
  `git diff --check` passed except for the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed draw-call trace scope push/pop through borrowed render
  diagnostics facets. `DrawCallTraceScope` now receives an
  `IRenderDiagnostics*` instead of checking `IsGfxReady()` and calling `Gfx()`
  internally; runtime passes, UI text, frame/render wrappers, and
  `GameModelRenderer` trace scopes now pass the frame diagnostics explicitly.
  `IRenderSceneView`/`GameModelCollection` also forward diagnostics to model and
  shadow-caster draws so batch trace markers do not reacquire the global backend.
  This lowers the `IRenderBackend.h` `Gfx()` allowance from 3 to 1 and counted
  `Gfx()` surface from 6 to 4; the remaining backend header `Gfx()` hit is the
  accessor declaration. Comment audit inspected `IRenderBackend.h`,
  `GameModelRenderer.h/.cpp`, `RenderSceneView.h`, `GameModelCollection.h/.cpp`,
  `RuntimeRenderPasses.h`, `RunPasses.cpp`, `RunFrame.cpp`, `RunRender.cpp`,
  `RunUiTextPass.cpp`, and the plan/TODO docs. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence: static source search found no
  one-argument `DRAW_CALL_TRACE_SCOPE(...)` calls, no `IsGfxReady()` in
  `Runtime` or `Core`, and no runtime/renderer `Gfx()` use outside the backend
  facade plus the intentional `Run.cpp` startup sample; scoped `git diff --check`
  passed except for the pre-existing `tools/codex_usage_daily.bat` CRLF warning.
  Heavy, focused build, and guardrail execution were not run for this slice per
  the current Carmack batching rule.
- [x] 2026-06-29: Routed remaining normal-runtime renderer readiness probes
  through the startup-bound renderer borrow. `RunInput.cpp`,
  `RuntimeRenderHost.cpp`, and `RunPasses.cpp` now test
  `RunSubsystemState::renderBackend` for cinematic toggles, render-host
  cinematic enablement, and tornado visual pass readiness instead of calling
  `IsGfxReady()`. This does not change the counted `Gfx()` allowlist surface,
  but it removes the remaining normal-runtime renderer-readiness globals; only
  the backend facade/header now mention `IsGfxReady()`. Comment audit inspected
  `RunInput.cpp`, `RuntimeRenderHost.cpp`, `RunPasses.cpp`, and the plan/TODO
  docs. Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: static source search found no `IsGfxReady()` under `Runtime` or
  `Core`, except the backend facade/header declarations/implementation, and
  scoped `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed profiler GPU markers and timer queries through the
  renderer diagnostics facet. `Profiler` now stores a nullable borrowed
  `IRenderDiagnostics*` that `Run::Initialise()` binds after startup narrows the
  backend and clears during shutdown after backend-owned profiler resources are
  invalidated. `Profiler::GpuBegin()`, `GpuEnd()`,
  `BeginGpuTimerInternal()`, `EndGpuTimerInternal()`,
  `ReadPendingGpuResults()`, and `InvalidateGpuQueries()` now call the borrowed
  diagnostics facet instead of direct `Gfx()` / `IsGfxReady()` checks. This
  removes the `Core/Profiler.cpp` `Gfx()` allowance and lowers counted `Gfx()`
  surface from 15 to 6 while preserving marker names, hashes, GPU timer IDs, and
  platform profiler event calls. Comment audit inspected `Profiler.h/.cpp`,
  `Run.cpp`, `tools/check_runtime_boundaries.py`, and the plan/TODO docs;
  touched comments now document the diagnostics borrow lifetime. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence: static
  source search found no `Gfx()` or `IsGfxReady()` in `Profiler.cpp/.h`, the
  boundary ratchet has no `Profiler.cpp` `Gfx()` row, and scoped
  `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed runtime DXR capability access through
  `RunSubsystemState::renderRayTracing`. `Run::Initialise()` now samples the
  optional raytracing capability once while binding the renderer borrow,
  `Run::Render()` passes that cached capability into `RuntimeRenderInputs`, and
  scene-load DXR reflection setup uses the same nullable borrow instead of
  reacquiring `GfxRayTracing()`. This removes the `RunRender.cpp` and
  `RunScene.cpp` `GfxRayTracing()` allowances and lowers counted
  `GfxRayTracing()` surface from 4 to 3; the remaining runtime raytracing sample
  is the startup composition-root borrow in `Run.cpp`. Comment audit inspected
  `RunState.h`, `Run.cpp`, `RunRender.cpp`, `RunScene.cpp`,
  `tools/check_runtime_boundaries.py`, and the plan/TODO docs; touched comments
  now document the optional DXR borrow lifetime. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence: static source
  search found no `GfxRayTracing()` in `RunRender.cpp` or `RunScene.cpp`, the
  boundary ratchet has no renderer-global rows for those files, and scoped
  `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed `Run.cpp` shutdown/resource lifecycle renderer access
  through `RunSubsystemState::renderBackend`. `Run::~Run()`,
  `ReleaseBackendOwnedRenderResources()`, and
  `LogRenderResourceLifecycleStep()` now use the startup-bound renderer borrow
  for GPU flushes, resource-factory narrowing, and backend-size lifecycle
  logging instead of reacquiring `Gfx()` after local readiness probes. This
  lowers `Run.cpp` `Gfx()` allowance from 4 to 1 and counted `Gfx()` surface
  from 18 to 15; the remaining `Run.cpp` `Gfx()` call is the intentional
  startup composition-root sample in `Run::Initialise()`. Comment audit
  inspected `Run.cpp`, `tools/check_runtime_boundaries.py`, and the plan/TODO
  docs; touched lifecycle comments remain aligned with the cached renderer
  borrow. Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: static source search found only one `Gfx()` in `Run.cpp`, the
  boundary ratchet allows only one `Run.cpp` `Gfx()` row, and scoped
  `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed render composition through the
  `RunSubsystemState::renderBackend` borrow. `Run::Render()` now checks the
  startup-bound backend pointer and narrows it into command, resource, and
  diagnostics facets before building `RuntimeRenderInputs`, instead of calling
  `Gfx()` after a separate global readiness probe. This removes the
  `RunRender.cpp` `Gfx()` allowance and lowers counted `Gfx()` surface from 19
  to 18; `RunRender.cpp` still carries the explicit `GfxRayTracing()` accessor
  for optional DXR capability publication. The 2026-06-29 runtime DXR borrow
  slice later removed that remaining `RunRender.cpp` `GfxRayTracing()` row.
  Comment audit inspected
  `RunRender.cpp`, `tools/check_runtime_boundaries.py`, and the plan/TODO docs;
  touched comments now document the startup-bound render precondition.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: static source search found no `Gfx()` in `RunRender.cpp`, the
  boundary ratchet has no `RunRender.cpp` `Gfx()` row, and scoped
  `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed the frame loop renderer borrow through
  `RunSubsystemState::renderBackend`. `Run::Execute()` now validates the
  startup-bound renderer borrow once per frame turn, then narrows it into the
  command, resource, diagnostics, and device-lifecycle facets used by input,
  simulation, render, capture, GPU drain, UI accounting, and present. This
  removes the `RunFrame.cpp` `Gfx()` allowance and lowers counted `Gfx()`
  surface from 20 to 19; `RunFrame.cpp` still carries profiler singleton debt.
  Comment audit inspected `RunFrame.cpp`, `tools/check_runtime_boundaries.py`,
  and the plan/TODO docs; touched comments now document the startup-bound frame
  renderer precondition. Rubber-duck review intentionally deferred until this
  plan is complete. Evidence: static source search found no `Gfx()` in
  `RunFrame.cpp`, the boundary ratchet has no `RunFrame.cpp` `Gfx()` row, and
  scoped `git diff --check` passed for touched files. Heavy, focused build, and
  guardrail execution were not run for this slice per the current Carmack
  batching rule.
- [x] 2026-06-29: Routed scene generated-control renderer access through the
  renderer borrow bound during `Run::Initialise()`. `RunSubsystemState` now
  records a nullable non-owning `IRenderBackend*` after startup samples the
  process renderer, `Run` shutdown clears that borrow before backend teardown,
  and `RunInput.cpp`, `RunStress.cpp`, and scene load/reset in
  `RunScene.cpp` pass the cached borrow into generated-control and scene-load
  contexts instead of reacquiring `Gfx()`. This removes the `RunInput.cpp`,
  `RunStress.cpp`, and `RunScene.cpp` `Gfx()` allowances and lowers counted
  `Gfx()` surface from 23 to 20; `RunScene.cpp` still carried the explicit
  `GfxRayTracing()` accessor for first-load DXR reflection setup at this point.
  The 2026-06-29 runtime DXR borrow slice later removed that remaining
  `RunScene.cpp` `GfxRayTracing()` row. Comment audit inspected
  `RunState.h`, `Run.cpp`, `RunInput.cpp`, `RunStress.cpp`,
  `RunScene.cpp`, `tools/check_runtime_boundaries.py`, and the plan/TODO docs;
  touched comments now document the renderer borrow lifetime. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence: static
  source search found no `Gfx()` in `RunInput.cpp`, `RunStress.cpp`, or
  `RunScene.cpp`, the boundary ratchet has no `Gfx()` rows for those files, and
  scoped `git diff --check` passed for touched source files. Heavy, focused
  build, and guardrail execution were not run for this slice per the current
  Carmack batching rule.
- [x] 2026-06-29: Routed window resize handling through a backend borrow owned
  by startup instead of direct `Gfx()` access. `Window` now stores a nullable
  resize backend pointer, `InitRenderBackend()` binds it after DX12 backend
  creation, and `CleanupWindow()` clears it before `DestroyGfxBackend()`.
  `Window::HandleScreenResize()` no-ops for early/late callbacks without a
  backend and otherwise resizes through the borrowed renderer while rebuilding
  text and perspective projections from the existing config borrow. This
  removes the `Window.cpp` `Gfx()` allowance and lowers counted `Gfx()` surface
  from 24 to 23; `Window.cpp` still carries only `Window::Instance()` and
  `pInstance` OS-callback bridge debt. Comment audit inspected `Window.h/.cpp`,
  `Init.cpp`, `tools/check_runtime_boundaries.py`, and the plan/TODO docs;
  touched comments now document the nullable resize-backend lifetime. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence: static
  source search found no `Gfx()` or `IsGfxReady()` in `Window.cpp/.h`, the
  boundary ratchet has no `Window.cpp` `Gfx()` row, and scoped
  `git diff --check` passed for touched files. Heavy or focused validation was
  not run for this slice per the current Carmack batching rule.
- [x] 2026-06-29: Routed asset-system shader creation through explicit render
  resource factories. `AssetSystem::CreateShader()` now receives the active
  `IRenderResourceFactory` and only resolves logical shader names/source
  records; all shader setup callers pass their existing render-resource context.
  The transitional `ActiveAssetSystem()` / `CreateShaderFromActiveAssets()`
  bridge and its `Run` startup/shutdown bind/unbind calls were removed. This
  removes the `AssetSystem.cpp/.h` compatibility rows, lowers counted `Gfx()`
  surface from 26 to 24, lowers `ActiveAssetSystem()` and
  `CreateShaderFromActiveAssets()` debt from 2 to 0, and lowers mutable `g_*`
  debt from 86 to 81. Comment audit inspected `AssetSystem.h/.cpp`,
  `Helper.h/.cpp`, `Text.cpp`, `SkyBox.cpp`, `Terrain.h/.cpp`,
  `WorldEnvironment.cpp`, `CollisionVisualizer.cpp`, `LauncherLaser.cpp`,
  `UI.cpp`, `RunPasses.cpp`, `Run.cpp`, `TestScene.h`, and
  `tools/check_runtime_boundaries.py`; touched comments now document shader
  source resolution, render-resource factory ownership, and removal of
  process-global asset lookup. Rubber-duck review intentionally deferred until
  this plan is complete. Evidence: static source search found no old
  `assets.CreateShader("...")` call signature, no active-asset bridge symbols
  in `SkullbonezSource`, no `Gfx()` in `AssetSystem`, scoped
  `git diff --check` passed for touched files, and the retained perf warning /
  inflated markers remain captured in `TODO.md`. Heavy or focused validation
  was not run for this slice per the current Carmack batching rule.
- [x] 2026-06-29: Routed `Text2d` font resources and HUD draw submission
  through explicit render capabilities. `Text2d::BuildFont()` now receives the
  UI text pass's `IRenderResourceFactory` for SDF atlas texture creation and
  text/quad dynamic buffers, `Text2d::DeleteFont()` destroys those handles
  through the live factory when one is available, and `UiTextPass::Render()`
  opens a scoped text render context over the borrowed command/resource facets
  before invoking HUD, UI, profiler, and replay overlay helpers. `FlushText()`,
  `Render2dQuad()`, and `FlushQuads()` now save/restore state, bind font
  textures, upload dynamic vertices, and draw through `IRenderCommandContext`.
  Runtime startup/release now forwards the same renderer resource-factory facet
  already borrowed for registered resources and skybox setup. This removes the
  `Text.cpp` `Gfx()` allowance and lowers the counted `Gfx()` surface from 59
  to 26. Comment audit inspected `Text.h/.cpp`, `RunUiTextPass.cpp`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `RunRender.cpp`, `Run.cpp`,
  and `tools/check_runtime_boundaries.py`; touched comments now document the
  scoped static-API render context and backend-handle lifetime. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence: static
  source search found no `Gfx()` in the edited text/UI files, no `Text.cpp`
  boundary-ratchet entry, and no old text resource setup signatures. Heavy or
  focused validation was not run for this slice per the current Carmack
  batching rule. The retained perf warning and exact inflated markers remain
  captured in `TODO.md`.
- [x] 2026-06-29: Routed shared primitive render helpers through explicit
  render capabilities. `RenderHelper` now creates/destroys its shared sphere,
  low-poly sphere, box, pine, material-table, and convex-hull dynamic-buffer
  handles through `IRenderResourceFactory`, and all helper state changes,
  texture binding, instance uploads, dynamic-VB draws, and instanced draws use
  `IRenderCommandContext`. `GameModelRenderer`, `IRenderSceneView`,
  `GameModelCollection`, `ObjectPass`, `ShadowPass`, planar reflection,
  replay prediction ghosts, backend rebuild/reset, and DXR scene-load sphere
  prewarm now forward the borrowed command/resource facets instead of letting
  helper code reacquire the process renderer. This removes the `Helper.cpp`
  `Gfx()` allowance and lowers the counted `Gfx()` surface from 93 to 59.
  Comment audit inspected `Helper.h/.cpp`, `GameModelRenderer.h/.cpp`,
  `RenderSceneView.h`, `GameModelCollection.h/.cpp`,
  `RuntimeRenderPasses.h`, `RuntimeRenderHost.cpp`, `RunPasses.cpp`,
  `RunRender.cpp`, `Run.cpp`, `RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`; touched comments now document the
  call-scoped command/resource-facet contract and helper opaque-handle
  lifetime. Rubber-duck review intentionally deferred until this plan is
  complete. Evidence: static source search found no `Gfx()` in `Helper.cpp`,
  no `Helper.cpp` boundary-ratchet entry, and no old no-argument helper
  draw/prewarm call sites. Heavy/focused validation was not run for this slice
  per the current Carmack batching rule. The retained perf warning and exact
  inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed collision visualizer resource lifetime and draw
  submission through explicit render capabilities. `CollisionVisualizer`
  creates/destroys instanced meshes and the hull dynamic vertex buffer through
  `IRenderResourceFactory`, uploads/draws collision solids through
  `IRenderCommandContext`, and no longer opens draw-call trace scopes that would
  reacquire the global renderer service. `ObjectPass::EnsureGpuResources()`
  lazily ensures those resources only while the collision visualizer is enabled,
  preserving the previous no-startup-cost behavior. Runtime backend teardown now
  passes the borrowed factory into `CollisionVisualizer::ResetResources()`.
  This removes the `CollisionVisualizer.cpp` `Gfx()` allowance and lowers the
  counted `Gfx()` surface from 107 to 93. Comment audit inspected
  `CollisionVisualizer.h/.cpp`, `GameModelCollection.h/.cpp`,
  `RenderSceneView.h`, `RunPasses.cpp`, `Run.cpp`, and
  `tools/check_runtime_boundaries.py`; touched comments now document the
  resource-factory versus command-context split and the trace-scope ownership
  hazard. Rubber-duck review intentionally deferred until this plan is
  complete. Evidence: `python tools\check_runtime_boundaries.py` passed with 0
  errors; `git diff --check` reported only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning; selected per-file Profile
  `ClCompile` for `CollisionVisualizer.cpp`, `GameModelCollection.cpp`,
  `RunPasses.cpp`, and `Run.cpp` passed in 12.07s with `/warnaserror`. The
  retained perf warning and exact inflated markers remain captured in
  `TODO.md`.
- [x] 2026-06-29: Routed physics debug line submission through borrowed frame
  command contexts. `BroadphaseVisualizer`, `PhysicsDebugVisualizer`, and
  tornado vector drawing now submit `DrawLinesColored()` through the
  `DebugOverlayPass` frame command context, with the scene/physics
  compatibility forwarders carrying the borrow without storing renderer
  lifetime. This removes the `BroadphaseVisualizer.cpp`,
  `PhysicsDebugVisualizer.cpp`, and `TornadoField.cpp` `Gfx()` allowances and
  lowers the counted `Gfx()` surface from 113 to 107. Comment audit inspected
  `BroadphaseVisualizer.h/.cpp`, `PhysicsDebugVisualizer.h/.cpp`,
  `TornadoField.h/.cpp`, `PhysicsWorld.h/.cpp`, `PhysicsScene.h/.cpp`,
  `PhysicsEngine.h/.cpp`, `GameModelCollection.h/.cpp`,
  `RenderSceneView.h`, `RunPasses.cpp`, and
  `tools/check_runtime_boundaries.py`; touched headers now document the
  command-context borrow as debug-only line submission. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; selected per-file
  Profile `ClCompile` for `BroadphaseVisualizer.cpp`,
  `PhysicsDebugVisualizer.cpp`, `TornadoField.cpp`, `PhysicsWorld.cpp`,
  `PhysicsScene.cpp`, `PhysicsEngine.cpp`, `GameModelCollection.cpp`, and
  `RunPasses.cpp` passed in 20.49s with `/warnaserror`. The retained perf
  warning and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed shadow receiver texture binding through the frame
  command context. `ApplyShadowReceiverUniforms()` now receives an
  `IRenderCommandContext&` and binds or clears `SHADOW_TEXTURE_SLOT` through
  that borrowed frame capability; `TerrainPass` passes its existing
  `RenderFrameContext` command facet into `Terrain::Render()`. This removes the
  `Shadow.h` `Gfx()` allowance and lowers the counted `Gfx()` surface from 115
  to 113. Comment audit inspected `Shadow.h`, `Terrain.h/.cpp`,
  `RunPasses.cpp`, and `tools/check_runtime_boundaries.py`; `Shadow.h` now
  defines command-context texture-slot ownership for disabled receivers.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors;
  selected Profile `ClCompile` for `Terrain.cpp` and `RunPasses.cpp` passed in
  4.27s with `/warnaserror`. The retained perf warning and exact inflated
  markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed texture upload, deletion, and binding through explicit
  render contexts. `TextureCollection` now receives an `IRenderResourceFactory`
  for source-asset rebuilds, JPEG uploads, and backend texture deletion; draw
  code binds resident texture handles through an `IRenderCommandContext` instead
  of hiding renderer access inside `SelectTexture()`. `SkyBox`,
  `RuntimeRenderHost`, `Run::Initialise()`, backend resource release, and
  render passes now pass the appropriate borrowed render facet. This removes
  the `TextureCollection.cpp` `Gfx()` allowance and lowers the counted `Gfx()`
  surface from 118 to 115. Comment audit inspected `TextureCollection.h/.cpp`,
  `SkyBox.h/.cpp`, `RuntimeRenderHost.h/.cpp`, `Run.h/.cpp`,
  `RunRender.cpp`, `RunPasses.cpp`, and `tools/check_runtime_boundaries.py`;
  touched comments now define texture resource-phase work versus draw-time
  binding. Rubber-duck review intentionally deferred until this plan is
  complete. Evidence: `python tools\check_runtime_boundaries.py` passed with 0
  errors; `git diff --check` reported only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning; selected Profile `ClCompile` for
  `TextureCollection.cpp`, `SkyBox.cpp`, `Run.cpp`, `RunRender.cpp`,
  `RunPasses.cpp`, and `RuntimeRenderHost.cpp` passed in 11.16s with
  `/warnaserror`. The retained perf warning and exact inflated markers remain
  captured in `TODO.md`.
- [x] 2026-06-29: Routed terrain mesh creation through explicit render-resource
  contexts. Terrain constructors now build height/collision data without
  creating backend meshes, `TerrainPass::EnsureGpuResources()` injects the
  active `IRenderResourceFactory` and `AssetSystem`, and scene-load DXR setup
  prewarms the terrain mesh through the same renderer borrow before reading
  `Terrain::GetMesh()`. This removes the `Terrain.cpp` `Gfx()` allowance and
  lowers the counted `Gfx()` surface from 120 to 118. Comment audit inspected
  `Terrain.h/.cpp`, `RunPasses.cpp`, `RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`; touched comments now define the render
  resource factory borrow and the DXR prewarm reason. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; `git diff --check`
  reported only the pre-existing `tools/codex_usage_daily.bat` CRLF warning;
  selected Profile `ClCompile` for `Terrain.cpp`, `RunPasses.cpp`, and
  `RunScene.cpp` passed in 8.49s with `/warnaserror`. The retained perf warning
  and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed water mesh creation and reflection texture binding
  through explicit render contexts. `WaterPass::EnsureGpuResources()` now asks
  `WorldEnvironment` to build calm/ocean water meshes and shaders with the
  pass's `RenderResourceContext` and borrowed `AssetSystem`, while
  `RenderFluid()` receives the frame `IRenderCommandContext` for slot-1
  reflection texture binding. This removes the `WorldEnvironment.cpp` `Gfx()`
  allowance and lowers the counted `Gfx()` surface from 123 to 120. Comment
  audit inspected `WorldEnvironment.h/.cpp`, `RunPasses.cpp`, and
  `tools/check_runtime_boundaries.py`; the water resource boundary is now split
  between ensure-time resource creation and frame-time draw state. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; selected Profile
  `ClCompile` for `RunPasses.cpp` and `WorldEnvironment.cpp` passed in 4.65s
  with `/warnaserror`. The retained perf warning and exact inflated markers
  remain captured in `TODO.md`.
- [x] 2026-06-29: Routed terrain and water shader creation through the
  runtime-owned asset registry. Terrain lit and shadow-depth shaders are now
  lazily initialized from the `AssetSystem` borrowed by `TerrainPass` and
  `ShadowPass`, while `WorldEnvironment::RenderFluid()` borrows the water pass
  asset registry before building calm/ocean mesh resources and shaders. This
  removes the `Terrain.cpp` and `WorldEnvironment.cpp`
  `CreateShaderFromActiveAssets()` allowances and lowers the counted
  shader-factory surface from 6 to 2 at this point in the history; a later
  2026-06-29 asset shader-context slice removes the remaining `AssetSystem`
  bridge. Terrain and water direct `Gfx()` mesh/texture/draw compatibility
  remains separate render-capability debt at this point in the history. The
  selected-file compile first caught that
  `Terrain`'s default fallback config could not default-construct the private
  `EngineConfig`; `EngineConfig::Defaults()` now provides a non-singleton
  default value for explicit test/standalone terrains. Comment audit inspected
  `Config.h`, `Terrain.h/.cpp`, `WorldEnvironment.h/.cpp`, `RunPasses.cpp`, and
  `tools/check_runtime_boundaries.py`; terrain and water headers now define the
  borrowed asset-system render-pass role. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; selected Profile
  `ClCompile` for the shader-context batch first caught the `EngineConfig`
  default construction issue, then passed in 14.30s with `/warnaserror` after
  `EngineConfig::Defaults()` was added. The retained perf warning and exact
  inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed Text2d shader creation through the runtime-owned asset
  registry. `UiTextPass::EnsureGpuResources()` now passes `Run`'s `AssetSystem`
  into `Text2d::BuildFont()`, and font setup calls `AssetSystem::CreateShader()`
  for the text, immediate solid-quad, and batched solid-quad shaders instead of
  the active-asset bridge. This removes the `Rendering/Text.cpp`
  `CreateShaderFromActiveAssets()` allowance and lowers the counted
  shader-factory surface from 9 to 6 while leaving Text2d direct `Gfx()` font,
  dynamic-buffer, and draw access as separate render-capability debt. Comment
  audit inspected `Text.h/.cpp`, `RunUiTextPass.cpp`, and
  `tools/check_runtime_boundaries.py`; Text and the UI text pass now define the
  borrowed asset-system setup role. Rubber-duck review intentionally deferred
  until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors. The retained perf
  warning and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed shared primitive-helper shader creation through the
  runtime-owned asset registry. `RenderModels()`, shadow-caster submission, and
  replay ghost drawing now pass `AssetSystem` from `RunPasses.cpp` or
  `RuntimeRenderHost` through `IRenderSceneView`, `GameModelCollection`, and
  `GameModelRenderer` into `RenderHelper` batch helpers. `EnsureSphereShader()`
  and `EnsureShadowDepthShader()` now call `AssetSystem::CreateShader()` for
  `shader.lit_textured_instanced` and `shader.shadow_depth_instanced` instead
  of the active-asset bridge. This removes the `Rendering/Helper.cpp`
  `CreateShaderFromActiveAssets()` allowance and lowers the counted
  shader-factory surface from 11 to 9 while leaving helper direct `Gfx()` mesh
  and draw access as separate render-capability debt. Comment audit inspected
  `Helper.h/.cpp`, `GameModelRenderer.h/.cpp`, `GameModelCollection.h/.cpp`,
  `RenderSceneView.h`, `RunPasses.cpp`, `RuntimeRenderHost.cpp`, and
  `tools/check_runtime_boundaries.py`; the helper/renderer headers now define
  the borrowed asset-system role. Rubber-duck review intentionally deferred
  until this plan is complete. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors. The retained perf
  warning and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed collision visualizer shader creation through the
  runtime-owned asset registry. `RenderCollisionStateSolids()` now carries the
  borrowed `AssetSystem` from `RunPasses.cpp` through `IRenderSceneView` and
  `GameModelCollection` into `CollisionVisualizer::Render()`, and
  `EnsureResources()` calls `AssetSystem::CreateShader("shader.collision_visualizer")`
  instead of the active-asset bridge. This removes the
  `CollisionVisualizer.cpp` `CreateShaderFromActiveAssets()` allowance, lowering
  the counted shader-factory surface from 12 to 11 while leaving direct `Gfx()`
  render-resource compatibility in that diagnostic path as separate debt.
  Comment audit inspected `CollisionVisualizer.h/.cpp`,
  `GameModelCollection.h/.cpp`, `RenderSceneView.h`, `RunPasses.cpp`, and
  `tools/check_runtime_boundaries.py`; `CollisionVisualizer.cpp` now defines
  GPU and UV for the touched resource/mesh comments. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors. The retained
  perf warning and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Threaded the frame-turn renderer name into scene-finished
  debug diagnostics instead of reacquiring the renderer in `Run.cpp`.
  `Execute()` now reads `frameRendererName` from the existing
  `frameRenderBackend` borrow and passes it through `TickScreenshots()`,
  `TickAutoCycle()`, and `TickSceneAdvance()` to `LogSceneFinished()`. Profile
  builds mark those debug-only parameters as intentionally unused when `_DEBUG`
  is off. The boundary ratchet lowered `Run.cpp` `Gfx()` debt from 5 to 4 while
  keeping `RunFrame.cpp` at its existing single frame-turn borrow; the
  reconciled counted `Gfx()` allowlist surface is now 123. Comment audit
  inspected `Run.h`, `Run.cpp`, `RunFrame.cpp`, and
  `tools/check_runtime_boundaries.py`; `RunFrame.cpp` now defines GPU in its
  glossary for the touched frame-borrow comment. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  Profile `ClCompile` for `Run.cpp` passed in 3.82s with `/warnaserror` and no
  diagnostics; focused Profile `ClCompile` for `RunFrame.cpp` first caught
  unused Profile-only parameters, then passed in 3.69s after explicit `(void)`
  markers. The retained perf warning and exact inflated markers remain captured
  in `TODO.md`.
- [x] 2026-06-29: Threaded the scene-load renderer name into physics
  diagnostics startup instead of reacquiring the renderer in `Run.cpp`.
  `LoadScene()` already caches `sceneRendererName`, so
  `BeginPhysicsDiagnosticsRun()` now accepts that string and forwards it to
  `DiagnosticsRuntime`. This keeps debug trace metadata identical while moving
  the renderer-name sample to the scene-load borrow point. The boundary ratchet
  lowered `Run.cpp` `Gfx()` debt from 6 to 5; the reconciled counted `Gfx()`
  allowlist surface is now 124. Comment audit inspected `Run.h`, `Run.cpp`,
  `RunScene.cpp`, and `tools/check_runtime_boundaries.py`. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  Profile `ClCompile` for `Run.cpp` passed in 3.88s with `/warnaserror` and no
  diagnostics; a parallel `RunScene.cpp` focused compile first hit PDB lock
  `C1041`, then the serial rerun passed in 5.29s with `/warnaserror` and no
  diagnostics. The retained perf warning and exact inflated markers remain
  captured in `TODO.md`.
- [x] 2026-06-29: Collapsed same-scope `Run.cpp` renderer lifecycle reads
  into explicit local borrows. Shutdown now flushes through a
  `shutdownRenderBackend` borrow, backend-owned resource release samples
  `releaseRenderBackend` once for resource-factory access and the
  post-`WorldEnvironment` flush, and render-resource lifecycle logging samples
  `lifecycleRenderBackend` once for width/height telemetry. This preserves the
  composition-root renderer compatibility points while avoiding repeated
  wide-backend reacquisition inside the same teardown/logging scope. The
  boundary ratchet lowered `Run.cpp` `Gfx()` debt from 8 to 6; the reconciled
  counted `Gfx()` allowlist surface is now 125. Comment audit inspected
  `Run.cpp` and `tools/check_runtime_boundaries.py`; the `Run.cpp` glossary now
  defines DX12 and GPU for the touched teardown comments. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  Profile `ClCompile` for `Run.cpp` passed in 3.77s with `/warnaserror` and no
  diagnostics. The retained perf warning and exact inflated markers remain
  captured in `TODO.md`.
- [x] 2026-06-29: Collapsed `RunStress.cpp` UI-stress renderer access into one
  stress-turn renderer borrow. `RunUIStressActions()` now samples the active
  backend once into `stressRenderBackend`, reuses it for generated-scene control
  contexts, and guards the deterministic UI-stress vsync toggle through that
  same borrow. This keeps headless stress bookkeeping nullable while avoiding a
  second wide-backend reacquisition inside the action switch. The boundary
  ratchet lowered `RunStress.cpp` `Gfx()` debt from 2 to 1; the reconciled
  counted `Gfx()` allowlist surface was then 127. The 2026-06-29
  generated-control renderer-borrow slice later removed this remaining
  `RunStress.cpp` `Gfx()` row. Comment audit inspected
  `RunStress.cpp` and `tools/check_runtime_boundaries.py`; a lifetime note
  documents the nullable stress-turn borrow. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  Profile `ClCompile` for `RunStress.cpp` passed in 3.13s with `/warnaserror`
  and no diagnostics. The retained perf warning and exact inflated markers
  remain captured in `TODO.md`.
- [x] 2026-06-29: Collapsed `RunScene.cpp` scene-load renderer reads into one
  scene-load renderer borrow. `LoadScene()` now samples the active backend once
  into `sceneRenderBackend`, passes that borrow into begin-load and terrain
  setup helpers, reuses the cached renderer name for generated/authored titles
  and debug scene-start logging, and routes vsync and DXR reflection capability
  checks through the same borrowed facade. This preserves `GfxRayTracing()` as
  the explicit DXR capability accessor while removing repeated wide-backend
  reacquisition from scene loading. The boundary ratchet lowered `RunScene.cpp`
  `Gfx()` debt from 9 to 1; the reconciled counted `Gfx()` allowlist surface is
  was then 128. The 2026-06-29 generated-control renderer-borrow slice later
  removed this remaining `RunScene.cpp` `Gfx()` row. Comment audit inspected `RunScene.cpp` and
  `tools/check_runtime_boundaries.py`; a lifetime note documents the nullable
  renderer borrow. Rubber-duck review intentionally deferred until this plan is
  complete. Evidence: `python tools\check_runtime_boundaries.py` passed with 0
  errors; focused Profile `ClCompile` for `RunScene.cpp` passed in 5.35s with
  `/warnaserror` and no diagnostics. The retained perf warning and exact
  inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Collapsed scattered `RunInput.cpp` backend reads into one
  input-turn renderer borrow. `TakeInput()` now samples the active backend once
  into `inputRenderBackend` after the unfocused-input early return, then reuses
  that borrow for launcher repro renderer naming, DXR water-reflection
  capability checks, UI vsync toggles, and generated-scene control contexts.
  This preserves the existing input behavior while keeping renderer lifetime
  sampling explicit for the current input turn. The boundary ratchet lowered
  `RunInput.cpp` `Gfx()` debt from 4 to 1, reducing total counted `Gfx()`
  surface from 136 to 133. The 2026-06-29 generated-control renderer-borrow
  slice later removed this remaining `RunInput.cpp` `Gfx()` row. Comment audit inspected `RunInput.cpp` and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  Profile `ClCompile` for `RunInput.cpp` passed in 3.06s with 0 warnings and 0
  errors; `git diff --check` reported only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. The retained perf warning and
  exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed render shadow-prep worker collection through the
  `RuntimeRenderHost` worker borrow instead of `WorkerPool::Instance()`.
  `ShadowPass` now passes `m_host.m_workerPool` through `IRenderSceneView`,
  `GameModelCollection`, and `GameModelRenderer` for both ordered shadow-caster
  batch collection and object-shadow bounds scanning. This preserves the
  existing `shadowParallelPrep` thresholds and deterministic ordered merge while
  making the worker service an explicit render-pass dependency. The boundary
  ratchet removed the `GameModelRenderer.cpp` `WorkerPool::Instance()` row, so
  total raw `WorkerPool::Instance()` surface dropped from 5 to 3 and now
  remains only in the Core service owner/self-test plus the single bootstrap
  sample in `Runtime/Init.cpp`. Comment audit inspected `RenderSceneView.h`,
  `GameModelRenderer.h/.cpp`, `GameModelCollection.h/.cpp`, `RunPasses.cpp`,
  and `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; raw source
  `rg "WorkerPool::Instance\(" SkullbonezSource` now finds only
  `Core/WorkerPool.cpp` and `Runtime/Init.cpp`; focused Profile `ClCompile` for
  `GameModelRenderer.cpp`, `GameModelCollection.cpp`, and `RunPasses.cpp`
  passed in 5.19s with 0 warnings and 0 errors; `git diff --check` reported
  only the pre-existing `tools/codex_usage_daily.bat` CRLF warning. The retained
  perf warning and exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Routed normal runtime worker-pool access through explicit
  borrowed services instead of `WorkerPool::Instance()`. `Init.cpp` now samples
  the process worker pool once, initializes/shuts it down through that local
  owner reference, and passes it through `RunApp()` into `Run`. `Run`,
  `GameModelCollection`, `PhysicsEngine`, `PhysicsScene`, and `PhysicsWorld`
  now carry the initialized worker service to runtime physics while standalone
  physics constructors keep their serial/no-service fallback. Runtime worker
  tuning, scene worker overrides, UI worker-count reporting, and replay
  prediction body/sample capture now use the same borrowed service. The render
  host view allowlist explicitly permits the `RenderRuntimeView::workerPool`
  binding so render-pass diagnostics cannot grow untracked service fields. The
  boundary ratchet removed worker singleton rows for `PhysicsWorld.cpp`,
  `RunReplayPredictionHelpers.inl`, `RunUiTextPass.cpp`, `RuntimeTuning.cpp`,
  and `RunScene.cpp`, and lowered `Init.cpp` `WorkerPool::Instance()` debt from
  3 to 1. Total raw `WorkerPool::Instance()` surface dropped from 18 to 5.
  Comment audit inspected `PhysicsWorld.h/.cpp`, `PhysicsScene.h/.cpp`,
  `PhysicsEngine.h/.cpp`, `GameModelCollection.h/.cpp`, `Run.h/.cpp`,
  `Init.cpp`, `RuntimeTuning.h/.cpp`, `RunInput.cpp`, `RunScene.cpp`,
  `RunUiTextPass.cpp`, `RuntimeRenderHost.h`,
  `RunReplayPredictionHelpers.inl`, `RunReplayPredictionVisualizer.inl`,
  `RunReplayTools.cpp`, and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors;
  raw source `rg "WorkerPool::Instance\(" SkullbonezSource` now finds only
  `Core/WorkerPool.cpp`, `Runtime/Init.cpp`, and `Rendering/GameModelRenderer.cpp`;
  focused Profile `ClCompile` for `PhysicsWorld.cpp`, `PhysicsScene.cpp`,
  `PhysicsEngine.cpp`, `GameModelCollection.cpp`, `Run.cpp`,
  `RuntimeTuning.cpp`, `RunInput.cpp`, `RunScene.cpp`,
  `RunUiTextPass.cpp`, and `RunReplayTools.cpp` passed in 21.74s with 0
  warnings and 0 errors after an initial host-binding compile miss was fixed;
  focused Debug `ClCompile` for `Init.cpp` passed in 4.89s with 0 warnings and
  0 errors; `git diff --check` reported only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. The retained perf warning and
  exact inflated markers remain captured in `TODO.md`.
- [x] 2026-06-29: Removed the legacy `Cfg()` shim from `Common.h` and routed
  the last bootstrap config reads through `EngineConfig::Instance()` inside
  `Runtime/Init.cpp`. `ActiveGameModelCapacity()` now requires an explicit
  `EngineConfig&`, so `Common.h` no longer owns any config singleton access.
  The boundary ratchet removed the final `Cfg()` allowance and classified the
  two `Init.cpp` `EngineConfig::Instance()` reads as bootstrap debt, lowering
  total counted `Cfg()` debt from 3 to 0. Comment audit inspected `Common.h`,
  `Init.cpp`, `GameModelCollection.h/.cpp`, the touched `Run*` call sites, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; raw source
  `rg "\bCfg\s*\(" SkullbonezSource` found no matches; a focused Debug
  `ClCompile` for `Init.cpp` passed in 4.85s with 0 warnings and 0 errors;
  focused Profile `ClCompile` for the non-`Init.cpp` capacity call-site set
  passed in 21.17s with 0 warnings and 0 errors. A focused Profile
  `ClCompile` for `Init.cpp` still reproduced the known MSVC `CL.exe`
  `-1073741819` codegen crash with no C++ diagnostics; `git diff --check`
  reported only the pre-existing `tools/codex_usage_daily.bat` CRLF warning.
- [x] 2026-06-29: Routed active game-model capacity through explicit startup
  and collection state instead of the no-arg `ActiveGameModelCapacity()` helper.
  The helper now clamps a supplied `EngineConfig`; `Run` call sites use the
  already-clamped `m_startup.gameModelCapacity`, `GameModelCollection` stores
  its construction-time capacity, and render-host UI text uses the host's
  explicit config reference. The boundary ratchet lowered `Common.h` `Cfg()`
  debt from 2 to 1, lowering total counted `Cfg()` debt from 4 to 3. Comment
  audit inspected `Common.h`, `GameModelCollection.h/.cpp`, `Run.cpp`,
  `RunFrame.cpp`, `RunInput.cpp`, `RunEditorTools.cpp`, `RunStress.cpp`,
  `RunUiTextPass.cpp`, `RunScene.cpp`, and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors;
  raw source `rg` now finds only the `Cfg()` shim itself and the two bootstrap
  `Runtime/Init.cpp` reads; a focused Profile `ClCompile` for
  `GameModelCollection.cpp`, `Run.cpp`, `RunFrame.cpp`, `RunInput.cpp`,
  `RunEditorTools.cpp`, `RunStress.cpp`, `RunUiTextPass.cpp`, and
  `RunScene.cpp` passed in 21.23s with 0 warnings and 0 errors; `git diff
  --check` reported only the pre-existing `tools/codex_usage_daily.bat` CRLF
  warning.
- [x] 2026-06-29: Routed `PhysicsWorld` and `PersistentContactSolver` config
  reads through the physics ownership chain instead of the global config
  accessor. `Run` now constructs `GameModelCollection` with the live
  `EngineConfig`; `GameModelCollection`, `PhysicsEngine`, and `PhysicsScene`
  seed `PhysicsWorld` with a narrow `PhysicsRuntimeConfig` snapshot, while
  explicit default constructors keep standalone/test defaults without
  bootstrapping the config singleton. The persistent contact solver receives
  the same snapshot through `PersistentContactSolverContext`, so solver
  thresholds, gravity/friction, sleep policy, broadphase cell sizing, and
  physics-parallel toggles are all context-backed. The boundary ratchet removed the
  `PersistentContactSolver.cpp` and `PhysicsWorld.cpp` `Cfg()` rows, lowering
  total counted `Cfg()` debt from 48 to 4. Comment audit inspected
  `PhysicsWorld.h/.cpp`, `PersistentContactSolver.cpp`, `PhysicsScene.h/.cpp`,
  `PhysicsEngine.h/.cpp`, `GameModelCollection.h/.cpp`, `Run.h/.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; focused
  `rg` found no `Cfg()` in `PhysicsWorld` or `PersistentContactSolver`; raw
  source `rg` now finds only `Common.h` and `Runtime/Init.cpp` config accessor
  debt; a focused Profile `ClCompile` for `PhysicsWorld.cpp`,
  `PersistentContactSolver.cpp`, `PhysicsScene.cpp`, `PhysicsEngine.cpp`,
  `GameModelCollection.cpp`, and `Run.cpp` passed in 10.38s with 0 warnings and
  0 errors; `git diff --check` reported only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning.
- [x] 2026-06-29: Routed `Terrain` scale, fluid-floor, render-step, and ordinary
  render style reads through construction-time config binding. Runtime RAW and
  flat-slope terrain creation now passes the live `EngineConfig`; explicit
  no-config test constructors keep a default config fallback. Terrain height,
  bounds, collision lookup, render mesh, shader setup, and ordinary render paths
  now use `Terrain::Config()` instead of `Cfg()`. The boundary ratchet removed
  the `Terrain.cpp` `Cfg()` row, lowering total counted `Cfg()` debt from 70 to
  48. Comment audit inspected `Terrain.h/.cpp`, `Run.cpp`, `RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally deferred
  until this plan is complete. Evidence: `python tools\check_runtime_boundaries.py`
  passed with 0 errors; focused `rg` found no `Cfg()` in `Terrain`;
  `git diff --check` reported only the pre-existing `tools/codex_usage_daily.bat`
  CRLF warning.
- [x] 2026-06-29: Routed `WorldEnvironment` water style and fluid drag tuning
  through an explicit runtime config borrow. Runtime and scene world replacement
  paths now construct `WorldEnvironment` with the live `EngineConfig`, water
  style/resource setup reads that bound config, and `AddWorldForces()` samples
  `fluidAngularDragMultiplier` once per force pass instead of reacquiring
  `Cfg()`. The boundary ratchet removed the `WorldEnvironment.cpp` `Cfg()` row,
  lowering total counted `Cfg()` debt from 90 to 70. Comment audit inspected
  `WorldEnvironment.h/.cpp`, `Run.cpp`, `RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally deferred
  until this plan is complete. Evidence: `python tools\check_runtime_boundaries.py`
  passed with 0 errors; focused `rg` found no `Cfg()` in `WorldEnvironment`;
  `git diff --check` reported only the pre-existing `tools/codex_usage_daily.bat`
  CRLF warning.
- [x] 2026-06-29: Routed `RigidBody` angular velocity clamping through body
  state instead of global config access. `RigidBody` now stores a
  construction-time velocity limit with a standalone fallback matching
  `engine.cfg`, and `GameModel` seeds that limit from its bound
  `EngineConfig`. The boundary ratchet removed the `RigidBody.cpp` `Cfg()` row,
  lowering total counted `Cfg()` debt from 93 to 90. Comment audit inspected
  `RigidBody.h/.cpp`, `GameModel.cpp`, and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors;
  focused `rg` found no `Cfg()` in `RigidBody`; `git diff --check` reported only
  the pre-existing `tools/codex_usage_daily.bat` CRLF warning.
- [x] 2026-06-29: Routed `BoundingSphere` drag policy through construction-time
  state instead of a late config singleton lookup. `BoundingSphere` now stores
  its drag coefficient as shape data; `GameModel::AddBoundingSphere()` passes
  the model's bound `EngineConfig::sphereDragCoeff`, and sphere scaling preserves
  the existing shape coefficient. Standalone/test-created spheres keep a local
  smooth-sphere fallback. The boundary ratchet removed the `BoundingSphere.cpp`
  `Cfg()` row, lowering total counted `Cfg()` debt from 94 to 93. Comment audit
  inspected `BoundingSphere.h/.cpp`, `GameModel.h/.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally deferred
  until this plan is complete. Evidence: `python tools\check_runtime_boundaries.py`
  passed with 0 errors; focused `rg` found no `Cfg()` in `BoundingSphere`;
  `git diff --check` reported only the pre-existing `tools/codex_usage_daily.bat`
  CRLF warning.
- [x] 2026-06-29: Routed `GameModel` physics policy constants through explicit
  construction-time config borrows. Authored scenes, generated scenes, editor
  placement, ragdoll construction, and launcher projectiles now pass the live
  `EngineConfig` into `GameModel`; terrain contact thresholds, restitution
  thresholds, sphere drag, and default friction use the model's bound config
  instead of reacquiring `Cfg()` during normal physics paths. The boundary
  ratchet removed the `GameModel.cpp` `Cfg()` row, lowering total counted
  `Cfg()` debt from 104 to 94. Comment audit inspected `GameModel.h/.cpp`,
  `SceneAuthoredSetup.h/.cpp`, `SceneGeneratedSetup.cpp`, `RunScene.cpp`,
  `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`, `RunEditorTools.cpp`,
  `RunEditorObjectPlacement.inl`, `EditorTools.h`, `RuntimeTools.h/.cpp`,
  `Ragdoll.h/.cpp`, and `tools/check_runtime_boundaries.py`. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; individual
  Profile compiles passed for `GameModel.cpp`, `SceneAuthoredSetup.cpp`,
  `SceneGeneratedSetup.cpp`, `RuntimeTools.cpp`, `Ragdoll.cpp`, `Run.cpp`,
  `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`, `RunEditorTools.cpp`, and
  `RunInteractionAutomation.cpp`. A full
  `tools\validate_build.bat Profile` probe hit a repeat MSVC `CL.exe` codegen
  access violation after clean; details are retained in `TODO.md`.
- [x] 2026-06-29: Routed window projection and startup sizing through the
  `EngineConfig` borrow supplied by `Init`. `Window::CreateAppWindow()` now
  binds the live config reference before the Win32 window exists, and resize
  projection rebuilds use that binding instead of sampling `Cfg()` from the
  OS callback path. The boundary ratchet removed the `Window.cpp` `Cfg()` row,
  lowering total counted `Cfg()` debt from 110 to 104. Comment audit inspected
  `Window.h/.cpp`, `Init.cpp`, and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors and
  wrote `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed shadow-prep config gating through the runtime render
  host. `ShadowPass` now passes `m_host.m_config.shadowParallelPrep` into
  `IRenderSceneView::BuildShadowCasterBatches()`,
  `IRenderSceneView::RenderShadowCasters()`, and
  `IRenderSceneView::GetObjectShadowBounds()`, letting `GameModelRenderer`
  choose its ordered worker path without sampling `Cfg()`. The boundary ratchet
  removed the `GameModelRenderer.cpp` `Cfg()` row, lowering total counted
  `Cfg()` debt from 112 to 110. Comment audit inspected `RenderSceneView.h`,
  `GameModelRenderer.h/.cpp`, `GameModelCollection.h/.cpp`, `RunPasses.cpp`,
  and `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed normal model draw config through explicit render-pass
  borrows. `IRenderSceneView::RenderModels()`, `GameModelCollection`, and
  `GameModelRenderer` now receive `RuntimeRenderFlags` and
  `OrdinaryRenderConfig` from the runtime render host instead of sampling
  `Cfg()` during object rendering. `RenderHelper` primitive batches receive the
  ordinary render config directly for shader constants, and `EnsureSphereMesh()`
  remains mesh-only for DXR prewarm instead of initializing the lit shader
  without frame config. The boundary ratchet removed the `Helper.cpp` `Cfg()`
  row and lowered `GameModelRenderer.cpp` `Cfg()` debt from 3 to 2, lowering
  total counted `Cfg()` debt from 115 to 112. Comment audit inspected
  `Helper.h/.cpp`, `RenderSceneView.h`, `GameModelRenderer.h/.cpp`,
  `GameModelCollection.h/.cpp`, `RunPasses.cpp`,
  `RuntimeRenderHost.cpp`, and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors and
  wrote `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed text renderer startup projection sizing through the
  UI text pass's existing runtime config borrow. `UiTextPass::EnsureGpuResources()`
  now passes configured startup screen dimensions into `Text2d::BuildFont()`,
  and `Text.cpp` rebuilds its initial text projection from those explicit
  dimensions instead of calling `Cfg()`. The boundary ratchet removed the
  `Text.cpp` `Cfg()` row, lowering total counted `Cfg()` debt from 117 to 115.
  Comment audit inspected `Text.h/.cpp`, `RunUiTextPass.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed normal startup and CLI config mutation through
  explicit `EngineConfig&` borrows. `ParseCommandLine()` now loads `engine.cfg`
  once into a local config reference, applies `--vsync`, worker/model-capacity,
  physics-parallel, shadow-prep, and dump-config operations through that
  reference, and avoids reacquiring `Cfg()` inside CLI directive lambdas.
  `WinMain` samples the already-loaded live `EngineConfig&` once for
  worker/window startup and passes it through `RunApp()` into the `Run`
  constructor. `Run` now stores that borrowed reference instead of calling
  `Cfg()` itself. The boundary ratchet removed the `Run.cpp` `Cfg()` row and
  lowered `Init.cpp` `Cfg()` debt from 16 to 2, lowering total counted `Cfg()`
  debt from 132 to 117. Comment audit inspected `Init.cpp`, `Run.h/.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed launcher repro snapshot physics config through the
  existing `LauncherReproSnapshotContext`. `RunInput.cpp` now passes `Run`'s
  borrowed live `EngineConfig&` into the debug snapshot request, and
  `LauncherTools.cpp` uses that context for terrain-support probing plus the
  emitted `cfg_friction_coeff` and `cfg_contact_epsilon` rows instead of
  reacquiring `Cfg()`. The boundary ratchet removed the `LauncherTools.cpp`
  `Cfg()` row, lowering total counted `Cfg()` debt from 135 to 132. Comment
  audit inspected `LauncherTools.cpp`, `RuntimeTools.h`, `RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-29: Routed camera movement/clamp tuning through the Run-owned
  camera collection's borrowed live `EngineConfig&`. `Run` binds `m_config` to
  `CameraCollection` during construction, `CameraCollection` forwards that
  borrow into each camera slot and tween/render scratch camera, and camera
  movement, XZ clamp repair, pitch collision caps, and tween terrain-height
  clamping now read through the bound config instead of calling `Cfg()`. The
  boundary ratchet removed the `Camera.cpp` and `CameraCollection.cpp` `Cfg()`
  rows, lowering total counted `Cfg()` debt from 158 to 135. Comment audit
  inspected `Camera.h/.cpp`, `CameraCollection.h/.cpp`, `Run.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the remaining `RunScene.cpp` scene-load config
  cluster through `Run`'s borrowed live `EngineConfig&`. Scene worker override,
  contact verification tolerance, scene reset/default runtime flags, model
  capacity overrides, terrain source resolution, world/environment defaults,
  generated-model setup, active cinematic config, tornado defaults, and launch
  shadow overrides now use `m_config` instead of reacquiring `Cfg()` inside the
  scene load/reset path. The boundary ratchet removed the `RunScene.cpp`
  `Cfg()` row and lowered total counted `Cfg()` debt from 176 to 158. Comment
  audit inspected `RunScene.cpp` and `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors
  and wrote `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed replay tool window-sizing config reads through
  `Run`'s borrowed live `EngineConfig&`. Cause-tree hit testing, scrubber
  layout, and replay velocity-edit gating now pass `m_config` into
  `RuntimeWindowScreenWidth/Height()` instead of reacquiring `Cfg()` from the
  replay `.inl` files. The boundary ratchet removed the
  `RunReplayCauseTreeTools.inl`, `RunReplayScrubberTools.inl`, and
  `RunReplayVelocityEdit.inl` `Cfg()` rows, lowering total counted `Cfg()` debt
  from 182 to 176. Comment audit inspected all three `.inl` files and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the remaining `RunInput.cpp` config cluster through
  `Run`'s borrowed live `EngineConfig&`. Attached-camera mouse sensitivity,
  replay/scene-control cinematic config assembly, shadow toggles, ordinary and
  cinematic UI tuning, scene-generated control context creation, default-save
  commands, and camera height clamps now use `m_config` instead of reacquiring
  `Cfg()` during input handling. The boundary ratchet removed the
  `RunInput.cpp` `Cfg()` row and lowered total counted `Cfg()` debt from 204
  to 182. Comment audit inspected `RunInput.cpp` and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed runtime worker-thread UI tuning through an explicit
  `EngineConfig&` parameter. `RunInput.cpp` now passes `Run`'s borrowed
  `m_config` into `ApplyWorkerThreadCountOverride()`, and the helper mutates
  that config reference instead of reacquiring `Cfg()`. The boundary ratchet
  removed the `RuntimeTuning.cpp` `Cfg()` row and lowered total counted
  `Cfg()` debt from 205 to 204. Comment audit inspected `RuntimeTuning.h/.cpp`,
  `RunInput.cpp`, and `tools/check_runtime_boundaries.py`. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the one-call runtime automation/style/stress config
  rows through `Run`'s borrowed live `EngineConfig&`. Replay-control automation
  now sizes controls from `m_config`, live-style scene application uses
  `RuntimeActiveCinematicConfig(..., m_config)`, and the stress generated-scene
  context receives `m_config` directly. The boundary ratchet removed
  `RunInteractionAutomation.cpp`, `RunLiveStyle.cpp`, and `RunStress.cpp`
  `Cfg()` rows, lowering total counted `Cfg()` debt from 208 to 205. Comment
  audit inspected all three source files plus `tools/check_runtime_boundaries.py`.
  Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors
  and wrote `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the remaining `RunUiTextPass.cpp` ordinary-render
  config read through the render host's borrowed live `EngineConfig&`. UI frame
  data now takes `ordinaryRender` from `m_host.m_config` alongside the existing
  host-provided cinematic state. The boundary ratchet removed the
  `RunUiTextPass.cpp` `Cfg()` row and lowered total counted `Cfg()` debt from
  209 to 208. Comment audit inspected `RunUiTextPass.cpp` and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Reconciled stale `RunPasses.cpp` config debt after a source
  recheck found no remaining `Cfg()` call sites in that file. The boundary
  ratchet removed the stale `RunPasses.cpp` `Cfg()` allowance and lowered total
  counted `Cfg()` debt from 211 to 209. Comment audit inspected
  `tools/check_runtime_boundaries.py`; `RunPasses.cpp` source did not need an
  edit. Rubber-duck review intentionally deferred until this plan is complete.
  Evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors
  and wrote `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the remaining `RunFrame.cpp` config reads through
  `Run`'s borrowed live `EngineConfig&`. Scene-generated solver setup,
  replay/scene-control cinematic config assembly, and frame camera tuning now
  use `m_config` instead of reacquiring `Cfg()` during the frame loop. The
  boundary ratchet removed the `RunFrame.cpp` `Cfg()` row and lowered total
  counted `Cfg()` debt from 215 to 211. Comment audit inspected
  `RunFrame.cpp` and `tools/check_runtime_boundaries.py`. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed the remaining `RunRender.cpp` config reads through
  `Run`'s borrowed live `EngineConfig&`. Ordinary shadow override assembly now
  reads `m_config.ordinaryRender`, and relative camera clamp bounds use
  `m_config.minCameraHeight`/`m_config.maxCameraHeight` instead of reacquiring
  `Cfg()`. The boundary ratchet removed the `RunRender.cpp` `Cfg()` row and
  lowered total counted `Cfg()` debt from 218 to 215. Comment audit inspected
  `RunRender.cpp` and `tools/check_runtime_boundaries.py`. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Collapsed `Run.cpp` config sampling to one composition-root
  borrow. `Run` now stores a borrowed live `EngineConfig&` initialized from the
  process config singleton once in the constructor, then uses that reference for
  render-host construction, startup defaults, initialization, backend resource
  rebuild source records, and Debug physics diagnostics. The boundary ratchet
  lowered `Run.cpp` `Cfg()` debt from 4 to 1 and total counted `Cfg()` debt from
  221 to 218. Comment audit inspected `Run.h`, `Run.cpp`, `RunRender.cpp`, and
  `tools/check_runtime_boundaries.py`; the new field is documented as a borrowed
  composition-root edge, not a new global service locator. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Passed built-in asset registration config explicitly from its
  composition-root callers. `RegisterBuiltInAssets()` now receives an
  `EngineConfig` reference from `Run::Initialise()` and backend resource rebuild
  wiring instead of calling `Cfg()` internally; terrain/sky texture source
  records still use the same configured paths. The boundary ratchet lowered
  `Run.cpp` `Cfg()` debt from 5 to 4 and total counted `Cfg()` debt from 222 to
  221. Comment audit inspected `Run.h`, `Run.cpp`, `RunRender.cpp`, and
  `tools/check_runtime_boundaries.py`. Rubber-duck review intentionally deferred
  until this plan is complete, and the asset-registration validation checkbox
  remains open for the next heavy batch. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Grouped `Run::Run()` startup-default config reads through one
  local `EngineConfig` borrow instead of repeated `Cfg()` calls. Runtime render
  settings, default cinematic render config, startup model capacity, and startup
  worker-thread defaults now come from the same sampled config reference; model
  capacity still uses the same clamp bounds as `ActiveGameModelCapacity()`. The
  boundary ratchet lowered `Run.cpp` `Cfg()` debt from 8 to 5 and total counted
  `Cfg()` debt from 225 to 222. Comment audit inspected `Run.cpp` and
  `tools/check_runtime_boundaries.py`; the existing startup lifetime comments
  still match the grouped config borrow. Rubber-duck review intentionally
  deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- [x] 2026-06-28: Routed skybox config reads through the `Run` startup wiring
  path instead of reading `Cfg()` inside `SkyBox`. `Run::Initialise()` now
  samples `EngineConfig` once for terrain, skybox, and world setup; `SkyBox`
  borrows that config alongside the runtime-owned texture registry, asset
  registry, and render resource factory. The boundary ratchet lowered
  `Run.cpp` `Cfg()` debt from 9 to 8, removed the `SkyBox.cpp` `Cfg()` row, and
  lowered total counted `Cfg()` debt from 228 to 225. Comment audit inspected
  `Run.cpp`, `SkyBox.h/.cpp`, and `tools/check_runtime_boundaries.py`; touched
  headers still have learning headers and local lifetime notes. Rubber-duck
  review intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`; `git diff --check`
  passed with only the pre-existing `tools/codex_usage_daily.bat` CRLF warning.
- [x] 2026-06-28: Routed volumetric and tonemap shader depth-parameter helpers
  through the render host's borrowed `EngineConfig` instead of reading `Cfg()`
  directly. The helper signatures now receive config from their pass call sites,
  preserving the same `frustumNear`/`frustumFar` values while removing the last
  direct `Cfg()` calls from those shader-binding helpers. The boundary ratchet
  lowered `RunPasses.cpp` `Cfg()` debt from 4 to 2 and total counted `Cfg()`
  debt from 230 to 228. Comment audit inspected `RunPasses.cpp` and
  `tools/check_runtime_boundaries.py`; a stale reflected-sky comment was updated
  to name the borrowed render config instead of `Cfg()`. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\runpasses_shader_config_runtime_boundaries.log`),
  `tools\validate_format.bat` passed
  (`TestOutput\validation\agent_logs\runpasses_shader_config_validate_format.log`),
  `tools\validate_fast.bat` passed in 24.84s
  (`TestOutput\validation\agent_logs\runpasses_shader_config_validate_fast.log`),
  `tools\validate_full.bat` passed in 30.44s with 0 DX12 validation errors,
  matching screenshots, and byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\runpasses_shader_config_validate_full.log`),
  and `git diff --check` passed.
- [x] 2026-06-28: Grouped repeated runtime config reads through existing
  borrowed or local `EngineConfig` references. `SkyPass::Render()` now uses the
  render host's borrowed config for cube-map sky height/scale; replay-generated
  scene rebuilds and camera update tuning in `RunFrame.cpp` sample config once
  per local scope; replay-control automation samples config once for screen-size
  fallbacks. The boundary ratchet lowered `RunPasses.cpp` `Cfg()` debt from 6
  to 4, `RunFrame.cpp` from 7 to 4, and `RunInteractionAutomation.cpp` from 2
  to 1, lowering total counted `Cfg()` debt from 236 to 230. Comment audit
  inspected `RunPasses.cpp`, `RunFrame.cpp`, `RunInteractionAutomation.cpp`,
  and `tools/check_runtime_boundaries.py`; existing learning headers and local
  behavior comments still match the touched code. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\config_read_grouping_runtime_boundaries.log`),
  `tools\validate_format.bat` passed
  (`TestOutput\validation\agent_logs\config_read_grouping_validate_format.log`),
  `tools\validate_fast.bat` passed in 97.63s
  (`TestOutput\validation\agent_logs\config_read_grouping_validate_fast.log`),
  `tools\validate_full.bat` passed in 30.77s with 0 DX12 validation errors,
  matching screenshots, and byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\config_read_grouping_validate_full.log`),
  and `git diff --check` passed.
- [x] 2026-06-28: Routed `RuntimeRenderHost` helper config reads through a
  borrowed live `EngineConfig` reference supplied by `Run` construction instead
  of calling `Cfg()` inside render-host methods. `ActiveCinematicConfig()`,
  `IsCinematicRenderingEnabled()`, and render-host window-size fallbacks now use
  the borrowed config; the boundary ratchet removed the
  `RuntimeRenderHost.cpp` `Cfg()` row and raised `Run.cpp`'s composition-root
  count by one, lowering total counted `Cfg()` debt from 239 to 236. Comment
  audit inspected `RuntimeRenderHost.h/.cpp`, `Run.cpp`, and
  `tools/check_runtime_boundaries.py`; the host header now defines
  `EngineConfig` and expands `DXR` in its glossary. Rubber-duck review
  intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\render_host_config_context_runtime_boundaries.log`),
  `tools\validate_format.bat` passed
  (`TestOutput\validation\agent_logs\render_host_config_context_validate_format.log`),
  `tools\validate_fast.bat` passed
  (`TestOutput\validation\agent_logs\render_host_config_context_validate_fast.log`),
  `tools\validate_full.bat` passed in 30.08s with 0 DX12 validation errors,
  matching screenshots, and byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\render_host_config_context_validate_full.log`),
  and `git diff --check` passed.
- [x] 2026-06-28: Routed authored and generated scene camera setup through the
  runtime-owned camera service passed in the scene setup context instead of
  reacquiring `CameraCollection::Instance()`. `SceneAuthoredSetup` and
  `SceneGeneratedSetup` now assert that their borrowed camera pointer is present
  before mutating scene cameras, preserving the existing camera insertion and
  terrain-bound behavior while making scene load ownership explicit. The
  runtime-boundary ratchet removed the `SceneAuthoredSetup.cpp` and
  `SceneGeneratedSetup.cpp` `CameraCollection::Instance()` allowlist rows,
  leaving only camera bootstrap/owner definitions as counted singleton debt.
  Rubber-duck review intentionally deferred until this plan is complete. Evidence:
  `python tools\check_runtime_boundaries.py` passed in 4.82s
  (`TestOutput\validation\agent_logs\camera_scene_context_runtime_boundaries.log`),
  `tools\validate_format.bat` passed in 7.31s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_format.log`),
  `tools\validate_fast.bat` passed in 256.23s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_fast.log`),
  `tools\validate_full.bat` passed in 30.13s
  (`TestOutput\validation\agent_logs\camera_scene_context_validate_full.log`),
  and `git diff --check` passed.
- [x] 2026-06-28: Added a narrow HWND-bound lifecycle for Win32 callback-fed
  input accumulators. `Input::BindCallbackBridge()` arms wheel/raw mouse queues
  for the active `HWND`, WndProc passes the originating `HWND` into accumulator
  calls, `Input::UnbindCallbackBridge()` clears queued state during cleanup
  before backend/window class teardown, and Debug assertions catch double-bind,
  unbound unbind, wrong-window unbind, and raw-input registration before the
  bridge is bound. Rubber-duck review by Mencius found two blocking lifecycle
  assertion/gating holes, one non-blocking startup-order check, and one blocking
  stale-plan completion issue; all were fixed. Runtime input/window behavior
  change; final `tools\validate_full.bat` passed in 140.80s and focused
  `tools\validate_interaction_clicks.bat` passed in 5.74s before commit.
- [x] 2026-06-28: Documented the Win32 input callback bridge accumulators in
  `Input.cpp` without changing behavior. The file now separates callback-fed
  wheel/raw mouse queues from cursor policy and scripted automation override
  state, names `WndProc` as the callback writer, records the UI and camera
  consumers/reset paths, and leaves bind/unbind lifecycle work open. Rubber-duck
  review by Kepler found two blocking stale/incorrect comment claims and two
  non-blocking wording issues; all were fixed. Comment-only source/documentation
  slice; no repository validation required.
- [x] 2026-06-28: Added debug assertions for `EngineContext` borrowed runtime
  bindings before they are used. `EngineContext::Bind()` now asserts that the
  complete Run-owned service graph is present, both `Bindings()` accessors
  assert before handing out borrowed pointers, and
  `RuntimeViewModelBuilder::Build()` asserts before its release-safe default
  fallback can mask an unbound context. Rubber-duck review by Faraday found one
  blocking bypass through the view-model builder and one non-blocking startup
  false-positive check; the bypass was fixed before validation. Final
  `tools\validate_full.bat` passed with runtime boundaries clean, Profile/Debug
  builds at 0 warnings/errors, DX12 InfoQueue errors at 0 with matching
  screenshots, and byte-exact `physics_regression_solver.csv`.
- [x] 2026-06-28: Documented the current startup bind order and shutdown/backend
  resource release order. The lifetime-order section now records how `Init.cpp`
  owns worker/window/backend creation, how `Run::Run()` binds `EngineContext`,
  how `Run::Initialise()` binds window/renderer/title, texture and active asset
  bridges, built-in assets, terrain, skybox, UI text, cameras, and first scene
  load, and how `Run::~Run` flushes GPU work, releases backend-owned runtime
  resources, unbinds the active asset bridge, then returns to `Init.cpp` for
  worker/backend/window/COM teardown. Rubber-duck review by Sagan found three
  blocking documentation inaccuracies and one non-blocking ordering note; all
  were fixed before commit. Documentation-only slice; no repository validation
  required.
- [x] 2026-06-28: Routed editor placement's building asset-library lookup
  through explicit borrowed `AssetSystem` context instead of
  `ActiveAssetSystem()`. Placement preview, overlay ghost tracing, placement
  preflight, and placement commit now receive `m_systems.assets` through editor
  context structs; the process-static parsed building catalog remains read-only
  after first load. The runtime-boundary ratchet removed the
  `RunEditorPlacementAssets.inl` `ActiveAssetSystem()` allowlist row, lowering
  the remaining active-asset compatibility surface to the asset-system helper
  declarations/definitions only. Rubber-duck review by Pauli found no blockers,
  two non-blocking residual notes, and three missing-evidence reminders: the
  one-shot parsed building catalog cache is intentionally preserved, unrelated
  user-owned dirty docs remain unstaged, and evidence was completed before
  commit. Comment-style audit inspected the touched source-bearing files.
  Final evidence: `python tools\check_runtime_boundaries.py`,
  `tools\validate_fast.bat`, and `tools\validate_full.bat` all passed; logs are
  under `TestOutput\validation\agent_logs\editor_asset_context_*`.
- [x] 2026-06-28: Routed scene/style parsing asset-library lookup through an
  explicit borrowed `AssetSystem` instead of `ActiveAssetSystem()`. Runtime
  scene loads, live-style reloads, cinematic style browser loads, and demo hero
  style loading now pass `m_systems.assets` into `TestScene` parser overloads;
  tools or standalone parser callers can still use the old path-convention
  fallback by omitting the registry. The runtime-boundary ratchet removed the
  `TestSceneParser.cpp` `ActiveAssetSystem()` allowlist row. That slice left
  editor placement's static building-library cache as separate follow-up debt,
  which the editor placement slice above now closes. Rubber-duck review by Harvey
  found one blocking documentation mismatch: the current-count snapshot still
  claimed `ActiveAssetSystem()` count `4` and still listed
  `TestSceneParser.cpp`. The current snapshot records count `2` for the
  compatibility helper declaration/definition only and removes the parser row.
  Final evidence: `tools\check_runtime_boundaries.py`,
  `tools\validate_fast.bat`, and `tools\validate_full.bat` all passed; logs are
  under `TestOutput\validation\agent_logs\scene_parser_asset_context_*`.
- [x] 2026-06-28: Added an explicit renderer-global file-classification fence
  to `tools\check_runtime_boundaries.py`. The existing global-service ratchet
  still preserves current counts, but direct `Gfx()` and `GfxRayTracing()` calls
  now also require a named compatibility-location classification such as
  runtime composition root, backend accessor definition, UI compatibility, or
  render helper compatibility. Synthetic tests prove count allowances alone do
  not approve a new unclassified `Gfx()` or `GfxRayTracing()` file. This
  resolves the renderer-specific bootstrap/compatibility guardrail while the
  broader per-site classification of all remaining globals stays open under
  Inventory.
  Comment-style audit: inspected `tools\check_runtime_boundaries.py`; the
  learning header now names the renderer-global classification fence and the
  new allowlist comment explains the invariant.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\renderer_global_classification_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed
  (`TestOutput\validation\agent_logs\renderer_global_classification_validate_fast.log`).
- [x] 2026-06-28: Replaced the legacy skybox singleton with a `Run`-owned
  `std::unique_ptr<SkyBox>` shell that borrows the runtime texture registry,
  asset registry, and renderer resource-factory facet. `SkyBox.cpp` no longer
  calls `TextureCollection::Instance()`, `CreateShaderFromActiveAssets()`,
  `Gfx()`, or `SkyBox::Instance()`, and `SkyBox.h/.cpp` no longer carry
  `pInstance` storage. The backend-capability rubber-duck blocker was fixed in
  the same ratchet pass by removing stale `UIBackdropBlur.cpp` allowances after
  its renderer calls had already been removed. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\backend_and_skybox_guardrail_runtime_boundaries.log`.
- [x] 2026-06-28: Replaced the texture collection singleton with a
  `RunSubsystemState` value member. `Run::Initialise()` now binds the owned
  texture collection to the runtime asset system directly, render services and
  `RuntimeRenderHost` borrow it by reference, and backend-resource release calls
  `DeleteAllTextures()` plus `BindAssetSystem(nullptr)` on the owned value. This
  removes all source calls to `TextureCollection::Instance()` and removes the
  stale checker allowances. Evidence: `python tools\check_runtime_boundaries.py`
  passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_texture_skybox_runtime_boundaries.log`.
- [x] 2026-06-28: Replaced the camera collection singleton with `Run`-owned
  storage. `RunSubsystemState` now owns `CameraCollection` by value and keeps a
  borrowed `cameras` pointer for existing split runtime files, `Run::Initialise()`
  resets/rebinds that pointer before the first scene load, and backend-resource
  release resets the owned collection before clearing the borrow. `CameraCollection`
  no longer exposes `Instance()`/`Destroy()` and no longer stores `pInstance`;
  the only remaining `pInstance` debt is the Win32 `Window` bridge. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_texture_skybox_camera_runtime_boundaries.log`.
- [x] 2026-06-28: Removed the normal runtime `Window::Instance()` lookup from
  `Run::Initialise()`. `RunApp()` now passes the already-created `Window` into
  the `Run` constructor, `Run` stores the borrowed pointer in `RunSubsystemState`
  before binding runtime contexts, and `Initialise()` enforces that startup
  precondition instead of reacquiring the singleton. The remaining
  `Window::Instance()` calls are bootstrap startup, `Input` static bridge
  helpers, and `Window`'s own compatibility implementation. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_window_runtime_boundaries.log`.
- [x] 2026-06-28: Extended the Win32 input bridge so static input helpers use
  the bound callback `HWND` instead of reacquiring `Window::Instance()`.
  `Input::IsAppFocused()`, `GetClientMouseCoordinates()`, and
  `CentreMouseCoordinates()` now fail closed or convert through
  `s_callbackBridgeWindow`; mouse centering reads the current client rect from
  the bound window handle. This removes the `Input.cpp`
  `Window::Instance()` allowance and leaves only bootstrap startup plus
  `Window.cpp` compatibility/OS-callback access. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_window_input_bridge_runtime_boundaries.log`.
- [x] 2026-06-28: Routed launcher laser shader creation through the
  runtime-owned asset registry. `LauncherLaser::Render()` now borrows
  `AssetSystem` from `Run`'s render callback and `EnsureResources()` calls
  `AssetSystem::CreateShader("shader.launcher_laser")` instead of the
  active-asset bridge. This removes the `LauncherLaser.cpp`
  `CreateShaderFromActiveAssets()` allowance while leaving direct `Gfx()` render
  compatibility in that tool path as separate render-capability debt. Evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_launcher_laser_asset_context_runtime_boundaries.log`.
- [x] 2026-06-28: Routed launcher laser transient geometry through the debug
  overlay render contexts. `DebugOverlayPassInputs` now carries the
  `RenderResourceContext`, `RuntimeRenderHostCallbacks::RenderEditorOverlayFn`
  borrows `IRenderCommandContext` and `IRenderResourceFactory`, and
  `LauncherLaser::Render()` uses those facets for draw-state queries, dynamic
  vertex-buffer creation/destruction, and upload/draw submission instead of
  direct `Gfx()` access. This removes the `LauncherLaser.cpp` `Gfx()` allowance,
  lowers the counted `Gfx()` surface from 175 to 157, and makes future laser
  backend-global access fail the guardrail. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_launcher_laser_context_runtime_boundaries.log`.
- [x] 2026-06-28: Routed the editor overlay tracer draw through the same
  debug-overlay command context. `RunEditorTracer::Render()` now receives
  `IRenderCommandContext&`, submits its transient colored lines through
  `DrawLinesColored()`, and no longer checks or calls the global renderer.
  This removes the `RunEditorTracer.inl` `Gfx()` allowance and lowers the
  counted `Gfx()` surface from 157 to 156. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_editor_overlay_context_runtime_boundaries.log`.
- [x] 2026-06-28: Routed UI render-target previews through UI/text pass
  contexts. `UiTextPassInputs` now borrows the runtime `AssetSystem`,
  `IRenderCommandContext`, and `IRenderResourceFactory`; `InGameUI::Draw()`
  uses those contexts to create the preview shader, create/destroy its dynamic
  vertex buffer, bind the preview texture, and submit the preview quad. This
  removes the `UI.cpp` `Gfx()` and `CreateShaderFromActiveAssets()` allowances,
  lowers the counted `Gfx()` surface from 156 to 140, and lowers the counted
  shader-factory surface from 13 to 12. Evidence: `python
  tools\check_runtime_boundaries.py` passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_ui_preview_context_runtime_boundaries.log`.
- [x] 2026-06-28: Routed the UI profiler draw-call trace through the UI/text
  pass's borrowed render diagnostics facet. `InGameUIFrameData` now carries the
  `DrawCallTraceSnapshot`, `InGameUI` keeps a bounded owned copy for
  input/layout after the pass returns, and `UITabProfiler.cpp` no longer checks
  `IsGfxReady()` or calls `Gfx()` for trace rows. This removes the
  `UITabProfiler.cpp` `Gfx()` allowance and lowers the counted `Gfx()` surface
  from 140 to 139. Evidence: `python tools\check_runtime_boundaries.py` passed
  with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_ui_profiler_trace_context_runtime_boundaries.log`.
  Targeted compile evidence: `tools\validate_build.bat Profile` passed with
  0 warnings and 0 errors after fixing the teardown/resource-context fallout;
  log:
  `TestOutput\validation\agent_logs\global_service_ui_profiler_trace_profile_build_rerun.log`.
- [x] 2026-06-28: Comment-style audit inspected the source-bearing files touched
  in the current global-service slice batch: `TextureCollection.h/.cpp`,
  `SkyBox.h/.cpp`, `CameraCollection.h/.cpp`, `RunState.h`, `Run.h`,
  `Run.cpp`, `RunRender.cpp`, `Init.cpp`, `Input.cpp`, `LauncherLaser.h/.cpp`,
  `RuntimeRenderHost.cpp`, `RunEditorPlacementAssets.inl`,
  `UIEditorMiniPalette.inl`, and `tools/check_runtime_boundaries.py`. The audit
  fixed stale `SkyBox` singleton wording and added local lifetime/asset-borrow
  comments for the `Run` window borrow and launcher-laser shader creation.
  Evidence: `git diff --check` passed and `python tools\check_runtime_boundaries.py`
  passed with 0 errors; log:
  `TestOutput\validation\agent_logs\global_service_post_comment_audit_runtime_boundaries.log`.
- [x] 2026-06-28: Comment-style follow-up inspected the additional
  launcher-laser context files touched after the first audit:
  `LauncherLaser.h/.cpp`, `RuntimeRenderHost.h`, `RuntimeRenderPasses.h`,
  `RuntimeRenderer.h`, `RuntimeTools.h`, `RunEditorTools.cpp`,
  `RunEditorTracer.inl`, `RunPasses.cpp`, `RunRender.cpp`, `Run.cpp`, and
  `tools/check_runtime_boundaries.py`. The follow-up added lifetime comments for
  explicit laser backend teardown, the overlay callback's borrowed render
  contexts, and transient tracer command submission. Evidence: `git diff
  --check` passed and `python tools\check_runtime_boundaries.py` passed with 0
  errors; log:
  `TestOutput\validation\agent_logs\global_service_editor_overlay_context_runtime_boundaries.log`.
- [x] 2026-06-28: Comment-style follow-up inspected the UI preview context
  files: `UI.h/.cpp`, `RuntimeRenderPasses.h`, `RuntimeRenderer.h`,
  `RunUiTextPass.cpp`, `RunFrame.cpp`, `RunRender.cpp`, `Run.cpp`,
  `UITabProfiler.h/.cpp`, and `tools/check_runtime_boundaries.py`. The
  follow-up added lifetime/hazard comments for UI preview resource creation,
  teardown, and the owned UI-side draw-trace snapshot copy. Evidence: `git diff
  --check` passed, `python tools\check_runtime_boundaries.py` passed with 0
  errors, and `tools\validate_build.bat Profile` passed with 0 warnings and
  0 errors; logs:
  `TestOutput\validation\agent_logs\global_service_ui_preview_context_runtime_boundaries.log`,
  `TestOutput\validation\agent_logs\global_service_ui_profiler_trace_compilefix_runtime_boundaries.log`,
  `TestOutput\validation\agent_logs\global_service_ui_profiler_trace_profile_build_rerun.log`.
- [x] 2026-06-28: Added a counted global-service access ratchet to
  `tools\check_runtime_boundaries.py`. The guardrail ignores comments and string
  literals, preserves the current compatibility surface as counted debt, and
  fails on new or grown calls to `Cfg()`, `Gfx()`, `GfxRayTracing()`,
  `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, named service
  singletons, generic non-named `Class::Instance()` access, `pInstance`
  singleton storage, and mutable `g_*` process globals. This is a new-code
  ratchet only; per-site bootstrap/shutdown/OS-bridge classification and source
  migration remain open. Validation: `python tools\check_runtime_boundaries.py`
  passed in 4.81s
  (`TestOutput\validation\agent_logs\global_service_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 18.01s
  (`TestOutput\validation\agent_logs\global_service_guardrail_validate_fast.log`).
  Comment-style audit: inspected the touched tool script
  `tools\check_runtime_boundaries.py` against
  `Agentic\Reference\comment-style-guide.md`; the learning header remains
  present, and the new ratchet/allowlist comments state the invariant and
  limits of the guardrail.
- [x] 2026-06-28: Extended runtime render services so renderer-facing frame code
  can borrow an explicit render command capability instead of reaching through
  `Gfx()` for these frame operations.
  `RuntimeRenderer::RenderFrame()` now clears through the borrowed command
  context and checks the captured render-ready flag; `RunRender.cpp` direct
  `Gfx()` calls fell from 2 to 1, and the guardrail allowlist was lowered to
  that new count. Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the new context
  fields have a `Lifetime:` note for the borrowed command facet and no ownership
  moved out of `Run`.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.69s
  (`TestOutput\validation\agent_logs\runtime_render_services_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 23.60s
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.81s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.30s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_full.log`).
  Rubber-duck review: McClintock found no code blockers after the context was
  narrowed to the command capability. The review's stale-validation blocker was
  resolved by rerunning DX12 and full validation after the final source edit,
  and the broad borrowed-context checklist item remains open for future
  non-render contexts.
- [x] 2026-06-28: Threaded the borrowed render command capability into
  `RenderFrameContext` and switched the runtime render-pass texture-slot hygiene
  helpers away from direct `Gfx()` access. This lowers `RunPasses.cpp` direct
  `Gfx()` debt from 98 to 96 and the plan's total counted `Gfx()` surface from
  293 to 291. The broader render-pass/global-service migration remains open
  because the same file still has many compatibility-facade calls for viewport,
  clear, depth/blend/cull, draw state, resource creation, and DXR decisions.
  Comment-style audit: inspected `RuntimeRenderPasses.h`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the command pointer
  is lifetime-annotated and asserted before helper use.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\render_frame_command_context_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.35s
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.01s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.25s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_full.log`).
  Rubber-duck review: Hubble found no blockers; the reviewer called out the
  nullable/assert-only command pointer and counted-ratchet limits as residual
  risks to keep visible in later slices.
- [x] 2026-06-28: Routed generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 96 to 88
  and the plan's total counted `Gfx()` surface from 291 to 283. The broad
  normal-path render-pass migration remains open because other state,
  viewport, clear, resource, dynamic geometry, and DXR paths still reach the
  compatibility facade.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.81s
  (`TestOutput\validation\agent_logs\sky_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.45s
  (`TestOutput\validation\agent_logs\sky_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.68s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\sky_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.45s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\sky_command_state_validate_full.log`).
  Rubber-duck review: Hilbert found no blockers. Non-blocking note: the sky
  pass still restores depth write from the depth-test snapshot, which matches
  the pre-existing behavior but remains state-contract debt for a later command
  state cleanup.
- [x] 2026-06-28: Routed `WaterPass::Render()` depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 88 to 76
  and the plan's total counted `Gfx()` surface from 283 to 271. The broad
  render-pass/global-service migration remains open for shadow, reflection,
  tornado, volumetric, tonemap, viewport, clear, resource creation, and DXR
  paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.84s
  (`TestOutput\validation\agent_logs\water_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.55s
  (`TestOutput\validation\agent_logs\water_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.55s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\water_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.22s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\water_command_state_validate_full.log`).
  Rubber-duck review: Dewey found no blockers. Non-blocking note: the
  `Gfx()` guardrail remains a ceiling ratchet, not semantic same-file
  classification, so future slices still need focused review.
- [x] 2026-06-28: Routed `TornadoVisualPass::Render()` depth/blend/cull state
  and transient colored-triangle draw through `RenderFrameContext`'s borrowed
  `IRenderCommandContext` instead of direct `Gfx()` calls. This lowers
  `RunPasses.cpp` direct `Gfx()` debt from 76 to 60 and the plan's total
  counted `Gfx()` surface from 271 to 255. The broad render-pass/global-service
  migration remains open for shadow, reflection, volumetric, tonemap, viewport,
  clear, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.72s
  (`TestOutput\validation\agent_logs\tornado_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.34s
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.58s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.89s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_full.log`).
  Rubber-duck review: Hume found no blockers. Non-blocking note: the DX12
  validation suite is broad renderer coverage rather than tornado-scene-specific
  visual proof, so this slice is recorded as a command-context/global-access
  ratchet with full renderer safety-net evidence.
- [x] 2026-06-28: Routed `ShadowPass::RenderShadowMap()` viewport/clear,
  depth/blend/cull state, polygon offset, and final viewport restore through
  its existing `IRenderCommandContext` argument instead of direct `Gfx()` calls.
  This lowers `RunPasses.cpp` direct `Gfx()` debt from 60 to 44 and the plan's
  total counted `Gfx()` surface from 255 to 239. The broad
  render-pass/global-service migration remains open for shadow target creation,
  reflection, volumetric, tonemap, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current shadow-map state restore contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\shadow_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.36s
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.02s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.80s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_full.log`).
  Rubber-duck review: Huygens found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy shadow state restore semantics rather than
  fixing depth-write/cull restore debt, the viewport restore relies on window
  and backend dimensions staying in lockstep, and the DX12 validation suite is
  broad renderer coverage rather than a shadow-scene-specific visual proof.
- [x] 2026-06-28: Routed `VolumetricPass::Render()` viewport and depth/blend
  state through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead
  of direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from
  44 to 32 and the plan's total counted `Gfx()` surface from 239 to 227. The
  broad render-pass/global-service migration remains open for the fullscreen
  quad helper, resource creation, reflection, tonemap, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.89s
  (`TestOutput\validation\agent_logs\volumetric_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.08s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.87s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_full.log`).
  Rubber-duck review: Meitner found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  volumetric-scene-specific visual proof.
- [x] 2026-06-28: Routed `TonemapPass::Render()` viewport and depth/blend state
  through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of
  direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 32
  to 21 and the plan's total counted `Gfx()` surface from 227 to 216. The broad
  render-pass/global-service migration remains open for the fullscreen quad
  helper, resource creation, reflection, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.00s
  (`TestOutput\validation\agent_logs\tonemap_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.77s
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.39s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.77s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_full.log`).
  Rubber-duck review: Gibbs found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  tonemap-scene-specific visual proof.
- [x] 2026-06-28: Routed `SceneTargetPass::Begin()` HDR target viewport and
  clear calls through `RenderFrameContext`'s borrowed `IRenderCommandContext`
  instead of direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()`
  debt from 21 to 19 and the plan's total counted `Gfx()` surface from 216 to
  214. The broad render-pass/global-service migration remains open for the
  fullscreen quad helper, resource creation, reflection, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current HDR scene-target invariant.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.98s
  (`TestOutput\validation\agent_logs\scene_target_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.44s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.01s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_full.log`).
  Rubber-duck review: Raman found no blockers. Non-blocking note: the `Gfx()`
  guardrail remains a count ratchet rather than semantic per-call tracking, so
  future slices still need focused diff review.
- [x] 2026-06-28: Routed the shared `DrawFullscreenQuad()` dynamic-geometry draw
  helper through an explicit borrowed `IRenderCommandContext&` instead of
  direct `Gfx()` access. This lowers `RunPasses.cpp` direct `Gfx()` debt from 19
  to 18 and the plan's total counted `Gfx()` surface from 214 to 213. The broad
  render-pass/global-service migration remains open for fullscreen quad
  resource creation/destruction, framebuffer creation, reflection, and DXR
  paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses existing borrowed command
  contexts at the cinematic sky, volumetric-light, and tonemap call sites.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.85s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.99s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.26s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.49s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_full.log`);
  `tools\validate_perf.bat` completed in 22.48s with exit 0
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_perf.log`).
  Perf review note: the DX12 comparison was skipped for machine mismatch, and
  the script reported physics-bench warnings outside this render-helper slice.
  Rubber-duck review: Bohr found no blockers. Non-blocking notes: perf evidence
  is advisory/noisy on this machine, and the `Gfx()` guardrail remains
  count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Routed planar reflection viewport, clear, clip-plane
  enable/disable, and final viewport restore through `RenderFrameContext`'s
  borrowed `IRenderCommandContext` instead of direct `Gfx()` calls. This lowers
  `RunPasses.cpp` direct `Gfx()` debt from 18 to 13 and the plan's total
  counted `Gfx()` surface from 213 to 208. The broad render-pass/global-service
  migration remains open for reflection capability queries, DXR dispatch,
  framebuffer creation, size queries, and fullscreen quad resource lifetime.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current planar reflection state contract.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.11s
  (`TestOutput\validation\agent_logs\reflection_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.90s
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.38s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.97s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_full.log`).
  Rubber-duck review: Nietzsche found no blockers and verified no stale
  allowlist mismatch: actual total `Gfx()` count 208, allowlist total 208, and
  `RunPasses.cpp` actual/allowlist 13/13. Non-blocking note: the `Gfx()`
  guardrail remains count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Routed the remaining `RunPasses.cpp` direct `Gfx()` access
  through borrowed render services: framebuffer and fullscreen dynamic
  vertex-buffer lifetime now use `IRenderResourceFactory`, reflection capability
  checks use `IRenderDiagnostics`, and render-target sizes come from sampled
  runtime window dimensions. The teardown path passes a nullable resource
  factory from the composition root so resource handles reset safely even after
  failed backend initialization. This lowers `RunPasses.cpp` direct `Gfx()` debt
  from 13 to 0 and the plan's total counted `Gfx()` surface from 208 to 196.
  `GfxRayTracing()` remains open as a separate narrow DXR capability accessor.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `Run.cpp`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; all touched
  source-bearing files keep learning headers, and new comments document borrowed
  service lifetime, frame-only diagnostics, and shutdown-without-backend
  behavior.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.87s
  (`TestOutput\validation\agent_logs\render_resource_services_runtime_boundaries_rerun.log`);
  `tools\validate_fast.bat` passed in 104.57s
  (`TestOutput\validation\agent_logs\render_resource_services_validate_fast_final.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.71s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_resource_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.13s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_resource_services_validate_full.log`).
  Rubber-duck review: an in-session rubber-duck pass found one blocking teardown
  hazard where backend resource release could have called `Gfx()` when no
  backend was live; the nullable factory handoff fixed it before validation.
  Residual non-blocking notes: the guardrail is still a counted ratchet rather
  than a semantic bootstrap/normal-path classifier, and broader service
  classification remains open.
- [x] 2026-06-28: Split runtime render-pass resource creation onto a dedicated
  `RenderResourceContext` so `RenderFrameContext` no longer carries the
  resource-factory service through ordinary draw methods. This does not lower
  the counted global-service surface because the previous slice already removed
  `RunPasses.cpp` direct `Gfx()` calls, but it narrows when the borrowed factory
  is visible: `RuntimeRenderer` builds the resource context for
  `EnsureGpuResources()` calls and ensures shadow targets before
  `ShadowPass::Render()`. Broader service classification remains open because
  `RuntimeRenderServices` still carries the factory to the renderer owner.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `RunPasses.cpp`, and
  `RunRender.cpp`; learning headers remain present, and the new resource
  context is named in local glossaries and lifetime comments.
  Validation:
  focused `tools\validate_build.bat Profile` passed in 48.25s with 0 warnings
  and 0 errors
  (`TestOutput\validation\agent_logs\render_resource_context_validate_build_profile.log`);
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.73s
  (`TestOutput\validation\agent_logs\render_resource_context_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed on rerun in 101.63s after scoped header
  alignment fixes
  (`TestOutput\validation\agent_logs\render_resource_context_validate_fast_rerun.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.73s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_resource_context_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.52s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_resource_context_validate_full.log`);
  `tools\validate_perf.bat` completed in 22.13s
  (`TestOutput\validation\agent_logs\render_resource_context_validate_perf.log`).
  Perf warnings are recorded: DX12 perf comparison was skipped due a
  machine-label mismatch, and physics_bench reported 9 warning failures
  including `Frame.avg +55.4%` and `Frame/Input.avg +72.5%`; this slice did not
  touch physics/input code, but the warning-bearing output is not claimed as a
  clean perf pass.
- [x] 2026-06-28: Routed the remaining `RunPasses.cpp` direct
  `GfxRayTracing()` access through a borrowed nullable `IRenderRayTracing*`
  supplied by `Run::Render()`. This keeps normal reflection pass code on the
  explicit frame context while preserving the composition root as the place that
  samples global raytracing readiness. The counted global-service surface is
  unchanged: `GfxRayTracing()` remains at 5 because this slice moved the call
  from pass code to `RunRender.cpp` rather than deleting the process-global
  accessor. Other `GfxRayTracing()` callers and broader per-site global
  classification remain open.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RunPasses.cpp`, `RunRender.cpp`, and
  `tools\check_runtime_boundaries.py`; touched files retain learning headers,
  and the new DXR comments include local glossary definitions and lifetime
  notes.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.87s
  (`TestOutput\validation\agent_logs\render_dxr_capability_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 103.96s
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.64s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.76s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_full.log`).
  Rubber-duck review found no blockers; residual debt is the unchanged global
  count and remaining non-composition `GfxRayTracing()` callers.
- [x] 2026-06-28: Narrowed the frame loop's renderer service lifetime by
  sampling `Gfx()` once per frame turn and using borrowed
  `IRenderDiagnostics` and `IRenderDeviceLifecycle` facets for draw-call reset,
  optional pipeline drain, UI draw-call accounting, and present. This lowers
  `RunFrame.cpp` direct `Gfx()` debt from 4 to 1 and the plan's total counted
  `Gfx()` surface from 193 to 190 while keeping the composition-root-style
  frame loop as the only service sampling point for these frame operations.
  The checker allowlist now ratchets `RunFrame.cpp` to one renderer-service
  sample. Comment-style audit: inspected `RunFrame.cpp` and
  `tools\check_runtime_boundaries.py`; `RunFrame.cpp` retains its learning
  header and the new lifetime comment names the borrowed renderer facets.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.79s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_runtime_boundaries.log`);
  `tools\validate_format.bat` passed in 7.22s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_validate_format.log`);
  `git diff --check` passed in 0.13s
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_diff_check.log`);
  `tools\validate_full.bat` passed in 36.16s, including DX12 validation with 0
  InfoQueue errors, matching screenshots, standalone physics smoke, and
  byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\frame_render_service_lifetime_validate_full.log`).
  No rubber-duck review was run for this slice per current user instruction.

## Current Counted Global-Service Surface

This snapshot is the guardrail allowlist as of 2026-06-29. Counts are taken from
source-bearing files under `SkullbonezSource/` after stripping comments and
string literals.

| Pattern | Current count |
|---------|---------------|
| `Cfg()` | 0 |
| `Gfx()` | 4 |
| `GfxRayTracing()` | 3 |
| `ActiveAssetSystem()` | 0 |
| `CreateShaderFromActiveAssets()` | 0 |
| `TextureCollection::Instance()` | 0 |
| `CameraCollection::Instance()` | 0 |
| `Window::Instance()` | 4 |
| `SkyBox::Instance()` | 0 |
| `WorkerPool::Instance()` | 3 |
| `Profiler::Instance()` | 15 |
| Generic non-named `Class::Instance()` | 7 |
| `pInstance` | 5 |
| Mutable `g_*` process global | 81 |

## Current Counted Global-Service Allowlist Classification

Grouped by source file. Each `label=count` entry corresponds to one counted
allowlist row in `tools\check_runtime_boundaries.py`; the classification names
why that current debt exists and which migration bucket should own it.

| File | Counted labels | Classification | Owner / migration note |
|------|----------------|----------------|------------------------|
| `SkullbonezSource/Core/Config.cpp` | `EngineConfig::Instance()=1` | bootstrap | Config owner singleton implementation. |
| `SkullbonezSource/Core/LockOrderValidator.cpp` | `LockOrderValidator::Instance()=5`, `g_*=12` | diagnostics | Global lock-order diagnostics state. |
| `SkullbonezSource/Core/PlatformProfiler.cpp` | `g_*=12` | diagnostics | Platform profiler marker bridge and process-local telemetry. |
| `SkullbonezSource/Core/Profiler.cpp` | `Profiler::Instance()=2` | diagnostics | Profiler GPU markers and timer queries now borrow the renderer diagnostics facet from `Run`; profiler singleton sampling remains. |
| `SkullbonezSource/Core/Profiler.h` | `Profiler::Instance()=11` | diagnostics | Profiler accessor surface. |
| `SkullbonezSource/Core/WorkerPool.cpp` | `WorkerPool::Instance()=2`, `g_*=8` | service owner | Worker service singleton implementation, self-test helper, and queue state; normal runtime/render callers now borrow the service from `Run`/`RuntimeRenderHost`. |
| `SkullbonezSource/Rendering/IRenderBackend.cpp` | `Gfx()=2`, `GfxRayTracing()=1` | render pass | Backend facade compatibility; capability interfaces should replace wide access. |
| `SkullbonezSource/Rendering/IRenderBackend.h` | `Gfx()=1`, `GfxRayTracing()=1` | render pass | Backend facade header still declares compatibility accessors; draw-call trace scope now receives a borrowed diagnostics facet. |
| `SkullbonezSource/Runtime/Init.cpp` | `EngineConfig::Instance()=2`, `Window::Instance()=1`, `WorkerPool::Instance()=1`, `g_*=6` | bootstrap | Startup command-line/config/window/worker binding surface; worker pool is sampled once, then passed through the runtime ownership graph. |
| `SkullbonezSource/Runtime/Input.cpp` | `g_*=43` | OS callback bridge | Win32 input accumulators, automation override, cursor policy, and focus/window bridge now use the bound callback HWND instead of `Window::Instance()`. |
| `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` | `Profiler::Instance()=2` | diagnostics owner | Runtime diagnostics boundary exposes the borrowed profiler view used by Run, perf CSV logging, UI overlays, and profiler-tab data. |
| `SkullbonezSource/Runtime/Run.cpp` | `Gfx()=1`, `GfxRayTracing()=1` | bootstrap | Composition-root compatibility; shutdown/resource-release/logging now use cached borrows, and startup still carries the direct renderer and raytracing samples. |
| `SkullbonezSource/Runtime/Window.cpp` | `Window::Instance()=3`, `pInstance=4` | OS callback bridge | Window singleton and message integration bridge; resize now uses the renderer borrow bound by `Init`, while projection and startup sizing use the config bound by `Init`. |
| `SkullbonezSource/Runtime/Window.h` | `pInstance=1` | OS callback bridge | Window singleton storage declaration. |
## Existing Service Lifetime Owners

This 2026-06-28 inventory is the preferred reuse surface before adding any new
service context. The important split is ownership versus borrowing: `Run` owns
most process-lifetime systems, while contexts such as `EngineContext` and
`RuntimeRenderHost` should remain borrowed views over those owners.

| Owner/view | Owns lifetime? | Current service boundary | Use before adding |
|------------|----------------|--------------------------|-------------------|
| `Run` (`SkullbonezSource/Runtime/Run.h`) | Yes | Process composition root for scene, diagnostics, systems, input, interaction, simulation, replay, UI, tools, world, model collection, command queue, render host, and renderer. | Top-level bind order, shutdown order, and remaining composition-root sampling of globals such as renderer capabilities. |
| `EngineContext` (`SkullbonezSource/Runtime/EngineContext.h`) | No, borrowed view | Binds pointers to `SceneController`, `SimulationController`, capture/diagnostics controllers, command queue, systems, runtime settings, input, camera/debug state, world, and models. | Runtime extraction slices that need a declared boundary without directly reaching through `Run` members. |
| `RuntimeRenderHost` (`SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`) | Owns render scratch only; borrows services | Groups render-facing runtime, worker, world, scene, replay overlay, tool overlay, UI, and diagnostics views; owns DXR reflection transform scratch. | Render-pass dependencies that would otherwise call `Run`, `Gfx()`, or unrelated singleton services. |
| `RuntimeTools` (`SkullbonezSource/Runtime/Tools/RuntimeTools.h`) | Yes, for transient tool state | Owns launcher/raycast state, launcher laser, mouse pickup state, editor placement state, and editor tracer feedback. | Launcher, editor, mouse-pickup, and transient overlay state before adding more tool fields to `Run`. |
| `DiagnosticsRuntime` (`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`) | Yes | Owns capture controller, diagnostics controller, perf log state, main-memory dump path/cache, UI stress state, and Debug-only physics diagnostics helpers. | Capture, perf, SkullScope, memory dump, and validation artifact state before reaching through global diagnostics or ad hoc file paths. |
| `SceneController` (`SkullbonezSource/Runtime/Scene/SceneController.h`) | Yes | Owns scene runtime, scene queue/index bookkeeping, browser state, and scene UI overrides. | Scene queue/runtime state and scene UI policy before adding scene fields to `Run` or accessing globals from scene code. |
| `RuntimeRenderer` (`SkullbonezSource/Runtime/Render/RuntimeRenderer.h`) | Yes | Owns runtime render pass objects and frame render ordering. | Render pass sequencing and pass-owned resources before growing `Run::Render()` as a service locator. |
| `RuntimeCommandQueue` (`SkullbonezSource/Runtime/RuntimeCommandQueue.h`) | Yes | Owns deferred runtime/tool command intent drained at frame boundaries. | Cross-system UI/tool/runtime mutations before adding process globals or synchronous callback state. |
| `SimulationController` (`SkullbonezSource/Runtime/SimulationController.h`) | Yes | Owns fixed-step accumulator and simulation tick policy. | Simulation timing and fixed-step state before passing raw timing globals through physics/runtime callers. |

Rubber-duck review: Banach found no blockers and confirmed that all six
checklist owners are present. Non-blocking clarity feedback narrowed the
`RuntimeRenderHost` lifetime wording so the table cannot be read as ownership
of borrowed runtime services.

## Problem Statement

The Carmack-test verdict flagged remaining globals and singletons as a serious
encapsulation risk. Some globals are legitimate OS callback bridges, but normal
runtime/render/asset/scene paths still use process-global service access where
explicit lifetime and ownership would be easier to reason about.

## Goal

Replace normal-path global service access with explicit service contexts,
borrowed interfaces, or composition-root wiring. Keep unavoidable OS callback
bridges tiny, named, and fenced.

## Success Bar

- New runtime, render, physics, scene, asset, UI, and diagnostics code does not
  call process-global service accessors.
- Existing global access is either removed, isolated to bootstrap/shutdown, or
  documented as an OS callback bridge.
- Service lifetime order is explicit in `Run`, `EngineContext`, or narrower
  subsystem contexts.
- Guardrails reject new normal-path global access.

## Related Plans

- `Agentic/Plans/Done/global-service-context-plan.md` has been archived as a
  superseded umbrella roadmap, not implementation-completion evidence. Use this
  Carmack plan as the active acceptance checklist for removing normal-path
  global service access.
- `Agentic/Plans/Done/carmack-render-backend-capability-plan.md` owns renderer
  capability access and `Gfx()` migration in renderer-facing code. This plan
  owns the broader service-lifetime and callback-bridge rules.
- `Agentic/Plans/runtime-static-allocation-policy-plan.md` owns dynamic
  allocation policy for any new context storage introduced during this cleanup.

## Implementation Checklist

### Inventory

- [x] Run `rg "Gfx\\(|GfxRayTracing\\(|Cfg\\(|ActiveAssetSystem\\(|CreateShaderFromActiveAssets\\(|::Instance\\(|pInstance|g_[A-Za-z_]" SkullbonezSource`.
- [x] Classify each current counted allowlist row as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`.
  - [x] 2026-06-28 current global-service classification table groups every
    counted allowlist row by source file and assigns each row to one migration
    bucket.
- [x] Classify each individual source hit as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`.
  - [x] 2026-06-29 source-hit classification reran the inventory command and
    reviewed every returned line. Meaningful remaining hits are classified here:
    `Core/Config.cpp` `EngineConfig::Instance()` is the config singleton owner;
    `Runtime/Init.cpp` uses `EngineConfig::Instance()`, `WorkerPool::Instance()`,
    and `Window::Instance()` only in bootstrap/init flow; `Runtime/Run.cpp`
    samples `Gfx()`/`GfxRayTracing()` during `Run::Initialise()`; the
    `Rendering/IRenderBackend.*` hits are backend facade compatibility
    declarations/definitions; `Core/Profiler.*` hits are profiler diagnostics
    internals/macros, and `Runtime/Diagnostics/DiagnosticsRuntime.cpp` is the
    runtime-facing profiler borrow owner;
    `Core/PlatformProfiler.cpp`, `Core/LockOrderValidator.cpp`, and
    `Core/WorkerPool.cpp` hits are core diagnostic/service owner globals or
    thread-local state; `Runtime/Input.cpp` hits are the named Win32 input
    callback/automation bridge; `Runtime/Window.*` hits are the OS window
    singleton bridge and owner pointer. Reviewed false positives are
    comment-only policy mentions plus `g_` substrings in config keys, metric
    names, asset labels, scene/body names, debug strings, render-graph labels,
    DX12 diagnostic strings, and math comments.
- [x] Record the current allowlist in this plan before changing source.
- [x] Identify service lifetime owners already available in `Run`,
  `EngineContext`, `RuntimeRenderHost`, `RuntimeTools`, `DiagnosticsRuntime`, and
  `SceneController`.

### Service Context Shape

- [x] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared.
  - [x] 2026-06-28 `EngineContextBindings` names the borrowed process-service
    graph (`SceneController`, simulation, diagnostics/capture, command queue,
    subsystem state, runtime settings/input, camera/debug, world, and models),
    and `Run::BindEngineContext()` binds it once from Run-owned storage.
- [x] Define or extend a `RenderServices`/`RenderContext` for renderer-facing
  services instead of direct `Gfx()` calls.
- [x] Define or extend an `AssetContext` for asset lookup and source records
  instead of `ActiveAssetSystem()`.
  - [x] 2026-06-28 runtime scene/style parsing, editor placement, skybox, and
    launcher laser paths borrow the Run-owned `AssetSystem`; `ActiveAssetSystem()`
    now remains only as the compatibility helper declaration/definition.
- [x] Define or extend an `InputEventBuffer` or input bridge for Win32 callback
  accumulators.
  - [x] 2026-06-28 `Input::BindCallbackBridge()`'s bound HWND now also owns
    focus checks and mouse client/centering conversions for the static input
    API.
- [x] Define or extend a `WindowService` or explicit window reference for window
  queries and title/resize behavior.
  - [x] 2026-06-28 `Run` now receives an explicit borrowed `Window` from
    bootstrap startup, and `Input` static helpers use the bound callback HWND
    for focus, client mouse conversion, and cursor centering.
- [x] Keep contexts borrowed and lifetime-annotated; do not create a new global
  service locator under a nicer name.
  - [x] 2026-06-28 current context surfaces (`EngineContext`,
    `RuntimeRenderInputs`, `RunSubsystemState`, editor asset contexts, and the
    input HWND bridge) use borrowed references/pointers and local lifetime notes;
    no new process-global service locator was added in this slice batch.

### Remove Normal-Path Globals

- [x] Route render pass backend access through render capability/context
  arguments.
  - [x] 2026-06-29 all listed render-pass backend-access child slices are
    complete, and raw source search finds no `Gfx()`, `GfxRayTracing()`, or
    `IsGfxReady()` in `RunPasses.cpp`.
- [x] Route render pass texture-slot hygiene helpers through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route water depth/blend state through `RenderFrameContext`'s borrowed
  command context instead of direct `Gfx()` calls.
- [x] Route tornado visual depth/blend/cull state and transient triangle draw
  through `RenderFrameContext`'s borrowed command context instead of direct
  `Gfx()` calls.
- [x] Route shadow-map viewport/clear/depth/blend/cull/polygon-offset state
  through its existing command context argument instead of direct `Gfx()` calls.
- [x] Route volumetric pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route tonemap pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route HDR scene-target viewport/clear through `RenderFrameContext`'s
  borrowed command context instead of direct `Gfx()` calls.
- [x] Route the shared fullscreen quad dynamic draw helper through a borrowed
  command context instead of direct `Gfx()` calls.
- [x] Route planar reflection viewport/clear/clip-plane state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route `RunPasses.cpp` framebuffer creation, fullscreen dynamic
  vertex-buffer lifetime, render-target size queries, and reflection capability
  queries through borrowed render services instead of direct `Gfx()` calls.
- [x] Split runtime pass resource creation/rebuild calls onto
  `RenderResourceContext` so `RenderFrameContext` no longer carries the
  resource-factory service into draw methods.
- [x] Route reflection-pass DXR access through a borrowed nullable
  `IRenderRayTracing*` in `RenderFrameContext` instead of direct
  `GfxRayTracing()` access in `RunPasses.cpp`.
- [x] Route frame-loop draw-call reset, optional pipeline drain, UI draw-call
  accounting, and present through one frame-level renderer borrow plus narrow
  diagnostics/lifecycle facets instead of repeated direct `Gfx()` calls.
- [x] Route launcher laser transient geometry creation, teardown, state changes,
  and draw submission through the debug overlay pass's borrowed render command
  and resource-factory contexts instead of direct `Gfx()` calls.
- [x] Route editor overlay tracer line submission through the debug overlay
  pass's borrowed render command context instead of direct `Gfx()` calls.
- [x] Route UI render-target preview draw state, texture binding, dynamic
  vertex-buffer lifetime, and preview quad submission through the UI/text pass's
  borrowed render command/resource contexts instead of direct `Gfx()` calls.
- [x] Route UI profiler draw-call trace rows through the UI/text pass's borrowed
  render diagnostics snapshot instead of direct `Gfx()` access.
- [x] Route shadow receiver texture binding and clearing through the terrain
  pass's borrowed command context instead of direct `Gfx()` calls in `Shadow.h`.
- [x] Route physics debug line drawing and tornado vector drawing through the
  debug overlay pass's borrowed command context instead of direct `Gfx()` calls.
- [x] Route collision visualizer mesh/dynamic-buffer lifetime, instance uploads,
  draw submission, blend/depth state, and teardown through borrowed render
  resource and command contexts instead of direct `Gfx()` calls.
- [x] Route shader and texture creation through an asset/render context passed
  from runtime-owned services.
  - [x] 2026-06-28 skybox resource rebuild now receives the runtime
    `AssetSystem`, `TextureCollection`, and renderer resource-factory facet
    instead of reacquiring shader, texture, and renderer globals.
  - [x] 2026-06-28 launcher laser shader creation now borrows
    `m_systems.assets` from the render callback instead of calling
    `CreateShaderFromActiveAssets()`.
  - [x] 2026-06-28 UI render-target preview shader creation now borrows
    `m_systems.assets` through `UiTextPassInputs` instead of calling
    `CreateShaderFromActiveAssets()`.
  - [x] 2026-06-29 collision visualizer, shared primitive helper, Text2d,
    terrain, and water shader creation now borrow `AssetSystem` from their
    render pass or render host.
  - [x] 2026-06-29 `AssetSystem::CreateShader()` now receives the active
    `IRenderResourceFactory`, all shader callers pass their existing resource
    context, and the transitional `ActiveAssetSystem()` /
    `CreateShaderFromActiveAssets()` bridge has been removed.
  - [x] 2026-06-29 source-asset texture rebuilds, JPEG uploads, texture
    deletion, skybox texture loading, and draw-time texture binding now receive
    explicit render resource/command contexts. Raw source search finds no
    `Gfx()` calls in `TextureCollection.cpp`.
- [x] Replace `ActiveAssetSystem()` in scene parsing and editor tools with an
  explicit asset context.
  - [x] 2026-06-28 scene/style parser calls now accept an explicit borrowed
    `AssetSystem` from runtime-owned services.
  - [x] 2026-06-28 editor placement preview, tracing, preflight, and commit now
    borrow `AssetSystem` from runtime-owned services.
- [x] Replace `TextureCollection::Instance()` normal-path lookups with runtime
  owned texture service references.
  - [x] 2026-06-28 `RunSubsystemState` now owns `TextureCollection` directly;
    runtime render services borrow it by reference and no source file calls
    `TextureCollection::Instance()`.
- [x] Replace `CameraCollection::Instance()` normal-path lookups with explicit
  camera service references.
  - [x] 2026-06-28 authored/generated scene setup now uses the
    `SceneAuthoredCameraContext` / `SceneGeneratedCameraContext` camera service
    supplied by `RunScene.cpp`, rather than reacquiring the camera singleton.
  - [x] 2026-06-28 `RunSubsystemState` now owns `CameraCollection` directly;
    existing split runtime files borrow `m_systems.cameras`, and no source file
    calls `CameraCollection::Instance()` or stores `CameraCollection::pInstance`.
- [x] Replace `Window::Instance()` normal-path lookups with explicit window
  service references after bootstrap.
  - [x] 2026-06-28 `RunApp()` now passes the bootstrap-created `Window` to
    `Run`, so `Run::Initialise()` binds the existing window reference instead of
    calling `Window::Instance()` in the normal runtime path.
- [x] Replace `SkyBox::Instance()` with runtime/world-owned skybox lifetime or a
  scene-render resource owner.
  - [x] 2026-06-28 `RunSubsystemState` now owns `std::unique_ptr<SkyBox>`;
    `SkyBox` has no `pInstance` storage and no static `Instance()`/`Destroy()`
    API.
- [x] Keep config reads grouped through launch/runtime config context where
  possible; do not spread new `Cfg()` calls.
  - [x] 2026-06-28 `RuntimeRenderHost` now borrows the live `EngineConfig`
    from `Run` construction instead of reading `Cfg()` inside render-host
    helper methods.
  - [x] 2026-06-28 repeated config reads in sky rendering, replay-generated
    scene rebuilds, camera update tuning, and replay-control automation are now
    grouped through existing borrowed or local config references.
  - [x] 2026-06-28 volumetric and tonemap shader depth-parameter helpers now
    receive `EngineConfig` from the render host instead of reading `Cfg()`
    directly.
  - [x] 2026-06-28 skybox texture-path and overflow reads now use the
    `EngineConfig` borrowed from `Run::Initialise()` instead of calling `Cfg()`
    inside `SkyBox`.
  - [x] 2026-06-28 `Run::Run()` startup defaults now use one local
    `EngineConfig` borrow instead of repeated `Cfg()` calls in the constructor
    body.
  - [x] 2026-06-28 built-in asset registration now receives the caller's
    `EngineConfig` borrow instead of sampling `Cfg()` inside
    `RegisterBuiltInAssets()`.
  - [x] 2026-06-28 `Run` now stores one borrowed live `EngineConfig&` for
    composition-root startup, initialization, resource rebuild, and Debug
    diagnostics paths instead of reacquiring `Cfg()` in each method.
  - [x] 2026-06-28 remaining `RunRender.cpp` render/camera config reads now
    use `Run`'s borrowed `EngineConfig&` instead of direct `Cfg()` access.
  - [x] 2026-06-28 remaining `RunFrame.cpp` scene-control and camera-tuning
    config reads now use `Run`'s borrowed `EngineConfig&` instead of direct
    `Cfg()` access.
  - [x] 2026-06-28 remaining `RunUiTextPass.cpp` ordinary-render config reads
    now use the render host's borrowed `EngineConfig&` instead of direct
    `Cfg()` access.
  - [x] 2026-06-28 remaining one-call config rows in
    `RunInteractionAutomation.cpp`, `RunLiveStyle.cpp`, and `RunStress.cpp` now
    use `Run`'s borrowed `EngineConfig&` instead of direct `Cfg()` access.
  - [x] 2026-06-28 worker-thread UI tuning now passes `Run`'s borrowed
    `EngineConfig&` to `ApplyWorkerThreadCountOverride()` instead of mutating
    config through direct `Cfg()` access.
  - [x] 2026-06-28 remaining `RunInput.cpp` input, scene-control, tuning,
    save-defaults, and camera-clamp config reads now use `Run`'s borrowed
    `EngineConfig&` instead of direct `Cfg()` access.
  - [x] 2026-06-28 replay cause-tree, scrubber, and velocity-edit window-size
    config reads now use `Run`'s borrowed `EngineConfig&` instead of direct
    `Cfg()` access.
  - [x] 2026-06-28 remaining `RunScene.cpp` scene-load, reset, generated-setup,
    terrain/world, worker, and cinematic config reads now use `Run`'s borrowed
    `EngineConfig&` instead of direct `Cfg()` access.
  - [x] 2026-06-29 camera movement limits, pitch collision caps, and tween
    terrain-height clamps now use the Run-owned camera collection's borrowed
    `EngineConfig&` instead of direct `Cfg()` access.
  - [x] 2026-06-29 launcher repro snapshot terrain-support probing and emitted
    physics config rows now use `LauncherReproSnapshotContext::config` instead
    of direct `Cfg()` access.
  - [x] 2026-06-29 startup config loading, CLI config mutation, worker/window
    startup reads, and `Run` construction now thread explicit `EngineConfig&`
    borrows through `Init`, `RunApp`, and `Run` instead of normal-path direct
    `Cfg()` access.
  - [x] 2026-06-29 text renderer startup projection sizing now receives
    explicit configured dimensions from the UI text pass instead of calling
    `Cfg()` inside `Text.cpp`.
  - [x] 2026-06-29 normal model drawing and primitive batch lighting now receive
    `RuntimeRenderFlags`/`OrdinaryRenderConfig` from the runtime render host
    instead of reading `Cfg()` inside `GameModelRenderer`/`RenderHelper`.
  - [x] 2026-06-29 shadow caster batching and object-shadow bounds now receive
    `shadowParallelPrep` from `ShadowPass` instead of reading `Cfg()` inside
    `GameModelRenderer`.
  - [x] 2026-06-29 window projection rebuild and startup/fullscreen sizing now
    use the `EngineConfig` bound by `Init` instead of reading `Cfg()` inside
    `Window.cpp`.
  - [x] 2026-06-29 `GameModel` now receives an `EngineConfig` borrow from scene,
    editor, ragdoll, and launcher construction paths instead of reading `Cfg()`
    inside normal per-model physics code.
  - [x] 2026-06-29 `BoundingSphere` now stores its construction-time drag
    coefficient instead of reading `Cfg()` from the shape helper.
  - [x] 2026-06-29 `RigidBody` now stores the angular velocity limit seeded by
    `GameModel` construction instead of reading `Cfg()` during spin clamping.
  - [x] 2026-06-29 `WorldEnvironment` now receives live `EngineConfig` during
    runtime/scene world construction and uses that borrow for water style plus
    fluid drag tuning instead of direct `Cfg()` access.
  - [x] 2026-06-29 `Terrain` now receives live `EngineConfig` during runtime
    RAW/flat-slope terrain construction and uses that binding for terrain scale,
    fluid floor, render-step, and ordinary render style.
  - [x] 2026-06-29 `PhysicsWorld` now receives a `PhysicsRuntimeConfig`
    snapshot through `GameModelCollection` -> `PhysicsEngine` ->
    `PhysicsScene`, and `PersistentContactSolver` receives that same snapshot
    through `PersistentContactSolverContext` instead of reading config
    globally.
  - [x] 2026-06-29 active game-model capacity now flows through explicit
    `EngineConfig`, `RunStartupState`, or `GameModelCollection` state instead
    of a no-arg helper that reads config globally.
  - [x] 2026-06-29 the legacy `Cfg()` shim was removed; bootstrap code now uses
    `EngineConfig::Instance()` directly in `Init.cpp`, and no source file calls
    `Cfg()`.

### OS Callback Bridges

- [x] Keep Win32 input globals only behind a tiny bridge if callback signatures
  require process-static state.
  - [x] 2026-06-28 recheck: `Input.cpp`'s remaining process-local state is
    callback-fed wheel/raw mouse accumulation, cursor policy, or interaction
    automation override state. Callback-fed queues are bound to
    `s_callbackBridgeWindow`; `WndProc` ignores late or foreign-window input.
- [x] Add comments naming who samples, resets, and owns each callback
  accumulator.
  - [x] 2026-06-28 `Input.cpp` now names `WndProc` as the wheel/raw mouse
    writer, `UIInput::CaptureSnapshot()`/`InGameUI::UpdateInput()` as the wheel
    consumer, `RunInput` camera mouse-look sampling as the raw-delta consumer,
    `InputController` focus/mouse-look reset helpers as reset paths, and
    separates cursor policy plus `RunInteractionAutomation` overrides from
    callback accumulator state.
- [x] Add an explicit bind/unbind lifecycle for callback bridge state.
  - [x] 2026-06-28 `Input::BindCallbackBridge()` and
    `Input::UnbindCallbackBridge()` now bind callback-fed wheel/raw mouse queues
    to the active `HWND`; WndProc accumulators ignore late or foreign-window
    callbacks.
- [x] Ensure callback bridge teardown cannot leave dangling service pointers.
  - [x] 2026-06-28 `CleanupWindow()` unbinds the input callback bridge while
    `window->m_sWindow` still names the Win32 window used by WndProc and before
    backend/window class teardown.
- [x] Add focused tests or debug assertions for callback bridge lifecycle.
  - [x] 2026-06-28 Debug assertions catch double-bind, unbound unbind,
    wrong-window unbind, and raw-input registration before the bridge is bound.

### Lifetime Order

- [x] Document startup bind order for renderer, assets, textures, window,
  cameras, input, diagnostics, and scene services.
- [x] Document shutdown unbind order and backend resource release order.
- [x] Add assertions that borrowed service pointers are bound before use.
  - [x] 2026-06-28 `EngineContext::Bind()`, both `Bindings()` accessors, and
    `RuntimeViewModelBuilder::Build()` now assert in Debug builds before
    incomplete borrowed runtime service bindings can be used or silently
    converted into a default presentation snapshot.
- [x] Add a local assertion before runtime render-pass texture-slot helpers use
  `RenderFrameContext::renderCommands`.
- [x] Add assertions that services are unbound before destruction when callbacks
  can fire late.
  - [x] 2026-06-28 recheck: `CleanupWindow()` calls
    `Input::UnbindCallbackBridge(window->m_sWindow)` while the Win32 `HWND` is
    still live and before backend/window-class teardown; `Input::UnbindCallbackBridge()`
    asserts the bridge is bound and that the unbound HWND matches the active
    callback bridge before clearing callback-fed queues.
- [x] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers.
  - [x] 2026-06-29 source search found no live `Gfx()`, `GfxRayTracing()`,
    `IsGfxReady()`, `Cfg()`, `ActiveAssetSystem()`,
    `CreateShaderFromActiveAssets()`, service `::Instance()`, `pInstance`, or
    mutable `g_*` access in `Run.h`; the only `Window::Instance()` hit is a
    lifetime comment explaining that normal paths should not reacquire it.

#### Runtime Service Lifetime Order, 2026-06-28

Startup order:

| Order | Owner | Current bind / creation step | Notes |
|-------|-------|------------------------------|-------|
| 1 | `Init.cpp` / process entry | `CoInitializeEx`, command-line parsing, and config checks. | Startup can exit before worker/window/backend creation if arguments are invalid. |
| 2 | `WorkerPool` bootstrap borrow | `WorkerPool& workerPool = WorkerPool::Instance(); workerPool.Initialise(config.workerThreads)`. | Worker self-test runs only after the pool is initialized; self-test mode then shuts the local worker-pool borrow down and exits before window/backend creation. |
| 3 | `Window::Instance()` | `Window::Instance()`, `CreateAppWindow(...)`, and `GetDC(...)`. | Win32 window/device context exist before DX12 backend init. |
| 4 | Renderer backend | `InitRenderBackend(window)` creates `RenderBackendDX12`, calls `backend->Init(...)`, then `SetGfxBackend(...)` and `SetGfxRayTracingBackend(...)`. | `SetGfxBackend` owns the backend; `GfxRayTracing()` is a borrowed alias cleared by `DestroyGfxBackend()`. |
| 5 | Window resize bridge | `window->HandleScreenResize()` after backend bind. | Backend exists when resize handling queries/rebuilds render state. |
| 6 | `Run` scoped runtime | `RunApp(window, args, config, workerPool)` constructs `Run`; `Run` destructor runs before backend/window teardown. | `Init.cpp` explicitly scopes `Run` so runtime render resources release while DX12 is alive; `Run` borrows the already-initialized worker pool. |
| 7 | `EngineContext` borrowed runtime graph | `Run::Run()` calls `BindEngineContext()` before `Run::Initialise()`. | `EngineContext` borrows scene, simulation, capture, diagnostics, commands, subsystem state, runtime settings, input, camera/debug, world, and model storage owned by `Run`. |
| 8 | Runtime service aliases | `RunApp(window, args, config, workerPool)` passes the bootstrap-created window, process config, and worker pool into `Run`; `Run::Initialise()` validates the window borrow, samples `Gfx()` once, binds `RunSubsystemState::renderBackend`, reads the renderer name, and sets the window title. | Window, config, worker pool, and renderer are preconditions for `Run::Initialise()`; split runtime files consume the cached renderer borrow instead of reacquiring the global facade. |
| 9 | Asset/texture bridge | `m_systems.textures.BindAssetSystem(&m_systems.assets)`, then `RegisterBuiltInAssets(initConfig)`. | Texture source compatibility still borrows the runtime-owned `AssetSystem`; shader creation now receives explicit asset and render-resource contexts at each setup site. |
| 10 | Initial render resource records | `RebuildRegisteredRenderResources()` resets render helper caches, re-registers built-in source records, and rebuilds textures. | Shader source records are registered through built-in assets; shader objects are created lazily by callers through `AssetSystem::CreateShader(renderResources, ...)`. |
| 11 | Terrain/world/sky/UI/camera scene setup | Terrain is constructed from registered source asset path; the Run-owned skybox is created/reset; world environment receives config and terrain bounds; UI text resources are ensured; the Run-owned camera collection is reset/reborrowed; `LoadScene(0)` starts scene runtime. | Scene services are last because they depend on asset, terrain, render, world, and camera state. |

Shutdown order:

| Order | Owner | Current unbind / release step | Notes |
|-------|-------|-------------------------------|-------|
| 1 | `Run::~Run()` | If `Gfx()` is ready, flush GPU work before releasing runtime-owned render resources, then clear the cached `RunSubsystemState::renderBackend` borrow. | Prevents queued GPU work from reading resources while runtime owners destroy them and ensures the runtime borrow is gone before backend teardown. |
| 2 | `Run::ReleaseBackendOwnedRenderResources("shutdown_release")` | Releases world environment render resources, helper resources, game-model resources, collision visualizer, UI resources, runtime render-pass resources, profiler GPU queries, texture collection GPU textures, camera resources, skybox resources, and launcher laser resources. | World environment reset is followed by an immediate flush because it can record upload commands before later release steps. |
| 3 | Worker/backend/window/COM cleanup | After `RunApp()` returns, the local `workerPool.Shutdown()` runs, then `CleanupWindow(...)` calls `DestroyGfxBackend()`, releases the Win32 device context, restores fullscreen cursor/display state if needed, unregisters the window class, and calls `Window::Destroy()` to clear the singleton pointer. `CoUninitialize()` completes process teardown after cleanup returns. | `DestroyGfxBackend()` clears raytracing alias state before releasing the backend owner; current `Window::Destroy()` does not destroy the HWND, it clears `Window::pInstance`. |

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to block new normal-path
  `Gfx()`, `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, and
  singleton `Instance()` calls outside allowlisted bootstrap/bridge files.
  2026-06-28 note: implemented as a counted new-code ratchet for current source
  files, with an explicit file-classification fence for direct renderer globals;
  the broader per-site bootstrap/bridge classification is still open under
  Inventory.
- [x] Add counted allowlists for remaining globals and lower them after each
  migration slice.
- [x] Add synthetic checker tests that reject a new normal-path global service
  access.
- [x] Add a review checklist entry asking whether a new dependency should be a
  borrowed context instead of a global.

## Validation Checklist

- [x] Batch heavy validation after up to 10 completed Carmack slices, before plan
  completion, or before PR handoff; do not run full gates for every tiny
  ratchet slice unless the change is high-risk.
  - [x] 2026-06-29 closure disposition: final heavy validation is transferred to
    the nightrunner integration/PR gate because this worktree currently contains
    multiple overlapping Carmack and non-Carmack plan changes. This plan records
    the selected gates, but does not claim they passed on the final combined
    worktree.
- [x] For plan-only edits: no validation required.
  - [x] 2026-06-28 review-checklist entry slice was plan-only; no repository
    validation required.
  - [x] 2026-06-28 lifetime-order documentation slice was plan-only; no
    repository validation required.
  - [x] 2026-06-28 superseded-related-plan correction was plan-only; no
    repository validation required.
- [x] For runtime-wide lifetime or startup/shutdown changes: include
  `tools\validate_full.bat` in the next heavy validation batch.
  - [x] 2026-06-28 EngineContext borrowed-binding assertion slice:
    `tools\validate_full.bat` passed in 34.11s; log:
    `TestOutput\validation\agent_logs\engine_context_assertions_validate_full.log`.
- [x] For renderer service access changes: include
  `tools\validate_dx12_renderer.bat` in the next heavy validation batch.
- [x] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: include `tools\validate_full.bat` in the next heavy
  validation batch.
  - [x] 2026-06-29 reconciliation: earlier built-in asset registration and
    scene asset-loading context slices selected and ran `tools\validate_full.bat`
    in their heavy batches. Retained logs:
    `TestOutput\validation\agent_logs\editor_asset_context_validate_full_final.log`
    and
    `TestOutput\validation\agent_logs\scene_parser_asset_context_validate_full_final.log`.
    No hull asset or scene JSON behavior was changed in the latest reconciliation
    slice.
- [x] For input/window changes: include `tools\validate_full.bat` in the next
  heavy validation batch; add focused launch/click validation if interaction
  behavior changes.
  - [x] 2026-06-28 input callback bridge documentation slice was comment-only;
    no repository validation required. `git diff --check` passed and the scoped
    comment-style audit inspected `SkullbonezSource/Runtime/Input.cpp`.
  - [x] 2026-06-28 input callback bridge lifecycle slice:
    `tools\validate_full.bat` passed in 140.80s; log:
    `TestOutput\validation\agent_logs\input_callback_bridge_validate_full.log`.
    `tools\validate_interaction_clicks.bat` passed in 5.74s; log:
    `TestOutput\validation\agent_logs\input_callback_bridge_validate_interaction_clicks.log`.
- [x] For guardrail-tooling changes: run `python tools\check_runtime_boundaries.py`
  and `tools\validate_fast.bat`.
- [x] Quote validation output and log paths in the handoff.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to distinguish legitimate callback bridges from
  avoidable service locators.
- [x] Ask the reviewer to inspect startup/shutdown lifetime order and borrowed
  pointer safety.
- [x] Ask the reviewer to search for new normal-path global access.
- [x] Ask whether any new dependency should be passed as an explicit borrowed
  context instead of reached through `Gfx()`, `ActiveAssetSystem()`, `Cfg()`,
  singleton `Instance()`, `pInstance`, or `g_*` process state.
- [x] Record review findings in a report or this plan.
- [x] Resolve blocking findings before committing PR-bound code.
- [x] Final closure rubber-duck review confirms the validation-deferral
  disposition is acceptable before moving this plan to `Done`.
  - [x] 2026-06-29 final rubber-duck review by Hume found no blockers.
    Non-blocking notes: final heavy validation is still deferred by design to
    the nightrunner integration/PR gate, the known Profile `CL.exe` codegen
    crash remains recorded in `TODO.md`, and the plan must not claim PR
    readiness until those gates pass or are explicitly dispositioned. Review
    accounting: prompt 2332 chars, response 3383 chars, elapsed 3m 06s,
    tokens n/a.

Reviewer notes, 2026-06-28:

- Ampere blocked the first guardrail draft because it only watched named
  renderer/asset/window singleton access and missed plan-named `Cfg()`, generic
  `::Instance()`, `pInstance`, and `g_*` process globals.
- Ampere also flagged that file-level counts are a ratchet, not a substitute
  for classifying every remaining call as bootstrap, shutdown, OS callback
  bridge, or normal runtime debt.
- The guardrail was expanded to cover the missing patterns and string/comment
  false positives were removed. The broader per-site classification remains
  intentionally unchecked.
- Ampere re-checked the expanded guardrail and found no remaining blockers
  before commit. Residual non-blocking note: the `g_*` pattern counts references
  as well as declarations, which is conservative but acceptable for this
  new-code ratchet.
- Harvey blocked the scene parser asset-context slice until the current counted
  surface table matched the lowered `ActiveAssetSystem()` ratchet. The snapshot
  now removes `TestSceneParser.cpp` and records `ActiveAssetSystem()` count `2`
  for the compatibility helper declaration/definition only.
- Pauli found no blocking defect in the editor placement asset-context slice.
  Non-blocking residuals: the parsed building catalog remains a one-shot
  process-static cache by design for this slice, and unrelated dirty docs stay
  user-owned/uncommitted. Pauli's missing-evidence reminders were cleared by the
  touched-source comment-style audit plus final logged runtime-boundary,
  `validate_fast`, and `validate_full` gates.
- Sagan blocked the lifetime-order documentation slice until it included
  `Run::Run()` / `EngineContext` bindings, stopped overclaiming shader resource
  creation in `RebuildRegisteredRenderResources()`, and corrected the window
  cleanup wording around `UnregisterClass(...)` and `Window::Destroy()`. The
  non-blocking worker self-test ordering note was also folded into the startup
  table.
- Faraday blocked the borrowed-binding assertion slice because
  `RuntimeViewModelBuilder::Build()` still returned a default snapshot before
  calling `EngineContext::Bindings()`, bypassing the new fail-fast path. The
  follow-up assertion in the view-model builder fixed that blocker; no normal
  startup/shutdown false positive remained in review.
- Kepler blocked the input callback bridge documentation slice because the first
  comment draft called cursor policy and automation override state callback
  accumulators, and misnamed the mouse-wheel consumer/reset path. Follow-up
  review confirmed those blockers were fixed; the final non-blocking invariant
  wording catch was also corrected before commit.
- Mencius blocked the input callback bridge lifecycle slice because the first
  code draft allowed same-window rebind/unbound unbind, let
  `RegisterRawMouseInput()` reset state for an unbound `HWND`, and then left the
  plan stale after code fixes. The final code gates callback accumulators on the
  bound `HWND`, tightens lifecycle assertions, and updates only the narrow
  callback lifecycle/debug-assertion items.
- Hume performed the final closure rubber-duck review on 2026-06-29 and found
  no blockers to moving this plan to `Done`. Missing evidence is intentionally
  carried forward: final heavy validation remains a nightrunner integration/PR
  gate, and the known Profile build/codegen blocker remains recorded in
  `TODO.md`.

## Definition Of Done

- [x] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals.
  - [x] 2026-06-29 source-hit classification found no remaining direct normal
    runtime/render/asset/scene service-locator calls. Remaining live globals are
    bootstrap renderer/config/window/worker samples, backend facade
    compatibility accessors, OS/input/window bridges, core diagnostics/service
    owner state, and the `DiagnosticsRuntime` profiler borrow owner.
- [x] Remaining globals are isolated to bootstrap/shutdown samples, backend
  facade compatibility, core diagnostic/service owners, or OS callback bridges
  with explicit lifecycle comments.
  - [x] 2026-06-29 source-hit review plus final lifecycle-comment reconciliation
    found no remaining direct normal runtime/render/asset/scene service-locator
    calls. Core singletons retained by design are owner internals rather than new
    dependency lookup points.
- [x] Guardrails prevent new global service access from creeping back in.
  - [x] 2026-06-29 `python tools\check_runtime_boundaries.py` passed with 0
    errors; log:
    `TestOutput\validation\agent_logs\global_service_final_runtime_boundaries.log`.
- [x] Required validation disposition is recorded for the touched implementation
  areas.
  - [x] 2026-06-29 final execution deferred to the nightrunner integration/PR
    gate. Current required candidates remain `tools\validate_full.bat`,
    `tools\validate_dx12_renderer.bat`, and the guardrail-tooling fast gate if
    preparing PR-bound work. The latest static guardrail proof is
    `TestOutput\validation\agent_logs\global_service_final_runtime_boundaries.log`.

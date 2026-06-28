# IN PROGRESS TODO Notes

## 2026-06-28 Perf Warning To Debug

Source log:
`TestOutput\validation\agent_logs\render_resource_context_validate_perf.log`

The DX12 perf comparison in that log was skipped because of a machine mismatch.
The actionable warning is `PHYSICS_BENCH Perf: 3959e0b1 vs 14795e0`.

Exact retained CPU avg rows from the log:

| Marker | Baseline avg ms | Current avg ms | Delta avg | Delta p50 |
|--------|-----------------|----------------|-----------|-----------|
| `Frame` | 0.4050 | 0.6293 | +55.4% | +67.0% |
| `Frame/Render` | 0.0987 | 0.4747 | +381.0% | +394.3% |
| `Frame/VsyncWait` | 0.1053 | 0.2558 | +142.9% | +94.3% |

Inflated render children shown in the retained CPU table:

| Marker | Baseline avg ms | Current avg ms | Delta avg | Delta p50 |
|--------|-----------------|----------------|-----------|-----------|
| `Frame/Render/Skybox` | 0.0138 | 0.1350 | +878.3% | +1004.3% |
| `Frame/Render/Balls` | 0.0112 | 0.0447 | +299.1% | +287.2% |
| `Frame/Render/Terrain` | 0.0045 | 0.0299 | +564.4% | +605.0% |

Inflated GPU rows shown in the retained table:

| Marker | Baseline avg ms | Current avg ms | Delta avg | Delta p50 |
|--------|-----------------|----------------|-----------|-----------|
| `Frame/Render/Skybox_gpu` | 0.0128 | 0.1016 | +693.7% | +323.3% |
| `Frame/Render/Balls_gpu` | 0.0205 | 0.0633 | +208.8% | +244.9% |
| `Frame/Render/Terrain_gpu` | 0.0162 | 0.1758 | +985.2% | +1198.8% |

Memory rows from the retained table:

| Metric | Baseline MB | Current MB | Delta |
|--------|-------------|------------|-------|
| `mem_start` | 72.20 | 83.91 | +11.71 MB |
| `mem_restart` | 78.07 | 149.17 | +71.10 MB |
| `mem_end` | 78.07 | 149.17 | +71.10 MB |

The `Profile\physics_bench_perf.json` file was overwritten later, so exact p50
before/after values for run `3959e0b1` are not recoverable from retained JSON.
Only the p50 deltas above are preserved in the log.

## 2026-06-28 Carmack Completion Audit

Unchecked top-level checklist items after the 2026-06-29 completion audit
correction:

| Plan | Open items |
|------|------------|
| `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md` | 0 |
| `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md` | 0 |
| `Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md` | 0 |
| `Agentic/Plans/Done/carmack-render-backend-capability-plan.md` | 0 |

Raw remaining total across the active Carmack plans: 0.

The backend capability, global-service, physics standalone, and render graph
resource ownership plans are currently in `Done`, and each was rubber-ducked
before/for its move. Remaining source work from superseded Carmack documents is
owned by the active broad plans named below, not by active Carmack files.

2026-06-29 closure policy for the remaining Carmack documents:

- The goal is to clear narrow Carmack coordination documents, not to complete
  every broad architecture roadmap nested inside them.
- Source-work items stay unchecked while the implementation is merely
  transferred/deferred to a larger active plan. A transferred note is useful
  ownership routing, not completion evidence.
- A Carmack document can move to `Done` with superseded broad items only if the
  file-by-file review and rubber-duck pass explicitly accept that disposition;
  checked items must say superseded/deprecated, not source-complete.
- Before any Carmack plan moves to `Done`, run a final rubber-duck review for
  that plan and confirm the reviewer accepts the transfer/defer disposition.

Post-move correction: `Agentic/Plans/Done/carmack-render-backend-capability-plan.md`
still contained a stale note saying a follow-up rubber-duck pass was required.
An in-session completion review checked its zero-open checklist state, source
surface, and `python tools\check_runtime_boundaries.py` with 0 errors; the plan
now records that no blocking issue was found for it staying in `Done`.

Completed global-service closure:

- `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md` moved to `Done`
  on 2026-06-29 after file-by-file source review, zero unchecked checklist
  items, and Hume's final read-only rubber-duck verdict of no blockers.
  Remaining counted access is classified as startup/shutdown sampling, backend
  facade compatibility, core diagnostic/service owner internals, or Win32
  callback bridge state. The latest guardrail proof is
  `TestOutput\validation\agent_logs\global_service_final_runtime_boundaries.log`.
- The global-service implementation work is reconciled; final heavy validation
  is deferred to the integration/PR gate because the worktree contains multiple
  overlapping in-progress plan changes. Required future gate candidates remain
  `tools\validate_full.bat`, `tools\validate_dx12_renderer.bat`, and
  `tools\validate_fast.bat` / `python tools\check_runtime_boundaries.py` for
  guardrail-tooling coverage.
- `SkyBox.cpp`, `WorldEnvironment.cpp`, `Terrain.cpp`, `PhysicsWorld.cpp`, and
  `PersistentContactSolver.cpp` are no longer part of this counted config debt
  after their config-context slices. Active model capacity also no longer uses a
  no-arg config helper. The legacy `Cfg()` shim has been removed and raw source
  search finds no `Cfg()` calls. Bootstrap config singleton access is now
  classified as explicit `EngineConfig::Instance()` debt in `Runtime/Init.cpp`.
  `PhysicsWorld.cpp`, replay prediction capture, worker-thread UI tuning, scene
  worker overrides, UI worker-count reporting, and render shadow-prep worker
  collection also no longer call `WorkerPool::Instance()`. Remaining
  `WorkerPool::Instance()` debt is limited to the Core service owner/self-test
  and one bootstrap sample in `Runtime/Init.cpp`. `World/*` no longer carries
  counted `Gfx()` or `CreateShaderFromActiveAssets()` debt.
- `RunSubsystemState` now carries the renderer borrow bound during
  `Run::Initialise()`. `RunInput.cpp`, `RunStress.cpp`, and
  `Runtime/Scene/RunScene.cpp` use that cached borrow for generated-control and
  scene-load contexts instead of reacquiring `Gfx()`, removing their counted
  `Gfx()` rows and lowering counted `Gfx()` debt to 20.
- `RunFrame.cpp` now uses the same startup-bound `RunSubsystemState` renderer
  borrow for the per-frame command/resource/diagnostics/lifecycle facets instead
  of reacquiring `Gfx()`, lowering counted `Gfx()` debt to 19. Its remaining
  counted service debt is profiler singleton access.
- `RunRender.cpp` now uses the same startup-bound `RunSubsystemState` renderer
  borrow for render command/resource/diagnostics facets instead of reacquiring
  `Gfx()`, lowering counted `Gfx()` debt to 18.
- `RunSubsystemState` also carries the optional DXR borrow sampled during
  `Run::Initialise()`. `RunRender.cpp` and `Runtime/Scene/RunScene.cpp` now use
  that cached `renderRayTracing` pointer instead of reacquiring
  `GfxRayTracing()`, lowering counted `GfxRayTracing()` debt to 3. The remaining
  runtime raytracing-global debt is the startup composition-root sample in
  `Run.cpp`.
- `Profiler` now binds the renderer diagnostics facet from `Run::Initialise()`
  and clears it during `Run` shutdown. `Core/Profiler.cpp` no longer calls
  `Gfx()` / `IsGfxReady()` for platform GPU markers or GPU timer queries,
  lowering counted `Gfx()` debt to 6 while preserving the exact marker names and
  timer IDs used by perf CSV/debugging.
- Runtime/UI profiler reads now route through
  `DiagnosticsRuntime::RuntimeProfiler()`. `Run.cpp`, `RunFrame.cpp`,
  `RunUiTextPass.cpp`, `RuntimeDiagnostics.cpp`, and `UITabProfiler.cpp` no
  longer call `Profiler::Instance()` directly; the remaining counted profiler
  singleton surface is Core profiler internals/macros plus the
  `DiagnosticsRuntime` profiler-borrow owner.
- Remaining normal-runtime renderer readiness probes now read the cached
  `RunSubsystemState::renderBackend` borrow instead of `IsGfxReady()`:
  `RunInput.cpp`, `RuntimeRenderHost.cpp`, and `RunPasses.cpp` are clean. Only
  the backend facade/header still mention `IsGfxReady()`.
- `DrawCallTraceScope` now receives a borrowed `IRenderDiagnostics*` from each
  frame/pass/model/UI caller instead of calling `Gfx()` internally, lowering
  counted `Gfx()` debt to 4. `IRenderSceneView`/`GameModelCollection` forward
  diagnostics to model and shadow-caster batch draws; the remaining
  `IRenderBackend.h` `Gfx()` hit is only the accessor declaration.
- `Run.cpp` now uses the startup-bound `RunSubsystemState` renderer borrow for
  shutdown/resource-release/logging lifecycle access. Its counted `Gfx()` debt
  is down to 1, and that remaining call is the intentional startup sample in
  `Run::Initialise()`.
- `Window.cpp` no longer calls `Gfx()` for `WM_SIZE`/startup resize. `Init`
  binds the live renderer into `Window` after backend creation and clears that
  borrow before backend teardown; `Window` resize callbacks no-op when the
  backend borrow is absent. The boundary ratchet now has no `Window.cpp`
  `Gfx()` allowance, lowering counted `Gfx()` debt to 23.
- `Physics/Debug/CollisionVisualizer.cpp` no longer calls
  `CreateShaderFromActiveAssets()`; collision-solid passes now borrow
  `Run`'s `AssetSystem` through `IRenderSceneView`/`GameModelCollection`.
  It also no longer calls `Gfx()`; collision visualizer resource lifetime and
  draw submission now use borrowed render-resource and command contexts.
- `Rendering/Helper.cpp` no longer calls `CreateShaderFromActiveAssets()` or
  `Gfx()`; normal object, shadow caster, replay ghost, reset/rebuild, and DXR
  sphere-prewarm paths now borrow the active `AssetSystem`,
  `IRenderCommandContext`, and `IRenderResourceFactory` from the render pass,
  scene view, render host, or scene-load composition root.
- `Rendering/Text.cpp` no longer calls `CreateShaderFromActiveAssets()` or
  `Gfx()`; `UiTextPass` now supplies the active `AssetSystem`,
  `IRenderResourceFactory`, and scoped `IRenderCommandContext` used for font
  resource setup, text/quad dynamic buffers, HUD draw state, uploads, and
  flushes.
- `World/Terrain.cpp` and `World/WorldEnvironment.cpp` no longer call
  `CreateShaderFromActiveAssets()`; terrain and water render passes now borrow
  `Run`'s `AssetSystem` and the active `IRenderResourceFactory` for shader
  setup.
- `Assets/AssetSystem.h/.cpp` no longer expose `ActiveAssetSystem()` or
  `CreateShaderFromActiveAssets()`, and `AssetSystem.cpp` no longer calls
  `Gfx()` or owns the old active asset global. `AssetSystem::CreateShader()`
  now receives the caller's active `IRenderResourceFactory`.
- `World/WorldEnvironment.cpp` no longer calls `Gfx()`; water mesh creation now
  runs through `WaterPass::EnsureGpuResources()` and reflection texture binding
  uses the frame command context.
- `World/Terrain.cpp` no longer calls `Gfx()`; terrain mesh creation now runs
  through `TerrainPass::EnsureGpuResources()` with the pass render-resource
  factory, and scene-load DXR setup prewarms the mesh through the same active
  renderer borrow before reading `Terrain::GetMesh()`.
- `Assets/TextureCollection.cpp` no longer calls `Gfx()`; source-asset texture
  rebuilds, JPEG uploads, and backend texture deletion now receive an
  `IRenderResourceFactory`, while draw-time texture selection binds resident
  handles through the frame `IRenderCommandContext`.
- `Rendering/Shadow.h` no longer calls `Gfx()`; shadow receiver texture binding
  and clearing now use the frame `IRenderCommandContext` supplied through
  `TerrainPass` and `Terrain::Render()`.
- Heavy/final validation is intentionally deferred until the whole Carmack
  plan is complete, per the current orchestration rule.

Build probe warning after the GameModel config slice:

- `tools\validate_build.bat Profile` first caught normal compile errors from
  the in-progress config-threading edit; those were fixed.
- After the fixes, `tools\validate_build.bat Profile` failed twice with no C++
  diagnostics and `CL.exe` exit code `-1073741819` during code generation.
- A clean Profile build was attempted with MSBuild `Clean` followed by
  `tools\validate_build.bat Profile`; it reproduced the same `CL.exe` codegen
  access violation.
- `SKULLBONEZ_PLATFORM_TOOLSET=v143 tools\validate_build.bat Profile` was
  attempted because `AGENTS.md` names v143 as expected, but this machine reports
  `ERROR: Requested platform toolset v143 not found.`
- Individual Profile compiles passed for `GameModel.cpp`,
  `SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `RuntimeTools.cpp`,
  `Ragdoll.cpp`, `Run.cpp`, `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`,
  `RunEditorTools.cpp`, and `RunInteractionAutomation.cpp`.
  The failure remains a repeatable full-Profile-build blocker to resolve before
  PR-bound validation is claimed.
- A later focused Profile `ClCompile` for the physics config-context slice
  passed in 10.38s with 0 warnings and 0 errors for `PhysicsWorld.cpp`,
  `PersistentContactSolver.cpp`, `PhysicsScene.cpp`, `PhysicsEngine.cpp`,
  `GameModelCollection.cpp`, and `Run.cpp`. The focused compile does not clear
  the repeatable full-Profile-build blocker above.
- A later focused Profile `ClCompile` for the active-capacity config slice
  passed in 21.23s with 0 warnings and 0 errors for `GameModelCollection.cpp`,
  `Run.cpp`, `RunFrame.cpp`, `RunInput.cpp`, `RunEditorTools.cpp`,
  `RunStress.cpp`, `RunUiTextPass.cpp`, and `RunScene.cpp`. The focused compile
  does not clear the repeatable full-Profile-build blocker above.
- A later focused Debug `ClCompile` for `Init.cpp` passed in 4.85s with 0
  warnings and 0 errors after removing `Cfg()`. A focused Profile `ClCompile`
  for `Init.cpp` reproduced the same MSVC `CL.exe` codegen access violation
  (`-1073741819`) with no C++ diagnostics, so the full-Profile-build blocker
  still stands.
- A later focused Profile `ClCompile` for the worker-service routing slice
  initially caught a real compile miss: `RunUiTextPass.cpp` tried to read
  `m_workerPool` from `RuntimeRenderHost`. The fix added an explicit
  `RenderRuntimeView::workerPool` binding and updated the render-host view
  allowlist deliberately.
- The rerun focused Profile `ClCompile` passed in 21.74s with 0 warnings and 0
  errors for `PhysicsWorld.cpp`, `PhysicsScene.cpp`, `PhysicsEngine.cpp`,
  `GameModelCollection.cpp`, `Run.cpp`, `RuntimeTuning.cpp`, `RunInput.cpp`,
  `RunScene.cpp`, `RunUiTextPass.cpp`, and `RunReplayTools.cpp`.
- A focused Debug `ClCompile` for `Init.cpp` after the worker-service routing
  slice passed in 4.89s with 0 warnings and 0 errors. The known Profile
  `Init.cpp` codegen crash remains intentionally unclaimed.
- 2026-06-29 post-archive broad gate: `tools\validate_full.bat` was run after
  the documentation/comment archive commit to test the remaining dirty source
  worktree. Phase 0 passed (`validate_project_filters` 0 errors and
  `validate_runtime_boundaries` 0 errors), then Phase 1 failed during
  `tools\validate_build.bat Profile` with the same `CL.exe` codegen access
  violation: `MSB6006: "CL.exe" exited with code -1073741819`. The run ended
  after `00:01:39.11` with 0 warnings and 1 error. Log:
  `TestOutput\validation\agent_logs\nightrunner_validate_full_after_plan_archive.log`.
- A later focused Profile `ClCompile` for the render shadow-prep worker routing
  slice passed in 5.19s with 0 warnings and 0 errors for
  `GameModelRenderer.cpp`, `GameModelCollection.cpp`, and `RunPasses.cpp`.
  Raw source search now finds `WorkerPool::Instance()` only in
  `Core/WorkerPool.cpp` and `Runtime/Init.cpp`.
- A later focused Profile `ClCompile` for the `RunInput.cpp` input-turn
  renderer borrow slice passed in 3.06s with 0 warnings and 0 errors. The
  boundary ratchet allowed one `RunInput.cpp` `Gfx()` call at that point; the
  2026-06-29 generated-control renderer-borrow slice later removed that row.
- A later focused Profile `ClCompile` for the `RunScene.cpp` scene-load
  renderer borrow slice passed in 5.35s with `/warnaserror` and no diagnostics.
  The boundary ratchet allowed one `RunScene.cpp` `Gfx()` call at that point;
  the 2026-06-29 generated-control renderer-borrow slice later removed that row.
- A later focused Profile `ClCompile` for the `RunStress.cpp` UI-stress
  renderer borrow slice passed in 3.13s with `/warnaserror` and no diagnostics.
  The boundary ratchet allowed one `RunStress.cpp` `Gfx()` call at that point;
  the 2026-06-29 generated-control renderer-borrow slice later removed that row.
- A later focused Profile `ClCompile` for the `Run.cpp` renderer lifecycle
  borrow slice passed in 3.77s with `/warnaserror` and no diagnostics. The
  boundary ratchet now allows six `Run.cpp` `Gfx()` calls.
- A later focused Profile `ClCompile` for the physics-diagnostics renderer-name
  threading passed for `Run.cpp` in 3.88s with `/warnaserror` and no diagnostics.
  A parallel `RunScene.cpp` compile first hit PDB lock `C1041`; the serial rerun
  passed in 5.29s with `/warnaserror` and no diagnostics. The boundary ratchet
  now allows five `Run.cpp` `Gfx()` calls.
- A later focused Profile `ClCompile` for the scene-finished renderer-name
  threading passed for `Run.cpp` in 3.82s with `/warnaserror` and no diagnostics.
  The first `RunFrame.cpp` compile caught Profile-only unused `rendererName`
  parameters; after explicit `(void)` markers, `RunFrame.cpp` passed in 3.69s
  with `/warnaserror` and no diagnostics. The boundary ratchet now allows four
  `Run.cpp` `Gfx()` calls; the 2026-06-29 frame-loop renderer-borrow slice later
  removed the remaining `RunFrame.cpp` `Gfx()` row.
- A later focused Profile `ClCompile` for the shader-context batch first caught
  `Terrain` fallback constructors trying to default-construct private
  `EngineConfig`; `EngineConfig::Defaults()` now supplies a non-singleton
  default value for explicit test/standalone terrains. The rerun selected-file
  compile passed in 14.30s with `/warnaserror` for
  `CollisionVisualizer.cpp`, `GameModelCollection.cpp`, `GameModelRenderer.cpp`,
  `Helper.cpp`, `RunPasses.cpp`, `RuntimeRenderHost.cpp`, `RunUiTextPass.cpp`,
  `Terrain.cpp`, `Text.cpp`, and `WorldEnvironment.cpp`. At this point in the
  history the boundary ratchet left `CreateShaderFromActiveAssets()` only in
  `AssetSystem.h/.cpp`; a later asset shader-context slice removed that bridge.
- A later focused Profile `ClCompile` for the water render-resource/context
  slice passed in 4.65s with `/warnaserror` for `RunPasses.cpp` and
  `WorldEnvironment.cpp`. The boundary ratchet now has no
  `WorldEnvironment.cpp` `Gfx()` allowance.
- A later focused Profile `ClCompile` for the terrain render-resource/context
  slice passed in 8.49s with `/warnaserror` for `Terrain.cpp`,
  `RunPasses.cpp`, and `RunScene.cpp`. `python
  tools\check_runtime_boundaries.py` passed with 0 errors, and `git diff
  --check` still reports only the pre-existing `tools/codex_usage_daily.bat`
  CRLF warning. The boundary ratchet now has no `Terrain.cpp` `Gfx()`
  allowance, lowering counted `Gfx()` debt to 118.
- A later focused Profile `ClCompile` for the texture resource/context slice
  passed in 11.16s with `/warnaserror` for `TextureCollection.cpp`,
  `SkyBox.cpp`, `Run.cpp`, `RunRender.cpp`, `RunPasses.cpp`, and
  `RuntimeRenderHost.cpp`. `python tools\check_runtime_boundaries.py` passed
  with 0 errors, and `git diff --check` still reports only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. The boundary ratchet now has no
  `TextureCollection.cpp` `Gfx()` allowance, lowering counted `Gfx()` debt to
  115.
- A later focused Profile `ClCompile` for the shadow receiver binding slice
  passed in 4.27s with `/warnaserror` for `Terrain.cpp` and `RunPasses.cpp`.
  `python tools\check_runtime_boundaries.py` passed with 0 errors. The boundary
  ratchet now has no `Shadow.h` `Gfx()` allowance, lowering counted `Gfx()`
  debt to 113.
- A later focused per-file Profile `ClCompile` for the physics debug
  line-submission slice passed in 20.49s with `/warnaserror` for
  `BroadphaseVisualizer.cpp`, `PhysicsDebugVisualizer.cpp`, `TornadoField.cpp`,
  `PhysicsWorld.cpp`, `PhysicsScene.cpp`, `PhysicsEngine.cpp`,
  `GameModelCollection.cpp`, and `RunPasses.cpp`. `python
  tools\check_runtime_boundaries.py` passed with 0 errors. The boundary ratchet
  now has no `BroadphaseVisualizer.cpp`, `PhysicsDebugVisualizer.cpp`, or
  `TornadoField.cpp` `Gfx()` allowance, lowering counted `Gfx()` debt to 107.
- A later focused per-file Profile `ClCompile` for the collision visualizer
  render-context slice passed in 12.07s with `/warnaserror` for
  `CollisionVisualizer.cpp`, `GameModelCollection.cpp`, `RunPasses.cpp`, and
  `Run.cpp`. `python tools\check_runtime_boundaries.py` passed with 0 errors,
  and `git diff --check` still reports only the pre-existing
  `tools/codex_usage_daily.bat` CRLF warning. The boundary ratchet now has no
  `CollisionVisualizer.cpp` `Gfx()` allowance, lowering counted `Gfx()` debt to
  93.
- A later static-only helper render-context slice removed all direct
  `Gfx()` calls from `Rendering/Helper.cpp`. `RenderHelper`,
  `GameModelRenderer`, `IRenderSceneView`, `GameModelCollection`, object,
  shadow, reflection, replay-ghost, reset/rebuild, and DXR sphere-prewarm paths
  now forward borrowed command/resource capabilities. The boundary ratchet now
  has no `Helper.cpp` `Gfx()` allowance, lowering counted `Gfx()` debt to 59.
  Per the current Carmack batching rule, no focused build, guardrail execution,
  or heavy validation was run for this slice; static source search found no
  `Gfx()` in `Helper.cpp` and no old helper draw/prewarm call sites.
- A later static-only text render-context slice removed all direct `Gfx()`
  calls from `Rendering/Text.cpp`. Font atlas/dynamic-buffer setup and teardown
  now use `IRenderResourceFactory`, while `UiTextPass::Render()` opens a scoped
  text render context so HUD, UI, profiler, and replay overlay flushes draw
  through `IRenderCommandContext`. The boundary ratchet now has no `Text.cpp`
  `Gfx()` allowance, lowering counted `Gfx()` debt to 26. Per the current
  Carmack batching rule, no focused build, guardrail execution, or heavy
  validation was run for this slice; static source search found no `Gfx()` in
  the edited text/UI files and no old text resource setup signatures.
- A later static-only asset shader-context slice made
  `AssetSystem::CreateShader()` take the active `IRenderResourceFactory` and
  removed the old `ActiveAssetSystem()` / `CreateShaderFromActiveAssets()`
  bridge. All shader setup callers now pass their existing render-resource
  context. The boundary ratchet has no `AssetSystem.cpp/.h` compatibility rows,
  lowering counted `Gfx()` debt to 24, `ActiveAssetSystem()` debt to 0,
  `CreateShaderFromActiveAssets()` debt to 0, and mutable `g_*` debt to 81.
  Per the current Carmack batching rule, no focused build, guardrail execution,
  or heavy validation was run for this slice; static source search found no old
  shader-call signature, no active-asset bridge symbols in `SkullbonezSource`,
  and no `Gfx()` in `AssetSystem`.
- A later static-only window resize-context slice removed `Window.cpp` direct
  `Gfx()` access. `InitRenderBackend()` binds the active renderer into `Window`
  for resize callbacks, `CleanupWindow()` clears that borrow before backend
  teardown, and early/late `WM_SIZE` callbacks no-op without a bound backend.
  The boundary ratchet now has no `Window.cpp` `Gfx()` row, lowering counted
  `Gfx()` debt to 23. Per the current Carmack batching rule, no focused build,
  guardrail execution, or heavy validation was run for this slice; static source
  search found no `Gfx()` / `IsGfxReady()` in `Window.cpp/.h` and scoped
  `git diff --check` passed for touched files.
- A later static-only generated-control renderer-borrow slice added
  `RunSubsystemState::renderBackend`, bound it in `Run::Initialise()`, cleared
  it during `Run` shutdown, and routed `RunInput.cpp`, `RunStress.cpp`, and
  `Runtime/Scene/RunScene.cpp` through that borrow instead of direct `Gfx()`.
  The boundary ratchet now has no `Gfx()` rows for those files, lowering counted
  `Gfx()` debt to 20. Per the current Carmack batching rule, no focused build,
  guardrail execution, or heavy validation was run for this slice; static source
  search found no `Gfx()` in those three files and scoped `git diff --check`
  passed for touched files.
- A later static-only frame-loop renderer-borrow slice routed `RunFrame.cpp`
  through `RunSubsystemState::renderBackend` instead of direct `Gfx()` for the
  per-frame render facets. The boundary ratchet now has no `RunFrame.cpp`
  `Gfx()` row, lowering counted `Gfx()` debt to 19. Per the current Carmack
  batching rule, no focused build, guardrail execution, or heavy validation was
  run for this slice; static source search found no `Gfx()` in `RunFrame.cpp`
  and scoped `git diff --check` passed for touched files.
- A later static-only render-composition renderer-borrow slice routed
  `RunRender.cpp` through `RunSubsystemState::renderBackend` instead of direct
  `Gfx()` for render service facets. The boundary ratchet now has no
  `RunRender.cpp` `Gfx()` row, lowering counted `Gfx()` debt to 18. Per the
  current Carmack batching rule, no focused build, guardrail execution, or heavy
  validation was run for this slice; static source search found no `Gfx()` in
  `RunRender.cpp` and scoped `git diff --check` passed for touched files.
- A later static-only `Run.cpp` lifecycle renderer-borrow slice routed shutdown,
  backend-resource release, and render-resource lifecycle logging through
  `RunSubsystemState::renderBackend` instead of direct `Gfx()`. The boundary
  ratchet now allows only the startup composition-root `Run.cpp` `Gfx()` call,
  lowering counted `Gfx()` debt to 15. Per the current Carmack batching rule, no
  focused build, guardrail execution, or heavy validation was run for this
  slice; static source search found only one `Gfx()` in `Run.cpp` and scoped
  `git diff --check` passed for touched files.
- A later static-only runtime DXR borrow slice added
  `RunSubsystemState::renderRayTracing`, bound it in `Run::Initialise()`,
  cleared it during `Run` shutdown, and routed `RunRender.cpp` plus
  `Runtime/Scene/RunScene.cpp` through that optional capability borrow instead
  of direct `GfxRayTracing()`. The boundary ratchet now has no renderer-global
  rows for `RunRender.cpp` or `RunScene.cpp`, lowering counted
  `GfxRayTracing()` debt to 3. Per the current Carmack batching rule, no focused
  build, guardrail execution, or heavy validation was run for this slice; static
  source search found no `GfxRayTracing()` in those two files and scoped
  `git diff --check` passed for touched files.
- A later static-only profiler diagnostics-borrow slice added
  `Profiler::BindRenderDiagnostics()`, bound it from `Run::Initialise()`, and
  cleared it during `Run` shutdown. `Core/Profiler.cpp` now routes platform GPU
  markers, GPU timer begin/end/read, and GPU timer invalidation through the
  borrowed `IRenderDiagnostics` facet instead of direct `Gfx()` /
  `IsGfxReady()` calls. The boundary ratchet now has no `Profiler.cpp` `Gfx()`
  row, lowering counted `Gfx()` debt to 6. Per the current Carmack batching
  rule, no focused build, guardrail execution, or heavy validation was run for
  this slice; static source search found no `Gfx()` / `IsGfxReady()` in
  `Profiler.cpp/.h` and scoped `git diff --check` passed for touched files.
- A later static-only renderer-readiness slice routed `RunInput.cpp`,
  `RuntimeRenderHost.cpp`, and `RunPasses.cpp` through the cached
  `RunSubsystemState::renderBackend` borrow instead of `IsGfxReady()`. Per the
  current Carmack batching rule, no focused build, guardrail execution, or heavy
  validation was run for this slice; static source search found no
  `IsGfxReady()` under `Runtime` or `Core` outside the backend facade/header and
  scoped `git diff --check` passed for touched files.
- A later static-only draw-call trace scope slice changed
  `Rendering::DrawCallTraceScope` to consume an `IRenderDiagnostics*` supplied
  by each frame/pass/model/UI caller instead of calling `Gfx()` internally.
  `IRenderSceneView` and `GameModelCollection` now forward diagnostics to model
  and shadow-caster batch draws so nested trace markers stay explicit. The
  boundary ratchet now records only one `IRenderBackend.h` `Gfx()` hit for the
  accessor declaration, lowering counted `Gfx()` debt to 4. Per the current
  Carmack batching rule, no focused build, guardrail execution, or heavy
  validation was run for this slice; static source search found no one-argument
  `DRAW_CALL_TRACE_SCOPE(...)` calls, no `Gfx()` in `IRenderBackend.h` except
  the declaration, and scoped `git diff --check` passed except for the
  pre-existing `tools/codex_usage_daily.bat` CRLF warning.
- A later static-only profiler diagnostics-borrow slice added
  `DiagnosticsRuntime::RuntimeProfiler()` and routed explicit runtime/UI
  profiler reads through that diagnostics boundary. `InGameUIFrameData` carries
  a nullable borrowed profiler pointer for profiler-tab marker rows while
  retaining cached draw-call trace rows. The boundary ratchet now allows
  `Profiler::Instance()` only in `Core/Profiler.*` and
  `Runtime/Diagnostics/DiagnosticsRuntime.cpp`, lowering counted
  `Profiler::Instance()` debt from 27 to 15. Per the current Carmack batching
  rule, no focused build, guardrail execution, or heavy validation was run for
  this slice; static source search found no `Profiler::Instance()` in `Runtime`
  or `UI` outside `DiagnosticsRuntime.cpp`, and scoped `git diff --check` passed
  except for the pre-existing `tools/codex_usage_daily.bat` CRLF warning.

Completed/superseded physics-standalone closure:

- `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md` moved to
  `Done` on 2026-06-29 after file-by-file source review, zero unchecked
  checklist items, and Volta's final read-only rubber-duck verdict of no
  blockers for the superseded-plan disposition. This is not evidence that the
  broad source authority migration is implemented.
- `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` exist, but
  source comments and implementation still describe them as compatibility
  snapshots in `GameModelCollection` order rather than fully authoritative
  owners for body, collider, and render-presented state.
- `PhysicsWorld` still takes `PhysicsModelAccess&` across the normal step,
  diagnostics, sleep, narrowphase, and point-joint paths. `PhysicsWorld.cpp`
  still reads `GameModelBodyStream` and calls `GameModel::SweepGameModel(...)`
  for object sweep tests.
- `GameModelCollection` still implements `PhysicsModelAccess`, exposes
  `MutablePhysicsModelsForCompatibility()`, emits SkullScope frames, forwards
  physics diagnostics configuration, and owns the current collection-backed
  compatibility boundary.
- `Ragdoll` and legacy `PointJointConstraint` paths still use model indices
  for scene ragdoll construction and solver constraints. Standalone point-joint
  records use handles, but the plan explicitly leaves legacy scene/ragdoll
  migration open.
- Runtime/editor/replay storage is not yet proven handle-based. Source search
  still finds scene setup, editor/tool, replay, and presentation paths tied to
  model indices or `GameModelCollection`.
- These remaining items are now authoritative in
  `Agentic/Plans/IN PROGRESS/physics-game-model-authority-plan.md`, which owns
  the broad store-authority, stable-handle, replay/editor/runtime handle
  storage, render mirror, deletion, SkullScope, fixed-tree-release handoff, and
  validation work. Volta accepted retiring the Carmack standalone plan as a
  completed API/adapter beachhead plus supersession record.

Completed/superseded render-graph-resource closure:

- `Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md` moved to
  `Done` on 2026-06-29 after file-by-file source review, zero unchecked
  checklist items, and Mendel's final read-only rubber-duck verdict of no
  blockers for the superseded-plan disposition. This is not evidence that live
  graph-owned resource/barrier/descriptor/perf work is implemented.
- `RenderGraph` has API-neutral resource descriptors, access declarations, and
  callback scheduling support, but `RuntimeRenderResources.h` still names
  pass-owned framebuffers, shaders, shadow payloads, and fullscreen buffers as
  runtime/pass resources released by pass reset hooks.
- `RunRender.cpp` has callback-owned pass execution for many production passes,
  but transient resource allocation is not yet live graph/backend execution and
  backbuffer/present/FBO first-state ownership remains backend/manual.
- `FramebufferDX12`, backbuffer preparation, texture/mip upload, mesh upload,
  BLAS/TLAS, DXR reflection texture, and readback still have manual transition
  or barrier hot spots recorded in the plan. Source search still finds raw
  `ResourceBarrier()` in DXR acceleration-structure code.
- Descriptor allocation still follows DX12 backend objects and per-frame
  transient descriptor ranges. The plan still needs graph-created resource
  descriptor ownership and lifetime policy.
- These remaining items are now authoritative in
  `Agentic/Plans/IN PROGRESS/render-graph-irender-interface-plan.md`, which owns
  broader graph execution/backend-interface work, including FBO/backbuffer,
  BLAS/TLAS, DXR reflection, readback state/barrier ownership, descriptor
  lifetime, transient resources, validation, and the retained perf-warning
  follow-up. Mendel accepted retiring the Carmack render graph resource plan as
  a completed callback/declaration/guardrail beachhead plus supersession record.

Current run-shell-extraction blocker:

- `run-shell-extraction-plan.md` previously reported 0 unchecked items because
  its remaining work was written as numbered phase tasks, not Markdown
  checkboxes. Source review still shows `Run` wiring the broad runtime graph and
  `RuntimeRenderHost` acting as a compatibility bridge. The plan now has
  explicit open completion gates for Phases 1-4 and must stay in `IN PROGRESS`
  until those gates are completed or superseded.
- 2026-06-29 rubber-duck verdict from Darwin: keep this plan in
  `IN PROGRESS`. Passing `python tools\check_runtime_boundaries.py` proves the
  guardrails pass, not that the architecture gates are complete. Current source
  still shows broad `RuntimeRenderHost` runtime/world/scene/replay/tool/UI/
  diagnostics bindings and callbacks in
  `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`.

Current runtime-interaction blocker:

- `runtime-interaction-state-machine-hardening-plan.md` previously reported
  0 unchecked items because its remaining work was prose/phase tasks. Source
  review still shows `Run::TakeInput()` / `RouteRuntimePointerInput(...)`
  owning tool-specific routing, launcher fire, replay target picking, and
  compatibility cleanup. The plan now has explicit open completion gates and
  must stay in `IN PROGRESS`.
- 2026-06-29 rubber-duck verdict from Darwin: keep this plan in
  `IN PROGRESS`. The remaining work is source-backed, including
  `RouteRuntimePointerInput(...)` sequentially handling editor world click,
  mouse pickup, attached-camera click, replay path picking, and launcher fire
  from `SkullbonezSource/Runtime/RunInput.cpp`.

Superseded broad roadmaps moved to `Done`:

- `engine-architecture-next-steps-plan.md` is superseded by the narrower active
  owner plans and is not implementation completion evidence.
- `game-model-data-boundary-plan.md` is superseded by
  `physics-game-model-authority-plan.md` and the Carmack physics plan.
- `global-service-context-plan.md` is superseded by the Carmack global-service
  plan, which still owns the active open work.

Current large active-plan blockers:

- `physics-game-model-authority-plan.md` still has 140 open top-level checklist
  items. It owns the broad GameModel/physics authority migration beyond the
  Carmack standalone API slices; source still shows compatibility body-handle
  mapping, model-index bridges, and store authority work remaining.
- `render-graph-irender-interface-plan.md` still has 114 open top-level
  checklist items. It owns broader graph execution/backend-interface work beyond
  the Carmack render-graph resource-ownership notes; transient resource
  allocation, graph-owned barriers, descriptor lifetime, and many pass families
  remain open.

## 2026-06-29 Deferred Plan Owner Index

This section is the active owner for source-backed work that is too broad to
complete in the current plan-clearing pass. A plan moved to `Done` under this
section is an archived/deferred coordination record, not evidence that the
underlying source migration is implemented.

Deferred source work must be resumed from the bullets below and the archived
plan linked by each bullet. Before deleting any bullet here, inspect current
source again and prove the specific gate is complete or intentionally
superseded by a newer authoritative plan.

### Physics / GameModel Authority

Archived plan after transfer:
`Agentic/Plans/Done/physics-game-model-authority-plan.md`

Transferred open count: 140 unchecked checklist items.

Active owner scope:

- Finish stable entity/body/collider/render-instance identity and invalidation
  rules. Source evidence at transfer time still included compatibility
  `HandleForModelIndex(...)`, `ModelIndexForHandle(...)`, `legacyModelIndex`,
  and `PhysicsModelAccess` paths across physics stores, scene, world, ragdoll,
  diagnostics, replay/editor/runtime integration, and render projection.
- Promote `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` from
  compatibility snapshots in `GameModelCollection` order to authoritative
  owners. At transfer time, `PhysicsBodyStore`, `ColliderStore`, and
  `RenderInstanceStore` comments and methods still described compatibility
  handle mirroring and model-index writeback.
- Remove or shrink `GameModelCollection` / `GameModel` as production physics and
  render authority. At transfer time, `GameModelCollection` still implemented
  `PhysicsModelAccess`, exposed compatibility model mutation/read paths, and
  used `GameModelCollectionPhysicsAdapter` for legacy model-index commands.
- Finish scene/editor/replay/runtime handle storage so durable handles, not raw
  model indices, cross subsystem boundaries.
- Preserve deterministic physics and replay behavior. Required future PR gates
  remain `tools\validate_physics.bat`; add `tools\validate_perf.bat` for hot
  path/storage-order changes and `tools\validate_full.bat` when runtime or scene
  behavior changes.

### Render Graph / `IRenderBackend` Interface

Archived plan after transfer:
`Agentic/Plans/Done/render-graph-irender-interface-plan.md`

Transferred open count: 114 unchecked checklist items.

Active owner scope:

- Finish graph pass declarations, diagnostics, and execution reporting so every
  production pass has stable names plus declared reads/writes/imported resources
  and missing declarations fail early.
- Move remaining pass families and transient resources into graph-owned
  scheduling/allocation where appropriate. At transfer time, many production
  passes, transient render targets, framebuffer/backbuffer/present paths,
  readback/capture paths, DXR reflection, BLAS/TLAS, and descriptor lifetime
  policy remained backend/manual or partially diagnostic.
- Replace `DiagnosticOnly` graph barrier policy with graph-owned execution where
  the graph can safely own the transition. Document backend exceptions
  explicitly where DX12-owned objects must retain manual barriers.
- Continue splitting the wide `IRenderBackend` facade into narrow capabilities
  without adding new API-family abstraction layers. Required future PR gates
  remain `tools\validate_dx12_renderer.bat`; add `tools\validate_full.bat` for
  broad runtime/render host changes and `tools\validate_perf.bat` for pass-order
  or resource lifetime changes.
- Investigate the retained `PHYSICS_BENCH Perf: 3959e0b1 vs 14795e0` warning
  before claiming graph/resource ownership perf-clean.

### Run Shell Extraction

Archived plan after transfer:
`Agentic/Plans/Done/run-shell-extraction-plan.md`

Transferred open count: 5 unchecked completion gates.

Active owner scope:

- Complete or supersede Phase 1 render-host service narrowing.
- Complete or supersede Phase 2 tool/replay behavior extraction.
- Complete or supersede Phase 3 scene-lifecycle side-effect extraction.
- Complete or supersede Phase 4 compatibility-surface collapse.
- Recheck success criteria against `Run.h`, `RuntimeRenderHost.h`, and
  runtime-boundary guardrails before deleting this TODO section.

Transfer evidence:

- Darwin's 2026-06-29 rubber-duck review blocked treating the plan as
  source-complete. Source still showed broad `RuntimeRenderHost` runtime/world/
  scene/replay/tool/UI/diagnostics bindings and callback surface.
- `python tools\check_runtime_boundaries.py` passed with 0 errors at transfer
  time; that proves guardrails are passing, not that extraction work is done.

### Runtime Interaction State Machine

Archived plan after transfer:
`Agentic/Plans/Done/runtime-interaction-state-machine-hardening-plan.md`

Transferred open count: 5 unchecked completion gates.

Active owner scope:

- Finish migrating launcher, manipulator, editor, inspect, and replay tools into
  explicit tool/gesture owners outside `Run::TakeInput()`.
- Replace remaining direct tool-specific world-click branches in
  `RouteRuntimePointerInput(...)` with controller/router decisions and commands.
- Make replay scrub/live hold, velocity edit, prediction, cause-tree, and
  path/branch target interactions fully gesture-owned.
- Verify manipulator physics policy is explicit and does not depend on inspect
  step-held behavior.
- Recheck final acceptance criteria and manual matrix before deleting this TODO
  section.

Transfer evidence:

- Darwin's 2026-06-29 rubber-duck review blocked treating the plan as
  source-complete. Source still showed `Run::TakeInput()` and
  `RouteRuntimePointerInput(...)` owning tool-specific routing, launcher fire,
  replay target picking, and compatibility cleanup.
- `python tools\check_runtime_boundaries.py` passed with 0 errors at transfer
  time; that proves guardrails are passing, not that interaction ownership is
  done.

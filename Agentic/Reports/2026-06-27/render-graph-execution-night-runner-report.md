# Render Graph Execution Night Runner Report

Date: 2026-06-27
Branch: `Night-Runner-27th-June`
Plan: `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`
Impact area: DX12 renderer, render graph, runtime render orchestration,
diagnostics, validation tooling

## Outcome

The render graph can now own command callback execution. The graph still owns
the existing DX12 barrier vocabulary and transition diagnostics, and this slice
adds the missing executable pass path:

- `RenderGraphPassExecutionOwner` distinguishes declaration-only passes from
  callback-owned passes.
- `RenderGraphPassContext` gives callbacks only graph/pass/debug-label context.
- `RenderGraph::SetPassCallback` registers a raw function pointer plus
  caller-owned user data.
- `RenderGraph::ExecuteCallbacks` dry-runs or executes callback-owned passes in
  graph order.
- Enabled callback-owned passes are rejected unless they declare at least one
  resource read or write.
- `RuntimeRenderer` now routes cinematic `ToneMapPass` through the executable
  graph path.

The migrated tonemap body is intentionally unchanged. Shader binding, texture
slot order, depth/blend state, viewport setup, and full-screen draw logic remain
inside `TonemapPass::Render`.

## Implemented Ownership Boundary

| Area | Current owner after this slice | Compatibility still present |
|------|--------------------------------|-----------------------------|
| Callback registration | `RenderGraph::SetPassCallback` | Runtime owns the caller-side callback data lifetime. |
| Callback ordering | `RenderGraph::ExecuteCallbacks` | Only `ToneMapPass` is callback-owned so far. |
| Callback validation | `RenderGraph::ExecuteCallbacks(DryRun)` | The graph validates declarations, not semantic shader correctness. |
| Tonemap command call | Executable graph callback | The pass body remains `TonemapPass::Render`. |
| Barrier execution | Existing DX12 graph-owned helpers | This slice did not change transition emission. |
| Frame graph diagnostics | `RenderPipeline` snapshot dump | Other pass families remain declaration-only. |

## Why ToneMapPass First

`ToneMapPass` is the safest first execution target:

- It is a full-screen pass.
- It does not own present.
- It does not dispatch DXR.
- It does not change object, terrain, shadow, or water batching.
- It has a small resource surface: scene color, scene depth, optional
  volumetric light, and the swapchain backbuffer.
- The existing pass body could be invoked unchanged from a callback.

## Diagnostic Evidence

Focused cinematic command:

```text
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\cinematic_volumetric.scene.json --fixed-step --vsync off
```

Result:

```text
RENDER_GRAPH_CINEMATIC_EXIT=0
RENDER_GRAPH_CINEMATIC_SECONDS=2.209
Log: TestOutput\validation\night_runner_render_graph_cinematic_volumetric.log
Screenshot: Profile\cinematic_volumetric.bmp (5,143,326 bytes)
```

`Debug\dx12_frame_graph_actual.txt` confirmed:

```text
tonemap_callback_owned=true
[8] ToneMapPass queue=Graphics barriers=DiagnosticOnly execution=Callback callback_enabled=true debug_label=Frame/Render/Tonemap
read  CinematicSceneColor as PixelShaderResource
read  CinematicSceneDepth as PixelShaderResource
read  VolumetricLight as PixelShaderResource
write SwapchainBackbuffer as RenderTarget
```

The same dump retained transition diagnostics before `ToneMapPass` and
`Present`, including `VolumetricLight RenderTarget -> PixelShaderResource` and
`SwapchainBackbuffer Present -> RenderTarget`.

## Validation Evidence

Focused iteration build:

```text
tools\validate_build.bat Profile
PASS: Build Profile|x64 succeeded.
Build succeeded.
0 Warning(s)
0 Error(s)
RENDER_GRAPH_PROFILE_BUILD_EXIT=0
RENDER_GRAPH_PROFILE_BUILD_SECONDS=12.975
Log: TestOutput\validation\night_runner_render_graph_profile_build_02.log
```

Architecture tests:

```text
tools\validate_dx12_arch_tests.bat
PASS: all DX12 architecture unit tests passed.
DX12_ARCH_TESTS_EXIT=0
DX12_ARCH_TESTS_SECONDS=4.559
Log: TestOutput\validation\night_runner_render_graph_dx12_arch_tests_final.log
```

Fast gate:

```text
tools\validate_fast.bat
PASS: All source files correctly formatted.
PASS: Project filter validation passed.
PASS: Runtime boundary validation passed.
Build succeeded.
0 Warning(s)
0 Error(s)
RENDER_GRAPH_VALIDATE_FAST_FINAL_EXIT=0
RENDER_GRAPH_VALIDATE_FAST_FINAL_SECONDS=66.826
Log: TestOutput\validation\night_runner_render_graph_validate_fast_final.log
```

DX12 renderer gate:

```text
tools\validate_dx12_renderer.bat
DX12 validation errors: 0
PASS: DX12 InfoQueue reported 0 validation errors.
PASS: DX12 screenshots match committed baselines.
RENDER_GRAPH_VALIDATE_DX12_EXIT=0
RENDER_GRAPH_VALIDATE_DX12_SECONDS=17.599
Log: TestOutput\validation\night_runner_render_graph_validate_dx12_renderer.log
Manifest: TestOutput\validation\dx12_renderer\20260627T124827Z\manifest.json
```

Full gate:

```text
tools\validate_full.bat
PASS: Project filter validation passed.
PASS: Runtime boundary validation passed.
DX12 validation errors: 0
PASS: DX12 screenshots match committed baselines.
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
RENDER_GRAPH_VALIDATE_FULL_EXIT=0
RENDER_GRAPH_VALIDATE_FULL_SECONDS=27.185
Log: TestOutput\validation\night_runner_render_graph_validate_full.log
```

Perf gate:

```text
tools\validate_perf.bat
RENDER_GRAPH_VALIDATE_PERF_EXIT=0
RENDER_GRAPH_VALIDATE_PERF_SECONDS=22.015
Log: TestOutput\validation\night_runner_render_graph_validate_perf.log
```

Perf warning acceptance:

- The perf script completed with exit 0, but it is warning-bearing evidence.
- DX12 perf comparison was skipped because this machine label differs from the
  committed baseline machine.
- The repeated `physics_bench` warning shape is the same known baseline
  comparison issue from Plan 2: frame/render/vsync/memory failures are reported
  against commit `14795e0`.
- `Frame/Physics` stayed within noise in this run: avg +2.7%, p50 +1.5%.
- The migrated callback path only runs during cinematic tonemap, while the
  flagged `physics_bench` scene does not exercise cinematic tonemap ownership.
- No perf baseline files were updated.

## Checklist Reconciliation

- [x] Startup contract was run before source edits.
- [x] Pre-existing dirty files were checked; Plan 2 was committed and pushed
      before Plan 3 edits began.
- [x] Existing render graph barrier-completion plan was read.
- [x] Callback API exists and validates resource declarations.
- [x] CPU architecture tests cover callback ordering, dry-run, disabled
      callbacks, and missing declarations.
- [x] `ToneMapPass` is the first production callback-owned pass.
- [x] The focused cinematic frame graph dump proves callback ownership.
- [x] Comment-style audit is recorded in
      `Agentic/Reports/2026-06-27/render-graph-execution-comment-audit.md`.
- [x] Source plan checklist is reconciled in
      `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`.
- [x] Final validation gates passed: arch tests, fast, DX12 renderer, full, and
      perf with documented warnings.

## Residual Follow-Up

The graph is executable, but the entire renderer is not graph-executed yet.
Future work should migrate remaining pass families one at a time, add transient
resource descriptors and lifetime intervals, then move selected render targets
to graph-owned transient allocation before removing direct runtime pass
scheduling.

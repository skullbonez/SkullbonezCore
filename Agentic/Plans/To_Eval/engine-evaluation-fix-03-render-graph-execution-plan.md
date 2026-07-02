# Engine Evaluation Fix 03: Render Graph Execution Ownership

Date: 2026-06-27
Status: Completed Night Runner implementation slice
Source finding: the render graph owned DX12 barrier vocabulary, but pass
command recording still had no executable graph-owned path.
Impact area: DX12 renderer, render graph, runtime render orchestration,
diagnostics, validation tooling

## Completed Goal

Make the render graph executable and move one low-risk full-screen production
pass into graph callback ownership without changing shader binding, draw logic,
visual baselines, or DX12 barrier emission.

Completed shape:

```text
RenderGraph
  Declares resources and passes, now with callback ownership metadata.

RenderGraph::ExecuteCallbacks
  Dry-runs callback declarations or executes enabled callbacks in pass order.

RuntimeRenderer
  Builds a small executable graph for ToneMapPass and lets the graph call the
  existing pass body.

RenderPipeline diagnostics
  Records tonemap_callback_owned=true and execution=Callback in the executed
  frame graph dump.
```

This slice intentionally proves executable graph ownership with `ToneMapPass`
first. Broader pass-family migration, transient allocation, and direct runtime
pass-order removal remain follow-up work after this validated callback path.

## Non-Goals Honored

- [x] Do not change visual output during callback migration.
- [x] Do not redesign materials, shaders, or root signatures.
- [x] Do not add Vulkan, Metal, DX11, or OpenGL abstractions.
- [x] Do not move scene loading, physics, input, or UI state into the graph.
- [x] Do not migrate object, terrain, water, shadow, reflection, or debug pass
      families in the first callback slice.
- [x] Do not weaken graph-owned DX12 barrier diagnostics.

## Phase 0: Baseline And Inventory

- [x] Run the Agent Startup Contract before editing.
- [x] Run `git status --short --branch`; Plan 2 was committed and pushed before
      Plan 3 edits began.
- [x] Read this plan,
      `Agentic/Plans/Done/dx12-render-graph-completion-plan.md`, and
      `Agentic/Plans/dx12-final-architecture-next-steps.md`.
- [x] Inspect `RenderGraph`, `Dx12RenderGraphExecutor`, `RenderPipeline`,
      `RuntimeRenderer::RenderFrame`, and the pass implementations in
      `RunPasses.cpp`.
- [x] Confirm existing graph-owned barrier work was already completed by the
      prior DX12 render graph plan.
- [x] Select `ToneMapPass` as the first callback-owned pass because it is a
      contained full-screen pass with explicit scene color, depth,
      volumetric-light, and backbuffer resources.
- [x] Write the dated implementation report:
      `Agentic/Reports/2026-06-27/render-graph-execution-night-runner-report.md`.

Validation checklist:

- [x] Source inventory itself required no validation.
- [x] Source changes are covered by the validation evidence below.

## Phase 1: Executable Pass Callback API

- [x] Add `RenderGraphPassExecutionOwner` to distinguish declaration-only
      passes from callback-owned passes.
- [x] Add `RenderGraphPassContext` with graph, pass, pass index, debug label,
      and dry-run state.
- [x] Add a raw function-pointer callback API instead of `std::function` so
      callback ownership does not force closure allocation.
- [x] Add `RenderGraphCallbackExecutionMode::DryRun` for side-effect-free
      validation.
- [x] Add `RenderGraph::SetPassCallback`.
- [x] Add `RenderGraph::ExecuteCallbacks`.
- [x] Reject enabled callback-owned passes that declare no resource reads or
      writes.
- [x] Preserve the existing barrier compile and DX12 transition executor path.
- [x] Show `execution=Callback`, callback enabled state, and debug label in
      graph dumps.

Validation checklist:

- [x] `tools\validate_dx12_arch_tests.bat`
- [x] `tools\validate_fast.bat`

## Phase 2: Architecture Tests

- [x] Test callback execution order.
- [x] Test dry-run validation does not invoke callbacks.
- [x] Test disabled callbacks are counted but not invoked.
- [x] Test enabled callback-owned passes without resource declarations are
      rejected.
- [x] Keep tests CPU-only with no D3D12 device requirement.

Validation checklist:

- [x] `tools\validate_dx12_arch_tests.bat`

## Phase 3: ToneMapPass Callback Migration

- [x] Add `RuntimeRenderer::ExecuteTonemapThroughRenderGraph`.
- [x] Declare `CinematicSceneColor` read.
- [x] Declare `CinematicSceneDepth` read.
- [x] Declare `VolumetricLight` read only when the volumetric pass produced it.
- [x] Declare `SwapchainBackbuffer` render-target write.
- [x] Run graph callback dry-run before the live callback.
- [x] Execute the existing `TonemapPass::Render` body through the callback.
- [x] Preserve the existing `sceneAlreadyUnbound` and `volumetricReady`
      semantics.
- [x] Preserve shader binding, texture slot order, viewport, depth, blend, and
      draw logic.
- [x] Leave the broader render pass order in `RuntimeRenderer` unchanged.

Validation checklist:

- [x] `tools\validate_build.bat Profile`
- [x] `tools\validate_dx12_renderer.bat`
- [x] `tools\validate_full.bat`

## Phase 4: Diagnostics

- [x] Add `RenderSceneSnapshot::tonemapCallbackOwned`.
- [x] Include `tonemapCallbackOwned` in snapshot equality.
- [x] Record `tonemap_callback_owned=true` in
      `Debug/dx12_frame_graph_actual.txt` after the focused cinematic launch.
- [x] Mark diagnostic `ToneMapPass` as `execution=Callback` only when the live
      frame used the callback path.
- [x] Preserve transition dump output for scene color, scene depth, volumetric
      light, backbuffer, and present transitions.

Validation checklist:

- [x] Focused cinematic launch:
      `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\cinematic_volumetric.scene.json --fixed-step --vsync off`
- [x] Confirm `Profile\cinematic_volumetric.bmp` was written.
- [x] Confirm `Debug\dx12_frame_graph_actual.txt` contains
      `tonemap_callback_owned=true`.
- [x] Confirm `Debug\dx12_frame_graph_actual.txt` contains
      `ToneMapPass ... execution=Callback`.

## Phase 5: Validation And Review

- [x] Run `git diff --check`.
- [x] Run `tools\validate_dx12_arch_tests.bat`.
- [x] Run `tools\validate_fast.bat`.
- [x] Run `tools\validate_dx12_renderer.bat`.
- [x] Run `tools\validate_full.bat`.
- [x] Run `tools\validate_perf.bat`.
- [x] Document the known warning-bearing perf result instead of calling it
      clean evidence.
- [x] Run comment-style audit for every touched source-bearing file.
- [x] Request rubber-duck review to validate that all checkboxes are ticked and
      honest.

## Final Acceptance Checklist

- [x] Render graph callback ownership exists in production code.
- [x] Callback-owned passes can be dry-run validated without executing draw
      code.
- [x] Callback-owned passes execute in graph pass order.
- [x] Enabled callback-owned passes must declare resource use.
- [x] `ToneMapPass` command recording is called through the graph callback path
      during cinematic rendering.
- [x] `ToneMapPass` declares all resources used by the migrated call site.
- [x] Existing DX12 barrier execution remains graph-owned and unchanged.
- [x] `Debug/dx12_frame_graph_actual.txt` names the callback-owned tonemap pass.
- [x] DX12 validation reports zero errors.
- [x] DX12 screenshot baseline comparison passes.
- [x] Full validation passes.
- [x] Perf validation was run and warning acceptance is documented.
- [x] Comment-style audit was run for every touched source-bearing file.

## Agent Do-Not-Miss Checklist

- [x] Do not move multiple risky pass families in one diff.
- [x] Do not let callback storage retain broad runtime state.
- [x] Do not hide resource use inside a callback without declaring it.
- [x] Do not weaken graph diagnostics while migrating execution.
- [x] Do not alter visual baselines.
- [x] Do not skip DX12 renderer validation.
- [x] Do not skip full validation after touching `RunRender.cpp`.
- [x] Do not pretend the warning-bearing perf comparison is clean.

## Validation Evidence

- [x] `tools\validate_build.bat Profile`:
      `TestOutput\validation\night_runner_render_graph_profile_build_02.log`,
      exit 0, 12.975s.
- [x] `tools\validate_dx12_arch_tests.bat`:
      `TestOutput\validation\night_runner_render_graph_dx12_arch_tests_final.log`,
      exit 0, 4.559s.
- [x] `tools\validate_fast.bat`:
      `TestOutput\validation\night_runner_render_graph_validate_fast_final.log`,
      exit 0, 66.826s.
- [x] `tools\validate_dx12_renderer.bat`:
      `TestOutput\validation\night_runner_render_graph_validate_dx12_renderer.log`,
      exit 0, 17.599s.
- [x] `tools\validate_full.bat`:
      `TestOutput\validation\night_runner_render_graph_validate_full.log`,
      exit 0, 27.185s.
- [x] `tools\validate_perf.bat`:
      `TestOutput\validation\night_runner_render_graph_validate_perf.log`,
      exit 0, 22.015s, with documented warnings.
- [x] Focused cinematic launch:
      `TestOutput\validation\night_runner_render_graph_cinematic_volumetric.log`,
      exit 0, 2.209s.

## Follow-Up Roadmap

The executable graph path is now real, but render graph ownership is not done
for the whole renderer. Follow-up slices should:

- Move `VolumetricLightPass` next because it is the upstream full-screen
  producer for `ToneMapPass`.
- Move sky, shadow, reflection, water, debug, terrain, and object pass families
  one family at a time.
- Add transient resource descriptors and lifetime intervals before graph-owned
  allocation.
- Move one low-risk post or volumetric target to graph-owned transient
  allocation with fence-safe lifetime.
- Add a source-search guardrail once more pass families have callback ownership.
- Remove direct runtime pass scheduling only after production pass callbacks and
  transient resource lifetime have enough validation history.

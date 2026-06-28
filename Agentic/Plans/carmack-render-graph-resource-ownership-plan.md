# Carmack Render Graph Resource Ownership Plan

Date: 2026-06-28
Status: In progress
Impact area: DX12 renderer, render graph, resource lifetime, validation, performance
Validation note: plan-only edits require no validation. PR-bound render graph,
resource barrier, descriptor, render target, or screenshot-visible work requires
`tools\validate_dx12_renderer.bat`; allocation-sensitive render work also
requires `tools\validate_perf.bat`.

## Completed Slices

- [x] 2026-06-28: Moved `CinematicSceneBegin` / `SceneTargetPass::Begin` into
  render graph callback ownership with declared `CinematicSceneColor` and
  `CinematicSceneDepth` writes, a dry-run before live callback execution,
  `scene_target_callback_owned` executed-frame diagnostics, and a runtime
  boundary guardrail blocking the old direct `m_sceneTargetPass.Begin(...)`
  scheduling path. `CinematicSceneDepth` intentionally imports with
  `Unknown` initial access in this transitional graph because `FramebufferDX12`
  still owns the concrete first-frame/steady-state depth transition.
  Validation:
  `python tools\check_runtime_boundaries.py` passed in 3.40s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 57.78s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.85s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_dx12_renderer.log`);
  DX12 artifact manifests:
  `TestOutput\validation\dx12_renderer\20260628T000233Z\manifest.json` and
  `TestOutput\validation\dx12_renderer\20260628T000400Z\manifest.json`
  with screenshot comparison summaries in the same directories;
  `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --scene
  SkullbonezData\scenes\cinematic_volumetric.scene.json --cinematic` passed in
  2.15s and produced a frame graph with `scene_target_callback_owned=true`,
  `CinematicSceneBegin execution=Callback`, and `CinematicSceneDepth
  initial=Unknown`
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_cinematic_frame_graph.txt`);
  `tools\validate_full.bat` passed in 27.19s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_full.log`).
  Comment-style audit: inspected the touched source-bearing files
  (`RunRender.cpp`, `RuntimeRenderer.h`, `RenderSceneSnapshot.h`,
  `RenderPipeline.cpp`, and `check_runtime_boundaries.py`) against
  `Agentic\Reference\comment-style-guide.md`; existing learning headers remain
  present, and the new graph/FBO depth handoff is documented next to the
  declaration.
  Rubber-duck review: Poincare found no blockers, flagged the scene-depth
  initial-state assumption as non-blocking, and confirmed the `Unknown`
  handoff fix resolved that concern.

## Problem Statement

The Carmack-test verdict credited DX12 validation and render-graph progress but
did not call the render architecture finished. The graph records pass/resource
intent and can execute callback-owned passes, but transient resource ownership,
descriptor lifetime, and full production pass/resource scheduling are not yet
owned end to end by the graph.

## Goal

Make the render graph the production owner of frame pass execution, resource
state transitions, transient render-target lifetime, and validation diagnostics,
while keeping DX12-specific emission inside the backend executor.

## Success Bar

- All frame passes declare their reads, writes, and callback execution through
  the graph.
- Ordinary render targets and transient resources are allocated, reused, and
  released through graph-owned lifetime policy.
- Manual barriers are reduced to reviewed compatibility cases with guardrails.
- DX12 validation remains zero-error and screenshot baselines stay clean.

## Related Plans

- `Agentic/Plans/render-graph-irender-interface-plan.md` is the active renderer
  umbrella plan. Use this Carmack plan as the graph-resource-ownership
  acceptance checklist for that work.
- `Agentic/Plans/carmack-render-backend-capability-plan.md` covers
  capability-interface narrowing. This plan covers production pass execution,
  transient resource lifetime, descriptor lifetime, and barrier ownership.
- `Agentic/Plans/runtime-static-allocation-policy-plan.md` owns the allocation
  policy that graph transient-resource work must satisfy.

## Implementation Checklist

### Inventory

- [ ] List every `RuntimeRenderer` pass and whether it is callback-owned,
  declaration-only, or manual.
- [ ] List every render target, framebuffer, backbuffer, depth target, shadow map,
  reflection target, volumetric target, water target, and UI/dynamic buffer used
  in a frame.
- [ ] List every manual transition, UAV barrier, resource release hook, and
  backend state change that is not graph-owned.
- [ ] List all DX12 descriptor heaps, descriptor ranges, and resource handles used
  by migrated passes.
- [ ] Capture the current DX12 validation and screenshot baseline evidence before
  moving the next pass family.

### Pass Execution Ownership

- [x] Move one additional low-risk pass family under graph callback execution.
- [x] Keep pass body behavior unchanged during the first migration slice.
- [x] Require each callback-owned pass to declare at least one read or write.
- [x] Validate callback dry-run before execute mode.
- [x] Record callback-owned status in frame graph diagnostics.
- [ ] Repeat pass migration in small slices until every production pass is graph-owned.

### Resource Declaration

- [ ] Give every frame resource a stable graph name.
- [ ] Assign concrete `RenderGraphResourceAccess` values for every read/write.
- [ ] Replace `Unknown` access with explicit initial or imported states where the
  backend can know them.
- [ ] Add subresource declarations where one texture uses different subresources.
- [ ] Keep native DX12 resource identity diagnostic-only in API-neutral graph records.

### Transient Resource Lifetime

- [ ] Add transient resource descriptors for graph-owned render targets.
- [ ] Add a graph allocator or backend executor path that creates transient DX12
  resources from descriptors.
- [ ] Reuse transient resources only when lifetimes do not overlap and descriptor
  compatibility is proven.
- [ ] Release transient resources through graph/backend lifetime policy, not pass
  destructors scattered across runtime code.
- [ ] Record resource allocation, reuse, high-water, and release diagnostics.
- [ ] Ensure transient allocation does not introduce steady-frame heap growth.

### Barrier Ownership

- [ ] Route graph-compiled transition records through the DX12 graph executor for
  live barrier emission.
- [ ] Route UAV ordering through explicit graph-owned policy.
- [ ] Remove pass-local or backend-local barriers after graph output is proven
  equivalent.
- [ ] Add diagnostics that compare expected graph transitions with emitted DX12
  barriers during validation.
- [ ] Fail validation on unknown states that should be concrete by the migrated
  phase.

### Descriptor And Resource Binding

- [ ] Name which system owns descriptor allocation for graph-created resources.
- [ ] Make descriptor lifetime follow graph resource lifetime.
- [ ] Keep material/object descriptor tables separate from transient frame target
  descriptors.
- [ ] Ensure graph-owned resources can be sampled, rendered into, and captured by
  screenshot validation without ad hoc backend lookups.

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to reject direct scheduling of
  passes that have migrated to graph callback ownership.
- [ ] Add guardrails for new manual barriers in migrated pass families.
- [ ] Add guardrails for new `Unknown` access in migrated resources unless
  explicitly allowlisted.
- [x] Add synthetic checker tests for migrated pass scheduling and manual-barrier
  rejection.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [x] After each pass migration: run `tools\validate_dx12_renderer.bat`.
- [x] For barrier or resource lifetime changes: run `tools\validate_dx12_renderer.bat` and verify `dx12_validation.txt` is zero-error.
- [ ] For transient allocation or descriptor storage changes: run `tools\validate_perf.bat`.
- [x] For broad runtime render host changes: run `tools\validate_full.bat`.
- [x] Save manifest paths and screenshot diff artifacts in the handoff.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to verify graph declarations match actual pass behavior.
- [x] Ask the reviewer to inspect resource lifetime, descriptor lifetime, and barrier ordering.
- [x] Ask the reviewer to look for false confidence from declaration-only graph output.
- [x] Ask the reviewer to check for new per-frame allocation in graph diagnostics.
- [x] Resolve blocking review findings before committing PR-bound code.

## Definition Of Done

- [ ] Production frame pass execution is graph-owned.
- [ ] Transient frame resources are graph-owned or explicitly imported.
- [ ] Manual barriers are gone from migrated pass families or explicitly reviewed.
- [x] DX12 validation and screenshot baselines pass after each slice.
- [ ] Performance evidence shows graph ownership did not introduce recurring
  steady-frame allocation or measurable hot-path regression.

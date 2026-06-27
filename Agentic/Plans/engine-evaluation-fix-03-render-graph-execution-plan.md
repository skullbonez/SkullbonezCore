# Engine Evaluation Fix 03: Render Graph Execution Ownership

Date: 2026-06-27
Status: Draft implementation plan
Source finding: the render graph owns important DX12 barrier behavior, but it is
still not the full owner of pass execution, transient resources, and frame
resource lifetime.
Impact area: DX12 renderer, render graph, render pass orchestration, runtime
render host, shader/resource lifetime, validation tooling
Validation for this document-only change: none required

## Goal

Move from "runtime calls passes and the graph applies barriers" to "the graph
owns pass order, resource use declarations, transition execution, pass
callbacks, and frame diagnostics."

Target shape:

```text
RuntimeRenderer
  Builds a RenderFrameDescription from scene/replay/tools/UI state.

RenderGraphBuilder
  Declares imported resources, transient resources, passes, reads, writes, UAV
  ordering, and callbacks.

RenderGraphExecutor
  Compiles dependencies, applies DX12 transitions, executes pass callbacks in
  order, emits telemetry, and records validation diagnostics.

DX12 backend
  Owns native command list/resource emission details but not frame pass order.
```

This plan follows the completed
`Agentic/Plans/Done/dx12-render-graph-completion-plan.md`. That completed plan
moved production barrier ownership to graph-owned DX12 helpers. This plan picks
up the intentionally deferred pass-callback and resource-lifetime work.

## Why This Matters

The current graph is valuable, but it is still easy for rendering architecture
to drift:

- pass bodies and resource declarations can diverge,
- optional paths can miss graph declarations,
- transient resource lifetime remains pass/backend owned,
- runtime render orchestration still knows too much about DX12-sensitive pass
  sequencing,
- future render features can bypass graph vocabulary if no executable graph
  path exists.

The fix is not a wholesale renderer rewrite. It is a pass-by-pass migration
where the first callback is deliberately low risk.

## Current Anchors To Inspect

- `SkullbonezSource/Rendering/RenderGraph.h`
- `SkullbonezSource/Rendering/RenderGraph.cpp`
- `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`
- `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- `SkullbonezSource/Rendering/RenderPipeline.h`
- `SkullbonezSource/Rendering/RenderPipeline.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `tools/validate_dx12_renderer.bat`

## Non-Goals

- Do not change visual output during callback migration.
- Do not redesign materials, shaders, or root signatures as part of the first
  graph-execution slice.
- Do not add Vulkan/Metal abstractions.
- Do not move scene loading, physics, input, or UI state ownership into the
  render graph.
- Do not allocate per-frame heap objects for pass callbacks unless measured and
  justified.
- Do not remove graph-owned barrier telemetry while migrating callbacks.

## Design Rules

- Migrate one pass family at a time.
- Keep pass body code recognizable during the first callback migration.
- A pass callback must declare every resource it reads or writes.
- Imported resources and transient resources must be labeled differently.
- DX12 native types stay in DX12-specific executor/backend code.
- Graph diagnostics are part of the feature, not optional debug noise.
- Every source-bearing implementation slice must follow
  `Agentic/Reference/comment-style-guide.md`.
- Use the repo-local orchestrator skill before implementing this plan unless
  the user explicitly asks to bypass it.

## Phase 0: Verify Current Graph And Barrier Baseline

Purpose: ensure the starting point really matches the completed barrier plan.

Checklist:

- [ ] Run the Agent Startup Contract from `AGENTS.md`.
- [ ] Run `git status --short --branch` and record pre-existing dirty files as
      user-owned.
- [ ] Read this plan,
      `Agentic/Plans/Done/dx12-render-graph-completion-plan.md`, and
      `Agentic/Plans/dx12-final-architecture-next-steps.md`.
- [ ] Search for raw `ResourceBarrier(` calls and confirm they are limited to
      the graph executor and documented low-level D3D12 build helpers.
- [ ] Search for old generic `TransitionBarrier(` production usage.
- [ ] Inspect `Debug/dx12_frame_graph_actual.txt` generation and confirm it is
      built from the executed frame path.
- [ ] Inspect graph telemetry fields for pass, resource, subresource, before
      access, after access, and source label.
- [ ] Inventory every render pass and mark whether it is:
      graph-declared only, graph-barrier-owned, callback-owned, or not yet in
      graph vocabulary.
- [ ] Write or update a dated report under `Agentic/Reports/` with the pass
      ownership inventory.

Validation checklist:

- [ ] Documentation-only inventory needs no repository validation.
- [ ] If diagnostics or validation tooling changes, run
      `tools\validate_fast.bat` and `tools\validate_dx12_renderer.bat`.

## Phase 1: Add Executable Pass Callback API

Purpose: make the graph capable of owning command recording without moving a
real pass yet.

Checklist:

- [ ] Add a callback-capable pass descriptor or parallel executable-pass
      structure to `RenderGraph`.
- [ ] Define a small `RenderGraphPassContext` that exposes only the command
      recording services a pass needs.
- [ ] Avoid storing broad runtime state in callbacks.
- [ ] Avoid heap allocation in normal per-frame pass registration where
      practical.
- [ ] Add support for pass-local debug labels and telemetry names.
- [ ] Keep existing barrier execution path unchanged.
- [ ] Add architecture tests in `Dx12ArchUnitTests` for callback registration,
      callback ordering, disabled/culled passes, and missing resource
      declarations.
- [ ] Add a dry-run mode that compiles and validates callbacks without executing
      them.

Validation checklist:

- [ ] `tools\validate_fast.bat`
- [ ] `tools\validate_dx12_renderer.bat`
- [ ] If per-frame allocation behavior changes, add `tools\validate_perf.bat`.

## Phase 2: Move One Low-Risk Fullscreen Pass Into A Callback

Purpose: prove executable graph ownership with minimal rendering risk.

Preferred first candidates:

1. Tonemap or final fullscreen resolve.
2. Volumetric combine pass.
3. A similarly contained fullscreen pass with simple read/write resources.

Selection checklist:

- [ ] The pass has a small, explicit resource set.
- [ ] The pass does not own swapchain present.
- [ ] The pass does not own DXR dispatch.
- [ ] The pass does not change object/terrain/water batching.
- [ ] Existing graph telemetry already names its resources.
- [ ] The pass body can be called unchanged from the callback.

Implementation checklist:

- [ ] Declare every resource read and write by the selected pass.
- [ ] Move only the pass call site into graph callback execution.
- [ ] Keep shader binding and draw logic unchanged in the first slice.
- [ ] Preserve depth, blend, viewport, render target, and descriptor state
      restore behavior.
- [ ] Emit callback telemetry before and after pass execution.
- [ ] Keep a rollback path that restores the direct pass call.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat`
- [ ] Confirm `dx12_validation.txt` reports zero errors.
- [ ] Confirm screenshot baselines match.
- [ ] Inspect `Debug/dx12_frame_graph_actual.txt` and confirm the selected pass
      is callback-owned.

## Phase 3: Add Transient Resource Metadata

Purpose: prepare graph-owned frame resource lifetime without changing all
allocation policy in one step.

Checklist:

- [ ] Distinguish imported resources from graph-created transient resources.
- [ ] Add transient resource descriptors for size, format, sample count, bind
      flags, clear value, and debug name.
- [ ] Add lifetime intervals from first use to last use.
- [ ] Add aliasing eligibility metadata but do not alias resources until
      telemetry proves lifetimes are correct.
- [ ] Add descriptor requirements for RTV, DSV, SRV, UAV, and readback uses.
- [ ] Keep current resource owners as compatibility providers during the first
      metadata slice.
- [ ] Add tests for transient lifetime ranges and imported-vs-transient misuse.

Validation checklist:

- [ ] `tools\validate_fast.bat`
- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_perf.bat` if graph compile or allocation cost changes.

## Phase 4: Migrate Post, Sky, Shadow, Reflection, Water, And Debug Passes

Purpose: move pass families with clear resource boundaries before object-heavy
paths.

Checklist:

- [ ] Move volumetric and tonemap pass callbacks after the first pass proves
      stable.
- [ ] Move sky pass callback and verify depth state expectations.
- [ ] Move shadow pass callback and verify depth-only target transitions.
- [ ] Move raster reflection pass callback and verify reflection target
      sampling by water.
- [ ] Move DXR reflection callback only after raster reflection and UAV
      telemetry are stable.
- [ ] Move water pass callback and verify reflection, depth, blend, and known
      intersection diagnostics are preserved.
- [ ] Move debug overlay callbacks and ensure they do not contaminate production
      material state.
- [ ] Keep each pass family in its own commit or tightly scoped pair.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat` after each pass family.
- [ ] Add focused launches or screenshots for water/reflection if the standard
      render suite does not cover the touched path enough.
- [ ] `tools\validate_full.bat` if runtime pass orchestration changes broadly.

## Phase 5: Migrate Object, Terrain, And Hot Render Paths

Purpose: move the highest-impact production paths only after graph callbacks
and telemetry are proven.

Checklist:

- [ ] Move terrain callback and preserve terrain-hidden, style, texture, and
      physics-terrain visual behavior.
- [ ] Move object/model callback and preserve batching, material table binding,
      instancing, shadows, and collision-visualizer mode.
- [ ] Move launcher/tracer/editor render overlays only after runtime owner
      views are narrow enough.
- [ ] Confirm graph callback execution does not add per-object or per-draw
      allocations.
- [ ] Confirm pass callbacks consume render-facing views rather than
      `RuntimeRenderHost` internals.
- [ ] Confirm material/shader binding order remains equivalent.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_perf.bat`
- [ ] `tools\validate_full.bat`
- [ ] Include manual hot-path review notes for allocation-sensitive code.

## Phase 6: Make Graph-Owned Transient Allocation Real

Purpose: let the graph own temporary frame targets after pass callback
ownership is stable.

Checklist:

- [ ] Choose one low-risk transient target, such as a post/volumetric target.
- [ ] Allocate it through graph resource metadata instead of pass-local owner.
- [ ] Tie resource reuse to frame fence safety.
- [ ] Preserve named descriptor accounting and exhaustion diagnostics.
- [ ] Keep imported swapchain, persistent textures, and acceleration-structure
      resources out of transient ownership.
- [ ] Add telemetry for transient allocation count, size, aliasing disabled or
      enabled, and lifetime interval.
- [ ] Only enable aliasing after non-aliased graph allocation validates cleanly.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_perf.bat`
- [ ] `tools\validate_full.bat` if lifetime or resize behavior changes.
- [ ] For frame allocator/upload lifetime-sensitive changes, run the DX12 gate
      three consecutive times before PR completion.

## Phase 7: Remove Direct Runtime Pass Scheduling

Purpose: make graph execution the normal render path.

Checklist:

- [ ] Replace direct pass call order in runtime renderer with graph build plus
      graph execute.
- [ ] Keep `RuntimeRenderer` responsible for building frame descriptions, not
      issuing individual pass draw calls.
- [ ] Remove obsolete compatibility direct-call paths after graph callback
      ownership is complete.
- [ ] Update frame graph diagnostics so callback ownership is obvious.
- [ ] Add a validation/source-search guardrail that prevents new production
      passes from bypassing graph declaration.
- [ ] Update architecture docs and session state with the new graph ownership
      boundary.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_perf.bat`
- [ ] `tools\validate_full.bat`
- [ ] Confirm `dx12_validation.txt` is zero-error.
- [ ] Confirm screenshot baselines match.
- [ ] Confirm no new per-frame allocation spike in perf evidence.

## Final Acceptance Checklist

- [ ] Every production render pass is declared in the executable graph.
- [ ] Every production render pass callback declares all read/write resources.
- [ ] Graph execution owns pass order and DX12 transition application.
- [ ] Direct runtime pass scheduling is removed or limited to graph build input.
- [ ] Imported and transient resources are distinguished in graph telemetry.
- [ ] At least one transient resource family is graph-owned with fence-safe
      lifetime.
- [ ] Raw DX12 barriers remain limited to the graph executor and documented
      low-level D3D12 object-build helpers.
- [ ] `Debug/dx12_frame_graph_actual.txt` names callback-owned passes and their
      resource accesses.
- [ ] `Dx12ArchUnitTests` cover callback ordering, missing declarations,
      transient lifetimes, and disabled passes.
- [ ] Comment-style audit was run for every touched source-bearing file.
- [ ] Final PR-bound validation includes DX12 renderer, perf when needed, and
      full validation for broad orchestration changes.

## Agent Do-Not-Miss Checklist

- [ ] Do not move multiple risky pass families in one diff.
- [ ] Do not let a callback access broad runtime state to save time.
- [ ] Do not hide resource use inside a callback without declaring it.
- [ ] Do not weaken graph diagnostics while migrating execution.
- [ ] Do not create per-frame heap churn in graph build or callbacks.
- [ ] Do not alter visual baselines unless the change is intentional and
      validated.
- [ ] Do not skip focused water/reflection review when water or reflection pass
      ownership moves.
- [ ] Do not remove the rollback path for the first callback slice until it has
      passed the DX12 renderer gate.


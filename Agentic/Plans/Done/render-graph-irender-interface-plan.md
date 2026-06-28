# Render Graph / `IRenderBackend` Interface Plan

Date: 2026-06-27
Status: Deferred to `Agentic/Plans/IN PROGRESS/TODO.md`; archived plan is not source-complete.
Impact areas: DX12 renderer, render graph execution, render backend interface, runtime render host, tests
Validation for this plan edit: Documentation-only. No repository validation required.

## 2026-06-29 Deferred Archive Disposition

This plan is being archived as a deferred coordination record, not as
source-complete implementation evidence. The remaining checklist items are too
broad for the current plan-clearing pass and remain source-backed. Their active
owner is now `Agentic/Plans/IN PROGRESS/TODO.md` under
`2026-06-29 Deferred Plan Owner Index / Render Graph / IRenderBackend
Interface`.

Any checked item below that says `Transferred to TODO 2026-06-29` means the work
was moved to that TODO owner. It does not mean the underlying source migration
is implemented.

## Completed Slices

- [x] 2026-06-27: Removed DXR reflection state from `Run.h`; runtime reflection transform scratch now lives behind `RuntimeRenderHost`.
- [x] 2026-06-27: Split DXR reflection calls out of `IRenderBackend` into `IRenderRayTracing`; migrated runtime reflection setup, dispatch, and debug preview callers.
- [x] 2026-06-27: Added runtime boundary guardrails for direct `Gfx().DXR`-style calls and for reintroducing DXR declarations to `IRenderBackend.h`.
- [x] 2026-06-27: Validated the raytracing-interface slice with `tools\validate_fast.bat`, `tools\validate_dx12_renderer.bat`, and `tools\validate_full.bat`.
- [x] 2026-06-27: Moved `VolumetricLightPass` command recording into the render graph callback path and marked callback ownership in frame graph diagnostics.
- [x] 2026-06-27: Added a runtime-boundary guardrail blocking direct `VolumetricLightPass`/`ToneMapPass` scheduling after graph callback migration.
- [x] 2026-06-27: Folded `VolumetricLightPass` and `ToneMapPass` into one cinematic post render graph so the graph owns their callback order and resource dependency.
- [x] 2026-06-27: Moved `DebugOverlayPass` scheduling into a render graph callback and recorded callback ownership in executed frame graph diagnostics.
- [x] 2026-06-27: Moved `TornadoVisualPass` scheduling into a render graph callback while preserving the pass body's no-draw result.
- [x] 2026-06-28: Moved `UiTextPass` scheduling into a render graph callback; scene frame-graph diagnostics remain separate from this late overlay graph.

## Goal

Make the render graph the production owner of pass scheduling and resource lifetime while narrowing the renderer interface so callers depend on the specific capability they need instead of the wide `IRenderBackend` facade.

The target shape is:

- Runtime rendering submits work through a graph-oriented frame path.
- Render passes declare their reads, writes, attachments, imported resources, and execution callback.
- `Dx12RenderGraphExecutor` owns graph execution details for DX12: pass ordering, resource states, barriers, descriptor/resource binding coordination, and diagnostics.
- Transient graph resources are described by the graph, allocated by the DX12 executor/backend layer, and released or reused according to frame-safe lifetime rules.
- DXR reflection ownership is removed from `Run.h` and moved behind renderer/runtime render-host owned interfaces.
- `IRenderBackend` remains as a temporary compatibility facade while callers migrate.
- Narrow capability views replace the wide interface for core frame commands, resource creation, capture/readback, GPU timers/profiling markers, debug draw, dynamic geometry, and ray tracing.
- DX12-specific details stay in DX12-owned headers unless a consciously named engine-facing capability requires them.

This plan continues the completed `engine-evaluation-fix-03-render-graph-execution-plan.md` slice. That slice made graph callbacks executable and moved `ToneMapPass`; this plan moves the remaining runtime pass families and splits the backend interface.

## Non-Goals

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not add or resurrect OpenGL, DX11, or Vulkan runtime abstractions.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not create fake cross-API lowest-common-denominator interfaces.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not move every pass family in one oversized diff.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not change visual baselines unless a rendering behavior change is intentional and reviewed.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not introduce async command-list recording until graph scheduling, barriers, and lifetime are stable.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not widen `IRenderBackend` while claiming to split it.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not expose raw DX12 types through engine-facing interfaces unless the interface is explicitly DX12-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not remove the compatibility facade until all production callers have moved.
- [x] Do not leave DXR reflection state, pass helpers, or backend-specific reflection setup in `Run.h`.

## Phase 0 - Startup, Inventory, and Slice Choice

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Follow the repository Agent Startup Contract before editing.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Confirm the current branch and dirty state with `git status --short --branch`; treat pre-existing dirty files as user-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read this plan and the current handoff in `Agentic/SessionState.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read `Agentic/Plans/engine-architecture-next-steps-plan.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Skim `Agentic/Plans/codebase-top-10-cleanup-plan.md` for older renderer cleanup context, but prefer current source over old assumptions.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Choose one implementation slice only: graph pass migration, transient resource ownership, backend interface split, or cleanup guardrails.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) State the selected impact area before editing.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) State the deferred PR-gate validation command before editing.

Render graph inventory:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all current graph-owned pass callbacks.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all direct runtime pass scheduling that bypasses the graph.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all graph imported resources and their owner.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all render targets that are candidates for graph-owned transient allocation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all manually issued barriers around graph-managed passes.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all debug output files that describe frame graph execution.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify the next lowest-risk pass family to migrate.

Backend interface inventory:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) List every method in `SkullbonezSource/Rendering/IRenderBackend.h`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Group each method by caller and capability: frame/device, resource creation, swapchain/present, capture/readback, GPU timing/profiling, debug draw, dynamic geometry, ray tracing, scene submission, settings, diagnostics, or legacy.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all direct `IRenderBackend` call sites.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all global renderer accessor call sites in `IRenderBackend.cpp` and related runtime host code.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List every DXR reflection declaration, field, helper, callback, and include currently reachable from `Run.h`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify optional or no-op interface methods that should become capability queries or be deleted.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify DX12-specific concepts currently visible in engine-facing headers.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Record the inventory in a short handoff note under `Agentic/Reports/` if the slice is not completed in one sitting.

## Phase 1 - Harden the Render Graph Contract

Before moving more passes, make pass declaration and diagnostics difficult to misuse.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure each graph pass has a stable name.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure each graph pass declares every read resource.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure each graph pass declares every write resource.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure imported resources are explicitly marked as imported.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure transient resources have descriptors before allocation ownership moves to the graph.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure pass callbacks receive only the context they need.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure graph execution diagnostics show pass order.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure graph execution diagnostics show resource reads and writes.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure graph execution diagnostics show imported versus transient resources.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure missing resource declarations fail fast in debug or validation paths.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add a small dry-run or inspection path if the existing graph has one; otherwise document why it is deferred.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add comments for resource lifetime, imported ownership, and pass callback invariants in touched source.

Done when:

- [x] A migrated pass can be reviewed from its graph declarations without reading unrelated runtime scheduling code.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) The executor can report what it executed and which resources each pass touched.
- [x] A callback-owned pass with no declared graph resource handoff is detectable before it becomes a subtle DX12 hazard.

## Phase 2 - Move Runtime Pass Families into Graph Callback Ownership

Move one family at a time and validate the behavior before moving the next. The preferred next pass from prior roadmap notes is `VolumetricLightPass`.

Pass-family checklist, repeated for each selected family:

- [x] Identify the pass entry point and current direct scheduling path.
- [x] Identify every input resource.
- [x] Identify every output resource.
- [x] Identify every imported resource owner.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify every transient resource candidate.
- [x] Identify required resource states before and after the pass.
- [x] Identify CPU-side data dependencies and lifetime requirements.
- [x] Register the pass in the graph with complete read/write declarations.
- [x] Move pass execution into a graph callback.
- [x] Prove whether execution now goes through `Dx12RenderGraphExecutor`, or explicitly record any remaining callback-only/resource-transition handoff debt.
- [x] Remove or disable the old direct scheduling path for that pass.
- [x] Keep pass-specific resource setup outside the callback only when ownership requires it and document why.
- [x] Update graph diagnostics to include the pass.
- [x] Run the smallest focused build or launch needed to answer implementation questions while iterating.
- [x] Defer formal repository validation to the PR gate unless the user explicitly asks for it.

Suggested migration order:

- [x] `VolumetricLightPass`
- [x] `DebugOverlayPass`
- [x] `TornadoVisualPass`
- [x] `UiTextPass`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Low-risk post-processing passes after tone map
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Reflection or environment passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Shadow passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Water passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Sky passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Terrain passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Object/model passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Debug draw passes
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Capture/readback passes

Do-not-miss checklist:

- [x] Pass order is unchanged unless the behavior change is intentional and documented.
- [x] Every DX12 resource transition is either graph-owned or explicitly justified.
- [x] Clear/load/store behavior is preserved.
- [x] Resize behavior is preserved.
- [x] Screenshot timing remains deterministic.
- [x] DX12 validation output remains zero-error for renderer-validation slices.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Any visual baseline update is intentional, reviewed, and followed by `tools\validate_dx12_renderer.bat`.

## Phase 3 - Move Transient Resource Ownership into the Graph

Do this after enough pass families declare resources accurately.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add or complete transient resource descriptors for texture targets.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add or complete transient resource descriptors for buffer targets if needed.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define descriptor fields: dimensions, format, sample count, usage flags, clear value, and resize policy.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define imported resource descriptors separately from transient descriptors.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Track first use and last use for transient resources.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Keep aliasing disabled until lifetime intervals are proven correct, unless the slice explicitly implements aliasing validation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move one low-risk post-processing or volumetric target to graph-owned allocation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Prove resize and recreate behavior for that graph-owned target.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Prove frame-lag/fence safety for graph-owned resource release or reuse.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove the old manual allocation path for that target after validation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Repeat with more targets only after the first target is stable.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Imported swapchain/backbuffer resources remain backend-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Imported persistent history buffers remain owned by the system that needs cross-frame lifetime.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Transient resources do not outlive the frame unless explicitly promoted to persistent history.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Descriptor heap and SRV/RTV/DSV ownership is clear.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Framebuffer, backbuffer/present, BLAS/TLAS, DXR reflection, and readback
  state/barrier ownership is either graph-owned or explicitly documented as a
  DX12 backend exception after the superseded Carmack resource plan.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) GPU readback/capture does not read a released transient resource.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Resource state diagnostics name graph-owned transients clearly.

## Phase 4 - Split `IRenderBackend` into Narrow Capability Interfaces

Keep the split mechanical and caller-driven. Introduce a capability only when current callers clearly need it.

Candidate capability groups:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Core frame/device capability: begin frame, execute graph/frame commands, end frame, present, resize, frame index, synchronization.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Resource creation capability: textures, buffers, render targets, depth targets, views, upload helpers.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Capture/readback capability: screenshots, GPU readback, validation captures, baseline artifacts.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) GPU profiling capability: timers, platform markers, marker scopes, validation marker output.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Debug draw capability: lines, shapes, overlays, debug render queues.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Dynamic geometry capability: transient vertex/index uploads and per-frame geometry streams.
- [x] Ray tracing capability: DXR scene/build/update/dispatch functions.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Diagnostics/settings capability: validation flags, debug names, renderer stats, feature support.

Interface split checklist:

- [x] Name each new interface from the current source vocabulary; avoid speculative generic names.
- [x] Add the smallest interface needed for one caller group.
- [x] Back the new interface with the existing DX12 backend implementation.
- [x] Migrate one caller group to the new capability.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Leave `IRenderBackend` forwarding or exposing compatibility until all callers move.
- [x] Keep DX12-only includes out of engine-facing headers unless the new interface is DX12-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Convert optional no-op methods into explicit capability checks or remove them after callers migrate.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Avoid global access expansion; pass capability references through existing runtime ownership paths where practical.
- [x] Update comments for ownership, lifetime, thread expectations, and frame timing.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Repeat for the next caller group only after the current group is validated.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) `IRenderBackend.h` gets smaller over the full plan, not larger.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Callers no longer receive a full backend when they only need one capability.
- [x] Runtime host and renderer lifetime still destroy capabilities in a safe order.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Capture/readback paths still work for validation artifacts.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) GPU marker and profiler paths still work for `--platform-profiler-markers`.
- [x] DXR code stays isolated from non-DXR callers.
- [x] No interface split adds per-frame heap allocation in a hot path.

## Phase 5 - Clean Up Runtime Renderer and Host Wiring

Once graph execution and capability views exist, simplify the runtime path.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `RuntimeRenderer` submit graph work through the graph/executor path.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `RuntimeRenderHost` provide only the capability references needed by runtime systems.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove direct pass scheduling from runtime code after each migrated pass is graph-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove direct backend calls from pass code where a graph context or narrow capability is sufficient.
- [x] Move DXR reflection state and helper declarations out of `Run.h`.
- [x] Put DXR reflection ownership behind renderer/runtime render-host owned types with narrow inputs from gameplay/runtime state.
- [x] Remove any `Run.h` includes that exist only for DXR reflection implementation details.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove stale compatibility methods from `IRenderBackend` after final callers migrate.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove stale include dependencies caused by the old wide backend interface.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update renderer diagnostics and frame graph outputs to match the new execution path.
- [x] Add search guardrails for accidental new wide-backend dependencies, direct runtime pass scheduling regressions, and DXR reflection declarations returning to `Run.h`.

Searches to run before declaring cleanup done:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "IRenderBackend" SkullbonezSource`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "Gfx\(|GetRenderBackend|RenderBackendDX12" SkullbonezSource`
- [x] `rg "Execute.*Pass|Run.*Pass|Render.*Pass" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "RenderGraph" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "D3D12|ComPtr<ID3D12|DXGI" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "DXR|Dxr|Raytrac|Reflection" SkullbonezSource/Runtime/Run.h SkullbonezSource/Runtime/RunInternal.h`

## Phase 6 - Validation, Guardrails, and Review

Repository validation scripts are PR/commit gates. Do not run them repeatedly while iterating unless the user explicitly asks for validation.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Documentation-only changes: no repository validation required.
- [x] Render graph pass migration: run `tools\validate_dx12_renderer.bat` before PR-bound commit.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Transient resource allocation, resource barriers, descriptor lifetime, or render target ownership changes: run `tools\validate_dx12_renderer.bat`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Upload buffer, dynamic geometry, descriptor heap, or per-frame allocation changes: run `tools\validate_dx12_renderer.bat` and `tools\validate_perf.bat`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Investigate the retained `physics_bench` perf warning from
  `Agentic/Plans/IN PROGRESS/TODO.md` before claiming graph/resource ownership
  is performance-clean.
- [x] `IRenderBackend`, `RenderBackendDX12`, `Rendering/DX12/*`, or DXR reflection ownership/interface changes: run `tools\validate_dx12_renderer.bat`.
- [x] Runtime host, window, resize, init, shutdown, or device lifecycle changes: run `tools\validate_full.bat`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Profiling marker changes: run `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` in addition to the relevant renderer gate.
- [x] Tooling script changes: run `tools\validate_fast.bat`, then run the changed script.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) If unsure at PR gate: run `tools\agent_validate.bat`.

Validation evidence checklist:

- [x] Capture the exact command.
- [x] Capture meaningful output lines.
- [x] Capture the log path if output is mirrored to a file.
- [x] Confirm zero build warnings.
- [x] Confirm `dx12_validation.txt` reports zero DX12 validation errors when renderer validation is required.
- [x] Confirm screenshot comparisons pass or document intentional baseline updates.
- [x] Confirm no required validation was skipped.

Review checklist:

- [x] Inspect all touched source-bearing files with `Agentic/Skills/comment-style-audit/skill.md`.
- [x] Run a focused source search for accidental direct backend access after interface migration.
- [x] Review resource lifetime against frame-lag/fence behavior.
- [x] Review pass declarations against actual resource reads and writes.
- [x] Review hot paths for new heap allocations.
- [x] Ask for or perform an independent rubber-duck review before final PR-bound handoff if the slice changes resource barriers, pass ordering, or interface lifetime.

## Final Acceptance Checklist

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Production pass scheduling is graph-owned for all targeted pass families.
- [x] The graph declares every read/write/imported/transient resource for migrated passes.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `Dx12RenderGraphExecutor` owns execution diagnostics and resource-state handling for migrated graph passes.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) At least one low-risk transient resource is graph-owned before claiming resource lifetime migration.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `IRenderBackend` is reduced to a compatibility facade or deleted after callers migrate.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Narrow capability interfaces serve caller groups without exposing the full backend.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) DX12-specific details are isolated to DX12-owned headers or consciously named DX12 capabilities.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Runtime renderer and host wiring no longer route pass code through unnecessary global backend access.
- [x] `Run.h` no longer owns or declares DXR reflection state or helpers.
- [x] Renderer validation is selected and run for any code slice that changes render behavior.
- [x] All touched source-bearing files pass the repository comment quality gate.
- [x] `git status --short --branch` has been checked before handoff or commit.

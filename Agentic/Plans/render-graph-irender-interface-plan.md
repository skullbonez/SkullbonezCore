# Render Graph / `IRenderBackend` Interface Plan

Date: 2026-06-27
Status: In progress
Impact areas: DX12 renderer, render graph execution, render backend interface, runtime render host, tests
Validation for this plan edit: Documentation-only. No repository validation required.

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

- [ ] Do not add or resurrect OpenGL, DX11, or Vulkan runtime abstractions.
- [ ] Do not create fake cross-API lowest-common-denominator interfaces.
- [ ] Do not move every pass family in one oversized diff.
- [ ] Do not change visual baselines unless a rendering behavior change is intentional and reviewed.
- [ ] Do not introduce async command-list recording until graph scheduling, barriers, and lifetime are stable.
- [ ] Do not widen `IRenderBackend` while claiming to split it.
- [ ] Do not expose raw DX12 types through engine-facing interfaces unless the interface is explicitly DX12-owned.
- [ ] Do not remove the compatibility facade until all production callers have moved.
- [x] Do not leave DXR reflection state, pass helpers, or backend-specific reflection setup in `Run.h`.

## Phase 0 - Startup, Inventory, and Slice Choice

- [ ] Follow the repository Agent Startup Contract before editing.
- [ ] Confirm the current branch and dirty state with `git status --short --branch`; treat pre-existing dirty files as user-owned.
- [ ] Read this plan and the current handoff in `Agentic/SessionState.md`.
- [ ] Read `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`.
- [ ] Read `Agentic/Plans/engine-architecture-next-steps-plan.md`.
- [ ] Skim `Agentic/Plans/codebase-top-10-cleanup-plan.md` for older renderer cleanup context, but prefer current source over old assumptions.
- [ ] Choose one implementation slice only: graph pass migration, transient resource ownership, backend interface split, or cleanup guardrails.
- [ ] State the selected impact area before editing.
- [ ] State the deferred PR-gate validation command before editing.

Render graph inventory:

- [ ] List all current graph-owned pass callbacks.
- [ ] List all direct runtime pass scheduling that bypasses the graph.
- [ ] List all graph imported resources and their owner.
- [ ] List all render targets that are candidates for graph-owned transient allocation.
- [ ] List all manually issued barriers around graph-managed passes.
- [ ] List all debug output files that describe frame graph execution.
- [ ] Identify the next lowest-risk pass family to migrate.

Backend interface inventory:

- [ ] List every method in `SkullbonezSource/Rendering/IRenderBackend.h`.
- [ ] Group each method by caller and capability: frame/device, resource creation, swapchain/present, capture/readback, GPU timing/profiling, debug draw, dynamic geometry, ray tracing, scene submission, settings, diagnostics, or legacy.
- [ ] List all direct `IRenderBackend` call sites.
- [ ] List all global renderer accessor call sites in `IRenderBackend.cpp` and related runtime host code.
- [ ] List every DXR reflection declaration, field, helper, callback, and include currently reachable from `Run.h`.
- [ ] Identify optional or no-op interface methods that should become capability queries or be deleted.
- [ ] Identify DX12-specific concepts currently visible in engine-facing headers.
- [ ] Record the inventory in a short handoff note under `Agentic/Reports/` if the slice is not completed in one sitting.

## Phase 1 - Harden the Render Graph Contract

Before moving more passes, make pass declaration and diagnostics difficult to misuse.

- [ ] Ensure each graph pass has a stable name.
- [ ] Ensure each graph pass declares every read resource.
- [ ] Ensure each graph pass declares every write resource.
- [ ] Ensure imported resources are explicitly marked as imported.
- [ ] Ensure transient resources have descriptors before allocation ownership moves to the graph.
- [ ] Ensure pass callbacks receive only the context they need.
- [ ] Ensure graph execution diagnostics show pass order.
- [ ] Ensure graph execution diagnostics show resource reads and writes.
- [ ] Ensure graph execution diagnostics show imported versus transient resources.
- [ ] Ensure missing resource declarations fail fast in debug or validation paths.
- [ ] Add a small dry-run or inspection path if the existing graph has one; otherwise document why it is deferred.
- [ ] Add comments for resource lifetime, imported ownership, and pass callback invariants in touched source.

Done when:

- [x] A migrated pass can be reviewed from its graph declarations without reading unrelated runtime scheduling code.
- [ ] The executor can report what it executed and which resources each pass touched.
- [x] A callback-owned pass with no declared graph resource handoff is detectable before it becomes a subtle DX12 hazard.

## Phase 2 - Move Runtime Pass Families into Graph Callback Ownership

Move one family at a time and validate the behavior before moving the next. The preferred next pass from prior roadmap notes is `VolumetricLightPass`.

Pass-family checklist, repeated for each selected family:

- [x] Identify the pass entry point and current direct scheduling path.
- [x] Identify every input resource.
- [x] Identify every output resource.
- [x] Identify every imported resource owner.
- [ ] Identify every transient resource candidate.
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
- [ ] Low-risk post-processing passes after tone map
- [ ] Reflection or environment passes
- [ ] Shadow passes
- [ ] Water passes
- [ ] Sky passes
- [ ] Terrain passes
- [ ] Object/model passes
- [ ] Debug draw passes
- [ ] Capture/readback passes

Do-not-miss checklist:

- [x] Pass order is unchanged unless the behavior change is intentional and documented.
- [x] Every DX12 resource transition is either graph-owned or explicitly justified.
- [x] Clear/load/store behavior is preserved.
- [x] Resize behavior is preserved.
- [x] Screenshot timing remains deterministic.
- [x] DX12 validation output remains zero-error for renderer-validation slices.
- [ ] Any visual baseline update is intentional, reviewed, and followed by `tools\validate_dx12_renderer.bat`.

## Phase 3 - Move Transient Resource Ownership into the Graph

Do this after enough pass families declare resources accurately.

- [ ] Add or complete transient resource descriptors for texture targets.
- [ ] Add or complete transient resource descriptors for buffer targets if needed.
- [ ] Define descriptor fields: dimensions, format, sample count, usage flags, clear value, and resize policy.
- [ ] Define imported resource descriptors separately from transient descriptors.
- [ ] Track first use and last use for transient resources.
- [ ] Keep aliasing disabled until lifetime intervals are proven correct, unless the slice explicitly implements aliasing validation.
- [ ] Move one low-risk post-processing or volumetric target to graph-owned allocation.
- [ ] Prove resize and recreate behavior for that graph-owned target.
- [ ] Prove frame-lag/fence safety for graph-owned resource release or reuse.
- [ ] Remove the old manual allocation path for that target after validation.
- [ ] Repeat with more targets only after the first target is stable.

Do-not-miss checklist:

- [ ] Imported swapchain/backbuffer resources remain backend-owned.
- [ ] Imported persistent history buffers remain owned by the system that needs cross-frame lifetime.
- [ ] Transient resources do not outlive the frame unless explicitly promoted to persistent history.
- [ ] Descriptor heap and SRV/RTV/DSV ownership is clear.
- [ ] GPU readback/capture does not read a released transient resource.
- [ ] Resource state diagnostics name graph-owned transients clearly.

## Phase 4 - Split `IRenderBackend` into Narrow Capability Interfaces

Keep the split mechanical and caller-driven. Introduce a capability only when current callers clearly need it.

Candidate capability groups:

- [ ] Core frame/device capability: begin frame, execute graph/frame commands, end frame, present, resize, frame index, synchronization.
- [ ] Resource creation capability: textures, buffers, render targets, depth targets, views, upload helpers.
- [ ] Capture/readback capability: screenshots, GPU readback, validation captures, baseline artifacts.
- [ ] GPU profiling capability: timers, platform markers, marker scopes, validation marker output.
- [ ] Debug draw capability: lines, shapes, overlays, debug render queues.
- [ ] Dynamic geometry capability: transient vertex/index uploads and per-frame geometry streams.
- [x] Ray tracing capability: DXR scene/build/update/dispatch functions.
- [ ] Diagnostics/settings capability: validation flags, debug names, renderer stats, feature support.

Interface split checklist:

- [x] Name each new interface from the current source vocabulary; avoid speculative generic names.
- [x] Add the smallest interface needed for one caller group.
- [x] Back the new interface with the existing DX12 backend implementation.
- [x] Migrate one caller group to the new capability.
- [ ] Leave `IRenderBackend` forwarding or exposing compatibility until all callers move.
- [x] Keep DX12-only includes out of engine-facing headers unless the new interface is DX12-owned.
- [ ] Convert optional no-op methods into explicit capability checks or remove them after callers migrate.
- [ ] Avoid global access expansion; pass capability references through existing runtime ownership paths where practical.
- [x] Update comments for ownership, lifetime, thread expectations, and frame timing.
- [ ] Repeat for the next caller group only after the current group is validated.

Do-not-miss checklist:

- [ ] `IRenderBackend.h` gets smaller over the full plan, not larger.
- [ ] Callers no longer receive a full backend when they only need one capability.
- [x] Runtime host and renderer lifetime still destroy capabilities in a safe order.
- [ ] Capture/readback paths still work for validation artifacts.
- [ ] GPU marker and profiler paths still work for `--platform-profiler-markers`.
- [x] DXR code stays isolated from non-DXR callers.
- [x] No interface split adds per-frame heap allocation in a hot path.

## Phase 5 - Clean Up Runtime Renderer and Host Wiring

Once graph execution and capability views exist, simplify the runtime path.

- [ ] Make `RuntimeRenderer` submit graph work through the graph/executor path.
- [ ] Make `RuntimeRenderHost` provide only the capability references needed by runtime systems.
- [ ] Remove direct pass scheduling from runtime code after each migrated pass is graph-owned.
- [ ] Remove direct backend calls from pass code where a graph context or narrow capability is sufficient.
- [x] Move DXR reflection state and helper declarations out of `Run.h`.
- [x] Put DXR reflection ownership behind renderer/runtime render-host owned types with narrow inputs from gameplay/runtime state.
- [x] Remove any `Run.h` includes that exist only for DXR reflection implementation details.
- [ ] Remove stale compatibility methods from `IRenderBackend` after final callers migrate.
- [ ] Remove stale include dependencies caused by the old wide backend interface.
- [ ] Update renderer diagnostics and frame graph outputs to match the new execution path.
- [x] Add search guardrails for accidental new wide-backend dependencies, direct runtime pass scheduling regressions, and DXR reflection declarations returning to `Run.h`.

Searches to run before declaring cleanup done:

- [ ] `rg "IRenderBackend" SkullbonezSource`
- [ ] `rg "Gfx\(|GetRenderBackend|RenderBackendDX12" SkullbonezSource`
- [x] `rg "Execute.*Pass|Run.*Pass|Render.*Pass" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [ ] `rg "RenderGraph" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [ ] `rg "D3D12|ComPtr<ID3D12|DXGI" SkullbonezSource/Runtime SkullbonezSource/Rendering`
- [ ] `rg "DXR|Dxr|Raytrac|Reflection" SkullbonezSource/Runtime/Run.h SkullbonezSource/Runtime/RunInternal.h`

## Phase 6 - Validation, Guardrails, and Review

Repository validation scripts are PR/commit gates. Do not run them repeatedly while iterating unless the user explicitly asks for validation.

- [ ] Documentation-only changes: no repository validation required.
- [x] Render graph pass migration: run `tools\validate_dx12_renderer.bat` before PR-bound commit.
- [ ] Transient resource allocation, resource barriers, descriptor lifetime, or render target ownership changes: run `tools\validate_dx12_renderer.bat`.
- [ ] Upload buffer, dynamic geometry, descriptor heap, or per-frame allocation changes: run `tools\validate_dx12_renderer.bat` and `tools\validate_perf.bat`.
- [x] `IRenderBackend`, `RenderBackendDX12`, `Rendering/DX12/*`, or DXR reflection ownership/interface changes: run `tools\validate_dx12_renderer.bat`.
- [x] Runtime host, window, resize, init, shutdown, or device lifecycle changes: run `tools\validate_full.bat`.
- [ ] Profiling marker changes: run `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` in addition to the relevant renderer gate.
- [x] Tooling script changes: run `tools\validate_fast.bat`, then run the changed script.
- [ ] If unsure at PR gate: run `tools\agent_validate.bat`.

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

- [ ] Production pass scheduling is graph-owned for all targeted pass families.
- [x] The graph declares every read/write/imported/transient resource for migrated passes.
- [ ] `Dx12RenderGraphExecutor` owns execution diagnostics and resource-state handling for migrated graph passes.
- [ ] At least one low-risk transient resource is graph-owned before claiming resource lifetime migration.
- [ ] `IRenderBackend` is reduced to a compatibility facade or deleted after callers migrate.
- [ ] Narrow capability interfaces serve caller groups without exposing the full backend.
- [ ] DX12-specific details are isolated to DX12-owned headers or consciously named DX12 capabilities.
- [ ] Runtime renderer and host wiring no longer route pass code through unnecessary global backend access.
- [x] `Run.h` no longer owns or declares DXR reflection state or helpers.
- [x] Renderer validation is selected and run for any code slice that changes render behavior.
- [x] All touched source-bearing files pass the repository comment quality gate.
- [x] `git status --short --branch` has been checked before handoff or commit.

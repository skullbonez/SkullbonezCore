# DX12 Backend Ownership Decomposition — Retire The Last Laundered God Class

Date: 2026-07-18
Status: Active — 2/8 tasks
Branch: `nightrunner-17th-july`
Impact area: `SkullbonezSource/Rendering/DX12/*`, `Rendering/I*.h` consumers,
project filters
Owner: DX12 rendering backend

## Problem And Evidence (measured 2026-07-18 at `nightrunner-17th-july` tip, 06a17ff31)

`RenderBackendDX12` is one class implementing seven interfaces
(`IRenderDeviceLifecycle`, `IRenderResourceFactory`, `IRenderCommandContext`,
`IRenderDiagnostics`, `IRenderCaptureBackend`, `IRenderRayTracing`,
`IRenderShaderDevelopment` — `RenderBackendDX12.h:639-645`) with a
1,095-line header and 9,437 lines across `RenderBackendDX12.cpp`, `.DXR.cpp`,
`.Textures.cpp`, `.Readback.cpp`, `.DynamicGeometry.cpp`, and partner
headers. The interfaces segregate consumers, but device lifetime, PSO
caching, descriptor heaps, upload arenas, screenshot readback, DXR, GPU
timers, draw-call tracing, and fault injection all share one object's
private mutable state. Per the `AGENTS.md` god-object closure rule, a class
whose authority remains reachable through sibling translation units is not
decomposed — the TU split is exactly the laundering pattern that rule names.

Partial owners already exist inside the class (`Dx12RenderDevice`,
`Dx12FrameOwner`, `Dx12TextureOwner`, `Dx12PipelineOwner`,
`Dx12GeometryOwner`, `Dx12RaytracingOwner` — `RenderBackendDX12.h:678-694`),
proving the direction works; the remaining backend-resident state
(descriptor heaps/allocators at `RenderBackendDX12.h:726-756`, graph
transient pools at `:793-802`, readback retention at `:771-772`, GPU timers,
diagnostics counters) has not moved.

Prior rulings acknowledged: round 3 excluded `RenderBackendDX12`
re-partitioning beyond the bindless task, and round-6 monolith right-sizing
ruled on its TU cohesion. This plan proceeds on fresh explicit owner
direction (2026-07-18) and does ownership decomposition, not TU
right-sizing; those rulings are superseded only for this scope.

## Goal

Every remaining multi-domain state cluster moves into a concrete owner with
typed boundaries: descriptor-heap ownership, capture/readback, render-graph
transient materialization, GPU timing/diagnostics, and shader-development
hot-reload each get (or complete) an owner class. `RenderBackendDX12`
shrinks to the composition point that owns the concrete owners, sequences
frame begin/submit/present, and implements the seven interfaces by
delegating to owner state that no longer lives in the backend class itself.

## Non-Goals

- **The seven consumer interfaces stay exactly as they are** (2026-07-18
  owner ruling: retained for future consumers). No interface merging,
  splitting, renaming, or signature change.
- No behavioral change: identical command streams, identical committed DX12
  screenshot baselines, zero DX12 InfoQueue validation errors.
- No barrier-model change (binding decision: DX12 explicit helpers own live
  barriers; RenderGraph does not become a barrier compiler).
- No `FRAME_COUNT` change (stays 2 per the round-6 ruling).
- No new inheritance beyond the existing interface set; owners are concrete
  value-composed classes.
- No baseline, golden, or screenshot refresh.

## Tasks

Every task that touches DX12 source runs `tools\validate_dx12_renderer.bat`
plus the mandatory bounded `tools\run_graphics_stress.bat 1` per MASTER
rule 10, with command, measured runtime, and exit evidence recorded.

- [x] D0 — State census and owner map. Inventory every private member of
  `RenderBackendDX12` at the current tip, attribute each to a target owner
  (existing owner, new owner, or genuinely composition-root), and record
  per-cluster fence/lifetime invariants that must move with the state.
  Owner ratifies map and branch. Evidence: dated census under
  `Agentic/Reports/`. Gate: none (documentation).
- [x] D1 — Descriptor ownership owner. Move the RTV/DSV/SRV heaps, staging
  heap, descriptor sizes, static/transient SRV allocation, and the
  CPU-descriptor allocators behind one concrete descriptor owner that
  enforces the fence-lifetime rule internally. Gate: renderer + stress.
- [ ] D2 — Capture/readback owner. Move screenshot capture, readback
  buffers, and the uncertain-readback retention policy
  (`m_uncertainReadbackResources`) into a concrete capture owner
  implementing the `IRenderCaptureBackend` operations' state. Gate:
  renderer + stress.
- [ ] D3 — Render-graph transient owner. Move
  `m_graphTransientResources` / bindings / stats and the active-graph
  render-target save/restore block into a concrete graph-transient owner;
  it borrows the descriptor owner, never raw heap pointers. Gate:
  renderer + stress.
- [ ] D4 — Diagnostics and GPU-timing owner. Move `GpuTimerStateDX12`
  consumption, draw-call counters/high-water, visibility stats,
  `DrawCallTrace`, and fault injection behind the diagnostics owner
  surface. Gate: renderer + stress.
- [ ] D5 — Shader-development owner. Move hot-reload / shader-development
  state and operations (the `IRenderShaderDevelopment` implementation
  state) into a concrete owner; cold-path only, no per-frame authority.
  Gate: renderer + stress.
- [ ] D6 — Composition-root shrink and header diet. With D1-D5 landed,
  remove dead private members/helpers from `RenderBackendDX12`, re-home
  helper declarations to their owners, and measure the header/class size
  drop. Interface method bodies may delegate to owners, but no owner state
  may remain declared in the backend class. Gate: renderer + stress, plus
  `validate_full` (broad include surface).
- [ ] D7 — Independent ownership review and closure. One review over the
  logical backend module (class, all TUs, every owner) under the god-object
  closure rule: no reach-back, no backend-resident multi-domain state, no
  compatibility aliases. Any credible finding reopens its task. Final
  gates: `validate_full`, `validate_dx12_renderer`, stress; run the
  renderer gate 3 consecutive times if upload/frame-allocator code moved
  (danger-zone rule). Update MASTER-PLAN, SessionState, and delete this
  plan on closure.

## Dependencies And Decisions

- Independent of the scene plan; may run in parallel on its own branch if
  the owner staggers merges.
- Runs before `small-findings-hardening` (that plan's PSOKey12 identity
  task rebases on the pipeline-owner surface this plan finalizes).
- D0 owner decisions: whether the backend class keeps direct frame
  begin/present sequencing or gains a small frame-sequencer owner; final
  owner names (domain nouns, no `*Manager`/`*Context`).

## Progress Evidence

- D0 evidence (2026-07-18): the complete 1,095-line header/private-surface
  census is ratified at current tip `cffce392e`. Top-level frame
  begin/close/submit/present sequencing remains at `RenderBackendDX12`, while
  epoch/fence state remains in `Dx12FrameOwner`; no duplicate sequencer is
  introduced. New owner names are `Dx12DescriptorHeaps`,
  `Dx12BackbufferCapture`, `Dx12GraphTransientPool`, `Dx12Diagnostics`, and
  `Dx12ShaderDevelopment`. Every private member/helper and the fence/lifetime
  invariant that moves with each cluster is recorded in
  `../../Reports/2026-07-18/dx12-backend-owner-census.md`. D0 is
  documentation-only, so no repository validation was required.
- D1 evidence (2026-07-18): `Dx12DescriptorHeaps` now owns all four raw heaps,
  fixed capacities, RTV/DSV/SRV row allocators, stable back-buffer RTVs, and
  the main DSV. Callers use typed allocation, publication, binding, reset, and
  retirement operations; no raw heap or allocator accessor remains. The frame
  owner retains only the covering-fence proof and routes retired CPU rows back
  by `Dx12CpuDescriptorKind`, while framebuffers borrow the single concrete
  descriptor owner instead of three allocator aliases. The seven retained
  consumer interfaces are byte-identical and `FRAME_COUNT` remains 2. Comment
  audit: `../../Reports/2026-07-18/dx12-backend-d1-comment-audit.md`, 13/13
  checked with none deferred. Final gates: project filters passed with 730/730
  items; `validate_fast` passed formatting, Profile build, and 291/291 tests
  with 21,455/21,455 assertions; `validate_dx12_renderer` passed in 52.978 s
  with zero InfoQueue errors and all committed captures accepted; and
  `run_graphics_stress.bat 1` ran crash-free for 61.395 s. No baseline,
  golden, screenshot, or coverage-floor file changed.

## Acceptance

- `RenderBackendDX12` declares no descriptor heaps, readback buffers,
  graph-transient pools, diagnostics counters, or shader-development state
  as direct members; each lives in a ratified concrete owner.
- The seven interfaces are byte-identical to their pre-plan declarations.
- Independent review records zero credible god-object or reach-back
  findings across the logical module.
- All mapped gates pass from final source: zero DX12 validation errors,
  committed screenshot baselines matched, every stress run ≥10 s crash-free
  with recorded evidence, zero baseline refresh.

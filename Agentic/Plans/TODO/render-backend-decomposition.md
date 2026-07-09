# Render Backend Decomposition

Date: 2026-07-09 (consolidated)
Status: In progress — ~50% complete (facet split done; concrete-owner split open)
Impact area: DX12 renderer, backend interfaces, runtime render wiring
Consolidates: `render-graph-irender-interface-plan.md` (remaining work,
re-scoped) and the RGRAPH-* rows from the 2026-07-07 overnight blocker ledger.
Full completed-slice history in git history of those files.

## Scope decision (binding)

The 2026-07-09 owner decision (recorded in
`engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md`, executed by
`engine-cleanup-plans/11-render-abstraction-leaks.md`) **retires the
diagnostic RenderGraph path**: no render-graph compiler, no graph-owned
transient allocation, no further pass-family graph migration. DX12 explicit
hand-coded barriers are the honest architecture.

Consequently the old plan's Phase 1 (graph contract hardening), remaining
Phase 2 pass-family migrations, and Phase 3 (graph-owned transient resources)
are **dropped, not pending**. Blocker rows RGRAPH-003, RGRAPH-014, and
RGRAPH-029 are superseded by the same decision. What survives is backend
*decomposition*: the concrete DX12 owner is still an aggregate.

## Already done (summary)

FAC-001 complete: `IRenderBackend.h` deleted; `RenderBackendDX12` implements
the narrow lifecycle/resource/command/diagnostics/capture/raytracing facets
directly; runtime and scene callers borrow facets; `Gfx()` and the renderer
global are gone; DXR reflection state is out of `Run.h`.

## Remaining work

### A. Concrete DX12 owner split (was RGRAPH-007/010/022/023/024)

`RenderBackendDX12` remains the aggregate implementation behind the facets
(~2,500-line main TU plus partials). Split by real ownership, one owner per
slice, no `*Bridge`/`*Adapter` shims:

- [ ] A1. Texture ownership (`CreateTexture2D` family): device resource
  creation, upload reservations, descriptor allocation, SRV registration, and
  the texture-handle table become a named texture owner inside the DX12 layer.
- [ ] A2. Pipeline/PSO cache: PSO creation + draw prep state (root signature,
  bytecode, raster/depth/blend, RTV format, cache array) become a named
  pipeline owner.
- [ ] A3. DXR owner (`RayTracingBackendDX12` or equivalent): device5/command
  list lifetime, BLAS/TLAS/SBT state, reflection resources — designed as a
  real owner, not a forwarding shim.
- [ ] A4. After A1–A3, re-evaluate what remains of `RenderBackendDX12.cpp` and
  either keep it as the slim device/frame owner or continue splitting.

### B. Resource-capability design (was RGRAPH-004)

- [ ] B1. Callers of `IRenderResourceFactory` span shader, mesh, framebuffer,
  texture, dynamic-VB, and instancing needs. Decide whether one factory is
  honest or a narrower capability per caller family is worth the interface
  count. This previously failed the inheritance budget; that budget is being
  deleted by engine-cleanup plan 03 — decide on merit, not on the linter.

### C. Dual-ownership hazard (FAC-007)

- [ ] C1. `RenderBackendDX12` caches borrowed aliases
  (`m_device`/`m_swapChain`/`m_commandList`) duplicating pointers
  `Dx12RenderDevice` owns. Fix so device recreation cannot dangle them
  (single-owner accessors or explicit re-bind protocol).

### D. Cleanup

- [ ] D1. Remove remaining direct backend calls from pass code where a narrow
  capability suffices; keep DX12 types out of engine-facing headers.
- [ ] D2. Verify capture/readback and `--platform-profiler-markers` paths
  after each split slice.

## Acceptance

- [ ] `RenderBackendDX12` no longer implements texture, pipeline, and DXR
  ownership in one class; each has a named owner.
- [ ] No borrowed-alias member can dangle across device recreation.
- [ ] No new `*Bridge`/`*Adapter`/callback shims on render hot paths.
- [ ] `dx12_validation.txt` = 0 errors on every slice; screenshots match
  committed baselines.

## Validation map

| Slice | Gate |
|-------|------|
| Any backend split slice | `validate_dx12_renderer` |
| Upload buffer / frame allocator adjacency | `validate_dx12_renderer` ×3 consecutive |
| Profiling marker changes | renderer gate + `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` |
| Device lifecycle changes | `validate_full` |

# Render Interface And WorkerPool Slimming

Date: 2026-07-12
Status: Not started — 0/5 phases complete
Impact area: rendering abstraction layer (`I*` interfaces, mesh/shader draw
path), worker pool task API
Owner: rendering + core threading
Priority: Nice to have (2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

Two fossil paths survive from earlier eras and now cost without paying:

- `RenderBackendDX12` implements seven interfaces
  (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h:912-918`), and draws
  route through `IMesh`/`IShader` virtuals, yet DX12 is the only runtime
  renderer by binding repository rule. The role-interface *split* is good for
  seams and tests; the per-draw virtual dispatch is abstraction tax retained
  from the GL/DX11 parity era.
- `WorkerPool` still exposes `std::function`-based `Task`/`IndexFunction`/
  `ChunkFunction` paths (`SkullbonezSource/Core/WorkerPool.h:62-64`) alongside
  the `NoAlloc` templated variants. Every non-NoAlloc dispatch is a potential
  type-erased heap allocation; the good path exists but the bad path coexists
  and remains callable from hot code.

Per the hot-path/inheritance review rule, surviving inheritance must name its
owner, why value composition is insufficient, call frequency, and perf
evidence — this plan produces exactly that record for what stays and deletes
what does not qualify.

## Goal

Hot per-draw and per-dispatch paths are non-virtual and non-allocating. The
interface layer survives only where it is a genuine stable boundary (capture,
diagnostics, lifecycle seams used by tests), with the retention rationale
recorded. The heap-allocating worker-pool paths are gone or confined to
cold phases.

## Non-Goals

- No second render backend and no preparation for one.
- No change to draw semantics, root-signature contract, or shader ABI.
- No worker-pool scheduling redesign; only the task-passing API.

## Phases

- [ ] **W1 — WorkerPool call-site inventory and migration.** Inventory every
  `Submit`/`ParallelFor`/`ParallelForChunks`/`MakeChunks` caller; migrate hot
  (per-frame) callers to the `NoAlloc` variants. Acceptance: dated inventory
  table; no per-frame caller uses a `std::function` path.
- [ ] **W2 — Delete or confine the `std::function` paths.** Remove the
  type-erased overloads, or if cold callers (startup, tooling) justify
  retention, gate them behind names that make the allocation explicit and add
  them to the allocation-policy allowlist with owner/reason/cap. Acceptance:
  the allocation-policy checker passes with no new silent exceptions.
- [ ] **R1 — Interface-layer usage measurement.** Measure per-frame virtual
  dispatch on the draw path (draw count x virtual hops from
  `IMesh::Draw`/`IShader` activation/`IRenderCommandContext` calls) in a
  representative heavy scene, and inventory which `I*` interfaces have
  test/capture consumers versus pure single-implementation pass-throughs.
  Acceptance: dated report under `Agentic/Reports/` with the dispatch counts
  and a keep/collapse verdict per interface, each keep naming its consumer.
- [ ] **R2 — Collapse where the evidence says so.** Devirtualize or remove the
  interfaces R1 marked collapse (typical shape: runtime passes hold concrete
  `RenderBackendDX12`-facing types; retained seams stay narrow). Record the
  inheritance-retention rationale required by the hot-path review rule for
  every survivor. Acceptance: R1's collapse list is implemented;
  `tools\validate_perf.bat` shows neutral-or-better frame cost.
- [ ] **RW3 — Review and gates.** Independent review against the hot-path
  data/inheritance rule (no new `*Adapter`/`*Bridge` spellings, no callback
  seams reintroduced), comment-style audit of touched files, then full gates
  per the map below.

## Dependencies And Decisions

- Run last of the six 2026-07-12 review plans; widest blast radius and pure
  cleanup.
- R2 proceeds only on R1 evidence — if measured dispatch cost is negligible
  *and* every interface has a real consumer, the correct outcome is a
  documented keep-everything verdict, and R2 closes as evidence-recorded
  without churn.

## Acceptance

No hot-path `std::function` dispatch, allocation checker clean, R1 report
committed, R2 verdicts implemented or recorded, perf gate neutral or better.

## Validation

`tools\validate_full.bat` (broad scope: `Common.h`-adjacent core plus
renderer), `tools\validate_perf.bat`, and `tools\run_graphics_stress.bat 1`
with recorded command/runtime/exit evidence for the DX12 modifications. Run
`python tools\check_allocation_policy.py --repo .` after W2.

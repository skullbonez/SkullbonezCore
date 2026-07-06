# Authoritative Architecture Remediation Tonight

Date: 2026-07-06
Status: Authoritative active plan set
Impact area: runtime architecture, physics authority, service contexts, render host, DX12/render graph
Validation for this documentation-only change: none required

## Authority

For the five scopes below, this plan set supersedes older architecture plans,
cleanup plans, and roadmap notes. Older plans remain useful history, but they
are no longer the source of truth for what to run tonight.

The files under `In_Progress/Inventories/` (the carmack 579-hit checklist and
the physics-standalone checklist/workqueue) are inventories feeding plans 02
and 03 — reference material, **not** tonight's workqueue. The workqueue is the
five `authoritative-plan-0*.csv` files only; the closed physics-standalone
queue (165/165) stays closed.

Unattended execution is governed by
`Agentic/Plans/Done/overnight-run-protocol.md`: pre-flight green
baseline, commit-per-cluster, two-strikes-then-block failure rule, the
`overnight` safe/defer column in each CSV, and the FILL filler queue.

The objective is not to make `Run` stop constructing the engine tree. The
objective is to move behavioral authority and data ownership behind the
subsystems that already exist or are named here.

## Plans

| Order | Plan | CSV workqueue | Required PR validation after implementation |
|-------|------|---------------|---------------------------------------------|
| 1 | `authoritative-plan-01-run-composition-root.md` | `authoritative-plan-01-run-composition-root.csv` | `tools\validate_full.bat` unless a slice is proven input-only and non-runtime |
| 2 | `authoritative-plan-02-physics-store-authority.md` | `authoritative-plan-02-physics-store-authority.csv` | `tools\validate_physics.bat`; add `tools\validate_perf.bat` for storage/hot-loop changes |
| 3 | `authoritative-plan-03-explicit-service-contexts.md` | `authoritative-plan-03-explicit-service-contexts.csv` | Depends on touched area; broad service movement defaults to `tools\validate_full.bat` |
| 4 | `authoritative-plan-04-render-host-frame-snapshot.md` | `authoritative-plan-04-render-host-frame-snapshot.csv` | `tools\validate_dx12_renderer.bat`; use `tools\validate_full.bat` if runtime lifecycle changes |
| 5 | `authoritative-plan-05-render-graph-backend-split.md` | `authoritative-plan-05-render-graph-backend-split.csv` | `tools\validate_dx12_renderer.bat`; add `tools\validate_perf.bat` for hot binding changes |

## Tonight Run Rules

1. Do not start by deleting code. Start by adding or tightening the smallest
   static guardrail that prevents new violations in the selected scope. Every
   guardrail records the current census as a stored budget number and ships a
   self-test — a ratchet without a baseline number is lint, not a ratchet.
2. Pick one plan and one CSV row cluster. Do not mix all five at once. For
   unattended runs, **exactly one plan owns the whole night** — plans 01, 03,
   and 04 overlap on `Run`/render-host code and will self-conflict if
   interleaved. Only FILL rows from the overnight protocol may be taken
   outside the chosen plan.
3. Preserve behavior while moving authority. A clean boundary that changes
   physics or render output is not clean.
4. Do not introduce new generic `Runtime`, `Bridge`, `Compatibility`, or
   service-bag types unless the row explicitly says to create a temporary
   owner-labeled compatibility surface.
5. Every implementation slice must update its CSV row statuses before handoff.
6. Source-bearing edits must pass the comment quality gate for touched files.
7. Validation scripts are PR gates, not every-edit checks. Use focused builds or
   inspections during iteration.

## Definition Of Done For The Whole Set

- `Run` is a thin lifetime/composition root and frame scheduler.
- Physics body, collider, render-instance, replay identity, and diagnostics
  authority live in stores or scene/physics owners, not `GameModelCollection`.
- Normal-path code receives explicit services or snapshots instead of calling
  `Gfx()`, `Cfg()`, or singleton accessors.
- `RuntimeRenderHost` no longer borrows broad `Run` state; render passes consume
  frame snapshots and narrow render/world services.
- `IRenderBackend` is no longer the normal draw dependency; DX12 graph/pass
  ownership is split into capability-specific APIs.

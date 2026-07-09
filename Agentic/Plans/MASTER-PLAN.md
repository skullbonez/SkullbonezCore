# MASTER PLAN — Repository Plan Inventory

Date: 2026-07-09
Status: Authoritative inventory of every remaining plan in the repository.

This is the single source of truth for remaining plan work. Update a row's
percentage when its plan advances; delete the row and the plan file when it
completes. **Completed plans are deleted, not archived** — git history is the
archive. Do not recreate `Done/`, `Failed/`, `Rejected/`, `In_Progress/`, or
`To_Eval/` folders; live plans go in `TODO/` (or `engine-cleanup-plans/` for
that campaign), and nothing else.

Percentages come from plan checklists where they exist and are estimates
otherwise.

## Engine cleanup campaign (`engine-cleanup-plans/`)

Protocol: [00-EXECUTION-GUIDE.md](../../engine-cleanup-plans/00-EXECUTION-GUIDE.md).
Owner decisions of 2026-07-09 are binding — see
[HANDOFF-2026-07-09-OWNER-DECISIONS.md](../../engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md).

| Plan | Status | % | Remaining work |
|------|--------|---|----------------|
| [03 Governance apparatus removal](../../engine-cleanup-plans/03-governance-apparatus-reduction.md) | In Progress | 15% | Delete the regex boundary checker + all `MAX_*` ratchets and invocations; strip regex-enforced gates from `AGENTS.md`; boolean DX12-only check; relax comment gate. **Prerequisite:** step 2.1 waits on `TODO/behavioral-test-depth.md` P1+P4 (tests are the stated enforcement replacement). |
| [04 Error-handling reconciliation](../../engine-cleanup-plans/04-error-handling-policy-reconciliation.md) | In Progress | 85% | Strict throws 283 → 7. Remaining: allocator-safe fatal for `RuntimeAllocationTracker`, math test contracts, closure (ratchet deletion with plan 03). Also absorbs the remnants of fable-05 (see Retired below). |
| [07 Allocation-gate right-sizing](../../engine-cleanup-plans/07-allocation-gate-right-sizing.md) | In Progress | 15% | Right-size the checker; keep global zero-alloc default, replay-only exception. |
| [11 Render abstraction leaks](../../engine-cleanup-plans/11-render-abstraction-leaks.md) | In Progress | 80% | Retire the diagnostic RenderGraph path per owner decision; remove stale barrier-ownership claims. |
| [13 Facade retirement (rule)](../../engine-cleanup-plans/13-facade-retirement.md) | In Progress | 75% | Cross-cutting rule; FAC-005 executes via plan 14, FAC-004 needs an owner, FAC-007 executes via `TODO/render-backend-decomposition.md`. |
| [14 Public physics API boundary](../../engine-cleanup-plans/14-public-physics-api-boundary.md) | Proposed | 0% | No `GameModel`/raw `modelIndex`/solver containers in `PhysicsApi.h`/`PhysicsEngine.h`. |
| [15 Review gaps (2026-07-09)](../../engine-cleanup-plans/15-review-gaps.md) | Proposed | 0% | Comment-boilerplate cleanup (15.4) lives here; 15.1/15.2/15.3 execute via TODO plans; 15.5/15.6 are small hygiene slices. |

## Consolidated active plans (`Agentic/Plans/TODO/`)

Created 2026-07-09 by consolidating `fable_plans/` (open work), `To_Eval/`,
and `In_Progress/` — grouped by subsystem, done parts removed, stale parts
re-scoped. Constituent history is in git history of the deleted files.

| Plan | Status | % | Remaining work |
|------|--------|---|----------------|
| [behavioral-test-depth](TODO/behavioral-test-depth.md) | Proposed | 0% | **Highest-leverage plan in the inventory.** Solver-stage, manifold, parser, and replay round-trip unit tests; injected-bug drill; gates plan 03's enforcement switch. |
| [physics-authority-and-identity](TODO/physics-authority-and-identity.md) | In progress | 55% | Body/collider authority completion, scene/entity metadata split, stable-identity storage rule (handles, `ModelRowHint`), PHYS blocker knot (needs a physics-owner design decision). |
| [render-backend-decomposition](TODO/render-backend-decomposition.md) | In progress | 50% | Concrete DX12 owner split (textures, PSO cache, DXR owner), resource-capability decision, FAC-007 dual-ownership fix. Graph-buildout scope dropped per owner decision. |
| [interaction-state-machine](TODO/interaction-state-machine.md) | In progress | 45% | Phases P4–P10: camera capture, launcher/manipulator/editor/replay gesture migration, commands/events, bool-cluster deletion. |
| [replay-prediction-and-memory](TODO/replay-prediction-and-memory.md) | In progress | 35% | Prediction worker job (fable-03 P3), memory-quality tuning (re-derive data model first), replay code-size right-sizing. |
| [runtime-shell-decomposition](TODO/runtime-shell-decomposition.md) | In progress | 25% | Render-host narrowing, tool/replay/scene ownership moves, mega-TU splits (`RunInput`, `TestSceneParser`), `RunInternal.h` retirement, `Common.h` slimming, RUN blocker knot. |
| [shadow-edge-quality](TODO/shadow-edge-quality.md) | Planned | 5% | S0 baseline → tight-map terrain receivers → Poisson/PCSS filtering → presets. |
| [fracture-replay-feature](TODO/fracture-replay-feature.md) | Backlog | 0% | Feature: GPU fracture with reversible replay; sequence after replay memory data-model decisions. |

## Next fable plans (`Agentic/Plans/NEXT_FABLE_PLANS/` — protected; active agent works only here)

| Plan | % |
|------|---|
| physics-vcxproj-split-plan | in flight (agent running) |
| space-nbody-gravity-demo-plan | 0% |

## Retired in substance (no successor file)

- **Global service retirement** — verified complete 2026-07-09: `Gfx()`,
  `Window::Instance`, `CameraCollection::Instance`,
  `TextureCollection::Instance`, `WorkerPool::Instance`, `ActiveAssetSystem`,
  `EngineConfig::Instance` all have zero source call sites; `Cfg()` survives
  only in two `Common.h` comments. Remnant (delete the `Common.h` `Config.h`
  compatibility include) is owned by `TODO/runtime-shell-decomposition.md`
  E3. `Log()` (662 sites) and the startup-bound `Profiler` borrow are the
  deliberate ambient survivors per the closed plan-12 decision.
- **fable-05 unified error handling** — engine-cleanup plan 04 is the same
  campaign and has executed most of fable-05's remaining rows (DX12 throw
  conversions done; strict throws now 7). Its ratchet-to-zero closure step is
  superseded by plan 03's ratchet deletion. Any remaining scene/asset Lane R
  rows live in plan 04's inventory.
- **fable-07 blocker remediation + overnight blocker ledger** — the open
  PHYS-*/RGRAPH-*/RUN-* rows were absorbed into the three matching TODO plans
  as "known hard blockers"; clusters D/E and SVC rows were already resolved.
- **fable-01/02/08/09** — checklist-complete; deleted.

## Consolidation record (2026-07-09)

Two owner-directed passes, no commits (another agent active on
NEXT_FABLE_PLANS; do not touch that folder):

1. Deleted `Agentic/Plans/Done|Failed|Rejected` (124 files),
   `engine-cleanup-plans/DONE/` (8), 13 superseded `To_Eval` plans, 42 stale
   handoffs, `Agentic/PlanOrder.md`.
2. Consolidated `fable_plans/` + `To_Eval/` + `In_Progress/` (17 remaining
   plan/progress/ledger files) into the 7 grouped `TODO/` plans above and
   deleted the source folders. Kept load-bearing engine-cleanup handoffs:
   OWNER-DECISIONS, FINAL-TAKEOVER, PLAN04-DX12-SHADER-STATUS,
   PLAN02-REPLAY-SNAPSHOT.

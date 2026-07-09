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
| [13 Facade retirement (rule)](../../engine-cleanup-plans/13-facade-retirement.md) | In Progress | 75% | Cross-cutting rule; FAC-005 executes via plan 14, FAC-004 needs an owner, FAC-007 executes via `TODO/render-backend-decomposition.md`. |
| [14 Public physics API boundary](../../engine-cleanup-plans/14-public-physics-api-boundary.md) | In Progress | 40% | Public count/hint vocabulary is in place; implementation remains for removing raw dense row authority and solver-container signatures in `PhysicsApi.h`/`PhysicsEngine.h`. |
| [15 Review gaps (2026-07-09)](../../engine-cleanup-plans/15-review-gaps.md) | Proposed | 0% | Comment-boilerplate cleanup (15.4) lives here; 15.1/15.2/15.3 execute via TODO plans; 15.5/15.6 are small hygiene slices. |

## Consolidated active plans (`Agentic/Plans/TODO/`)

Created 2026-07-09 by consolidating `fable_plans/` (open work), `To_Eval/`,
and `In_Progress/` — grouped by subsystem, done parts removed, stale parts
re-scoped. Constituent history is in git history of the deleted files.

| Plan | Status | % | Remaining work |
|------|--------|---|----------------|
| [behavioral-test-depth](TODO/behavioral-test-depth.md) | In progress | 42% | P1 solver-stage tests, P4 replay snapshot/hash round-trip, and most P3 parser failure cases are complete. Remaining: P2 manifold reduction, P3 `assetInstances[]` round-trip, P5 injected-bug drill, and P6 sustaining rule. |
| [physics-authority-and-identity](TODO/physics-authority-and-identity.md) | In progress | 55% | Body/collider authority completion, scene/entity metadata split, stable-identity storage rule (handles, `ModelRowHint`), PHYS blocker knot (needs a physics-owner design decision). |
| [render-backend-decomposition](TODO/render-backend-decomposition.md) | In progress | 50% | Concrete DX12 owner split (textures, PSO cache, DXR owner), resource-capability decision, FAC-007 dual-ownership fix. Graph-buildout scope dropped per owner decision. |
| [interaction-state-machine](TODO/interaction-state-machine.md) | In progress | 45% | Phases P4–P10: camera capture, launcher/manipulator/editor/replay gesture migration, commands/events, bool-cluster deletion. |
| [runtime-shell-decomposition](TODO/runtime-shell-decomposition.md) | In progress | 25% | Render-host narrowing, tool/replay/scene ownership moves, mega-TU splits (`RunInput`, `TestSceneParser`), `RunInternal.h` retirement, `Common.h` slimming, RUN blocker knot. |
| [shadow-edge-quality](TODO/shadow-edge-quality.md) | Planned | 5% | S0 baseline → tight-map terrain receivers → Poisson/PCSS filtering → presets. |
| [fracture-replay-feature](TODO/fracture-replay-feature.md) | Backlog | 0% | Feature: GPU fracture with reversible replay; sequence after replay memory data-model decisions. |

## Retired in substance (no successor file)

- **Global service retirement** — verified complete 2026-07-09: `Gfx()`,
  `Window::Instance`, `CameraCollection::Instance`,
  `TextureCollection::Instance`, `WorkerPool::Instance`, `ActiveAssetSystem`,
  `EngineConfig::Instance` all have zero source call sites; `Cfg()` survives
  only in two `Common.h` comments. Remnant (delete the `Common.h` `Config.h`
  compatibility include) is owned by `TODO/runtime-shell-decomposition.md`
  E3. `Log()` (662 sites) and the startup-bound `Profiler` borrow are the
  deliberate ambient survivors per the closed plan-12 decision.
- **fable-05 unified error handling** — completed through engine-cleanup plan 04
  on 2026-07-10. Strict source throws are now zero, no throw-count ratchet was
  reinstated, and the completed plan/inventory files were deleted per MASTER
  convention. Stale include/comment hygiene remains in plan 15.6.
- **audit iss-05 allocation-gate right-sizing** — completed through
  engine-cleanup plan 07 on 2026-07-10. Runtime guard diagnostics were trimmed
  without weakening pass/fail enforcement, replay remains the only approved
  runtime allocation exception, and the static checker now covers direct
  heap/reserve APIs, owning dynamic STL members, and STL growth calls with
  owner/phase/cap allowlist metadata. The completed plan/inventory files were
  deleted per MASTER convention.
- **audit iss-09 render abstraction leaks** — completed through engine-cleanup
  plan 11 on 2026-07-10. Backbuffer state is tracked as an explicit resource
  state, replay ribbons draw through generic transient triangles, and the
  diagnostic RenderGraph skeleton/live-barrier comparison path was deleted.
  RenderGraph remains a pass/resource declaration, callback scheduling, and
  transient texture lifetime layer; DX12 explicit backend helpers own live
  transition and UAV barrier emission. The completed plan file was deleted per
  MASTER convention.
- **fable-07 blocker remediation + overnight blocker ledger** — the open
  PHYS-*/RGRAPH-*/RUN-* rows were absorbed into the three matching TODO plans
  as "known hard blockers"; clusters D/E and SVC rows were already resolved.
- **fable-01/02/08/09** — checklist-complete; deleted.

## Consolidation record (2026-07-09)

Two owner-directed passes:

1. Deleted `Agentic/Plans/Done|Failed|Rejected` (124 files),
   `engine-cleanup-plans/DONE/` (8), 13 superseded `To_Eval` plans, 42 stale
   handoffs, `Agentic/PlanOrder.md`.
2. Consolidated `fable_plans/` + `To_Eval/` + `In_Progress/` (17 remaining
   plan/progress/ledger files) into the 7 grouped `TODO/` plans above and
   deleted the source folders. Kept load-bearing engine-cleanup handoffs:
   OWNER-DECISIONS, FINAL-TAKEOVER, PLAN04-DX12-SHADER-STATUS,
   PLAN02-REPLAY-SNAPSHOT.

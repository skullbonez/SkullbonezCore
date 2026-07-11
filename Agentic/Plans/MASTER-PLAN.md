# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-11
Status: Authoritative inventory of every live repository plan

## Inventory Rules

1. Live implementation plans are listed here and stored under `TODO/`, except
   the active engine-cleanup campaign file.
2. Completion uses checked-phase counts, not subjective percentages. Partial
   phases are named as partial and do not increment the completed count.
3. A checkbox closes only with its acceptance evidence and required validation.
4. Completed plans/checklists are deleted; git history is the archive.
5. Every dependency link must resolve to a live file. A missing link is a plan
   defect and blocks the dependent phase.
6. A plan must name owner, problem/evidence, goal, non-goals where needed,
   phases, dependencies/decisions, acceptance, and validation.
7. Source measurements are dated and scoped; historical numbers are not reused
   as current evidence.
8. God-object cleanup is reviewed across logical types/modules, not individual
   files. A short facade, shared context, callback bag, or forwarding owner does
   not satisfy an ownership deletion proof.

## Execution Priority

1. Validation gate integrity V0-V2 and DX12 failure inventory D0.
2. DX12 command-state/failure propagation D1-D5.
3. Behavioral gaps P2/P3/P5 while the CPU umbrella is integrated.
4. Runtime-shell input/command/scene extractions, coordinated with UI and
   interaction ownership.
5. Replay workspace/right-sizing and physics stable-identity work.
6. Render concrete-owner decomposition after failure propagation.
7. Stale reference cleanup as a documentation-only parallel lane.
8. Shadow quality after renderer foundations; fracture replay remains blocked.

## Engine Cleanup Campaign

| Plan | State | Verified basis | Next work |
|---|---|---|---|
| [15 review gaps](../../engine-cleanup-plans/15-review-gaps.md) | In progress | 15.4 and 15.5 complete; 15.1-15.3 delegated | Execute the 15.6 inventory checklist; owning TODO plans close the other findings |

## Active Architecture, Safety, And Test Plans

| Plan | State | Verified phase count | Next blocking action |
|---|---|---:|---|
| [validation-gate-integrity](TODO/validation-gate-integrity.md) | Externally blocked | 5/6 | V3 needs default-branch hosted runs, branch protection, and trusted runner administration; no local implementation remains |
| [dx12-failure-propagation](TODO/dx12-failure-propagation.md) | Complete | 6/6 | Retain as closure evidence; failure propagation, transactional recreation, device-loss teardown, and fault injection are proven |
| [behavioral-test-depth](TODO/behavioral-test-depth.md) | Complete | 6/6 | Retain as closure evidence; named tests, four mutation drills, stop proofs, and final gates pass |
| [runtime-shell-decomposition](TODO/runtime-shell-decomposition.md) | Complete | 27/27 completed items | Retain as closure evidence; final inventory, deletion proofs, adversarial review, and required gates pass |
| [runtime-ui-control-architecture-cleanup](TODO/runtime-ui-control-architecture-cleanup.md) | Complete | 7/7 | Retain as closure evidence; 96-file inventory, shared surfaces, deletion proofs, review fixes, and final gates pass |
| [interaction-state-machine](TODO/interaction-state-machine.md) | Complete | 6/6 remaining phases | Retain as closure evidence; typed gesture payload, native capture, focus/UI crossing, deletion proofs, and repeat adversarial review pass |
| [replay-architecture-and-right-sizing](TODO/replay-architecture-and-right-sizing.md) | Complete | 6/6 | Retain as closure evidence; stable identity, bounded memory, named gates, rollback proof, and source-size justifications are complete |
| [physics-authority-and-identity](TODO/physics-authority-and-identity.md) | Complete | 16/16 current items | Handle-owned mutation, coordinated lifecycle, stable runtime identity, full gate, and repeat adversarial review complete |
| [render-backend-decomposition](TODO/render-backend-decomposition.md) | In progress | 0/8 remaining items | Begin the texture-owner split from the completed RuntimeRenderer composition boundary |
| [stale-plan-reference-cleanup-15.6-checklist](TODO/stale-plan-reference-cleanup-15.6-checklist.md) | Complete | 86/86 files | Retain as reconciled evidence; no source rows remain |

## Planned Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](TODO/shadow-edge-quality.md) | Planned | 0/5 | DX12 failure state safe; coordinate renderer owner/binding work |
| [fracture-replay-feature](TODO/fracture-replay-feature.md) | Blocked backlog | 0/7 | Replay R3, render ownership, and mandatory CPU gate complete |

## Binding Decisions And Open Decisions

Binding:

- DX12 explicit helpers own live barriers; RenderGraph does not become a barrier
  compiler.
- No exceptions in engine code; recoverable failures must propagate rather than
  disappear.
- `Run` remains only process/frame composition after five named ownership
  extractions.
- Scene-lifetime physics ownership is promoted through
  `SceneController`/`PhysicsScene`; `Run` wires it and `GameModelCollection`
  stops owning `PhysicsEngine`.
- Inspect and Editor share one stable selection identity; workspace-specific
  gesture/presentation state remains separate.
- Completed files are deleted rather than archived in the tip tree.

Open and blocking:

- CI: register a GPU-capable Windows/DX12 runner before making runtime CI a
  required check; CPU Windows CI does not wait for that runner.

## Engine Cleanup Campaign Closure Gate

Before deleting `runtime-shell-decomposition.md` or closing the engine-cleanup
campaign:

- [ ] One final independent ownership review covers the complete logical `Run`
  surface, every extracted owner, and the current high-fan-in/mega-module
  inventory. It records zero credible god-object, shared-state-hub, callback-bag,
  forwarding-facade, or renamed-compatibility findings.
- [ ] The review's method/field ownership inventory, inspected hotspot list,
  concrete evidence, and zero-finding verdict are committed under
  `Agentic/Reports/<date>/`. Any credible finding reopens its owning plan and
  blocks campaign closure.

## Plan Closure Checklist

Before deleting any plan:

- [ ] Every phase checkbox is complete with evidence.
- [ ] All hard decisions are resolved in the plan or a binding owner record.
- [ ] Required focused and broad gates passed from final source/data state.
- [ ] New/changed test targets are registered in the CPU umbrella.
- [ ] Source comment audit requirements are satisfied.
- [ ] Current measurements and deletion proofs are rerun.
- [ ] Session state and this inventory are updated in the same commit.
- [ ] The plan and completed execution checklist are deleted; commit history is
  the archive.
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
| [15 Review gaps (2026-07-09)](../../engine-cleanup-plans/15-review-gaps.md) | In progress | 50% | 15.4 comment-boilerplate cleanup and 15.5 Window encapsulation are complete. Remaining direct work: 15.6 stale comment/Common.h hygiene. |

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
| [dx12-post-final-cleanup](TODO/dx12-post-final-cleanup.md) | Planned | 0% | DX12 post-chain cleanup from the 2026-07-11 review: delete dead cloud/noise shader code, consolidate the duplicated god-ray march into the half-res volumetric pass, bloom cost cleanup, named style modes, cinematic config dedupe. Progress checklist: `TODO/dx12-post-final-cleanup-progress.md`. |
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
- **FAC-005 public physics API boundary** — completed through engine-cleanup
  plan 14 on 2026-07-10. Public physics signatures no longer expose `GameModel`,
  raw dense `modelIndex`/`modelCount` authority, or public `PhysicsEngine`
  solver-container accessors. Existing dense-row readers go through
  `PhysicsEngineStoreQueries` until narrower handle/view queries replace them.
  `tools\validate_physics.bat` passed with byte-exact physics output. The
  completed plan file was deleted per MASTER convention.
- **Facade retirement rule** - completed through engine-cleanup plans 10, 13,
  and 14 on 2026-07-10. `IRenderBackend`, `EngineContext`, cached DX12 aliases,
  `SimulationController`, and FAC-005 public physics API leaks reached the
  structural graduate/delete end state. The completed plan 13 file was deleted
  per MASTER convention.
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

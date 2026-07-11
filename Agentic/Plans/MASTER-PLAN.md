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
9. 2026-07-11 gap-review plans (table below) slot in after their named start
   conditions; entity-model-endgame is decision-blocked first.

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

## Planned Architecture Work (2026-07-11 gap review)

Added from the 2026-07-11 architecture gap review; written before the same
day's overnight completions landed, then reconciled against them on merge.
Reconciliation notes live inside each plan.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [dx12-post-final-cleanup](TODO/dx12-post-final-cleanup.md) | Planned | 0/6 | Independent; shader edits coordinate with shader-pipeline-modernization if both run |
| [shader-pipeline-modernization](TODO/shader-pipeline-modernization.md) | Planned | 0/8 | Coordinate with render-backend-decomposition texture/PSO owner split and shadow-edge-quality root-signature work — root-signature ownership must move once, not twice |
| [render-visibility-architecture](TODO/render-visibility-architecture.md) | Planned | 0/7 | Independent; P0 instrumentation can start any time |
| [sim-render-interpolation](TODO/sim-render-interpolation.md) | Planned | 0/5 | Independent; P1 capture-determinism guard lands first |
| [editor-undo-redo](TODO/editor-undo-redo.md) | Planned | 0/5 | Unblocked — interaction-state-machine completed 2026-07-11; build on its command/gesture surface |
| [data-format-versioning](TODO/data-format-versioning.md) | Planned | 0/5 | Rescoped 2026-07-11: scene schema versioning (v2 + v1 upgrade) already shipped with physics-authority C1b; remaining scope is assets/hulls/cfg + the uniform policy and migration tool |
| [engine-config-decomposition](TODO/engine-config-decomposition.md) | Planned | 0/5 | Independent; physics-default moves isolated behind byte-exact `validate_physics` |
| [entity-model-endgame](TODO/entity-model-endgame.md) | **Decision-blocked** | 0/5 | Ownership decision conflict: the plan recorded a Run-owned `SimulationController` per the 2026-07-11 morning owner answers, but the same day's completed physics work implemented `SceneController`/`PhysicsScene` ownership (and plan 13 previously retired a `SimulationController` facade). Owner must reconcile before any slice starts — see the plan's Decision Conflict section |

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [instant-prediction-velocity-chaos](TODO/instant-prediction-velocity-chaos.md) | In progress (paused) | 1/50 checklist items | Progress checklist: `TODO/instant-prediction-velocity-chaos-progress.md`; resume per owner priority |
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
- Entity-model ownership reconciliation (see entity-model-endgame row above):
  confirm `SceneController`/`PhysicsScene` ownership stands (it is implemented
  and validated) or direct a replacement; also confirm whether the unified
  `EntityId` registry is still wanted now that per-subsystem stable identity
  (`PhysicsBodyHandle`/`ReplayBodyId`/`PhysicsSceneObjectId`) shipped.

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

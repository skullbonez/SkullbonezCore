# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-11
Status: Authoritative inventory of every live repository plan

## Inventory Rules

1. Live implementation plans are listed here and stored under `TODO/`, except
   the active engine-cleanup campaign file.
2. Completion uses checked-phase counts, not subjective percentages. Partial
   phases are named as partial and do not increment the completed count.
3. A checkbox closes only with its acceptance evidence and required validation.
4. Completed plans/checklists are deleted; git history is the archive. A
   completed plan may remain temporarily only when this file explicitly marks
   it as evidence for an unmet aggregate closure gate; delete it as soon as
   that gate passes.
5. Every dependency link must resolve to a live file. A missing link is a plan
   defect and blocks the dependent phase.
6. A plan must name owner, problem/evidence, goal, non-goals where needed,
   phases, dependencies/decisions, acceptance, and validation.
7. Source measurements are dated and scoped; historical numbers are not reused
   as current evidence.
8. God-object cleanup is reviewed across logical types/modules, not individual
   files. A short facade, shared context, callback bag, or forwarding owner does
   not satisfy an ownership deletion proof.
9. `Agentic/Plans/WNF/` holds owner-parked "will not do now" plans. Agents
   ignore that folder entirely — do not list, resume, update, or delete its
   contents unless the owner explicitly moves a file back out of it.
10. Every DX12 modification must complete a crash-free graphics-stress run of
    at least 10 seconds before commit/PR handoff. The standard bounded proof is
    `tools\run_graphics_stress.bat 1`; record the command, measured runtime,
    and successful exit evidence alongside the normal DX12 renderer gate.

## Commit Progress Contract

Every commit produced by a plan runner must begin with the owning plan, that
plan's completed task count after the commit, and overall portfolio completion
after the commit:

```text
<PLAN_NAME>, TASK <DONE> / <TASK_COUNT>, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

Example with ten 10-task plans: if plans 1 and 2 are complete and plan 3 is at
5/10, the correct overall value is 25/100 = 25%:

```text
Plan 3, TASK 5 / 10, 25% OVERALL COMPLETE — implement the current slice
```

Rules:

1. `DONE` is the owning plan's completed ledger tasks after the commit, not the
   ordinal number of the commit or the number of raw Markdown checkboxes.
2. `OVERALL_PERCENT` is `round(100 * portfolio done / portfolio total)` using
   the authoritative ledger below. Never estimate it subjectively.
3. Companion/progress checklists do not add a second denominator. Their count
   is represented by the owning plan's ledger row. The prediction plan is the
   current deliberate 50-task exception because its execution checklist is the
   accepted task source.
4. Completed-plan ledger rows remain after their plan files are deleted. They
   preserve arithmetic only; git history and reports remain the evidence
   archive. New or rescoped plans update the ledger and denominator in the same
   commit.
5. One plan owns each plan-runner commit. Split unrelated plan work. For an
   unavoidable aggregate governance/documentation commit, use `MASTER-PLAN`
   with the bounded governance task count and list every affected plan in the
   body; the overall percentage still comes from this ledger. Commits made
   outside a plan runner use normal commit subjects and do not claim plan
   progress.
6. Every plan-implementation prompt must include the fully resolved required
   first line before implementation begins. `AGENTS.md` and the orchestrator
   skill repeat this requirement so it is present in agent prompts, not merely
   discoverable here.

### Portfolio Progress Ledger

Scope: every concrete non-WNF plan in the current MASTER portfolio. The engine-
cleanup campaign meta-plan is excluded because its work is represented by the
concrete plan rows and counting it would duplicate tasks.

| Plan | Done | Tasks | Plan complete |
|---|---:|---:|---:|
| validation-gate-integrity | 5 | 6 | 83% |
| dx12-failure-propagation | 6 | 6 | 100% |
| behavioral-test-depth | 6 | 6 | 100% |
| runtime-shell-decomposition | 27 | 27 | 100% |
| runtime-ui-control-architecture-cleanup | 7 | 7 | 100% |
| interaction-state-machine | 6 | 6 | 100% |
| replay-architecture-and-right-sizing | 6 | 6 | 100% |
| physics-authority-and-identity | 16 | 16 | 100% |
| render-backend-decomposition | 8 | 8 | 100% |
| stale-plan-reference-cleanup-15.6-checklist | 86 | 86 | 100% |
| dx12-post-final-cleanup | 6 | 6 | 100% |
| shader-pipeline-modernization | 0 | 8 | 0% |
| render-visibility-architecture | 0 | 7 | 0% |
| sim-render-interpolation | 0 | 5 | 0% |
| editor-undo-redo | 0 | 5 | 0% |
| data-format-versioning | 0 | 5 | 0% |
| engine-config-decomposition | 4 | 5 | 80% |
| entity-model-endgame | 4 | 4 | 100% |
| instant-prediction-velocity-chaos | 52 | 52 | 100% |
| shadow-edge-quality | 0 | 5 | 0% |
| **Portfolio total** | **239** | **276** | **87%** |

## Current Execution Priority

For maximum impact with minimal rework, use this binding critical path:

`renderer ownership` → `DX12 cleanup` →
`engine-config decomposition` → `shader contract` →
`visibility` → `shadows` → `interpolation` → `editor` → `data versioning`

1. **Engine-cleanup aggregate review and plan deletion — parallel lane.** Run
   review preparation alongside the critical path rather than as another serial
   implementation campaign. Fix every credible ownership finding, pass the
   closure gate, and delete the eight retained completed plans to remove stale
   control-plane noise.
2. **`dx12-post-final-cleanup`.** Delete dead shaders before inventorying or
   baking them, then consolidate god rays, reduce bloom cost, name style modes,
   and complete config deduplication before shader manifests, reflection data,
   and visual references lock in the surviving surface.
3. **`engine-config-decomposition`.** Start after DX12 cleanup phase 5 so the
   final cinematic/shadow config shape moves directly into domain structs
   without reorganizing duplicated fields twice.
4. **`shader-pipeline-modernization` P0-P5.** With the surviving shader set and
   A2 owner established, execute inventory, offline DXC, reflection contracts,
   root-signature consolidation, pipeline cache, then the bindless decision.
   Record P5's decision before shadow S1. P6 hot reload is optional follow-up.
5. **`render-visibility-architecture`.** P0 instrumentation may start at any
   time; implementation waits for stable backend ownership. Complete main,
   shadow, reflection, and instancing culling before final shadow-quality work
   so its GPU budget reflects the actual visible workload.
6. **`shadow-edge-quality`.** S0 baseline capture may run earlier. S1 waits for
   backend A2, shader P3, shader P5's binding decision, and visibility closure.
   Then execute filtering, snapping/bias, and only afterward decide whether
   cascades or clipmaps are necessary.
7. **`sim-render-interpolation`.** Begin after entity identity and renderer
    ownership stabilize; avoid churning presentation transforms, cameras,
    capture timing, and replay across moving foundations.
8. **`editor-undo-redo`.** Interaction ownership is ready, but history must
    target final `PhysicsSceneObjectId` and post-`GameModelCollection` scene
    APIs, so entity-model closure is a hard prerequisite.
9. **`data-format-versioning`.** Asset/hull preparation is independent, but
    schedule delivery here. The `engine.cfg` portion waits for config
    decomposition so version plumbing targets the surviving parser/domain
    structure once.

Validation V3 administration remains an external parallel lane: prove
`merge_group`, require the hosted CPU check, and activate a trusted-main/manual
DX12 runner without exposing persistent infrastructure to public-PR code.

## Engine Cleanup Campaign

| Plan | State | Verified basis | Next work |
|---|---|---|---|
| [15 review gaps](../../engine-cleanup-plans/15-review-gaps.md) | In progress | 15.4 and 15.5 complete; 15.1-15.3 delegated | Execute the 15.6 inventory checklist; owning TODO plans close the other findings |

## Active Architecture, Safety, And Test Plans

Every Complete row retained in this table is temporary engine-cleanup campaign
closure evidence under inventory rule 4. Delete those files together when the
aggregate closure gate passes; do not treat retention as a permanent archive.

| Plan | State | Verified phase count | Next blocking action |
|---|---|---:|---|
| [validation-gate-integrity](TODO/validation-gate-integrity.md) | Externally blocked | 5/6 | Hosted CPU PR proof now passes; V3 still needs merge-group proof, required branch protection, and trusted DX12 runner administration |
| [dx12-failure-propagation](TODO/dx12-failure-propagation.md) | Complete | 6/6 | Retain as closure evidence; failure propagation, transactional recreation, device-loss teardown, and fault injection are proven |
| [behavioral-test-depth](TODO/behavioral-test-depth.md) | Complete | 6/6 | Retain as closure evidence; named tests, four mutation drills, stop proofs, and final gates pass |
| [runtime-shell-decomposition](TODO/runtime-shell-decomposition.md) | Complete | 27/27 completed items | Retain as closure evidence; final inventory, deletion proofs, adversarial review, and required gates pass |
| [runtime-ui-control-architecture-cleanup](TODO/runtime-ui-control-architecture-cleanup.md) | Complete | 7/7 | Retain as closure evidence; 96-file inventory, shared surfaces, deletion proofs, review fixes, and final gates pass |
| [interaction-state-machine](TODO/interaction-state-machine.md) | Complete | 6/6 remaining phases | Retain as closure evidence; typed gesture payload, native capture, focus/UI crossing, deletion proofs, and repeat adversarial review pass |
| [replay-architecture-and-right-sizing](TODO/replay-architecture-and-right-sizing.md) | Complete | 6/6 | Retain as closure evidence; stable identity, bounded memory, named gates, rollback proof, and source-size justifications are complete |
| [physics-authority-and-identity](TODO/physics-authority-and-identity.md) | Complete | 16/16 current items | Retain as closure evidence; handle-owned mutation, coordinated lifecycle, stable runtime identity, full gate, and repeat adversarial review complete |
| [stale-plan-reference-cleanup-15.6-checklist](TODO/stale-plan-reference-cleanup-15.6-checklist.md) | Complete | 86/86 files | Retain as reconciled evidence; no source rows remain |

## Planned Architecture Work (2026-07-11 gap review)

Added from the 2026-07-11 architecture gap review; written before the same
day's overnight completions landed, then reconciled against them on merge.
Reconciliation notes live inside each plan.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [shader-pipeline-modernization](TODO/shader-pipeline-modernization.md) | Planned | 0/8 | After A2, DX12 cleanup, and config decomposition; execute P0-P5 in order and record P5 before shadow S1; P6 is optional follow-up |
| [render-visibility-architecture](TODO/render-visibility-architecture.md) | Planned | 0/7 | P0 instrumentation may start any time; implementation follows stable backend ownership and closes before shadow-quality implementation |
| [sim-render-interpolation](TODO/sim-render-interpolation.md) | Planned | 0/5 | After entity-model closure and stable renderer ownership; P1 capture-determinism guard lands first |
| [editor-undo-redo](TODO/editor-undo-redo.md) | Planned | 0/5 | After entity-model closure; build history on final `PhysicsSceneObjectId` and post-`GameModelCollection` scene APIs |
| [data-format-versioning](TODO/data-format-versioning.md) | Planned | 0/5 | Deliver after editor; asset/hull preparation is independent, but `engine.cfg` waits for config decomposition; scene v1→v2 remains the precedent |
| [engine-config-decomposition](TODO/engine-config-decomposition.md) | Active | 4/5 | Domain-owned binding tables now share one allocation-free ordered lookup/dump traversal with exact row and runtime-dump evidence; run closure audit and independent review next |

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](TODO/shadow-edge-quality.md) | Planned | 0/5 | S0 may run early; S1 waits for backend A2, shader P3/P5 decision, and visibility closure |

Fracture replay was moved to `WNF/` by the owner on 2026-07-11 (inventory
rule 9 applies — it is not live work and is not tracked here).

## Binding Decisions And Open Decisions

Binding:

- DX12 explicit helpers own live barriers; RenderGraph does not become a barrier
  compiler.
- Any DX12 modification requires a crash-free graphics-stress run lasting at
  least 10 seconds; `tools\run_graphics_stress.bat 1` is the standard bounded
  proof.
- No exceptions in engine code; recoverable failures must propagate rather than
  disappear.
- `Run` remains only process/frame composition after five named ownership
  extractions.
- Scene-lifetime physics ownership is promoted through
  `SceneController`/`PhysicsScene`; `Run` wires it and `GameModelCollection`
  stops owning `PhysicsEngine`.
- Inspect and Editor share one stable selection identity; workspace-specific
  gesture/presentation state remains separate.
- Completed files are deleted rather than archived in the tip tree, except for
  the temporary aggregate-closure evidence allowed by inventory rule 4.
- Root-signature work executes in one ownership sequence: render-backend A2
  establishes the pipeline owner, shader P1-P3 modernizes and consolidates its
  contract, and shadow S1 extends that surviving contract.
- Scene schema v2 and its deterministic v1 upgrade are the versioning-policy
  precedent. Other formats adopt the same semantics without renaming the scene
  field, resetting its version history, or adding a competing scene migration.
- The Current Execution Priority critical path is binding. Plan-local work may
  run early only where that section explicitly names a preparation or parallel
  lane; it must not cross a listed dependency barrier.
- 2026-07-11 owner ruling (definitive): no `SimulationController` — the
  implemented `SimulationSystem` pacing / `SceneController` ownership / `Run`
  frame-order split stands. No unified `EntityId` registry —
  `PhysicsSceneObjectId` is the engine's single cross-system object identity;
  per-subsystem handles remain the hot-path currency.

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

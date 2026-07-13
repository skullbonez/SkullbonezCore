# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-13
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
| shader-pipeline-modernization | 8 | 8 | 100% |
| render-visibility-architecture | 7 | 7 | 100% |
| sim-render-interpolation | 5 | 5 | 100% |
| editor-undo-redo | 5 | 5 | 100% |
| data-format-versioning | 5 | 5 | 100% |
| engine-config-decomposition | 5 | 5 | 100% |
| entity-model-endgame | 4 | 4 | 100% |
| instant-prediction-velocity-chaos | 52 | 52 | 100% |
| shadow-edge-quality | 5 | 5 | 100% |
| dx12-descriptor-and-handle-lifetime | 5 | 5 | 100% |
| determinism-contract-hardening | 4 | 4 | 100% |
| upload-arena-overflow-policy | 4 | 4 | 100% |
| frame-view-calling-convention | 4 | 4 | 100% |
| render-interface-and-workerpool-slimming | 5 | 5 | 100% |
| runtime-contract-enforcement | 5 | 5 | 100% |
| adversarial-review-round-3 | 10 | 10 | 100% |
| replay-prediction-fidelity-probe | 5 | 5 | 100% |
| replay-monolith-decomposition | 0 | 9 | 0% |
| **Portfolio total** | **317** | **327** | **97%** |

## Current Execution Priority

For maximum impact with minimal rework, use this binding critical path:

`replay-monolith-decomposition → validation-gate V3 external administration`

0. **Replay architecture lane — live.** The prediction fidelity prerequisite
   is complete with closure evidence in
   `../Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md`. Execute
   `TODO/replay-monolith-decomposition.md` (M0→M8); the permanent fourth replay
   scrub step is its binding divergence detector.
1. **Validation-gate V3 — blocked external lane.** Repository implementation is
   complete. Remaining work requires a real `merge_group` proof, required CPU
   branch protection, and trusted/ephemeral DX12 runner administration.
2. **Adversarial-review round 3 — locally complete.** All ten tasks and the
   final independent review are closed; evidence lives in
   `../Reports/2026-07-13/adversarial-review-round-3-closure.md`.
3. **Adversarial-review remediation round 1 — locally complete.** All five
   active 2026-07-12 remediation plans are closed. The comment-rot sweep
   remains owner-parked in `WNF/` (no comment changes yet), so it is not live
   work or part of the portfolio ledger.
4. **Adversarial-review round 2 — locally complete.** EngineLog fatal-path
   thread safety, SpatialGrid input validation, AmortizedTask lifetime guards,
   and worker-pool exception-plumbing removal are complete and validated.

## Engine Cleanup Campaign

| Plan | State | Verified basis | Next work |
|---|---|---|---|
| [aggregate closure](../Reports/2026-07-12/engine-cleanup-aggregate-closure.md) | Complete | Independent full-module and narrow repeat reviews are clear | Campaign closed; eight retained evidence plans deleted |

## Active Architecture, Safety, And Test Plans

| Plan | State | Verified phase count | Next blocking action |
|---|---|---:|---|
| [validation-gate-integrity](TODO/validation-gate-integrity.md) | Blocked | 5/6 | V3 needs merge-group proof, required branch protection, and trusted/ephemeral DX12 runner administration |
| [replay-prediction-fidelity-probe](../Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md) | Complete | 5/5 | Engine snapshot completeness and the permanent 120-tick predicted/live replay gate are closed |
| [replay-monolith-decomposition](TODO/replay-monolith-decomposition.md) | Live | 0/9 | Complete M0/M1, then extract the five owners (presentation → timeline/scrubber → authoring → prediction) behind a thin `ReplayRuntime` composition root |

## Planned Architecture Work (2026-07-11 gap review)

Added from the 2026-07-11 architecture gap review; written before the same
day's overnight completions landed, then reconciled against them on merge.
Reconciliation notes live inside each plan.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [sim-render-interpolation](../Reports/2026-07-12/sim-render-interpolation-closure.md) | Complete | 5/5 | Allocation-free live interpolation, deterministic capture pinning, coherent cameras/listener, review, and final gates complete |
| [editor-undo-redo](../Reports/2026-07-12/editor-undo-redo-closure.md) | Complete | 5/5 | Fixed command history, stable-id recreation, exact state-fingerprint proof, lifecycle clearing, review, and final gates complete |
| [data-format-versioning](../Reports/2026-07-12/data-format-versioning-closure.md) | Complete | 5/5 | Asset/config v0 upgrades, hull v1 window, no-downgrade writers, migration tool, review, and final gates complete |

## Adversarial Review Remediation (2026-07-12)

Source: 2026-07-12 independent adversarial source review of the DX12 backend,
physics core, math layer, and frame loop (findings referenced with file:line
evidence inside each plan). Grouped by owner ruling into must-do and
nice-to-have lanes.

Must do:

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [dx12-descriptor-and-handle-lifetime](../Reports/2026-07-12/dx12-descriptor-and-handle-lifetime-closure.md) | Complete | 5/5 | Fence-safe SRV/UAV and framebuffer RTV/DSV reclamation, generation handles, and 131-turnover stress proof complete |
| [determinism-contract-hardening](../Reports/2026-07-12/determinism-contract-hardening-closure.md) | Complete | 4/4 | Explicit `/fp:precise` pins, complete chunk-accumulation audit, documented MSVC v143 envelope, and byte-exact isolated physics gates complete |
| [upload-arena-overflow-policy](../Reports/2026-07-12/upload-arena-overflow-policy-closure.md) | Complete | 4/4 | Per-category accounting, bounded replay ribbons, phase-aware caller drops, and flush-free stress/perf/DX12 proof complete |

Nice to have (start only after the must-do lane closes):

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [frame-view-calling-convention](../Reports/2026-07-12/frame-view-calling-convention-closure.md) | Complete | 4/4 | Four non-copyable capability slices replace high-arity frame calls without recreating a universal context bag |
| [render-interface-and-workerpool-slimming](../Reports/2026-07-12/render-interface-and-workerpool-slimming-closure.md) | Complete | 5/5 | Typed fixed-ring dispatch, measured interface retention, independent review, and full/perf/DX12 stress gates complete |

## Adversarial Review Remediation Round 2 (2026-07-12)

Source: second 2026-07-12 adversarial pass over post-remediation source
(worker primitives, logging/fatal path, broadphase input validation). All four
findings are consolidated into one plan because they share a theme — internal
contracts enforced by convention instead of code — and none justifies a
separate validation cycle.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [runtime-contract-enforcement](../Reports/2026-07-12/runtime-contract-enforcement-closure.md) | Complete | 5/5 | Mutex-owned logger, bounded fatal probes, broadphase input guards, task lifetime enforcement, and final gates complete |

Recorded clean in the same pass (no plan needed): `Fence` signal/wait
ordering, upload-reservation saturating arithmetic, replay `make_unique`
allowlist compliance, descriptor free-list double-alloc/double-free checks,
and `strtod`-based config parsing.

Owner-parked 2026-07-12 (inventory rule 9 applies — not live work, not in the
ledger): `WNF/dx12-frame-path-comment-rot-sweep.md`. The owner ruled no
comment changes yet; the Present GPU-timer dead-store finding it carries stays
recorded there for when the plan is restored.

Deliberately not planned (owner may revisit): AoS `PhysicsBodyRecord` layout
reshaping and terrain warm-start/clamp heuristic replacement — both working,
honestly documented, and baseline-entangled; undertake only with a concrete
perf or stacking-stability motivation. Repeated glossary-header deduplication
is available as a documentation-only plan if the owner wants it (currently
excluded by the same no-comment-changes ruling).

## Adversarial Review Remediation Round 3 (2026-07-13)

Source: 2026-07-12 owner-commissioned adversarial architecture review of the
full source tree at the `nightrunner-11th-july` tip, re-verified against the
`nightrunner-12th-july` tip on 2026-07-13. Owner ruled findings in or out on
2026-07-12/13; in-scope work is consolidated into one ten-task plan ordered
header hygiene → mechanical namespace/ownership passes → C++20/`std::span` →
solver SIMD → DX12 bindless and frame headroom.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [adversarial-review-round-3](../Reports/2026-07-13/adversarial-review-round-3-closure.md) | Complete | 10/10 | Three-frame SM6.6 bindless raster path, measured perf budget, independent review, and final gates complete |

Owner-ruled out of scope in this round (recorded so they are not re-litigated):
replay subsystem right-sizing, unit-test depth expansion, sleep parallel-array
consolidation, `Init.cpp` decomposition, and any `RenderBackendDX12`
re-partitioning beyond the bindless/frame-headroom task. The unpinned-`/fp`
finding was already closed by `determinism-contract-hardening`.

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](../Reports/2026-07-12/shadow-edge-quality-closure.md) | Complete | 5/5 | Fixed Poisson filtering, detail-first terrain sampling, texel snapping, measured presets, and no-cascades decision complete |

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

- [x] One final independent ownership review covers the complete logical `Run`
  surface, every extracted owner, and the current high-fan-in/mega-module
  inventory. It records zero credible god-object, shared-state-hub, callback-bag,
  forwarding-facade, or renamed-compatibility findings.
- [x] The review's method/field ownership inventory, inspected hotspot list,
  concrete evidence, and zero-finding verdict are committed under
  `Agentic/Reports/<date>/`. Any credible finding reopens its owning plan and
  blocks campaign closure.

## Plan Closure Checklist

Before deleting any plan:

- [x] Every phase checkbox is complete with evidence.
- [x] All hard decisions are resolved in the plan or a binding owner record.
- [x] Required focused and broad gates passed from final source/data state.
- [x] New/changed test targets are registered in the CPU umbrella.
- [x] Source comment audit requirements are satisfied.
- [x] Current measurements and deletion proofs are rerun.
- [x] Session state and this inventory are updated in the same commit.
- [x] The plan and completed execution checklist are deleted; commit history is
  the archive.

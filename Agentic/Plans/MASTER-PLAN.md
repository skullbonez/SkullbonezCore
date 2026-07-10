# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-10
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
| [validation-gate-integrity](TODO/validation-gate-integrity.md) | In progress | 4/6 | V3 CPU/runtime CI, then V4 sanitizer/static analysis |
| [dx12-failure-propagation](TODO/dx12-failure-propagation.md) | In progress | 4/6 | D4 partial initialization and guarded optional-feature failure propagation |
| [behavioral-test-depth](TODO/behavioral-test-depth.md) | In progress | 3/6; P3/P5 partial | P3 with scene/entity ownership C1-C3; then remaining P5/P6 drills |
| [runtime-shell-decomposition](TODO/runtime-shell-decomposition.md) | In progress | 3/26 remaining items | Complete B1b/B1d-B1f pointer, focus, cursor, native-capture, and later-poll ownership |
| [runtime-ui-control-architecture-cleanup](TODO/runtime-ui-control-architecture-cleanup.md) | Planned | 0/7 | U0 tracked UI-surface inventory |
| [interaction-state-machine](TODO/interaction-state-machine.md) | In progress | 0/6 remaining phases | I4 capture/focus behavior with CPU + interaction proof |
| [replay-architecture-and-right-sizing](TODO/replay-architecture-and-right-sizing.md) | Planned | 0/6 | R0 reconciled file/state/memory inventory |
| [physics-authority-and-identity](TODO/physics-authority-and-identity.md) | In progress | 4/16 current items | C1b explicit schema-versioned scene object ids, then scene-owned metadata/creation C2-C4 |
| [render-backend-decomposition](TODO/render-backend-decomposition.md) | In progress | 0/8 remaining items | Wait for DX12 D0-D3, then texture-owner split |
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

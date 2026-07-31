# Solver Diagnostic Hot-Path Cost - HP2 Payload Elimination

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/solver-diagnostic-hot-path-cost.md`
Phase: HP2 - eliminate payload construction on the counting path
Source base: `bd9ffff2`
Status: COMPLETE

## Result

The count-only pipeline path now observes canonical event cardinality without
constructing `PhysicsPipelineRecord` values. The step-owned full/count choice is
hoisted before producer row loops or becomes a compile-time specialization.
Count-only execution therefore performs no trace-only body-position sampling,
record filling, diagnostic magnitude calculation, or per-row capacity
comparison.

The full-record lane preserves the existing stage order, row fields, and
4,096-event saturation. No callback, sink interface, context bag, service bag,
or new allocation owner was introduced.

## Producer Closure

| Producer owner | Count-only behavior | Full-record behavior |
|---|---|---|
| Broadphase candidate and Debug sleep-pruned rows | Batches the already validated candidate-list cardinality; Debug preserves the two historical event classes. | Retains the canonical sorted pair walk and existing position/magnitude payload. |
| Object narrowphase, serial | `PhysicsWorld` selects `ProcessObjectNarrowphasePair<false>` before the pair loop; events carry kind/count evidence in a scalar while the payload optional remains disengaged. The sequencer submits one batch after the loop. | Selects `<true>`, engages each payload optional, and commits complete rows in original order. |
| Object narrowphase, parallel | Each worker callable selects one compile-time island lane before that island's pair loop. Count-only event slots retain no payload, and the sequencer submits one batch after the ordered commit walk. | Engages one payload per observed event slot and commits later in original pair order. |
| Terrain hit | `PhysicsWorld` selects `PrepareCandidateCommit<false>` before the awake-body loop, leaves the payload optional disengaged, and submits one count batch after the loop. | `<true>` samples the prior terrain payload and commits it at the same sequencer gap. |
| Persistent contact solver | Manifold, terrain-row, precompute, iteration, writeback, correction, and cache phases use `RetainPipelineRecords=false`; each event increments a stage-owned scalar. | The same phases retain their prior bounded record list and capacity checks. |
| Sleep island and transition rows | `RunIslandStageMode<false>` batches awake decision rows and transition events; `ApplyTransitionsMode<false>` constructs no record. | `<true>` records at the former call positions. |

`PersistentContactSolverSideEffects` uses exactly one pipeline representation per
step: ordered rows in full mode, or `pipelineEventCount` in count-only mode.
`PhysicsWorld::CommitContactSolverConsequences` commits the matching
representation into the step recorder, whose batch addition saturates exactly
like repeated `Record` calls.

`PhysicsPipelineTraceRecorder::RecordEvents` rejects full-record mode through
Lane F. This preserves the owner invariant that retained-record count and
canonical event count cannot diverge through accidental API misuse.

## Structural And Compiled Evidence

Source inspection confirms every producer-side `PhysicsPipelineRecord`
construction is either:

- inside an `if constexpr (RetainPipelineRecords)` specialization;
- inside a helper reached only from a full-record branch outside the row loop;
  or
- part of recorder-focused test data.

The Profile `PersistentContactSolver.obj` disassembly contains all four
`SolveRowsIterations<CollectConvergenceDiagnostics, RetainPipelineRecords>`
specializations. Bounded symbol sections report:

| Specialization | Disassembly lines | Square-root sites |
|---|---:|---:|
| `<true, true>` | 4458-4979 | 2 |
| `<true, false>` | 4980-5423 | 1 |
| `<false, true>` | 5424-5844 | 2 |
| `<false, false>` | 5845-6194 | 1 |

The one site common to all lanes is simulation-owned friction normalization.
The second site exists only in full-record lanes and is the pipeline-only
`sqrtf(accT1^2 + accT2^2)` payload magnitude. This proves the diagnostic square
root is absent from count-only code rather than merely skipped by a runtime
branch.

Artifact:
`TestOutput/validation/hp2_persistent_solver_disasm.txt`.

## Focused Coverage

- Recorder batch saturation: count batches reach 4,095, 4,096, and remain
  saturated after a 17-event overflow while retaining no rows.
- Persistent solver equivalence: full/count lanes produce identical logical
  event counts and exact body position, linear velocity, and angular velocity;
  count-only side effects retain zero rows.
- Sleep equivalence: relaxed and stretched point-joint islands report the same
  event count and counter state in both modes; count-only retains zero rows.
- Parallel narrowphase equivalence: all 256 pair slots retain the same event
  kind, full mode engages and retains body payload, and count-only leaves the
  payload optional disengaged.
- Recorder misuse coverage: a named Profile fatal child proves batch counting
  terminates when full-record retention is active.
- Profile, Debug, and Automation solution rebuilds pass with zero warnings or
  errors.

## Ownership Review

The compile-time policy does not change responsibility:

- `PhysicsPipelineTraceRecorder` owns saturation and optional row retention.
- `PhysicsStepDiagnostics` fixes mode at `BeginStep`.
- Each stage owns its canonical event-production point.
- `PhysicsWorld` remains the serial commit sequencer.
- The persistent-contact transaction continues to own row construction and
  solver phase order.

The exact current-source rulings for the persistent manifold/precompute phases
and the narrowphase pair/island kernels were refreshed after their signatures
and bodies gained compile-time trace policy. Function-complexity,
wide-signature, and synchronized Automation/Debug/Profile reachability strict
inventories pass; the rulings retain the existing concrete phase owners and do
not introduce an allowance or count budget.

## Comment Audit

`Agentic/Skills/comment-style-audit/skill.md` was applied to the touched-file
scope. The checklist is this report's producer table plus the following exact
inventory: 17 source-bearing files checked, zero deferred, zero unchecked.

- `PersistentContactSolver.cpp`, `PhysicsWorld.cpp`
- `PhysicsBroadphaseStage.cpp`
- `PhysicsContactSolverStage.cpp/.h`
- `PhysicsNarrowphaseStage.cpp/.Execution.cpp/.h`
- `PhysicsSleepController.cpp/.h`
- `PhysicsStepDiagnostics.cpp/.h`
- `PhysicsTerrainStage.cpp/.h`
- `TestPersistentContactSolver.cpp`, `TestPhysicsStageState.cpp`,
  `TestRuntimeContracts.cpp`

Every file retains a complete learning header. Updated local and header
invariants explain optional payload ownership, the full/count representation,
hoisted dispatch, deterministic event identity, and the absence of callbacks or
retained owner borrows.

## Validation

- `tools\validate_fast.bat`: PASS, including formatting, dependency and
  ownership checks, Profile/Debug builds, the complete CPU suite, and compiled
  symbol reachability; 456 cases / 2,424,707 assertions. Captured artifact:
  `TestOutput/validation/validate_fast_hp2.log`.
- `python tools/inventory_unreachable_symbols.py --repo . --strict`: PASS after
  synchronized Automation/Debug/Profile rebuilds; 79/79 rows ruled.
- Focused Profile and Debug recorder, solver, sleep, and narrowphase tests:
  PASS.
- Profile fatal-contract probe for full-mode `RecordEvents`: PASS.
- Independent review: the first pass rejected embedded payloads, row-local
  count submission, and the unenforced recorder invariant. All three findings
  were repaired. Final re-review verdict: ACCEPT, with no blocking findings or
  missing HP2 evidence.

## Deferred To HP3

HP3 owns repository-wide exactness and the measured performance result:
committed Physics CSV baselines, Replay hashes and visual golden without
refresh, enabled overlay/SkullScope identity, allocation-policy validation,
full validation, and before/after Profile timing on the HP0 `perf_1000` scene.

# Solver Diagnostic Hot-Path Cost Closure

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/solver-diagnostic-hot-path-cost.md`
Branch: `nightrunner-30th-JUL-26`
Campaign commits through implementation: `790620cf`, `b98b0e46`, `bd9ffff2`,
and `1b018642`

## Outcome

The ordinary Runtime physics path now preserves only the saturated pipeline
event count. It does not construct `PhysicsPipelineRecord` payloads, load the
trace-only body position, execute the diagnostic tangent-magnitude `sqrtf`, or
perform a row-local capacity comparison. Replay capture, prediction, the
pipeline overlay, Debug SkullScope, and the default direct/prediction engines
still select full-record mode and receive the original ordered 56-byte rows.

The change is behavior-neutral and measurably cheaper. Physics remains
byte-exact, replay visual and causal values remain exact, and the enabled
diagnostic trace is byte-identical to HP0. On the same two-pass Profile
`perf_1000` workload, mean `SolveRows` time fell from `0.120002` ms to
`0.070923` ms, a 40.90% reduction.

## Exactness

### Physics

`tools\validate_physics.bat` reproduced the committed 44,401-line
`physics_regression_varied.csv` byte-for-byte. Its SHA-256 remains
`7F6B88B290F102E57345F894A4C27C2A9201EED74CCFC1E5D213488031B72572`.

### Replay visual, causal, and durable artifact

The one-process Replay visual-fidelity gate passed:

- 17 focused cases / 75 assertions;
- 2,401 visual ticks;
- 200 moved and 175 toppled wall bricks;
- 200 causal nodes;
- one prediction generation and one presented cascade;
- 62 saved/loaded ticks;
- every visual, causal, semantic-packet, artifact-byte, prediction-artifact,
  and determinism false-pass control rejected its mutation.

The committed visual and causal goldens did not move. Their SHA-256 values
remain:

- visual: `E2214AE2F8E1676C647DF1D3AFE308D30D12501E63DED1CE4027AA403B60F714`;
- causal: `64576170593A93134A60FB449DFF9EA2C7DA795FDFC22C983144E0B1A5588D8D`.

The first ordinary invocation exposed a historical provenance defect rather
than behavioral drift. The visual golden records scene hash `5E066982...9086`,
which is exactly the SHA-256 of the committed scene's JSON values with CRLF
line endings. The repository has enforced LF for JSON since April 2026, and
the committed/current scene hash is `3B970CCE...EA10`. The golden's capture
commit contains that same LF blob, proving the earlier quaternion closure
captured a policy-violating CRLF working copy.

No golden was refreshed or reconciled. For the authoritative proof, the one
named scene was presented transiently in the exact CRLF byte representation
named by the immutable golden, the complete 6,800-frame gate passed, and a
`finally` block restored the exact committed LF bytes. Post-run, the protected
scene and baseline paths were clean and the restored scene matched the tracked
LF blob at `3B970CCE...EA10`. An independent in-memory comparison of the LF run
also found no difference across every approved final-state field and all 2,401
visual ticks; script, config, and shader provenance already matched.

### Overlay and SkullScope

The enabled full-record command was repeated for 180 fixed steps with
`--physics-debug pipeline` and `--physics-diag`. The resulting
348,925,625-byte NDJSON trace has SHA-256
`9EDDA8DDC0C092FB2B084EDDF78A9BF6273CF2374BFCCBF714E7AF50451C197D`,
exactly matching HP0. Full diagnostic rows, ordering, values, overlay input,
and SkullScope output are therefore unchanged when a consumer is active.

## Allocation Policy

The allocation-policy self-test and repository scan pass. The strict
two-generation Replay probe passes with
`predictionTrajectorySteadyStateNoReserveGrowth=true`,
unchanged reserve-growth counts, and `predictionGenerationCount=2`. The
focused scene-capacity owner test passes one case / 6,320 assertions.

Both 4,096-row, 229,376-byte full-record reservations remain necessary:

- `PhysicsStepDiagnostics.physicsPipelineTrace`;
- `PhysicsContactSolverStage.pipelineRecords`.

Both retain `PhysicsCapacityReason::PipelineRecords`. Count-only execution
leaves the lists empty, but Replay, overlay, and SkullScope may activate full
recording after scene load. Their fixed capacity prevents illegal
steady-gameplay growth while preserving the bounded diagnostic capability.

## Profile Timing

Command:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData/scenes/perf_1000.scene.json
```

Both measurements use two deterministic passes, discard frames 1-30, and
retain frames 31-1000: 970 rows per pass and 1,940 rows total.

| Scope | HP0 mean ms | HP3 mean ms | Change | HP3 pass means ms |
|---|---:|---:|---:|---:|
| `Frame/Physics` | 1.575496 | 1.469743 | -6.71% | 1.435419 / 1.504068 |
| `Frame/Physics/Narrowphase/PersistentContacts` | 0.272922 | 0.181089 | -33.65% | 0.179146 / 0.183032 |
| `Frame/Physics/Narrowphase/PersistentContacts/SolveRows` | 0.120002 | 0.070923 | -40.90% | 0.070471 / 0.071374 |

The recorded result is evidence, not a new performance budget. Raw HP3 CSV and
analysis JSON are retained under
`TestOutput/validation/solver_diagnostic_hot_path_cost/`.

## Validation

| Command | Result |
|---|---|
| `tools\validate_tests.bat` | PASS; 456 cases / 2,424,707 assertions |
| `tools\validate_physics.bat` | PASS; committed Physics CSV byte-exact |
| `tools\validate_replay_visual_fidelity.bat` | PASS; one process and all positive/negative controls, using the frozen golden's exact CRLF scene provenance as described above |
| `python tools\check_allocation_policy.py --self-test` | PASS |
| `python tools\check_allocation_policy.py --repo .` | PASS; 0 allowlist errors |
| `tools\validate_replay_allocation_policy.bat` | PASS; strict two-generation proof |
| focused scene-capacity owner test | PASS; 1 case / 6,320 assertions |
| enabled overlay/SkullScope trace | PASS; exact HP0 byte hash |
| `tools\validate_perf.bat` | PASS; DX12 and physics-bench baselines report no regressions |
| `tools\validate_full.bat` | PASS; default repository gate |
| protected-baseline `git diff --exit-code` | PASS; no Physics, Replay, DX12, or physics-bench baseline changed |

Validation logs are retained under
`TestOutput/validation/solver_diagnostic_hot_path_cost/`. The passing
one-process Replay log is
`validate_replay_visual_fidelity_crlf_provenance.log`; the generically named
log preserves the expected initial LF-provenance rejection.

## Comment Audit

HP1 audited 13/13 touched source-bearing files and HP2 audited 17/17, with zero
deferred or unchecked files. HP3 changed documentation only. The implementation
comments name the recorder owner, count/full-record invariant, capacity reason,
and the count-only construction boundary.

## Independent Review

The fresh read-only closure review returned **ACCEPT/CLEAR** with no blocking
findings. It independently reproduced the CRLF provenance hashes, all three
Profile improvement percentages from the 1,940 rows, both 4,096-by-56-byte
reservation sizes and their continued owner need, zero allocation-policy
violations, unchanged reserve-growth count, and the 4/16 ledger arithmetic.

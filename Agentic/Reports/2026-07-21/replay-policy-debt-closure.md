# Replay Policy Debt Closure

Date: 2026-07-21
Branch: `nightrunner-20th-july`
Plan: `replay-policy-debt-closure`, RP0-RP3 complete
Verdict: closed; independent review clear with no remaining findings

## Outcome

The three defects filed by the replay-boundary audit are closed without a
Replay behavior, artifact-schema, baseline, golden, screenshot, or authored-
data change.

Runtime allocation phase attribution is thread-local, matching reserve-owner
attribution. The two-generation prediction path has a repeatable strict gate
that proves frame-180 completion, zero steady-gameplay allocations, and zero
reserve-policy violations. Its per-generation task uses fixed in-place storage,
and the final five owner-zero allocations were correctly assigned to the narrow
Automation diagnostics formatter rather than hidden behind a Replay owner.

Replay, Physics, Rendering, and runtime presentation now carry
`PhysicsSceneObjectId` directly. The Replay-owned `ReplayBodyId`, duplicate raw
`replayBodyId` fields, and the definition-only
`PhysicsReplaySolverSnapshotView` and
`SceneWorld::TryQueueReplayRenderPoseOverride` facades are gone. Dense model
rows remain repairable hints. Artifact readers and writers still encode the
same fixed-width `id.value` scalars, and the durable artifact and byte-mutation
controls pass without a schema or golden change.

## Allocation Privilege Inventory

The final source census contains exactly three Replay reserve registrations,
three `REPLAY_GROWTH_OWNER_POLICIES` rows, and three published memory-stat rows.
Caps and ratified high-water evidence remain unchanged:

| Owner | Hard cap | Ratified high water | Final RP3 high water | Growths | Failed growths |
|---|---:|---:|---:|---:|---:|
| `replay_recorder_samples` | 32 MiB | 17,737,640 B | 17,737,640 B | 933 | 0 |
| `replay_solver_snapshot` | 8 MiB | 2,877,186 B | 2,877,186 B | 2 | 0 |
| `replay_prediction_working_set` | 256 MiB | 110,979,828 B | 110,942,788 B | 9,257 | 0 |

Every descriptor remains Replay-phase-only, carries an owner-specific reason,
uses a hard byte cap, and publishes high-water, growth, and failure counters.
The recorder and solver comments and allocation allowlist name the correct
owners and aggregate caps.

## Boundary And Identity Proofs

`TestOutput/agent_logs/rp3_static_closure_proofs_final.log` records:

- zero `Runtime/Replay/*` includes from Physics, Rendering, Scene, World, or
  Core;
- zero source/test/first-party-tool occurrences of `ReplayBodyId`, `replayBodyId`,
  `PhysicsReplaySolverSnapshotView`, or
  `TryQueueReplayRenderPoseOverride`;
- zero raw `sceneObjectId`/`replayBodyId` integer parameter declarations across
  Physics, Rendering, Replay, and Runtime Scene;
- exactly three Replay reserve registrations, policy rows, and stats rows; and
- no replay-campaign baseline, golden, screenshot, or CSV changes.

The RP2 336-test artifact round trip and RP3 337-test final suite both accept
the existing byte-canonical artifact encoding. The frame-exact visual gate also
accepts saved/load packet equality, prediction-state integrity, and every
semantic/byte/determinism mutation control.

The final broad terminology census found one cold diagnostic label in
`tools/replay_query.py`: editor-transform `row.values[1]` was still emitted as
`replayBodyId` even though `BuildEditorTransform` writes
`PhysicsSceneObjectId::value`. The query output now calls that unchanged scalar
`sceneObjectId`. A bounded read of the existing topology-probe artifact proves
the corrected label without changing artifact bytes or parser layouts.

## RP3 Performance Blocker And Fix

The first two RP3 performance gates failed the DX12 frame threshold at
`+17.4%/+15.6%` and `+18.7%/+18.7%` average/p50. A forced Profile rebuild
reduced the result to `+13.2%` average but still failed the 12% gate. No
baseline refresh was attempted.

A detached RP1 control worktree passed the same gate under current machine
conditions at `+1.8%/+1.2%`, proving the remaining slowdown belonged to RP2.
The semantic diff showed that deleting the four-byte duplicate ID moved
`PhysicsBodyRecord::rotationalInertia` from offset 16 to offset 12. The final
fix keeps a documented four-byte non-identity alignment lane after
`PhysicsSceneObjectId`, pins the offset with `static_assert`, and adds a focused
doctest. It does not restore a second identity authority.

The final performance gate passes at `+9.4%/+8.7%` average/p50, with the
allocation scan/guard, selected-ball structural witness, absolute budgets, and
physics comparison all clean.

## Closure Validation

| Command | Time | Result |
|---|---:|---|
| RP3 static boundary/identity/allocation/comment proofs | 2.2 s | PASS; zero forbidden surfaces, exactly three owners, 57/57 cumulative comment audit |
| `tools\validate_replay_allocation_policy.bat` (pre-fix) | 16.53 s | PASS; frame 180, zero gameplay/policy violations |
| `tools\validate_perf.bat` (attempt 1) | 83.75 s | Correctly blocked: DX12 frame average/p50 +17.4%/+15.6% |
| `tools\validate_perf.bat` (attempt 2) | 85.86 s | Correctly blocked: DX12 frame average/p50 +18.7%/+18.7% |
| detached RP1 `tools\validate_perf.bat` control | 157.28 s | PASS; DX12 frame average/p50 +1.8%/+1.2% |
| forced Profile rebuild | 37.84 s | PASS; zero warnings/errors |
| `tools\validate_perf.bat` after rebuild | 84.21 s | Correctly blocked: DX12 frame average +13.2% |
| targeted alignment build + doctest | 10.81 s + 1.48 s | PASS; 1 case / 1 assertion |
| `tools\validate_perf.bat` (final aligned tip) | 137.29 s | PASS; DX12 +9.4%/+8.7%, allocation and physics comparisons clean |
| `tools\validate_full.bat` (final aligned tip) | 145.73 s | PASS; 337 tests / 68,634 assertions, CPU umbrella and five runtime lanes |
| `tools\validate_physics.bat` (final aligned tip) | 55.30 s | PASS; handle mirror and 44,401-line CSV byte-exact |
| `tools\validate_replay_allocation_policy.bat` (final aligned tip) | 16.66 s | PASS; frame 180, zero gameplay/policy violations, three owners, zero failed growths |
| `tools\validate_replay_visual_fidelity.bat` (one engine generation) | 441.56 s | PASS; 2,401 ticks, one generation/presentation, durable artifact and all controls |
| `tools\validate_fast.bat` (review hardening) | 56.35 s | PASS; 337 tests / 68,634 assertions, zero warnings/errors |
| `tools\validate_replay_allocation_policy.bat` (hardened final tip) | 16.72 s | PASS; fresh report, frame 180, exactly two generations, zero gameplay/policy violations |
| `tools\replay_query.py` topology-artifact identity witness | 0.4 s | PASS; `sceneObjectId: 6`, no retired label, artifact read-only |
| `tools\validate_fast.bat` (final tool tip) | 55.46 s | PASS; 337 tests / 68,634 assertions, zero warnings/errors |

Final transcripts are under `TestOutput/agent_logs/` with the `rp3_` prefix.
The formal commands ran headlessly because this execution environment cannot
open a visible console; all output was mirrored to those logs.

## Independent Review

One fresh independent rubber-duck reviewed the complete campaign diff, source
censuses, current strict report/log, performance-control evidence, artifact
compatibility, final mapped gates, and ledger reconciliation. It found no
blocking issue, no missing evidence, and one non-blocking gate-hardening item:
the strict allocation script did not remove prior artifacts or independently
assert exactly two prediction generations.

The same reviewer confirmed the remediation. The script now deletes its exact
prior report/log before launch, quotes their paths, and requires
`predictionGenerationCount == 2` in addition to success, frame 180, and zero
allocation/policy violations. The mapped `validate_fast` and hardened strict
gate both pass. A second focused follow-up confirmed the cold replay-query label
now names `sceneObjectId` without changing artifact layout or bytes. Final
review verdict: clear to archive with no remaining findings. Review time was
approximately 5 minutes 5 seconds, plus the focused follow-ups.

## Comment Quality Audit

Checklist: this report section. The RP2 rerun checked 48/48 source/test files,
0 deferred. The cumulative RP0-RP3 inventory from `git diff --name-only
9e964615^ --` contains 57 source/test/substantial-tool files; all 57 are
checked, with 0 deferred and 0 unchecked. Every file has Purpose, Summary,
Glossary, Invariants, and Related sections. Local comments cover allocation
phase/owner attribution, fixed task lifetime, typed identity authority,
artifact scalar compatibility and query naming, cold-record vector alignment,
and strict-gate one-process/two-generation invariants.

## Ledger Reconciliation

RP0-RP3 are complete. Under MASTER inventory rule 4, this completed four-task
plan leaves the active/future ledger. Portfolio progress changes from 20/22
after RP3 completion to 17/18 (94%) after mechanical removal of the completed
plan. The architecture-review campaign has no remaining agent-actionable plan;
E17 extended hands-on UI acceptance remains the explicitly parked owner item.

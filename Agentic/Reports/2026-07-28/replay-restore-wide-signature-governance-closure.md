# Replay Restore / Wide-Signature Governance Closure

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/DONE/replay-restore-wide-signature-governance.md`
Status: Complete — RG0 through RG3

## Outcome

The hard 12-parameter ceiling is replaced by a qualitative mandatory-review
trigger. All 33 operations at the original trigger were freshly ruled: 28
remain with concrete-owner reasons and five Replay restore/topology operations
were repaired. The final strict inventory contains 28 current trigger rows,
all with exact current `retain-owner` rulings.

Replay restore no longer exposes the fused apply/complete operations or their
five `repair-plan` rulings. Focused operations own mutation, topology,
checkpoint, stepping, verification, branch/timeline publication, diagnostics,
and scrubber publication. `ReplayRestoreTransaction` owns and enforces the
selection-to-terminal phase walk, retained rollback evidence, branch
publication proof, and recoverable-failure arbitration.

## Independent Review And Remediation

The first RG3 review found one blocker: `Complete()` could accept a pending
branch timeline reset, and `MarkRolledBack()` could accept caller assertion
without proof that the retained live state was reapplied. The initial
phase-only test could not detect either false pass.

The correction:

- adds `TimelineResetApplied` and requires
  `TargetVerified -> TimelineResetApplied -> Complete` for branch restores;
- permits direct `TargetVerified -> Complete` only when no timeline reset is
  pending;
- records live-backup application only after the solver restore succeeds, and
  after hash verification on the V2 path;
- requires mutated state, a retained backup, and the application proof before
  `RolledBack`; and
- adds positive stateful transaction tests plus child-process fatal probes for
  pending branch completion and unproved rollback.

Repeat independent review found zero blockers and no non-blocking findings.

## Mandatory Ownership Review

1. **Aggregate ownership:** `ReplayRestoreTransaction` enforces its documented
   phase, publication, and rollback invariant; no authority-free aggregate was
   added.
2. **Capability slices:** none introduced. Concrete owner borrows remain
   synchronous and expire when the focused operation returns.
3. **Extraction scars:** none introduced. The inventory contains only the
   unrelated ruled `WorkerPool::indexFn` row.
4. **Rename evasion:** none. The deleted fused restore entry points did not
   reappear behind a context, service bag, capability partition, callback pack,
   forwarder, or reach-back.
5. **False claims:** none remain. Transaction comments, state, fatal guards,
   and tests describe and enforce the same ordering rule.

The three changed-file operations still at the trigger are
`RunStartupProbeWorkflows`, `TickProbes`, and `TickScrubberInput`. Each has an
exact current `retain-owner` ruling for one synchronous Replay operation.

## Comment Audit

The eight RG2 touched source-bearing files plus
`SkullbonezTests/TestRuntimeContracts.cpp` were inspected against the comment
guide and audit skill. The transaction invariant and fatal-probe file header
were reconciled with the final owner and state transitions.

Checked: 9. Deferred: 0.

## Validation

All final-source validation ran in isolated worktrees that excluded the
owner's uncommitted warm-start evaluation diff.

| Proof | Result |
|---|---|
| wide-signature self-test + strict scan | PASS; 28/28 current trigger rows ruled |
| authority-free aggregate strict scan | PASS; 86/86 ruled |
| extraction-scar scan | PASS; 1/1 ruled |
| `tools\validate_tests.bat` after review remediation | PASS in 35.6 s; 424/424 cases, 2,410,325 assertions |
| `tools\validate_fast.bat` | PASS in 211.8 s |
| `tools\validate_replay_visual_fidelity.bat` | PASS in 439.8 s; one process/generation/presentation, 2,401 ticks, every control |
| `tools\validate_full.bat` | PASS in 408.7 s; all CPU/runtime lanes and byte-exact 44,401-line Physics CSV |

An earlier isolated snapshot inherited Git's committed `engine.cfg` hash
`86d53f...` and was rejected before behavior comparison because the approved
local Replay fixture is `541816...`. A fresh worktree received the unchanged
approved main-checkout fixture and passed. No source, configuration baseline,
Replay baseline, visual baseline, DX12 baseline, Physics baseline, or
performance baseline was refreshed.

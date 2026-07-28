# Replay Restore / Wide-Signature RG2 Decomposition

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/TODO/replay-restore-wide-signature-governance.md`
Phase: RG2

## Outcome

The five RG0 `repair-plan` signatures are gone. Replay restore now exposes
focused operations for:

- restore-kind mutation (`RestoreSolverSampleAsLive`,
  `RestoreV2ArtifactTargetState`);
- topology reset, population, verification, checkpoint application, and
  rollback (`ResetReplayGeneratedSceneOwners`,
  `PopulateReplayGeneratedScene`, `PrepareReplayRestoreTopology`,
  `ApplyReplayRestoreCheckpoint`, `RestoreReplayLiveBackupOrFatal`);
- per-frame event application, physics advancement, and hash verification
  (`ApplyReplayRestoreEventsForFrame`,
  `AdvanceReplayRestorePhysicsFrame`,
  `ValidateReplayRestoreSteppedFrame`);
- branch/timeline publication (`ApplyRestoredBranchTimeline`); and
- terminal scrubber publication (`CompleteLiveRestoreScrubber`).

`ReplayRestoreTransaction` remains the non-copyable invariant owner for phase
order, failure/rollback arbitration, retained rollback evidence, and terminal
publication eligibility. Its focused transition matrix proves that successful
publication requires `Complete` and failed publication requires `Failed` or
`RolledBack`.

## Mandatory Ownership Review

1. **Authority-free aggregate:** none added. `BuildOutcome` returns an existing
   detached result value and does not courier owner references.
2. **Capability-slice partition:** none added. Focused operations receive their
   concrete synchronous owners directly.
3. **Extraction scar:** none introduced. Concrete `SceneController` sub-owner
   borrows expire within each operation and are not aliases used to preserve
   former member spelling.
4. **Rename evasion:** none. The fused apply/complete entry points are deleted;
   their responsibilities did not reappear behind a context, service bag,
   forwarding facade, callback pack, or owner reach-back.
5. **False ownership/comment claim:** none found. The transaction comment and
   terminal-publication guard describe and enforce the same phase rule.

The strict signature inventory reports 28 current trigger rows, all with
current `retain-owner` rulings. The five obsolete `repair-plan` rulings were
deleted rather than retained as allowances.

## Comment Audit

All eight touched source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md` and the repo comment-audit skill:

- `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
- `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`
- `SkullbonezSource/Runtime/App/ReplayRuntime.h`
- `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp`
- `SkullbonezSource/Runtime/App/ReplayValidation.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`

Checked: 8. Deferred: 0.

## Validation

Validation ran in the isolated clean worktree
`C:\SkullbonezCore-rg2-validation`, excluding the owner's uncommitted
warm-start evaluation diff:

| Proof | Result |
|---|---|
| staged diff whitespace check | PASS |
| wide-signature self-test + strict scan | PASS; 28/28 ruled |
| authority-free aggregate strict scan | PASS; 86/86 ruled |
| extraction-scar scan | PASS; 1/1 ruled |
| Profile build | PASS in 45.9 s; zero warnings/errors |
| `tools\validate_tests.bat` | PASS in 14.8 s; 423/423 cases, 2,410,303 assertions |

No baseline was refreshed. RG3 owns the independent review, Replay
visual-fidelity invocation, and broad closure gates.

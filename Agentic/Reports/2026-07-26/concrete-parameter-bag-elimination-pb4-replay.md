# Concrete Parameter-Bag Elimination PB4 Replay

Date: 2026-07-26
Implementation base: `848862f4`
Branch: `nightrunner-25th-JUL-26`

## Result

PB4 passes. Replay capture and camera focus now consume concrete domain
values, while restore is owned by a detached value-and-cursor transaction.
The following retired symbols have zero definitions/usages:

- `ReplayCaptureInput`;
- `ReplayCameraFocusRequest`;
- `ReplayRestoreOwnerContext`;
- `ReplayRestoreStepContext`;
- `ReplayRestoreEventContext`;
- `ReplaySolverSampleRestoreContext`;
- `ReplayArtifactTopologyOwners`;
- `ReplaySceneTimelineResetOwners`.

The repository-wide threshold-13 inventory remains empty.

## Concrete Replay Boundaries

`ReplayTimeline::CaptureFrame` receives the scene frame, physics step,
world-presentation sample, camera sample, launcher-visual sample, concrete
simulation owners and branch value. `ReplaySolverRecorder` derives simulation
time from the frame index and positive physics step instead of accepting a
redundant field.

`ReplayPresentation::ApplyCameraFocus` receives the selected row, row index,
focus kind, and resolved geometry. It preserves its own private restore fields;
there is no isomorphic camera-focus request.

`ReplayRestoreTransaction` owns detached backup/result values and the cursor
walk:

`Idle -> ArtifactSelected -> LiveBackupCaptured -> TopologyPrepared ->
CheckpointApplied -> TargetStepped -> TargetVerified -> Complete`.

Pre-mutation failure enters `Failed`; post-mutation failure and rollback enter
`RolledBack`. Illegal transitions are lane-F fatal. Runtime owners are
synchronous phase-method borrows and are never stored as pointers,
references, callbacks, or service bundles.

## Review Repairs

The first diagnostic review found that replacing the restore contexts had
also dropped success, failure, fallback, and probe publications. The final
transaction exposes detached diagnostics, and the App/probe boundary
synchronously publishes them through the concrete diagnostics and scene
session owners.

The first Replay artifact gate found a missing profiler frame around target
stepping. The scoped profiler owner was restored without widening the
operation: the transaction now records the interactive-scene request itself,
leaving the step function at 12 concrete parameters. The artifact rerun
passes.

## Comment Audit

Touched-source inventory: 25 files checked, 25 compliant, 0 deferred.

The audit verified or corrected:

- Replay capture-value ownership and derived simulation time;
- camera-focus input and retained presentation state;
- transaction phase order, rollback boundaries, and borrow lifetimes;
- detached diagnostic publication and bounded diagnostic text;
- profiler-frame lifetime during restore stepping;
- no-allocation and fixed-capacity Replay behavior.

Every touched source-bearing file has the required learning header and local
concept, reason, invariant, lifetime, or hazard comments where needed.

## Static Proofs

The eight retired-symbol scans return no rows.
`tools/inventory_wide_signatures.py --threshold 13 --format json` returns
`[]`. Dependency validation passes all 27 rules and 46 fixtures. The lower
UI-to-Runtime and downward-Replay include proofs return no rows.

The allocation-policy checker reports 0 allowlist errors after scanning 459
files. Introduced-line review found no replacement context/service/bindings
bag, inheritance, virtual dispatch, callback pack, type erasure, or runtime
allocation API.

## Validation

- Profile and Debug warning-as-error builds: PASS;
- transaction cursor and diagnostic-detachment tests: PASS, 2 cases / 165
  assertions;
- focused Replay round-trip and timeline tests: PASS, 66 assertions;
- `tools\validate_tests.bat`: PASS in 11.3 seconds, 399 cases / 2,403,592
  assertions;
- `tools\validate_replay_v2_artifact.bat`: PASS after restoring the scoped
  profiler frame;
- `tools\validate_replay_allocation_policy.bat`: PASS in 23.6 seconds with
  the strict two-generation policy;
- `tools\validate_replay_scrub.bat`: PASS in 395.2 seconds with one
  process/generation/presentation, 2,401 ticks, and all false-pass controls;
- `tools\validate_full.bat`: PASS in 170.1 seconds:
  - formatting and `Related:` paths;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation, Replay, and prediction runtime lanes;
  - DX12 validation with zero errors and no baseline refresh;
  - 44,401-line physics regression byte-exact.

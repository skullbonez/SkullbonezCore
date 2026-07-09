# Plan 04 Handoff - Replay Load Recoverable Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Committed slice target: convert the command-line replay v2 presentation artifact
load failure from an exception exit to a recoverable process-boundary result.

This closes only inventory row 232. Plan 04 remains in progress. Continue with
remaining Phase 3 Lane R boundaries unless the remaining
RuntimeAllocationTracker Lane F row has a clean/approved perf gate path.

## Changed

- `SkullbonezSource/Runtime/Init.cpp`
  - Replaced the `throw std::runtime_error("failed to load replay v2 presentation artifact")`
    startup path with `SbResult::Failure("Runtime/ReplayLoad", ...)` returned
    through the existing `reportRunResult` boundary.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
  - Added the Step 3.1 partial-progress note for the replay-load boundary.
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
  - Added the Phase 3 row 232 progress note.
- `Agentic/SessionState.md`
  - Updated the active Plan 04 validation and latest handoff pointer.

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 116.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 151.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 1
- Deferred: 0
- File: `SkullbonezSource/Runtime/Init.cpp`

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Elapsed: 00:01:02.4353853 from validation log timestamp delta. The wrapper
    timing line was not appended to this log.
  - Log: `Agentic/Reports/validate_full_plan04_replay_load_result_20260709.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, DX12
    screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.
- `tools\validate_replay_v2_artifact.bat`
  - Result: passed, `VALIDATE_REPLAY_V2_ARTIFACT: ALL PASSED`.
  - Exit code: 0.
  - Elapsed: 00:00:57.3068451.
  - Log:
    `Agentic/Reports/validate_replay_v2_artifact_plan04_replay_load_result_20260709.log`
  - Key evidence: replay save/load probe passed, file/target/branch restore
    probes passed, timeline mutation rejection exited 1 as expected, generated
    topology restore passed, and Profile/Debug launch binaries were left ready.

## SkullScope And Query Accounting

`validate_replay_v2_artifact` used bounded replay and SkullScope queries.
No query output was reported as truncated.

Runtime trace commands:

```bat
C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson
C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --replay-restore-failure-file-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson
C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_generated_topology.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_runtime.physicsdiag.ndjson
```

Query commands:

```bat
tools\physics_query.bat TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson restore --limit 4
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay summary
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay frame 0 --limit 4
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay body 1 --frames 0:23 --limit 8
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay hashes --frames 0:23 --limit 8
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay branches --limit 8
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay events --frames 0:23 --limit 8
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay event-cursors --frames 0:23 --limit 8
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay checkpoints --frames 0:23 --limit 8 --body-limit 2
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay export-skullscope --frames 0:5 --out TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact
tools\physics_query.bat TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson summary
```

On-disk artifacts:

- `replay_save_probe.skreplay`: 13,062 bytes.
- `replay_save_probe_runtime.physicsdiag.ndjson`: 83,316 bytes.
- `replay_save_probe.physicsdiag.ndjson`: 2,465 bytes.
- `replay_save_probe.physicsdiag.sqlite`: 204,800 bytes.
- `replay_restore_failure.physicsdiag.ndjson`: 1,580 bytes.
- `replay_restore_failure.physicsdiag.sqlite`: 204,800 bytes.
- `replay_generated_topology_probe.skreplay`: 17,234 bytes.
- `replay_generated_topology_runtime.physicsdiag.ndjson`: 189,771 bytes.

GPT-read query output:

- Restore failure physics query: 1,030 bytes.
- Replay query outputs: 18,493 bytes total, with per-query byte counts reported
  by the script:
  `physics_summary=1844`, `replay_summary=1684`, `replay_frame=988`,
  `replay_body=2461`, `replay_hashes=2396`, `replay_branches=308`,
  `replay_events=5634`, `replay_event_cursors=206`,
  `replay_checkpoints=1775`, `replay_export=167`.
- Total GPT-read query output: 19,523 bytes.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Good next small candidates to inspect with CodeGraph first:
  `Window.cpp` window creation, `Core/Timer.cpp` high-resolution counter,
  `Input.cpp` Win32 cursor recoverable rows, or
  `Scene/TestSceneParser.cpp` parser recoverable paths.
- Defer large DX12/ConvexHullShape recoverable clusters until they can be
  reviewed and validated as their own slices.

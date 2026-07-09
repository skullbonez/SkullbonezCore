# Plan 04 Probe Failures Handoff

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Step 2.1, replay/interaction probe failure conversion

## Completed

- Converted all four Plan 04 P rows away from exception exits:
  - `Run.cpp` rows 247-248.
  - `RunInteractionAutomation.cpp` rows 255-256.
- Replay save-probe startup failures now record a `RunReplayProbeState`
  failure and are reported before `Initialise()` returns control to normal
  launch.
- Interaction automation setup and frame-loop failures now write report
  `ok=false`, quit the message loop, and return an `SbResult` through
  `Run::InteractionAutomationResult()` at the process boundary.
- Current counts after the slice:
  - Strict anchored `SkullbonezSource` throw statement count: 117.
  - `SkullbonezSource` `SB_FATAL` macro invocation count: 151.
  - Strict anchored `SkullbonezTests` throw statement count: 3.

## Validation

- `tools\validate_full.bat` passed in 00:01:05.7535614.
  - Runtime boundaries: 0 errors.
  - Profile/Debug builds: 0 warnings, 0 errors.
  - DX12 validation errors: 0.
  - DX12 screenshots matched committed baselines.
  - `physics_regression_solver.csv` matched byte-exactly.
  - Log: `Agentic/Reports/validate_full_plan04_probe_failures_20260709.log`.
- `tools\validate_replay_scrub.bat` passed in 00:00:11.1128721.
  - Replay scrub probe selected replay frame 6 before live frame 23.
  - Replay restore probe restored frame 6 with solver hash
    `2656034071244054778`.
  - Log:
    `Agentic/Reports/validate_replay_scrub_plan04_probe_failures_20260709.log`.
- `tools\validate_interaction_clicks.bat` passed in 00:00:14.1358083.
  - `inspect_gizmo_click_report.json` wrote `ok=1`.
  - `replay_prediction_click_report.json` wrote `ok=1`.
  - Log:
    `Agentic/Reports/validate_interaction_clicks_plan04_probe_failures_20260709.log`.

## SkullScope Accounting

- Scrub trace command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`
- Scrub query:
  `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`
- Scrub artifacts: trace 54,932 bytes; SQLite cache 225,280 bytes; GPT-read
  query output 1,512 bytes.
- Restore trace command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`
- Restore query:
  `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`
- Restore artifacts: trace 54,912 bytes; SQLite cache 225,280 bytes; GPT-read
  query output 967 bytes.
- Total GPT-read query output: 2,479 bytes. No query output was treated as
  truncated.

## Comment Audit

Touched-file audit scope, checked 4, deferred 0:

- `SkullbonezSource/Runtime/Init.cpp`
- `SkullbonezSource/Runtime/Run.cpp`
- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`

No subsystem/full-pass checklist plan was required because this was a
touched-file audit, not a scoped subsystem comment pass.

## Remaining Work

- RuntimeAllocationTracker row 9 (`throw std::bad_alloc()`) remains the only
  known Lane F row, but the previous attempt was reverted because
  `tools\validate_perf.bat` repeatedly failed the physics-bench perf comparison
  while allocation policy and allocation guard checks passed.
- Plan 04 Phase 3 remains open: convert Lane R scene/asset/file IO, input,
  window, timer, and DX12 environment failures to recoverable results one
  boundary at a time.

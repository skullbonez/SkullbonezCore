# Plan 04 Handoff - Window Creation Recoverable Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Committed slice target: convert native Window creation failure from an exception
exit to a recoverable startup result.

This closes only inventory row 210. Plan 04 remains in progress. The current
run is paused here for the user's computer restart. Continue with remaining
Phase 3 Lane R boundaries unless the remaining RuntimeAllocationTracker Lane F
row has a clean/approved perf gate path.

## Changed

- `SkullbonezSource/Runtime/Window.h`
  - Added the `SbResult` dependency.
  - Changed `Window::CreateAppWindow` from `void` to `SbResult`.
- `SkullbonezSource/Runtime/Window.cpp`
  - Replaced `throw std::runtime_error("Window creation failed")` with
    `SbResult::Failure("Runtime/Window", "Window creation failed.")`.
  - Added a nearby Lane R comment explaining that native desktop/window
    creation can fail because of host environment state.
  - Returns `SbResult::Success()` after HWND/input binding succeeds.
- `SkullbonezSource/Runtime/Init.cpp`
  - `WinMain` now reports failed Window creation to `stderr`, shuts down
    `WorkerPool`, calls `CoUninitialize()`, and returns exit code 1 before
    `GetDC` or DX12 backend startup.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
  - Added the Step 3.1 partial-progress note for the Window creation boundary.
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
  - Added the Phase 3 row 210 progress note.
- `Agentic/SessionState.md`
  - Updated current Plan 04 validation, latest handoff pointer, and pause note.

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 114.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 151.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 3
- Deferred: 0
- Files:
  - `SkullbonezSource/Runtime/Init.cpp`
  - `SkullbonezSource/Runtime/Window.cpp`
  - `SkullbonezSource/Runtime/Window.h`

The edited source-bearing files have learning headers. The new Window failure
path has a nearby Lane R comment, and the header declaration names the
recoverable startup result behavior.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:10.2404826.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_window_create_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- First `tools\validate_full.bat`
  - Result: failed at DX12 renderer validation formatting check.
  - Exit code: 1.
  - Elapsed: 00:00:40.4021576.
  - Log:
    `Agentic/Reports/validate_full_plan04_window_create_result_20260709.log`
  - Key evidence: Profile and Debug builds passed first; formatter reported
    `Window.h` needed the header formatting pipeline.
- Formatting fix/check
  - Used `tools\align_header_inline_comments.py --repo . --write
    .\SkullbonezSource\Runtime\Window.h` to restore header alignment after
    clang-format.
  - `tools\validate_format.bat` passed.
  - Exit code: 0.
  - Elapsed: 00:00:09.3233037.
  - Log:
    `Agentic/Reports/validate_format_plan04_window_create_after_fix_20260709.log`
- Final `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:01:05.6535012.
  - Log:
    `Agentic/Reports/validate_full_plan04_window_create_result_20260709_rerun.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, DX12
    screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.

No SkullScope workflow was used in this slice.

## Rubber Duck

No rubber-duck pass was run. This was an ordinary incremental row conversion,
not a completed major plan/checkpoint.

## Pause Point

The current run should stop here for the user's computer restart after commit
and push. The goal remains incomplete, and the next agent should resume from
this handoff plus `Agentic/SessionState.md`.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Good next small candidates to inspect with CodeGraph first:
  `Input.cpp` Win32 cursor environment rows or `CaptureSystem.cpp`
  screenshot/capture recoverable rows.
- Keep `Timer.cpp`, larger DX12, `TextureCollection`, and `ConvexHullShape`
  recoverable clusters as dedicated slices because they likely have broader
  API and validation surface area.

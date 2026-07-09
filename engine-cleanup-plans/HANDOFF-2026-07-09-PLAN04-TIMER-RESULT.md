# Plan 04 Handoff - Timer Recoverable Startup Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Completed slice target: convert the high-resolution counter startup failure in
`Timer.cpp` from a constructor exception to a recoverable runtime startup
result.

This closes only inventory row 223. Plan 04 remains in progress.

## Changed

- `SkullbonezSource/Core/Timer.h/.cpp`
  - `Timer` now default-constructs inert storage.
  - Added `Timer::Initialise()` returning `SbResult`.
  - `QueryPerformanceFrequency`, zero-frequency, and startup
    `QueryPerformanceCounter` failures now return
    `SbResult::Failure("Core/Timer", ...)`.
  - Timer sampling before successful startup, or a post-startup
    `QueryPerformanceCounter` failure, is now a `SB_FATAL("Core/Timer", ...)`
    owner invariant.
- `SkullbonezSource/Runtime/RunTimerState.h`
  - Added `RunTimerState::Initialise()` to initialize all five Run-owned timers
    before scene loading or frame-loop use.
- `SkullbonezSource/Runtime/Run.cpp`
  - `Run::Initialise()` reports timer startup failure through
    `m_lastSceneLoadResult`, so `RunApp` emits the existing recoverable runtime
    failure diagnostic and exits 1 without using constructor unwinding.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
- `Agentic/SessionState.md`

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 104.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 153.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 4
- Deferred: 0
- Files:
  - `SkullbonezSource/Core/Timer.cpp`
  - `SkullbonezSource/Core/Timer.h`
  - `SkullbonezSource/Runtime/Run.cpp`
  - `SkullbonezSource/Runtime/RunTimerState.h`

The edited files have learning headers. New comments define the timer Lane R
startup boundary and explain why timer storage is inert until `Run::Initialise`
can report failures through the process boundary.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:10.9987094.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_timer_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:01:06.2075778.
  - Log:
    `Agentic/Reports/validate_full_plan04_timer_result_20260709.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, source formatting clean, DX12
    validation errors 0, DX12 screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.

No SkullScope workflow was used in this slice.

## Rubber Duck

No rubber-duck pass was run. This was an ordinary incremental row conversion,
not a completed major plan/checkpoint.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Keep larger DX12, `TextureCollection`, `ConvexHullShape`, and text/font
  recoverable clusters as dedicated slices because they have broader API and
  validation surface area.

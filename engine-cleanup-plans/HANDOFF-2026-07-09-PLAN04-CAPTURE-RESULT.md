# Plan 04 Handoff - Capture Screenshot Recoverable Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Committed slice target: convert screenshot capture/readback/file-output
failures from exception exits to recoverable runtime capture results.

This closes only inventory rows 198 through 201. Plan 04 remains in progress.
Continue with remaining Phase 3 Lane R boundaries unless the remaining
RuntimeAllocationTracker Lane F row has a clean/approved perf gate path.

## Changed

- `SkullbonezSource/Runtime/CaptureSystem.h/.cpp`
  - Added `SbResult` to `RuntimeCaptureResult` and changed
    `RuntimeCaptureSink::SaveScreenshot` to return that result.
  - Changed `CaptureSystem::SaveBackbufferBmp` and `WriteExact` to return Lane R
    `SbResult::Failure("Runtime/CaptureSystem", ...)` for unsupported capture,
    invalid readback dimensions, file-open failure, and short writes.
  - Screenshot-and-exit, scheduled screenshot, interval capture, and auto-cycle
    only report completion after the capture result succeeds.
- `SkullbonezSource/Runtime/CaptureController.h/.cpp`
  - Propagates `SbResult` from BMP writing and prints the capture success message
    only after success.
- `SkullbonezSource/Runtime/RunCapture.cpp` and `Run.h`
  - `Run::SaveScreenshot` now returns the capture result while keeping the
    startup-bound capture backend invariant fatal.
- `SkullbonezSource/Runtime/RunFrame.cpp`
  - Frame screenshot automation and auto-cycle captures report capture failures
    to `stderr`, print a runtime-exit reason, and post quit with exit code 1
    instead of marking scene capture complete.
- `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`
  - Interaction screenshot actions fail automation and write report `ok=false`
    when the capture result fails.
- `SkullbonezSource/Runtime/RunLiveStyle.cpp` and
  `SkullbonezSource/Runtime/LiveStyleController.h`
  - Live-style capture writes `capture_error`, reports to `stderr`, and clears
    the pending capture request on failure.
- `SkullbonezSource/Runtime/RunInput.cpp`
  - UI screenshot commands report capture failures to `stderr`.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
  - Added the Step 3.1 partial-progress note for the capture boundary.
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
  - Added the Phase 3 rows 198-201 progress note.
- `Agentic/SessionState.md`
  - Updated current Plan 04 validation and latest handoff pointer.

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 110.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 151.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 11
- Deferred: 0
- Files:
  - `SkullbonezSource/Runtime/CaptureController.cpp`
  - `SkullbonezSource/Runtime/CaptureController.h`
  - `SkullbonezSource/Runtime/CaptureSystem.cpp`
  - `SkullbonezSource/Runtime/CaptureSystem.h`
  - `SkullbonezSource/Runtime/LiveStyleController.h`
  - `SkullbonezSource/Runtime/Run.h`
  - `SkullbonezSource/Runtime/RunCapture.cpp`
  - `SkullbonezSource/Runtime/RunFrame.cpp`
  - `SkullbonezSource/Runtime/RunInput.cpp`
  - `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`
  - `SkullbonezSource/Runtime/RunLiveStyle.cpp`

The edited files have learning headers. Lane R capture behavior is documented in
the capture result carrier, `CaptureSystem::SaveBackbufferBmp`, frame automation
failure handling, and touched Run/LiveStyle headers.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:10.8437565.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_capture_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:01:04.6668478.
  - Log:
    `Agentic/Reports/validate_full_plan04_capture_result_20260709.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, formatting clean, DX12 validation
    errors 0, DX12 screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.

No SkullScope workflow was used in this slice.

## Rubber Duck

No rubber-duck pass was run. This was an ordinary incremental row conversion,
not a completed major plan/checkpoint.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Good next small candidate to inspect with CodeGraph first:
  `Input.cpp` Win32 cursor/client-coordinate environment rows 202, 204, 205,
  207, and 208.
- Keep `Timer.cpp`, larger DX12, `TextureCollection`, and `ConvexHullShape`
  recoverable clusters as dedicated slices because they likely have broader
  API and validation surface area.

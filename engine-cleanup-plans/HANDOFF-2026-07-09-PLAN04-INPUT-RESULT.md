# Plan 04 Handoff - Input Cursor Recoverable Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Committed slice target: convert Runtime/Input Win32 cursor and client-coordinate
failures from exception exits to recoverable input results.

This closes only inventory rows 202, 204, 205, 207, and 208. Plan 04 remains in
progress.

## Changed

- `SkullbonezSource/Runtime/Input.h/.cpp`
  - Added `Input::MouseCoordinatesResult` with an inline `SbResult`.
  - Changed `Input::GetMouseCoordinates` and
    `Input::GetClientMouseCoordinates` to return that result.
  - Changed `Input::SetMouseCoordinates` and
    `Input::CentreMouseCoordinates` to return `SbResult`.
  - `GetCursorPos`, `ScreenToClient`, `SetCursorPos`, `ClientToScreen`, and
    center-path `SetCursorPos` failures now return
    `SbResult::Failure("Runtime/Input", ...)` with `GetLastError()` detail.
- `SkullbonezSource/Runtime/RunInput.cpp`
  - Reports recoverable cursor failures to `stderr` at the runtime input frame
    boundary and keeps neutral pointer coordinates when the current frame lacks
    valid client coordinates.
- `SkullbonezSource/Runtime/InputController.h/.cpp`
  - Carries a recoverable cursor lookup result from camera mouse-look input to
    Run. Mouse-look resets instead of using stale client coordinates.
- `SkullbonezSource/UI/UIInput.cpp`
  - Uses automation mouse overrides without querying Win32; otherwise leaves
    neutral UI coordinates when cursor lookup fails.
- Editor/replay input callers
  - Optional gizmo, placement scale, mouse pickup, replay cause tree, scrubber,
    and velocity-edit actions skip the current frame when client coordinates are
    unavailable.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
- `Agentic/SessionState.md`

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 105.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 151.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 11
- Deferred: 0
- Files:
  - `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
  - `SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp`
  - `SkullbonezSource/Runtime/Input.cpp`
  - `SkullbonezSource/Runtime/Input.h`
  - `SkullbonezSource/Runtime/InputController.cpp`
  - `SkullbonezSource/Runtime/InputController.h`
  - `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp`
  - `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp`
  - `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp`
  - `SkullbonezSource/Runtime/RunInput.cpp`
  - `SkullbonezSource/UI/UIInput.cpp`

The edited files have learning headers. New comments define the Lane R input
result vocabulary and explain why Win32 cursor failures return results instead
of unwinding through WndProc or the run loop.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:12.6993802.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_input_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- `tools\validate_full.bat`
  - First attempt: failed formatting after 00:00:44.8937859 because
    `SkullbonezSource/Runtime/Input.h` needed header formatting.
  - Header formatting was fixed with targeted clang-format plus
    `tools\align_header_inline_comments.py`; `tools\validate_format.bat` then
    passed in 00:00:09.3041026.
  - Final result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:01:06.3735933.
  - Log:
    `Agentic/Reports/validate_full_plan04_input_result_20260709_final.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors, source
    formatting clean, Profile/Debug builds 0 warnings/errors, DX12 validation
    errors 0, DX12 screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.

No SkullScope workflow was used in this slice.

## Rubber Duck

No rubber-duck pass was run. This was an ordinary incremental row conversion,
not a completed major plan/checkpoint.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Keep larger DX12, `TextureCollection`, `ConvexHullShape`, and `Timer.cpp`
  recoverable clusters as dedicated slices because they likely have broader API
  and validation surface area.

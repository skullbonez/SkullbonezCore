# Plan 04 Handoff - Scene Object-Group Recoverable Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Committed slice target: convert authored scene object-group metadata failure
from an exception exit to a recoverable scene-load result.

This closes only inventory row 249. Plan 04 remains in progress. Continue with
remaining Phase 3 Lane R boundaries unless the remaining
RuntimeAllocationTracker Lane F row has a clean/approved perf gate path.

## Changed

- `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
  - Removed the local `<stdexcept>` dependency.
  - Changed `MakeSceneObjectGroupCreateDesc` to fill an output descriptor and
    return `SbResult`.
  - Invalid authored group metadata now returns
    `SbResult::Failure("Runtime/SceneAuthoredSetup", ...)` with kind/root/part
    diagnostics.
  - Both `convex_hull` and `convex_hull_state` setup paths return that result
    through `SceneAuthoredSetup::SetUpGameModels`; `Run::LoadScene` already
    logs and reports that scene-load failure.
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
  - Added the Step 3.1 partial-progress note for the scene object-group
    metadata boundary.
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
  - Added the Phase 3 row 249 progress note.
- `Agentic/SessionState.md`
  - Updated current Plan 04 validation and latest handoff pointer.

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 115.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 151.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 1
- Deferred: 0
- File: `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`

The file already had the required learning header. The new failure path has a
nearby Lane R comment explaining the scene-file metadata condition.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:05.4846485.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_scene_group_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- First `tools\validate_full.bat`
  - Result: failed at DX12 renderer validation formatting check.
  - Exit code: 1.
  - Elapsed: 00:00:32.5653233.
  - Log:
    `Agentic/Reports/validate_full_plan04_scene_group_result_20260709.log`
  - Key evidence: Profile and Debug builds passed first; formatter reported
    `SceneAuthoredSetup.cpp` needed formatting.
- Formatting fix/check
  - Used `tools\find_clang_format.bat` to locate clang-format and formatted
    only `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`.
  - `tools\validate_format.bat` passed.
  - Exit code: 0.
  - Elapsed: 00:00:09.3112561.
  - Log:
    `Agentic/Reports/validate_format_plan04_scene_group_after_fix_20260709.log`
- Final `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:00:56.0458616.
  - Log:
    `Agentic/Reports/validate_full_plan04_scene_group_result_20260709_rerun.log`
  - Key evidence: project filters 0 errors, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, DX12
    screenshots matched committed baselines, and
    `physics_regression_solver.csv` was byte-exact.

No SkullScope workflow was used in this slice.

## Rubber Duck

No rubber-duck pass was run. This was an ordinary incremental row conversion,
not a completed major plan/checkpoint.

## Next Work

- Continue Plan 04 Phase 3 Lane R conversions one boundary at a time.
- Good next small candidates to inspect with CodeGraph first:
  `Core/Timer.cpp` high-resolution counter startup capability,
  `Window.cpp` window creation, or `Input.cpp` Win32 cursor environment rows.
- Keep the larger DX12, `TextureCollection`, and `ConvexHullShape` recoverable
  clusters as dedicated slices because they have broader API and validation
  surface area.

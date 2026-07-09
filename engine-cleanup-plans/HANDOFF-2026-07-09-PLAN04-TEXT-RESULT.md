# Plan 04 Handoff - Text Atlas Recoverable Startup Result

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 / Step 3.1 Lane R boundary

## Status

Completed slice target: convert the text/font SDF atlas startup generation and
load-after-generate failures in `Text.cpp` from exceptions to recoverable
runtime startup results.

This closes only inventory rows 96 and 97. Plan 04 remains in progress.

## Changed

- `SkullbonezSource/Rendering/Text.h/.cpp`
  - `Text2d::BuildFont()` now returns `SbResult`.
  - Missing or stale SDF atlas generation failure now returns
    `SbResult::Failure("Rendering/Text", ...)`.
  - Load-after-generate failure now returns
    `SbResult::Failure("Rendering/Text", ...)`.
- `SkullbonezSource/Runtime/RunUiTextPass.cpp`
  - `UiTextPass::EnsureGpuResources()` now returns the text resource setup
    result.
- `SkullbonezSource/Runtime/RunRender.cpp`
  - `RuntimeRenderer::EnsureUiTextResources()` now returns the UI text pass
    resource setup result.
- `SkullbonezSource/Runtime/Run.cpp`
  - `Run::Initialise()` reports atlas setup failure through
    `m_lastSceneLoadResult`, so the existing runtime process boundary emits a
    recoverable diagnostic and exits before scene loading.
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `engine-cleanup-plans/04-error-handling-policy-reconciliation.md`
- `engine-cleanup-plans/04-throw-site-lane-inventory.md`
- `Agentic/SessionState.md`

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` => 102.
- Source `SB_FATAL` invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` => 153.

## Comment Audit

Touched-file audit only, no subsystem checklist required.

- Checked: 7
- Deferred: 0
- Files:
  - `SkullbonezSource/Rendering/Text.cpp`
  - `SkullbonezSource/Rendering/Text.h`
  - `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
  - `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
  - `SkullbonezSource/Runtime/Run.cpp`
  - `SkullbonezSource/Runtime/RunRender.cpp`
  - `SkullbonezSource/Runtime/RunUiTextPass.cpp`

The edited files have learning headers. New comments define the Lane R
resource setup boundary in the text/render owner headers, and `Run::Initialise`
has a local `Why:` comment for reporting SDF atlas setup failures before scene
loading.

## Validation

The current tool session cannot open a visible console window, so validation ran
through PowerShell and mirrored output to logs.

- `tools\validate_build.bat Profile`
  - Result: passed.
  - Exit code: 0.
  - Elapsed: 00:00:10.8513394.
  - Log:
    `Agentic/Reports/validate_build_profile_plan04_text_result_20260709.log`
  - Key evidence: Profile build completed with 0 warnings and 0 errors.
- `tools\validate_full.bat`
  - Result: passed, `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Exit code: 0.
  - Elapsed: 00:01:01.1972670.
  - Log:
    `Agentic/Reports/validate_full_plan04_text_result_20260709.log`
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
- Keep larger DX12, `TextureCollection`, and `ConvexHullShape` recoverable
  clusters as dedicated slices because they have broader API and validation
  surface area.
- Resume the remaining RuntimeAllocationTracker Lane F row only with an
  allocator-safe fatal strategy and a clean or explicitly approved perf gate.

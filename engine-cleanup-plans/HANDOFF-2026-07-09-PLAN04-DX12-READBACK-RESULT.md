# Plan 04 DX12 Readback Result Handoff - 2026-07-09

## Status

- Branch: `nightrunner-8th-july`.
- Plan 04 remains active and incomplete.
- Completed this slice: Phase 3 Lane R DX12 screenshot readback rows 94 and 95.
- User requested faster progress; next agents should batch compatible
  same-lane/same-subsystem rows that share one validation gate.

## Changed

- `IRenderCaptureBackend::CaptureBackbuffer` now returns `SbResult` with output
  pixels/dimensions passed by reference.
- `RenderBackendDX12::CaptureBackbuffer` removes the unused `ThrowIfFailed`
  helper, restores the backbuffer state if readback buffer creation fails, and
  returns `SbResult::Failure("Rendering/DX12", ...)`.
- `CaptureSystem::SaveBackbufferBmp` propagates backend readback failures and
  checks that the returned pixel buffer is large enough before writing the BMP.

## Counts

- Strict anchored source throw statements:
  `rg -n "^\s*throw\b" SkullbonezSource` -> 100.
- `SB_FATAL` macro invocations:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` -> 153.

## Comment Audit

- Scope:
  - `SkullbonezSource/Rendering/IRenderCaptureBackend.h`
  - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
  - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`
  - `SkullbonezSource/Runtime/CaptureSystem.cpp`
- Checked: 4.
- Deferred: 0.
- No subsystem checklist plan was required; this was a touched-file audit.

## Validation

- `tools\validate_build.bat Profile` passed in 00:00:11.0128484 with 0
  warnings and 0 errors.
  Log:
  `Agentic/Reports/validate_build_profile_plan04_dx12_readback_result_20260709.log`.
- `tools\validate_dx12_renderer.bat` passed with
  `VALIDATE_DX12_RENDERER: ALL PASSED`, DX12 validation errors 0, and
  screenshots matching committed baselines.
  Log:
  `Agentic/Reports/validate_dx12_renderer_plan04_readback_result_20260709_final.log`.
- `tools\validate_full.bat` passed in 00:00:47.7232961 with
  `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters 0 errors, runtime
  boundaries 0 errors, Profile/Debug builds 0 warnings/errors, source
  formatting clean, DX12 validation errors 0, screenshots matching baselines,
  and `physics_regression_solver.csv` byte-exact.
  Log:
  `Agentic/Reports/validate_full_plan04_dx12_readback_result_20260709.log`.

## Deferred Notes

- RuntimeAllocationTracker row 9 is still deferred. An allocator-safe fatal
  edit was tried and reverted because `tools\validate_perf.bat` failed twice on
  timing thresholds while the allocation guard itself was clean. Resume only
  with a clean/approved perf gate path.
- No SkullScope was used.
- No rubber-duck review was run; this was a bounded source slice, not a whole
  cleanup-plan checkpoint.

## Next Work

- Continue Phase 3 Lane R. Batch compatible rows where they share the same
  subsystem and validation gate, for example adjacent DX12 resource/device
  result conversions behind the renderer startup/reporting boundary.

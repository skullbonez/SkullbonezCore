# Plan 04 RenderDeviceDX12 Descriptor Fatal Handoff - 2026-07-09

## Summary

- Converted six `Dx12DescriptorAllocator` fixed-capacity/index/caller-invariant
  throws to `SB_FATAL("RenderDeviceDX12", ...)`.
- Preserved allocator API shape; no caller signatures changed.
- This was a late Phase 1 cleanup. The original inventory listed these rows as
  R unless the message identified an owner invariant; fixed descriptor capacity
  and invalid descriptor-table indices are render-owner invariants.

## Converted Scope

- Static SRV heap exhaustion.
- Transient SRV heap exhaustion.
- Zero-count transient descriptor range requests.
- Transient SRV range exhaustion.
- Shader-visible descriptor index validation.
- Staging descriptor index validation.

## Counts

- Strict anchored source throw statement count: 75.
- `SB_FATAL` macro invocations: 159.

## Comment Audit

- Touched source-bearing files checked:
  `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`.
- Checked count: 1.
- Deferred count: 0.
- No subsystem checklist was required because this was a touched-file audit.

## Validation

- `tools\validate_build.bat Profile`: passed in 00:00:05.3974010, 0 warnings,
  0 errors.
  Log: `Agentic/Reports/validate_build_profile_plan04_descriptor_fatals_20260709.log`
- Initial `tools\validate_dx12_renderer.bat` failed formatting on
  `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`; a targeted VS-bundled
  `clang-format` pass was applied to that file only.
- Final `tools\validate_dx12_renderer.bat`: passed in 00:00:27.1200192 with
  `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, DX12 validation errors
  0, and screenshots matching committed baselines.
  Log: `Agentic/Reports/validate_dx12_renderer_plan04_descriptor_fatals_20260709_rerun.log`
- `tools\validate_full.bat` was not required because the final diff touched only
  DX12 renderer code and Plan 04 documentation.

## Next

- Continue batching compatible same-lane/same-validator rows.
- Keep fence signal/wait failures separate; those need a result-returning fence
  boundary or a deliberate fatal classification.
- Keep `CreateDepthStencil` with resize/present device-loss handling rather than
  treating it as startup-only work.

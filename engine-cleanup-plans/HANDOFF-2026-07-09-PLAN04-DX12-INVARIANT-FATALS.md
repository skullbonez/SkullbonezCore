# Plan 04 DX12 Invariant Fatal Handoff - 2026-07-09

## Summary

- Converted three remaining DX12 owner-invariant throws to `SB_FATAL`.
- Kept D3D12 resource creation, fence, and device-loss failures deferred for
  recoverable-result work.
- No public API shape changed.

## Converted Scope

- `Dx12DescriptorAllocator::Init`: invalid descriptor allocator geometry.
- `Dx12ReadbackBuffer::MapRead`: requested map range larger than the readback
  buffer.
- `ToDx12GraphColorFormat`: unsupported render-graph color transient enum.

## Counts

- Strict anchored source throw statement count: 72.
- `SB_FATAL` macro invocations: 162.

## Comment Audit

- Touched source-bearing files checked:
  `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` and
  `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`.
- Checked count: 2.
- Deferred count: 0.
- No subsystem checklist was required because this was a touched-file audit.

## Validation

- `tools\validate_build.bat Profile`: passed in 00:00:06.8697852, 0 warnings,
  0 errors.
  Log: `Agentic/Reports/validate_build_profile_plan04_dx12_invariant_leftovers_20260709.log`
- `tools\validate_dx12_renderer.bat`: passed in 00:00:26.8442917 with
  `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, DX12 validation errors
  0, and screenshots matching committed baselines.
  Log: `Agentic/Reports/validate_dx12_renderer_plan04_dx12_invariant_leftovers_20260709.log`
- `tools\validate_full.bat` was not required because the final diff touched only
  DX12 renderer code and Plan 04 documentation.

## Next

- Continue batching compatible same-lane/same-validator rows.
- Shader creation and texture loading need deliberate recoverable result
  boundaries rather than null-pointer fallbacks.
- Remaining DX12 resource creation, fence, present, and resize failures should
  stay in Lane R unless a specific row is proven to be an owner invariant.

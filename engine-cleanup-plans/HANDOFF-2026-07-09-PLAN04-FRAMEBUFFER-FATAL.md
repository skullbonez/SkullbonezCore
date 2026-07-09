# Handoff: Plan 04 FramebufferDX12 Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: FramebufferDX12 backend-lifetime slice validated and recorded for
restart. The broader engine-cleanup goal remains active and incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing `FramebufferDX12.cpp`.
- Converted `FramebufferDX12.cpp` Lane F row 43 from the Step 0.1 inventory to
  `SB_FATAL("FramebufferDX12", ...)`.
- Added `../../Core/FatalError.h` to `FramebufferDX12.cpp`.
- Kept the replacement scoped to the missing initialized DX12 backend lifetime
  invariant. The color/depth texture creation throws remain Lane R for the
  recoverable-result phase.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`; checked 1, deferred 0.
  The learning header and local create guard now record the initialized-backend
  lifetime invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 143.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 129.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to:

`Agentic/Reports/validate_dx12_renderer_plan04_framebuffer_fatal_20260709.log`

Command:

```powershell
tools\validate_dx12_renderer.bat
```

Result:

- Exit code: 0
- Elapsed: 00:00:26.4805520
- Formatting passed.
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `VALIDATE_DX12_RENDERER: ALL PASSED`

## Resume Point

Resume Plan 04 Step 1.1 with the remaining Lane F sites. Notes:

- `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9 still needs an
  allocator-safe fatal strategy. Do not blindly call `SB_FATAL` from allocation
  hooks because `FatalError.cpp` logs through `EngineLog::Get()`, while the
  allocation tracker comments forbid allocating or using engine services in the
  hook path.
- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining small slices include `RunScene` row 250,
  `TextureCollection.cpp` Lane F rows 212-214, 216, 218, and 222, or the smaller
  DX12 helper rows such as `DynamicGeometry`, `DXR`, `Pipeline`, `Profiler`, and
  `Mesh`.

Do not mark the broader goal complete.

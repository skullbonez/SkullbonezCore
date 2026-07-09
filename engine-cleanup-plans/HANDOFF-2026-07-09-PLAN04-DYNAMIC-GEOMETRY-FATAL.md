# Handoff: Plan 04 RenderBackendDX12 Dynamic Geometry Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: RenderBackendDX12 dynamic-geometry PSO-cache slice validated and
recorded for restart. The broader engine-cleanup goal remains active and
incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing
  `RenderBackendDX12.DynamicGeometry.cpp`.
- Converted `RenderBackendDX12.DynamicGeometry.cpp` Lane F row 51 from the Step
  0.1 inventory to `SB_FATAL("RenderBackendDX12", ...)`.
- Added `../../Core/FatalError.h` to
  `RenderBackendDX12.DynamicGeometry.cpp`.
- Kept the replacement scoped to fixed grid-line PSO cache exhaustion. The two
  DX12 device/shader creation throws in the same file remain Lane R for the
  recoverable-result phase.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`;
  checked 1, deferred 0. The learning header and local PSO-cache guard now
  record the fixed render-target-format cache invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 141.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 131.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to:

`Agentic/Reports/validate_dx12_renderer_plan04_dynamic_geometry_fatal_20260709.log`

Command:

```powershell
tools\validate_dx12_renderer.bat
```

Result:

- Exit code: 0
- Elapsed: 00:00:27.3493942
- Formatting passed.
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `VALIDATE_DX12_RENDERER: ALL PASSED`

## Resume Point

The user asked to pause after committing this slice so they can restart the
computer. Resume Plan 04 Step 1.1 with the remaining Lane F sites after restart.
Notes:

- `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9 still needs an
  allocator-safe fatal strategy. Do not blindly call `SB_FATAL` from allocation
  hooks because `FatalError.cpp` logs through `EngineLog::Get()`, while the
  allocation tracker comments forbid allocating or using engine services in the
  hook path.
- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining small slices include `RunScene` row 250,
  `TextureCollection.cpp` Lane F rows 212-214, 216, 218, and 222,
  `RenderBackendDX12.Pipeline.cpp` rows 89-91, and
  `RenderBackendDX12.Profiler.cpp` row 101.

Do not mark the broader goal complete.

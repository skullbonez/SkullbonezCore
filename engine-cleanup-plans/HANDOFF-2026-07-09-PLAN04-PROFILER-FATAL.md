# Handoff: Plan 04 RenderBackendDX12 Profiler Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: RenderBackendDX12 platform-profiler GPU stack slice validated and
recorded. The broader engine-cleanup goal remains active and incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing
  `RenderBackendDX12.Profiler.cpp`.
- Converted `RenderBackendDX12.Profiler.cpp` Lane F row 101 from the Step 0.1
  inventory to `SB_FATAL("RenderBackendDX12", ...)`.
- Added `../../Core/FatalError.h` to `RenderBackendDX12.Profiler.cpp`.
- Removed an unused local descriptor-heap fatal reporter from the profiler split
  file. The DX12 HRESULT helper throw remains Lane R for the recoverable-result
  phase.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`; checked 1,
  deferred 0. The learning header now defines GPU timer, PIX, and the
  platform-profiler GPU stack, and the local guard records the fixed stack-depth
  invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 137.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 135.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to logs under `Agentic/Reports/`.

Renderer gate:

```powershell
tools\validate_dx12_renderer.bat
```

Result:

- Exit code: 0
- Elapsed: 00:00:27.3000145
- Formatting passed.
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `VALIDATE_DX12_RENDERER: ALL PASSED`
- Log: `Agentic/Reports/validate_dx12_renderer_plan04_profiler_fatal_20260709.log`

Platform-profiler marker run:

```powershell
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 5 --vsync off
```

Result:

- Exit code: 0
- Elapsed: 00:00:09.1357466
- Output included `[platform-profiler] Platform profiler marker emission enabled.`
- Log: `Agentic/Reports/platform_profiler_markers_plan04_profiler_fatal_20260709.log`

Note: an initial raw `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`
launch did not self-terminate within the shell timeout because it had no frame
bound. It was stopped by PID 83956 only, then rerun with `--frames 5`.

## Resume Point

Resume Plan 04 Step 1.1 with the remaining Lane F sites. Notes:

- `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9 still needs an
  allocator-safe fatal strategy. Do not blindly call `SB_FATAL` from allocation
  hooks because `FatalError.cpp` logs through `EngineLog::Get()`, while the
  allocation tracker comments forbid allocating or using engine services in the
  hook path.
- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining small slices include `RunScene` row 250 and
  `TextureCollection.cpp` Lane F rows 212-214, 216, 218, and 222.

Do not mark the broader goal complete.

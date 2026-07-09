# Handoff: Plan 04 Input Window-Bridge Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: Input window-bridge slice validated and recorded for restart. The
broader engine-cleanup goal remains active and incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing `Input.cpp`.
- Converted both `Input.cpp` Lane F rows from the Step 0.1 inventory to fatal
  diagnostics through a local helper that calls `SB_FATAL("Input", ...)`:
  rows 203 and 206.
- Added `../Core/FatalError.h` to `Input.cpp`.
- Kept the replacements scoped to the missing input window bridge lifetime
  invariant in `Input::GetClientMouseCoordinates` and
  `Input::CentreMouseCoordinates`.
- Left the neighboring Win32 cursor/client-coordinate failures as Lane R throws
  for the recoverable-result phase.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Runtime/Input.cpp`; checked 1, deferred 0. The learning
  header now names the input window bridge and its startup binding invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 150.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 122.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to:

`Agentic/Reports/validate_full_plan04_input_bridge_fatals_20260709.log`

Command:

```powershell
tools\validate_full.bat
```

Result:

- Exit code: 0
- Elapsed: 00:00:55.3535461
- `VALIDATE_PROJECT_FILTERS: ALL PASSED`
- `VALIDATE_RUNTIME_BOUNDARIES: ALL PASSED`
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `VALIDATE_PHYSICS: ALL PASSED`
- `VALIDATE_FULL: DEFAULT GATE PASSED`

## Resume Point

Resume Plan 04 Step 1.1 with the remaining Lane F sites. Notes:

- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining small slices include `GameModelRenderer.cpp` row 4,
  `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9, `SceneRuntime` row
  254, `RunScene` row 250, `AssetSystem` rows 251-253, or the smaller DX12
  helper rows such as `DynamicGeometry`, `DXR`, `Pipeline`, `Profiler`, `Mesh`,
  `Framebuffer`, and `TLAS`.

Do not mark the broader goal complete.

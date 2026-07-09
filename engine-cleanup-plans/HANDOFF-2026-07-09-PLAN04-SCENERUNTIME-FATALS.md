# Handoff: Plan 04 SceneRuntime Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: SceneRuntime scene-object-id slice validated and recorded for restart.
The user requested a pause for a computer restart. The broader engine-cleanup
goal remains active and incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing `SceneRuntime.cpp`.
- Converted `SceneRuntime.cpp` Lane F row 254 from the Step 0.1 inventory to
  `SB_FATAL("SceneRuntime", ...)`.
- Added `../../Core/FatalError.h` to `SceneRuntime.cpp` and removed the now
  unused `<stdexcept>` include.
- Kept the replacement scoped to scene object id range exhaustion. Scene object
  id 0 remains reserved as "not assigned"; live allocations must not wrap or
  cross the uint32 id ceiling.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`; checked 1, deferred 0. The
  learning header and local allocation comment now record the scene object id
  cursor invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 148.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 124.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to:

`Agentic/Reports/validate_full_plan04_sceneruntime_fatals_20260709.log`

Command:

```powershell
tools\validate_full.bat
```

Result:

- Exit code: 0
- Elapsed: 00:00:54.4774207
- `VALIDATE_PROJECT_FILTERS: ALL PASSED`
- `VALIDATE_RUNTIME_BOUNDARIES: ALL PASSED`
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `VALIDATE_PHYSICS: ALL PASSED`
- `VALIDATE_FULL: DEFAULT GATE PASSED`

## Resume Point

Pause here for the requested computer restart. Resume Plan 04 Step 1.1 with the
remaining Lane F sites. Notes:

- `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9 still needs an
  allocator-safe fatal strategy. Do not blindly call `SB_FATAL` from allocation
  hooks because `FatalError.cpp` logs through `EngineLog::Get()`, while the
  allocation tracker comments forbid allocating or using engine services in the
  hook path.
- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining small slices include `RunScene` row 250, `AssetSystem` rows
  251-253, or the smaller DX12 helper rows such as `DynamicGeometry`, `DXR`,
  `Pipeline`, `Profiler`, `Mesh`, `Framebuffer`, and `TLAS`.

Do not mark the broader goal complete.

# Handoff: Plan 04 RunScene Lane F Slice

Date: 2026-07-09  
Branch: `nightrunner-8th-july`  
Status: RunScene DXR render-facet slice validated and recorded. The broader
engine-cleanup goal remains active and incomplete.

## Completed This Slice

- Followed the repo startup/orchestrator path for Plan 04 implementation work:
  read the required startup docs, checked the dirty worktree, confirmed
  CodeGraph was current, and used CodeGraph before editing `RunScene.cpp`.
- Converted `RunScene.cpp` Lane F row 250 from the Step 0.1 inventory to
  `SB_FATAL("RunScene", ...)`.
- Added `../../Core/FatalError.h` to `RunScene.cpp`.
- Kept the replacement scoped to missing render resource, command, or
  diagnostics facets during DXR reflection helper-mesh warm-up.
- Comment-style audit for touched source-bearing files:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`; checked 1, deferred 0. The
  learning header and local guard now record render backend facets and the DXR
  reflection facet-binding invariant.

## Current Counts

- Strict anchored throw statement scan:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 136.
- Fatal call-site scan:
  `rg -n "SB_FATAL\s*\(" SkullbonezSource` reports 136.

The inventory table in `04-throw-site-lane-inventory.md` remains the Step 0.1
baseline snapshot. Phase notes track converted rows.

## Validation

Headless shell was used because this Codex session cannot open a visible console
window. Output was mirrored to:

`Agentic/Reports/validate_full_plan04_runscene_fatal_20260709.log`

Command:

```powershell
tools\validate_full.bat
```

Result:

- Exit code: 0
- Elapsed: 00:01:00.4684976
- Project filters: 0 errors.
- Runtime boundaries: 0 errors.
- Profile and Debug builds succeeded with 0 warnings and 0 errors.
- DX12 validation errors: 0.
- DX12 screenshots matched committed baselines.
- `physics_regression_solver.csv` matched the committed baseline byte-exact.
- `VALIDATE_FULL: DEFAULT GATE PASSED`

## Resume Point

Resume Plan 04 Step 1.1 with the remaining Lane F sites. Notes:

- `Runtime/Allocation/RuntimeAllocationTracker.cpp` row 9 still needs an
  allocator-safe fatal strategy. Do not blindly call `SB_FATAL` from allocation
  hooks because `FatalError.cpp` logs through `EngineLog::Get()`, while the
  allocation tracker comments forbid allocating or using engine services in the
  hook path.
- Math rows are coupled to unit tests that still document/assert the legacy
  throwing contract, so handle them as a coordinated math/test-contract slice.
- Good remaining bounded source slice: `TextureCollection.cpp` Lane F rows
  212-214, 216, 218, and 222.

Do not mark the broader goal complete.

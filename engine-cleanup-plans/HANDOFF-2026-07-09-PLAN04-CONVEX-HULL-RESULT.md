# Plan 04 Convex Hull Result Handoff

Date: 2026-07-09
Branch: `nightrunner-8th-july`
Slice: Plan 04 Phase 3 Lane R, ConvexHullShape hull-load boundary

## Completed

- Converted the 41 `ConvexHullShape.cpp` authored hull parse/load throw rows
  from the Step 0.1 inventory (rows 143-183) to `SbResult`-based recoverable
  results.
- Added `ConvexHullShape::TryLoadFromFile(const char*, ConvexHullShape&)`.
  Scene/runtime/editor callers now use it at recoverable boundaries.
- Kept `ConvexHullShape::LoadFromFile` as a legacy valid-path wrapper for
  existing tests; unexpected wrapper failures are now
  `SB_FATAL("Physics/ConvexHullShape", ...)`.
- Replaced scaled-hull face degeneration with a fatal owner invariant. Positive
  finite copy-scale should preserve validated baked hull topology.
- Scene-authored convex hulls now return hull-load failures through
  `SceneAuthoredSetup::SetUpGameModels` and the existing scene-load reporter.
- Test-scene default mass lookup feeds hull failures into the existing parser
  `Fail` boundary. Editor placement/cache helpers log and skip failed hulls.

## Counts

- Strict anchored source throws:
  `rg -n "^\s*throw\b" SkullbonezSource` reports 22 sites, down from 63.
- `SB_FATAL` macro invocations:
  `rg -n "SB_FATAL\(" SkullbonezSource` reports 165, up from 163.

Remaining strict throw rows:

- `SkullbonezSource\Scene\TestSceneParser.cpp`: 1
- `SkullbonezSource\Runtime\Allocation\RuntimeAllocationTracker.cpp`: 1
- DX12 renderer files: 20 total across `FramebufferDX12.cpp`, `MeshDX12.cpp`,
  `RenderBackendDX12*.cpp`, `RenderDeviceDX12.cpp`, and `ShaderDX12.cpp`.

## Validation

Visible console windows were unavailable in this tool session, so output was
mirrored to logs.

- `tools\validate_build.bat Profile`
  - Exit 0 in `00:00:18.2303251`
  - 0 warnings, 0 errors
  - Log: `Agentic/Reports/validate_build_profile_plan04_convex_hull_result_20260709.log`
- `python tools\check_runtime_boundaries.py --repo . --json-out Agentic/Reports/check_runtime_boundaries_plan04_convex_hull_result_20260709.json`
  - Exit 0 in `00:00:19.5329653`
  - 0 errors
  - The generated JSON summary was removed after the pass; log retained at
    `Agentic/Reports/check_runtime_boundaries_plan04_convex_hull_result_20260709.log`
- `tools\validate_physics.bat`
  - Exit 0 in `00:00:29.0767839`
  - `VALIDATE_PHYSICS: ALL PASSED`
  - Debug/Profile builds reported 0 warnings and 0 errors; deterministic
    physics output matched the committed baseline.
  - Log: `Agentic/Reports/validate_physics_plan04_convex_hull_result_20260709.log`

## Comment Audit

Touched-file audit inspected 6 source-bearing files, with 0 deferred:

- `SkullbonezSource/Physics/ConvexHullShape.cpp`
- `SkullbonezSource/Physics/ConvexHullShape.h`
- `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp`
- `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp`
- `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- `SkullbonezSource/Scene/TestSceneParser.cpp`

No subsystem checklist plan was required because this was a touched-file audit,
not a subsystem comment pass.

## Next Batch

Batch the remaining DX12 Lane R rows by owner/validation gate. The highest
throughput next slice is likely the DX12 shader/resource creation group under
`tools\validate_dx12_renderer.bat`, while leaving the allocator `bad_alloc` row
separate until an allocator-safe fatal strategy and perf gate are agreed.

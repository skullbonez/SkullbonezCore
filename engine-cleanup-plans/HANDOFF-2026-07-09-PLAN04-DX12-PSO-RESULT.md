# Plan 04 DX12 PSO/Dynamic Geometry Result Handoff - 2026-07-09

Branch: `nightrunner-8th-july`

## Summary

- Converted three strict DX12 throw rows to Lane R logged skip/neutral-handle
  reporting: `RenderBackendDX12.DynamicGeometry.cpp` rows 49 and 50 plus
  `RenderBackendDX12.Pipeline.cpp` row 88 from the Step 0.1 inventory.
- `RenderBackendDX12::CreatePSO` now logs graphics PSO creation failure and
  returns `nullptr`. `PrepareDraw` returns `false` when no valid PSO can be
  bound, and mesh/dynamic/instanced draw callers skip the affected draw.
- `EnsureGridLinePipeline` logs debug-line PSO creation failures and skips that
  diagnostic overlay draw.
- `CreateInstancedMesh` logs failed static vertex buffer creation and returns
  handle `0`, matching the existing upload/draw no-op contract for invalid
  instanced mesh handles.
- Fixed PSO cache exhaustion remains `SB_FATAL`; this slice only converted
  device/shader/resource creation failure paths.
- Strict anchored source throws are now 14. `SB_FATAL` macro invocations are 165.

## Validation

- `tools\validate_dx12_renderer.bat` passed in 00:00:34.3 with
  `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds
  0 warnings/errors, DX12 validation errors 0, and screenshots matching
  committed baselines.
  Log: `Agentic/Reports/validate_dx12_renderer_plan04_pso_result_20260709.log`
  Manifest: `TestOutput\validation\dx12_renderer\20260709T090239Z\manifest.json`
- `python tools\check_runtime_boundaries.py` passed in 00:00:19.5 with 0 errors.
  Log: `Agentic/Reports/check_runtime_boundaries_plan04_pso_result_20260709.log`

## Comment Audit

Touched-file audit checked 4 source-bearing files with 0 deferred:

- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`

No subsystem checklist was required because this was a touched-file pass, not a
subsystem-wide comment pass.

## Remaining Plan 04 Shape

Remaining strict source throws:

- `Scene/TestSceneParser.cpp`: authored scene parser `Fail` boundary.
- `Runtime/Allocation/RuntimeAllocationTracker.cpp`: `std::bad_alloc`; keep
  deferred until an allocator-safe fatal strategy and clean/approved perf gate
  are available.
- DX12 resource/fence/shader rows:
  `FramebufferDX12.cpp`, `MeshDX12.cpp`, `RenderBackendDX12.cpp` graph transient
  materialization, `RenderBackendDX12.Resources.cpp`, `RenderDeviceDX12.cpp`,
  and `ShaderDX12.cpp`.

Recommended next batches:

- Resource-factory batch: framebuffer/mesh/shader creation needs a coherent
  `IRenderResourceFactory` failure contract or audited null-resource behavior.
- Fence-timeline batch: `RenderDeviceDX12` signal/wait failures need a result
  path that does not force broad draw-command migration accidentally.
- Graph-transient batch: `MaterializeGraphTransientResources` needs either a
  command-context result contract or a stats failure path consumed by runtime
  render passes.
- Scene parser boundary: `TestSceneParser` has one authored-input row and may be
  a smaller non-DX12 Lane R slice if the parser `Fail` contract is already
  localized.

The unrelated untracked user-owned file remains:
`Agentic/Plans/NEXT_FABLE_PLANS/space-nbody-gravity-demo-plan.md`.

# Plan 04 DX12 Lifecycle/Texture Result Handoff - 2026-07-09

Branch: `nightrunner-8th-july`

## Summary

- Converted five strict DX12 throw rows to Lane R reporting:
  `RenderBackendDX12.cpp` rows 52, 71, 72, and 73 plus
  `RenderBackendDX12.Textures.cpp` row 102 from the Step 0.1 inventory.
- `IRenderDeviceLifecycle::Present` and `Resize` now return `SbResult`.
  `Run::Execute` returns `SbResult`, and `RunApp` routes runtime present
  failures through `reportRunResult`.
- `Window::HandleScreenResize` now returns `SbResult`; startup checks the
  initial resize result, and WndProc logs resize failures before posting quit.
- `RenderBackendDX12::CreateDepthStencil` returns `SbResult` through startup
  and resize. `RenderBackendDX12::CreateTexture2D` logs device/resource creation
  failure and returns texture handle `0`, matching the existing nonresident
  texture contract.
- Strict anchored source throws are now 17. `SB_FATAL` macro invocations are 165.

## Validation

- `tools\validate_build.bat Profile` passed in 00:00:19.3 with 0 warnings/errors.
  Log: `Agentic/Reports/validate_build_profile_plan04_dx12_lifecycle_texture_20260709.log`
- First `tools\validate_dx12_renderer.bat` attempt stopped at formatting on
  `SkullbonezSource\Physics\ConvexHullShape.cpp`; a targeted one-file
  clang-format run changed line wrapping only.
- Final `tools\validate_dx12_renderer.bat` passed in 00:00:47.1 with
  `VALIDATE_DX12_RENDERER: ALL PASSED`, DX12 validation errors 0, and screenshots
  matching committed baselines.
  Log: `Agentic/Reports/validate_dx12_renderer_plan04_lifecycle_texture_20260709.log`
  Manifest: `TestOutput\validation\dx12_renderer\20260709T085430Z\manifest.json`
- `python tools\check_runtime_boundaries.py` passed in 00:00:19.6 with 0 errors.
  Log: `Agentic/Reports/check_runtime_boundaries_plan04_lifecycle_texture_20260709.log`

## Comment Audit

Touched-file audit checked 10 source-bearing files with 0 deferred:

- `SkullbonezSource/Physics/ConvexHullShape.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- `SkullbonezSource/Rendering/IRenderDeviceLifecycle.h`
- `SkullbonezSource/Runtime/Init.cpp`
- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/Window.cpp`
- `SkullbonezSource/Runtime/Window.h`

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
  materialization, `RenderBackendDX12.DynamicGeometry.cpp`,
  `RenderBackendDX12.Pipeline.cpp`, `RenderBackendDX12.Resources.cpp`,
  `RenderDeviceDX12.cpp`, and `ShaderDX12.cpp`.

Recommended next batches:

- Resource-factory batch: framebuffer/mesh/shader creation needs a coherent
  `IRenderResourceFactory` failure contract or audited null-handle behavior.
- Fence-timeline batch: `RenderDeviceDX12` signal/wait failures need a result
  path that does not force broad draw-command migration accidentally.
- Graph-transient batch: `MaterializeGraphTransientResources` needs either a
  command-context result contract or a stats failure path consumed by runtime
  render passes.

The unrelated untracked user-owned file remains:
`Agentic/Plans/NEXT_FABLE_PLANS/space-nbody-gravity-demo-plan.md`.

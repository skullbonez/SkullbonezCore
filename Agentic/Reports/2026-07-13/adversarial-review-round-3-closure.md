# Adversarial Review Round 3 Closure

Date: 2026-07-13

Branch: `nightrunner-12th-july`

Plan result: 10 / 10 tasks complete

## Outcome

Round 3 closed the accepted architecture findings in dependency order. The ten
commits remove header namespace leakage, give scene capacity an owner, isolate
the Win32 prelude, compile engine exceptions out, retire `Basics`, dissolve the
rendering helper, upgrade all projects to C++20, adopt spans at dense-store
seams, reject a measured-slower solver SIMD candidate, and finish with a
three-frames-in-flight SM6.6 bindless DX12 raster path.

R10 raises the frame count from two to three and keeps every per-frame array,
allocator, fence value, and swap-chain resource sized from the shared constant.
Its 32 MiB upload arena therefore reserves 96 MiB across three frames. The
reviewed perf baselines move by 32.96/36.71/36.72 MiB for DX12 and
33.19/36.25/35.99 MiB for the physics bench at start/restart/end. This is the
intentional third-arena budget, not growth during steady runtime.

The raster root signature now carries six b1 descriptor indices and enables
`CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`; affected shaders read
`ResourceDescriptorHeap[index]`. Static staging descriptors are mirrored at the
same stable shader-visible index, while the fenced transient range remains for
compute mip generation, DXR tables, and other genuinely dynamic views. The
per-draw transient SRV allocation/copy/table-binding loop is gone.

The minimum device contract is Shader Model 6.6 plus Resource Binding Tier 3.
Unsupported devices return a recoverable startup diagnostic. No descriptor-table
fallback remains because retaining it would preserve the exact per-draw copies
this task removes. Descriptor indices stay inside the backend-owned b1 payload;
they were deliberately not written into engine material-instance rows, because
that would leak DX12 heap identity across the backend boundary and collide with
the existing packed material flags.

## Validation Evidence

- `tools\validate_full.bat`: passed in 114.5s. Mandatory CPU lanes passed,
  Profile/Debug built with zero warnings, DX12 produced zero InfoQueue errors
  with matching screenshots, standalone physics smoke passed, and
  `physics_regression_varied.csv` matched 44,401 lines byte-exactly. Log:
  `TestOutput/validation/r10_validate_full.log`.
- `tools\validate_dx12_renderer.bat`, three consecutive final implementation
  runs: 53.5s, 53.8s, and 53.6s. Every run reported zero InfoQueue errors and
  all three screenshots matched. Logs:
  `TestOutput/validation/r10_dx12_closure_1.log` through `_3.log`.
- Post-review shader-comment/freshness pass:
  `tools\validate_dx12_renderer.bat` passed in 53.7s with zero InfoQueue errors
  and matching screenshots. Log:
  `TestOutput/validation/r10_dx12_post_review.log`.
- Post-rubber-duck contract-fix pass: `tools\validate_dx12_renderer.bat` passed
  in 54.1s with zero InfoQueue errors and matching screenshots. Log:
  `TestOutput/validation/r10_dx12_post_duck.log`.
- `tools\run_graphics_stress.bat 1`: final post-review run completed in 61.6s
  and exited successfully through its PID-scoped timeout. From 15.903s to
  60.987s, warmed working set changed from 537,464,832 to 538,193,920 bytes:
  +729,088 bytes (+0.70 MiB), flat within sampling noise. Private bytes changed
  by +671,744 bytes. Artifacts: `TestOutput/graphics_stress/latest_memory.csv`
  (666 bytes) and `latest_memory.json` (4,578 bytes).
- `tools\validate_perf.bat`: the first run correctly rejected the intentional
  third-arena memory step. `tools\update_baselines.bat --perf --require`
  refreshed only the reviewed perf baselines; the exact final-tree perf gate
  passed in 66.1s. DX12 measured 173.50/243.06/243.06 MiB against the new
  173.29/243.41/243.41 baseline, and the physics bench measured
  173.44/239.06/239.06 against 173.54/239.75/239.49. Both lanes launch DX12 and
  therefore both own the third 32 MiB upload arena. Representative DX12 CPU
  markers improved: `Frame/Render` 0.7300 to
  0.4508 ms, balls 0.0474 to 0.0329 ms, and terrain 0.0315 to 0.0142 ms.
- `tools\validate_tests.bat`: passed in 6.5s after the review fix; 179/179 test
  cases and 4,072/4,072 assertions passed, including independent b0/b1 size
  coverage and rejection of b1 texture indices from vertex stages.
- `tools\bake_shaders.bat --check`: 43 stages current under DXC 1.8.2502.11
  and Shader Model 6.6.
- Acceptance searches found no hardcoded two-frame arrays/constant, stale
  t0..t5/HLSL-5 wording, or transient allocation/copy/root-table call in
  `RenderBackendDX12.Pipeline.cpp`.

## Comment-Style Audit Checklist

Scope: every source-bearing file in the final R10 diff. Checked: 29. Deferred:
0. Unchecked: none. `GeneratedShaderReflection.h` was inspected as deterministic
tool output; its generated-file banner is the intentional header exception.

- [x] `SkullbonezData/shaders/UIBackdropBlur.hlsl`
- [x] `SkullbonezData/shaders/generate_mips.hlsl`
- [x] `SkullbonezData/shaders/grid_line.hlsl`
- [x] `SkullbonezData/shaders/lit_textured.hlsl`
- [x] `SkullbonezData/shaders/lit_textured_instanced.hlsl`
- [x] `SkullbonezData/shaders/post_tonemap.hlsl`
- [x] `SkullbonezData/shaders/post_volumetric_light.hlsl`
- [x] `SkullbonezData/shaders/solid_color.hlsl`
- [x] `SkullbonezData/shaders/solid_color_batch.hlsl`
- [x] `SkullbonezData/shaders/text.hlsl`
- [x] `SkullbonezData/shaders/ui_render_target_preview.hlsl`
- [x] `SkullbonezData/shaders/unlit_textured.hlsl`
- [x] `SkullbonezData/shaders/water_calm.hlsl`
- [x] `SkullbonezData/shaders/water_ocean.hlsl`
- [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/GeneratedShaderReflection.h` (generated)
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [x] `SkullbonezSource/Rendering/RenderRasterBindingContract.h`
- [x] `SkullbonezSource/Rendering/ShaderContracts.h`
- [x] `SkullbonezSource/Rendering/ShaderReflectionContracts.h`
- [x] `SkullbonezTests/TestShaderReflectionContracts.cpp`
- [x] `tools/bake_shaders.py`

The audit added a local b1/root-constant invariant to converted shaders,
corrected stale t-register and transient-copy explanations, and reconciled all
shipping source labels with the global Shader Model 6.6 bake. No terms require
human-approved wording.

## Review

The separate read-only rubber-duck review initially blocked closure on two
recording/evidence issues and raised two non-blocking contract comments:

- The planned “existing per-instance payload” wording conflicted with the b1
  root-constant implementation. Resolution: `Dx12PipelineOwner` is the explicit
  owner of native descriptor indices. Keeping them in backend root constants
  preserves engine material rows and their packed flags; this owner ruling is
  now recorded in source and this report.
- The two perf baseline refreshes needed exact provenance. Resolution: both
  lanes were documented as DX12 consumers of the third upload arena and the
  exact final tree passed against those baselines with the memory figures above.
- b1 was accepted in any raster stage. Resolution: reflection now restricts it
  to pixel stages and a rejecting vertex-stage regression test passes.
- `RenderDeviceDX12.h` still taught the retired transient-copy raster flow.
  Resolution: the learning block now explains same-index static publication,
  b1 root constants, and the compute/DXR-only transient range.

The reviewer found no descriptor-lifetime defect and judged R1-R9 evidence
credible. Residual evidence risk is limited to unsupported SM6.6/Tier-3 hardware
and focused static-publication fault injection; this machine exercises only the
supported runtime path, while repeated renderer/stress gates cover publication
in normal use.

## Remaining External Work

Round 3 has no remaining local task. Portfolio task 313 is validation-gate V3,
which remains blocked on external merge-group proof, required branch
protection, and trusted/ephemeral DX12 runner administration.

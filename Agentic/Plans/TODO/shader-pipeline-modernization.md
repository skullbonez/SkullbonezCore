# Shader Pipeline Modernization And Binding Standardization

Date: 2026-07-11
Status: In progress — 25% (P0-P1 complete)
Impact area: DX12 renderer, all HLSL shaders, shader tooling, build/validation
scripts
Origin: 2026-07-11 architecture gap review. Owner directive: complete
rearchitecture to best practice, including the binding model. Compilation
modernization and binding standardization are one plan because
reflection-driven binding (the fix for the hand-maintained slot ABI) is only
possible once the compiler layer is modernized.

## Problem

1. **Runtime FXC compilation at Shader Model 5.0.** Every raster shader is
   compiled from HLSL source text at startup via `D3DCompile(...)` targeting
   `vs_5_0`/`ps_5_0` (`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` ~122-165).
   This locks the engine out of the entire SM6.x feature surface (wave ops,
   enhanced barriers, SM6.6 dynamic resources), recompiles everything on every
   launch, and means shader compile errors are runtime Lane R events instead
   of build failures.
2. **Hand-maintained slot ABI.** CPU root signatures, input layouts, and
   descriptor bindings must match each shader "exactly" by manual discipline —
   every shader file's invariant header says so, and plan documents carry
   warnings like "do not steal t4". There is no shader-reflection-driven
   validation, so every cbuffer or slot edit is a latent mismatch.
3. **No pipeline caching.** No `ID3D12PipelineLibrary`, no on-disk shader
   cache; PSOs and bytecode are rebuilt from scratch each run.
4. **One precedent already exists.** `reflect.rt.hlsl` is compiled offline to
   `reflect.rt.dxil` (DXR requires DXC/SM6.3+), proving the repo already
   builds and loads DXIL for one shader family. This plan extends that model
   to everything.

## Goal

- All shaders compile **offline via DXC to DXIL** at a pinned SM6.x baseline;
  the runtime loads bytecode and never invokes a compiler in shipping paths.
- Binding contracts are **generated from shader reflection** at bake time and
  **validated at load time** — a CPU/HLSL mismatch becomes a named startup
  failure (or better, a build failure), never a silent corruption.
- Root signatures are consolidated to a small named set with one documented
  slot map, ending per-shader tribal-knowledge contracts.
- PSO/bytecode caching removes redundant startup work.
- Dev-only hot reload becomes possible (cold-path recompile + PSO rebuild).

## Scope decisions (binding)

- **SM6.0 minimum baseline; SM6.6 evaluated at the P5 gate.** Do not sprinkle
  per-shader model targets; one pinned baseline in the bake script.
- **Offline compilation is the architecture; runtime compile survives only as
  a dev-mode fallback** behind an explicit launch option, and may be deleted
  once hot reload (P6) exists.
- **No new inheritance, no allocation-policy exceptions.** Bytecode blobs are
  loaded pre-gameplay (cold path); reflection metadata is baked into fixed
  POD tables.
- **Bindless is a decision gate, not a default.** SM6.6
  `ResourceDescriptorHeap` indexing is evaluated on merit at P5 with the
  descriptor-management owner; the plan is complete without it if the owner
  declines.
- Binding order is fixed. `TODO/render-backend-decomposition.md` A2 first
  establishes the concrete pipeline/root-signature owner. P1-P3 then modernize
  bytecode and consolidate that owner's contract. P4 places its cache in that
  owner. `TODO/shadow-edge-quality.md` S1 follows P3 and extends the surviving
  named contract.
- Critical-path refinement: do not run P0 against shaders scheduled for deletion
  or an interim config shape. DX12 cleanup and engine-config decomposition are
  now complete, so P0-P5 may start against the surviving surface. Their closure
  evidence is in `Agentic/Reports/2026-07-12/`; record the P5 bindless decision
  before shadow S1. P6 hot reload is optional follow-up and does not block
  shadows.

## P0 inventory and compiler baseline

The tracked inventory was generated with
`git ls-files "SkullbonezData/shaders/*.hlsl"`: 23 sources total. Twenty-one
are raster programs with `main_vs`/`main_ps`, one is the mip-generation compute
program, and one is the existing raytracing library. Field order below is the
authored cbuffer order; resource registers include spaces where authored.

| Shader | Entry points / target family | Cbuffer layout | Resource registers |
|--------|------------------------------|----------------|--------------------|
| `UIBackdropBlur.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uProjection`, `float4 uTexelSize` | `uTexture t0`, `sSampler0 s0` |
| `collision_visualizer.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uView`, `float4x4 uProjection`, `float4 uClipPlane`, `float4 uLightPosition` | none |
| `generate_mips.hlsl` | `main_cs` | `GenerateMipsCB b0`: `uint NumMipLevels`, `uint SrcDimension`, `float TexelSizeX`, `float TexelSizeY` | `LinearClampSampler s0`, `SrcMip t0`, `OutMip1..4 u0..u3` |
| `grid_line.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uViewProj` | none |
| `launcher_laser.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uViewProj` | none |
| `lit_textured.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uModel/uView/uProjection`, `float4 uClipPlane/uLightPosition/uLightAmbient/uLightDiffuse/uMaterialAmbient/uMaterialDiffuse/uCinematicTerrain/uCinematicBasin/uStyleModes/uTerrainTint/uTerrainAccent/uTerrainGrid`, `float4x4 uShadowViewProj`, `float4 uShadowParams/uShadowFlags` | `uTexture t0`, `uShadowMap t3`, `sSampler0 s0`, `sSampler3 s3` |
| `lit_textured_instanced.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uView/uProjection`, `float4 uClipPlane/uLightPosition/uLightAmbient/uLightDiffuse/uMaterialAmbient/uMaterialDiffuse`, `int uObjectStyle`, `int uPrimitiveShape`, `float uMaterialAlpha`, `float _objectStylePad`, `float4x4 uShadowViewProj`, `float4 uShadowParams/uShadowFlags` | `uTexture t0`, `uShadowMap t3`, `uMaterialTable t4`, `sSampler0 s0`, `sSampler3 s3` |
| `post_tonemap.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float uExposure/uGamma/uVolumetricCompositeStrength/_padding0`, `float4 uDepthParams/uFogParams`, `float3 uFogColor`, `float _padding1`, `float4 uBloomTexelSize/uBloomParams/uStyleGrade` | `uSceneTex t0`, `uDepthTex t1`, `uVolumetricTex t2`, `sSampler0 s0`, `sSampler1 s1` |
| `post_volumetric_light.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4 uDepthParams/uSunShaftParams`, `float3 uSunColor`, `float _padding0`, `float4 uVolumetricParams` | `uSceneTex t0`, `uDepthTex t1`, `sSampler0 s0`, `sSampler1 s1` |
| `reflect.rt.hlsl` | `RayGen`, `ClosestHit`, `Miss` (`lib_6_3`) | `RTConstants b1`: `float4x4 gInvViewProj`, `float3 gCameraPos`, `float gWaterY`, `float3 gLightPos`, `float gTime`, `float3 gSkyColorTop`, `float _pad0`, `float3 gSkyColorBottom`, `float _pad1` | `gScene t0 space1`, `gOutput u0`, `gSphereTex..gSkyBack t0..t7`, `gSampler s0` |
| `shadow_depth.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uModel/uView/uProjection`, `float4 uClipPlane/uCinematicTerrain/uCinematicBasin` | none |
| `shadow_depth_instanced.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uView/uProjection`, `float4 uClipPlane` | none |
| `sky_atmosphere.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4 uSunParams`, `float3 uSunColor`, `float _padding0`, `float3 uHorizonColor`, `float _padding1`, `float3 uZenithColor`, `float _padding2`, `float4 uCloudParams`, `float4x4 uInvView/uInvProjection`, `int uSkyMode`, `float3 _padding3` | none |
| `soft_additive_ribbon.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uViewProj` | none |
| `solid_color.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uProjection`, `float4 uColor` | none |
| `solid_color_batch.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uProjection` | none |
| `text.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uProjection` | `uFontTexture t0`, `sSampler0 s0` |
| `tornado_fx.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uViewProj` | none |
| `trajectory_ribbon.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uViewProj`, `float4 uViewportPixels/uRibbonStyle` | none |
| `ui_render_target_preview.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uProjection`, `float4 uPreviewParams` | `uTexture t0`, `sSampler0 s0` |
| `unlit_textured.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uModel/uView/uProjection`, `float4 uColorTint` | `uTexture t0`, `sSampler0 s0` |
| `water_calm.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uModel/uView/uProjection/uReflectVP`, `float4 uColorTint`, `float uReflectionStrength/uWaterFresnelF0`, `float3 uCameraWorld`, `int uNoReflect`, `float uCinematicMode`, `int uWaterMode`, `float uSunGlintStrength`, `float3 uSunColor`, `float4 uBasinMask`, `float uBasinMaskFeather`, `float3 _pad0` | `uReflectionTex t1`, `sSampler1 s1` |
| `water_ocean.hlsl` | `main_vs`, `main_ps` | `Uniforms b0`: `float4x4 uModel/uView/uProjection/uReflectVP`, `float4 uColorTint`, `float uTime/uWaveHeight/uReflectionStrength/uWaterFresnelF0`, `float3 uCameraWorld`, `float uPerturbStrength`, `int uFlatWater/uNoReflect`, `float uCinematicMode/uSunGlintStrength`, `float3 uSunColor`, `float _pad0` | `uReflectionTex t1`, `sSampler1 s1` |

Pinned compiler decision: Windows SDK `10.0.26100.0` x64
`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe`,
reporting `dxcompiler.dll 1.8.2502.11 (239921522)` and `dxil.dll
1.8.2502.11`. The bake rejects any different version. Raster and compute use
one SM6.0 baseline (`vs_6_0`, `ps_6_0`, `cs_6_0`); it is the minimum already
chosen by this plan, is supported by the DX12/DXIL runtime that the renderer
requires, and avoids raising the hardware contract before P5 evaluates SM6.6.
The existing DXR library remains `lib_6_3` because raytracing requires it.

## P0-P1 implementation evidence

- [x] P0: reconciled all 23 tracked HLSL sources and recorded every entry-point
  family, cbuffer field order, and `b/t/s/u` register above.
- [x] P1: `tools/bake_shaders.py` and `.bat` compile 43 shipping stages (21 VS,
  21 PS, one CS) with `-WX -Ges -O3 -Zpc -Qstrip_debug`; 43 `.dxil` files total
  244,460 bytes. `shader_manifest.json` SHA-256 is
  `0cf28e3689ed1cd37636d4b8b0da607fcf0143ddea7c73de4263f54d50885023`.
- [x] Shipping raster and mip-generation paths accept only source- and
  bytecode-hash-current manifest rows. `D3DCompile` remains solely behind the
  exact `--dev-compile-shaders` option and logs
  `dx12_shader_dev_source_fallback` with the rejected-bake reason.
- [x] DXC/FXC hazards were made explicit: `-Zpc` matches the existing
  `#pragma pack_matrix(column_major)` and column-major engine matrix storage;
  every sampler/resource is explicitly registered (no inference dependency);
  `-O3` is pinned across configurations, so optimizer differences are a visual
  gate risk rather than configuration drift. No visual baseline was changed.
- [x] Focused checks: bake 1.68s, immediate bake + freshness check 2.1s;
  a second bake changed 0/44 total DXIL files (43 modernized stages plus the
  existing raytracing library);
  final Profile build 5.78s with zero warnings/errors; direct DX12 two-frame request
  exited 0 in 2.96s, proving baked DXIL load and DXC container reflection.
  Withholding one VS asset under `--dev-compile-shaders` emitted exactly one
  named fallback line; restoring it returned `--check` to PASS. Allocation
  policy scan passed in 7.53s with zero allowlist errors.
  The initial `tools\validate_fast.bat` coordinator run stopped after 11.899s
  because the project-filter validator rejected wildcard DXIL items and had no
  rule for the new manifest source/data. The fix lists all 43 generated stages
  explicitly and teaches the validator the manifest source prefix and shader
  JSON resource; `tools\validate_fast.bat` then passed in 25.426s with 651/651
  project/filter items and zero-warning Profile/Debug builds.
  `tools\validate_dx12_renderer.bat` passed in 23.011s: 43-stage freshness,
  zero DX12 InfoQueue errors, and all three screenshot comparisons passed with
  no baseline update (`water_ball_test` max diff 33, `solver_smoke` 61,
  `space_three_body` 0). `tools\run_graphics_stress.bat 1` passed in 60.906s:
  13,242 frames, 368 scene loads, PID-scoped timeout shutdown, stdout 54,708
  bytes, stderr 0 bytes, memory CSV 667 bytes, and shutdown JSON 4,580 bytes.
  Touched-source/tool comment audit: 7/7 checked, 0 deferred, including the
  already-compliant project-filter validator changed during the gate fix.

## Phases

### P0 — Inventory and baseline decision

Inventory every shader (`SkullbonezData/shaders/*.hlsl`), its entry points,
cbuffer layouts, and current register slots into a table in this plan.
Confirm DXC availability in the toolchain (bundled with the Windows SDK the
repo already requires; record the pinned dxc version). Decide and record the
SM6.x baseline. Documentation-only; no validation.

### P1 — Offline DXC bake step

- Add `tools\bake_shaders.py` (+ `.bat` wrapper): compiles every raster
  shader to `.dxil` next to source (mirroring the `reflect.rt.dxil`
  convention), fails loudly on any warning, and writes a manifest with source
  hash → bytecode mapping so stale bytecode is detectable.
- `ShaderDX12` loads `.dxil` when present and current; falls back to runtime
  `D3DCompile` only under a dev launch option, with a log line naming the
  fallback.
- Wire the bake step into `tools\validate_dx12_renderer.bat` before the
  build step so stale/failed shader compiles fail the gate first.
- Hazards to verify explicitly: DXC vs FXC semantic differences —
  `#pragma pack_matrix(column_major)` honoring, sampler/register inference,
  and optimization differences that can shift pixel output. Expect near- but
  possibly not byte-identical images; if screenshots diff, inspect before
  accepting any baseline update.

Gate: `tools\validate_fast.bat` (new tool script), then
`tools\validate_dx12_renderer.bat`. Baseline update only if DXC output
legitimately shifts pixels, in an isolated reviewed commit.

### P2 — Reflection-generated binding metadata

- Extend the bake step: use DXC reflection to emit, per shader, a POD table
  of cbuffer sizes/field offsets, bound resource slots (t/s/u/b), and input
  layout signature into a generated header or binary sidecar.
- At shader load, validate the CPU-side binding contract against the
  reflected table: cbuffer size mismatch, missing slot, or layout mismatch is
  a named Lane R failure with owner diagnostics (fail at startup, not at
  draw).
- Add a `SkullbonezTests`/`Agentic/Tests` architecture test asserting every
  shipped shader's reflection table matches the CPU contract structs — the
  "CPU and HLSL must match by hand" review rule becomes machine-checked.

Gate: `tools\validate_tests.bat` + `tools\validate_dx12_renderer.bat`,
baselines unchanged (metadata-only phase).

### P3 — Root signature consolidation

- Design a small named set of root signatures (expected: one raster contract
  covering lit/unlit/water/post families, one for text/UI if its layout
  genuinely differs, DXR keeps its existing local/global signatures). Define
  each in one place with a documented slot map that replaces the per-shader
  invariant folklore; consider HLSL `[RootSignature]` attributes compiled at
  bake time so the shader is the single source of truth.
- Migrate shader families one commit per family; the reflection check from
  P2 verifies each migration mechanically.
- Retire "do not steal tN" style comments in favor of the named slot map;
  update `RenderRasterBindingContract.h` to be generated-from or verified-
  against the new map.

Gate per family: `tools\validate_dx12_renderer.bat`, baselines unchanged
(binding refactor must be visually inert). Root-signature changes are the
highest-risk slice in this plan — the shadow plan's warning applies to every
lit shader at once.

### P4 — PSO and bytecode caching

- Add `ID3D12PipelineLibrary`-based PSO cache (or driver-cache-friendly
  explicit blob cache) persisted under a writable cache dir; startup loads
  the library and misses compile-and-store. Cache invalidation keys on the
  P1 manifest hashes.
- Measure and record before/after startup time in this plan.

Gate: `tools\validate_dx12_renderer.bat` ×3 consecutive (cache warm/cold
paths touch frame startup), `tools\validate_perf.bat`.

### P5 — SM6.6 dynamic-resources decision gate (owner decision)

Evaluate moving material/texture binding to SM6.6 `ResourceDescriptorHeap`
indexing (bindless): what it deletes (per-draw descriptor table juggling,
slot budget pressure — the t4 class of problem disappears), what it costs
(descriptor lifetime discipline, debuggability, minimum feature level).
Present the trade to the owner; implement only on explicit approval as its
own phase with `validate_dx12_renderer` + stress gates. The plan is
completable with a "no, revisit later" recorded here.

### P6 — Dev hot reload (cold path, optional but cheap after P1/P4)

File-watch or manual-key recompile of changed shaders through the same DXC
path, PSO rebuild through the P4 cache, all outside steady gameplay
allocation rules (explicit cold utility action). Delete the runtime
`D3DCompile` fallback once this lands.

Gate: `tools\validate_dx12_renderer.bat`; hot reload itself is dev-only and
manually verified.

### P7 — Closure

Touched-file comment audit; single rubber-duck review; final
`tools\validate_full.bat`; update SessionState/MASTER-PLAN; delete plan files
on completion.

## Acceptance

- [ ] No shipping-path runtime shader compilation; all shaders load pinned
      SM6.x DXIL with hash-verified freshness.
- [ ] A deliberate CPU/HLSL cbuffer mismatch is caught by the P2 machine
      check (test this the same way behavioral-test-depth P5 drills bugs).
- [ ] Root signatures reduced to a named, documented set; no per-shader slot
      folklore comments remain.
- [ ] Startup does not recompile unchanged shaders or rebuild unchanged PSOs.
- [ ] `dx12_validation.txt` = 0 errors and screenshots match accepted
      baselines at every phase.

## Validation map

| Slice | Gate |
|-------|------|
| Bake tooling | `validate_fast`, then `validate_dx12_renderer` |
| DXC migration / root signatures | `validate_dx12_renderer` (baselines unchanged unless reviewed) |
| Reflection contract checks | `validate_tests` + `validate_dx12_renderer` |
| PSO cache | `validate_dx12_renderer` ×3 + `validate_perf` |
| Bindless (if approved) | `validate_dx12_renderer` + `run_graphics_stress.bat 1` |

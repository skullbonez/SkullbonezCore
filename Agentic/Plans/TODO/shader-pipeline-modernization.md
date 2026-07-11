# Shader Pipeline Modernization And Binding Standardization

Date: 2026-07-11
Status: In progress — 88% (P0-P6 complete)
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

P2 implementation evidence (2026-07-12):

- [x] Pinned `dxc -dumpbin` reflection now covers all 43 shipping
  raster/compute stages. The manifest records cbuffer names/sizes and every
  field name/offset/size, all `b/t/s/u` resources with space/type/dimension,
  and VS input signatures. The bake also emits
  `GeneratedShaderReflection.h` as deterministic fixed POD; `--check` rejects
  either stale representation. P1 compiler flags, version pin, and bytecode
  hashes remain the compilation identity.
- [x] `ShaderDX12` validates required CPU uniform/resource declarations during
  startup and compares the loaded container's cbuffer size and every field
  offset/size against generated metadata. `Dx12PipelineOwner` validates the
  concrete CPU input layout against the VS signature before publishing a PSO.
  Failures are named Lane R events owned by `ShaderDX12` or
  `Dx12PipelineOwner`; no exception or callback boundary was added.
- [x] `TestShaderReflectionContracts.cpp` proves 43/43 stage coverage,
  explicitly covers the compute stage (16-byte cbuffer, seven resources, no
  raster input), checks all seven current CPU program contracts, and performs
  deliberate mismatch drills by changing `lit_textured.uTexture` from t0 to
  t2 and `uModel` from a 64-byte matrix to a 16-byte vector. Exact focused command
  `Profile\SKULLBONEZ_TESTS.exe --test-case="Shader reflection contracts:*"`
  passed 5 cases and 103 assertions after both mutations were restored in-memory;
  the additional case compares all 21 raster VS signatures against the
  independent CPU-owned input-layout table.
- [x] Focused evidence: bake plus reflection generation passed in 2.34s;
  immediate `python tools\bake_shaders.py --check` passed 43 stages; targeted
  test build passed with zero warnings/errors; `tools\validate_build.bat
  Profile` passed in 6.603s wall time (5.99s MSBuild) with zero
  warnings/errors; direct two-frame DX12 smoke exited 0 in 2.851s with 52,571
  stdout bytes and zero stderr bytes. The coordinator then made the generated
  header formatter-canonical: the first `validate_fast` stopped before build
  in 10.548s on `GeneratedShaderReflection.h`; the bake now discovers the
  repository's pinned `clang-format`, formats the emitted bytes, and preserves
  byte-exact `--check` freshness. The retry passed in 35.583s with 653/653
  project/filter rows and zero build warnings/errors.
- [x] Formal evidence: `tools\validate_tests.bat` passed in 2.533s with 144
  cases and 3,414 assertions. `tools\validate_dx12_renderer.bat` passed in
  24.432s with zero DX12 validation errors and committed-baseline maximum
  channel differences of 50 (`water_ball_test`), 70 (`solver_smoke`), and 0
  (`space_three_body`); no baseline was updated. Mandatory
  `tools\run_graphics_stress.bat 1` passed in 60.916s with PID-scoped timeout
  exit 0 after 12,780 frames and 355 scene loads; stdout was 53,075 bytes,
  stderr was empty, and the 666-byte CSV plus 4,580-byte shutdown JSON memory
  artifacts were written.
- [x] Touched-source/tool comment audit: 11/11 files checked, zero deferred.
  The ten authored source/tool files retain complete learning headers and local
  contract/invariant explanations; the eleventh is the deterministic generated
  reflection header whose generator banner makes the no-manual-edit contract
  explicit.

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

P3 implementation evidence (2026-07-12):

- [x] Consolidated the 21 raster families under the single named
  `UnifiedRaster` root signature. A second text/UI signature was rejected as
  dishonest duplication: reflection proves those families use the same b0,
  t0..t4, s0/s1/s3, space-zero envelope as lit, unlit, water, and post. The
  generate-mips compute signature and existing DXR local/global ownership are
  unchanged.
- [x] `RenderRasterBindingContract.h` is now the one documented slot map:
  `DrawConstants` b0/root 0; `PrimaryTexture` t0/root 1;
  `SecondaryTexture` t1/root 2; `VolumetricTexture` t2/root 3; `ShadowMap`
  t3/root 4; `MaterialTable` t4/root 5; and named static samplers
  `LinearWrap` s0, `LinearClamp` s1, and `ShadowPointClamp` s3. Native
  descriptor construction and every raster draw-time root bind consume that
  table instead of repeating root-parameter literals.
- [x] P2 metadata mechanically verifies the map at startup and in tests across
  all 42 raster stages. Non-zero spaces, resources outside b0/t0..t4/s0/s1/s3,
  vertex-stage textures/samplers, non-2D textures, and unexpected register
  classes fail as a named Lane R `Dx12PipelineOwner` startup result before root
  signature or PSO publication. Mutation drills prove t5 and s2 are rejected.
- [x] Family migration was grouped into this coherent binding slice per the
  owner speed/granularity directive; no HLSL register changed, so the change is
  intentionally bytecode- and visual-output-inert. Shader-local slot ownership
  is replaced by the named map in repository shader guidance and CPU contract
  documentation.
- [x] Focused evidence: final declared-toolset Profile test build passed in
  3.588s and engine build passed in 8.643s, both with zero warnings/errors; exact
  `Profile\SKULLBONEZ_TESTS.exe --test-case="Shader reflection contracts:*"`
  passed 7 cases and 115 assertions in 2.510s; a visible-window two-frame DX12
  water-scene smoke exited 0 in 2.028s with 466 stdout bytes and zero stderr.
  The first build invocation forced unavailable v143 and stopped before
  compilation in 1.336s; rerunning the project-declared v145 corrected the
  environment selection. A hidden-window smoke produced a zero-sized client
  area and stopped in 3.035s; the required visible-window rerun passed.
- [x] Touched-source comment audit: 7/7 checked, zero deferred.
- [x] Formal coordinator evidence: `tools\validate_tests.bat` passed in 3.540s
  with 146 cases and 3,426 assertions. `tools\validate_dx12_renderer.bat`
  passed in 34.877s with zero DX12 validation errors and unchanged committed
  baselines (maximum channel differences 50, 70, and 0). Mandatory
  `tools\run_graphics_stress.bat 1` passed in 60.889s with PID-scoped timeout
  exit 0 after 13,036 frames and 363 scene loads; stdout was 54,060 bytes,
  stderr was empty, and the 667-byte CSV plus 4,576-byte shutdown JSON memory
  artifacts were written.

### P4 — PSO and bytecode caching

- Add `ID3D12PipelineLibrary`-based PSO cache (or driver-cache-friendly
  explicit blob cache) persisted under a writable cache dir; startup loads
  the library and misses compile-and-store. Cache invalidation keys on the
  P1 manifest hashes.
- Measure and record before/after startup time in this plan.

Gate: `tools\validate_dx12_renderer.bat` ×3 consecutive (cache warm/cold
paths touch frame startup), `tools\validate_perf.bat`.

### P4 implementation evidence (2026-07-12)

- `Dx12CachedPsoStore` persists driver-produced
  `ID3D12PipelineState::GetCachedBlob()` records under
  `%LOCALAPPDATA%\SkullbonezCore\PipelineCache`, with a fixed-buffer
  `SKULLBONEZ_PSO_CACHE_DIR` override for isolated validation. Startup maps the
  file read-only and parses at most 96 records into fixed metadata; PSO misses
  never perform disk I/O or grow storage. Shutdown closes the old mapping,
  requests fresh blobs while the fixed live PSO array still owns them, and
  streams a capped 16 MiB replacement through an atomic temporary-file rename.
- File identity is SHA-256 of the complete P1 manifest (pinned compiler,
  flags, and every source/compile-input/bytecode hash) plus SHA-256 of the
  serialized `UnifiedRaster` root signature. Each record is SHA-256-addressed
  by exact shader bytes and canonical field-by-field graphics descriptor/input
  layout state. COM pointers and `CachedPSO` output bytes are deliberately
  excluded. A warm cached-blob rejection is a named Lane R event, evicts the
  row, and retries the same native PSO creation once without cached bytes.
- Focused identity tests passed 2 cases and 19 assertions in 2.469s, including
  repeatability, pointer independence, unchanged identity with an attached
  cached blob, and invalidation after manifest, root-signature, shader-byte,
  blend-state, or input-layout mutations. The final Profile solution build
  passed in 7.075s (6.40s MSBuild) with zero warnings/errors.
- The initial `ID3D12PipelineLibrary` prototype was rejected rather than
  shipped: its isolated renderer-suite cold launch exited 0 in 2.029s, but the
  accepted 106,692-byte library triggered `DXGI_ERROR_DEVICE_REMOVED` on the
  warm launch. This was the evidence for selecting the plan-permitted explicit
  cached-blob format. With that replacement, the identical isolated renderer
  suite exited 0 with empty stderr on both paths: cold 3.690s and warm 2.025s
  (1.665s / 45.1% lower wall time), persisting a bounded 143,392-byte file.
  These focused timings establish the path behavior.
- Formal isolated-cache renderer gates passed three consecutive times: cold
  24.733s, warm 23.413s, and warm 23.319s. All three reported zero DX12
  validation errors, unchanged committed-baseline maximum channel differences
  of 50/70/0, and the same bounded 143,392-byte cache artifact. A final renderer
  gate after the input-contract regression fix also passed with zero validation
  errors and matching screenshots.
- The first perf gate exposed six rejected shadow PSOs per frame: P2 had treated
  reflected vertex inputs and mesh attributes as equal sets, even though DX12
  legally permits a POSITION-only shadow shader to consume a richer
  POSITION/NORMAL/TEXCOORD mesh. The corrected subset contract uses fixed
  diagnostics, and a regression test proves both the legal superset and a bad
  POSITION width. Final `tools\validate_tests.bat` passed 149 cases and 3,448
  assertions. Final `tools\validate_perf.bat` passed with zero steady-gameplay
  allocations (220,255 total cold/lifecycle allocations), both DX12 and physics
  absolute budgets, and no baseline regressions.
- Mandatory `tools\run_graphics_stress.bat 1` completed its one-minute
  PID-scoped run after 13,177 frames and 367 scene loads. Stdout was 54,552
  bytes, stderr was empty, and the 663-byte memory CSV plus 4,578-byte shutdown
  JSON were written.
- Touched-source/tool comment audit: 12/12 checked, zero deferred. New cache,
  reflection-contract, regression-test, renderer, and validation-tool files all
  retain complete learning headers plus local canonical-hash, lifetime,
  allocation, recovery, and Lane R explanations.

### P5 — SM6.6 dynamic-resources decision gate (owner decision)

Evaluate moving material/texture binding to SM6.6 `ResourceDescriptorHeap`
indexing (bindless): what it deletes (per-draw descriptor table juggling,
slot budget pressure — the t4 class of problem disappears), what it costs
(descriptor lifetime discipline, debuggability, minimum feature level).
Present the trade to the owner; implement only on explicit approval as its
own phase with `validate_dx12_renderer` + stress gates. The plan is
completable with a "no, revisit later" recorded here.

P5 decision (2026-07-12): **No — revisit later.** This is a completed decision
gate, not an implementation deferral inside the current architecture.

| Evidence | Current engine | Bindless consequence |
|---|---|---|
| Descriptor pressure | One-minute stress finished with 22 textures, texture capacity 28, 25 static SRV rows, and transient peak 115/2,048. UnifiedRaster's t0-t4 map is not exhausted. | Removes per-draw descriptor copies and the fixed t-slot ceiling, but solves no measured capacity or frame-budget failure today. |
| Device baseline | P0 deliberately retained the SM6.0 shipping minimum. | `ResourceDescriptorHeap` requires both SM6.6 and Resource Binding Tier 3, narrowing the supported adapter set and requiring explicit feature rejection/fallback policy. |
| Lifetime/order | Static source descriptors plus fence-scoped transient rows already make reuse explicit and validated. | Heap-indexed descriptors/data are volatile; the resource/sampler heaps must be set before a directly-indexed root signature, and descriptor/resource lifetime becomes shader-index authority. |
| Shader/debug cost | Reflection names t0-t4 ownership and DX12 validation sees ordinary table binds. | Every migrated shader must carry descriptor indices, divergent indices require `NonUniformResourceIndex`, and out-of-range/stale indices can produce undefined reads or device reset. |

The measured benefit is therefore smaller than the compatibility, lifetime,
and debugging cost. No explicit owner approval to raise the hardware contract
was given, so the plan's implementation guard remains closed. Reopen P5 only
when one of these deletion conditions is measured: UnifiedRaster exceeds t0-t4,
static or transient descriptor capacity becomes a real gate failure, per-draw
descriptor copies become a demonstrated performance bottleneck, or the product
baseline explicitly requires Tier 3 + SM6.6. Review evidence:
`Dx12DescriptorAllocator`/UnifiedRaster source inspection, the P4 stress memory
snapshot above, and Microsoft's
[`HLSL Dynamic Resources`](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html)
and
[`Advanced use of Descriptor Tables`](https://learn.microsoft.com/en-us/windows/win32/direct3d12/advanced-use-of-descriptor-tables)
specifications. Documentation-only decision; no additional validation required.

### P6 — Dev hot reload (cold path, optional but cheap after P1/P4)

File-watch or manual-key recompile of changed shaders through the same DXC
path, PSO rebuild through the P4 cache, all outside steady gameplay
allocation rules (explicit cold utility action). Delete the runtime
`D3DCompile` fallback once this lands.

Gate: `tools\validate_dx12_renderer.bat`; hot reload itself is dev-only and
manually verified.

P6 implementation evidence (2026-07-12):

- [x] F9 is a normalized runtime input action backed by the optional
  `IRenderShaderDevelopment` capability and enabled only by the exact
  `--dev-shader-hot-reload` launch token. The cold action synchronously invokes
  `tools\bake_shaders.bat`, the same pinned DXC/manifest path used by validation.
- [x] `Dx12PipelineOwner` retains a fixed 64-row live raster-shader registry.
  Reload compiles every replacement into fixed bytecode payloads, verifies the
  running executable's reflection/constant layout, drains GPU work, persists
  and releases old PSOs, then adopts all raster payloads and the generate-mips
  compute PSO in one no-fail commit. Existing constant values survive. Grid-line
  PSOs and the P4 cache are invalidated/rekeyed explicitly; any bake, reflection,
  or candidate failure leaves current shaders and PSOs live.
- [x] Removed the `D3DCompile` fallback and `--dev-compile-shaders` policy from
  both raster and generate-mips startup. Runtime now accepts only manifest-current
  SM6 DXIL. `reflect.rt.dxil` retains its pre-existing separate library workflow
  and is outside the 43-stage P1 manifest/P4 raster-cache transaction.
- [x] Automated manual-path command used
  `--dev-shader-hot-reload --interaction-script
  SkullbonezData\interaction\shader_hot_reload_f9.json`. It exited 0 with
  `[shader-hot-reload] bake begin`, `[shader-hot-reload] committed`, a consumed
  F9 action, and `ok=true` report after 10 frames. The same script without the
  launch token exited 0, kept the app alive, and reported the exact opt-in
  requirement. A visible-window Computer Use pass also confirmed rendering
  continued after F9.
- [x] Focused Profile build passed in 5.15s with zero warnings/errors. The final
  allocation-policy scan passed 317 files with zero allowlist errors; the first
  staging design's banned `optional::emplace` was replaced by fixed payload rows
  rather than allowlisted.
- [x] Final broad gate completed in approximately 94s: 149/149 doctest cases
  and 3,487 assertions, every standalone CPU lane, zero-warning Profile/Debug
  builds, zero DX12 validation errors with unchanged screenshot maximum
  differences 33/61/0, and the 44,401-line physics baseline byte-exact. The
  wrapper returned a contradictory code after the script printed
  `VALIDATE_FULL: DEFAULT GATE PASSED`; P7 reruns the final gate directly.
- [x] Mandatory `tools\run_graphics_stress.bat 1` exited 0 after 13,107 frames
  and 365 scene loads. Stdout was 54,306 bytes, stderr was empty, and the
  671-byte memory CSV plus 4,578-byte shutdown JSON were written.
- [x] Touched-source/tool comment audit: 19/19 checked, zero deferred. The new
  capability and all touched renderer/runtime/input/test/tool files retain full
  learning headers and local allocation, transaction, lifetime, opt-in, and
  failure-path explanations.

### P7 — Closure

Touched-file comment audit; single rubber-duck review; final
`tools\validate_full.bat`; update SessionState/MASTER-PLAN; delete plan files
on completion.

## Acceptance

- [x] No shipping-path runtime shader compilation; all shaders load pinned
      SM6.x DXIL with hash-verified freshness.
- [x] A deliberate CPU/HLSL cbuffer mismatch is caught by the P2 machine
      check (test this the same way behavioral-test-depth P5 drills bugs).
- [x] Root signatures reduced to a named, documented set; no per-shader slot
      folklore comments remain.
- [x] Startup does not recompile unchanged shaders or rebuild unchanged PSOs.
- [x] `dx12_validation.txt` = 0 errors and screenshots match accepted
      baselines at every phase.

## Validation map

| Slice | Gate |
|-------|------|
| Bake tooling | `validate_fast`, then `validate_dx12_renderer` |
| DXC migration / root signatures | `validate_dx12_renderer` (baselines unchanged unless reviewed) |
| Reflection contract checks | `validate_tests` + `validate_dx12_renderer` |
| PSO cache | `validate_dx12_renderer` ×3 + `validate_perf` |
| Bindless (if approved) | `validate_dx12_renderer` + `run_graphics_stress.bat 1` |

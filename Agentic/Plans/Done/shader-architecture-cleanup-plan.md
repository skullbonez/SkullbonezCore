# Shader Architecture Cleanup Plan

Status: done on `main`; validated 2026-06-16
Created: 2026-06-11
Scope: render architecture, shader contracts, shader source hygiene, style data, material system v1
Implementation status: shader inventory, high-risk runtime contract diagnostics,
pass binder cleanup, CPU render-material compatibility mapping, expanded
instance material payloads, typed object/shadow CBV upload paths, object GPU
material table sampling, shader contract checking, and graph/native-resource
transition diagnostics are implemented.
Validation: `tools\validate_shaders.bat` passed with 0 errors and 7 known
manifest-coverage warnings; `tools\validate_full.bat` passed with DX12
validation errors 0, matching renderer baselines, byte-exact physics, and perf
validation.

## Completion Note

This plan is archived because the object-side shader/material architecture
cleanup landed on `main`. Terrain, water, sky, and post material cleanup should
be tracked in focused future plans rather than reopening this plan wholesale.

## Executive Summary

The shader architecture is not hopeless, but it is carrying several layers of history at once:

- The old fixed-function migration left a useful but minimal `IShader` abstraction.
- The retired tri-renderer work left useful lessons about contracts, but active shader work is now HLSL/DXC-first for DX12.
- The cinematic renderer added HDR, sky, fog, bloom, volumetric light, terrain relief, and water style controls into the same shader family.
- The 20 concept-look work added style files and object material names, but the runtime still reduces object material data to tint RGB plus one overloaded float mode.

That means the system works, but the contracts are mostly implicit. Shader inputs, texture slots, uniform names, cbuffer packing, instance attributes, and material meanings are spread across C++ call sites and duplicated GLSL/HLSL code. This is exactly the stage where a small material system and explicit shader/pass contracts will pay off. A full node material graph would be too much for this engine right now; a backend-neutral CPU material model, DX12-canonical shader metadata, and pass manifests are the right target.

Recommended strategy:

1. Stabilize shader contracts before changing look behavior.
2. Keep shader count low and make the existing shader families data-driven.
3. Add a compact `RenderMaterial`/`MaterialParams` layer for objects.
4. Separate frame/pass/style/material data in C++ even if the backend still uploads one reflected cbuffer per shader at first.
5. Make HLSL/DXC reflection the canonical shader contract for production.
6. Change backend/root-signature contracts only for concrete material/resource
   needs, and document the binding ABI at the same time.
7. Keep shader metadata portable enough that a future Vulkan or Metal backend can map engine contracts to SPIR-V/MSL without changing scene or material authoring.

## Current Architecture Read

### Render Backend Shape

The render API is centered on `IRenderBackend` and a global `Gfx()` accessor. The interface currently owns device lifecycle, swap chain/present, fixed-function-like state, shader creation, mesh creation, framebuffer creation, texture handles, screenshot capture, DXR hooks, GPU timers, dynamic vertex buffers, debug lines, and instancing.

Relevant anchors:

- `SkullbonezSource/SkullbonezIRenderBackend.h`
- `SkullbonezSource/SkullbonezRenderBackendDX12.cpp`
- `Agentic/Plans/architecture_pass_2026-06-02.md`

Shader creation is DX12-specific today:

- DX12 resolves `baseName.hlsl` and compiles `main_vs`/`main_ps`.
- DXR reflection is separate through `reflect.rt.hlsl` and checked-in `reflect.rt.dxil`.

The ordinary raster shader abstraction is:

```cpp
class IShader
{
    virtual void Use() const = 0;
    virtual void SetInt(const char* name, int value) const = 0;
    virtual void SetFloat(const char* name, float value) const = 0;
    virtual void SetVec3(...) const = 0;
    virtual void SetVec4(...) const = 0;
    virtual void SetMat4(...) const = 0;
};
```

This is simple and useful, but it is not a contract. It does not know which uniforms are required, which texture slots are valid, which vertex layout a shader expects, or which pass owns which state.

### Shader Upload Behavior

Current upload behavior is DX12-oriented:

- DX12 compiles HLSL, reflects constant-buffer variables, writes into one CPU-side cbuffer shadow, and suballocates/upload-binds it through root parameter 0.

Important behavior:

- Missing uniforms on DX12 are silently ignored because `Set*` returns when the reflected name is absent.
- That behavior keeps older call sites tolerant, but it hides stale names, typoed names, or backend-only divergence.

### DX12 Ordinary Raster Contract

The current DX12 raster root signature is a narrow implicit contract:

- Root 0: one CBV at `b0`.
- Root 1: one SRV table for `t0`.
- Root 2: one SRV table for `t1`.
- Root 3: one SRV table for `t2`.
- Static sampler `s0`: linear wrap.
- Static sampler `s1`: linear clamp.

This supports current passes, including three-texture post. It is not enough for a future material table, texture arrays, or bindless-style material textures without changing the contract.

Do not change this root signature as the first cleanup step. First make the material model explicit using the existing slots and instance data. Change root signatures only when a real material table or texture indirection needs it.

### Frame Render Path

The main pass orchestration currently lives in `SkullbonezRun::DrawPrimitives()` and related helpers in `SkullbonezRunRender.cpp`.

Current order, simplified:

1. Establish cinematic mode and light direction.
2. Prepare model streams.
3. Draw skybox or cinematic sky.
4. Render water reflection by FBO mirror path or DXR path.
5. Bind HDR cinematic scene FBO when enabled.
6. Render sky, objects, terrain, shadows, water.
7. Render broadphase and physics debug overlays.
8. Run volumetric light pass.
9. Run tonemap/fog/bloom/god-ray resolve to backbuffer.
10. Draw UI/text afterward.

This order is coherent. The problem is not the order; the problem is that shader state setup is scattered across `SkullbonezRunRender.cpp`, `SkullbonezHelper.cpp`, `SkullbonezTerrain.cpp`, `SkullbonezWorldEnvironment.cpp`, `SkullbonezSkyBox.cpp`, `SkullbonezText.cpp`, and UI blur helpers.

### Object Rendering And Materials

Objects render through shared instanced helpers:

- Spheres, boxes, and pine visuals use `lit_textured_instanced`.
- The instance payload is currently `mat4 model + vec4 tint`.
- `tint.rgb` is material color or texture multiplier.
- `tint.a` is overloaded:
  - negative or near zero means texture/classic behavior,
  - around `1` means solid tint,
  - values greater than about `1.25` encode material modes.
- `uObjectStyle` supplies a global fallback style mode.

Styles parse `object_material` directives into tint RGB plus `materialMode`, then apply that to matching `GameModel` instances. There is no active renderer material ID on `GameModel`. The visible `materialId` currently found in `SkullbonezGameModel.h` belongs to physics terrain/contact data, not object rendering.

This is the biggest material-system pressure point. The style data already wants named visual materials, but the renderer only receives tint plus one overloaded float.

### Terrain, Water, Sky, And Post

Terrain:

- Uses `lit_textured`.
- Has terrain relief and terrain style uniforms such as `uCinematicTerrain`, `uCinematicBasin`, `uStyleModes`, `uTerrainTint`, `uTerrainAccent`, and `uTerrainGrid`.
- Terrain style logic is embedded directly in the shader.

Water:

- Current runtime uses `water_calm` and `water_ocean`.
- Legacy `water.vert`, `water.frag`, and `water.hlsl` were removed after confirming no current source references.
- Water shaders accept reflection texture slot `t1`/sampler `s1`.
- Calm water carries basin mask and style mode.
- Ocean water carries time, wave height, perturb strength, cinematic glint, and reflection controls.

Sky/post:

- `sky_atmosphere` is a large procedural sky shader with multiple style modes.
- `post_tonemap` carries tonemap, fog, bloom approximation, cloud overlay, god-ray contribution, vignette, saturation, and contrast.
- `post_volumetric_light` carries half-res light shafts and cloud occlusion.

These are powerful but strongly authored. Several visual rules are still hard-coded in shader functions rather than clearly represented as style data.

## Shader Inventory

### Main Used Raster Shaders

| Shader | Current Role | Notes |
|--------|--------------|-------|
| `lit_textured.hlsl` | Terrain and non-instanced lit textured mesh path | Carries terrain relief and terrain style logic. |
| `lit_textured_instanced.hlsl` | Spheres, boxes, pine visuals | Carries object material mode logic and procedural beach-ball override. |
| `unlit_textured.hlsl` | Skybox cube faces | Small, stable. |
| `water_calm.hlsl` | Basin/calm water | Uses reflection texture and style water controls. |
| `water_ocean.hlsl` | Ocean/wave water | Uses animated waves and reflection perturbation. |
| `sky_atmosphere.hlsl` | Fullscreen procedural sky | Large style-mode shader. |
| `post_tonemap.hlsl` | Final cinematic resolve | Large post stack: tonemap, fog, bloom, god rays, grade. |
| `post_volumetric_light.hlsl` | Half-res light shafts | Duplicates some cloud/noise/depth ideas with tonemap and sky. |
| `shadow_depth.hlsl`, `shadow_depth_instanced.hlsl` | Depth-only shadow passes | Small, stable. |
| `collision_visualizer.hlsl` | Physics/collision debug geometry | Intentionally separate from normal materials. |
| `grid_line.hlsl` | Colored line/grid overlay path | DX12 overlay path. |
| `text.hlsl` | Text rendering | Stable UI/text shader. |
| `solid_color.hlsl` | Immediate solid 2D quad | Stable UI primitive. |
| `solid_color_batch.hlsl` | Batched per-vertex color UI quads | Stable UI primitive. |
| `UIBackdropBlur.hlsl` | UI blur texture shader | Renderer-adjacent UI path. |

### Special Or Candidate Legacy Assets

| Shader | Current Role | Cleanup Note |
|--------|--------------|--------------|
| `generate_mips.hlsl` | DX12 compute mip generation | DX12-only compute path; keep separate from raster manifests. |
| `reflect.rt.hlsl` | DXR reflection shader library | DX12-only raytracing path; needs its own manifest family. |
| `reflect.rt.dxil` | Precompiled DXR bytecode | Treat as generated/checked-in artifact with explicit rebuild rule. |
| `water.*` | Legacy/simple water shader | Removed after confirming no current source references; keep `water_calm.*` and `water_ocean.*`. |
| `UITextured.*` | Textured UI shader | Removed after confirming the lowercase `UI_textured.*` family is the active UI path. |

## Main Problems To Fix

### 1. Shader Contracts Are Implicit

Today a pass contract is split across:

- C++ calls to `SetMat4`, `SetVec4`, `SetInt`, etc.
- C++ calls to `BindTexture(handle, slot)`.
- Mesh creation booleans or instanced attribute arrays.
- GLSL `layout(location=...)`.
- HLSL semantics.
- DX12 root signature assumptions.

There is no single place that says:

- this shader requires vertex layout `P3_N3_UV2`,
- this shader reads textures `t0`, `t1`, `t2`,
- this pass must bind `SceneFrame`, `Lighting`, `Material`, or `PostParams`,
- this pass disables depth and blend,
- this pass expects `s0` wrap or `s1` clamp.

The result is manageable now, but fragile as soon as material data or additional textures arrive.

### 2. Material Data Is Overloaded

The renderer currently uses one `vec4` per instance to represent color, texture override, style family, and material mode. This made sense as a fast bridge, but it is no longer expressive enough.

The style files already describe material intent:

- matte,
- metal/chrome,
- neon/emissive,
- glass,
- toon,
- lowpoly,
- foliage,
- bark,
- stone,
- ridge,
- shore,
- pine.

Those should become typed visual material data, not magic values in `tint.a`.

### 3. Style And Material Are Blurred Together

`CinematicRenderConfig` contains:

- pass toggles,
- camera/sun composition,
- sky colors,
- cloud controls,
- volumetric controls,
- bloom controls,
- terrain relief,
- fog,
- art style modes,
- terrain palette,
- water palette,
- basin mask.

That is acceptable as a transitional config, but future code should distinguish:

- frame/pass settings,
- lighting settings,
- post/grade settings,
- terrain material/style settings,
- water material/style settings,
- object material settings.

Keep backward-compatible `cinematic_*` directives, but stop adding every new visual idea directly to one giant config shape.

### 4. Shader Inventory Drift Must Stay Visible

The GLSL and DX11-era duplicate shader families have been removed. The active
risk is now quieter: HLSL files, DXIL artifacts, project entries, and shader
contract metadata can drift apart if a future pass changes only one of them.

HLSL/DXC reflection should produce the canonical shader metadata, with any
future Vulkan/Metal path generated or translated from that contract rather than
maintained as a second handwritten source family.

### 5. Missing Uniforms Are Silent

Silent missing uniform behavior is dangerous during cleanup. It lets old C++ call sites survive, but it also lets a shader typo create a backend-specific no-op.

Recommended approach:

- Keep tolerant behavior in Release.
- Add debug/dev diagnostics that can report:
  - C++ tried to set a uniform that does not exist in shader contract,
  - shader declares a required uniform that the pass did not set,
  - shader expects a texture slot that was not bound before draw.

Do this through explicit pass/shader manifests, not by making every `Set*` call fatal immediately.

### 6. Pass Logic Is Not Owned By Pass Objects

The current `DrawPrimitives()` order is reasonable, but pass setup is not isolated. The same function decides reflection path, sky mode, target binding, model rendering, terrain, water, debug overlays, volumetric, and resolve.

Do not rewrite the pipeline first. But as shader cleanup proceeds, create small pass-binder helpers so each pass has one place that binds its shader inputs.

### 7. Some Shader Assets Need Lifecycle Decisions

Previously likely-unreferenced assets have been removed:

- `water.*`
- `UITextured.*`

Backend-specific assets should be documented rather than treated as accidental omissions:

- `debug_line.*` is GL-only.
- `generate_mips.hlsl` is DX12-only.
- `reflect.rt.hlsl` and `reflect.rt.dxil` are DXR-only.

Do not delete anything until `rg` confirms no source/data reference. Before
committing PR-bound removals, run `tools\validate_dx12_renderer.bat`.

## Target Architecture

### Principle: Small Material System, Not Material Graph

This engine does not need a node editor, shader permutation explosion, or Unreal-style material compiler right now.

Target v1:

- one compact material description,
- named material presets in style/scene data,
- per-object material assignment,
- one object shader family that can render the current material families,
- terrain/water/post style data as separate pass params,
- no per-scene shader forks.

### Proposed CPU Data Types

Names are illustrative. Final names should match local style.

```cpp
enum class RenderMaterialKind : uint8_t
{
    Textured = 0,
    Matte = 1,
    Metal = 2,
    Emissive = 3,
    Glass = 4,
    Toon = 5,
    LowPoly = 6,
    Shadow = 7,
    Foliage = 8,
    Bark = 9,
    Stone = 10,
    Ridge = 11,
    Shore = 12,
    Pine = 13
};

struct RenderMaterial
{
    char name[32];
    RenderMaterialKind kind;

    float baseColor[4];      // rgb + alpha
    float emissiveColor[3];
    float emissiveStrength;

    float roughness;
    float metallic;
    float specular;
    float transmission;      // glass/water-like response, optional

    float stylization;       // toon/low-poly/facet strength
    float textureMode;       // texture, procedural beachball, untextured, etc.
    uint32_t flags;
};
```

For v1, this can live entirely on CPU and be packed into instance attributes. A GPU material table can come later.

### Proposed Instance Data V1

Current instance payload:

```text
mat4 model
vec4 tintAndMode
```

Recommended staged payload:

```text
mat4 model
vec4 material0  // base rgb + alpha or mode
vec4 material1  // roughness, metallic, specular, emissive strength
vec4 material2  // emissive rgb + flags/style
```

This keeps batching simple and avoids DX12 root-signature changes. It is slightly larger per instance, but current `MAX_GAME_MODELS` and batching scale should tolerate it. If perf validation shows it matters, move to material IDs plus a table later.

Compatibility bridge:

- Existing `SetRenderTint(r,g,b,colorOverride)` maps into a default `RenderMaterial`.
- Existing style `object_material <target> <r> <g> <b> <mode>` maps to a named or generated material.
- Existing texture/beachball behavior remains material kind `Textured` with `textureMode = Beachball` or equivalent.

### Proposed Pass Data Model

Separate pass data by lifetime and meaning:

```cpp
struct FrameParams
{
    Matrix4 view;
    Matrix4 projection;
    float nearPlane;
    float farPlane;
    float time;
    float viewportSize[2];
};

struct LightingParams
{
    float lightPosition[4];   // view-space or directional, as today
    float ambient[4];
    float diffuse[4];
    float sunColor[3];
    float sunIntensity;
};

struct TerrainStyleParams
{
    int terrainMode;
    float relief;
    float basinDepth;
    float basinRimLift;
    float tint[3];
    float accent[3];
    float gridScale;
    float gridStrength;
    float basinMask[5];
};

struct WaterStyleParams
{
    int waterMode;
    float tint[4];
    float reflectionStrength;
    float glintStrength;
    float waveHeight;
    float perturbStrength;
    float basinMask[5];
};

struct PostStyleParams
{
    float exposure;
    float gamma;
    float fogColor[3];
    float fogStart;
    float fogEnd;
    float fogDensity;
    float bloomParams[4];
    float grade[4];
};
```

The backend does not need to expose these structs immediately. The first step can be C++ binder functions that call existing `Set*` methods in one place:

```cpp
BindFrameParams(shader, frame);
BindLightingParams(shader, lighting);
BindTerrainStyleParams(shader, terrainStyle);
BindWaterStyleParams(shader, waterStyle);
BindPostStyleParams(shader, postStyle);
```

This gives the code a real architecture without forcing a backend rewrite.

### Proposed Shader Manifest

Add a lightweight manifest per shader family, either as C++ tables or data files. Start with C++ to avoid parser work.

Example:

```cpp
struct ShaderResourceDecl
{
    const char* name;
    int slot;
    ShaderResourceKind kind;  // Texture2D, Sampler, CBuffer
    bool required;
};

struct ShaderUniformDecl
{
    const char* name;
    ShaderValueType type;
    bool required;
};

struct ShaderProgramDesc
{
    const char* baseName;
    VertexLayoutId vertexLayout;
    Span<ShaderUniformDecl> uniforms;
    Span<ShaderResourceDecl> resources;
};
```

Use it for:

- validation logging,
- shader creation diagnostics,
- pass binder assertions,
- eventual shader preload/cache,
- docs generated from source of truth.

Do not block the first material pass on full manifest coverage. Start with high-risk shaders:

1. `lit_textured_instanced`
2. `lit_textured`
3. `water_calm`
4. `water_ocean`
5. `sky_atmosphere`
6. `post_tonemap`
7. `post_volumetric_light`

### Shader Source Strategy

Recommended v1:

- Keep separate GLSL and HLSL files.
- Stop pretending identical comments are enough. Add manifest/static checks.
- Factor repeated logic inside each language if a low-risk include/preprocessor is added later.
- Avoid bringing in Slang, SPIRV-Cross, DXC-for-everything, or a custom shader DSL until the material model is stable.

Possible v2:

- Add a tiny repository-local shader preprocessor with `#include` for GL and DX.
- Introduce `SkullbonezData/shaders/common/` for shared snippets per language:
  - `common_lighting.glsl`
  - `common_lighting.hlsli`
  - `common_material.glsl`
  - `common_material.hlsli`
  - `common_depth.glsl`
  - `common_depth.hlsli`
- The preprocessor should preserve line diagnostics well enough to debug compile errors.

Do not introduce generated shader source unless the team is ready to own the generator.

## Recommended Implementation Phases

### Phase 0: Inventory And Guardrails

Goal:

- Make the current shader set explicit before touching behavior.

Tasks:

1. Add a shader inventory doc or generated report listing:
   - base name,
   - GL files,
   - HLSL files,
   - used by which C++ call sites,
   - texture slots,
   - vertex layout,
   - pass category.
2. Confirm removed legacy assets stay absent:
   - `water.*`,
   - `UITextured.*`.
3. Mark intentional backend-specific assets:
   - `debug_line.*` GL-only,
   - `generate_mips.hlsl` DX12-only,
   - `reflect.rt.hlsl`/`.dxil` DXR-only.
4. Add a short note near `SkullbonezData/shaders` or in `Agentic/Reference` explaining shader naming conventions.
5. Do not move files yet.

Acceptance:

- A future agent can tell whether a shader is active, legacy, or backend-specific without guessing.

2026-06-15 implementation note:

- `Agentic/Reference/shader-inventory.md` now lists the DX12-only shader set and
  high-risk runtime contract summary.
- `SkullbonezSource/SkullbonezShaderContracts.h` is the runtime-facing table for
  the first high-risk shader families.

Validation:

- Documentation-only inventory: no validation required.
- If any shader files are removed or renamed: `tools\validate_dx12_renderer.bat`.

### Phase 1: Shader Contract Diagnostics

Goal:

- Catch stale uniforms and texture-slot mistakes without changing visual output.

Tasks:

1. Add `ShaderProgramDesc` manifests for high-risk shaders.
2. Add optional debug logging for:
   - setting a uniform not declared in the manifest,
   - manifest-required uniform not set by pass binder,
   - required texture slot not bound before draw.
3. Keep existing tolerant behavior by default.
4. Add a config/debug flag for strict shader contract checks if needed.
5. Make DX12 reflection mismatch visible:
   - if manifest says `uFogParams` exists but reflection does not find it, log shader name.

Acceptance:

- Current scenes render the same.
- Missing or stale shader parameters produce actionable logs in dev mode.

2026-06-15 implementation note:

- `ShaderDX12` now looks up high-risk contracts, reports required uniform versus
  HLSL reflection drift, logs stale uniform setters, logs old texture resources
  still being set through uniform APIs, and reports required uniforms not set
  between `Use()` and cbuffer upload.
- Diagnostics are bounded and development-only where they inspect pass
  activation state; release/profile behavior still tolerates missing reflected
  names.

Validation:

- `tools\validate_dx12_renderer.bat`.

### Phase 2: Pass Binder Helpers

Goal:

- Move shader setup into named, testable binding functions before changing material behavior.

Tasks:

1. Introduce lightweight bind helpers:
   - `BindObjectPassParams`
   - `BindTerrainPassParams`
   - `BindWaterPassParams`
   - `BindSkyPassParams`
   - `BindVolumetricPassParams`
   - `BindTonemapPassParams`
2. Keep them near render code initially, or under a small renderer-adjacent file.
3. Replace scattered repeated `Set*` groups with bind helper calls.
4. Do not extract a full `RenderPipeline` class yet unless the changes stay small.
5. Keep `SkullbonezRun::DrawPrimitives()` as the pass scheduler for now.

Acceptance:

- Uniform binding is centralized by pass.
- The render order stays unchanged.
- Future material params have one clear insertion point.

2026-06-15 implementation note:

- Instanced object batch setup now uses a shared primitive batch binder.
- Fullscreen sky, volumetric-light, and tonemap passes now use local binder
  helpers in `SkullbonezRunPasses.cpp`.

Validation:

- `tools\validate_dx12_renderer.bat`.
- If this touches broad `SkullbonezRun*` behavior, prefer `tools\validate_full.bat`.

### Phase 3: Material System V1 CPU Model

Goal:

- Replace overloaded tint/mode semantics with explicit visual materials while preserving existing scene/style syntax.

Tasks:

1. Add `RenderMaterialKind` and `RenderMaterial`.
2. Add a small material preset registry:
   - texture/beachball,
   - matte,
   - metal/chrome,
   - emissive/neon,
   - glass,
   - toon/pixar,
   - lowpoly,
   - shadow/black,
   - foliage,
   - bark,
   - stone,
   - ridge,
   - shore/sand,
   - pine.
3. Add a renderer-facing material field to `GameModel` or a render snapshot layer.
4. Keep physics material IDs separate and do not reuse physics contact `materialId`.
5. Map existing `object_material` directives to the new material registry.
6. Preserve old tint behavior:
   - if no material is assigned, use current texture/tint path.
   - if old numeric mode is supplied, translate it through the registry.
7. Add scene/style diagnostics for unknown material names.

Acceptance:

- Existing styles still work.
- Object material intent is typed in C++.
- No shader visual behavior has to change yet.

2026-06-15 implementation note:

- `RenderMaterialKind` and `RenderMaterial` now exist as backend-neutral CPU
  data.
- `GameModel::SetRenderTint` still supports the compatibility path while
  mirroring tint/mode into `RenderMaterial`.
- Existing `object_material` style/scene directives now create typed render
  materials and apply them through `GameModel::SetRenderMaterial`, which still
  packs the old tint/mode bridge for current shaders.

Validation:

- Parser-only plumbing: `tools\validate_fast.bat`.
- If object rendering output changes in same slice: `tools\validate_dx12_renderer.bat`.

### Phase 4: Material Instance Payload

Goal:

- Send explicit material data to `lit_textured_instanced` without changing DX12 root signatures.

Tasks:

1. Extend the instanced static layout contract carefully.
2. Move from `mat4 + tint4` to either:
   - `mat4 + material0 + material1`, or
   - `mat4 + material0 + material1 + material2`.
3. Update DX12 `BuildInstancedInputLayout`.
4. Update `lit_textured_instanced.hlsl` VS input structs.
5. Keep a compatibility path for old `SetRenderTint` until call sites are migrated.
6. Do not add material texture arrays in this phase.

Acceptance:

- Objects can render at least textured, matte, metal, emissive, glass, toon, lowpoly, foliage/bark/stone through explicit material params.
- Spheres, boxes, and pines still batch.
- Existing non-concept scenes retain their appearance within validation thresholds.

Validation:

- `tools\validate_dx12_renderer.bat`.
- `tools\validate_perf.bat` if instance payload growth or batch preparation shows measurable hot-path cost.

2026-06-16 implementation note:

- The instanced object payload now packs `mat4 + material0 + material1 +
  material2` instead of `mat4 + tint4`.
- Spheres, boxes, and pine batches accept typed `RenderMaterial` data while the
  old tint overloads still translate through `MakeRenderMaterialFromLegacyTint`.
- `lit_textured_instanced.hlsl` consumes the expanded material rows, and
  `shadow_depth_instanced.hlsl` carries the same input layout so shared meshes
  remain compatible.
- The object renderer now reads `GameModel::GetRenderMaterial()` directly and
  keeps the fixed-contact highlight by temporarily overriding the render
  material color.

### Phase 5: Shader Generalization

Goal:

- Remove golden-hour and concept-specific hard-coding from general shaders.

Tasks:

1. Replace "directional light means procedural beach ball" with material selection.
2. Replace hard-coded object style fallback with material kind/style params.
3. Move terrain mode values into named enum documentation shared by parser and shader comments.
4. Parameterize sky/post/cloud assumptions that are currently composition-specific:
   - cloud hero mask placement,
   - lower cloud band weights,
   - vignette floor,
   - saturation/contrast defaults,
   - low-poly sky mode palettes.
5. Parameterize water basin/glint assumptions:
   - basin mask,
   - glint direction/intensity,
   - calm/ocean/wet-floor profile.
6. Keep shader family count low. Split only if a shader becomes unreasonably branch-heavy or needs a different vertex/resource contract.

Acceptance:

- The same shader families can produce golden hour, industrial, neon, fog, low-poly, abstract, and Pixar-inspired variants via data.
- No concept requires a one-off full shader copy.

Validation:

- `tools\validate_dx12_renderer.bat`.
- Manual visual review of representative concept scenes in DX12 first, then
  renderer validation.

2026-06-16 implementation note:

- Object material behavior is now driven by typed material rows and a material
  table sample rather than only by overloaded `tint.a` values.
- The legacy beachball, solid tint, and floor-material modes still resolve
  through a compatibility helper so existing scenes keep their intended look.
- Object-style fallback remains available for old content, but the shader now
  has explicit roughness, metallic, specular, emissive, stylization, and
  material-row inputs for the general material families.

### Phase 6: Shader Source Hygiene

Goal:

- Reduce duplicated GLSL/HLSL maintenance risk.

Tasks:

1. Add a small shader contract checker script:
   - compare manifest uniform/resource declarations to GLSL uniforms and HLSL cbuffers/textures,
   - report missing or extra required names,
   - report mismatched texture slots.
2. Add optional line-count or function-name inventory for high-risk shader pairs.
3. Add a simple include preprocessor only if it does not break error diagnostics.
4. If includes are added, factor common logic by language:
   - lighting helpers,
   - material mode helpers,
   - depth reconstruction,
   - screen UV helpers,
   - cloud/noise helpers.
5. Keep generated artifacts out of the runtime path unless a build step is formalized.

Acceptance:

- A shader contract drift is caught before runtime visual comparison.
- Large shader pairs are easier to review.

Validation:

- Changed tooling: `tools\validate_fast.bat`, then run the changed script directly.
- Shader source changes: `tools\validate_dx12_renderer.bat`.

2026-06-16 implementation note:

- `tools\validate_shaders.py` now parses HLSL cbuffer uniforms and
  `t/u/s` resource declarations, then compares them with
  `tools\shader_contracts.json`.
- High-risk shader contracts now list required uniform names and texture
  resources, including the object material table at `t4`.
- The checker reports missing required uniforms/resources and resource register
  drift before runtime visual comparison.

### Phase 7: Render Pipeline Extraction

Goal:

- Give shader/material passes clear ownership after contracts are stable.

Tasks:

1. Extract pass-level structs/classes incrementally:
   - `ReflectionPass`
   - `SceneOpaquePass`
   - `TerrainPass`
   - `WaterPass`
   - `SkyPass`
   - `VolumetricPass`
   - `TonemapPass`
2. Keep `SkullbonezRun` as coordinator until extraction is complete.
3. Let each pass own:
   - shader handles,
   - pass-specific framebuffers,
   - dynamic full-screen geometry if needed,
   - pass binder call,
   - pass capability checks.
4. Tie pass resources into the existing DX12 device/resource reset sequence.
5. Do not mix this with the first material shader behavior change.

Acceptance:

- `DrawPrimitives()` reads as pass scheduling, not detailed shader setup.
- DX12 resource rebuild remains deterministic.

Validation:

- `tools\validate_dx12_renderer.bat`.
- `tools\validate_full.bat` if `SkullbonezRun*`, scene lifecycle, or window/resource reset behavior changes broadly.

### Phase 8: Optional GPU Material Table

Goal:

- Scale beyond per-instance packed material params if needed.

This branch implements the small material-texture version because the expanded
object material rows need one shared GPU-visible lookup without introducing a
structured-buffer material API.

Possible designs:

1. Small fixed material array in cbuffer:
   - simple for DX12 if array size is small,
   - instance carries material index,
   - limited by cbuffer size and packing complexity.
2. Structured buffer/material buffer:
   - better scaling,
   - requires backend API additions,
   - requires DX12 root signature changes,
3. Material texture:
   - portable enough,
   - stores material rows in a 1D texture,
   - instance carries material row index,
   - awkward but practical if future backends need texture-backed indirection.

2026-06-16 implementation note:

- The ordinary raster ABI now exposes `t4` for a tiny object material table.
- `SkullbonezHelper.cpp` builds a 16x1 RGBA8 material table from the engine
  `RenderMaterialKind` defaults and binds it through `BindTexture(..., 4)`.
- `lit_textured_instanced.hlsl` samples `uMaterialTable` at `t4` with the
  existing point-clamp sampler `s3`.
- This is intentionally a fixed texture-table slice, not bindless descriptors
  or a structured-buffer material system.

Validation:

- `tools\validate_dx12_renderer.bat`.
- DX12 validation log must stay at zero errors.
- `tools\validate_perf.bat` if material lookup changes hot object rendering.

## Specific Cleanup Decisions

### Keep

- Keep HLSL as the canonical production shader source.
- Keep `IShader` name-based setters as a compatibility layer.
- Keep current shader families and make them data-driven.
- Keep collision visualization separate from production materials.
- Keep UI/text shaders simple and out of the material system.

### Change

- Add explicit shader/pass contracts.
- Add material data as typed render concepts.
- Move shader binding into pass-specific helpers.
- Replace overloaded `tint.a` material modes with explicit material params.
- Add diagnostics for missing uniforms/resources.
- Add shader inventory and contract checking from DXC/reflection-backed metadata.
- Add typed CBV upload paths where pass constants are already known as structs.
- Add the small object material table and `t4` binding contract.
- Define shader contracts in engine terms so future Vulkan/Metal mapping is possible without rewriting material/style data.

### Defer

- Full material graph.
- Shader language/toolchain unification through Slang, SPIR-V, or MSL translation until HLSL contracts are stable.
- Bindless textures.
- Descriptor indexing and broad bindless material textures.
- Structured-buffer material tables.
- Shader file physical reorganization.
- Broad render pipeline rewrite.
- Full graph-owned barrier execution; this branch records native resources in
  graph transitions and uses them for diagnostics, but live barriers remain in
  the DX12 backend.

### Avoid

- Do not add one shader per concept scene.
- Do not reintroduce a second handwritten shader language family without a concrete backend plan.
- Do not delete active HLSL/DXIL shader files without source/data search and renderer validation.
- Do not make missing uniforms fatal across the whole engine in the first slice.
- Do not mix material model changes with DX12 descriptor/root-signature changes unless necessary.

## Suggested Work Order

This is the safest commit sequence for another agent:

1. Documentation/inventory only.
2. Shader manifest tables and dev diagnostics, no behavior change.
3. Pass binder helpers, no behavior change.
4. CPU material model and parser/style mapping, compatibility mode.
5. Instance payload expansion and object shader updates.
6. Shader generalization for object materials.
7. Terrain/water/post style parameter cleanup.
8. Shader source hygiene tooling.
9. Pass extraction if the scheduler remains too broad.
10. Material texture table when the object shader needs shared GPU-visible
    defaults.

Each slice should be small enough that renderer validation failures point to a clear cause.

Current completion state on `main`: items 1-6 and 8/10 are implemented for the
object-material path. Terrain, water, and post style parameter cleanup plus
broader pass/resource ownership remain separate future slices.

## Validation Matrix

These commands are targeted pre-commit/PR gates, not as-you-go validation.

| Change Type | Required Validation |
|-------------|---------------------|
| This plan or shader inventory docs only | No validation required |
| Shader manifest diagnostics only | `tools\validate_dx12_renderer.bat` |
| Pass binder refactor with same behavior | `tools\validate_dx12_renderer.bat` |
| Scene/style parser material plumbing only | `tools\validate_fast.bat` |
| Object shader or instance payload change | `tools\validate_dx12_renderer.bat` |
| Terrain/water/post shader change | `tools\validate_dx12_renderer.bat` |
| DX12 root signature, descriptors, barriers, or upload changes | `tools\validate_dx12_renderer.bat`, verify `dx12_validation.txt` is zero, and run three consecutive DX12-heavy checks if barriers/uploads are involved |
| Renderer hot path allocation or batch payload changes | `tools\validate_dx12_renderer.bat` plus `tools\validate_perf.bat` |
| Broad `SkullbonezRun*` render pipeline extraction | `tools\validate_full.bat` |
| Tooling changes under `tools/*` | `tools\validate_fast.bat`, then run the changed script |

## Risk Register

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Shader contract drift | HLSL, DXIL artifacts, project entries, and manifests can drift apart | Add manifests, contract checker, and DX12 renderer validation before committing each shader slice. |
| DX12 root signature churn | Material tables can force descriptor changes | Keep the `t4` material-table expansion small, documented, and covered by renderer validation. |
| Future Vulkan/Metal lockout | DX12 details could leak into scene/material contracts | Keep pass, material, vertex-layout, and shader metadata in engine-owned terms; isolate D3D12-specific binding details in the DX12 device/shader layer. |
| Instance payload growth | Larger per-instance uploads can hurt perf | Measure with `validate_perf` if object batches change. |
| Silent missing uniforms | Current setters hide typos and shader drift | Add dev diagnostics before behavior changes. |
| Style config sprawl | `CinematicRenderConfig` already carries many unrelated settings | Introduce pass/style/material bind structs without breaking existing directives. |
| One-off concept shaders | Fast short-term, bad long-term | Keep shader count low and data-drive modes. |
| Deleting active shader assets | Some shaders are backend-specific or indirectly loaded | Run `rg`, document backend-only assets, validate the renderer suite before committing the deletion. |
| DX12 reset resource bugs | Shaders/resources rebuild after device/resource reset | Tie new pass resources into existing reset sequence and validate full lifecycle behavior. |

## Open Questions For Implementers

1. Should `RenderMaterial` remain on `GameModel`, or should rendering read a separate render snapshot to keep physics/game object data cleaner?
2. Is the current hybrid `material rows + t4 defaults table` good enough for the expected object counts, or should material IDs become the primary instance payload later?
3. Should style files define named material presets directly, or only assign existing presets with tint overrides?
4. Should `CinematicRenderConfig` be renamed/split, or should backward-compatible `RenderStyleConfig` wrap it first?
5. Should shader manifests be C++ tables first, or data files under `SkullbonezData/shaders`?
6. Should the removed legacy `water.*` and `UITextured.*` history be documented in a final cleanup report after pre-commit validation?

## Success Criteria

The cleanup is successful when:

- A new shader pass has an explicit contract before it is used.
- Material names in style files map to typed render materials, not magic floats.
- Existing DX12 scenes retain their intended appearance under screenshot validation.
- Material look changes happen in data and compact shader modes, not shader file forks.
- Missing shader inputs are visible during development.
- The DX12 root signature is changed only for a clear resource-model reason.
- The object material table binding is explicit in code, shader contracts, and
  reference docs.
- Shader/material contracts are DX12-canonical but not hard-wired to D3D12 scene data, leaving a future Vulkan/Metal mapping path open.

## Final Recommendation

Do the cleanup in two tracks:

1. Contract cleanup: shader inventory, manifests, diagnostics, pass binders.
2. Material cleanup: typed CPU materials, compatible style mapping, packed instance material params.

These two tracks reinforce each other. The contract work makes shader changes safer; the material work gives the shader contracts a real reason to exist. Start there before considering a bigger render pipeline rewrite or shader language unification.

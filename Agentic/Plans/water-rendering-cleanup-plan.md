# Water Rendering Cleanup Plan

Status: planning draft  
Created: 2026-06-11  
Scope: water shaders, reflection modes, water material/style data, known water rendering bugs  
Implementation status: plan only, no code changes in this pass

## Goal

Clean up the water rendering architecture so calm water, ocean water, FBO reflections, DXR reflections, cinematic/style water modes, and future water material controls have a clear owner and validation path.

## Current Read

Current water pieces:

- `WorldEnvironment::RenderFluid` renders water.
- Runtime decides whether water is hidden, flat, no-reflect, DXR reflect, or FBO reflect.
- Reflection pass is currently orchestrated in `SkullbonezRun::DrawPrimitives`.
- Current runtime uses:
  - `water_calm.*`,
  - `water_ocean.*`.
- Legacy `water.*` appears unreferenced by current source and should be audited.
- DXR reflection produces a texture that water samples like other reflection textures.
- The known bug list includes: water renders through back faces of spheres when intersecting the water surface.

## Problems

### 1. Reflection Ownership Is Split

Reflection rendering is scheduled in the main frame function. Water consumes the reflection result later. This is logical, but the contract is not explicit.

Needed explicit output:

```cpp
struct WaterReflectionInput
{
    uint32_t textureHandle;
    Matrix4 sampleVP;
    bool isDXR;
    bool valid;
};
```

### 2. Water Modes Are Spread Across Runtime, Style, And Shaders

Water has several meanings:

- hidden/off,
- flat debug,
- no reflection debug,
- calm full plane,
- basin pool,
- stylized basin,
- ocean,
- wet floor future mode,
- DXR reflection vs FBO reflection.

These should be represented as typed water render settings.

### 3. Water Material Is Not A Material

Current water style is uniform fields:

- tint,
- alpha,
- reflection strength,
- glint strength,
- basin mask,
- mode.

This is okay for water v1, but it should be named as `WaterStyleParams` rather than being spread through `CinematicRenderConfig` call sites.

### 4. Legacy Shader Files Need Decision

`water.vert`, `water.frag`, and `water.hlsl` appear to be old/simple water shaders. Do not delete until:

- source search confirms no references,
- data/search confirms no indirect references,
- renderer validation passes after removal.

## Target Shape

### Water Pass

Add a `WaterPass` or `RenderWaterPass` facade.

Responsibilities:

- choose water mode,
- bind calm/ocean shader,
- bind reflection texture,
- bind water style params,
- render calm/ocean meshes,
- own water-specific state restore,
- expose debug info.

Inputs:

```cpp
struct WaterPassInput
{
    Matrix4 view;
    Matrix4 projection;
    Matrix4 reflectionSampleVP;
    uint32_t reflectionTexture;
    float time;
    float waterY;
    bool flatWater;
    bool noReflect;
    WaterStyleParams style;
};
```

### Water Style Params

```cpp
enum class WaterMode
{
    Off,
    Basin,
    CalmPlane,
    Ocean,
    WetFloor,
    StylizedBasin
};

struct WaterStyleParams
{
    WaterMode mode;
    float tintR;
    float tintG;
    float tintB;
    float alpha;
    float reflectionStrength;
    float glintStrength;
    float waveHeight;
    float perturbStrength;
    float basinCenterX;
    float basinCenterZ;
    float basinRadiusX;
    float basinRadiusZ;
    float basinFeather;
};
```

Keep `cinematic_water_*` directives compatible, but bind them through `WaterStyleParams`.

## Known Bug Direction

Bug: water renders through back faces of spheres when intersecting water surface.

Likely causes to investigate:

- transparent water draw order,
- water depth write/test settings,
- reflection sample showing underwater/back-face content,
- clipping only applied in reflection pass, not main pass,
- object/water intersection with alpha blending and no sorting,
- missing depth pre-pass or water depth behavior.

Potential fixes:

1. Keep water depth test on but depth write off.
2. Ensure water draws after opaque objects and terrain.
3. Avoid showing reflection contribution where geometry is in front of water.
4. Use scene depth in cinematic water if available.
5. Add intersection fade/foam mask in water shader.
6. In non-cinematic path, keep simple and stable unless baselines require change.

Do not fix this during architecture extraction unless explicitly scoped. It affects render baselines.

## Phase Plan

### Phase 1: Water Shader Inventory

Tasks:

1. Confirm active shader files.
2. Confirm legacy `water.*` references.
3. Document active/legacy/backend-specific water assets.

Validation:

- Documentation only: no validation required.

### Phase 2: Water Reflection Contract

Tasks:

1. Introduce `WaterReflectionInput` or equivalent.
2. Make reflection pass output explicit.
3. Pass reflection texture and sample VP into water pass.
4. No visual behavior change.

Validation:

- `tools\validate_renderers.bat`.

### Phase 3: Water Style Params Binder

Tasks:

1. Add `WaterStyleParams` helper.
2. Bind calm/ocean water uniforms through one function.
3. Keep existing uniform names for now.
4. Keep existing behavior.

Validation:

- `tools\validate_renderers.bat`.

### Phase 4: Water Pass Extraction

Tasks:

1. Move water mode decision and draw calls into `WaterPass`.
2. Keep `WorldEnvironment` as mesh/resource owner until resource plan catches up.
3. Return debug info if useful.

Validation:

- `tools\validate_renderers.bat`.

### Phase 5: Legacy Shader Cleanup

Tasks:

1. Remove or archive unreferenced `water.*` only after confirming references.
2. Update shader inventory.

Validation:

- `tools\validate_renderers.bat`.

### Phase 6: Water Bug Investigation

Tasks:

1. Create a focused scene for sphere/water intersection.
2. Capture GL/DX11/DX12 screenshots.
3. Check depth/blend states around water pass.
4. Test candidate fix behind a narrow change.

Validation:

- `tools\validate_renderers.bat`.
- Add or update visual baseline only with explicit intent.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs/inventory only | No validation required |
| Reflection contract refactor | `tools\validate_renderers.bat` |
| Water uniform binder | `tools\validate_renderers.bat` |
| Water pass extraction | `tools\validate_renderers.bat` |
| Shader removal | `tools\validate_renderers.bat` |
| Water visual bug fix | `tools\validate_renderers.bat` |
| DXR reflection changes | `tools\validate_renderers.bat`, DX12 validation log zero |

## Risks

| Risk | Mitigation |
|------|------------|
| Reflection sample matrix changes | Keep FBO and DXR sample VP explicit and covered by renderer validation. |
| Water blending differs by backend | Validate GL/DX11/DX12 together after state changes. |
| Legacy shader removal breaks hidden path | Search all source/data and validate before committing the removal. |
| Water bug fix shifts baselines | Use focused scene and intentional baseline update only if approved. |
| DX12 FBO/depth transitions regress | Keep barriers explicit and inspect validation log. |

## Success Criteria

- Water pass has explicit inputs and outputs.
- Water style params are bound from one place.
- Legacy water shader files have a documented status.
- Reflection source selection is clear.
- Known water intersection bug has a focused investigation path.

# Water Rendering Cleanup Plan

Status: implementation review
Created: 2026-06-11
Scope: water shaders, reflection modes, water material/style data, known water rendering bugs
Implementation status: phases 2-5 are partially implemented on `codex/post-pr73-roadmap`; legacy `water.*` shader files remain removed; phase 6 includes the first water-depth/blend-state mitigation, exact render-state restore, and intentional DX12 baseline refresh.

Retirement note: code-heavy water cleanup should use the DX12-only renderer
validation gate. Do not reintroduce OpenGL or DX11 water paths.

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
- Legacy `water.*` was removed after source/data reference checks; `water_calm.*` and `water_ocean.*` remain active.
- DXR reflection produces a texture that water samples like other reflection textures.
- The known bug list includes: water renders through back faces of spheres when intersecting the water surface.

## Problems

### 1. Reflection Ownership Is Split

Reflection rendering is scheduled in the main frame function. Water consumes the reflection result later. This is logical, but the contract is not explicit.

Implemented explicit output:

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

### 4. Legacy Shader Files Removed

`water.vert`, `water.frag`, and `water.hlsl` were old/simple water shaders. Cleanup confirmed no current source or data references, then removed the tracked files and project entries. Renderer validation remains a PR-gate requirement after the cleanup is committed.

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

1. Introduced `WaterReflectionInput`.
2. Made reflection texture/sample VP/no-reflect state explicit at the water call boundary.
3. Passed reflection input through `WaterPass`.
4. Visual behavior changed later in phase 6 when water depth-write behavior was adjusted.

Validation:

- Covered by `tools\validate_full.bat` on `codex/post-pr73-roadmap`.

### Phase 3: Water Style Params Binder

Tasks:

1. Added `WaterStyleParams`.
2. Bound calm/ocean water uniforms through shared helpers.
3. Kept existing shader uniform names.
4. Preserved style inputs apart from the phase 6 water-depth visual change.

Validation:

- Covered by `tools\validate_full.bat` on `codex/post-pr73-roadmap`.

### Phase 4: Water Pass Extraction

Tasks:

1. `WaterPass` now owns water draw ordering and state setup around `WorldEnvironment::RenderFluid`.
2. `WorldEnvironment` remains mesh/resource owner.
3. Debug info was not added.
4. Water render-state restore now uses exact depth-write and blend-function queries instead of depth-test/blend-enable proxies.

Validation:

- Covered by `tools\validate_full.bat` on `codex/post-pr73-roadmap`.

### Phase 5: Legacy Shader Cleanup

Tasks:

1. Keep legacy `water.*` absent unless a new water family deliberately reintroduces it.
2. Keep shader inventory aligned with `water_calm.*` and `water_ocean.*`.

Validation:

- `tools\validate_dx12_renderer.bat`.

### Phase 6: Water Bug Investigation

Tasks:

1. Reused existing `water_ball_test` and `solver_smoke` DX12 captures.
2. Captured DX12 screenshots and compared against committed baselines.
3. Changed water to depth-test while depth-write is disabled.
4. Intentionally refreshed affected DX12 baselines after visual inspection.
5. Restored depth-write and blend-function state from their real previous values, not from depth-test/blend-enable proxies.

Validation:

- `tools\validate_full.bat` passed after the intentional baseline update.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs/inventory only | No validation required |
| Reflection contract refactor | `tools\validate_dx12_renderer.bat` |
| Water uniform binder | `tools\validate_dx12_renderer.bat` |
| Water pass extraction | `tools\validate_dx12_renderer.bat` |
| Shader removal | `tools\validate_dx12_renderer.bat` |
| Water visual bug fix | `tools\validate_dx12_renderer.bat` |
| DXR reflection changes | `tools\validate_dx12_renderer.bat`, DX12 validation log zero |

## Risks

| Risk | Mitigation |
|------|------------|
| Reflection sample matrix changes | Keep FBO and DXR sample VP explicit and covered by renderer validation. |
| Water blending/depth order drifts | Validate DX12 focused scenes and inspect baseline diffs after state changes. |
| Legacy shader removal breaks hidden path | Search all source/data and validate before committing the removal. |
| Water bug fix shifts baselines | Use focused scene and intentional baseline update only if approved. |
| DX12 FBO/depth transitions regress | Keep barriers explicit and inspect validation log. |

## Success Criteria

- Water pass has explicit reflection/style inputs.
- Water style params are bound from one place.
- Legacy water shader files have a documented status.
- Reflection source selection is clear.
- Known water intersection bug has DX12 baseline coverage and exact water render-state restore.

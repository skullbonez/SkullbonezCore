# Post And Cinematic Stack Plan

Status: planning draft  
Created: 2026-06-11  
Scope: HDR scene target, sky, volumetric light, tonemap, fog, bloom, grade, cinematic/style naming  
Implementation status: plan only, no code changes in this pass

## Goal

Turn the current cinematic renderer into an explicit, data-driven post-processing stack while preserving existing `cinematic_*` compatibility.

The current stack is valuable. The cleanup is about naming, pass ownership, shader contracts, and style data separation.

## Current Read

Current cinematic resources:

- HDR scene FBO, RGBA16F.
- Half-res volumetric light FBO, RGBA16F.
- `sky_atmosphere` fullscreen shader.
- `post_volumetric_light` fullscreen shader.
- `post_tonemap` fullscreen shader.
- Dynamic fullscreen quad VB.

Current behavior:

- Cinematic mode renders world into HDR scene target.
- Sky can be procedural.
- Volumetric pass samples scene color/depth to create light texture.
- Tonemap pass samples scene color/depth/volumetric texture and applies fog, bloom approximation, god rays, cloud overlay, grade, exposure, and gamma.

Current problem:

- "Cinematic" now means a general render style/post stack.
- Shader uniforms are large ad hoc groups.
- Sky/post/volumetric duplicate cloud and sun concepts.
- Some look choices are hard-coded in shaders.
- Pass ownership still lives mostly in runtime functions.

## Naming Direction

Keep public compatibility:

- `cinematic_rendering`
- `cinematic_*`
- Cine UI labels if existing UX expects them.

Internally, move toward:

- `RenderStyleConfig`
- `PostStyleParams`
- `SkyStyleParams`
- `VolumetricStyleParams`
- `ToneMapPass`
- `SceneColorPass`

Do not rename everything in one pass. Add wrappers and aliases.

## Target Stack

```text
SceneColorTarget (HDR color + shader-readable depth)
  SkyPass
  ObjectPass
  TerrainPass
  ShadowPass
  WaterPass

VolumetricLightPass
  input: scene color, scene depth
  output: half-res light texture

ToneMapPass
  input: scene color, scene depth, volumetric light
  output: backbuffer
```

Optional future passes:

- bloom prefilter/downsample/blur chain,
- color grading LUT,
- screen-space fog separate from tonemap,
- temporal accumulation for volumetric light,
- debug pass thumbnails.

Do not add these before the current stack is explicit.

## Data Model

### Sky Style Params

```cpp
struct SkyStyleParams
{
    int skyMode;
    bool cloudsEnabled;
    float sunScreenX;
    float sunScreenY;
    float sunIntensity;
    float skyGlowStrength;
    float sunColor[3];
    float horizonColor[3];
    float zenithColor[3];
    float cloudCoverage;
    float cloudSoftness;
    float cloudScale;
    float cloudIntensity;
};
```

### Volumetric Style Params

```cpp
struct VolumetricStyleParams
{
    bool enabled;
    bool godRaysEnabled;
    float sunScreenX;
    float sunScreenY;
    float sunShaftStrength;
    float sunShaftFalloff;
    float volumetricStrength;
    float volumetricDensity;
    float volumetricDecay;
    float fogDensity;
    float sunColor[3];
    float cloudParams[4];
};
```

### Post Style Params

```cpp
struct PostStyleParams
{
    float exposure;
    float gamma;
    bool fogEnabled;
    bool bloomEnabled;
    float fogColor[3];
    float fogStart;
    float fogEnd;
    float fogDensity;
    float fogMaxOpacity;
    float bloomThreshold;
    float bloomKnee;
    float bloomStrength;
    float bloomRadius;
    float saturation;
    float contrast;
    float vignette;
};
```

## Shader Contract Cleanup

High-risk uniforms:

- `uDepthParams`
- `uFogParams`
- `uSunShaftParams`
- `uBloomParams`
- `uCloudParams`
- `uStyleGrade`
- `uVolumetricCompositeStrength`

Plan:

1. Keep uniform names initially.
2. Bind them through `BindPostStyleParams`, `BindSkyStyleParams`, and `BindVolumetricStyleParams`.
3. Add shader manifest entries.
4. Only then consider renaming uniforms for clarity.

## Depth Reconstruction

Depth behavior is cross-renderer sensitive:

- GL and DX use different clip-space depth conventions.
- `Gfx().UsesZeroToOneDepth()` exists for backend convention.
- Post shaders reconstruct linear depth from sampled depth and near/far params.

Rules:

- Keep depth reconstruction helper identical across GL/HLSL.
- Add shader contract tests/checks around depth params.
- Avoid backend-specific constants hidden in shader math.
- Validate fog and volumetric scenes across GL/DX11/DX12.

## Bloom Direction

Current bloom is approximated inside tonemap by sampling nearby scene pixels.

V1 cleanup:

- Keep current approximation.
- Move bloom params into `PostStyleParams`.
- Make mode/strength data-driven.

Future:

- Add real bloom chain:
  - prefilter,
  - downsample,
  - blur,
  - upsample/composite.

Do not add real bloom before pass ownership and target management are clean.

## Cloud/Noise Duplication

Sky, volumetric, and tonemap all contain cloud/noise ideas.

Short-term:

- Keep duplication.
- Use shared CPU params.
- Add notes in shader comments that cloud params must stay aligned.

Medium-term:

- Add common GLSL/HLSL include snippets if shader source hygiene work is ready.

## Phase Plan

### Phase 1: Data Wrappers

Tasks:

1. Add helper functions to build sky/volumetric/post params from current config.
2. Keep `CinematicRenderConfig` fields unchanged.
3. Bind through params.

Validation:

- `tools\validate_renderers.bat`.

### Phase 2: Pass Binders

Tasks:

1. Add `BindSkyStyleParams`.
2. Add `BindVolumetricStyleParams`.
3. Add `BindPostStyleParams`.
4. Keep uniform names unchanged.

Validation:

- `tools\validate_renderers.bat`.

### Phase 3: Pass Extraction

Tasks:

1. Extract sky fullscreen pass.
2. Extract volumetric pass.
3. Extract tonemap pass.
4. Centralize fullscreen quad resource.

Validation:

- `tools\validate_renderers.bat`.
- DX12 validation log zero.

### Phase 4: Style Generalization

Tasks:

1. Add clearer internal style names.
2. Keep public `cinematic_*` compatibility.
3. Move hard-coded shader constants into data where needed for concept scenes.

Validation:

- `tools\validate_renderers.bat`.

### Phase 5: Optional Bloom Chain

Tasks:

1. Add target manager for bloom mips.
2. Add downsample/upsample shaders.
3. Keep GL/DX11/DX12 parity.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat`.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs only | No validation required |
| Param wrapper only | `tools\validate_renderers.bat` |
| Shader bind helper extraction | `tools\validate_renderers.bat` |
| Post/volumetric pass extraction | `tools\validate_renderers.bat` |
| Depth reconstruction change | `tools\validate_renderers.bat` |
| New bloom chain | `tools\validate_renderers.bat` plus `tools\validate_perf.bat` |
| DX12 FBO/resource transitions | `tools\validate_renderers.bat`, DX12 log zero |

## Risks

| Risk | Mitigation |
|------|------------|
| Whole-image baseline changes | Keep first phases behavior-preserving and validate frequently. |
| Depth convention drift | Centralize depth reconstruction and test GL/DX outputs. |
| Volumetric pass samples stale target | Make FBO bind/unbind and transitions pass-owned. |
| Bloom cost grows | Validate perf after any real bloom chain. |
| Naming churn breaks scenes | Keep `cinematic_*` directives as compatibility aliases. |

## Success Criteria

- Cinematic/post stack is explicit pass data, not scattered uniform calls.
- Existing cinematic scenes still render.
- Sky, volumetric, and tonemap params are clear and shared.
- Future styles can vary post look without shader forks.

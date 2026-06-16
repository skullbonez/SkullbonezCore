# Real Shadow System Plan

Status: planning draft
Created: 2026-06-11
Scope: real-time directional shadows for GL, DX11, and DX12
Implementation status: plan only, no code changes in this pass

## Goal

Replace the current fake/projected shadow look with a real shadow-map system where shadows are derived from:

- the cinematic sun/light direction,
- actual object silhouettes,
- actual object positions and orientation,
- terrain depth and terrain normal/orientation,
- renderer-consistent projection math across GL, DX11, and DX12.

The target is not merely "dark blobs under things." The target is believable directional shadowing: trees, crates, rocks, beach balls, and terrain features should cast shadows in the same direction, with contact near the caster and projected shape changing naturally over slopes.

## Current Read

The repo already has a shadow pass, but it is not a real light-space projection system.

Current pieces:

- `SkullbonezData/shaders/shadow.vert`
- `SkullbonezData/shaders/shadow.frag`
- `SkullbonezData/shaders/shadow.hlsl`
- `SkullbonezRun::DrawPrimitives()` renders `Frame/Render/Shadows` after terrain and before water.
- `GameModelCollection::RenderShadows(...)` renders instanced circular decals beneath objects.
- Recent hero-scene terrain grounding also includes shader-side hard-coded/contact-style darkening in `lit_textured.*`.

This is useful for cheap grounding, but it cannot produce realistic shadows because:

- shadow direction is not derived from the sun direction,
- object silhouettes are not rendered from the light,
- terrain slope does not affect shadow projection,
- tree/box/ball shapes all collapse into authored discs or blobs,
- the terrain cannot cast onto itself,
- results are scene-authored rather than physically tied to geometry.

## Definition Of Success

For the showcase scene:

- beach balls cast round/striped-silhouette shadows that extend away from the sun,
- crates cast box-shaped shadows with visible orientation,
- trees cast trunk/canopy silhouettes that agree with the sun angle,
- rocks cast short grounded shadows instead of fake blobs,
- terrain receives shadows consistently across hills and the pond basin,
- GL, DX11, and DX12 outputs remain visually equivalent,
- DX12 validation reports zero errors.

For the renderer:

- the old disc-shadow path remains available as a fallback/debug mode,
- real shadows can be enabled/disabled from scene/style config,
- shadow map resolution, softness, bias, and cascade mode are configurable,
- validation artifacts make shadow regressions easy to diagnose.

## Non-Goals

Do not start with:

- ray traced shadows,
- fully dynamic area lights,
- contact-hardening PCSS as the first milestone,
- every light in the scene casting shadows,
- a deferred renderer conversion,
- a broad frame graph rewrite,
- perfect foliage translucency,
- water caustics or transparent shadowing.

Those can come later. The first real system should be one high-quality directional sun shadow map with clean renderer parity.

## Target Architecture

Add a real `ShadowMapPass` before the main world draw.

Frame order target:

1. Build frame camera and cinematic sun/light data.
2. Compute directional-light view/projection matrix.
3. Render shadow casters into a depth texture from the light view.
4. Render reflection pass, using shadows only if the reflection path can sample them correctly.
5. Render sky.
6. Render objects, sampling shadow map.
7. Render terrain, sampling shadow map.
8. Render optional legacy/contact shadow pass only when enabled.
9. Render water.
10. Render volumetrics/tonemap/UI.

The real shadow map should become the primary grounding mechanism. The current disc pass should either be disabled when real shadows are enabled or kept as a very subtle contact-only supplement.

## Core Data Model

Introduce a renderer-neutral shadow frame payload:

```cpp
struct ShadowRenderConfig
{
    bool enabled = false;
    int mapSize = 2048;
    int pcfRadius = 1;
    float depthBias = 0.0015f;
    float slopeBias = 0.0035f;
    float normalBias = 0.20f;
    float strength = 0.55f;
    float softness = 1.0f;
    float maxDistance = 1400.0f;
    bool terrainCasts = true;
    bool objectsCast = true;
    bool objectsReceive = true;
    bool terrainReceives = true;
};

struct ShadowFrameData
{
    Matrix4 lightView;
    Matrix4 lightProjection;
    Matrix4 lightViewProjection;
    Matrix4 shadowTextureMatrix;
    Vector3 lightDirectionWorld;
    uint32_t shadowDepthTexture = 0;
    int mapSize = 0;
    bool valid = false;
};
```

Scene/style controls:

```text
cinematic_shadows on
cinematic_shadow_map_size 2048
cinematic_shadow_strength 0.55
cinematic_shadow_softness 1.0
cinematic_shadow_depth_bias 0.0015
cinematic_shadow_slope_bias 0.0035
cinematic_shadow_normal_bias 0.20
cinematic_shadow_max_distance 1400.0
```

Keep defaults conservative. Real shadows should not silently appear in old regression scenes unless explicitly enabled or gated by cinematic mode.

## Light-Space Camera

For v1, use one orthographic directional shadow map.

Inputs:

- main camera view/projection,
- cinematic sun direction,
- terrain bounds or scene bounds,
- optional hero-scene focus point.

Algorithm:

1. Derive `lightDirectionWorld` from the existing cinematic sun/light vector.
2. Build a light view matrix looking from `focus - lightDir * distance` toward `focus`.
3. Fit an orthographic projection around the active camera frustum or authored scene bounds.
4. Stabilize the projection by snapping the light-space origin to shadow texel increments.
5. Use backend-specific depth convention:
   - GL: clip depth `[-1, 1]`
   - DX11/DX12: clip depth `[0, 1]`
6. Produce `shadowTextureMatrix` that maps world position to shadow texture UV/depth.

Important: shadow stabilization is not optional. Without texel snapping, camera movement will make shadows shimmer.

## Terrain Orientation Requirement

Terrain must receive shadows using world position and terrain normal.

The terrain shader should:

- transform each fragment world position into shadow map space,
- compare receiver depth against shadow map depth,
- use terrain normal and light direction for slope-aware bias,
- reduce acne on faces pointed away from the light,
- preserve low-poly facet readability.

If visual terrain displacement exists in the vertex shader, the shadow caster version must apply the same displacement. Otherwise shadows will detach around the pond basin and any stylized terrain relief.

## Caster Set

V1 casters:

- dynamic balls,
- dynamic boxes,
- fixed boxes such as trunks, leaves, crates,
- terrain mesh if `terrainCasts` is enabled.

V1 non-casters:

- sky,
- water,
- debug overlays,
- UI/text,
- transparent water/reflection surfaces,
- optional very small decorative pebbles if performance requires pruning.

Caster culling:

- frustum cull against light frustum where feasible,
- skip objects below a tiny projected-screen threshold only after visual testing,
- do not skip showcase trees or crates.

## Backend Work

### Renderer Interface

Extend `IRenderBackend` with shadow-depth resource support.

Possible minimal API:

```cpp
struct ShadowMapHandle
{
    uint32_t id = 0;
    int width = 0;
    int height = 0;
};

virtual ShadowMapHandle CreateShadowMap(int width, int height) = 0;
virtual void DestroyShadowMap(ShadowMapHandle handle) = 0;
virtual void BindShadowMapForWriting(ShadowMapHandle handle) = 0;
virtual void BindShadowMapForSampling(ShadowMapHandle handle, int slot) = 0;
virtual void UnbindShadowMap() = 0;
```

If adding a typed handle is too much for v1, use a backend-owned `ShadowMapResource` class similar to framebuffer ownership. Do not overload ordinary color framebuffer behavior unless it can represent depth-only targets cleanly on all three renderers.

### OpenGL

Implementation:

- create `GL_DEPTH_COMPONENT24` or `GL_DEPTH_COMPONENT32F` texture,
- attach to FBO depth attachment,
- set no color attachment,
- set clamp-to-border with white border color,
- use nearest/linear depending on manual PCF plan,
- render depth-only pass with color writes disabled,
- sample via `sampler2DShadow` or manual depth sampling.

Recommendation:

- start with manual depth sampling plus explicit PCF for parity with HLSL.
- avoid hardware compare samplers until GL/DX parity is proven.

### DX11

Implementation:

- create typeless depth texture, for example `DXGI_FORMAT_R24G8_TYPELESS`,
- create DSV as `DXGI_FORMAT_D24_UNORM_S8_UINT` or depth-only equivalent,
- create SRV as `DXGI_FORMAT_R24_UNORM_X8_TYPELESS`,
- bind DSV without RTVs during shadow pass,
- unbind SRV before writing and unbind DSV before sampling,
- use comparison or manual sampling consistently with GL/DX12.

### DX12

Implementation:

- create depth texture resource with DSV and SRV descriptors,
- add resource states:
  - `D3D12_RESOURCE_STATE_DEPTH_WRITE`
  - `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`
- add correct barriers before/after shadow pass,
- ensure descriptor heap indexing is stable,
- add shadow sampler/root descriptor table to affected PSOs/root signatures,
- verify DX12 InfoQueue remains clean.

DX12 is the highest-risk backend because root signatures, descriptor lifetime, and resource state transitions must all agree.

## Shader Work

Add depth-only caster shaders:

- `shadow_depth.vert`
- `shadow_depth.frag` or no-op fragment for GL if needed
- `shadow_depth.hlsl`
- instanced variant if the existing instanced mesh path cannot share one shader

Receiver shader changes:

- `lit_textured.vert`
- `lit_textured.frag`
- `lit_textured_instanced.vert`
- `lit_textured_instanced.frag`
- HLSL equivalents
- terrain shader path if terrain uses separate setup through `lit_textured`

Receiver uniforms/constants:

```glsl
uniform mat4 uShadowViewProj;
uniform sampler2D uShadowMap;
uniform vec4 uShadowParams; // strength, bias, slopeBias, texelSize
uniform vec4 uShadowFlags;  // enabled, receive, pcfRadius, reserved
```

Sampling:

```text
world position -> shadow clip -> shadow uv/depth
if uv outside [0,1], shadow = 1.0
compare receiver depth - bias against sampled caster depth
PCF average around uv
shadow visibility darkens direct diffuse/specular only, not all ambient
```

Important lighting rule:

- Shadows should reduce direct sun contribution.
- Shadows should not make ambient/fog/sky fill disappear.

This keeps shaded areas readable and avoids ugly black cutouts.

## Bias Strategy

Implement three bias terms:

- constant depth bias for baseline acne prevention,
- slope-scaled bias based on `1 - dot(normal, lightDir)`,
- normal offset bias for receiver world position before projection.

Acceptance criteria:

- no obvious terrain acne,
- no floating peter-pan gap under crates/balls,
- tree trunks do not detach from their own shadows,
- steep terrain facets do not flicker between lit and shadowed.

Bias must be shared in concept across GL/HLSL, even if clip-depth math differs internally.

## Filtering And Quality

V1:

- 2048 or 4096 square shadow map,
- manual 3x3 PCF,
- stable texel snapping,
- one cascade.

V2:

- 2 or 3 cascades for large scenes,
- per-cascade debug visualization,
- 5x5 PCF option,
- receiver-plane depth bias if needed.

V3:

- PCSS/contact-hardening for close hero shots,
- temporal stabilization if camera movement exposes shimmer,
- per-material shadow receive strength.

For the current investor scene, a stable 4096 single directional map may be enough because the camera is locked and the visible world is compact.

## Integration Points

Likely code areas:

- `SkullbonezIRenderBackend.h`
- `SkullbonezRenderBackendGL.*`
- `SkullbonezRenderBackendDX11.*`
- `SkullbonezRenderBackendDX12.*`
- `SkullbonezRunRender.cpp`
- `SkullbonezGameModelCollection.*`
- `SkullbonezTerrain.*`
- `SkullbonezWorldEnvironment.*` only if water/reflection needs shadow awareness
- `SkullbonezTestScene.*`
- `SkullbonezTestSceneParser.cpp`
- `SkullbonezConfig.*`
- `SkullbonezData/shaders/*`
- `tools/check_parity.py` or renderer validation artifacts if shadow-specific diff output is added

Coordinate with:

- `Agentic/Plans/Done/render-pipeline-extraction-plan.md`
- `Agentic/Plans/renderer-parity-debugging-plan.md`
- `Agentic/Plans/Done/render-resource-lifetime-plan.md`
- `Agentic/Plans/Done/shader-architecture-cleanup-plan.md`

This can be implemented before full render-pipeline extraction, but named-pass cleanup would make it safer.

## Phase Plan

### Phase 0: Baseline And Test Scenes

Tasks:

1. Capture current fake-shadow output for:
   - `water_ball_test`
   - `solver_smoke`
   - low-poly showcase scene
2. Add a focused shadow test scene:
   - sloped terrain,
   - one ball,
   - one rotated box,
   - one tree-like stack,
   - fixed camera,
   - fixed sun direction.
3. Document expected visual behavior in the scene comments.

Validation:

```bat
tools\validate_renderers.bat
```

### Phase 1: Backend Shadow Resource

Tasks:

1. Add renderer-neutral shadow map resource creation.
2. Implement GL depth FBO.
3. Implement DX11 depth texture DSV/SRV.
4. Implement DX12 depth resource DSV/SRV/barriers.
5. Add a debug clear/readback path if cheap.

Validation:

```bat
tools\validate_fast.bat
```

Then run a tiny shadow-resource smoke scene once per renderer.

### Phase 2: Depth-Only Caster Pass

Tasks:

1. Compute light view/projection matrix.
2. Add `ShadowMapPass` before world rendering.
3. Render balls, boxes, tree parts, and terrain into depth.
4. Ensure fixed objects and dynamic objects share caster path.
5. Add GPU timer marker `Frame/Render/ShadowMap`.
6. Keep old shadow discs available behind config.

Validation:

```bat
tools\validate_renderers.bat
```

Expected output may not visually change until receiver sampling lands.

### Phase 3: Terrain Receives Real Shadows

Tasks:

1. Pass shadow matrix and texture to terrain shader.
2. Sample shadow map in terrain fragment shader.
3. Apply shadow only to direct sun contribution.
4. Add constant/slope/normal bias.
5. Tune PCF for stable terrain shadows.

Acceptance:

- crates/balls/trees visibly cast along sun direction onto terrain,
- slope facets receive continuous shadows,
- no obvious acne in the hero scene.

Validation:

```bat
tools\validate_renderers.bat
```

### Phase 4: Objects Receive Real Shadows

Tasks:

1. Add shadow sampling to normal and instanced object shaders.
2. Ensure beach balls shade believably without losing their red/yellow identity.
3. Ensure boxes and tree trunks receive shadows from nearby objects.
4. Decide whether tiny rocks receive shadows or rely on ambient only.

Validation:

```bat
tools\validate_renderers.bat
```

### Phase 5: Quality, Stability, And Debug Views

Tasks:

1. Add debug view:
   - shadow map preview,
   - light frustum overlay,
   - cascade bounds later.
2. Add scene/style controls for map size, strength, PCF radius, and bias.
3. Add texel snapping for stable camera movement.
4. Add shadow-only screenshot artifacts for parity debugging.

Validation:

```bat
tools\validate_renderers.bat
```

If GPU time rises meaningfully:

```bat
tools\validate_perf.bat
```

### Phase 6: Optional Cascades

Tasks:

1. Split camera frustum into 2-3 cascades.
2. Render each cascade into array texture or atlas.
3. Select cascade in receiver shader by view depth.
4. Stabilize each cascade independently.
5. Add debug cascade tint mode.

Only do this if one high-resolution map cannot cover both close objects and distant tree shadows.

Validation:

```bat
tools\validate_renderers.bat
tools\validate_perf.bat
```

## Validation Policy

This is renderer/shader/backend work. Required validation after implementation:

```bat
tools\validate_renderers.bat
```

Required when shadow pass cost, extra draw calls, PCF samples, cascades, or object counts change hot-path behavior:

```bat
tools\validate_perf.bat
```

DX12-specific requirement:

- `dx12_validation.txt` must report 0 validation errors.
- Shadow depth resources must not produce descriptor heap or resource barrier warnings.

Parity requirement:

- GL vs DX11 and GL vs DX12 average pixel diff must remain below the existing threshold.
- Because real shadows create high-contrast edges, also inspect max-diff/heatmap artifacts if parity average approaches the threshold.

## Performance Budget

V1 expected cost:

- one extra depth pass over shadow casters,
- one extra depth texture,
- 9 shadow taps per receiving pixel for 3x3 PCF.

Initial budget target:

- shadow map pass under 1.0 ms on the target machine for the showcase scene,
- receiver sampling under 1.5 ms at current capture resolution,
- no per-frame heap allocations,
- no new CPU-side per-object allocations in the draw loop.

Optimization levers:

- lower map size from 4096 to 2048,
- reduce PCF radius,
- cull small casters,
- skip object receive in non-cinematic scenes,
- use one cascade for locked hero cameras,
- render terrain into shadow map only when terrain self-shadowing matters.

## Risks

High-risk areas:

- GL vs DX depth convention mismatch.
- DX12 resource barriers and descriptor state.
- Matrix multiplication order differences between GLSL and HLSL.
- Shadow acne on low-poly terrain facets.
- Peter-panning under boxes/balls from excessive bias.
- Terrain visual displacement not matching shadow caster displacement.
- Reflection pass accidentally sampling stale or wrong shadow resources.
- PCF differences causing parity drift.

Mitigations:

- Start with a single tiny test scene.
- Add debug shadow map capture immediately.
- Keep manual PCF math mirrored between GLSL and HLSL.
- Keep the legacy disc pass behind a switch for quick visual fallback.
- Validate each backend before adding cascades or advanced filtering.

## Suggested Commit Rhythm

1. `render: add shadow map resources`
2. `render: add directional shadow caster pass`
3. `render: sample real shadows on terrain`
4. `render: sample real shadows on objects`
5. `render: add shadow debug controls`
6. `render: tune showcase shadows`

Each commit should include:

- affected backend(s),
- exact validation command,
- DX12 validation result,
- parity numbers,
- screenshot paths for before/after.

## Time Estimate

Minimum viable real directional shadows:

- 2-4 days if backend resource APIs are straightforward.

Investor-quality pass with stable bias, good PCF, showcase tuning, debug views, and GL/DX11/DX12 parity:

- 4-7 days.

Cascaded shadows and polished large-world behavior:

- 1-2 additional weeks depending on renderer architecture cleanup.

## Stop Conditions

Stop and reassess if:

- DX12 barriers/root signatures become the dominant work,
- parity diverges and cannot be diagnosed from final screenshots,
- bias tuning consumes more than half a day without a debug view,
- shadow map pass exceeds perf budget,
- the current render pass structure makes integration fragile.

If blocked, first build debug capture tooling and pass boundaries rather than continuing to tune blindly.

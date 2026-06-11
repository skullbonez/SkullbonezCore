# 20 Concept Looks Rendering Plan

Status: review draft  
Created: 2026-06-10  
Reference: `Agentic/Concepts.png`  
Scope: renderer infrastructure, scene cycling, shader/style data, materials, scene authoring  
Implementation status: plan only, no code changes in this pass

## Goal

Support all 20 concept looks from `Agentic/Concepts.png` as authored scenes that can be browsed with the left and right arrow keys.

The target experience:

1. Launch a concept scene pack.
2. Press Right to move from concept 01 to concept 02.
3. Press Left to move back.
4. Every scene has a visibly distinct look, not just a different camera angle.
5. The scenes stay renderer-neutral: OpenGL, DX11, and DX12 should share the same authored look data and shader behavior.

## Current Engine Read

The engine already has useful pieces for this:

- Left/right keys already cycle scenes.
- Scene files already support strict directives for camera, physics, water, terrain visibility, screenshots, fixed-step playback, and many `cinematic_*` overrides.
- The render path already has an HDR cinematic target and post resolve.
- `CinematicRenderConfig` already controls exposure, gamma, sun screen position, sky colors, cloud controls, god rays, volumetric light, bloom, fog, and terrain relief.
- The renderer backend abstraction already supports GL/DX11/DX12 shader pairs and framebuffer creation.
- The material side is still very thin: renderable objects only carry per-instance tint and color override.
- The current cinematic shaders are strongly authored around one golden-hour scene. Several visual choices are hard-coded in shader functions rather than controlled by data.

The important conclusion: the engine is much closer than a blank renderer, but the current data model is still "one cinematic look with many tuning knobs", not "20 art-directed looks".

## Main Decision

We should not solve this by adding 20 independent scene shaders.

We should build a small render-style and material layer, then author the 20 scenes mostly as data.

Recommended shape:

- One style config per scene.
- One compact material parameter struct.
- A small set of reusable shader modes.
- A concept scene suite or presentation mode for arrow-key cycling.
- A dynamic texture registry before adding asset-heavy looks.

This is intentionally not a full Unreal-style material graph. That would be too big for tonight and would distract from shipping the 20 looks. The right target is a "material system v1": named material presets with enough parameters to vary color, roughness, metalness, specular response, emissive glow, and stylization.

## Why Scene Shaders Alone Are Not Enough

Adding shaders per scene would work for a few whole-frame looks, but it falls apart for these concepts:

- `3. Studio Lighting Showcase`: needs reflective/glossy balls, polished floor response, and controlled highlights.
- `11. Sci-fi Test Chamber`: needs clean wall/floor material, emissive light strips, sharp reflections.
- `12. Low Poly Art Style`: needs flat shaded normals/material mode, simplified texture handling, and palette control.
- `16. Tron Grid`: needs emissive grid lines and black background, probably not normal terrain texture.
- `19. Abstract Render Showcase`: explicitly requires many different BRDF/materials in one scene.
- `20. Pixar-Inspired`: needs soft stylized lighting and material response, not just post color.

The existing per-instance tint can fake some colors, but it cannot express:

- roughness,
- metallic response,
- emissive surfaces,
- unlit surfaces,
- flat/toon response,
- per-object material assignment,
- multiple material families in one scene.

So: add a small material system, not a huge one.

## Current Infrastructure Inventory

### Scene Cycling

Current behavior:

- Left/right arrows call `LoadAdjacentSceneFromBrowser`.
- That cycles through all `.scene` files discovered in `SkullbonezData/scenes`.
- The list is sorted alphabetically.
- It does not currently appear suite-aware for manual arrow cycling.

Problem for the concept pack:

- If the 20 concept scenes are just mixed into the global scene folder, left/right will pass through unrelated physics/UI/regression scenes unless naming and filtering are controlled.

Recommended solution:

- Add a concept presentation mode where left/right cycles the active launch queue when launched with `--suite`.
- Or, for the fastest implementation, name scenes `concept_01_...scene` through `concept_20_...scene` so they sort contiguously, and add a `concepts.suite` for automation.

Preferred implementation:

- Update left/right cycling to prefer `m_sceneQueue` when the current scene belongs to the queue and the queue has more than one scene.
- Fall back to the browser list for normal UI scene browsing.
- This preserves existing scene browser behavior while making a launched concept suite behave like a slide deck.

### Cinematic Render Path

Current behavior:

- `IsCinematicRenderingEnabled()` gates the HDR/post stack.
- `EnsureCinematicRenderResources()` creates:
  - full-size HDR scene FBO,
  - half-size volumetric light FBO,
  - `post_tonemap`,
  - `post_volumetric_light`,
  - `sky_atmosphere`,
  - fullscreen quad dynamic VB.
- Main world rendering happens into the HDR scene FBO.
- Optional volumetric light is generated from scene color/depth.
- Tonemap resolves to the backbuffer.
- UI/text are drawn after the world render, so they stay display-space.

Good news:

- This is the right foundation for many of the concept looks.

Current limitation:

- The path is named and shaped as "cinematic" and the shader internals are tuned for the golden-hour reference.

Recommended direction:

- Keep the render path.
- Broaden the data from `CinematicRenderConfig` into a `RenderStyleConfig` or embed a style subsection inside it.
- Keep backwards compatibility with existing `cinematic_*` scene directives while adding new general `style_*` or `look_*` directives.

### Object Rendering

Current behavior:

- Spheres and boxes render through `lit_textured_instanced`.
- Both use the same shared shader.
- Instance payload is:
  - model matrix,
  - tint RGB,
  - color override scalar.
- All non-debug objects share the same texture selected before drawing.
- In cinematic directional-light mode, the shader overrides the beach-ball texture with a procedural red/yellow beach ball.

Good news:

- Hardware instancing is already in place.
- Per-instance data already exists and can be extended.

Current limitation:

- No material ID per object.
- No material parameters per instance.
- No way for a scene file to assign materials.

Recommended direction:

- Extend instance payload from `mat4 + tint4` to `mat4 + tint4 + materialPacked`.
- Keep the existing tint behavior for compatibility.
- Add either:
  - a material ID index into a small GPU-side uniform array, or
  - direct material floats in the instance stream.

For tonight, direct packed material floats are simpler. For long-term, material IDs plus a material table scale better.

### Terrain Rendering

Current behavior:

- Terrain uses `lit_textured`.
- Cinematic path can apply visual-only basin relief and warm terrain grading.
- Terrain texture is global from `engine.cfg`.

Good news:

- Terrain shader already has cinematic relief hooks.

Current limitation:

- Terrain look is still mostly one texture plus hard-coded cinematic brown/orange grading.
- No per-scene terrain texture override.
- No style mode for low-poly, snow, photogrammetry, grid, sci-fi chamber floor, etc.

Recommended direction:

- Add terrain material/style parameters:
  - terrain palette,
  - texture mode,
  - relief mode,
  - normal mode,
  - roughness/specular,
  - snow/ice amount,
  - grid/emissive line amount.
- Add scene directives for terrain texture or terrain material preset.

### Water Rendering

Current behavior:

- Water has calm and ocean shaders.
- Cinematic mode currently turns calm water into a hard-coded oval basin pool.
- Cinematic mode skips the outer ocean pass.
- Ocean pass supports animated waves and reflection perturbation.

Good news:

- Water reflection infrastructure already exists.
- Calm/ocean split can support pool vs ocean world.

Current limitation:

- Basin center/radius is hard-coded.
- Sun glint position is hard-coded around the golden-hour sun.
- No scene-level water material/preset.
- Some concepts need no water, some need basin pool, some need full ocean, some need wet ground, and some need stylized/non-real water.

Recommended direction:

- Add water style params:
  - water mode: none, basin, ocean, wet floor, grid disabled.
  - water tint,
  - alpha,
  - reflection strength,
  - glint strength,
  - wave height,
  - perturb strength,
  - basin center/radius/feather.

### Sky and Atmosphere

Current behavior:

- Non-cinematic scenes use skybox textures.
- Cinematic scenes can use `sky_atmosphere`.
- `sky_atmosphere` draws a screen-space procedural sky, sun disk, clouds.
- Cloud placement is currently hard-coded for the golden-hour shot.

Good news:

- A procedural sky shader is the right idea.

Current limitation:

- Alien planet, storm front, Nordic winter, Pixar sky, massive planet, dreamscape, and Tron black grid all need different sky modes.

Recommended direction:

- Add sky modes:
  - procedural sun sky,
  - dual sun alien sky,
  - storm sky,
  - clean studio black/gray background,
  - sci-fi interior background,
  - ocean horizon sky,
  - space/planet sky,
  - stylized painterly/Pixar sky,
  - black Tron void.
- Keep this in one shader if possible using a `uSkyMode` int and style params.
- Split into a second shader only if a mode becomes too branch-heavy or needs distinct geometry.

### Texture System

Current behavior:

- `TOTAL_TEXTURE_COUNT` is 8.
- Current slots are effectively consumed by:
  - terrain texture,
  - sphere texture,
  - six skybox faces.
- Texture collection maps fixed hashes to backend handles.

Problem:

- The concept pack will need more textures:
  - snow/ice,
  - photogrammetry ground,
  - concrete/industrial,
  - sci-fi floor/walls,
  - low-poly/painterly palettes or ramps,
  - neon signs/grid masks,
  - storm/ocean/noise maps if not fully procedural.

Recommended direction:

- Replace the fixed 8-slot collection with a dynamic texture registry.
- Keep legacy texture hashes working.
- Add scene directives for texture aliases:
  - `texture ground concepts/photogrammetry_ground.png`
  - `texture sphere concepts/beachball.png`
  - `texture decal_neon concepts/neon_signs.png`
- Use name hashes or string keys internally.

This is one of the first infrastructure tasks because asset-heavy scenes will otherwise fight the old fixed slot model.

## Proposed New Data Model

### RenderStyleConfig

Add a new struct, or extend the existing cinematic config carefully.

Suggested fields:

```cpp
enum class SkyMode
{
    LegacySkybox,
    ProceduralSun,
    StudioGradient,
    Storm,
    AlienDualSun,
    SpacePlanet,
    Painterly,
    Pixar,
    TronBlack
};

enum class PostStyleMode
{
    Neutral,
    Realistic,
    IndustrialMono,
    Cyberpunk,
    Painterly,
    LowPoly,
    RetroHDR,
    FogWorld,
    Storm,
    Tron,
    Pixar,
    AbstractShowcase
};

enum class WaterMode
{
    None,
    BasinPool,
    FullOcean,
    WetFloor,
    ReflectiveStudioFloor
};

struct RenderStyleConfig
{
    bool enabled;
    SkyMode skyMode;
    PostStyleMode postMode;
    WaterMode waterMode;

    float exposure;
    float gamma;
    float contrast;
    float saturation;
    float vignetteStrength;

    float sunScreenX;
    float sunScreenY;
    float sunDirX;
    float sunDirY;
    float sunDirZ;
    float sunColorR;
    float sunColorG;
    float sunColorB;
    float sunIntensity;

    float ambientR;
    float ambientG;
    float ambientB;
    float groundBounceR;
    float groundBounceG;
    float groundBounceB;

    float skyHorizonR;
    float skyHorizonG;
    float skyHorizonB;
    float skyZenithR;
    float skyZenithG;
    float skyZenithB;
    float skyGlowStrength;

    float cloudCoverage;
    float cloudSoftness;
    float cloudScale;
    float cloudIntensity;
    float cloudSeed;
    float cloudPattern;

    float fogColorR;
    float fogColorG;
    float fogColorB;
    float fogStart;
    float fogEnd;
    float fogDensity;
    float fogMaxOpacity;

    float bloomThreshold;
    float bloomKnee;
    float bloomStrength;
    float bloomRadius;

    float volumetricStrength;
    float volumetricDensity;
    float volumetricDecay;

    float terrainStyle;
    float terrainRelief;
    float terrainSnow;
    float terrainWetness;

    float waterTintR;
    float waterTintG;
    float waterTintB;
    float waterAlpha;
    float waterReflectionStrength;
    float waterWaveHeight;
    float waterGlintStrength;
};
```

This can be staged. Do not add every field in one pass unless the implementation remains clean. The critical first fields are style mode, post controls, sky mode, water mode, and light/material controls.

### MaterialParams

Minimum useful material v1:

```cpp
enum class MaterialStyle
{
    Lit,
    Unlit,
    Toon,
    LowPoly,
    Metallic,
    Glassy,
    Rubber,
    Emissive,
    Matte,
    Snow,
    Wet,
    Grid
};

struct MaterialParams
{
    float baseR;
    float baseG;
    float baseB;
    float alpha;

    float roughness;
    float metallic;
    float specular;
    float emissiveStrength;

    float emissiveR;
    float emissiveG;
    float emissiveB;
    float styleFlags;
};
```

For instanced objects, use either:

- material ID plus material table, or
- packed material params per instance.

Recommended for tonight:

- Add `materialId` to `GameModel`.
- Add `material` to `SceneBall` and `SceneBox`.
- Add a small fixed material table on the CPU for scene presets.
- Batch by shape and material ID if necessary.

Recommended long-term:

- Material ID in instance data.
- GPU material table uniform/structured buffer.
- Backend-friendly cap such as 64 scene materials.

### Scene Directives

Keep authoring simple. Examples:

```text
look golden_hour
look brutal_industrial
look neon_cyberpunk

material beachball classic_beach_ball
material chrome metallic 0.92 0.92 0.95 roughness 0.12 metallic 1.0 specular 1.0
material neon_magenta emissive 1.0 0.0 0.8 emissive 6.0

terrain_material sandstorm
water_mode basin
sky_mode storm
post_style cyberpunk

ball hero_center material beachball 620 56 630 28 115 26 0.55
box test_wall material matte_white 500 60 500 20 5 20 1000 0.2
```

If changing existing `ball` syntax is too disruptive, add separate assignment directives:

```text
ball hero_center 620 56 630 28 115 26 0.55
object_material hero_center chrome
object_tint hero_center 1.0 0.2 0.1 1.0
```

This second approach is safer because it does not break existing strict parsing of `ball` and `box`.

Recommended parser path:

1. Add named material definitions.
2. Add `object_material <objectName> <materialName>`.
3. Add `object_tint <objectName> <r> <g> <b> <override>`.
4. Apply those after object construction in `SetUpGameModelsFromScene`.

## Shader Strategy

### Keep Shader Count Low

Reusable shader set:

- `lit_textured_instanced`: object material rendering.
- `lit_textured`: terrain material rendering.
- `sky_atmosphere`: all procedural sky modes.
- `post_tonemap`: all post styles, fog, bloom approximation, color grade.
- `post_volumetric_light`: depth/god-ray/shaft texture.
- `water_calm` and `water_ocean`: style-aware water.
- Specialty additions only as needed:
  - `neon_grid`,
  - `particle_dust_rain`,
  - `light_panel` or emissive primitives.

### Pull Out Golden-Hour Hard-Codes

Current hard-coded areas to generalize:

- `HeroCloudMask` in `sky_atmosphere`.
- `HeroCloudMask` in `post_tonemap`.
- `HeroCloudMask` in `post_volumetric_light`.
- fixed cloud band multipliers that zero out lower procedural clouds.
- terrain sunset palette in `lit_textured`.
- procedural beach ball forced whenever light w is 0 in `lit_textured_instanced`.
- water basin mask center/radius in C++ and shader constants.
- water glint screen X/Y hard-coded around golden-hour sun.
- final contrast/saturation/vignette in `post_tonemap`.

These should become style fields or material fields.

### Material Shader Direction

Do not jump directly to full PBR.

Use "PBR-lite":

- Directional sun.
- Ambient sky.
- Ground bounce.
- Lambert or wrapped diffuse.
- Blinn/Phong specular with roughness controlling exponent.
- Metallic blend that shifts diffuse into specular response.
- Emissive term.
- Toon/low-poly modes as flags.

This gets us most of the concept looks at low risk.

## Scene Pack Structure

Suggested files:

```text
SkullbonezData/scenes/concepts.suite
SkullbonezData/scenes/concept_01_golden_hour_realism.scene
SkullbonezData/scenes/concept_02_brutal_industrial.scene
SkullbonezData/scenes/concept_03_studio_lighting_showcase.scene
SkullbonezData/scenes/concept_04_neon_cyberpunk.scene
SkullbonezData/scenes/concept_05_alien_planet.scene
SkullbonezData/scenes/concept_06_desert_storm.scene
SkullbonezData/scenes/concept_07_painterly.scene
SkullbonezData/scenes/concept_08_retro_future_2005.scene
SkullbonezData/scenes/concept_09_atmospheric_fog_world.scene
SkullbonezData/scenes/concept_10_ocean_world.scene
SkullbonezData/scenes/concept_11_scifi_test_chamber.scene
SkullbonezData/scenes/concept_12_low_poly_art_style.scene
SkullbonezData/scenes/concept_13_massive_scale.scene
SkullbonezData/scenes/concept_14_storm_front.scene
SkullbonezData/scenes/concept_15_photogrammetry_ground.scene
SkullbonezData/scenes/concept_16_tron_grid.scene
SkullbonezData/scenes/concept_17_dreamscape.scene
SkullbonezData/scenes/concept_18_nordic_winter.scene
SkullbonezData/scenes/concept_19_abstract_render_showcase.scene
SkullbonezData/scenes/concept_20_pixar_inspired.scene
```

Suggested launch:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --suite SkullbonezData\scenes\concepts.suite --cinematic --hold
```

After suite-aware arrows:

- Left/right should cycle within `concepts.suite`.
- `Q` should still cycle renderers.
- `R` should reset the current look.

## Shared Composition Strategy

The concept sheet keeps the same broad subject: many red/yellow balls over different worlds. To finish tonight, reuse a common authored layout where possible:

- A hero foreground ball.
- Mid-ground ball clusters.
- Distant balls for atmospheric depth.
- A few floating silhouettes for sky/shaft reads.
- Shared camera variants:
  - low horizon,
  - high showcase,
  - interior/studio,
  - massive scale/wide,
  - top-down/tron.

Do not hand-place 20 completely unrelated populations if time is tight. Create one base composition generator or copy a base layout and alter:

- camera,
- water mode,
- terrain style,
- sky mode,
- material palette,
- post style,
- object scale distribution.

## Concept-by-Concept Plan

| # | Look | Current Support | Required Additions | Notes |
|---|------|-----------------|--------------------|-------|
| 1 | Golden Hour Realism | Mostly present | Generalize current cinematic hard-codes | This is the current hero scene. Use it as baseline. |
| 2 | Brutal Industrial | Partial | monochrome post, wet dark terrain/concrete, overcast sky | Can use same geometry with grey palette and high AO/contrast. |
| 3 | Studio Lighting Showcase | Partial | studio sky/background, reflective floor/water-floor mode, glossy material | Needs material roughness/specular. |
| 4 | Neon Cyberpunk | Partial | emissive materials, neon signs/panels, magenta/cyan grade, wet floor | Needs emissive material and bloom. |
| 5 | Alien Planet | Partial | alien sky mode, dual suns, purple terrain/vegetation colors, floating rocks | Floating rocks can be fixed boxes/spheres. |
| 6 | Desert Storm | Partial | dust/fog color, particle/dust overlay, heat shimmer optional | Can fake dust in post first; particles later. |
| 7 | Painterly | Partial | painterly post mode, soft shadows, palette quantization or brush noise | Use stylized post before new textures. |
| 8 | Retro Future 2005 | Mostly present | high bloom/lens flare style, saturated HDR, shiny water | Current cinematic path can get close. |
| 9 | Atmospheric Fog World | Mostly present | heavier fog, low visibility, desaturated sky | Current fog path can handle first pass. |
| 10 | Ocean World | Partial | full ocean water mode, horizon camera, sun glints | Needs cinematic path not to skip ocean pass. |
| 11 | Sci-fi Test Chamber | Low | interior geometry, light strips, white glossy material | Needs emissive strips and chamber meshes/boxes. |
| 12 | Low Poly Art Style | Partial | flat shading/material flag, low-poly terrain look, flat colors | Needs normal/style mode, maybe lower sphere tessellation or faceted shader. |
| 13 | Massive Scale | Partial | giant planet/sky object, tiny camera scale, huge terrain/fog | Can start with procedural sky planet disk. |
| 14 | Storm Front | Partial | storm sky, lightning bolt/pass, rain/wet surfaces | Lightning can be overlay/sky shader first. |
| 15 | Photogrammetry Ground | Low | high-detail ground texture, neutral daylight, terrain texture override | Needs texture registry and terrain texture directive. |
| 16 | Tron Grid | Low | black world, emissive grid, wire outlines/emissive object edges | Needs grid shader/material; can hide terrain texture. |
| 17 | Dreamscape | Partial | floating islands/rocks, pastel sky, surreal bloom | Fixed boxes/spheres can stand in for islands at first. |
| 18 | Nordic Winter | Partial | snow/ice terrain material, cold ambient, crisp lighting | Needs terrain snow palette/texture. |
| 19 | Abstract Render Showcase | Low | multiple material presets in one scene | This is the strongest argument for material system. |
| 20 | Pixar-Inspired | Partial | soft stylized sky, warm diffuse, simple green hills, cute scale | Needs stylized material/post but can be done with existing geometry. |

## Implementation Phases

### Phase 0: Safety And Presentation Prep

Goal:

- Make it easy to review the 20 concepts without fighting the existing scene browser.

Tasks:

1. Add `concepts.suite`.
2. Add suite-aware left/right cycling, or confirm alphabetic contiguous scene cycling is acceptable for tonight.
3. Decide screenshot paths for each concept.
4. Keep `--hold` behavior for manual review.

Validation:

- Scene/load behavior change: `tools\validate_fast.bat` at minimum.
- If render behavior is also touched in same change: `tools\validate_renderers.bat`.

### Phase 1: Render Style Presets

Goal:

- Replace one-off `cinematic_*` tuning with reusable named looks.

Tasks:

1. Add a `style <name>` scene directive that loads `SkullbonezData/styles/<name>.style`.
2. Store look values in parsed `.style` files, not C++ preset tables.
3. Preserve existing explicit `cinematic_*` overrides as final scene-local overrides.
4. Add style mode fields needed by shaders:
   - post mode,
   - sky mode,
   - water mode,
   - contrast,
   - saturation,
   - vignette,
   - sun direction/screen position,
   - cloud pattern/seed.

Acceptance:

- Existing `cinematic_volumetric.scene` still renders.
- A scene can switch from golden hour to industrial by changing one line.

Validation:

- `tools\validate_renderers.bat`.

### Phase 2: Material System v1

Goal:

- Allow scenes to assign named visual materials without changing physics.

Tasks:

1. Add `MaterialParams`.
2. Add a small scene material table.
3. Add material preset definitions:
   - classic beach ball,
   - matte white,
   - dark wet concrete,
   - chrome,
   - black rubber,
   - gold,
   - colored plastic,
   - neon magenta,
   - neon cyan,
   - snow/ice,
   - toon green.
4. Add style-file material directives or parsed material tables.
5. Add `object_material <objectName> <materialName>`.
6. Store material ID in `GameModel`.
7. Pass material ID/params to instanced object shader.

Acceptance:

- `19. Abstract Render Showcase` can show many distinct surfaces in one scene.
- Existing scenes default to classic beach-ball behavior.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` if batching changes increase draw calls or instance payload size meaningfully.

### Phase 3: Texture Registry

Goal:

- Remove the 8-texture cap before asset-heavy looks.

Tasks:

1. Replace fixed texture array with dynamic records.
2. Preserve legacy hash lookup for ground, sphere, and skybox textures.
3. Add named texture loading.
4. Add optional scene texture aliases.
5. Add safe reload/reset behavior when renderer changes.

Acceptance:

- Existing scenes still load default textures.
- Concept scenes can load additional ground/snow/concrete/neon textures.

Validation:

- `tools\validate_renderers.bat`.

### Phase 4: Shader Generalization

Goal:

- Stop hard-coding golden-hour composition inside general shaders.

Tasks:

1. Add `uPostMode`, `uSkyMode`, and material/style uniforms.
2. Parameterize cloud pattern and hero cloud placement.
3. Parameterize final contrast/saturation/vignette.
4. Parameterize water basin mask/glint.
5. Replace "directional light means procedural beach ball" with material/style selection.
6. Add emissive material support.
7. Add low-poly/toon style support.

Acceptance:

- Same shader files can produce golden hour, industrial, neon, fog, low-poly, and Pixar variants.

Validation:

- `tools\validate_renderers.bat`.

### Phase 5: Specialty Effects

Goal:

- Add the few effects that cannot be faked well with color grading.

Tasks:

1. Dust/rain particle or fullscreen overlay mode.
2. Lightning sky overlay for storm.
3. Neon/grid emissive primitives for Tron/cyberpunk.
4. Planet disk/ring procedural sky elements.
5. Optional heat shimmer distortion for desert.

Acceptance:

- `4`, `6`, `13`, `14`, and `16` become recognizably distinct.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` if particles/post effects are enabled.

### Phase 6: Author The 20 Scenes

Goal:

- Create all 20 concept scenes.

Tasks:

1. Create base object layout file or copy from `cinematic_volumetric.scene`.
2. Author each concept scene with:
   - `look`,
   - camera,
   - water mode,
   - world/physics off if static,
   - material assignments,
   - screenshot target.
3. Add all scene paths to `concepts.suite`.
4. Review manually in GL first.
5. Port/tune parity through DX11 and DX12.

Acceptance:

- Left/right cycles all 20 scenes.
- Each scene clearly matches its named concept.
- No unrelated regression scenes appear between concept looks during suite review.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` if new effects are performance-sensitive.

## Suggested Tonight Build Order

If the goal is "20 scenes tonight", do not start with the hardest material showcase. Build in this order:

1. Suite-aware arrow cycling.
2. `look <name>` presets.
3. Parameterize existing golden-hour shader hard-codes enough to vary sky/post/water.
4. Add material ID and a minimal material table.
5. Author first 8 scenes that mostly reuse existing infrastructure:
   - 01 Golden Hour,
   - 02 Industrial,
   - 08 Retro Future,
   - 09 Fog World,
   - 10 Ocean World,
   - 18 Nordic Winter,
   - 20 Pixar,
   - 07 Painterly.
6. Add emissive/grid/lighting support:
   - 04 Neon Cyberpunk,
   - 11 Sci-fi Test Chamber,
   - 16 Tron Grid,
   - 19 Abstract Showcase.
7. Add specialty sky/environment looks:
   - 05 Alien Planet,
   - 13 Massive Scale,
   - 14 Storm Front,
   - 17 Dreamscape.
8. Add texture-heavy and atmosphere-heavy scenes:
   - 06 Desert Storm,
   - 12 Low Poly,
   - 15 Photogrammetry.

If time gets tight:

- Prioritize distinct look presets over perfect geometry.
- Use procedural sky/post/material changes first.
- Use placeholder geometry for chambers/islands/rocks.
- Do not chase photogrammetry asset perfection before the scene pack cycles cleanly.

## Cut Lines If Time Runs Short

Must have:

- Left/right cycles the 20 concept scenes.
- Each scene has a unique authored look.
- Material system v1 supports at least beach ball, matte, metal, emissive, toon, snow/wet.
- GL path works first, then renderer validation.

Can defer:

- Full dynamic texture registry if procedural materials are enough for the first pass.
- Real particles for sand/rain.
- Fully physical PBR.
- True volumetric clouds.
- Complex interior meshes.
- Perfect photogrammetry ground.
- Dedicated UI controls for every new material/style parameter.

Do not defer:

- Avoiding 20 copy-pasted one-off shaders.
- DX11/DX12 parity once shader changes are promoted.
- Keeping existing render tests valid.
- Ensuring no code path relies on golden-hour hard-codes for every cinematic scene.

## Validation Plan

Documentation-only plan:

- No validation required.

Implementation validation:

- Scene parser/scene file additions only:
  - `tools\validate_renderers.bat` if screenshots/render behavior are part of the change.
  - `tools\validate_fast.bat` if it is just scene loading/plumbing.
- Shader/render backend changes:
  - `tools\validate_renderers.bat`.
- Texture registry or resource lifetime changes:
  - `tools\validate_renderers.bat`.
- Performance-sensitive post, particles, volumetric, or batching changes:
  - `tools\validate_perf.bat`.
- Broad mixed changes:
  - `tools\validate_full.bat`.

For DX12:

- Verify `dx12_validation.txt` has zero validation errors after render changes.
- Do not accept "looks fine in GL" as done.

## Acceptance Checklist

The feature is done when:

- `SkullbonezData/scenes/concepts.suite` lists all 20 looks.
- The app launches into the concept pack.
- Left/right arrows cycle only the concept pack when launched that way.
- Every concept scene has a distinct silhouette, color palette, lighting/post style, and material treatment.
- Existing non-concept scenes still load and render.
- GL, DX11, and DX12 render the concept scenes without backend-specific shader failures.
- Required validation output is captured and reported.

## Final Recommendation

Build the material system v1 and render-style presets first. They are the shortest path to making all 20 scenes feel intentional rather than like one scene with different color filters.

The current cinematic pipeline is a strong base. The work is not to throw it away. The work is to make it data-driven, then author the concepts on top of it.

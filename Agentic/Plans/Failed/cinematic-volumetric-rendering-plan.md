# Cinematic Volumetric Rendering Plan

## Purpose

This plan describes how to move SkullbonezCore toward the reference look shown in the conversation: a warm sunset scene with many red/yellow balls over a basin terrain, reflective water, glowing sun, atmospheric depth, soft cloud silhouettes, and visible volumetric shafts of light.

The goal is not merely to add one effect called "volumetric lighting". The reference image is the result of a full frame style:

- High dynamic range lighting.
- Filmic exposure and tonemapping.
- Low-angle golden sun.
- Warm atmospheric scattering.
- Bright bloom around sun and highlights.
- Layered depth fog.
- Sky/cloud occlusion of the sun.
- God rays and volumetric shafts.
- Terrain and water materials that respond strongly to grazing light.
- High object count with readable silhouettes and specular highlights.

The recommended strategy is one-renderer look development with all-renderer architecture:

- Use OpenGL first for fast shader and visual iteration.
- Keep the render pipeline and scene controls renderer-neutral from the beginning.
- Port the proven pass graph to DX11 next.
- Port to DX12 before volumetric clouds or raymarched lighting become mainline.
- Treat the work as incomplete until GL, DX11, and DX12 all validate.

Documentation-only changes to this plan require no validation. Any implementation touching renderer backends, shaders, screenshots, scene behavior, or visual baselines requires `tools\validate_renderers.bat`. Any performance-sensitive post-processing or volumetric work also requires `tools\validate_perf.bat`.

## Reference Visual Breakdown

### Composition

The target image is a cinematic wide frame looking across a basin or shallow valley.

- The horizon sits slightly above center.
- The camera is low enough that the terrain fills the lower half of the image.
- The sun is near the horizon, left of center.
- The biggest foreground ball is near the lower center, partially silhouetted and rim-lit.
- Many smaller balls fill the scene at different depths, which makes the fog and haze legible.
- The top-left overlay text is flat white and unaffected by scene tonemapping.
- The bottom-left stats text is also flat white, rendered after scene resolve.

For the first implementation scene, make composition deterministic. Do not use runtime-random placement for validation or visual comparison.

### Color Palette

The target is dominated by sunset warmth, but it should not become a single flat orange wash.

Primary ranges:

- Sun core: near-white with warm yellow edge.
- Sky near sun: yellow/orange.
- High sky/cloud shadows: muted brown, violet, or desaturated gray-orange.
- Far terrain: low-contrast amber haze.
- Foreground terrain: dark earthy brown with orange rim highlights.
- Water: reflective amber with darker brown/green low-angle areas.
- Balls: saturated red and yellow, with warm highlights and dark red/brown shadow sides.

Suggested starting values:

- Sun color: `(1.0, 0.68, 0.32)`.
- Sun intensity before tonemap: `8.0` to `20.0`.
- Sky ambient: `(0.35, 0.22, 0.16)`.
- Ground bounce: `(0.45, 0.20, 0.08)`.
- Fog color near sun: `(1.0, 0.46, 0.16)`.
- Fog color away from sun: `(0.36, 0.22, 0.18)`.
- Exposure: start around `1.0`, tune after HDR is in place.
- Bloom threshold: start around `1.2`.
- Bloom strength: start around `0.25` to `0.45`.

These values are look-development defaults, not permanent hard-coded constants. They should become scene or config controls once the pipeline exists.

### Lighting Character

The reference lighting is driven by a low sun.

- Direct light angle should be shallow, almost grazing the terrain.
- Terrain ridges should catch warm highlights.
- Ball edges facing the sun should get strong rim light.
- Ball shadow sides should not go fully black; they should receive warm sky/ground ambient.
- Specular highlights should be small and bright on balls and water.
- The sun itself should be much brighter than the final display range and then compressed by tonemapping.

This requires moving away from the current simple point-light-feeling Phong setup and toward a shared sun/atmosphere model.

### Depth And Atmosphere

The reference scene has strong aerial perspective:

- Far terrain loses contrast and shifts orange.
- Middle distance has visible vertical shafts of illuminated haze.
- Objects farther from the camera are lower contrast and more amber.
- The near foreground remains dark enough to frame the image.

The first version should use screen-space depth fog and height fog. Later versions can add true volumetric scattering.

### Clouds

The clouds in the reference serve two purposes:

- They create dramatic bright edges around the sun.
- They provide occlusion shapes for god rays.

The first pass does not need fully raymarched volumetric clouds. A procedural sky/cloud layer can establish the silhouette and occlusion pattern. Full volumetric clouds should come after the renderer-neutral HDR/post stack works in all three backends.

## Current Engine Starting Point

Relevant existing pieces:

- `SkullbonezSource/SkullbonezIRenderBackend.h` already abstracts GL, DX11, and DX12.
- `SkullbonezData/shaders/lit_textured.*` and `lit_textured_instanced.*` provide current Phong material rendering.
- `SkullbonezData/shaders/water_calm.*` and `water_ocean.*` provide reflective water.
- `SkullbonezData/shaders/unlit_textured.*` provides skybox-style unlit textured rendering.
- `SkullbonezData/shaders/shadow.*` provides simple projected shadow discs.
- `SkullbonezRun::DrawPrimitives()` currently renders skybox, reflection pre-pass, balls, terrain, shadows, water, then debug overlays.
- Reflection already uses framebuffer support and DX12 has additional DXR reflection machinery.

Important current limitations:

- Main scene rendering is effectively LDR.
- There is no shared HDR scene target.
- There is no fullscreen post stack for scene tonemapping.
- Existing lighting is simple Phong with limited art direction controls.
- Existing skybox is texture-face based rather than an atmospheric sky model.
- Existing fog/god-ray/cloud pipeline does not exist.
- Existing water reflection is useful but not yet tied into a modern color pipeline.

## Renderer Strategy

### Why OpenGL First

Use OpenGL first for look development because:

- GLSL iteration is fast.
- Resource and state setup are lower friction.
- It is easier to tune visual curves when the pipeline is changing quickly.
- The first phase is mostly about discovering the final visual recipe.

OpenGL must not become a special-case product path. Every new concept should be named and structured as a renderer-neutral pass, even if only the GL implementation exists at first.

### Why Not DX12 First

DX12 should not be the first look-development backend because:

- Descriptor management slows experiments.
- Resource state transitions add noise while visual math is unstable.
- PSO/root signature changes can obscure shader issues.
- InfoQueue validation must remain zero-error, which is valuable but expensive during experimentation.

DX12 should still arrive early. The handoff point is after HDR scene target, tonemap, and bloom are stable in GL. Do not wait until clouds and volumetric lighting are fully designed before checking DX12 feasibility.

### Backend Rollout

Milestone order:

1. Renderer-neutral pass graph and config shape.
2. OpenGL HDR scene target plus tonemap resolve.
3. OpenGL bloom.
4. OpenGL sun/sky/fog look pass.
5. DX11 parity for HDR, tonemap, bloom, and fog.
6. DX12 parity for HDR, tonemap, bloom, and fog.
7. OpenGL god rays and cloud occlusion.
8. DX11/DX12 god-ray parity.
9. OpenGL volumetric clouds/light scattering.
10. DX11/DX12 volumetric parity.

Every milestone should leave the existing non-cinematic scenes valid.

## Scene Plan

### New Cinematic Scene

Add a deterministic scene for visual development once implementation begins:

- Suggested path: `SkullbonezData/scenes/cinematic_volumetric.scene`.
- Physics may be off for visual validation unless motion is part of a later demo.
- Text overlay may be on for visual match, but validation variants should also support text off.
- Vsync off for deterministic screenshot automation.
- Fixed camera with low angle and wide composition.

Suggested scene properties:

- Camera position low and pulled back from the basin.
- View target near the center of the water/terrain basin.
- Water enabled.
- Terrain visible.
- 80 to 140 balls placed with deterministic authored transforms.
- Several large foreground balls, many mid-ground balls, and a few small distant balls.
- Optional boxes only if needed for shadow/material contrast; the reference is ball-focused.

The existing scene parser may not currently support all needed style directives. Do not overload unrelated physics directives for rendering style. Add explicit render-style scene directives only when the renderer code is ready to consume them.

### Reference Asset

If the user-provided screenshot is available as a local attachment, save it as:

- `Agentic/Plans/cinematic-volumetric-reference.png`

Reference it from this plan or a later implementation handoff as:

```md
![Cinematic volumetric reference](cinematic-volumetric-reference.png)
```

If the image is not available as a file, do not fabricate a fake reference. Use the visual breakdown in this plan as the source of truth until a real image file is supplied.

## Public Controls And Config

Add controls gradually. Do not expose every internal shader constant immediately. Use a small set of stable art-direction controls first.

### Renderer Feature Controls

Recommended controls:

- `cinematic_rendering`: on/off.
- `hdr_enabled`: on/off.
- `bloom_enabled`: on/off.
- `fog_enabled`: on/off.
- `sky_atmosphere_enabled`: on/off.
- `god_rays_enabled`: on/off.
- `clouds_enabled`: on/off.
- `volumetric_lighting_enabled`: on/off.
- `cinematic_quality`: off/low/medium/high.

Validation scenes should be able to turn expensive effects off individually.

### Sun And Exposure Controls

Recommended controls:

- `sun_direction_x/y/z`.
- `sun_color_r/g/b`.
- `sun_intensity`.
- `sky_ambient_r/g/b`.
- `ground_bounce_r/g/b`.
- `exposure`.
- `tonemap_mode`.
- `gamma`.

Initial defaults:

- Tonemap mode: ACES approximation.
- Gamma: `2.2`.
- Exposure: `1.0`.
- Sun intensity: `12.0`.

### Fog Controls

Recommended controls:

- `fog_density`.
- `fog_height_density`.
- `fog_height_falloff`.
- `fog_start`.
- `fog_max_opacity`.
- `fog_sun_scatter_strength`.
- `fog_color_near_sun_r/g/b`.
- `fog_color_away_r/g/b`.

Initial defaults:

- Distance fog density: low enough that foreground balls remain crisp.
- Height fog strongest near water/terrain basin.
- Max opacity below `0.9` so the horizon does not become a flat wall.

### Bloom Controls

Recommended controls:

- `bloom_threshold`.
- `bloom_knee`.
- `bloom_strength`.
- `bloom_radius`.
- `bloom_mip_count`.

Initial defaults:

- Threshold: `1.2`.
- Knee: `0.5`.
- Strength: `0.35`.
- Mip count: 5.

### God-Ray Controls

Recommended controls:

- `god_ray_enabled`.
- `god_ray_density`.
- `god_ray_decay`.
- `god_ray_weight`.
- `god_ray_exposure`.
- `god_ray_max_samples`.

Initial defaults:

- Samples: 48 low, 72 medium, 96 high.
- Density: start `0.85`.
- Decay: start `0.94`.
- Weight: start `0.18`.

## Frame Pipeline

### Target End-State Pass Order

The final cinematic frame should be organized like this:

1. Update scene and camera.
2. Reflection pre-pass remains available for water.
3. Bind HDR scene target.
4. Clear HDR color and scene depth.
5. Render atmospheric sky and sun disk.
6. Render terrain into HDR.
7. Render instanced balls/boxes into HDR.
8. Render shadow discs or improved contact shadows.
9. Render water into HDR.
10. Apply depth/height fog contribution.
11. Render or composite cloud layer.
12. Generate sun/cloud/depth occlusion mask for god rays.
13. Apply god-ray pass into HDR or a separate light buffer.
14. Extract bloom bright pass from HDR.
15. Downsample and blur bloom chain.
16. Composite bloom with HDR scene.
17. Tonemap and gamma-correct into backbuffer.
18. Render UI, debug overlays, and text into backbuffer.
19. Present.

The first implementation does not need every pass. The critical first cut is:

1. HDR scene target.
2. Existing scene render into HDR.
3. Tonemap resolve.
4. UI/text after resolve.

### Existing Reflection Compatibility

Do not remove current water reflection modes in the first milestones.

First HDR milestone:

- Keep reflection pre-pass behavior unchanged.
- Render reflection target as it currently works.
- Render main scene into HDR target.
- Water shader samples the existing reflection texture.
- Water result writes into HDR target.

Later HDR reflection improvement:

- Consider making reflection FBO HDR too.
- Tonemap reflection only as part of final main scene, not before water samples it.
- Ensure GL/DX11/DX12 reflection color spaces match.

### UI And Text

Render UI/text after final tonemap to keep it clean and readable.

- Top-left concept text should remain pure white or near-white.
- Bottom-left stats should remain stable and not bloom.
- Debug overlays should retain their current behavior unless intentionally moved.

Do not let HDR exposure affect UI legibility.

## Shader Plan

All new shaders should exist in GLSL and HLSL pairs when promoted beyond the OpenGL spike.

Naming below is suggested. Implementers may adjust file names to match local conventions, but shader responsibilities should remain the same.

### Existing Shaders To Keep

Keep and evolve these:

- `lit_textured.vert/.frag/.hlsl`.
- `lit_textured_instanced.vert/.frag/.hlsl`.
- `water_calm.vert/.frag/.hlsl`.
- `water_ocean.vert/.frag/.hlsl`.
- `unlit_textured.vert/.frag/.hlsl`.
- `shadow.vert/.frag/.hlsl`.
- `solid_color.*` and UI/text shaders.

The fastest path is to adapt existing lit/water shaders to output HDR-compatible colors, then add post-processing around them.

### New Fullscreen Triangle/Quad Base

Add a reusable fullscreen pass shader pattern.

Suggested files:

- `fullscreen_triangle.vert`.
- `fullscreen_triangle.hlsl`.

Responsibilities:

- Emit a fullscreen triangle without a vertex buffer if backend supports it, or use a tiny static mesh if simpler.
- Provide UV coordinates with consistent origin handling across GL/DX.
- Hide GL/DX texture coordinate differences in shader or backend binding code.

Use this base for tonemap, bloom, fog composite, god rays, and final resolve.

### HDR Tonemap Shader

Suggested files:

- `post_tonemap.frag`.
- `post_tonemap.hlsl`.

Inputs:

- HDR scene color texture.
- Optional bloom texture.
- Exposure.
- Bloom strength.
- Tonemap mode.
- Gamma.

Responsibilities:

- Read HDR scene color.
- Add scaled bloom.
- Apply exposure.
- Apply filmic tonemap.
- Apply gamma correction.
- Output LDR backbuffer color.

Recommended first tonemap:

```text
ACES approximation:
x = color * exposure
out = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)
out = pow(out, 1 / gamma)
```

Why this shader matters:

- The reference sun and highlights need values above 1.0.
- Tonemapping creates the compressed warm glow without clipping everything to white.
- Color grading should happen here first, not scattered across material shaders.

### Bloom Prefilter Shader

Suggested files:

- `post_bloom_prefilter.frag`.
- `post_bloom_prefilter.hlsl`.

Inputs:

- HDR scene color.
- Bloom threshold.
- Bloom knee.

Responsibilities:

- Extract bright areas smoothly.
- Preserve sun disk, cloud edges, ball highlights, and water glints.
- Avoid blooming normal midtones too heavily.

Use a soft knee instead of hard threshold to avoid harsh halos.

### Bloom Downsample Shader

Suggested files:

- `post_bloom_downsample.frag`.
- `post_bloom_downsample.hlsl`.

Responsibilities:

- Build a mip-like bloom chain.
- Use a small weighted sample pattern.
- Keep samples deterministic and matching between GL/DX.

Quality:

- Low: 3 mips.
- Medium: 5 mips.
- High: 6 or 7 mips.

### Bloom Upsample Shader

Suggested files:

- `post_bloom_upsample.frag`.
- `post_bloom_upsample.hlsl`.

Responsibilities:

- Upsample lower bloom levels into higher levels.
- Use additive blending or explicit texture combine.
- Control radius with sample offsets.

Avoid per-pass arbitrary constants that diverge by backend.

### Sky Atmosphere Shader

Suggested files:

- `sky_atmosphere.frag`.
- `sky_atmosphere.hlsl`.

Inputs:

- Camera/view direction.
- Sun direction.
- Sun color/intensity.
- Horizon color.
- Zenith color.
- Mie scattering strength.
- Rayleigh-ish sky tint strength.
- Cloud opacity mask if available later.

Responsibilities:

- Replace or augment the six-face skybox for the cinematic scene.
- Render a warm horizon gradient.
- Render a bright sun disk.
- Render a soft glow around the sun.
- Darken/desaturate upper sky away from the sun.

First implementation:

- Procedural gradient plus sun disk.
- No physical scattering requirement.

Later implementation:

- Add approximate Rayleigh/Mie phase terms:
  - Rayleigh: `3 / (16*pi) * (1 + cosTheta^2)`.
  - Mie: Henyey-Greenstein with `g` around `0.76`.

The art goal matters more than physical correctness.

### Cloud Layer Shader

Suggested files:

- `sky_clouds.frag`.
- `sky_clouds.hlsl`.

Inputs:

- View direction.
- Sun direction.
- Time or fixed scene seed.
- Noise texture or procedural noise.
- Cloud coverage.
- Cloud softness.
- Cloud sun-edge boost.

Responsibilities:

- Create large, soft cloud silhouettes around the sun.
- Provide an occlusion mask for god rays.
- Add warm lit edges and darker cloud interiors.

First implementation:

- Screen-space or sky-direction noise layer.
- Deterministic fixed seed for screenshot scenes.
- Cloud mask only needs to be plausible, not fully 3D.

Later implementation:

- Raymarched volumetric clouds at half or quarter resolution.
- Temporal jitter if needed, but validation screenshots need deterministic capture controls.

### Depth Fog Shader

Suggested files:

- `post_depth_fog.frag`.
- `post_depth_fog.hlsl`.

Inputs:

- HDR scene color.
- Depth texture.
- Camera matrices or inverse projection.
- Sun direction.
- Fog settings.

Responsibilities:

- Reconstruct view-space or world-space position from depth.
- Apply distance fog.
- Apply height fog concentrated near basin/water.
- Tint fog warmer near the sun direction.
- Preserve foreground contrast.

Suggested formula:

```text
distanceFog = 1 - exp(-distance * fogDensity)
heightFog = 1 - exp(-max(0, fogBaseHeight - worldY) * heightDensity)
sunAmount = pow(saturate(dot(viewDir, sunDir)), sunPower)
fogColor = lerp(fogAwayColor, fogSunColor, sunAmount * fogSunScatterStrength)
fogFactor = saturate((distanceFog + heightFog) * fogMaxOpacity)
color = lerp(sceneColor, fogColor * fogLuminance, fogFactor)
```

This can be a fullscreen pass after opaque/water rendering.

### God-Ray Occlusion Shader

Suggested files:

- `post_godray_mask.frag`.
- `post_godray_mask.hlsl`.

Inputs:

- Depth texture.
- Sky/cloud mask or sky color.
- Sun screen position.

Responsibilities:

- Generate a mask where sun shafts can pass.
- Occlude rays behind terrain/balls.
- Use cloud mask to break up the shafts.

First implementation:

- White near sun, dark where depth indicates foreground occlusion.
- Multiply by cloud transmittance.

Important:

- Do not raymarch the whole volume first. The mask plus radial blur gets a large visual win cheaply.

### God-Ray Radial Blur Shader

Suggested files:

- `post_godray_radial.frag`.
- `post_godray_radial.hlsl`.

Inputs:

- God-ray mask.
- Sun screen position.
- Density.
- Decay.
- Weight.
- Exposure.
- Sample count.

Responsibilities:

- March screen-space samples from pixel toward sun.
- Accumulate decaying light.
- Output a low-resolution light-shaft texture.

Suggested classic radial blur:

```text
delta = (uv - sunScreenUV) * density / samples
illuminationDecay = 1
for sample:
    uv -= delta
    sampleValue = mask(uv)
    color += sampleValue * illuminationDecay * weight
    illuminationDecay *= decay
color *= exposure
```

Run at half or quarter resolution first.

### God-Ray Composite Shader

Suggested files:

- `post_godray_composite.frag`.
- `post_godray_composite.hlsl`.

Inputs:

- HDR scene color.
- God-ray texture.
- Sun color.
- God-ray strength.

Responsibilities:

- Add warm shafts into HDR before tonemapping.
- Clamp or scale to avoid washing out foreground.
- Allow quality settings to disable the pass.

### Volumetric Light Shader

Suggested files:

- `post_volumetric_light.frag`.
- `post_volumetric_light.hlsl`.

Inputs:

- Depth texture.
- Camera position and inverse view-projection.
- Sun direction/color.
- Density settings.
- Optional cloud shadow/transmittance texture.
- Optional blue-noise texture.

Responsibilities:

- Raymarch from camera to scene depth at low resolution.
- Accumulate single-scattering approximation.
- Apply depth-aware upsample before composite.
- Keep deterministic behavior for validation.

This is a later milestone. Do not build this before HDR, fog, and god rays are stable.

Initial quality:

- Low: quarter resolution, 24 steps.
- Medium: half resolution, 48 steps.
- High: half resolution, 64 to 96 steps.

Failure mode to avoid:

- Expensive full-resolution raymarching that tanks `validate_perf`.

### Lit Material Shader Upgrade

Existing files:

- `lit_textured.frag/.hlsl`.
- `lit_textured_instanced.frag/.hlsl`.

Visual upgrades:

- Use directional sun direction in addition to or instead of point light.
- Add warm diffuse sun.
- Add ambient sky and ground bounce.
- Add stronger rim response for sunset silhouettes.
- Increase specular brightness into HDR range.
- Keep texture tint behavior for existing scenes.

Suggested material model for v1:

```text
N = normalized normal
V = view direction
L = sun direction toward surface
NoL = saturate(dot(N, L))
diffuse = albedo * sunColor * sunIntensity * NoL
skyAmbient = albedo * skyAmbientColor * (0.35 + 0.65 * saturate(N.y))
groundBounce = albedo * groundBounceColor * saturate(-N.y * 0.5 + 0.5)
rim = pow(1 - saturate(dot(N, V)), rimPower) * saturate(dot(N, L) * 0.5 + 0.5)
specular = sunColor * sunIntensity * specStrength * pow(saturate(dot(reflect(-L, N), V)), shininess)
color = diffuse + skyAmbient + groundBounce + rim * rimColor + specular
```

Start with Phong/Blinn-Phong rather than a full PBR conversion. Full PBR is optional later.

Ball-specific visual goal:

- Red/yellow texture remains saturated.
- Shadow sides become deep red/brown, not black.
- Sun-facing rims and specular highlights can exceed 1.0 before tonemap.

### Terrain Shader Upgrade

Existing terrain rendering likely uses the lit textured path or nearby equivalent.

Visual upgrades:

- Low-angle sun should reveal terrain ridges.
- Add warm grazing diffuse.
- Add ambient occlusion approximation if cheap.
- Add distance fog through post, not directly in terrain shader initially.
- Consider simple slope-based color variation later.

For v1, do not add complex terrain material layering. The screenshot feel comes mostly from lighting, fog, tonemap, and terrain geometry.

### Water Shader Upgrade

Existing files:

- `water_calm.frag/.hlsl`.
- `water_ocean.frag/.hlsl`.

Visual upgrades:

- Output HDR-compatible water color.
- Add Fresnel reflection factor.
- Add sun glitter/specular.
- Warm reflected sky/sun response.
- Keep existing reflection texture sampling.
- Keep flat/no-reflect debug modes.

Suggested additions:

```text
V = normalize(cameraPos - worldPos)
N = water normal, flat for calm pass or wave-derived for ocean
F = fresnelSchlick(F0, saturate(dot(N, V)))
reflection = sample reflection texture
sunSpec = pow(saturate(dot(reflect(-sunDir, N), V)), waterShininess) * sunIntensity
waterBase = waterTint * ambient
color = lerp(waterBase, reflection, reflectionStrength * F) + sunSpec * sunColor
```

For the first upgrade, approximate water normal from existing wave function. Do not block HDR/bloom work on physically accurate water.

### Shadow Shader And Contact Shadows

Existing files:

- `shadow.vert/.frag/.hlsl`.

Keep shadow discs initially. They are cheap and already integrated.

Visual upgrades:

- Adjust opacity for cinematic scene so shadows remain visible through warm fog.
- Avoid making all shadows black; consider warm brown shadow tint.
- Later, add screen-space contact shadows only if needed.

Do not implement full shadow maps as part of the first cinematic milestone unless sun occlusion for volumetrics requires it later.

## Render Target And Backend Resource Plan

### HDR Scene Target

Add a renderer-neutral way to create scene targets with explicit format.

Minimum needed formats:

- LDR color: existing behavior.
- HDR color: RGBA16F preferred.
- Depth: shader-readable depth for post effects.

Current `IFramebuffer` is minimal and probably reflection-oriented. It may need extension or a sibling abstraction.

Required capabilities:

- Bind as render target.
- Unbind to backbuffer.
- Get color texture handle.
- Get depth texture handle or equivalent.
- Resize with window.
- Reset resources on backend reset.
- Specify color format.
- Specify whether depth is shader-readable.

Implementation default:

- Add a new scene framebuffer abstraction only if extending `IFramebuffer` would make reflection code messy.
- Keep reflection FBO stable during first HDR milestone.

### Bloom Chain Targets

Bloom needs multiple render targets:

- Prefilter target.
- Downsample mip targets.
- Upsample/combine targets.

Start with fixed maximum mips, allocated based on current viewport:

- mip 0: half resolution.
- mip 1: quarter.
- mip 2: eighth.
- mip 3: sixteenth.
- mip 4: thirty-second.

Use RGBA16F or R11G11B10F if available and consistent. RGBA16F is safer for parity.

### Depth Texture Access

Fog/god rays need depth.

Backend requirements:

- GL: depth texture attachment or depth copy from framebuffer.
- DX11: typeless depth resource with DSV/SRV views or separate depth copy.
- DX12: resource states for depth write to shader resource transition.

DX12 must have explicit barriers:

- Depth write during scene render.
- Transition to pixel shader resource for fog/god rays.
- Transition back to depth write next frame or target reuse.

### Fullscreen Pass API

Add renderer-neutral helpers for:

- Bind render target.
- Bind shader.
- Bind textures.
- Set small uniform block.
- Draw fullscreen triangle.

Avoid ad hoc fullscreen code inside `SkullbonezRun::DrawPrimitives()` for every pass. The run loop should orchestrate passes, not duplicate backend details.

## Milestone Implementation Plan

### Milestone 1: Cinematic Scene And Documentation

Goal:

- Create deterministic visual target scene and keep this plan as handoff source.

Work:

- Add `cinematic_volumetric.scene`.
- Add optional screenshot directive for one-frame capture.
- Ensure it runs with current renderer before new effects.
- Save reference image if available as a real local file.

Validation:

- Scene-only/render behavior change requires `tools\validate_renderers.bat`.

### Milestone 2: HDR Scene Target And Tonemap

Goal:

- Existing scene renders through HDR target and resolves to backbuffer.

Work:

- Add HDR scene target creation for GL first.
- Add fullscreen tonemap shader.
- Render UI/text after tonemap.
- Add config/scene controls for exposure and tonemap enable.
- Preserve non-HDR fallback path.

Shaders:

- `fullscreen_triangle`.
- `post_tonemap`.

Acceptance:

- Visual output should match current scene when exposure is neutral and bloom is off, except for expected gamma/tonemap differences.
- No UI exposure shift.

Validation:

- `tools\validate_renderers.bat` once promoted beyond GL spike.

### Milestone 3: Bloom

Goal:

- Bright sun/highlights glow like the reference.

Work:

- Add bloom target chain.
- Add prefilter/downsample/upsample/composite.
- Composite bloom before or inside tonemap.
- Add bloom controls.

Shaders:

- `post_bloom_prefilter`.
- `post_bloom_downsample`.
- `post_bloom_upsample`.
- `post_tonemap` updated for bloom composite.

Acceptance:

- Sun disk blooms.
- Ball highlights bloom lightly.
- Midtones do not smear.
- UI does not bloom.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` once chain is enabled in perf-relevant scenes.

### Milestone 4: Sun Lighting And Material Upgrade

Goal:

- Balls and terrain read as sunset-lit, with warm rim highlights and non-black shadows.

Work:

- Add shared sun settings.
- Update lit material shaders.
- Keep current texture/tint path intact.
- Add ambient sky and ground bounce.

Shaders:

- `lit_textured`.
- `lit_textured_instanced`.

Acceptance:

- Balls have strong warm highlights.
- Shadow sides remain readable.
- Existing scenes do not become wildly overexposed when cinematic mode is off.

Validation:

- `tools\validate_renderers.bat`.

### Milestone 5: Sky Atmosphere And Sun Disk

Goal:

- Replace static skybox look in cinematic mode with sunset sky and visible sun.

Work:

- Add procedural sky pass.
- Draw before terrain/models into HDR.
- Keep existing skybox for non-cinematic scenes.
- Add sun disk and glow that bloom can catch.

Shaders:

- `sky_atmosphere`.

Acceptance:

- Sun position matches light direction.
- Horizon has warm gradient.
- Brightness survives until tonemap.

Validation:

- `tools\validate_renderers.bat`.

### Milestone 6: Depth And Height Fog

Goal:

- Add atmospheric depth and basin haze.

Work:

- Make scene depth readable.
- Add fullscreen fog composite.
- Reconstruct view/world position consistently across GL/DX depth conventions.
- Add fog controls.

Shaders:

- `post_depth_fog`.

Acceptance:

- Far terrain loses contrast.
- Foreground remains readable.
- Fog color warms near sun direction.
- GL/DX depth convention differences are handled.

Validation:

- `tools\validate_renderers.bat`.

### Milestone 7: Water Cinematic Upgrade

Goal:

- Water contributes warm reflections and sun glints.

Work:

- Add Fresnel term.
- Add sun specular.
- Keep reflection modes.
- Make water output HDR color.
- Tune calm/ocean tint for sunset.

Shaders:

- `water_calm`.
- `water_ocean`.

Acceptance:

- Water reflects warm sky and objects.
- Low-angle glints are visible but not noisy.
- Existing water tests remain stable or baselines are intentionally updated.

Validation:

- `tools\validate_renderers.bat`.

### Milestone 8: God Rays

Goal:

- Add visible light shafts from sun through clouds/depth occluders.

Work:

- Add low-resolution god-ray mask.
- Add radial blur pass.
- Composite into HDR before tonemap.
- Use depth and later cloud mask for occlusion.

Shaders:

- `post_godray_mask`.
- `post_godray_radial`.
- `post_godray_composite`.

Acceptance:

- Shafts originate from sun screen position.
- Terrain/balls can interrupt shafts.
- Shafts are warm and soft.
- No shafts when sun is off-screen unless explicitly designed.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat`.

### Milestone 9: Cloud Layer

Goal:

- Add cloud silhouettes and warm cloud edges.

Work:

- Add deterministic procedural cloud layer.
- Composite clouds with sky.
- Feed cloud mask to god rays.
- Add coverage/softness controls.

Shaders:

- `sky_clouds`.
- `sky_atmosphere` integration.
- `post_godray_mask` cloud input.

Acceptance:

- Clouds create bright sun-edge contrast.
- God rays vary through cloud gaps.
- Cloud pattern is stable for screenshots.

Validation:

- `tools\validate_renderers.bat`.

### Milestone 10: Volumetric Lighting

Goal:

- Replace or augment radial god rays with depth-aware volumetric scattering.

Work:

- Add low-resolution raymarch pass.
- Use depth to stop march at scene surfaces.
- Use sun phase function and fog density.
- Upsample with depth awareness.
- Composite into HDR.

Shaders:

- `post_volumetric_light`.
- Optional `post_volumetric_upsample`.

Acceptance:

- Shafts occupy scene depth, not only screen-space streaks.
- Basin haze becomes spatially richer.
- Perf remains acceptable at low/medium quality.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat`.

### Milestone 11: Volumetric Clouds

Goal:

- Add fuller 3D cloud bodies if the simpler cloud layer is not enough.

Work:

- Raymarch cloud density at reduced resolution.
- Use deterministic noise texture or fixed procedural noise.
- Add sun lighting and edge glow.
- Output cloud color and transmittance.
- Feed cloud transmittance to sky and volumetric lighting.

Shaders:

- `sky_volumetric_clouds` or `post_volumetric_clouds`.

Acceptance:

- Clouds look dimensional near sun.
- Cost is controlled by quality setting.
- Output is deterministic in validation mode.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat`.

## Backend-Specific Notes

### OpenGL

Use OpenGL for first implementation.

Expected work:

- Add RGBA16F framebuffer support.
- Add shader-readable depth texture support.
- Add fullscreen pass draw.
- Add bloom target allocation.
- Implement GLSL shaders first.

Risks:

- Texture coordinate origin differences.
- Depth reconstruction convention differs from DX.
- sRGB/gamma behavior must be explicit.

### DX11

Port after GL tonemap/bloom/fog are visually stable.

Expected work:

- Add HDR render target resources and SRVs.
- Add typeless depth resource if needed.
- Add fullscreen pass pipeline state.
- Implement HLSL versions of GL shaders.

Risks:

- Constant buffer layout alignment.
- Texture binding slots conflicting with existing water/reflection textures.
- Depth SRV setup.

### DX12

Port before volumetric features become mainline.

Expected work:

- Add HDR render target resource states.
- Add descriptor allocation for scene color, depth, bloom chain, god-ray buffers.
- Add barriers for render target to shader resource transitions.
- Add PSOs/root signatures or reuse existing shader abstraction if it handles this.
- Keep InfoQueue validation at zero errors.

Risks:

- Descriptor heap pressure.
- Incorrect barriers between render target, shader resource, UAV, and present states.
- Resource lifetime during resize/reset.
- Screenshot/readback path capturing before final resolve.

DX12 validation is non-negotiable. If DX12 starts reporting errors, stop feature work and fix barriers/descriptors before continuing.

## Visual Tuning Workflow

Use a fixed cinematic scene and tune in this order:

1. Camera composition and object placement.
2. Sun direction.
3. Exposure and tonemap.
4. Sun intensity.
5. Material ambient/bounce.
6. Bloom threshold and strength.
7. Sky horizon/zenith colors.
8. Distance fog.
9. Height fog.
10. Water reflection/glint.
11. God rays.
12. Clouds.
13. Volumetric scattering.

Do not tune bloom before HDR exposure is stable. Do not tune fog before depth reconstruction is verified. Do not tune volumetric lighting before the cheap god-ray pass proves the art direction.

## Validation And Acceptance

Required validation for implementation:

- Renderer/shader/scene visual changes: `tools\validate_renderers.bat`.
- Performance-sensitive post or volumetric changes: `tools\validate_perf.bat`.
- Broad uncertain integration: `tools\validate_full.bat`.

Acceptance criteria for the complete feature:

- GL, DX11, and DX12 all render the cinematic scene.
- DX12 validation output has zero errors.
- Cross-renderer pixel diff remains below repository threshold for validation scenes.
- Existing water reflection, skybox, terrain, shadow, UI, screenshot, and renderer tests remain valid.
- Cinematic scene visibly includes:
  - warm sunset sky,
  - bright sun disk,
  - HDR bloom,
  - golden terrain highlights,
  - readable red/yellow ball shading,
  - water reflection and glints,
  - distance/height haze,
  - cloud occlusion,
  - god rays or volumetric shafts.
- Quality settings can disable expensive effects.
- UI/text remain readable and unaffected by scene exposure.

## Implementation Guardrails

- Do not force a PBR rewrite before HDR/post exists.
- Do not implement DX12-only volumetrics.
- Do not add GL-only scene directives.
- Do not let UI render into the HDR scene unless deliberately redesigned.
- Do not let tonemap/gamma happen twice.
- Do not sample an LDR reflection texture as if it were HDR once reflections are upgraded.
- Do not accept backend-specific depth reconstruction drift.
- Do not make volumetrics full resolution by default.
- Do not update visual baselines casually. Any baseline update must be intentional and validated.
- Do not claim visual parity without renderer validation output.

## Suggested First Implementation Slice

The first code slice should be intentionally narrow:

1. Add cinematic scene, if scene-only validation budget is acceptable.
2. Add OpenGL HDR scene framebuffer.
3. Render existing scene into HDR framebuffer.
4. Add `post_tonemap` fullscreen resolve.
5. Render UI/text after resolve.
6. Add exposure and gamma settings.
7. Capture screenshots and compare current vs HDR path.

This slice creates the foundation for every later visual feature. Bloom, fog, sky, god rays, and clouds should wait until this foundation is stable.


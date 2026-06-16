# Non-Cinematic Photoreal Lighting Plan

Status: proposed  
Created: 2026-06-16  
Branch: `codex/dx12-render-graph-completion`  
Scope: ordinary DX12 lighting, object materials, terrain lighting, shadow
receivers, water reflection, color management, live render controls, and demo
material calibration

## Purpose

Make the current generated demo scene read more like photographed objects under
plausible light, without relying on the cinematic post stack.

The user clarified that the cinematic effects are off. This plan therefore
targets the ordinary render path first:

- instanced object shading in `SkullbonezData/shaders/lit_textured_instanced.hlsl`,
- terrain shading in `SkullbonezData/shaders/lit_textured.hlsl`,
- scene light constants in `SkullbonezSource/SkullbonezHelper.cpp` and
  `SkullbonezSource/SkullbonezTerrain.cpp`,
- shadow receiver behavior through `SkullbonezSource/SkullbonezShadow.h`,
- water shaders in `SkullbonezData/shaders/water_calm.hlsl` and
  `SkullbonezData/shaders/water_ocean.hlsl`,
- ordinary texture and presentation color handling in the DX12 backend and asset
  upload paths,
- a new in-game UI render tab for playing with ordinary lighting, shadow, water,
  color, and material calibration controls live.

The goal is not to add more spectacle. The goal is to make simple direct light,
ambient light, shadows, water, material response, and color management agree
with each other.

## Validation Rule

This plan changes renderer shaders, materials, texture/color handling, and
visual baselines. Each implementation slice that changes code or shaders must
run the DX12 renderer gate before commit:

```bat
tools\validate_dx12_renderer.bat
```

If a slice changes texture upload formats, render backend resource formats,
shader binding contracts, or validation tooling, run:

```bat
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
```

If a slice changes hot per-fragment shader cost enough to affect the demo scene
or broad batches, also run:

```bat
tools\validate_perf.bat
```

Documentation-only updates to this plan require no validation.

## Ground Rules

1. Keep cinematic effects optional.
   Do not make photoreal lighting depend on bloom, volumetric lighting, god
   rays, cinematic fog, or cinematic sky.

2. Keep the ordinary demo scene recognizable.
   Improve lighting and material behavior without replacing the scene with a
   set-dressed cinematic concept scene.

3. Use physically plausible response before adding new effects.
   PBR, hemispherical ambient, correct shadow separation, Fresnel water, and
   color-space correctness come before screen-space flourishes.

4. Keep authoring stable.
   Existing `object_material` directives, generated balls/boxes, and material
   kinds should keep loading. Material fields may be interpreted more accurately,
   but scene syntax should not break.

5. Keep DX12-only validation as the safety net.
   Do not add OpenGL or DX11 parity work. Renderer regressions are measured with
   DX12 screenshots and zero DX12 validation errors.

6. Avoid changing physics.
   The work is render-only unless a visual test scene needs camera or material
   directives. Physics deterministic baselines should not move.

7. Make tuning interactive.
   New ordinary render controls should be available from a dedicated in-game UI
   tab so the current demo can be tuned live without reloading the scene or
   resetting physics.

## Current State

Useful source anchors:

| Area | Current source | Notes |
|------|----------------|-------|
| Object shader | `SkullbonezData/shaders/lit_textured_instanced.hlsl` | Ordinary path is Phong/material-mode shading with hard-coded beachball/rim/glint behavior. |
| Object material data | `SkullbonezSource/SkullbonezRenderMaterial.h` | Roughness, metallic, specular, emissive, transmission, and material kind already exist in CPU payloads. |
| Object constants | `SkullbonezSource/SkullbonezHelper.cpp` | `ApplySceneLightConstants` currently uses the same scene color for ambient and diffuse in ordinary mode. |
| Terrain shader | `SkullbonezData/shaders/lit_textured.hlsl` | Ordinary path is Lambert plus Phong specular and flat ambient. |
| Terrain constants | `SkullbonezSource/SkullbonezTerrain.cpp` | Ordinary terrain uses config light color for both ambient and diffuse. |
| Scene light config | `SkullbonezData/engine.cfg` | Ordinary `scene_light_color` is warm/pink and not separated into sun, sky, and ground terms. |
| Shadows | `SkullbonezSource/SkullbonezShadow.h` and both lit shaders | Shadows already exist, but receiver math should block direct light while preserving ambient. |
| Water | `SkullbonezData/shaders/water_calm.hlsl`, `water_ocean.hlsl` | Reflection blend is fixed-strength tint/reflection mixing; glint is screen/reflection-space. |
| Color output | DX12 texture upload, swapchain format, and shader sampling paths | Needs audit for sRGB decode/encode and double-gamma risk. |
| UI tabs | `SkullbonezSource/UI/SkullbonezUI.h`, `SkullbonezSource/UI/SkullbonezUI.cpp`, `SkullbonezSource/UI/UICommands.h` | Existing tabs include Scene, Physics, Options, Controls, and Cine; Cine has reusable slider/toggle command patterns. |

Important existing advantages:

- The material payload already carries most parameters needed for a modest PBR
  object pass.
- The ordinary renderer already has real directional shadow maps.
- Water already has reflection input, so the first realism step can be Fresnel
  weighting rather than a new reflection system.
- The generated demo can be seeded, which gives repeatable visual comparisons.

Known realism blockers:

- Ambient and direct light are not separate physical concepts in ordinary mode.
- Object highlights are not energy-conserving and are partly hand-authored by
  material branch.
- Shadow darkness is mixed into shader branches rather than treated as "direct
  light visibility."
- Water does not know about view angle.
- Color-space handling is not documented end to end for ordinary rendering.
- Beachball and box defaults use visual compatibility values, not calibrated
  plastic/rubber/paint response.

## Definition Of Done

The plan is complete when:

- ordinary generated-demo objects use a simple energy-conserving BRDF,
- material roughness/specular/metallic fields visibly affect the ordinary path,
- ordinary terrain and objects share the same light model vocabulary,
- sky/ground ambient fills shadows without making direct shadows disappear,
- shadow maps block only direct light contribution,
- water reflection is Fresnel-weighted and does not depend on fake ordinary-path
  glints,
- ordinary color management is documented and uses one intentional transfer path,
- a new in-game Render tab exposes live ordinary lighting/material controls,
- beachball and box defaults are calibrated as plausible non-metal materials,
- DX12 visual baselines are intentionally updated if output changes,
- `dx12_validation.txt` reports zero DX12 validation errors,
- DX12 screenshot comparison passes.

## Phase 0: Baseline And Audit

Status: not started

Goal: create a stable before/after reference and confirm the exact ordinary
render path before touching shader math.

Work:

- Pick a repeatable demo launch for visual comparisons. Use a fixed seed and
  ordinary rendering with cinematic mode off.
- Capture at least four references:
  - wide generated demo view,
  - close ball view,
  - close box view,
  - shadow/contact view,
  - water/reflection view if water is visible in the chosen camera.
- Record whether the screenshots come from an existing validation scene, a new
  targeted scene, or the live generated demo.
- Inspect ordinary shader binding for:
  - light position type and direction,
  - ambient/diffuse constants,
  - material payload rows,
  - shadow map binding and receiver flags,
  - texture formats and sampler assumptions.
- Write a short note in this plan or a dated report if the ordinary path differs
  from the assumptions above.

Suggested commands:

```bat
Profile\SKULLBONEZ_CORE.exe --seed 1001 --shadows on --cinematic off --vsync off --hold
```

If a stable scene file is better than the live generated demo, create or reuse a
small renderer look-dev scene with the same material population and no physics
dependency.

Expected result:

- A repeatable reference set exists before code changes.
- The implementation knows whether ordinary frames use direct swapchain output
  only or any tonemap-like path.
- The material and color-space assumptions are written down before the math is
  changed.

Validation:

- Capturing references through a launch does not replace formal validation.
- No repository validation is required for documentation-only audit notes.

Commit boundary:

- Documentation/report commit if only the audit is recorded.
- No shader changes in this phase.

## Phase 1: Live Ordinary Render Controls Tab

Status: not started

Goal: add a dedicated in-game Render tab for ordinary, non-cinematic lighting
controls so the demo scene can be tuned live.

This is not the existing Cine tab. The Cine tab should continue to own
cinematic rendering, post effects, concept looks, and cinematic scene controls.
The new tab should own ordinary render realism controls that still apply when
cinematic rendering is off.

UI architecture anchors:

| Area | Source |
|------|--------|
| Tab enum and UI frame data | `SkullbonezSource/UI/SkullbonezUI.h` |
| Tab input/draw dispatch | `SkullbonezSource/UI/SkullbonezUI.cpp` |
| One-frame UI command structs | `SkullbonezSource/UI/UICommands.h` |
| Existing slider/toggle patterns | `SkullbonezSource/UI/SkullbonezUI.cpp`, `UITabOptions.*`, `UITabPhysics.*` |
| Runtime command consumption | `SkullbonezSource/SkullbonezRunScene.cpp` and neighboring run-loop code |

Work:

- Add a new `InGameUITab::Render` entry and a visible tab label, preferably
  `Render`.
- Keep the tab compact and practical. It should feel like a look-dev control
  panel, not a tutorial panel.
- Add a small `UITabRender.h/.cpp` pair if the tab grows beyond a few controls.
  Reusing the Cine slider implementation style is fine, but do not route these
  controls through `CinematicRenderConfig`.
- Add a render tuning state struct separate from `CinematicRenderConfig`, for
  example `OrdinaryRenderConfig` or `RenderLightingConfig`.
- Add `UIRenderTuningCommands` or similar one-frame command output in
  `UICommands.h`.
- Add the live render config snapshot to `InGameUIFrameData`.
- Have the run loop consume Render-tab commands and mutate the active ordinary
  render config without reloading the scene or resetting physics.
- Preserve the values across scene reset in the same spirit as existing live
  Cine-tab edits, unless the user chooses a "Reset Render" command.
- Add scene/config save support only after the live path works. Preferred
  follow-up:
  - `Reset Render` restores engine defaults,
  - `Save Defaults` writes scene-local ordinary render overrides when operating
    on scene files,
  - generated demo tuning can be exported to a style/config file later.

Initial controls:

| Group | Control | Type | Notes |
|-------|---------|------|-------|
| Lighting | Sun intensity | slider | Ordinary direct light strength. |
| Lighting | Sun color RGB | sliders or color-style rows | Keep ranges modest to avoid neon tuning. |
| Lighting | Sky ambient RGB/intensity | sliders | Hemispherical ambient upper term. |
| Lighting | Ground ambient RGB/intensity | sliders | Hemispherical ambient lower term. |
| Lighting | Ambient strength | slider | Scales sky/ground fill together. |
| Shadows | Shadows enabled | toggle | Existing scene option can remain, but Render tab should expose the ordinary lighting intent. |
| Shadows | Strength/direct visibility | slider | Prefer 1.0 physically, but useful while tuning. |
| Shadows | Softness/PCF radius | slider or stepper | Keep bounded to avoid hiding aliasing with blur. |
| Shadows | Bias | slider | Small range; label as bias, not as an art control. |
| Shadows | Slope bias | slider | Small range; advanced but useful. |
| Water | Fresnel F0 | slider | Start around 0.02. |
| Water | Reflection strength scale | slider | Multiplies Fresnel result, not fixed blend replacement. |
| Water | Tint/absorption RGB | sliders | Ordinary water color. |
| Water | Roughness/wave normal strength | slider | If ocean normal approximation lands. |
| Color | Exposure or output brightness | slider | Only after Phase 6 decides the ordinary output transfer. |
| Color | Gamma/display transfer debug | slider or toggle | Debug/tuning only if needed; avoid making it a fake grade. |
| Materials | Ball roughness | slider | Runtime multiplier for generated/demo balls. |
| Materials | Ball specular | slider | Runtime multiplier for generated/demo balls. |
| Materials | Box roughness | slider | Runtime multiplier for generated/demo boxes. |
| Materials | Box specular | slider | Runtime multiplier for generated/demo boxes. |

Controls to avoid in this tab:

- Bloom, god rays, volumetric lighting, cinematic fog, cloud coverage, and
  concept style modes. Those remain Cine-tab controls.
- Physics settings.
- Renderer backend selection. DX12 is the only runtime renderer.
- Huge color grading controls that hide lighting mistakes.

Expected result:

- The user can open the in-game UI, switch to Render, and tune ordinary lighting
  while the current demo keeps running.
- Render-tab changes affect terrain, objects, shadows, and water as matching
  implementation phases land.
- Cine mode remains separate and optional.
- UI controls do not mutate engine state directly; they emit commands consumed
  by the run loop.

Validation:

For a UI-only skeleton that does not change renderer behavior:

```bat
tools\validate_fast.bat
```

Once the tab controls live renderer behavior or shader/config contracts:

```bat
tools\validate_dx12_renderer.bat
```

If the UI slice and renderer slice land together, run both:

```bat
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for Render-tab skeleton and command plumbing.
- One commit for live ordinary render config application.
- Later implementation phases may add individual controls as the corresponding
  shader/runtime feature lands.

Extra review points:

- Text must fit in the existing UI window at default size.
- The tab bar must still fit after adding `Render`.
- Slider hitboxes and draw geometry must stay aligned.
- Keyboard/mouse capture should behave like existing tabs.
- Saving scene defaults should not accidentally write cinematic controls when
  only Render-tab controls changed.

## Phase 2: Object PBR Foundation

Status: not started

Goal: replace ordinary object Phong/material-mode lighting with a compact,
energy-aware BRDF while keeping existing batching and material payloads.

Work:

- Add shared shader helpers inside `lit_textured_instanced.hlsl`:
  - `SaturateDot`,
  - Schlick Fresnel,
  - GGX normal distribution,
  - Smith or Schlick-GGX visibility,
  - diffuse Lambert term,
  - dielectric F0 from specular value,
  - metallic blend between albedo and F0.
- Use existing instance fields:
  - `material0.rgb` as base color/albedo multiplier,
  - `material1.x` as roughness,
  - `material1.y` as metallic,
  - `material1.z` as specular/F0 strength,
  - `material2.rgb * material1.w` as emissive.
- Clamp roughness to a nonzero floor, for example `0.045`, to avoid sparkling
  and divide-by-zero behavior.
- Treat all ordinary material modes through the same BRDF first.
- Preserve procedural beachball color generation, but remove beachball-specific
  ordinary-path rim/glint/wrap additions.
- Keep stylized material modes available for authored concept scenes, but do not
  let the default generated demo use stylized branches for realism.
- Ensure transparent/debug body rendering still receives alpha correctly.
- Add comments only where the BRDF helpers need orientation; avoid turning the
  shader into a textbook.

Suggested object shader shape:

```text
baseColor = material albedo or procedural beachball albedo
F0 = lerp(0.04 * specularScale, baseColor, metallic)
direct = (diffuse + specular) * lightColor * NdotL * shadowVisibility
ambient = baseColor * hemisphereAmbient
emissive = emissiveColor * emissiveStrength
out = ambient + direct + emissive
```

Expected result:

- Roughness visibly controls highlight width.
- Specular controls dielectric highlight intensity.
- Metallic works for explicit metal materials without corrupting default balls.
- Beachballs look like painted/plastic spheres instead of lit color panels.
- Existing scene/style material directives continue to parse and render.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for ordinary object BRDF conversion.

Rollback:

- Restore the previous ordinary-path branch in `lit_textured_instanced.hlsl`.
- Keep any audit/reference captures.

## Phase 3: Hemispherical Ambient For Objects And Terrain

Status: not started

Goal: replace flat ambient with plausible sky/ground fill in ordinary mode.

Work:

- Add a small ambient model to both object and terrain shaders:
  - upward-facing normals receive more sky color,
  - downward-facing normals receive darker/warmer ground bounce,
  - side-facing normals interpolate smoothly.
- Add CPU-side ordinary ambient constants if needed. Preferred first version:
  derive them from existing scene light config to avoid broad config/parser work.
- Better follow-up version:
  add explicit ordinary lighting config keys:
  - `scene_sun_color_*`,
  - `scene_sun_intensity`,
  - `scene_sky_ambient_*`,
  - `scene_ground_ambient_*`.
- Stop using the same `scene_light_color` as both ambient and diffuse in
  ordinary mode.
- Apply the same ambient convention to:
  - `lit_textured_instanced.hlsl` for objects,
  - `lit_textured.hlsl` for terrain.
- Keep ambient independent from shadow visibility.

Suggested first-pass values:

| Term | Intent |
|------|--------|
| Sky ambient | cool, desaturated blue-grey, low intensity |
| Ground ambient | warm brown/green, darker than sky |
| Direct light | neutral to warm sun key, stronger than either ambient |

Expected result:

- Shadowed object sides remain readable without looking self-illuminated.
- Terrain slopes have more photographic shape.
- Objects and terrain feel like they occupy the same outdoor light.
- The ordinary demo no longer depends on pink ambient wash.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for shader ambient changes.
- Separate commit if config parser/schema changes are introduced.

Extra review points:

- Make sure transparent debug bodies still read clearly.
- Avoid lifting ambient so high that shadows lose contact.
- Avoid over-saturated sky ambient; subtle is more realistic.

## Phase 4: Shadow Direct-Light Separation And Contact Quality

Status: not started

Goal: make shadows physically meaningful by applying shadow visibility only to
direct light, while ambient fill remains unshadowed.

Work:

- Audit every ordinary shader use of `ShadowVisibility`.
- Refactor object and terrain lighting so:
  - ambient contribution is computed first and never multiplied by shadow,
  - direct diffuse and direct specular are multiplied by shadow visibility,
  - emissive is never shadowed.
- Rename local variables where helpful:
  - `shadowFactor` to `directVisibility`,
  - `ambient` to `ambientIrradiance` or similar if it clarifies intent.
- Tune ordinary shadow config after Phase 2 and Phase 3:
  - map size,
  - PCF radius,
  - bias,
  - slope bias,
  - strength.
- Prefer physically correct direct visibility over artistic shadow strength.
  If `shadowStrength` remains less than 1.0, document why.
- Inspect object-on-object and object-on-terrain contact in screenshots.
- If the shadow map projection is too wide for the generated demo, tighten the
  light frustum before increasing map size.

Expected result:

- Direct sun is blocked in shadow.
- Ambient sky/ground fill keeps shadowed areas visible.
- Contact shadows make balls and boxes feel grounded.
- Shadow acne and peter-panning stay below visible threshold.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for shader shadow separation.
- Separate commit for shadow frustum/config tuning if the diff touches renderer
  backend or config defaults.

Extra review points:

- `water_ball_test.scene` and `shadow_contact.scene` should be inspected because
  they exercise terrain, water, object, and shadow ordering.
- If a visual baseline update is intentional, include before/after images or a
  report explaining why the new shadow is correct.

## Phase 5: Fresnel Water And Reflection Calibration

Status: not started

Goal: make ordinary water respond to view angle and reflected scene intensity
instead of fixed reflection blending and fake glints.

Work:

- Add view or camera information to water shader inputs if needed.
  Current water vertex outputs carry reflection coordinates and XZ position but
  do not expose a complete normal/view vector model.
- For calm water:
  - use a flat normal,
  - compute `VdotN`,
  - apply Schlick Fresnel with water F0 near 0.02,
  - blend tint/transmission and reflection through Fresnel.
- For ocean water:
  - approximate a wave normal from the same sine waves used for displacement,
  - use that normal for Fresnel and reflection perturbation,
  - keep existing reflection FBO sampling.
- Remove ordinary-path fake screen/reflection-space glint.
  If cinematic mode still wants authored glints, keep it behind cinematic mode.
- Calibrate water tint as absorption, not a fixed paint overlay:
  - shallow/flat view should show more tint,
  - grazing view should show more reflection.
- Clamp reflection UVs or edge behavior intentionally so out-of-frame reflection
  samples do not create obvious artifacts.

Expected result:

- Looking across water produces stronger reflection.
- Looking down at water produces more tint/transmission.
- Water no longer has a highlight that ignores camera/light geometry.
- Existing reflection toggle and no-reflection pass still work.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for calm water Fresnel.
- One commit for ocean normal/Fresnel if the ocean path needs additional data.

Extra review points:

- The known water/back-face intersection issue is related but not the core
  purpose of this plan. Do not mix that fix into the first Fresnel slice unless
  it blocks validation.
- Confirm reflection pass does not recursively sample water.

## Phase 6: Ordinary Color Management Audit And Fix

Status: not started

Goal: make ordinary rendering use one intentional color-space path from texture
load to lighting math to presented pixels.

Work:

- Audit texture creation for color textures:
  - terrain texture,
  - sphere/beachball texture if still sampled,
  - skybox textures,
  - UI/debug textures if they share upload paths.
- Determine whether the DX12 texture formats use sRGB variants for albedo data.
- Determine whether shader samples are already linearized by hardware or are
  being treated as linear despite being authored in sRGB.
- Determine whether the swapchain/backbuffer format applies sRGB conversion or
  the shader/output path writes display-encoded values manually.
- Add a short reference note if the path is currently correct but undocumented.
- If incorrect:
  - use sRGB texture formats for albedo/color textures,
  - keep normals/data textures linear,
  - keep lighting math linear,
  - apply exactly one final linear-to-display transfer.
- Avoid blindly adding gamma correction in shaders. First prove where conversion
  is missing.
- Keep UI text and debug overlays readable after the output transfer decision.

Expected result:

- Midtones no longer look crushed, washed, or doubly gamma-corrected.
- Albedo values can be calibrated predictably.
- Ordinary and validation screenshots are reproducible across the same display
  path.

Validation:

```bat
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
```

Run `validate_fast` here if backend texture format contracts, shader contracts,
or asset loading code change.

Commit boundary:

- One commit for color-space audit docs if no code changes are needed.
- One commit for texture/swapchain/shader color-space fixes if code changes are
  needed.

Extra review points:

- Check screenshots before and after with a neutral grey/mid-bright object if
  possible.
- Confirm UI colors are not unintentionally darkened by an output transfer
  change.

## Phase 7: Beachball And Box Material Calibration

Status: not started

Goal: make the default generated objects read as plausible real materials after
the lighting model is fixed.

Work:

- Tune default generated ball material:
  - non-metallic,
  - base color below pure 1.0,
  - roughness in a plastic/rubber range,
  - dielectric F0 around 0.04,
  - moderate specular.
- Tune default generated box material:
  - matte or painted material,
  - higher roughness than balls,
  - no metallic unless explicitly authored.
- Keep authored material presets usable:
  - `metal` and `chrome` should still be shiny,
  - `emissive` should still emit,
  - `glass` may remain approximate until a later transmission/refraction plan.
- Update CPU defaults in `SkullbonezRenderMaterial.h` only after the shader
  interpretation is stable.
- Consider a small style or scene override for the current demo only if global
  material defaults would break existing concept scenes.
- Avoid calibrating under a broken color pipeline. This phase should follow
  Phase 6 unless Phase 6 proves no color-space fix is needed.

Suggested starting values:

| Material | Metallic | Roughness | Specular/F0 intent |
|----------|----------|-----------|--------------------|
| Beachball/plastic | 0.0 | 0.38 to 0.58 | dielectric around 0.04 |
| Rubber-like ball | 0.0 | 0.62 to 0.82 | low specular |
| Painted box | 0.0 | 0.55 to 0.75 | modest dielectric |
| Matte debug box | 0.0 | 0.80 to 0.95 | low specular |

Expected result:

- Balls have plausible highlights but are not mirror-polished.
- Boxes look like matte/painted solids instead of flat color cubes.
- Material differences are visible under the same ordinary light.
- Concept scenes retain their authored material identities.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for material default calibration.
- Include before/after screenshots in the commit note or report if baselines
  move.

## Phase 8: Baseline Updates And Final Renderer Report

Status: not started

Goal: intentionally update visual expectations after the photoreal ordinary path
is stable.

Work:

- Re-run the reference captures from Phase 0.
- Compare:
  - generated demo wide shot,
  - close ball,
  - close box,
  - contact shadow,
  - water/reflection.
- Update committed DX12 visual baselines only when changes are expected and
  reviewed.
- Add a dated report under `Agentic/Reports/` with:
  - changed files,
  - screenshots or artifact paths,
  - lighting/material behavior summary,
  - validation command output,
  - any remaining known realism limitations.
- Update this plan's statuses or move it to `Agentic/Plans/Done/` after the
  final validated commit lands.

Expected result:

- The repo records why the visual output changed.
- Future agents can distinguish intentional lighting improvements from renderer
  regressions.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

If the implementation touched several renderer subsystems or color formats,
finish with:

```bat
tools\validate_full.bat
```

Commit boundary:

- One final docs/report/baseline commit after validation.

## Suggested Commit Sequence

1. `docs: capture ordinary lighting audit`
2. `feat: add ordinary render tuning tab`
3. `feat: add pbr object lighting to ordinary path`
4. `feat: add hemisphere ambient to ordinary lighting`
5. `fix: separate shadow visibility from ambient fill`
6. `feat: add fresnel water reflection`
7. `fix: correct ordinary render color management`
8. `tune: calibrate default demo materials`
9. `docs: record non-cinematic photoreal lighting validation`

Each commit body should mention:

- whether cinematic mode was involved,
- which ordinary path changed,
- what material/light behavior changed,
- which screenshots or baselines moved,
- validation command and meaningful result.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| PBR shader cost affects perf | Keep helper math compact, avoid branches in the ordinary hot path where possible, run `validate_perf` if needed. |
| Existing concept materials lose their authored look | Apply the physically plausible path to ordinary/default materials first, and leave stylized branches behind explicit material modes. |
| Shadows become too dark | Keep ambient unshadowed and calibrate ambient before increasing shadow strength. |
| Shadows become too soft or detached | Tune bias/frustum before increasing PCF radius or map size. |
| Water Fresnel needs data not currently passed | Add the smallest necessary view/camera uniform instead of replacing the water pipeline. |
| Color management changes every screenshot | Audit first, then make one transfer-path fix with explicit baseline review. |
| UI/debug overlays shift after color fix | Inspect overlays and keep UI color handling intentional. |
| Material calibration masks lighting bugs | Do material tuning after BRDF, ambient, shadows, and color-space decisions are stable. |

## Open Design Choices

Resolve these during Phase 0 or before the affected implementation phase:

- Should ordinary mode add explicit sky/ground ambient config keys, or should the
  first version derive ambient from existing scene light values?
- Should the Render tab own a new `OrdinaryRenderConfig`, or should it initially
  mutate a narrower runtime-only tuning overlay before config/schema work lands?
- Should Render-tab `Save Defaults` write scene-local ordinary render directives,
  engine config defaults, or an exported style file?
- Should the object and terrain BRDF helpers be duplicated in HLSL files for now,
  or factored into a shared include if the shader build supports includes cleanly?
- Should the ordinary renderer use a neutral filmic curve, swapchain sRGB, or a
  shader-side linear-to-sRGB conversion?
- Should water receive true camera world position, view-space position, or a
  cheaper derived view vector?
- Should the default generated demo use global material defaults, or should it
  receive an explicit ordinary-realism style descriptor?

Preferred initial answers:

- Add explicit ambient config only after a small derived-ambient prototype proves
  the look.
- Add a runtime ordinary render config first, then persist it after the live
  tuning surface feels right.
- Keep `Save Defaults` scene-local for authored scenes; treat generated demo
  export as a later workflow so live tuning does not unexpectedly edit global
  engine config.
- Duplicate minimal BRDF helpers first if shader include plumbing adds risk.
- Audit color management before choosing any output transfer change.
- Pass camera/world or view data to water only if a flat-normal approximation is
  not enough.
- Prefer global material default calibration if it does not break validation
  scenes; otherwise use a demo-specific style.

## Final Acceptance Checklist

- [ ] Current non-cinematic demo has before/after reference captures.
- [ ] In-game UI has a Render tab for live ordinary lighting/material tuning.
- [ ] Render-tab controls mutate ordinary render settings without resetting
      physics or reloading the scene.
- [ ] Ordinary object shader uses roughness/specular/metallic in a PBR-style
      direct-light equation.
- [ ] Procedural beachball color remains crisp but no longer uses hard-coded
      ordinary rim/glint lighting.
- [ ] Terrain and objects share a compatible sun plus sky/ground ambient model.
- [ ] Shadow visibility affects direct light only.
- [ ] Water reflection uses Fresnel weighting in ordinary mode.
- [ ] Ordinary color-space path is documented and has no known double-gamma or
      missing-gamma issue.
- [ ] Default generated balls and boxes have plausible non-metal material
      defaults.
- [ ] Intentional DX12 screenshot baseline changes are recorded.
- [ ] `dx12_validation.txt` reports zero DX12 validation errors.
- [ ] `tools\validate_dx12_renderer.bat` passes for the final renderer slice.

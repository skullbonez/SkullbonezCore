# Shadow Edge Quality - Plan

## Problem

Player-scale and object-scale shadows can look pixelated because the receiver
sees too little shadow-map detail and the current filter only averages a small
square of point-sampled depth comparisons. The visual target is the PC-style
soft silhouette reference from Call of Duty: readable caster shape, soft
feathered edge, no obvious block steps, and stable motion under camera changes.

## Current Implementation Notes

- `ShadowPass` builds two maps in `SkullbonezSource/Runtime/RunPasses.cpp`.
  The terrain frame is broad and terrain-centered, while the object frame is a
  tighter nearby-object map centered around the render look target.
- Terrain currently receives only the broad terrain shadow frame through
  `TerrainPass::Render`.
- Object receivers use the tighter object frame, which is why object-on-object
  shadows have better texel density than object-on-terrain contact shadows.
- `lit_textured.hlsl` and `lit_textured_instanced.hlsl` implement manual PCF by
  sampling `uShadowMap` with the point shadow sampler and averaging a square
  kernel clamped to radius 3.
- Default shadow quality is intentionally modest: 2048 map size, radius 1, and
  softness near 1.0.

## Goal

Make near object shadows on terrain and objects feel soft and high-density
without hiding aliasing behind excessive blur. The finished path should support
near/contact quality first, then leave a clean route to cascaded sun shadows if
terrain-wide shadowing still needs more detail.

## Non-Goals

- Do not add OpenGL or DX11 shadow paths. DX12 is the production renderer.
- Do not hide the issue by only raising default shadow-map resolution.
- Do not add per-frame heap growth in render hot paths.
- Do not replace the renderer abstraction with DX12-only call sites outside the
  backend boundary.

## Implementation Phases

| ID | Phase | Status | Validation |
|----|-------|--------|------------|
| `S0` | Baseline current aliasing and choose acceptance scenes | Pending | Screenshot/manual capture only |
| `S1` | Feed the tight object shadow map into terrain receivers | Pending | Targeted launch, then `tools\validate_dx12_renderer.bat` at PR gate |
| `S2` | Replace square point PCF with stable Poisson/contact-soft filtering | Pending | `tools\validate_dx12_renderer.bat`, add `tools\validate_perf.bat` if sample count increases materially |
| `S3` | Add texel snapping and quality presets | Pending | `tools\validate_dx12_renderer.bat` |
| `S4` | Decide whether cascaded/clipmap shadows are still needed | Pending | Plan-only unless implemented |

## Phase Details

### S0 - Baseline and Acceptance

1. Pick one small scene with a player/object silhouette shadow on terrain and
   one stress scene with many moving objects.
2. Capture current frame output and the Terrain/Object Shadow Depth debug
   previews.
3. Record the shadow-map settings used by each scene: map size, PCF radius,
   strength, softness, depth bias, slope bias, and max distance.
4. Define visual acceptance:
   - player-sized silhouette has no visible square stair steps at normal camera
     distance;
   - shadow edge is soft but still preserves head/shoulder/body shape;
   - camera orbit/zoom does not produce obvious crawling;
   - no self-shadow acne or detached peter-panning at contact points.

### S1 - Tight Object Shadows on Terrain

The broad terrain map spreads texels across the whole authored terrain. Reuse
the existing tight object shadow frame for terrain contact shadows instead of
depending only on the broad terrain map.

1. Extend terrain receiver data so TerrainPass can see both:
   - the broad terrain shadow frame for terrain/world coverage;
   - the tight object shadow frame for nearby object contact shadows.
2. Add a second optional shadow-map binding for terrain receivers. The current
   ABI uses t3 for shadow depth and t4 for the object material table, so this
   will require a deliberate texture-slot/root-signature expansion or an atlas
   design. Do not steal t4.
3. Update `lit_textured.hlsl` to combine broad and tight visibility by taking
   the darker result where both maps are valid.
4. Keep object receivers on the existing object shadow frame unless the filter
   work proves they need a separate path.
5. Make resource lifetime explicit in `ShadowPassResources` and clear disabled
   receiver bindings so stale descriptor rows cannot leak between passes.

### S2 - Better Filtering

The current square kernel softens a jagged mask but still reveals regular grid
structure. Replace it with a stable, art-directed filter.

1. Create a shared shadow sampling helper or keep the terrain/object shader
   copies mechanically identical if the shader toolchain does not support
   includes cleanly.
2. Start with Poisson disk PCF:
   - fixed 12 or 16 tap offsets;
   - small stable per-pixel rotation from world position or screen position;
   - sample radius driven by `shadowSoftness` and map texel size.
3. Add contact-hardening PCSS only after Poisson PCF is stable:
   - blocker search radius stays small near contact;
   - penumbra grows with receiver/blocker separation;
   - clamp penumbra so silhouettes do not become foggy blobs.
4. Consider a DX12 comparison sampler and `SampleCmp` for hardware bilinear PCF
   if it improves quality/perf with the existing depth SRV format.
5. Keep all new constants in the existing shadow config surface or in clearly
   named domain settings, not a generic compatibility bag.

### S3 - Stability and Presets

1. Snap tight shadow-map light-space centers to shadow texels to reduce crawling.
2. Add quality presets or scene/config fields only after the filter behavior is
   proven:
   - `High`: 4096 map, Poisson PCF, moderate softness;
   - `Ultra`: 8192 or stronger filtering for screenshots/cinematic scenes.
3. Retune default bias values with the new filter. Bias should remove acne
   without visibly lifting the shadow off contact.
4. Keep debug previews readable for both broad and tight maps.

### S4 - Cascaded/Clipmap Decision

If broad terrain shadows still alias after S1-S3, design a cascaded or clipmap
sun-shadow path:

1. 2-3 cascades first, not a full general-purpose system.
2. Camera-centered near cascade for player/contact detail.
3. Larger mid/far cascades for terrain and big silhouettes.
4. Stable cascade snapping and cross-fade bands to avoid popping.

## Likely Files

| Area | Files |
|------|-------|
| Shadow pass data and rendering | `SkullbonezSource/Runtime/RunPasses.cpp`, `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`, `SkullbonezSource/Rendering/Shadow.h` |
| Terrain/object receiver shaders | `SkullbonezData/shaders/lit_textured.hlsl`, `SkullbonezData/shaders/lit_textured_instanced.hlsl` |
| DX12 binding ABI if adding a second map | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`, `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`, shader contract declarations |
| Config and scene knobs | `SkullbonezSource/Core/Config.h`, `SkullbonezSource/Core/Config.cpp`, scene parser/runtime style files, selected scene JSON |
| Validation assets | `TestOutput/baselines/*.png` only when intentional visual output changes are accepted |

## Risks and Guardrails

- Root signature changes can break all lit shaders. Expand the slot contract
  deliberately and update shader validation coverage.
- Higher sample counts can hit GPU cost. Run `tools\validate_perf.bat` when the
  filter exceeds the current 3x3/5x5-ish budget.
- Soft filtering can hide peter-panning until motion exposes it. Test moving
  shadows, not just still screenshots.
- Existing depth framebuffer format is `R24G8_TYPELESS` with an
  `R24_UNORM_X8_TYPELESS` SRV. Confirm comparison-sampler support before
  committing to `SampleCmp`.
- Comment quality applies to touched source-bearing files; inspect changed
  source with `Agentic/Skills/comment-style-audit/skill.md` before reporting
  implementation complete.

## PR-Gate Validation

This plan is documentation-only, so no validation is required for the plan
itself. Implementation touches renderer and shader behavior, so the expected PR
gate is:

```bat
tools\validate_dx12_renderer.bat
```

Add this when sample counts, extra maps, or hot render paths materially change:

```bat
tools\validate_perf.bat
```

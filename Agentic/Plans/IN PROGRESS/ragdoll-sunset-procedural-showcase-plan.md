# Ragdoll Sunset Procedural Showcase Plan

Source request: create a plan for a cinematic ragdoll, ball, and brick wall scene matching the warm sunset reference, using procedural rendering only for now. Painted skies, photo textures, HDRIs, and mountains are deferred.

## Current Status

- Status: Not started.
- Scope: create a procedural cinematic sunset showcase variant of the existing ragdoll, ball, and brick wall setup.
- Impact area: scene data, terrain shader/materials, sky and post-processing shaders, shadow tuning, cinematic scene defaults, and visual QA.
- Non-goals: no painted skybox, no external HDRI/photo texture, no hand-painted mountain backdrop, and no full terrain sculpting pass.
- Target first frame: paused scene with the ball in the left foreground, ragdoll in front of the wall, brick wall on the right, low warm sun near the horizon, readable long shadows, warm procedural floor, and visible atmospheric glow.

## Checklist

- [ ] Create a new showcase scene in `SkullbonezData/scenes/`, proposed name: `aaa_ragdoll_sunset_showcase.scene.json`.
- [ ] Reuse the proven ball, ragdoll, and brick wall placement and physical properties from the current ragdoll showcase setup.
- [ ] Start the scene paused in cinematic mode with the ball, ragdoll, bricks, sun, and terrain visible before simulation advances.
- [ ] Lower the ball by the already requested small amount so it impacts the ragdoll lower than the earlier version.
- [ ] Frame the start camera like the reference: low wide angle, ball left foreground, ragdoll near center-right, wall right midground, sun just above or behind the wall line.
- [ ] Build a procedural sky mode with warm orange horizon color, rose-purple upper sky, sun disk, sun glow, and soft procedural cloud/noise bands.
- [ ] Add or extend a procedural atmospheric haze mode so the horizon has depth without painted mountains.
- [ ] Tune god rays or volumetric light shafts using the wall and ragdoll as occluders, with rays aimed broadly from the wall/sun direction toward the camera.
- [ ] Extend the terrain shader so a scene can request a solid/procedural material and ignore texture sampling.
- [ ] Create a warm flat terrain material: physically flat, visually rough, with procedural color variation, fine noise, tiny pits, and crack-like breakup.
- [ ] Keep terrain collision immovable and flat; all floor detail must be shader-side only.
- [ ] Tune object materials for contrast: tan/off-white bricks, dark glossy graphite ball with a sun highlight, and the existing blue/purple ragdoll colors under warm light.
- [ ] Fix the wall shadow presentation for the showcase so the wall casts a readable long shadow without a highly pixelated 200-brick silhouette.
- [ ] Prefer a non-physics shadow proxy or tighter shadow/receiver setup for wall presentation if individual-brick shadows are too noisy.
- [ ] Tune sun azimuth/elevation, exposure, bloom, tone mapping, ambient level, and shadow darkness until the scene has the same warm readable contrast as the reference.
- [ ] Capture deterministic start-frame screenshots during iteration and keep the final evidence under `TestOutput/visual_validation/ragdoll_sunset_showcase/`.
- [ ] Capture at least one post-play screenshot or short run artifact to prove the scene still advances from the paused setup correctly.

## Likely Files And Tools To Inspect

- `SkullbonezData/scenes/aaa_ragdoll_clean_sky.scene.json`
- Existing `SkullbonezData/scenes/aaa_ragdoll_*.scene.json` showcase variants.
- `SkullbonezData/assets/*.assets.json` for registered brick wall or reusable scene asset definitions.
- `SkullbonezData/shaders/lit_textured.hlsl` for terrain/object material behavior.
- `SkullbonezData/shaders/post_tonemap.hlsl` for exposure, contrast, bloom, and color response.
- `SkullbonezData/shaders/post_volumetric_light.hlsl` or equivalent volumetric/god-ray shader if present.
- `SkullbonezSource/Runtime/RunPasses.cpp` for scene-driven render pass and cinematic settings.
- Scene parser/config code that owns sky, terrain, shadow, cinematic, and post-processing fields.
- Runtime launch path for visual checks: `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json`.

## Implementation Notes

- Use procedural shader math for sky, clouds, haze, and terrain variation. Do not introduce painted textures or generated bitmap backgrounds for this pass.
- Defer mountains entirely unless the user later asks for procedural silhouettes or distant terrain.
- Keep the floor visually interesting but collision-simple: one giant immovable flat terrain surface.
- Keep the brick wall physically made of bricks, but allow a separate visual/shadow strategy if the physically accurate per-brick shadow is too aliased for the showcase view.
- Any reusable wall, proxy, or placeable object should be registered through the asset library rather than hardcoded as an editor-only compound object.
- Keep settings scene-local where possible so the showcase look does not unexpectedly recolor unrelated scenes.
- If shader parameters are added, expose them through scene data with defaults that preserve current scenes.

## Validation Plan

- This document-only planning change requires no repository validation.
- During implementation, use targeted Profile builds after code or shader changes.
- Required visual checks: inspect the paused start frame, capture screenshots during iteration, and compare the final frame against the reference composition and contrast.
- If only scene data changes, run `tools\validate_full.bat` before a PR-bound commit because `SkullbonezData/scenes/*.scene.json` maps to full validation.
- If shader or renderer behavior changes, run `tools\validate_dx12_renderer.bat`.
- If the final implementation mixes scene, shader, and runtime changes, prefer `tools\validate_full.bat` plus the targeted DX12 renderer gate if shader changes are included.
- Any touched source-bearing files need the repository comment-quality audit before final handoff.

## Open Risks And Questions

- The current low-angle wall shadow may be pixelated because the terrain receiver covers a large area while the 200 individual bricks each cast small silhouettes.
- A shadow proxy may be the best showcase compromise, but it must not affect physics or collision behavior.
- Existing cinematic sky support may not yet have enough procedural cloud and haze controls to hit the reference without shader work.
- God rays may need stronger volumetric density, better occluder alignment, and a lower sun elevation than the current clean-room scene.
- Bloom/exposure must be tuned carefully so the sun glows while the wall, ragdoll, and ball remain readable.
- The warm floor needs material breakup, but too much procedural contrast could distract from the physics scene.

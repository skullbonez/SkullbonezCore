# Ragdoll Sunset Procedural Showcase Progress

Purpose: give an agent a concrete checklist to complete the procedural sunset showcase tonight.

Parent plan: `Agentic/Plans/IN PROGRESS/ragdoll-sunset-procedural-showcase-plan.md`

## Hard Constraints

- [x] Use only procedural rendering for the showcase look. No painted skyboxes, photo textures, HDRIs, generated background bitmaps, or hand-painted mountain cards.
- [x] Keep the terrain physically flat, giant, immovable, and stable.
- [x] Keep the ball, ragdoll, and brick wall physical setup based on the current approved ragdoll scene.
- [x] Start the scene paused, cinematic, and already framed for presentation.
- [x] Preserve existing scenes by putting new behavior behind scene-local settings or safe defaults.
- [x] If source-bearing files are touched, run the comment-quality audit before final handoff.

2026-06-30 note: `aaa_ragdoll_sunset_showcase.scene.json` was created as the
smallest scene-local path from `aaa_ragdoll_clean_sky.scene.json`: same
registered `building.brick_wall_200` wall instance, same ragdoll setup, and the
lowered striker ball moved from `y=12.8` to `y=12.45`. Source changes stayed
default-off or opt-in: `sky_atmosphere.hlsl` adds sky style mode `20` for
open-horizon procedural skies, `RunUiTextPass.cpp` lets clean capture runs hide
the runtime mode badge through the existing `--hide-top-text` switch, and the
scene material matchers now let exact/prefix targets style a named ragdoll while
broad material targets still skip ragdoll parts. The scene uses terrain style
mode `13` with scene-authored `terrainTint`, `terrainAccent`, and `terrainGrid`
values so the floor remains procedural and physically flat without changing
renderer defaults.

## Definition Of Done Tonight

- [x] A new scene exists in `SkullbonezData/scenes/` named `aaa_ragdoll_sunset_showcase.scene.json` or an equally explicit `aaa_` showcase name.
- [x] The scene opens paused with the ball left foreground, ragdoll near the wall, wall right midground, and sun low behind or near the wall.
- [x] The floor is procedural terrain, not grass, not textured, and not falling.
- [x] The sky reads as a warm procedural sunset: orange horizon, rose/purple upper sky, bright sun disk, and atmospheric glow.
- [x] Shadows are long, dark, readable, and not dominated by chunky pixelated brick-wall artifacts.
- [x] Some visible god-ray or volumetric haze effect is present from the sun/wall direction toward the camera.
- [x] A final start-frame screenshot is saved under `TestOutput/visual_validation/ragdoll_sunset_showcase/`.
- [x] The final handoff names every changed file, the exact screenshot path, validation run, and remaining visual compromises.

## Phase 0 - Preflight

- [x] Run `git status --short --branch` and record pre-existing dirty files as user-owned.
- [x] Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`.
- [x] Read the parent plan linked above.
- [x] Locate the current best ragdoll scene and the ball/ragdoll/wall physical settings to reuse.
- [x] Locate the terrain shader, sky/post shaders, scene parser fields, and cinematic scene controls.
- [x] Decide the smallest validation gate based on the actual files changed.

## Phase 1 - Scene Skeleton And Framing

- [x] Create the new `aaa_` scene by copying the current best ragdoll setup, not the deleted/bad historical variants.
- [x] Keep the same brick wall placement and stable physical properties from the approved current setup.
- [x] Lower the ball a few centimeters from the last working position so it hits the ragdoll lower.
- [x] Increase ball speed only if the current approved setup still needs the stronger impact from the earlier request. Retained the approved `650.0` X velocity; no extra speed increase was needed for this slice.
- [x] Set the scene to start paused.
- [x] Enable cinematic mode from scene startup.
- [x] Set camera to a low wide composition matching the reference: ball left foreground, ragdoll before wall, wall right, sun visible above/near wall.
- [x] Set sun azimuth/elevation so the wall and ragdoll throw long shadows toward the camera.
- [x] Launch the scene once and save a rough framing screenshot before shader work.

## Phase 2 - Procedural Terrain Material

- [x] Add a scene-selectable terrain material mode that ignores texture sampling and uses solid/procedural color. Used existing scene-selectable terrain mode `13`.
- [x] Default the new mode so existing textured/grass scenes are unchanged. No terrain shader default changed.
- [x] Add warm floor controls: base color, secondary color, roughness, noise scale, noise strength, and crack/pit strength if practical. Used existing `terrainTint`, `terrainAccent`, and `terrainGrid`.
- [x] Keep all terrain displacement visual-only; collision remains a flat immovable plane/terrain.
- [x] Tune the floor toward warm reddish-brown/graphite rather than clean white.
- [x] Verify the floor is not grass and does not move during play.

## Phase 3 - Procedural Sunset Sky

- [x] Add or tune procedural sky parameters for horizon color, zenith color, sun disk color, sun disk size, sun glow radius, and cloud/noise strength.
- [x] Keep the sky procedural; do not introduce bitmap skies or painted cloud assets.
- [x] Build a warm orange horizon and rose/purple upper sky.
- [x] Add subtle procedural cloud/noise bands if the existing sky shader supports it cleanly.
- [x] Tune exposure so the sun blooms but does not wash out the wall, ragdoll, or ball.

## Phase 4 - God Rays, Haze, And Contrast

- [x] Enable or tune volumetric light shafts/god rays for the showcase scene.
- [x] Align the sun, camera, and wall so the wall can occlude the light enough to reveal rays.
- [x] Increase haze or ray density until the effect is visible in the paused start frame.
- [x] Keep ambient light low enough for long shadows to read.
- [x] Increase directional light contribution enough for bright sunset highlights.
- [x] Tune tone mapping, bloom, vignette, and contrast scene-locally where possible.

## Phase 5 - Wall Shadow Quality

- [x] Inspect why the wall shadow is more pixelated than the ragdoll shadow.
- [x] First try scene-local shadow tuning: tighter camera/frustum, better cascade/split, shadow bias, shadow resolution, or near/far bounds.
- [x] If individual brick shadows remain ugly, add a visual-only shadow proxy for the wall. Not needed after scene-local shadow softening/tighter framing.
- [x] Ensure any shadow proxy is non-colliding, non-physical, invisible or minimally visible, and does not alter brick simulation. No proxy was added.
- [x] Verify the physical 200-brick wall still exists and remains the thing the ragdoll hits.
- [x] Save before/after screenshots of the shadow fix.

## Phase 6 - Material Polish

- [x] Set brick material to warm off-white/tan so it catches sunset light.
- [x] Set ball material to dark graphite or black-chrome with a clear sun highlight.
- [x] Preserve the ragdoll's blue/purple identity but make it respond well to warm lighting.
- [x] Confirm the ball remains visible against the darker floor.
- [x] Confirm the ragdoll silhouette reads against the wall and long shadow.

## Phase 7 - Visual Verification

- [x] Build the required runtime configuration after code/shader changes.
- [x] Launch the showcase scene in DX12.
- [x] Capture the paused start frame.
- [x] Press space or otherwise advance the scene and capture one post-run frame.
- [x] Save visual evidence under `TestOutput/visual_validation/ragdoll_sunset_showcase/`.
- [x] Inspect screenshots manually; do not rely only on file existence.
- [x] If the screenshot still looks flat, fix lighting/sky/haze before moving to validation.
- [x] If the wall shadow still looks broken, fix or explicitly document the remaining compromise.

## Phase 8 - Required Validation And Handoff

- [x] Run `git status --short --branch` before validation.
- [x] If scene data changed only, run `tools\validate_full.bat`.
- [x] If shader or renderer behavior changed, run `tools\validate_dx12_renderer.bat`.
- [x] If physics/source behavior changed, include the matching physics gate from `AGENTS.md`.
- [x] Paste or preserve validation output in a log path and quote the meaningful result lines in the handoff.
- [x] Run the comment-quality audit for every touched source-bearing file.
- [x] Run `git status --short --branch` after validation.
- [x] Write the final handoff with changed files, validation output, screenshot paths, and any known follow-up.

## Evidence

- Start-frame screenshot: `TestOutput/visual_validation/ragdoll_sunset_showcase/ragdoll_sunset_showcase_start.bmp`.
- Post-play screenshot: `TestOutput/visual_validation/ragdoll_sunset_showcase/ragdoll_sunset_showcase_post_play.bmp`.
- Start capture command: `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --hide-top-text --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json`.
- Post-play capture command: `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --hide-top-text --scene TestOutput\visual_validation\ragdoll_sunset_showcase\ragdoll_sunset_showcase_post_play.scene.json`.
- Validation logs: `TestOutput/validation/agent_logs/ragdoll_sunset_validate_dx12_renderer.log` and `TestOutput/validation/agent_logs/ragdoll_sunset_validate_full.log`.

## Stop Conditions

- [ ] Stop and ask before changing global renderer defaults that would alter unrelated scenes.
- [ ] Stop and ask before replacing the real brick wall with a fake non-physical wall.
- [ ] Stop and ask before introducing painted assets, external texture dependencies, or mountains.
- [ ] Stop and ask if stable wall physics regresses before the ball reaches the ragdoll/wall interaction.

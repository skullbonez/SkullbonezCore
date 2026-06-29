# Ragdoll Sunset Procedural Showcase Progress

Purpose: give an agent a concrete checklist to complete the procedural sunset showcase tonight.

Parent plan: `Agentic/Plans/IN PROGRESS/ragdoll-sunset-procedural-showcase-plan.md`

## Hard Constraints

- [ ] Use only procedural rendering for the showcase look. No painted skyboxes, photo textures, HDRIs, generated background bitmaps, or hand-painted mountain cards.
- [ ] Keep the terrain physically flat, giant, immovable, and stable.
- [ ] Keep the ball, ragdoll, and brick wall physical setup based on the current approved ragdoll scene.
- [ ] Start the scene paused, cinematic, and already framed for presentation.
- [ ] Preserve existing scenes by putting new behavior behind scene-local settings or safe defaults.
- [ ] If source-bearing files are touched, run the comment-quality audit before final handoff.

## Definition Of Done Tonight

- [ ] A new scene exists in `SkullbonezData/scenes/` named `aaa_ragdoll_sunset_showcase.scene.json` or an equally explicit `aaa_` showcase name.
- [ ] The scene opens paused with the ball left foreground, ragdoll near the wall, wall right midground, and sun low behind or near the wall.
- [ ] The floor is procedural terrain, not grass, not textured, and not falling.
- [ ] The sky reads as a warm procedural sunset: orange horizon, rose/purple upper sky, bright sun disk, and atmospheric glow.
- [ ] Shadows are long, dark, readable, and not dominated by chunky pixelated brick-wall artifacts.
- [ ] Some visible god-ray or volumetric haze effect is present from the sun/wall direction toward the camera.
- [ ] A final start-frame screenshot is saved under `TestOutput/visual_validation/ragdoll_sunset_showcase/`.
- [ ] The final handoff names every changed file, the exact screenshot path, validation run, and remaining visual compromises.

## Phase 0 - Preflight

- [ ] Run `git status --short --branch` and record pre-existing dirty files as user-owned.
- [ ] Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`.
- [ ] Read the parent plan linked above.
- [ ] Locate the current best ragdoll scene and the ball/ragdoll/wall physical settings to reuse.
- [ ] Locate the terrain shader, sky/post shaders, scene parser fields, and cinematic scene controls.
- [ ] Decide the smallest validation gate based on the actual files changed.

## Phase 1 - Scene Skeleton And Framing

- [ ] Create the new `aaa_` scene by copying the current best ragdoll setup, not the deleted/bad historical variants.
- [ ] Keep the same brick wall placement and stable physical properties from the approved current setup.
- [ ] Lower the ball a few centimeters from the last working position so it hits the ragdoll lower.
- [ ] Increase ball speed only if the current approved setup still needs the stronger impact from the earlier request.
- [ ] Set the scene to start paused.
- [ ] Enable cinematic mode from scene startup.
- [ ] Set camera to a low wide composition matching the reference: ball left foreground, ragdoll before wall, wall right, sun visible above/near wall.
- [ ] Set sun azimuth/elevation so the wall and ragdoll throw long shadows toward the camera.
- [ ] Launch the scene once and save a rough framing screenshot before shader work.

## Phase 2 - Procedural Terrain Material

- [ ] Add a scene-selectable terrain material mode that ignores texture sampling and uses solid/procedural color.
- [ ] Default the new mode so existing textured/grass scenes are unchanged.
- [ ] Add warm floor controls: base color, secondary color, roughness, noise scale, noise strength, and crack/pit strength if practical.
- [ ] Keep all terrain displacement visual-only; collision remains a flat immovable plane/terrain.
- [ ] Tune the floor toward warm reddish-brown/graphite rather than clean white.
- [ ] Verify the floor is not grass and does not move during play.

## Phase 3 - Procedural Sunset Sky

- [ ] Add or tune procedural sky parameters for horizon color, zenith color, sun disk color, sun disk size, sun glow radius, and cloud/noise strength.
- [ ] Keep the sky procedural; do not introduce bitmap skies or painted cloud assets.
- [ ] Build a warm orange horizon and rose/purple upper sky.
- [ ] Add subtle procedural cloud/noise bands if the existing sky shader supports it cleanly.
- [ ] Tune exposure so the sun blooms but does not wash out the wall, ragdoll, or ball.

## Phase 4 - God Rays, Haze, And Contrast

- [ ] Enable or tune volumetric light shafts/god rays for the showcase scene.
- [ ] Align the sun, camera, and wall so the wall can occlude the light enough to reveal rays.
- [ ] Increase haze or ray density until the effect is visible in the paused start frame.
- [ ] Keep ambient light low enough for long shadows to read.
- [ ] Increase directional light contribution enough for bright sunset highlights.
- [ ] Tune tone mapping, bloom, vignette, and contrast scene-locally where possible.

## Phase 5 - Wall Shadow Quality

- [ ] Inspect why the wall shadow is more pixelated than the ragdoll shadow.
- [ ] First try scene-local shadow tuning: tighter camera/frustum, better cascade/split, shadow bias, shadow resolution, or near/far bounds.
- [ ] If individual brick shadows remain ugly, add a visual-only shadow proxy for the wall.
- [ ] Ensure any shadow proxy is non-colliding, non-physical, invisible or minimally visible, and does not alter brick simulation.
- [ ] Verify the physical 200-brick wall still exists and remains the thing the ragdoll hits.
- [ ] Save before/after screenshots of the shadow fix.

## Phase 6 - Material Polish

- [ ] Set brick material to warm off-white/tan so it catches sunset light.
- [ ] Set ball material to dark graphite or black-chrome with a clear sun highlight.
- [ ] Preserve the ragdoll's blue/purple identity but make it respond well to warm lighting.
- [ ] Confirm the ball remains visible against the darker floor.
- [ ] Confirm the ragdoll silhouette reads against the wall and long shadow.

## Phase 7 - Visual Verification

- [ ] Build the required runtime configuration after code/shader changes.
- [ ] Launch the showcase scene in DX12.
- [ ] Capture the paused start frame.
- [ ] Press space or otherwise advance the scene and capture one post-run frame.
- [ ] Save visual evidence under `TestOutput/visual_validation/ragdoll_sunset_showcase/`.
- [ ] Inspect screenshots manually; do not rely only on file existence.
- [ ] If the screenshot still looks flat, fix lighting/sky/haze before moving to validation.
- [ ] If the wall shadow still looks broken, fix or explicitly document the remaining compromise.

## Phase 8 - Required Validation And Handoff

- [ ] Run `git status --short --branch` before validation.
- [ ] If scene data changed only, run `tools\validate_full.bat`.
- [ ] If shader or renderer behavior changed, run `tools\validate_dx12_renderer.bat`.
- [ ] If physics/source behavior changed, include the matching physics gate from `AGENTS.md`.
- [ ] Paste or preserve validation output in a log path and quote the meaningful result lines in the handoff.
- [ ] Run the comment-quality audit for every touched source-bearing file.
- [ ] Run `git status --short --branch` after validation.
- [ ] Write the final handoff with changed files, validation output, screenshot paths, and any known follow-up.

## Stop Conditions

- [ ] Stop and ask before changing global renderer defaults that would alter unrelated scenes.
- [ ] Stop and ask before replacing the real brick wall with a fake non-physical wall.
- [ ] Stop and ask before introducing painted assets, external texture dependencies, or mountains.
- [ ] Stop and ask if stable wall physics regresses before the ball reaches the ragdoll/wall interaction.

# 90 Percent Better Showcase Plan

Date: 2026-06-11
Branch context: `codex/cinematic-renderer`
Goal: make the current low-poly renderer screenshot look dramatically more professional within a few days, prioritizing visible funding-demo impact over deep renderer perfection.

## Operating Instruction

This plan is authorized for autonomous execution. Agents executing this plan should implement, validate, commit, and push incremental branch changes as they go at each milestone.

Hard limits still apply:
- Do not submit a PR unless separately requested.
- Do not force-push.
- Do not rebase.
- Do not rewrite git history.
- Do not claim validation success without command output.
- Use visible console windows for builds, validation, and game launches when executing code changes.

## Definition Of Success

The final hero screenshot should be described by a random observer as:
- beautiful
- atmospheric
- stylized
- professional
- screenshot-worthy

It must not read as:
- prototype
- debug scene
- graphics test
- student project

The goal is not feature completeness. The goal is a convincing hero shot.

## Strategy

Treat the current image as raw material, not as a finished art direction. The fastest path to a 90 percent improvement is:

1. Lock one hero shot.
2. Make lighting, color, and atmosphere carry the scene.
3. Remove or hide objects that advertise "renderer test".
4. Add just enough terrain, water, sky, and set dressing to imply a world.
5. Validate renderer parity and commit after every meaningful slice.

Do not spend a full day polishing one subsystem. If a change is not improving the screenshot after 1-2 hours, cut scope and move to the next highest-impact slice.

## Validation Policy

Most implementation steps affect renderers, shaders, screenshots, scene files, or baselines.

Primary validation:
```bat
tools\validate_renderers.bat
```

Use additional validation only when relevant:
```bat
tools\validate_perf.bat
```
Run this if terrain scatter, object counts, per-frame post-processing, shadows, water, fog, or capture code might create a hot-path regression.

Documentation-only edits require no validation.

Each implementation milestone should end with:
1. capture the hero screenshot,
2. compare against the previous screenshot,
3. run required validation,
4. commit with useful notes,
5. push the branch.

## Commit And Push Rhythm

Commit after each visible milestone, not after every tiny tweak. Suggested commit subjects:
- `render: establish cinematic hero lighting`
- `scene: rebuild showcase composition`
- `render: add atmospheric sky depth`
- `render: improve showcase water presentation`
- `scene: dress terrain for hero screenshot`
- `render: polish capture output`

Commit bodies should include:
- what changed,
- why it improves the screenshot,
- which files/areas were affected,
- exact validation command and meaningful result,
- screenshot artifact path if generated.

Because this plan contains explicit user authorization, agents executing it should commit and push each completed milestone.

## Phase 0: Lock The Shot

Timebox: 30-60 minutes
Expected impact: very high

Objective: stop evaluating the renderer from arbitrary test-scene framing. Create one canonical funding-demo camera.

Tasks:
- Pick or create a dedicated showcase scene, preferably near the current cinematic scene.
- Lock camera position, FOV, sun angle, time of day, and screenshot frame.
- Put the pond and sunset on the main visual axis.
- Ensure there is clear foreground, midground, and background.
- Remove obvious accidental tangents, object edge crops, and test clutter from the hero view.
- Save a baseline screenshot before changes and a new screenshot after camera/composition changes.

Do not over-engineer camera tooling. Hard-coded scene/camera values are acceptable for the demo if they are isolated to the showcase scene.

Commit/push after validation if the screenshot is visibly better.

## Phase 1: Lighting, Exposure, Tonemap, Bloom

Timebox: 0.5 day
Expected impact: extreme

Objective: make the image feel like sunset instead of a lit test plane.

Tasks:
- Lower flat ambient contribution.
- Make the sun the dominant key light.
- Add warm gold/orange highlights and cooler blue-violet shadow fill.
- Tune exposure so the sun/horizon glows without washing out the scene.
- Add or tune filmic tonemapping for pleasant highlight rolloff.
- Add restrained bloom around the sun and bright water glints.
- Add a subtle vignette only if it improves focus without looking like a filter.
- Ensure red/yellow hero props remain saturated but not neon/debug-like.

Fast acceptance test:
- Squint test: the eye should immediately go to the sun/water/hero object.
- Grayscale test: the image should have a clear value hierarchy.
- Color test: greens should be natural olive/grass, not uniform acidic yellow-green.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when the hero screenshot has clear cinematic value structure.

## Phase 2: Atmosphere, Sky, And Distant Scale

Timebox: 0.5 day
Expected impact: extreme

Objective: replace "flat backdrop" with a believable low-poly world extending into the distance.

Tasks:
- Add layered mountain silhouettes behind the play area.
- Add aerial perspective: distant terrain shifts lighter, warmer near sun, cooler in shadows.
- Add stylized cloud bands with strong sunset color variation.
- Add a sun disk, halo, and soft radial glow.
- Use fog/depth haze to separate foreground, midground, and background.
- Make the far horizon carry the emotional mood of the image.

Keep this mostly procedural or scene-authored. Do not build a full weather system.

Fast acceptance test:
- The background should be screenshot-worthy even if foreground props are hidden.
- The horizon should no longer read as a flat green strip.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when the screenshot gains obvious depth and atmosphere.

## Phase 3: Composition Cleanup And Prop Triage

Timebox: 0.5 day
Expected impact: very high

Objective: stop the scene from looking like a renderer feature test.

Tasks:
- Remove or heavily reduce random cubes and balls unless they serve the composition.
- Convert white cubes into intentional monoliths, ruins, or markers.
- Reposition the main ball so it anchors the water reflection and focal point.
- Avoid objects floating at arbitrary heights unless they are clearly intentional.
- Create foreground framing using rocks, grass clumps, flowers, or reeds.
- Use repeated orange/yellow accents sparingly to guide the eye.
- Ensure left/right balance without perfect symmetry.

Fast acceptance test:
- A viewer should infer "place" before "primitive test".
- Every large object visible in the screenshot should have a compositional reason to exist.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when the scene reads as designed rather than scattered.

## Phase 4: Grounding, Shadows, And Contact

Timebox: 0.5-1 day
Expected impact: extreme

Objective: make objects belong in the world.

Tasks:
- Improve sun shadows for terrain and props.
- Add contact shadowing or screen-space ambient occlusion.
- Ensure tree trunks, rocks, monoliths, balls, and grass have dark grounding.
- Avoid large, mushy shadow blobs that obscure low-poly shape.
- Bias shadow softness toward stylized readability, not physical exactness.
- Ensure water-edge objects and shoreline rocks cast/receive plausible visual contact.

Fast acceptance test:
- No major object should look pasted on.
- The ground under objects should be visually darker and more believable.

Validation:
```bat
tools\validate_renderers.bat
```

If shadow changes create meaningful GPU cost:
```bat
tools\validate_perf.bat
```

Commit/push when grounding is visibly improved.

## Phase 5: Water Hero Pass

Timebox: 0.5 day
Expected impact: high

Objective: turn the pond from cyan geometry into the emotional center of the image.

Tasks:
- Add a shallow/deep water color gradient.
- Add fresnel edge response.
- Add sun glint and warm reflection color.
- Add reflection of the main ball and nearby silhouettes, even if approximated.
- Add subtle ripple normals or low-poly wave facets.
- Add soft shoreline blending, wet edge tint, or thin foam/reed detail.
- Keep water transparent enough to feel stylized, not like blue plastic.

Fast acceptance test:
- The pond should be one of the prettiest parts of the screenshot.
- The main ball reflection should read immediately.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when the water reads as intentional and attractive.

## Phase 6: Terrain And Set Dressing

Timebox: 0.5-1 day
Expected impact: high

Objective: make the ground feel authored and lived-in without building a full level.

Tasks:
- Sculpt or fake terrain ridges around the pond.
- Add shoreline stones, reeds, grass, and small flowers.
- Add a path or visual route leading toward the sunset.
- Add color variation patches to break up the flat terrain.
- Add a few large rocks to frame the foreground and midground.
- Improve tree placement and silhouettes; avoid duplicate-looking rows.
- Use denser detail near camera, sparser detail in the distance.

Fast acceptance test:
- The bottom third of the screenshot should look rich, not empty.
- The eye should have a path through the scene.

Validation:
```bat
tools\validate_renderers.bat
```

If object count or scatter logic increases:
```bat
tools\validate_perf.bat
```

Commit/push when terrain no longer reads as a flat test floor.

## Phase 7: Material And Low-Poly Style Polish

Timebox: 0.5 day
Expected impact: medium-high

Objective: make low-poly look like a deliberate art style instead of low-detail geometry.

Tasks:
- Introduce material presets for grass, rock, bark, leaves, water, monoliths, and hero props.
- Use controlled roughness/specular values.
- Add bevels or normal tricks to cubes/monoliths where they look too raw.
- Tune facet lighting so large triangles look designed.
- Reduce raw primary colors where they fight the sunset palette.
- Keep the striped ball crisp, but less debug-neon.

Fast acceptance test:
- Materials should feel curated even with simple geometry.
- No object should look like it came straight from a geometry unit test.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when materials support the art direction.

## Phase 8: Showcase Capture Polish

Timebox: 2-4 hours
Expected impact: medium-high

Objective: make the final screenshot look like a presentation asset.

Tasks:
- Add a deterministic high-resolution capture path or preset if not already present.
- Enable clean anti-aliasing or supersampled screenshot capture.
- Ensure no debug overlays, UI, cursor, console artifacts, or inconsistent frame timing.
- Add screenshot naming that includes renderer, scene, and timestamp or milestone.
- Capture GL, DX11, and DX12 outputs for parity.
- Pick the best final screenshot for handoff.

Validation:
```bat
tools\validate_renderers.bat
```

Commit/push when capture output is clean and repeatable.

## Stop Conditions

Stop polishing a slice and move on when:
- the screenshot improvement is no longer obvious,
- the slice exceeds its timebox,
- cross-renderer parity becomes risky,
- the implementation threatens deadline stability,
- a simpler scene-authored trick would produce the same visual result.

For a funding screenshot, a beautiful illusion that validates is better than a perfect general-purpose system that is not finished.

## Daily Demo Loop

At the start of each day:
1. capture current hero screenshot,
2. compare against previous best,
3. choose the next highest-impact slice,
4. timebox the work,
5. validate,
6. commit,
7. push.

At the end of each day:
1. save best screenshot path in the final update or session handoff,
2. list remaining visual blockers,
3. note exact validation output,
4. ensure branch is pushed.

## Priority Order If Time Collapses

If only one day remains:
1. lighting/exposure/tonemap/bloom,
2. sky/atmosphere/mountains,
3. composition cleanup,
4. contact shadows/AO,
5. water color/reflection cheat,
6. foreground grass/rocks/flowers,
7. final high-res capture.

If only half a day remains:
1. lock hero camera,
2. tune sunset lighting and color,
3. add atmospheric horizon/mountains,
4. remove test clutter,
5. capture a clean screenshot.

If only two hours remain:
1. create one beautiful screenshot scene,
2. hard-code the best camera and sun angle,
3. tune color/exposure,
4. hide embarrassing objects,
5. capture and push.

## Non-Goals

Do not spend deadline time on:
- physically perfect volumetrics,
- a full material editor,
- generalized biome tooling,
- broad asset pipeline redesign,
- arbitrary camera freedom,
- every possible scene,
- renderer architecture cleanup unless it blocks the hero shot.

This is a funding-demo visual pass. Build the postcard first.

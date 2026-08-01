# Look Lab Random Style Authoring — LL0 Census

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL0 — complete

## Decision

Look Lab version 1 will generate a detached, fully resolved standalone style
from a 64-bit seed with a specified SplitMix64 stream. It will vary only live
presentation values, preserve scene- and resource-quality values, apply through
`SceneController::ApplyLiveStyle`, and save the exact applied value rather than
reconstructing it from recipe defaults.

The LL6-corrected current source exposes 84 atomic `CinematicRenderConfig`
values: 8 top-level booleans, 64 top-level numeric/mode values, and 12 nested
shadow values. They map to 63 live override bits. The standalone material surface
adds 14 material kinds, six target forms, and eleven optional material payload
fields. All 23 tracked `*.style.json` files parse as schema v1.

F5 and F6 remain bound to `TogglePerformanceHistogram` and
`ToggleMemoryOverlay`. F10 and F11 have no current binding. Look Lab adds them
only in the keyboard-unblocked context and does not change F2, F3, F7, F8, or
F9.

## Current Ownership And Flow

| Concern | Current owner and behavior | LL1–LL6 contract |
|---|---|---|
| Input bindings | `Runtime/Input/InputController.Bindings.cpp` publishes immutable VK/action rows. F5 and F6 are keyboard-unblocked diagnostics; F10/F11 are absent. | Add explicit `LookLabReroll` and `LookLabSave` actions at F10/F11. Input publishes edges; it retains no candidate or transaction. |
| Standalone style parse | `AuthoredSceneParser` requires `format: skullbonez.style.json`, accepts versions 1–4, recursively expands `includes`, and applies only presentation sections. Every tracked style is v1. | Writer emits self-contained schema v1 with no `includes`; generator metadata stays in `look.txt`. |
| Style merge | `SceneController::ApplyLiveStyle` resets active cinematic values to startup defaults, overlays exactly the parsed override mask, resets ordinary object materials, applies ordered matching material rules, clears UI override bits, and clears browser selection. | Look Lab supplies every writable presentation field and its complete material rule list, so defaults/catalog changes cannot alter a saved look. |
| Scene reset/load | `SceneSessionState::ResetForLoad` restores default cinematic values and zero masks; scene activation then applies the new scene’s authored override mask. | Scene load/reset cancels candidate/save state and clears Look Lab status. It never reapplies a candidate to a different scene. |
| Screenshot | Current live-style capture is pinned before physics, drawn with the selected presentation, and saved after world/UI rendering in `RunPostDrawDiagnosticsPhase`, before ordinary screenshot automation and Present. | F11 arms one post-render request only after JSON and pending receipt commit. Completion atomically rewrites the receipt with saved/failed/cancelled status. |
| Runtime owner | `LiveStyleController` owns opt-in control-folder polling and one pending BMP path; it is automation infrastructure, not keyboard authoring. | A separate `Runtime/Direction/LookLabController` owns one candidate and one bounded save transaction. It borrows Scene and Capture through App sequencing. |

## Cinematic Field Census And Rulings

“Parser range” is the actual accepted JSON boundary today. Vector members use
only numeric/cardinality validation, so Look Lab’s narrower generator range is
the safety contract. “Derived” means the recipe computes the value from shared
palette/light facts rather than drawing it independently.

| Fields | Parser range / shape | Consumer and side effect | Ruling and generator constraint |
|---|---|---|---|
| `enabled` | bool | `RuntimeRenderer` selects the cinematic HDR/post path. | Derived `true`; an F10 candidate that disables its own render path is invalid. |
| `skyAtmosphereEnabled` | bool | Sky atmosphere pass; no resource resize. | Derived by recipe; true except explicit deep-space/graphic variants with another readable background. |
| `cloudsEnabled` | bool | Sky shader cloud contribution; existing shader/resource only. | Randomized per recipe, compatible with `cloudCoverage` and sky family. |
| `godRaysEnabled` | bool | Shaft contribution inside volumetric presentation. | Randomized/derived; false when volumetric is false or the recipe has no bright sun. |
| `volumetricLightingEnabled` | bool | Enables the half-resolution volumetric graph pass. Existing graph resource; no resize. | Randomized by recipe with bounded density/strength. |
| `bloomEnabled` | bool | Bloom/tonemap graph path. Existing resource; no resize. | Randomized by recipe; emissive recipes require it, documentary/high-key recipes may reduce or disable it. |
| `fogEnabled` | bool | Depth-based cinematic fog composite. | Randomized by recipe; if false, fog scalars remain resolved but inactive. |
| `terrainReliefEnabled` | bool | Vertex-only terrain displacement; collision is unchanged. | Randomized only for recipes that keep displacement within visibility bounds. |
| `exposure` | 0..16 | Tonemap brightness. | Derived, 0.55..2.25. Coupled to palette luminance, bloom, and emissive energy. |
| `gamma` | 0.1..8 | Tonemap output transfer. | Derived, 1.35..2.35; recipes do not use gamma as a substitute for exposure. |
| `sunAzimuth`, `sunElevation` (`sunScreenX/Y`) | 0..1 each | Shared sun direction for sky, direct light, shadows, water glint, and volumetrics. | Randomized/derived; elevation 0.12..0.88 and azimuth 0..1. |
| `sunColorR/G/B` | 0..4 each | Sky, direct lighting, shafts, water glint. | Derived from one palette illuminant, finite 0..3.2 per channel. |
| `sunIntensity` | 0..80 | Sky disk/direct light energy. | Derived 0.4..28; deep-space/emissive families use the low end. |
| `skyHorizonR/G/B`, `skyZenithR/G/B` | 0..4 each | Sky atmosphere gradient. Shader clamps to 0..2.2. | Derived together in perceptual palette space, 0..2.2, with bounded luminance separation. |
| `skyGlowStrength` | 0..16 | Sun/sky glow uniform. | Derived 0..3.0 from sun intensity and recipe haze. |
| `cloudCoverage` | 0..1 | Cloud procedural mask. | Randomized 0.05..0.88 when enabled. |
| `cloudSoftness` | 0.001..1 | Cloud edge transition. | Randomized 0.04..0.55. |
| `cloudScale` | 0.1..64 | Cloud procedural scale. | Randomized 1.2..18.0; finite and nonzero. |
| `cloudIntensity` | 0..4 | Cloud blend energy. | Derived 0.15..1.5 and zero/ignored when clouds are disabled. |
| `sunShaftStrength` | 0..8 | Volumetric radial energy. | Derived 0..1.6. |
| `sunShaftFalloff` | 0.1..10 | Volumetric radial decay. | Randomized 0.7..5.0. |
| `volumetricStrength` | 0..8 | Final shaft texture scale. | Derived 0..1.5. |
| `volumetricDensity` | 0..8 | Volumetric march density. | Derived 0.05..1.6. |
| `volumetricDecay` | 0..1 | Per-sample shaft decay. | Randomized 0.86..0.985. |
| `bloomThreshold` | 0..16 | Bloom bright-pass threshold. | Derived 0.35..2.4 from exposure/emissive energy. |
| `bloomKnee` | 0.001..8 | Bloom threshold softness. | Randomized 0.08..1.2. |
| `bloomStrength` | 0..8 | Bloom composite energy. | Derived 0..1.25. |
| `bloomRadius` | 0.1..32 | Bloom blur spread; existing target sizes are unchanged. | Randomized 0.8..8.0. |
| `terrainRelief` | 0..4 | Visual-only terrain height offset. | Derived 0..1.25. |
| `basinDepth`, `basinRimLift` | 0..256 | Visual-only basin displacement. | Derived with relief, 0..96 each; zero when relief is disabled. |
| `fogColorR/G/B` | 0..4 each | Fog composite color. | Derived from horizon/background palette, 0..2.2. |
| `fogStart` | 0..10000 | Fog near distance. | Derived from retained current scene scale, never independently random. |
| `fogEnd` | 0..20000 | Fog far distance. | Derived with `fogStart`; invariant `fogEnd >= fogStart + max(32, 0.15 * fogStart)`. |
| `fogDensity` | 0..0.1 | Exponential fog density. | Derived 0..0.012. |
| `fogMaxOpacity` | 0..1 | Maximum obscuration. | Derived 0..0.82; primary geometry must retain contrast against fog. |
| `skyMode` | parser accepts any int | Sky shader: generic gradient/ridges for most values; distinct low-poly 11, open-horizon 20, and literal-black deep-space 21 branches. | Randomize only named 0..13, 15..21. Exclude unassigned 14 and generic fallbacks 22..32. |
| `terrainMode` | parser accepts any int | Terrain shader has distinct branches 0..15. | Randomize 0..15, recipe-coupled to terrain palette/grid. |
| `objectStyle` | parser accepts any int | Default object shader kind; mode 6 also selects low-poly sphere mesh. | Randomize only top-level supported 0..7. Modes 8..13 enter through material rules, not the scene-wide mesh selector. |
| `waterMode` | parser accepts any int | 0 off, 1 basin, 2 ocean, 3 wet floor, 4 stylized basin. Basin modes clip against the retained mask. | Randomize 0..4 only when compatible with current scene water presentation; otherwise retain. |
| `styleSaturation` | numeric vec3 member | Tonemap grade. | Derived 0.55..1.85. |
| `styleContrast` | numeric vec3 member | Tonemap grade. | Derived 0.70..1.75 with a minimum midtone separation test. |
| `styleVignette` | numeric vec3 member | Tonemap edge attenuation. | Randomized 0..0.62; high-key families cap at 0.22. |
| `terrainTintR/G/B` | numeric vec3 | Terrain shader base palette. | Derived, 0.001..2.2; non-black when terrain is visible. |
| `terrainAccentR/G/B` | numeric vec3 | Terrain shader grid/bands/details. | Derived, 0..2.2 with contrast against tint. |
| `terrainGridScale`, `terrainGridStrength` | numeric vec2 | Terrain grid modes and authored accent overlay. | Derived: scale 8..96, strength 0..2; non-grid recipes set strength 0. |
| `waterTintR/G/B` | numeric vec3 | Water base tint. | Derived, 0..2.2 with visible luminance against terrain/sky. |
| `waterAlpha`, `waterReflectionStrength`, `waterGlintStrength` | numeric vec3 | Water blend, reflection mix, and sun glint. | Derived: alpha 0.25..1, reflection 0..1, glint 0..2.5. |
| `basinCenterX/Z`, `basinRadiusX/Z`, `basinFeather` | numeric vec5 | World-space water/terrain basin mask. Radius is clamped to at least 1 in shader. | Retain exactly. These are scene-coordinate presentation geometry, not art-direction randomness. |
| `shadow.enabled` (`shadows`) | bool | Shadow pass visibility; existing resources remain owned by renderer. | Randomized/derived only for recipes; disabling shadows must preserve readable grounding by palette/ambient contrast. |
| `shadow.terrainCasts`, `shadow.objectsCast`, `shadow.terrainReceives`, `shadow.objectsReceive` | bool each | Controls cast/receive participation for terrain and object geometry. | Retain exactly from the active presentation and serialize as one four-atom grouped override. |
| `shadow.mapSize` | 256..8192 | Allocates/resizes shadow-map resources. | Retain exactly: resource-quality policy. |
| `shadow.pcfRadius` | 0..3 | Shadow filter work/quality. | Retain exactly: resource-quality policy. |
| `shadow.strength` | 0..1 | Shadow visibility multiplier. | Derived 0.2..1.0. |
| `shadow.softness` | 0.25..4 | Shadow sample spread, not allocation. | Randomized 0.5..2.5. |
| `shadow.depthBias`, `shadow.slopeBias` | 0..0.05 | Scene/geometry-dependent acne and peter-panning control. | Retain exactly. |
| `shadow.maxDistance` | 128..10000 | Scene-scale shadow coverage. | Retain exactly. |

## Mode And Material Census

| Surface | Supported values | Generator ruling |
|---|---|---|
| Sky | 0 SunSky, 1 Industrial, 2 Studio, 3 NeonCyberpunk, 4 AlienPlanet, 5 DesertStorm, 6 Painterly, 7 RetroFuture, 8 AtmosphericFog, 9 OceanWorld, 10 SciFiTestChamber, 11 LowPolyArt, 12 MassiveScale, 13 StormFront, 15 TronGrid, 16 Dreamscape, 17 NordicWinter, 18 AbstractRender, 19 PixarInspired, 20 OpenHorizon, 21 DeepSpace. | All named values appear in the deterministic seed matrix. 14 and 22..32 are excluded. |
| Terrain | 0 TexturedWarm, 1 WornIndustrial, 2 PaleStudio, 3 NeonGrid, 4 AlienVeins, 5 DesertSlope, 6 Posterized, 7 LowPolyBasin, 8 DarkNeutral, 9 CoolStone, 10 SciFiGrid, 11 NordicSnow, 12 Photogrammetry, 13 ChromaticBands, 14 SoftIllustrated, 15 SolidStudio. | All 0..15 appear in the seed matrix with compatible tint/accent/grid facts. |
| Top-level object | 0 BeachBall, 1 Matte, 2 Metal, 3 Emissive, 4 Fresnel, 5 ToonBands, 6 LowPoly, 7 DarkRim. | All 0..7 appear. LowPoly is the only mode allowed to change sphere mesh selection. |
| Water | 0 Off, 1 Basin, 2 Ocean, 3 WetFloor, 4 StylizedBasin. | All 0..4 appear where retained scene water/mask facts permit them. |
| Material kinds | 0 Textured, 1 Matte, 2 Metal, 3 Emissive, 4 Glass, 5 Toon, 6 LowPoly, 7 Shadow, 8 Foliage, 9 Bark, 10 Stone, 11 Ridge, 12 Shore, 13 Pine. | All 14 typed kinds appear in the fixed seed matrix. Canonical writer spellings are `textured`, `matte`, `metal`, `emissive`, `glass`, `toon`, `lowpoly`, `shadow`, `foliage`, `bark`, `stone`, `ridge`, `shore`, `pine`. Aliases parse but are never written. |

Material targets are ordered and later matches win. `all`, `balls`, `boxes`,
`hulls`, and `convex_hulls` are broad selectors; broad selectors deliberately
skip simple ragdoll parts. `prefix:<text>` and an exact display name may opt a
named ragdoll part in. Empty prefixes match nothing. Look Lab version 1 writes
only broad `balls`, `boxes`, and `hulls` rules because its pure generator has no
scene-name authority. It retains no exact/prefix target copied from a curated
style.

Each emitted material is fully resolved with canonical `mode`, `tint`, `alpha`,
`roughness`, `metallic`, `specular`, `transmission`, `stylization`, `emissive`,
`strength`, `flags`, and `name`. Unit fields remain in 0..1, emissive strength is
0..8 for Look Lab, emissive colors are 0..4, flags are retained as zero, and
names are deterministic recipe/role labels shorter than 32 bytes. The parser
accepts numeric legacy modes and aliases (`texture`, `beachball`, `solid`,
`chrome`, `neon`, `pixar`, `low_poly`, `black`, `leaf`, `leaves`, `trunk`,
`rock`, `distant`, `sand`, `conifer`); the writer never emits them.

## Tracked Style Catalog

The catalog contains one complete base, one material-contract fixture, and 21
named looks. All are schema v1. Twenty-one named looks include `_concept_base`;
`material_authoring_contract` is self-contained.

| Style | `styleModes` | Material rule count | Recipe evidence |
|---|---:|---:|---|
| `_concept_base` | 0/0/0/1 | 0 | complete default field vocabulary |
| `abstract_render_showcase` | 18/13/2/1 | 7 | abstract/chromatic |
| `alien_planet` | 4/4/0/1 | 3 | alien/terrestrial |
| `atmospheric_fog_world` | 8/8/7/1 | 2 | atmospheric/low-key |
| `brutal_industrial` | 1/1/0/1 | 2 | industrial/low-key |
| `consequence` | 13/3/2/1 | 1 | storm/graphic |
| `desert_storm` | 5/5/1/0 | 2 | warm/atmospheric |
| `dreamscape` | 16/4/5/1 | 3 | dreamlike/space-facing |
| `golden_hour_realism` | 0/0/0/1 | 1 | realistic/warm |
| `low_poly_art_style` | 11/7/6/4 | 25 | low-poly/stylized |
| `massive_scale` | 12/8/1/2 | 2 | terrestrial/scale |
| `material_authoring_contract` | — | 4 | parser/material fixture, not a recipe |
| `neon_cyberpunk` | 3/3/3/1 | 4 | neon/low-key |
| `nordic_winter` | 17/11/4/0 | 3 | cool/high-key |
| `ocean_world` | 9/9/0/2 | 1 | ocean/terrestrial |
| `painterly` | 6/6/5/1 | 3 | painterly/stylized |
| `photogrammetry_ground` | 0/12/0/0 | 2 | realistic/terrestrial |
| `pixar_inspired` | 19/14/5/1 | 3 | whimsical/toon |
| `retro_future_2005` | 7/9/0/2 | 2 | retro/cool |
| `scifi_test_chamber` | 10/10/4/1 | 3 | graphic/studio |
| `storm_front` | 13/1/2/1 | 3 | atmospheric/storm |
| `studio_lighting_showcase` | 2/2/2/1 | 3 | high-key/studio |
| `tron_grid` | 15/3/7/0 | 3 | graphic/neon |

## Locked Generator Version 1

Generator version 1 uses unsigned 64-bit SplitMix64 exactly:

```text
state += 0x9E3779B97F4A7C15
z = state
z = (z xor (z >> 30)) * 0xBF58476D1CE4E5B9
z = (z xor (z >> 27)) * 0x94D049BB133111EB
return z xor (z >> 31)
```

Arithmetic wraps modulo 2^64. Floats use the high 24 result bits multiplied by
`1 / 16777216`, producing the exact half-open range [0,1). Recipe selection,
palette roles, scalar variation, feature switches, and material roles each use
a documented fixed draw order. No `std::uniform_*`, platform RNG, gameplay RNG,
renderer sampling RNG, or locale-dependent conversion participates.

The 14 recipe families are: `golden_realism`, `low_poly_storybook`,
`painterly_poster`, `neon_cyberpunk`, `tron_graphic`, `atmospheric_storm`,
`studio_high_key`, `industrial_low_key`, `desert_warm`, `nordic_cool`,
`ocean_terrestrial`, `alien_world`, `deep_space_dreamscape`, and
`abstract_chromatic`. Fixed seed-matrix coverage must hit every family, every
named sky value, all 16 terrain values, all 8 top-level object values, all 5
water values where compatible, and all 14 material kinds.

Candidate validity rejects non-finite values, parser-bound violations, unknown
modes, inverted fog, invisible terrain/water/object palettes, black-frame
luminance, incompatible deep-space/cloud/sun combinations, disabled master
rendering, and any mutation outside the detached presentation value.

## Exact Saved Output Contract

`look.style.json` is schema v1 and self-contained. Stable key order is:

1. `format`, `version`;
2. `cinematic` with the booleans, scalar fields, `styleModes`, `styleGrade`,
   `terrainTint`, `terrainAccent`, `terrainGrid`, `waterTint`, `waterProfile`,
   `basinMask`, and all shadow fields in parser-table order; and
3. `objectMaterials` in the generator’s stable role order.

Every one of the 84 atomic cinematic values is written, including retained
quality and basin-mask values, so reload cannot inherit a changed default.
`objectMaterials` carries the full resolved payload described above. JSON uses
locale-independent shortest round-trip decimal formatting and `\n` line endings.

Bundle grammar is locked to:

```text
LookLab/YYYY-MM-DD_HH-mm-ss_seed_<16-lowercase-hex-digits>/
  look.style.json
  look.txt
  look.png
```

The timestamp is local time; `look.txt` also records the numeric UTC offset,
16-digit lowercase seed, generator version, recipe, source scene path and
display name, filenames, flattened complete cinematic/material listing, and
`style_status` plus `screenshot_status`. JSON and the pending receipt are
written to sibling temporary files and atomically renamed. PNG is captured only
after a completed rendered frame that contains the candidate. Screenshot
failure leaves the valid style and an atomically finalized partial-success
receipt. F11 before F10 creates no folder.

## Pre-Implementation Measurements

The current Profile executable and `perf_1000.scene.json` were run for two
150-sample passes with 30 warm-up frames per pass. With no live-style control,
`Frame/Input` measured 0.119154 ms mean on pass 1 and 0.108307 ms on pass 2
(`TestOutput/ll0_baseline_perf.csv`). This is the current no-Look-Lab floor, not
a permanent performance baseline.

An opt-in live-style probe applied `low_poly_art_style` exactly once and then ran
9,817 held frames before the bounded investigation stopped its exact process.
`status.txt` reported `style_applies 1`, `captures 0`. Source inspection explains
the result: disabled `LiveStyleController::Tick` returns immediately; enabled
idle control performs two file-attribute stamp reads and parses/applies only
when a stamp changes. The probe also confirmed that `--live-style-control`
forces interactive hold and enables unrelated interactive behavior unless
Replay is explicitly disabled, so its raw `Frame/Input` timing is not a valid
comparison and is not claimed as one.

LL5 must measure Look Lab itself with Replay explicitly off. Its acceptance is
stricter than the existing harness: no per-frame Look Lab polling, filesystem
access, allocation, shader work, scene reload, or material churn. When neither
F10 nor F11 is pressed, the controller receives no App call that mutates or
touches retained candidate state.

## LL1–LL6 Acceptance Locks

- F5/F6 remain exact and F10/F11 are edge-triggered only in keyboard-unblocked
  context.
- Same seed plus generator version produces byte-identical detached candidates
  across Debug/Profile and repeated processes.
- The writer round-trips every cinematic atom and every material atom exactly.
- Look Lab never reads or advances simulation, gameplay, Replay, or renderer RNG.
- Camera pose/projection, transforms, topology, assets, physics, clocks, current
  scene path, and authored scene bytes remain unchanged by F10/F11.
- Resource quality, capacity, shadow-map allocation, shader compilation, debug
  state, window state, and existing tracked baselines remain unchanged.
- Scene load/reset and shutdown cancel pending work honestly; no stale candidate
  crosses scene ownership.
- One accepted F11 edge produces at most one bundle and one capture.

## Validation

Documentation-only. No repository validation command was required for LL0. The
source census used the current CodeGraph index, targeted source reads, all 23
tracked style documents, the immutable input table, and the focused Profile
probe described above. Production source, tracked styles, configuration,
baselines, and goldens were not changed.

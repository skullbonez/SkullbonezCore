# Shadow Edge Quality S0-S1 Evidence

Date: 2026-07-12
Plan: `shadow-edge-quality`
Scope: objective baselines and the terrain detail-shadow binding

## Baseline Set

The committed scenes already cover the three required observation shapes:

| Acceptance shape | Scene | Capture | Settings |
|---|---|---|---|
| Silhouette readability | `shadow_map_plain.scene.json` | `TestOutput/baselines/shadow_plain_reference.bmp` | 2048 map, radius 1, strength 0.68, softness 1.2, depth bias 0.0001, slope bias 0.00025 |
| Terrain receiver | `cinematic_shadow_map.scene.json` | `TestOutput/baselines/shadow_cinematic_reference.bmp` | 2048 map, radius 1, strength 0.68, softness 1.2, depth bias 0.00005, slope bias 0.0001 |
| Motion/contact stress | `rolling_shadow_contact.scene.json` | `TestOutput/baselines/shadow_motion_reference.bmp` | fixed step, 180 frames, 2048 map, radius 1, strength 0.7, softness 1.0, depth bias 0.00005, slope bias 0.0001 |

The pre-change captures show readable caster identity but visibly stepped square
PCF edges, especially on long box shadows and curved ball silhouettes. They are
reference evidence only; S1 does not replace them with post-change images.

## Depth Preview Evidence

- `TestOutput/baselines/shadow_terrain_depth_reference.jpg` records the broad
  `Terrain Shadow Depth` target at 2048 x 2048 (runtime handle 16 in the probe).
- `TestOutput/baselines/shadow_object_depth_reference.jpg` records the tight
  `Object Shadow Depth` target at 2048 x 2048 (runtime handle 18 in the probe).

The broad target covers the terrain-sized receiver footprint. The tight target
contains the nearby object caster silhouettes. Before S1, terrain sampled only
the broad target at `t3`, so the tighter silhouettes could not influence terrain
receivers even though the resource already existed.

## S1 Binding

The `UnifiedRaster` ABI now appends `t5` as `DetailShadowMap`. The existing
instanced material table remains at `t4`; no shader family reinterprets that
slot. Terrain declares both shadow depth resources in its render graph pass,
binds the broad map at `t3`, and binds or explicitly clears the tight map at
`t5` every draw.

`lit_textured.hlsl` samples the tighter projection when a terrain fragment is
inside its valid light-space footprint and falls back to the broad projection
outside it. This prevents the lower-density version of the same nearby caster
from leaving a stepped halo around the tight result. Disabled or unavailable
detail shadows receive identity/zero uniforms and a null `t5` binding.

## Validation

- Shader bake: 43 stages written with DXC 1.8.2502.11 in 2.585 seconds.
- Targeted Profile build: passed in 15.84 seconds with 0 warnings and 0 errors.
- `tools\validate_tests.bat`: 155/155 cases and 3524/3524 assertions passed in
  7.193 seconds after updating the deliberate uniform-count mutation fixture.
- `tools\validate_dx12_renderer.bat`: final run passed in 36.222 seconds; baked shader
  freshness, formatting, Profile/Debug builds, captures, and DX12 diagnostics
  all passed.
- `tools\validate_fast.bat`: passed after adding the missing `Frustum` math
  prefix to `validate_project_filters.py`; the project-filter inventory now
  reports 0 errors across 659 production project items.
- `python tools\validate_shaders.py`: the new `lit_textured` contract passed,
  while the repository-wide command still reports its two pre-existing
  post-processing contract errors (`post_tonemap` and
  `post_volumetric_light`) plus 54 inventory warnings. This is not used as S1
  success evidence.
- `tools\run_graphics_stress.bat 1`: final bounded run completed in 60.855 seconds
  wall time (59.711 seconds engine sample time), 12,644 frames and 352 scene
  loads; PID-scoped timeout produced `WM_QUIT`, `Execute returned`, and an empty
  stderr file.

## Comment Audit

Audited all 14 hand-authored touched source-bearing files, including the shader
reflection test and project-filter validator, against
`Agentic/Reference/comment-style-guide.md`. The
generated reflection header is generator-owned. Broad/detail map vocabulary,
the `t4` invariant, descriptor clearing, and frame-handle lifetime are explained
at their owning contracts. Deferred: 0.

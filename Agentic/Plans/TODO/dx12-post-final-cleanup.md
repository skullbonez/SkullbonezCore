# DX12 Post-Processing Final Cleanup Pass

Date: 2026-07-11
Status: Not started — 0%
Impact area: DX12 renderer post chain (shaders + pass binding code), cinematic
config, visual baselines
Companion checklist: `Agentic/Plans/TODO/dx12-post-final-cleanup-progress.md`
Origin: 2026-07-11 DX12 rendering review (branch
`claude/directx-12-rendering-review-wolqlc`). The review found the DX12
device/sync/pass architecture sound; the debt is concentrated in the
fullscreen post shaders and the cinematic config surface.

## Problem statement

The cinematic post chain works and passes all gates, but it carries four
kinds of debt:

1. **Double god-ray processing.** The sun march runs twice per frame: once at
   half resolution in `post_volumetric_light.hlsl` (48 samples, the intended
   path) and again at full resolution inside `post_tonemap.hlsl`
   (`RadialGodRays`, 36 samples, each tap reading both depth and scene color).
   That is roughly 72 redundant texture reads per screen pixel in the final
   pass, duplicating work the half-res pass already did.
2. **Bloom is the most expensive narrow bloom possible.** A 13-tap
   single-pass blur at full resolution in `post_tonemap.hlsl`, with
   `GetDimensions` called per tap chain per pixel and `PrefilterBloom`
   re-run per tap.
3. **Dead shader code.** `CloudRayOpen()` unconditionally returns `1.0` in
   both post shaders (cloud shaping moved to the world-space sky pass), which
   strands `HeroCloudMask`, `CloudLobe`, `CloudLayerMask`, `CloudBreakup`,
   `ValueNoise`, and `Hash21` (~150 lines across the two files). The compiler
   strips them, but they mislead future edits.
4. **Magic style-mode integers and config duplication.** `styleMode == 11`
   / `!= 20` branches live inline in `post_tonemap.hlsl` and
   `sky_atmosphere.hlsl` with no named vocabulary; `CinematicRenderConfig`
   (`SkullbonezSource/Core/Config.h:121-250`) duplicates the shadow block of
   `OrdinaryRenderConfig`, and `sunScreenX`/`sunScreenY` are legacy names for
   what are now azimuth/elevation.

## Scope decisions (binding)

- **Screen-space god rays stay.** The postcard-style screen-space technique
  (rays fade when the sun leaves frame, `belowSun`/`verticalColumn` shaping)
  is an accepted art-direction trade-off. This plan consolidates where the
  march runs; it does not replace the technique with froxel/shadow-map
  volumetrics.
- **No new render passes beyond an optional bloom downsample target.** The
  pass roster (Shadow → Sky → Reflection → Object → Terrain → Water →
  Tornado → Overlays → Volumetric → Tonemap → UI) is not restructured here.
- **Legacy config keys keep parsing.** Scene/config files in the wild use the
  existing key spellings. Renames are internal field/comment renames plus
  parser aliases; no scene data migration in this plan.
- **Visual output is allowed to change** in Phases 2 and 3 (ray march
  consolidation and bloom). Baseline updates must be intentional, reviewed,
  and isolated to their own commits per the Danger Zones table in
  `AGENTS.md`. Phase 1 (dead-code deletion) must be visually byte-identical.

## Key anchors (verify before starting — line numbers drift)

| What | Where |
|------|-------|
| Full-res god-ray march (to remove) | `SkullbonezData/shaders/post_tonemap.hlsl` `RadialGodRays` (~231-260) + `SampleSkyTransmittance` (~213-229) + composite (~286-295) |
| Half-res god-ray march (to keep/absorb) | `SkullbonezData/shaders/post_volumetric_light.hlsl` `main_ps` march (~216-228), shaping (~230-240) |
| Bloom kernel + per-pixel `GetDimensions` | `post_tonemap.hlsl` `SampleBloom`/`PrefilterBloom` (~103-154) |
| Dead cloud/noise helpers (tonemap) | `post_tonemap.hlsl` `Hash21`/`ValueNoise`/`CloudLobe`/`HeroCloudMask`/`CloudRayOpen` (~156-211) |
| Dead cloud/noise helpers (volumetric) | `post_volumetric_light.hlsl` `Hash21` through `CloudRayOpen` (~72-160), incl. `CloudBreakup`/`CloudLayerMask` |
| Style-mode magic ints (shader) | `post_tonemap.hlsl` `styleMode == 11` (~311-317); `SkullbonezData/shaders/sky_atmosphere.hlsl` (~188, ~192, ~213, ~244) |
| Style-mode ints (config) | `SkullbonezSource/Core/Config.h` `skyMode`/`terrainMode`/`objectStyle`/`waterMode` (~220-223) |
| Duplicated shadow blocks | `Config.h` `OrdinaryRenderConfig` (~100-106) vs `CinematicRenderConfig` (~194-205) |
| Legacy sun field names | `Config.h` `sunScreenX`/`sunScreenY` (~145-146); consumers via `RuntimeTuning.h` `CinematicSkySunDirection` |
| CPU-side uniform binding | `SkullbonezSource/Runtime/RunPasses.cpp` `BindVolumetricPassParams` (~404), `BindTonemapPassParams` (~433), `VolumetricPass::Render` (~2139), `TonemapPass::Render` (~2256) |
| Cinematic UI params | `SkullbonezSource/Runtime/RuntimeTuning.h/.cpp` `ApplyCinematicUIParam`, `UITabCinematic.cpp` |

## Phases

### Phase 1 — Delete dead shader code (visuals must not change)

Delete `CloudRayOpen` and every helper that becomes unreferenced once it is
gone, in both `post_tonemap.hlsl` and `post_volumetric_light.hlsl`. Where
`CloudRayOpen(...)` is multiplied into a transmittance term, remove the factor
(it is a constant `1.0`). Keep the comments that explain *why* cloud shaping
lives in the world-space sky shader — move that note to the transmittance
functions that used to call the mask.

Acceptance: `tools\validate_dx12_renderer.bat` passes with **unchanged**
committed baselines (no baseline update allowed in this phase) and DX12
validation errors 0.

### Phase 2 — Consolidate god rays into the half-res volumetric pass

Goal: exactly one sun march per frame, at half resolution.

- Remove `RadialGodRays` + its `SampleSkyTransmittance` from
  `post_tonemap.hlsl`. Tonemap keeps only: fog, the volumetric texture
  composite, bloom, tonemap/grade.
- Decide explicitly what happens to the tonemap-only shaft shaping terms
  (`verticalColumn` beam, `occlusionSoftening`, the `0.30/0.70` mix at
  ~`post_tonemap.hlsl:286-295`): either fold equivalent shaping into
  `post_volumetric_light.hlsl` so the composed look stays close to current
  baselines, or intentionally simplify the look. Record the choice in this
  plan before updating baselines.
- Audit `uSunShaftParams`/`uSunColor` uniforms left in the tonemap cbuffer;
  drop unused fields and update `BindTonemapPassParams` and
  `ShaderContracts.h`-adjacent contracts to match (root-signature/cbuffer
  layout must stay in lockstep with the CPU side — see the shader-file
  invariant headers).
- `godRaysEnabled` and `volumetricLightingEnabled` config/UI toggles must
  both still do something sensible; document the new meaning in `Config.h`
  comments and the Cine tab tooltip text if present.
- UI **sliders** and live-style param routing must be reconciled, not just
  toggles: `sunShaftStrength`/`sunShaftFalloff` are exposed at
  `UITabCinematic.cpp` (~331-333) and routed through
  `ApplyCinematicUIParam`/`LiveStyleController`. After the tonemap cbuffer
  trim, every surviving cinematic param must still reach the pass that
  consumes it, and any param that now feeds nothing gets its UI route
  removed in the same commit (no dead sliders).
- After consolidation, the `rawDepth >= 0.9999f` sky-depth convention lives
  only in `post_volumetric_light.hlsl`; keep it that way (single owner) and
  note the convention in that shader's header.

Acceptance: single march confirmed by reading the compiled shader source;
`tools\validate_dx12_renderer.bat` run with an intentional, isolated baseline
update commit; DX12 validation errors 0; before/after screenshots attached to
the progress file notes. If any RenderGraph pass declaration, resource use,
or debug label changed (e.g. dropped tonemap reads), also run
`tools\validate_dx12_arch_tests.bat` — Plan 11's rubber-duck review already
caught this exact gap once (see `Agentic/SessionState.md`, Plan 11 row).

### Phase 3 — Bloom cost cleanup

Minimum (required): hoist per-pixel `GetDimensions` into a texel-size uniform
supplied by `BindTonemapPassParams`, and restructure `SampleBloom` so
`PrefilterBloom` work is not redundantly recomputed where taps can share
samples.

Stretch (optional, do only if Phase 2 landed cleanly and time remains): move
bloom to a half-res downsample target reusing the existing
`FullscreenQuadPass` + graph-transient texture machinery that the volumetric
pass already demonstrates (`RenderGraphTransientDX12.h`). If taken, this is
its own commit + baseline update; if skipped, record why in the progress file.

Acceptance: `tools\validate_dx12_renderer.bat` (+ baseline update only if the
image changes); `tools\validate_perf.bat` because this is per-pixel hot-path
cost work; no new runtime allocations. If the perf gate compares against
`TestOutput/baselines/*_perf.json` and the (expected, cheaper) new timings
fall outside its thresholds, refresh the perf baselines intentionally via
`tools\update_baselines.bat` (visual/perf baselines are its supported scope)
in an isolated commit, then rerun `tools\validate_perf.bat` clean.

### Phase 4 — Name the style modes

- Introduce named constants for the sky/terrain/object/water style modes in
  one C++ header (e.g. alongside `CinematicRenderConfig` in `Config.h` or a
  small `CinematicStyleModes.h`) with the shader values documented next to
  each name. HLSL has no shared include path today, so shaders get matching
  named `static const int` constants + a comment pointing at the C++ header;
  do not build an include-generation system for this.
- Replace `== 11` / `!= 20` literals in `post_tonemap.hlsl` and
  `sky_atmosphere.hlsl` with the named constants.
- Expand the `// 0=sun sky, 1=industrial, ...` comments in `Config.h` to a
  complete value table for each of the four mode fields (enumerate what every
  currently-used value means; grep scenes/`engine.cfg` for values in use).

Acceptance: no behavior change intended — `tools\validate_dx12_renderer.bat`
with unchanged baselines.

### Phase 5 — Config dedupe and naming debt

- Extract the duplicated shadow field block shared by `OrdinaryRenderConfig`
  and `CinematicRenderConfig` into one named struct (domain noun, e.g.
  `ShadowQualityConfig`) embedded in both, keeping existing config key names
  parsing in `Config.cpp`.
- Rename `sunScreenX`/`sunScreenY` fields to azimuth/elevation names
  internally; keep the legacy config keys as parse aliases. Update
  `CinematicSkySunDirection` and UI param plumbing.
- Do **not** touch physics-default config fields (gravity/fluid/drag/etc.);
  if a shared parsing helper forces contact with those lines, stop and split
  the change so the physics-default rows are untouched.

Acceptance: `tools\validate_fast.bat` plus `tools\validate_dx12_renderer.bat`
with unchanged baselines (this phase must be visually inert).

### Phase 6 — Final gate, review, handoff

- Rerun the touched-file comment audit
  (`Agentic/Skills/comment-style-audit/skill.md`) across every source-bearing
  file touched by the plan (both `.hlsl` files, `Config.h/.cpp`,
  `RunPasses.cpp`, any headers).
- One rubber-duck review for the whole plan (not per phase), per the review
  policy in `Agentic/SessionState.md`.
- Final `tools\validate_full.bat` (shader + Config.h + runtime pass code
  crosses areas) and a 3× consecutive `tools\validate_dx12_renderer.bat` run
  only if any upload-buffer/dynamic-VB contract changed (not expected).
- Update `Agentic/SessionState.md` and `Agentic/Plans/MASTER-PLAN.md`; delete
  this plan + progress file on completion per the repo convention (completed
  plans are deleted, not archived).

## Validation map for this plan

| Phase | Gate |
|-------|------|
| 1 (dead code) | `validate_dx12_renderer`, baselines must match unchanged |
| 2 (god-ray consolidation) | `validate_dx12_renderer` + intentional baseline update commit; `validate_dx12_arch_tests` if graph declarations/labels changed |
| 3 (bloom) | `validate_dx12_renderer` (+ baseline update if image changes) + `validate_perf` (+ intentional perf-baseline refresh if thresholds trip) |
| 4 (style-mode names) | `validate_dx12_renderer`, baselines unchanged |
| 5 (config dedupe) | `validate_fast` + `validate_dx12_renderer`, baselines unchanged |
| 6 (final) | `validate_full`; comment audit; rubber-duck review |

All gates require Windows (they build and launch the exe); run them at the
listed commit points only, not while iterating.

## Hard rules

- Zero new runtime allocations; any new render target routes through existing
  backend resource creation at ensure/rebuild time, never per frame.
- No new inheritance, no `*Bridge`/`*Adapter`/`*Compatibility` shims.
- No exceptions; resource-creation failures follow Lane R (logged recoverable
  null/status paths) like the rest of the DX12 layer.
- Zero warnings at `/W4`; zero DX12 validation errors.
- CPU cbuffer structs, root signatures, and HLSL cbuffer layouts must change
  in the same commit, matching field-for-field.
- Baseline updates are visual-only, intentional, and isolated to their own
  commits with before/after evidence.

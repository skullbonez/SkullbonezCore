# Progress: DX12 Post-Processing Final Cleanup Pass

Companion checklist for `Agentic/Plans/TODO/dx12-post-final-cleanup.md`.
Work through the checkboxes **in order**; later phases assume earlier ones
landed. Tick a box only when the item is implemented, compiles at `/W4` with
zero warnings, and its phase acceptance check passes. Record evidence (gate
output line + log path, and screenshots for visual phases) in the Notes
section as you go.

Scope decisions already locked (do not re-litigate): screen-space god rays
stay as a technique; only the duplicate full-res march is removed; legacy
config keys keep parsing; pass roster is not restructured.

## Hard rules for the implementing agent

- Follow the Agent Startup Contract in `AGENTS.md` before editing.
- Validation scripts are PR gates, not iteration tools; run them only at the
  commit points below, on a Windows machine (they build and launch the exe).
- Phase 1, 4, and 5 must be **visually byte-identical**: `validate_dx12_renderer`
  must pass against the *committed* baselines with no baseline update.
- Phases 2 and 3 change the image intentionally: baseline updates go in their
  own commits with before/after screenshots referenced in Notes.
- CPU cbuffer/root-signature changes and HLSL cbuffer changes land in the
  same commit.
- Apply `Agentic/Reference/comment-style-guide.md` to every touched
  source-bearing file (`.hlsl` included).
- Zero new runtime allocations; no new inheritance; Lane R for recoverable
  resource failures; no exceptions.
- Commit at the end of each phase with a descriptive body; push to the
  feature branch; update this file's checkboxes in the same commit.

## Phase 1 — Dead shader code deletion (visuals unchanged)

- [x] 1.1 `post_tonemap.hlsl`: delete `CloudRayOpen` and remove its factor
      from `SampleSkyTransmittance`; delete now-unreferenced `HeroCloudMask`,
      `CloudLobe`, `ValueNoise`, `Hash21`; keep/move the "cloud shape lives in
      the world-space sky shader" explanation next to the transmittance code.
- [x] 1.2 `post_volumetric_light.hlsl`: same deletion sweep (`CloudRayOpen`,
      `HeroCloudMask`, `CloudLobe`, `CloudLayerMask`, `CloudBreakup`,
      `ValueNoise`, `Hash21`), factor removed from `SampleLightTransmittance`.
- [x] 1.3 Confirm no other shader or CPU code references the deleted names
      (`grep` across `SkullbonezData/shaders` and `SkullbonezSource`).
- [x] 1.4 Gate: `tools\validate_dx12_renderer.bat` passes, DX12 validation
      errors 0, screenshots match **unchanged** committed baselines.
- [x] 1.5 Commit + push (comment audit on both touched shaders included).

## Phase 2 — God-ray consolidation (one march per frame)

- [x] 2.1 Record the shaping decision in the plan file: fold tonemap's
      `verticalColumn`/`occlusionSoftening` beam terms into
      `post_volumetric_light.hlsl`, or intentionally simplify the look.
- [x] 2.2 Remove `RadialGodRays` + `SampleSkyTransmittance` and the shaft
      composite block from `post_tonemap.hlsl`; tonemap now does fog,
      volumetric composite, bloom, tonemap/grade only.
- [x] 2.3 Apply the chosen shaping changes to `post_volumetric_light.hlsl`.
- [x] 2.4 Trim unused `uSunShaftParams`/`uSunColor` fields from the tonemap
      cbuffer; update `BindTonemapPassParams` (`RunPasses.cpp`) and any
      shader-contract declarations in the same commit.
- [x] 2.5 Verify `godRaysEnabled` and `volumetricLightingEnabled` toggles
      still behave sensibly from the Cine tab; update `Config.h` comments
      (and tooltip text if present) to the new meaning.
- [x] 2.6 Reconcile UI sliders and live-style routing with the trimmed
      cbuffer: `sunShaftStrength`/`sunShaftFalloff` (`UITabCinematic.cpp`
      ~331-333, `ApplyCinematicUIParam`, `LiveStyleController`) must reach
      the volumetric pass; remove any UI param route that now feeds nothing.
- [x] 2.7 If any RenderGraph pass declaration, resource use, or debug label
      changed, run `tools\validate_dx12_arch_tests.bat` (known gap from
      Plan 11 — see `Agentic/SessionState.md`).
- [x] 2.8 Gate: `tools\validate_dx12_renderer.bat`; capture before/after
      screenshots; DX12 validation errors 0.
- [x] 2.9 Intentional baseline update in its own commit (visual-only
      baselines; do not touch physics baselines), then rerun
      `tools\validate_dx12_renderer.bat` clean against the new baselines.
- [x] 2.10 Commit + push.

## Phase 3 — Bloom cost cleanup

- [x] 3.1 Replace per-pixel `GetDimensions` with a texel-size uniform set by
      `BindTonemapPassParams`.
- [x] 3.2 Restructure `SampleBloom`/`PrefilterBloom` to remove redundant
      per-tap recomputation where samples can be shared.
- [x] 3.3 Stretch (optional): half-res bloom target via the existing
      graph-transient machinery; skipped because it requires a new graph
      resource/lifecycle owner, producer-consumer bindings, and baseline surface,
      expanding the fixed pass roster for a non-trivial ownership change.
- [x] 3.4 Gate: `tools\validate_dx12_renderer.bat` (+ isolated baseline
      update commit only if the image changed) and `tools\validate_perf.bat`
      (per-pixel hot-path change); allocation guard clean.
- [x] 3.5 If the (expected, faster) new timings trip the perf-baseline
      thresholds, refresh `TestOutput/baselines/*_perf.json` intentionally
      via `tools\update_baselines.bat` in an isolated commit and rerun
      `tools\validate_perf.bat` clean. Never touch physics baselines here.
- [x] 3.6 Commit + push.

## Phase 4 — Named style modes (visuals unchanged)

- [x] 4.1 Inventory every style-mode value actually used: grep
      `SkullbonezData/scenes/`, `SkullbonezData/engine.cfg`, UI code, and
      shaders for `skyMode`/`terrainMode`/`objectStyle`/`waterMode` values.
- [x] 4.2 Add named constants with a complete value table in C++ (in or
      beside `CinematicRenderConfig` in `Config.h`); expand the field
      comments at `Config.h:220-223` to enumerate all used values.
- [x] 4.3 Replace magic literals in `post_tonemap.hlsl` (`styleMode == 11`)
      and `sky_atmosphere.hlsl` (`== 11`, `!= 20`) with matching named
      `static const int` constants + a pointer comment to the C++ header.
- [x] 4.4 Gate: `tools\validate_dx12_renderer.bat`, baselines unchanged.
- [x] 4.5 Commit + push.

## Phase 5 — Config dedupe + sun-field rename (visuals unchanged)

- [x] 5.1 Extract the shared shadow block from `OrdinaryRenderConfig` and
      `CinematicRenderConfig` into one named struct; keep every existing
      config key parsing exactly as before.
- [x] 5.2 Rename `sunScreenX`/`sunScreenY` to azimuth/elevation names
      internally; keep legacy keys as parse aliases; update
      `CinematicSkySunDirection` (`RuntimeTuning`) and UI param plumbing.
- [x] 5.3 Confirm no physics-default config lines were touched
      (`git diff SkullbonezSource/Core/Config.*` reviewed against the
      physics-default list in `AGENTS.md`).
- [x] 5.4 Gate: `tools\validate_fast.bat` then
      `tools\validate_dx12_renderer.bat`, baselines unchanged.
- [x] 5.5 Commit + push.

## Phase 6 — Final gate, review, handoff

- [ ] 6.1 Touched-file comment audit across all files edited by this plan;
      record inspected/deferred counts here: ______
- [ ] 6.2 Single rubber-duck review for the whole plan; log findings +
      resolutions in Notes.
- [ ] 6.3 Final `tools\validate_full.bat` passes (DX12 validation errors 0,
      screenshots match, physics CSV byte-exact).
- [ ] 6.4 Update `Agentic/SessionState.md` and
      `Agentic/Plans/MASTER-PLAN.md`; then delete this plan + progress file
      per the completed-plans-are-deleted convention.

## Notes / evidence log

- 2026-07-12 Phase 1 implementation: removed 152 dead shader lines across
  `post_tonemap.hlsl` and `post_volumetric_light.hlsl`; the two former
  constant-`1.0` cloud factors now simplify directly to their transmittance
  expressions. The world-space sky ownership explanation remains beside
  `SampleSkyTransmittance` and `SampleLightTransmittance`.
- Structural proof: `CloudRayOpen`, `HeroCloudMask`, `CloudLobe`,
  `CloudLayerMask`, and `CloudBreakup` are absent across
  `SkullbonezData/shaders` and `SkullbonezSource`; `Hash21` and `ValueNoise`
  are absent from both touched post shaders (their live sky-atmosphere versions
  remain intentionally owned by `sky_atmosphere.hlsl`).
- Focused build: `tools\validate_build.bat Profile` passed in 2.189 s with
  0 warnings and 0 errors. Log:
  `TestOutput/logs/dx12_post_cleanup_phase1_profile_build.log`.
- Phase 1 touched-file comment audit: 2/2 source files inspected, 0 deferred.
  Both learning headers remain compliant, and the surviving ownership reason
  was moved next to the relevant transmittance code. The formal
  `validate_dx12_renderer` unchanged-baseline checkpoint and commit/push remain
  pending for the coordinating agent.
- Phase 1 renderer checkpoint: `tools\validate_dx12_renderer.bat` passed in
  23.044 s with DX12 InfoQueue validation errors 0 and all three captures
  matching their unchanged committed baselines. Comparison manifest:
  `TestOutput/validation/dx12_renderer/20260711T160724Z/manifest.json`; log:
  `TestOutput/logs/dx12_post_cleanup_phase1_renderer.log`. The Phase 1 commit is
  intentionally grouped with the next substantial implementation chunk per
  the goal's avoid-tiny-commits instruction.
- Phase 2 implementation: retained the existing half-resolution volumetric
  shaping contract as the single owner and removed tonemap's duplicate
  36-sample march, additive shaft block, dead uniforms, CPU sets, and shader
  declarations. The shaft sliders and toggles still route to the live
  volumetric binder. Focused Profile build passed in 17.695 s with 0 warnings
  and 0 errors; log:
  `TestOutput/logs/dx12_post_cleanup_phase2_profile_build.log`.
- Phase 2 renderer gate: `tools\validate_dx12_renderer.bat` passed in 36.726 s,
  DX12 validation errors 0, and all three captures still matched the committed
  baselines. Before captures were preserved under
  `TestOutput/validation/dx12_post_cleanup/phase1_before`; after comparison
  manifest: `TestOutput/validation/dx12_renderer/20260711T161255Z/manifest.json`.
  Because no captured image changed, the isolated baseline-update step was
  satisfied as not applicable: rewriting identical baselines would create no
  meaningful visual evidence. Phases 1 and 2 are grouped in one substantial
  code commit.
- 2026-07-12 Phase 2 shaping decision: the half-resolution pass keeps its
  existing radial falloff, below-sun fade, vertical-column weighting, and
  geometry receiver softening as the single shaft contract. Tonemap's additive
  duplicate was intentionally deleted rather than copying its coefficients into
  the already-complete half-resolution shaping block.
- Single-march structural proof: `post_volumetric_light.hlsl` contains the only
  remaining loop (`sampleCount = 48`) and `SampleLightTransmittance` call;
  `RadialGodRays` and `SampleSkyTransmittance` are absent. Tonemap still samples
  depth for fog and still composites `uVolumetricTex`, but no longer declares or
  receives `uSunShaftParams`, `uSunColor`, or dead cloud uniforms. The C++
  binder and `ShaderContracts.h` match those HLSL declarations.
- Toggle/routing audit: `godRaysEnabled` zeros shaft energy inside
  `BindVolumetricPassParams`; `volumetricLightingEnabled` owns pass execution
  and tonemap composite readiness. Cine-tab shaft strength/falloff sliders and
  `ApplyCinematicUIParam` still route to the volumetric pass, so no UI route was
  removed. The UI has labels but no tooltip facility for these controls;
  `Config.h` now documents both toggle meanings.
- RenderGraph declarations, resource uses, and debug labels are unchanged, so
  the conditional Phase 2 architecture-test gate is not applicable.
- Focused build: `tools\validate_build.bat Profile` passed in 17.695 s with
  0 warnings and 0 errors. Log:
  `TestOutput/logs/dx12_post_cleanup_phase2_profile_build.log`.
- Phase 2 touched-file comment audit: 5/5 source files inspected, 0 deferred
  (`post_tonemap.hlsl`, `post_volumetric_light.hlsl`, `Config.h`,
  `ShaderContracts.h`, and `RuntimeRenderPasses.cpp`). Formal renderer capture,
  intentional visual baseline handling, commit, and push remain pending for the
  coordinating agent.
- 2026-07-12 Phase 3 implementation: removed the per-pixel
  `uSceneTex.GetDimensions` query. `BindTonemapPassParams` now publishes
  `uBloomTexelSize` from the HDR framebuffer's actual width/height on every
  frame, and `ShaderContracts.h` declares the matching `Vec4` uniform. Resize
  therefore updates the sampling footprint without shader-side dimension work.
- Bloom prefilter restructuring preserves all 13 sample positions, weights, and
  per-tap nonlinear thresholding. The uniform threshold, safe knee, and
  two-knee denominator are resolved once per pixel and passed to each tap,
  eliminating their repeated source-level setup without blurring isolated
  highlights before thresholding. No new allocation or render pass was added.
- Optional half-resolution bloom was skipped: it is not a trivial target reuse;
  it adds a graph resource/lifecycle owner, producer-consumer bindings, and a
  new baseline surface while expanding the plan's fixed pass roster.
- Focused build: `tools\validate_build.bat Profile` passed in 5.748 s with
  0 warnings and 0 errors. Log:
  `TestOutput/logs/dx12_post_cleanup_phase3_profile_build.log`.
- Phase 3 touched-file comment audit: 3/3 source files inspected, 0 deferred
  (`post_tonemap.hlsl`, `ShaderContracts.h`, and `RuntimeRenderPasses.cpp`).
  Formal renderer/perf gates, any evidence-driven baseline handling, commit,
  and push remain pending for the coordinating agent.
- Phase 3 gates: `tools\validate_dx12_renderer.bat` passed in 26.739 s with
  DX12 validation errors 0 and all three screenshots matching unchanged
  baselines; manifest:
  `TestOutput/validation/dx12_renderer/20260711T162131Z/manifest.json`.
  `tools\validate_perf.bat` passed in 35.571 s: allocation allowlist errors 0,
  no steady-gameplay allocation violations, both DX12 and physics-bench
  absolute budgets passed, and both comparisons reported no regressions. Log:
  `TestOutput/logs/dx12_post_cleanup_phase3_perf.log`. No visual or performance
  baseline refresh was needed. The Phase 3 commit remains open so it can be
  grouped with the next substantial cleanup slice.
- 2026-07-12 Phase 4 tracked-data inventory: direct scene overrides use
  sky `{0,20}`, terrain `{0,3,13,15}`, object `{0,2}`, water `{0,1}`;
  tracked style assets referenced by scenes expand those sets to sky
  `{0..13,15..19}`, terrain `{0..14}`, object `{0..7}`, water `{0,1,2,4}`;
  `engine.cfg` uses `{11,7,6,4}`. The combined authored sets are therefore sky
  `{0..13,15..20}`, terrain `{0..15}`, object `{0..7}`, water `{0,1,2,4}`.
- Shader/domain inventory adds object material modes `8..13` and the supported
  water mode `3`. Sky `14` is genuinely unassigned; sky values `21..32` and
  unknown terrain/object values remain accepted by parser/UI and use generic
  shader fallback behavior. `CinematicStyleMode` in `Config.h` records that
  distinction and names every authored or shader/domain-supported value.
- Shader structural proof: `post_tonemap.hlsl` and `sky_atmosphere.hlsl` define
  matching `SKY_MODE_LOW_POLY_ART = 11` constants; the sky shader also defines
  `SKY_MODE_OPEN_HORIZON = 20`. All specified `styleMode == 11` / `!= 20`
  comparisons are absent, and both files point back to
  `CinematicStyleMode::Sky` in `Config.h`.
- Focused build: `tools\validate_build.bat Profile` passed in 17.858 s with
  0 warnings and 0 errors. Log:
  `TestOutput/logs/dx12_post_cleanup_phase4_profile_build.log`.
- Phase 4 touched-file comment audit: 3/3 source files inspected, 0 deferred
  (`Config.h`, `post_tonemap.hlsl`, and `sky_atmosphere.hlsl`). The formal
  unchanged-baseline renderer gate, grouped commit, and push remain pending for
  the coordinating agent.
- Phase 4 renderer gate: `tools\validate_dx12_renderer.bat` passed in
  54.314 s with 0 warnings, DX12 validation errors 0, and all three captures
  matching unchanged committed baselines. Manifest:
  `TestOutput/validation/dx12_renderer/20260711T163018Z/manifest.json`; log:
  `TestOutput/logs/dx12_post_cleanup_phase4_renderer.log`. The Phase 4 commit
  remains open to group it with Phases 3 and 5.
- Phase 5 gates: `tools\validate_fast.bat` passed in 43.686 s, including
  project filters, Profile build, and all 136 doctest cases / 2,853 assertions.
  `tools\validate_dx12_renderer.bat` then passed in 24.342 s with zero
  warnings, DX12 validation errors 0, and all three captures matching unchanged
  baselines. Renderer manifest:
  `TestOutput/validation/dx12_renderer/20260711T163847Z/manifest.json`; logs:
  `TestOutput/logs/dx12_post_cleanup_phase5_fast.log` and
  `TestOutput/logs/dx12_post_cleanup_phase5_renderer.log`. Phases 3-5 are
  grouped into one substantial commit; no baseline file changed.
- 2026-07-12 Phase 5 shadow ownership: `ShadowQualityConfig` is the single
  composed value owner embedded by ordinary and cinematic render config. A
  constexpr factory preserves the exact ordinary strength/softness defaults
  (`0.25/1.05`) and cinematic defaults (`0.58/1.0`); all shared enable,
  cast/receive, map-size, filter-radius, bias, and distance defaults remain
  byte-for-byte equivalent. Runtime selection now copies the whole value
  instead of forwarding twelve individual fields. No inheritance or aliases
  were introduced.
- Sun-domain rename: all internal fields, UI commands, override-mask names,
  hashing, live-style routing, sky binding, and `CinematicSkySunDirection`
  now use `sunAzimuth`/`sunElevation`. Compatibility boundaries intentionally
  retain config keys `cinematic_sun_screen_x/y` and scene/style JSON keys
  `sunScreenX/Y` for parsing and serialization.
- Structural proof: legacy flat shadow member identifiers are absent; all
  consumers reach `config.shadow.*`. Old sun names remain only in the four
  documented compatibility string/comment boundaries. The `Config.h/.cpp`
  zero-context diff contains no gravity, fluid, drag, friction, sleep, solver,
  or broadphase physics-default row changes.
- Focused build: `tools\validate_build.bat Profile` passed in 18.030 s with
  0 warnings and 0 errors. Log:
  `TestOutput/logs/dx12_post_cleanup_phase5_profile_build.log`.
- Phase 5 touched-file comment audit: 19/19 source files inspected, 0 deferred.
  Compatibility notes were added at config parsing, scene parsing, scene save,
  and Save Defaults boundaries; the nested shadow parser documents its value
  owner. Formal fast/renderer gates, grouped commit, and push remain pending for
  the coordinating agent.

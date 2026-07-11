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

- [ ] 1.1 `post_tonemap.hlsl`: delete `CloudRayOpen` and remove its factor
      from `SampleSkyTransmittance`; delete now-unreferenced `HeroCloudMask`,
      `CloudLobe`, `ValueNoise`, `Hash21`; keep/move the "cloud shape lives in
      the world-space sky shader" explanation next to the transmittance code.
- [ ] 1.2 `post_volumetric_light.hlsl`: same deletion sweep (`CloudRayOpen`,
      `HeroCloudMask`, `CloudLobe`, `CloudLayerMask`, `CloudBreakup`,
      `ValueNoise`, `Hash21`), factor removed from `SampleLightTransmittance`.
- [ ] 1.3 Confirm no other shader or CPU code references the deleted names
      (`grep` across `SkullbonezData/shaders` and `SkullbonezSource`).
- [ ] 1.4 Gate: `tools\validate_dx12_renderer.bat` passes, DX12 validation
      errors 0, screenshots match **unchanged** committed baselines.
- [ ] 1.5 Commit + push (comment audit on both touched shaders included).

## Phase 2 — God-ray consolidation (one march per frame)

- [ ] 2.1 Record the shaping decision in the plan file: fold tonemap's
      `verticalColumn`/`occlusionSoftening` beam terms into
      `post_volumetric_light.hlsl`, or intentionally simplify the look.
- [ ] 2.2 Remove `RadialGodRays` + `SampleSkyTransmittance` and the shaft
      composite block from `post_tonemap.hlsl`; tonemap now does fog,
      volumetric composite, bloom, tonemap/grade only.
- [ ] 2.3 Apply the chosen shaping changes to `post_volumetric_light.hlsl`.
- [ ] 2.4 Trim unused `uSunShaftParams`/`uSunColor` fields from the tonemap
      cbuffer; update `BindTonemapPassParams` (`RunPasses.cpp`) and any
      shader-contract declarations in the same commit.
- [ ] 2.5 Verify `godRaysEnabled` and `volumetricLightingEnabled` toggles
      still behave sensibly from the Cine tab; update `Config.h` comments
      (and tooltip text if present) to the new meaning.
- [ ] 2.6 Gate: `tools\validate_dx12_renderer.bat`; capture before/after
      screenshots; DX12 validation errors 0.
- [ ] 2.7 Intentional baseline update in its own commit (visual-only
      baselines; do not touch physics baselines), then rerun
      `tools\validate_dx12_renderer.bat` clean against the new baselines.
- [ ] 2.8 Commit + push.

## Phase 3 — Bloom cost cleanup

- [ ] 3.1 Replace per-pixel `GetDimensions` with a texel-size uniform set by
      `BindTonemapPassParams`.
- [ ] 3.2 Restructure `SampleBloom`/`PrefilterBloom` to remove redundant
      per-tap recomputation where samples can be shared.
- [ ] 3.3 Stretch (optional): half-res bloom target via the existing
      graph-transient machinery; if skipped, record the reason here: ______
- [ ] 3.4 Gate: `tools\validate_dx12_renderer.bat` (+ isolated baseline
      update commit only if the image changed) and `tools\validate_perf.bat`
      (per-pixel hot-path change); allocation guard clean.
- [ ] 3.5 Commit + push.

## Phase 4 — Named style modes (visuals unchanged)

- [ ] 4.1 Inventory every style-mode value actually used: grep
      `SkullbonezData/scenes/`, `SkullbonezData/engine.cfg`, UI code, and
      shaders for `skyMode`/`terrainMode`/`objectStyle`/`waterMode` values.
- [ ] 4.2 Add named constants with a complete value table in C++ (in or
      beside `CinematicRenderConfig` in `Config.h`); expand the field
      comments at `Config.h:220-223` to enumerate all used values.
- [ ] 4.3 Replace magic literals in `post_tonemap.hlsl` (`styleMode == 11`)
      and `sky_atmosphere.hlsl` (`== 11`, `!= 20`) with matching named
      `static const int` constants + a pointer comment to the C++ header.
- [ ] 4.4 Gate: `tools\validate_dx12_renderer.bat`, baselines unchanged.
- [ ] 4.5 Commit + push.

## Phase 5 — Config dedupe + sun-field rename (visuals unchanged)

- [ ] 5.1 Extract the shared shadow block from `OrdinaryRenderConfig` and
      `CinematicRenderConfig` into one named struct; keep every existing
      config key parsing exactly as before.
- [ ] 5.2 Rename `sunScreenX`/`sunScreenY` to azimuth/elevation names
      internally; keep legacy keys as parse aliases; update
      `CinematicSkySunDirection` (`RuntimeTuning`) and UI param plumbing.
- [ ] 5.3 Confirm no physics-default config lines were touched
      (`git diff SkullbonezSource/Core/Config.*` reviewed against the
      physics-default list in `AGENTS.md`).
- [ ] 5.4 Gate: `tools\validate_fast.bat` then
      `tools\validate_dx12_renderer.bat`, baselines unchanged.
- [ ] 5.5 Commit + push.

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

(append gate output lines, log paths, screenshot references, and decisions
here as phases complete)

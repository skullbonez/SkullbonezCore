# DX12 Post-Processing Final Cleanup Closure

Date: 2026-07-12
Plan: `dx12-post-final-cleanup`
Result: complete, 6/6 tasks

## Result

- Removed the constant-one cloud-mask helpers and their dead noise/lobe code
  from both post shaders. World-space cloud ownership remains documented beside
  the surviving transmittance functions.
- `post_volumetric_light.hlsl` is the sole screen-space sun march. Tonemap now
  owns fog, the completed half-resolution volumetric composite, bloom, exposure,
  and grading; its duplicate 36-sample shaft path and retired uniforms are gone.
- Bloom receives inverse HDR target dimensions once per pass, performs no
  shader-side `GetDimensions`, and resolves uniform threshold/knee setup once
  while preserving all 13 taps, positions, weights, and per-tap nonlinear
  thresholding. Half-resolution bloom was intentionally skipped because it
  requires a new graph resource/lifecycle owner and expands the fixed pass
  roster.
- `CinematicStyleMode` names every authored or shader-supported sky, terrain,
  object, and water value. Sky 14 is explicitly unassigned and parser-only
  fallback values remain distinct from named shader behavior. Matching HLSL
  constants replace the specified magic comparisons.
- `ShadowQualityConfig` is one composed shadow-policy value used by ordinary
  and cinematic rendering. Exact profile defaults, config keys, scene/style
  keys, ranges, override bits, and writer behavior are preserved.
- Internal sun state now uses azimuth/elevation vocabulary throughout render,
  scene, tuning, hashing, and UI code. Legacy `cinematic_sun_screen_x/y` and
  `sunScreenX/Y` spellings remain only at compatibility boundaries.

## Review closure

The independent plan-end rubber duck found no source-correctness blocker. It
did identify that the standard renderer and perf suites run with cinematic
post-processing disabled, so their identical screenshots and timings could not
prove the changed shaft/bloom path. The perf result is therefore treated only
as broad no-regression and allocation evidence, not a measured bloom speedup.

The visual gap was closed with the purpose-built enabled
`cinematic_volumetric.scene.json`. A detached `4b519082` worktree produced the
pre-change capture and the final branch produced the post-change capture. Both
1784x961 images have the same SHA-256 and zero pixel difference, so no visual
baseline update is warranted. The isolated worktree was removed after capture.

The final comment audit reconciled 23 source-bearing files from
`4b519082..HEAD`: 23 inspected, 0 deferred. Its only finding was an empty
`Config.cpp` glossary, fixed by documenting `ConfigSetting` and the configuration
registry. No physics defaults, scenes, or physics baselines changed.

## Validation evidence

- Phase 1 Profile build: 2.189s, zero warnings/errors; unchanged renderer gate:
  23.044s, zero InfoQueue errors, screenshots accepted.
- Phase 2 Profile build: 17.695s; renderer safety gate: 36.726s, zero InfoQueue
  errors. Targeted pre-change Profile build: 21.748s, zero warnings/errors.
- Targeted cinematic captures: final 2.356s, pre-change 3.450s, both exit 0;
  matching SHA-256, average/max pixel difference 0, zero pixels over threshold.
  Artifacts live under
  `TestOutput/validation/dx12_post_cleanup/cinematic_before_after/`.
- Phase 3 Profile build: 5.748s; renderer gate: 26.739s; perf gate: 35.571s.
  Allocation policy/guard were clean and DX12/physics-bench comparisons reported
  no regressions; this is broad safety evidence rather than path-specific timing.
- Phase 4 Profile build: 17.858s; renderer gate: 54.314s with zero InfoQueue
  errors and unchanged screenshots.
- Phase 5 Profile build: 18.030s; `tools\validate_fast.bat`: 43.686s, including
  136 doctest cases / 2,853 assertions; renderer gate: 24.342s, zero InfoQueue
  errors and unchanged screenshots.
- Final `tools\validate_full.bat`: passed in 112.840s. Every CPU lane passed,
  Profile/Debug builds had zero warnings/errors, DX12 InfoQueue errors were 0,
  screenshots matched, and the 44,401-line physics regression was byte-exact.
- Final `tools\run_graphics_stress.bat 1`: completed the intended PID-bounded
  60.932s run, exited 0 after 8,229 frames / 229 scene loads, and produced empty
  stderr. Final artifact sizes: stdout 34,201 bytes, memory CSV 667 bytes, memory
  JSON 4,580 bytes.

## Handoff

Portfolio progress is 235/276 tasks (85%). The next binding serial plan is
`engine-config-decomposition`, which now receives the final composed shadow and
cinematic configuration shape rather than duplicated fields.

# Render Visibility Architecture Closure

Date: 2026-07-12
Plan: `render-visibility-architecture`
Result: complete, 7/7 tasks

## Architecture result

- Main, planar-reflection, terrain-shadow, and object-shadow instance paths now
  make independent conservative visibility decisions from store-owned world
  bounds. Terrain, sky, water, fullscreen, replay ghost, and DXR paths remain
  outside the CPU instance-culling boundary by design.
- `Maths/Frustum` extracts normalized DX12 zero-to-one planes and tests spheres
  with a conservative epsilon. A separate unnormalized half-space test reuses
  the planar reflection's water clip plane and retains straddling instances.
- Main/reflection visible indices use a fixed `MAX_GAME_MODELS` stack array.
  Shadow caster preparation carries transform/radius value records; each light
  volume filters those records without model, handle, callback, or owner access.
- Visibility survivors alone append into the pre-reserved primitive instance
  arrays. Batch end uploads exactly that compact prefix and issues one instanced
  draw, so no whole-batch fallback or steady-path growth remains.
- Per-frame UI diagnostics expose candidates, actual valid submissions, culled
  rows, and draws for all four views. Invalid convex-hull rows are counted only
  if they reach the real append/draw path.

## Visibility and performance evidence

- Standard renderer suite:
  - `water_ball_test`: main 300 candidates / 54 submitted / 246 culled;
    reflection 300 / 93 / 207.
  - `solver_smoke`: all active views 1 / 1 / 0.
  - `three_body_chaos`: terrain shadow 3 / 0 / 3; object shadow 3 / 3 / 0.
- `perf_1000.scene.json`, 1,900 post-warmup visibility samples:
  - main averaged 781.61 submitted and 218.39 culled per 1,000 candidates;
  - reflection averaged 593.50 submitted and 406.50 culled;
  - terrain shadow retained all 1,000 by design;
  - object shadow averaged 7.83 culled and reached 27 culled.
- Direct 1,940-frame A/B runs used identical `perf_1000` commands and hardware:
  pre-visibility `b5a89af3` versus final `69d91e5e` source. Main object GPU
  time improved 0.5358 to 0.4692 ms (-12.4%); reflection object GPU time
  improved 0.5602 to 0.5270 ms (-5.9%). CPU culling cost rose, and total frame
  average moved 2.6048 to 2.7390 ms (+5.2%). This mixed result is recorded
  without refreshing a perf baseline; the formal perf gate reports no
  regression.

## LOD decision

No follow-up LOD plan is opened now. Visibility materially reduces dense-view
GPU work, while the standard perf lane's final main/reflection object GPU costs
are only 0.157/0.197 ms and remain far from a frame-budget constraint. Reopen
the decision if a supported-capacity scene sustains object GPU time above 2 ms,
or if triangle-heavy registered assets make visible-set culling insufficient.

## Independent review closure

Review ids: `render-visibility-architecture-duck-01` and narrow follow-up
`render-visibility-architecture-duck-02`.

- Reviewer: `/root/visibility_plan_end_review`.
- Accounting: initial prompt approximately 800 characters and response
  approximately 2,900 characters over roughly four minutes; follow-up prompt
  approximately 900 characters and response approximately 700 characters over
  roughly one minute. Token counts were unavailable.
- Initial verdict: no rendering defect; P5 lacked a direct generated-scene
  timing delta, P6 was not yet recorded, and valid-submission counters could
  overcount malformed convex-hull rows.
- Resolution: direct `perf_1000` A/B evidence records the mixed CPU/GPU result;
  P6 records the owner decision and trigger above; submission counters now
  increment only at actual append/draw sites. The narrow follow-up cleared all
  findings and authorized closure without another review.
- Residual risk: no isolated scene test targets an off-camera caster whose
  shadow lands on-camera. Per-light-frustum structure, orthographic boundary
  tests, conservative margins, unchanged captures, and stress evidence cover
  that case indirectly.

## Validation evidence

- Touched-source comment audits: P0-P1 13/13 and P2-P3/closure 5/5, zero
  deferred.
- `tools\validate_tests.bat`: 155/155 cases and 3,522/3,522 assertions passed.
- `tools\validate_dx12_renderer.bat`: three consecutive P4 runs passed; the
  final post-review-fix run also passed with zero InfoQueue errors and 33/61/0
  screenshot maxima.
- `tools\validate_perf.bat`: DX12 and physics-bench absolute budgets and
  comparisons passed with no regressions after the counter fix.
- `tools\validate_physics.bat`: 44,401-line varied physics output remained
  byte-exact; standalone physics/handle smoke passed.
- `tools\run_graphics_stress.bat 1`: 59.777330 seconds, 12,853 frames, 358
  scene loads, empty stderr, clean PID-scoped shutdown.
- Allocation policy: 317 files scanned with zero allowlist errors.

## Handoff

Portfolio progress is 255/276 tasks (92%). The next binding plan is
`shadow-edge-quality`, beginning with S0 baseline capture.

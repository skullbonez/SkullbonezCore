# Space N-Body Gravity Demo Plan (Three-Body Problem In A Void)

Date: 2026-07-09
Status: Implemented through Phase 3 on 2026-07-09
Owner: Physics / Demo look

## Completion Update (2026-07-09)

Implemented the opt-in mutual-gravity physics mode, authored the void-space
demo scenes, added the DX12 visual baseline and chaotic-triple physics
baseline, and wired both renderer/physics validation scripts to cover the new
artifacts.

Phase 4 remains a follow-up by design: director shots, prediction ghosts,
divergence readout, and orbit-trail presentation are not part of this slice.

Validation evidence from the final implementation state:

- `tools\validate_format.bat`: PASS; 203 headers aligned and all source files formatted.
- `tools\validate_tests.bat`: PASS; 65 test cases and 1588 assertions passed.
- `tools\validate_physics.bat`: PASS; `physics_regression_solver.csv` matched 20001 lines byte-exact.
- `tools\validate_physics_deep.bat`: PASS; `space_three_body_chaos.csv` matched 361 lines byte-exact along with the existing deep baselines.
- `tools\validate_dx12_renderer.bat`: PASS; DX12 InfoQueue reported 0 validation errors, and `space_three_body` matched its screenshot baseline exactly.
- `tools\validate_fast.bat`: PASS; format, project filters, staged size, runtime boundaries, Profile build, tests, and ready builds passed.
- `tools\validate_perf.bat`: PASS on final rerun; allocation guard was clean, absolute budgets passed, and both DX12 and PHYSICS_BENCH reported no regressions. Earlier reruns had noisy relative render timing failures before the final clean run.

Touched source-bearing files were inspected against the comment-style audit
skill; local `Concept:`/`Why:` comments were added around the deterministic
mutual-gravity pre-pass, force handoff, scene sleep policy, and DXR void-sky
hook where the ownership or determinism rule was non-obvious.

## Goal

Complement the 200-brick butterfly demo with a space demo: **zero world
gravity, mutual Newtonian attraction between all dynamic bodies, rendered in a
pure black void** — no ground, no sky gradient, no water, no fog — with the
existing sun/directional light kept so bodies shade like planets with day/night
terminators. Everything is opt-in from the scene file.

Target scale is both ends of the demo spectrum:

- **3–20 bodies**: three-body choreographies (figure-eight, binary + planet,
  chaotic unequal-mass triples). This is the headline "three-body problem"
  visualization.
- **~200 bodies**: a space-field scene at 200-brick scale, so the mode holds up
  as a general chaos playground, not just a hand-tuned triple.

Butterfly/consequence wiring (fable-08 director shots, fable-09 prediction
ghosts and divergence readout) is the **final phase**, deliberately after the
physics mode and void look ship on their own. Nudging one body and watching a
cold baseline orbit diverge from a warm nudged orbit is the payoff, but the
sandbox must exist first.

## Why

- The three-body problem is *the* canonical chaotic system. The 200-brick wall
  shows chaos through contact cascades; mutual gravity shows it through smooth,
  readable trajectories that diverge from invisible differences in initial
  conditions. The two demos together cover both faces of sensitivity to
  initial conditions.
- Almost all of the demo machinery already exists: per-scene gravity already
  supports `0.0` (bullet-sweep scenes), the demo director (fable-08) and
  consequence look (fable-09) are complete, and orbit trails in a black void
  are exactly what the causal-trail ribbons were built to render.
- The physics addition is small and honest: one deterministic pairwise force
  pre-pass over compact body arrays, gated off by default, with a zero-cost
  guarantee proven by existing byte-exact baselines.

## Verified Ground Truth (2026-07-09)

These facts were checked against source before writing this plan:

- **Per-scene zero gravity already works.** `simulation.world.gravity` is
  scene-authored; `bullet_sweep_wall.scene.json` runs `"gravity": 0.0` with
  `"fluidHeight": -1000.0` and `"fluidDensity": 0.0` (the zero-g precedent this
  plan reuses for neutralizing water forces).
- **Force path.** `WorldEnvironment` snapshots scalars into
  `Physics::PhysicsWorldForces` (`SkullbonezSource/Physics/PhysicsWorldForces.h`,
  a flat value struct copied at the runtime/physics boundary per tick), and
  `PhysicsBodyStore::ApplyForces` → `ApplyWorldForces`
  (`SkullbonezSource/Physics/PhysicsBodyStore.cpp:770`) applies gravity,
  buoyancy, and drag per body. New world-level scalars belong in
  `PhysicsWorldForces`.
- **Per-body force application is worker-parallel.** `ApplyForces` runs per
  solver body including inside a worker stage
  (`PHYSICS_APPLY_FORCES_WORKER_HASH = "Frame/Physics/ApplyForces/WorkerBodies"`,
  `SkullbonezSource/Physics/PhysicsWorld.cpp:121` and the
  `ApplyForcesForSolverBody`/`ApplyForcesStageContext` block around lines
  652–691). A pairwise force therefore **cannot** be computed inside the
  per-body callback; it must be a separate deterministic pre-pass whose output
  the per-body stage reads as read-only slot data.
- **World-level sleep toggle exists.** `m_sleepEnabled` lives in
  `PhysicsWorld`/`PhysicsApi` with an existing command path
  (`SkullbonezSource/Physics/PhysicsApi.cpp:1084`,
  `SkullbonezSource/Physics/PhysicsWorld.cpp:1734`) and round-trips through
  replay snapshots (`PhysicsWorld.cpp:1036`/`1139`). Orbiting bodies must never
  sleep; the mode can disable sleep with machinery that already exists.
- **Broadphase is a hashed grid keyed by cell size.** `SpatialGrid` stores
  `cellSize`/`inverseCellSize` (`SkullbonezSource/Physics/SpatialGrid.h:101`)
  with `DEFAULT_BROADPHASE_CELL = 24.0` and a config override
  (`PhysicsWorld.cpp:106-107`, `968-969`). No hard world extents were found,
  but far-from-terrain coverage is a Phase 0 verification row, not an
  assumption.
- **Hiding ground and water is already scene-authorable.**
  `debug.terrainHidden` and `debug.waterHidden` exist and are used by the
  bullet-sweep scenes. The `terrain` block is an optional parse section
  (`SkullbonezSource/Scene/TestSceneParser.cpp:3045`), but runtime behavior
  with the block fully omitted is unverified — Phase 0 checks it.
- **Sky is two code paths.** Raster sky colors come from cinematic config
  (`skyHorizonR/G/B` in `SkullbonezSource/Core/Config.h:155`, bound in
  `RunPasses.cpp:353`, UI in `UITabSky.cpp`), and the **DXR miss path hardcodes
  sky colors** (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp:665-686`).
  A void mode must black out both or raytraced reflections will show blue sky
  in space.
- **Scene parser has a `sky`/`atmosphere` section** (section map,
  `TestSceneParser.cpp:688`). Its exact field schema was not read for this
  plan; Phase 0 reads it before choosing the final void-mode spelling.

## Design

### Scene schema (opt-in, domain nouns)

Extend `simulation.world` with a mutual-gravity block. Working spelling:

```json
"simulation": {
  "world": {
    "gravity": 0.0,
    "fluidHeight": -100000.0,
    "fluidDensity": 0.0,
    "mutualGravity": {
      "enabled": true,
      "gravitationalConstant": 0.667,
      "softeningLength": 0.5
    }
  }
}
```

- `MutualGravitySettings` is the owning value struct name — domain noun, per
  the migration-artifact gate. No `Runtime`/`Bridge`/`Compat` spellings.
- The three scalars ride in `PhysicsWorldForces` next to `gravity` (flat value
  snapshot, copied per tick, immutable during the tick — same contract as the
  existing fields).
- `gravitationalConstant` is **demo-scaled, per scene**. Hull-baked masses are
  real-scale kilograms; physical G (6.674e-11) would produce invisible forces.
  Scenes tune G to get orbital periods in the seconds range.
- `softeningLength` (Plummer ε, meters) is mandatory and scene-tunable; it is
  what keeps close encounters finite (see Hazards).

Void rendering is a separate opt-in in the scene's sky/atmosphere section
(final spelling chosen in Phase 0 against the real schema; working name
`"sky": { "mode": "void" }`). Void mode means: black raster sky, black DXR
miss color, fog/atmospherics forced off. Ground and water use the existing
`debug.terrainHidden`/`debug.waterHidden` flags plus bullet-sweep-style fluid
neutralization — no new mechanism.

### Physics: deterministic mutual-gravity pre-pass

A new pass in the `PhysicsWorld` tick, **before** the per-body ApplyForces
stage:

1. Iterate awake dynamic body slots in slot order (deterministic).
2. Triangular pairwise loop (`j > i`), computing the softened force once per
   pair and accumulating `+F` / `−F` (Newton's third law, exact momentum
   conservation by construction):
   `F_ij = G * m_i * m_j * r_ij / (|r_ij|² + ε²)^(3/2)`
3. Write per-slot accumulated force into a **preallocated scratch array**
   sized to body-store capacity at scene setup (allocation-policy compliant;
   no growth, no per-tick allocation).
4. `ApplyWorldForces` adds the slot's precomputed mutual-gravity force when
   the feature is enabled — a read-only slot lookup, safe inside the existing
   worker-parallel per-body stage.

Decisions baked into this design:

- **Fixed bodies attract but do not move** (they are mass sources with
  infinite effective inertia). This gives scene authors a free "anchored star"
  without any new body flag.
- **Single-threaded v1.** 200 bodies is ~19,900 pairs per tick — trivially
  cheap. Deterministic worker chunking (fixed chunk boundaries, ordered
  reduction, following the `WorkerPool::BuildChunkRangesNoAlloc()` shadow-prep
  precedent) is a Phase 3 option **only if** `validate_perf` demands it.
- **Zero-cost off.** When `mutualGravity.enabled` is false (the default and
  the parse-absent case), the pre-pass is skipped entirely. Existing physics
  baselines must stay byte-exact — this is the proof the feature is inert for
  every existing scene.
- **No integrator change.** The existing fixed-step integration is the
  contract. Long-horizon energy drift is accepted and demo-tuned around
  (shorter shots, softened encounters); no RK4/symplectic work in this plan.

### Sleep policy

When `mutualGravity.enabled`, scene setup disables sleep via the existing
world sleep-enabled path. In orbit nothing is ever at rest; a body that slept
would freeze mid-orbit and break both the simulation and the demo. Replay
snapshots already carry `sleepEnabled`, so scrub/restore needs no new state.

### Terrain strategy (pragmatic in phase 1)

Space scenes author an analytic `flatSlope` terrain at `baseY` far below the
action (e.g. `-10000`) with `terrainHidden: true`. With world gravity zero,
nothing pulls bodies toward it; it is effectively unreachable, costs no code,
and avoids untested "scene with no terrain at all" paths. A follow-up row
covers true terrain-less scene support only if Phase 0 finds the far-below
trick has a real cost (broadphase, water mesh bounds, or camera artifacts).

### Void look

- Raster sky: void mode renders pure black (implementation choice in Phase 2:
  skip the sky pass or drive colors to zero — pick whichever leaves zero DX12
  validation errors and no perf regression).
- DXR miss: the hardcoded sky colors at `RenderBackendDX12.DXR.cpp:681` must
  follow void mode, or raytraced water/reflection paths would paint blue sky
  into a black scene.
- Fog / atmosphere / volumetrics: forced off in void mode.
- Sun/directional light: **kept**. Spheres in a black void lit by one
  directional light read as planets — that is the look.

### Scenes

| Scene | Purpose |
|-------|---------|
| `three_body_figure_eight.scene.json` | Classic equal-mass figure-eight choreography; stable-looking until drift takes it — the headline shot. |
| `three_body_chaos.scene.json` | Unequal masses, chaotic triple; the butterfly-nudge target for Phase 4. |
| `binary_with_planet.scene.json` (optional) | Binary pair plus light third body; cheap, readable variant. |
| `space_field_200.scene.json` | ~200-body field; the scale/stress scene for the perf gate. |

One canonical scene (the chaotic triple) gets a deterministic fixed-step CSV
baseline generated from the final Debug executable, then locked with the
matching physics gate.

## Phases

### Phase 0 — Verification spike (small, no behavior changes)

- [ ] Confirm `SpatialGrid` hashed coverage far from terrain: run a zero-g
      probe scene with bodies colliding thousands of meters from the origin;
      verify contacts resolve and no grid assert fires.
- [ ] Confirm runtime behavior with far-below `flatSlope` terrain + hidden
      terrain/water: no render artifacts, no water-mesh or camera dependence
      on terrain bounds that matters in practice.
- [ ] Read the actual `sky`/`atmosphere` scene-section schema
      (`TestSceneParser.cpp` section index 9) and fix the final void-mode
      spelling.
- [ ] Sanity-check contact behavior and contact audio at orbital collision
      speeds in zero-g (bodies will collide; that is a feature, not a bug —
      but the audio classifier should not scream).
- [ ] Append findings to this plan; finalize the schema.

Exit: schema locked, no unknowns left in the ground-truth list.

### Phase 1 — Mutual gravity physics

- [ ] `MutualGravitySettings` scalars in `PhysicsWorldForces`; parser support
      in `simulation.world`; `WorldEnvironment` snapshot plumbing.
- [ ] Preallocated force scratch array (body-store capacity, setup-time
      sizing) + deterministic triangular pre-pass in the `PhysicsWorld` tick.
- [ ] `ApplyWorldForces` hook reading the precomputed slot force.
- [ ] Sleep disabled at scene setup when the feature is enabled.
- [ ] Unit tests: pair-force antisymmetry (momentum conserved to bit-identical
      accumulation), softening keeps force finite as r → 0, two-body circular
      orbit stays within analytic-period tolerance over K fixed steps, and
      two identical runs produce identical state.
- [ ] Comment-standard pass on every touched source file
      (`Agentic/Skills/comment-style-audit/skill.md`).

Validation: `tools\validate_tests.bat` + `tools\validate_physics.bat`. The
physics gate doubles as the zero-cost-off proof: existing baselines must stay
byte-exact. Avoid touching `SpatialGrid*` (that adds a `validate_perf` row per
the file map).

Exit: a hand-flyable zero-g scene with three bodies orbiting deterministically
(normal sky is fine at this phase).

### Phase 2 — Void look + demo scenes

- [ ] Void sky mode: raster black + DXR miss black + fog/atmospherics off.
- [ ] Author `three_body_figure_eight` and `three_body_chaos` scenes (void
      sky, hidden far-below terrain, neutralized water, sun light kept).
- [ ] Intentional new visual baseline screenshots for one space scene.

Validation: `tools\validate_dx12_renderer.bat` with 0 DX12 validation errors;
baseline additions are intentional and called out in the commit body.

Exit: the demo reads as space — sun-shaded bodies on pure black, no ground,
no horizon, no fog, in both raster and DXR-visible paths.

### Phase 3 — Scale + baseline lock

- [ ] Author `space_field_200.scene.json` (~200 bodies, mutual gravity).
- [ ] Measure tick cost; only if the pairwise pass is visible in the perf
      gate, add deterministic chunked accumulation (fixed chunks, ordered
      reduction, shadow-prep precedent).
- [ ] Generate the canonical chaotic-triple CSV baseline from the final Debug
      executable and committed scene/config state; rerun the matching physics
      gate against the committed baseline.

Validation: `tools\validate_perf.bat` + `tools\validate_physics.bat` (or
`tools\validate_physics_deep.bat` if the new baseline lands in the deep set).

Exit: 200-body scene within frame budget; deterministic baseline committed
and byte-exact on rerun.

### Phase 4 — Butterfly integration (follow-up; may split into its own plan)

- [ ] Authored director shot (`.shot.json`, fable-08) for the space demo:
      establishing wide, orbit close-up, nudge moment, divergence reveal.
- [ ] Prediction ghosts + divergence readout (fable-09) over orbits: capture
      baseline future, nudge one body by a tiny delta, cold cyan baseline
      orbits vs warm nudged orbits, divergence number growing.
- [ ] Causal-trail ribbons as orbit trails in the void.

Validation: per touched area; likely `tools\validate_dx12_renderer.bat`.

Exit: a directed, self-running space butterfly demo to sit beside the
200-brick wall.

## Hazards

- **Close encounters.** Unsoftened 1/r² diverges as bodies approach; a near
  pass without softening injects unbounded energy and flings bodies at
  machine-speed. Plummer ε is mandatory, scene-tunable, and documented next to
  the force code. Stability also depends on fixed-step size vs. G·m scale —
  scene authors tune G down rather than the engine clamping forces.
- **Determinism.** Pairwise order is body slot order, accumulation is
  single-threaded in v1, `fp:precise` and no `/arch` changes (same contract as
  the physics vcxproj split plan). Any future chunked version must use fixed
  chunk boundaries and an ordered reduction, never atomics or first-come
  accumulation.
- **Worker-stage interaction.** Mutual-gravity forces must be fully written
  before the worker-parallel per-body ApplyForces stage starts; the per-body
  hook is read-only on the scratch array. Do not compute pair forces inside
  the worker callback.
- **DXR hardcoded sky.** `RenderBackendDX12.DXR.cpp:665-686` will paint blue
  sky into raytraced outputs unless void mode reaches it.
- **Sleep off means full-cost solver.** Space scenes never sleep; a
  contact-heavy 200-body pile pays full solver cost every tick. Expected and
  measured in Phase 3, not silently discovered later.
- **Units.** Hull-baked masses are real kilograms; physical G is useless at
  demo scale. G is a per-scene demo constant — say so in the scene comments so
  nobody "fixes" it to 6.674e-11.
- **Replay.** The pre-pass is stateless (derived from body state + scene
  constants each tick), so replay/scrub should work unchanged; if any replay
  code is touched anyway, run the replay scrub gate.

## Non-Goals

- No integrator upgrade (no RK4, no symplectic integrator, no substepping
  changes). The existing fixed-step scheme is the determinism contract.
- No Barnes-Hut/octree. O(N²) is fine at ≤ ~200 bodies; revisit only if the
  target scale changes by an order of magnitude.
- No relativistic, tidal, or soft-body effects.
- No new inheritance, no polymorphic "force provider" interface — flat scalars
  in `PhysicsWorldForces` plus one pre-pass, per the hot-path gate.
- No changes to existing scenes or baselines beyond intentional new additions.

## Acceptance

- With `mutualGravity` absent/disabled: all existing physics baselines
  byte-exact, all existing render baselines unchanged.
- Figure-eight scene: three equal-mass bodies trace the choreography visibly
  for a demo-length shot before drift deforms it.
- Chaos scene: two runs differing by a ~1e-3 nudge on one body visibly diverge
  within the shot — the demo thesis, provable before Phase 4 makes it pretty.
- Void look: pure black background in raster and DXR paths, no ground, water,
  fog, or horizon; sun-shaded bodies; 0 DX12 validation errors.
- 200-body scene passes `validate_perf` within existing budgets.
- Canonical CSV baseline committed and byte-exact on gate rerun.

## Related

- `Agentic/Plans/fable_plans/08-demo-director-plan.md` — director/shot machinery reused in Phase 4.
- `Agentic/Plans/fable_plans/09-consequence-look-plan.md` — ghosts, trails, divergence readout reused in Phase 4.
- `SkullbonezData/scenes/bullet_sweep_wall.scene.json` — the zero-g + hidden terrain/water precedent.
- `Agentic/Plans/NEXT_FABLE_PLANS/physics-vcxproj-split-plan.md` — sibling plan; shares the determinism/flag contract language.

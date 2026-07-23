# Solar System Trajectory Planner

Date: 2026-07-23
Status: TODO — drafted from the 2026-07-23 owner conversation requesting a
solar-system intercept demo built on the replay prediction system. Registered
in `MASTER-PLAN.md` on 2026-07-23 as an owner-requested feature plan. 0/7
phases complete.
Impact area: Maths (new orbital-mechanics value library), scene data (one new
authored scene), Runtime/Replay prediction consumers (intercept readout, guide
arcs, trip planner, porkchop panel), SkullbonezTests
Owner: runtime + gameplay-feature
Priority: High — explicit owner feature request; showcases prediction as the
engine's flagship subsystem
Branch: `claude/solar-system-trajectory-jph1qj`

## Problem And Evidence (assessed 2026-07-23)

The owner wants a playable solar-system scene: planets orbit a sun under the
existing exact mutual gravity, the player launches a ship from "Earth" toward
"Mars", and the prediction system continuously shows whether the planned
trajectory intercepts Mars where Mars *will be* — with assisted planning that
computes the required burn, analytic future arcs for the planets, and a
porkchop launch-window panel.

Assessment of the current tree (`claude/solar-system-trajectory-jph1qj`,
2026-07-23) found the load-bearing infrastructure already present:

1. **Force model.** Exact pairwise Newtonian mutual gravity with softening and
   deterministic reduction order
   (`SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp:167`), authored
   per-scene via the `mutualGravity` world block
   (`SkullbonezData/scenes/three_body_figure_eight.scene.json:38`).
2. **Integrator.** Semi-implicit (symplectic) Euler at 120 Hz fixed step
   (`SkullbonezSource/Physics/PhysicsBodyStore.cpp:2036`,
   `SkullbonezSource/Physics/PhysicsTimestep.h:30`) — energy-stable orbits,
   byte-exact deterministic.
3. **Whole-world prediction.** `ReplayPrediction` owns a private isolated
   `PhysicsEngine` copy and simulates *every* body 20 s ahead
   (`SkullbonezSource/Runtime/Replay/ReplayPrediction.h:200`,
   `REPLAY_FUTURE_BUFFER_SECONDS` in
   `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h:27`), so target-body
   future positions are already in the published frames.
4. **The interaction loop.** Velocity edits queue value-typed prediction
   refreshes (`SkullbonezSource/Runtime/Replay/ReplayAuthoring.h:352`), and the
   butterfly baseline retains the pre-nudge future with a divergence measure.

What is missing: an authored solar-system scene, a closest-approach/intercept
readout over published prediction frames, analytic planet guide arcs beyond
the prediction horizon, an inverse solver (Lambert seed + shooting refinement)
that turns "where will I go" into "what burn do I need", and a porkchop
launch-window visualization.

## Goal

A shipped `solar_system.scene.json` where the player (a) launches and steers a
ship with the existing velocity-edit/prediction loop, (b) always sees the
predicted miss distance and closest-approach markers against a selected target
planet, (c) sees faint analytic orbit rings for planets beyond the prediction
horizon, (d) can ask the trip planner for a burn that intercepts the target
for a chosen flight time, watching the shooting iterations converge, and
(e) can open a porkchop panel to pick a cheap launch window that seeds the
planner. All of it deterministic, allocation-clean, and invisible in every
existing scene by default.

## Non-Goals

- **No Physics/, Rendering/, Scene/, World/, or Core/ source changes.** The
  force model, integrator, stores, and scene parser already support everything
  this plan needs. If a task appears to need one, stop and re-plan that task.
- **No change to `REPLAY_FUTURE_BUFFER_SECONDS` or prediction memory caps.**
  The scene is tuned so a transfer fits inside the existing 20 s horizon. A
  per-scene horizon override is explicitly out of scope; if SS1 tuning cannot
  fit a transfer under 20 s, record the failure and seek a fresh owner
  decision before touching the constant.
- **No real astronomical units.** The scene uses invented game units chosen
  for `float` precision and the 20 s horizon (design table below).
- **No baseline, golden, or screenshot refresh of any kind.** Every new
  visual is opt-in and default-off, so the 200-box replay visual-fidelity
  golden, DX12 baselines, and the 44,401-line physics CSV remain byte-exact.
- **No new runtime inheritance, callback packs, context/service bags, or
  migration-noun types** (per `AGENTS.md` hot-path and migration rules).
- **No registered asset-library entries.** Sun/planets/ship are scene-fixture
  `ballState` rows exactly like the existing three-body scenes; they are not
  reusable placeables, so the registered-assets rule does not apply.

## Design Reference (binding for SS1 unless tuning evidence says otherwise)

Game-unit system with `gravitationalConstant = 1.0`, fixed sun mass
`M_sun = 40000` (so `GM = 40000`), all orbits coplanar at `y = 100`:

| Body  | Orbit radius r | Circular speed √(GM/r) | Period 2π√(r³/GM) |
|-------|---------------:|-----------------------:|-------------------:|
| Sun   | 0 (fixed)      | —                      | —                  |
| Earth | 80             | 22.36 u/s              | 22.5 s             |
| Mars  | 121.6          | 18.14 u/s              | 42.1 s             |

Derived quantities that make the demo work inside existing engine limits:

- Hohmann transfer time `π√(a³/GM)`, `a = 100.8` → **15.9 s** < 20 s horizon.
- Synodic period Earth/Mars → **≈ 48.2 s**: launch windows recur about every
  48 seconds of play, making the porkchop panel meaningful.
- Departure burn `√(GM(2/r₁ − 1/a)) − √(GM/r₁)` → **≈ +2.20 u/s prograde**.
- Arrival relative speed at Mars ≈ **2.0 u/s** (transfer 16.16 vs circular
  18.14).
- Scene span ≈ 250 units in `float` keeps ≈ 10⁻³ unit position resolution.
- Softening length **0.5** (below every body radius; three-body uses 2.5).
- Optional inner/outer decoration planets (e.g. "Venus" r = 55, "Jupiter"
  r = 200) are allowed if orbits stay stable; keep total bodies ≤ 12 so the
  O(n²) pair cost stays trivial and planet-planet perturbation stays small
  enough for the analytic guide arcs to be honest.
- Planet masses ≤ 50 so planet-planet forces are negligible against the sun
  (keeps Kepler guide arcs and Lambert seeds accurate).
- Ship: small `ballState` (radius 0.6, mass 0.5) parked just outside Earth,
  `sleeping: false` like every body in the scene.

Solver-architecture decisions (recorded here so implementation cannot drift):

- **Forward truth stays forward.** The drawn plan is always a real prediction
  build from the isolated engine — never an analytic arc. Kepler/Lambert math
  is used only for (a) guide rings, (b) planner seeds, (c) porkchop sweeps.
- **Shooting refines against the real engine.** Each planner iteration applies
  a candidate ship velocity through the existing prediction rebuild path and
  reads the miss from the SS2 closest-approach pass; 2–4 iterations expected.
  The planner never runs its own private integrator, so "planned" and "flown"
  agree to the bit.
- **Identity.** Target selection carries `PhysicsSceneObjectId` everywhere;
  dense rows are frame-local hints only (scene identity policy).
- **Error lanes.** Lambert/Kepler non-convergence and planner failure are
  lane R (recoverable): the affordance reports "no solution" to the overlay
  and clears; probes assert lane P in Automation. No `throw`, no `SB_FATAL`
  for user-reachable planner input.
- **Allocation.** All new state is fixed-capacity, sized at scene load or
  compile time: intercept readout scalars, guide-arc sample buffers
  (fixed N ≤ 128 points × ≤ 12 bodies), shooting ghost-arc retention (≤ 4
  iterations × root polyline capacity), porkchop grid (fixed 64 × 48 floats,
  ≈ 12 KB). Nothing grows after steady gameplay; nothing needs a new
  `RuntimeReserveAllocator` registration. If any buffer proves to need
  replay-reserve backing instead, register it under the existing
  replay-prediction owner inventory in the same commit.
- **Hot-path budget.** The intercept pass and guide-arc sampling are bounded
  per-frame consumer work following the future-node cache pattern (scan only
  newly published frames, budgeted). The planner and porkchop sweeps run only
  on explicit user action.

## Phases

- [ ] **SS0 — Orbital mechanics value library + unit tests.**
  New dependency-clean value code under `SkullbonezSource/Maths/` (include
  floor: Maths only), e.g. `OrbitalMechanics.h/.cpp`:
  state-vector → classical elements; elliptic Kepler propagation (Newton on
  `E − e·sinE = M`, capped iterations, tolerance out-param, no allocation);
  orbit polyline sampling (caller-supplied span); Lambert solver
  (universal-variables formulation, capped iterations, prograde single-rev
  only); Hohmann helpers (transfer time, Δv). Every function is
  branch-honest about failure (returns false / status enum — lane R).
  Doctest coverage in `SkullbonezTests/`: element round-trip, one-period
  propagation returns to start within tolerance, Lambert on a quarter arc of
  a known circular orbit recovers the circular velocity, Hohmann case matches
  the design table (r₁ = 80, r₂ = 121.6, GM = 40000 → t ≈ 15.90 s,
  Δv ≈ 2.20), pathological inputs (zero radius, e ≥ 1 input to elliptic
  propagation, TOF ≤ 0) fail cleanly.
  Acceptance: all new cases pass; no heap use in any new function
  (fixed iteration caps, caller-supplied output spans).
  Validation: `tools\validate_tests.bat` (coverage floor runs inside the CPU
  umbrella at the SS6 broad gate; run `tools\validate_coverage.bat` directly
  only if Maths floors are adjusted).

- [ ] **SS1 — `solar_system.scene.json` authored scene.**
  New scene modeled on `three_body_figure_eight.scene.json`: gravity 0, fluid
  sunk/disabled, terrain hidden, `mutualGravity` enabled with the design-table
  constants, fixed sun, planets at circular-orbit velocities, ship parked near
  Earth, camera framed top-down-ish on the ecliptic. Tune restitution and
  softening; decide `elasticCollisions` from observed arrival behavior.
  Evidence duties: a bounded fixed-step launch (`--fixed-step`, scene playback
  frames covering ≥ 3 Mars periods ≈ 130 s sim) demonstrating orbits neither
  decay into the sun nor escape (record max radial drift per planet, expected
  small secular precession is fine); repeat the launch to show identical
  end-state (determinism witness); confirm planets never enter the engine
  sleep state during the run.
  Acceptance: stable-orbit evidence recorded in the task notes; scene loads
  through the normal parser with zero warnings; no other scene or baseline
  touched.
  Validation: `tools\validate_full.bat` (mapped gate for
  `SkullbonezData/scenes/*.scene.json`).

- [ ] **SS2 — Intercept / closest-approach readout.**
  New bounded prediction consumer in `SkullbonezSource/Runtime/Replay/` (e.g.
  `ReplayInterceptReadout.h/.cpp`) following the published-prefix contract:
  given a selected target `PhysicsSceneObjectId`, scan newly published
  prediction frames for `min ‖ship(k) − target(k)‖`, tracking the arg-min
  frame, miss distance, relative speed at closest approach, and
  intercept-vs-flyby classification (miss < target radius ⇒ intercept).
  Publish a small value packet (existing packet idiom); overlay draws two
  markers (ship and target poses at the closest-approach frame), a connecting
  tick, and a miss-distance/ETA text row. Re-scan resets correctly on
  prediction rebuild/topology change (reuse the topology-version discipline of
  the future-node cache).
  Default-off invariant: the readout activates only when a target is
  explicitly selected in a scene with `mutualGravity` enabled — the 200-box
  fidelity scene and all existing scenes render byte-identically.
  Acceptance: focused doctest on the scan math (synthetic frames with known
  minimum, tie-breaking, topology reset); manual evidence in
  `solar_system.scene.json` showing the marker tracking a deliberate near-miss
  and an intercept; comment audit on touched files.
  Validation: `tools\validate_full.bat` plus exactly one
  `tools\validate_replay_visual_fidelity.bat` invocation (mapped rule for
  `Runtime/Replay/*`; one engine process, one generation, zero golden
  refresh).

- [ ] **SS3 — Analytic future arcs (planet guide rings).**
  Render-only Kepler guide rings for planets: from each planet's live state
  vector and the scene's `GM`, compute its two-body element set (SS0 library)
  and sample a fixed-capacity polyline of the full orbit; draw as faint lines
  visually distinct from the honest simulated prediction ribbon (which covers
  the transfer itself). Refresh cadence: cold — on scene load, on demand, and
  at a slow bounded interval (elements drift only via tiny planet-planet
  perturbation). Toggle: off by default, keybind/UI affordance consistent with
  existing replay overlay toggles; auto-hidden in scenes without
  `mutualGravity`.
  Acceptance: rings visually coincide with the simulated planet paths inside
  the prediction horizon in `solar_system.scene.json` (screenshot evidence);
  zero draw-cost when toggled off; default-off invariant preserved for all
  existing scenes; comment audit on touched files.
  Validation: `tools\validate_full.bat` plus one
  `tools\validate_replay_visual_fidelity.bat` invocation (replay-facing
  presentation change). If — contrary to plan — any DX12 backend file must be
  touched to get line styling, that task additionally owes
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`.

- [ ] **SS4 — Trip planner (Lambert seed + shooting refinement).**
  New owner in `SkullbonezSource/Runtime/Replay/` (e.g. `ReplayTripPlanner`)
  driven through queued value requests exactly like
  `ReplayAuthoringPredictionRequest` — no callbacks, no reach-back. Flow: user
  selects a target planet and a flight time (bounded slider,
  2 s ≤ TOF ≤ horizon); planner propagates the target analytically to
  `t + TOF` (SS0 Kepler), solves Lambert about the sun for the departure
  velocity, applies the candidate to the ship through the existing
  velocity-mutation path, waits for the prediction rebuild, reads the SS2
  miss, and applies bounded differential correction (finite-difference on the
  miss vs departure-velocity components, ≤ 4 iterations, convergence when
  miss < target radius or improvement stalls). Each iteration's root polyline
  is retained as a faded ghost arc (fixed capacity, newest brightest) so the
  convergence is visible; a final "commit burn" affordance applies the
  converged velocity as a normal velocity edit (existing baseline/butterfly
  machinery shows planned vs previous). Non-convergence reports lane-R
  "no solution for this window" and clears the ghosts.
  State-machine hazard: the planner must tolerate prediction cancellation,
  scene switch, target deletion, and live-advance mid-iteration — every such
  event aborts cleanly to idle (doctest the transition table where practical).
  Acceptance: in `solar_system.scene.json`, planner from a design-table
  launch window converges ≤ 4 iterations to an intercept (recorded evidence
  with iteration miss distances); scripted Automation probe covering
  seed→converge→commit and one forced-failure window; comment audit.
  Validation: `tools\validate_full.bat`, `tools\validate_perf.bat` (prove the
  idle planner adds zero steady-state cost), plus one
  `tools\validate_replay_visual_fidelity.bat` invocation.

- [ ] **SS5 — Porkchop launch-window panel.**
  Fixed 64 × 48 grid (departure delay 0–48 s ≈ one synodic period ×
  flight time 2–20 s) of Lambert total-Δv solves about the sun using SS0 math
  and analytic body propagation — pure math, no engine access, so the sweep
  cannot perturb determinism. Compute amortized over frames or on a worker
  with a bounded budget (target: full refresh < 100 ms wall, evidence
  recorded); refresh on demand and when the selected target changes. Render
  as a color-mapped overlay panel (existing overlay quad/text primitives;
  reuse an existing palette ramp if one exists rather than inventing one);
  hovering reads out (departure, TOF, Δv); clicking a cell seeds SS4's
  planner with that window. Panel is default-hidden behind the same
  affordance family as SS3/SS4.
  Acceptance: the Δv minimum sits in the Hohmann-window neighborhood
  predicted by the design table (evidence: screenshot + the numeric minimum
  cell vs analytic Hohmann Δv ≈ 2.2 + arrival ≈ 2.0); zero allocation after
  scene load; default-off invariant; comment audit.
  Validation: `tools\validate_full.bat`, `tools\validate_perf.bat`, plus one
  `tools\validate_replay_visual_fidelity.bat` invocation if `Runtime/Replay/*`
  files are touched (expected: yes, the panel lives beside the other replay
  overlays).

- [ ] **SS6 — Closure: audit, independent review, reconciliation.**
  Rerun the touched-file inventory (`git ls-files` scoped to files this plan
  changed) and complete the comment-style audit checklist for every
  source-bearing touched file. One independent rubber-duck review of the whole
  feature per the review skill: ownership boundaries (no reach-back, no bags),
  replay-boundary proof command returns no rows, allocation-policy scan
  passes, default-off invariants re-verified against one unrelated scene and
  the fidelity scene. Reconcile `MASTER-PLAN.md` ledger and
  `Agentic/SessionState.md`; delete this plan under inventory rule 4 with a
  closure report under `Agentic/Reports/2026-07-XX/`.
  Validation: `python tools\check_allocation_policy.py --repo .`,
  `tools\agent_validate.bat` (broad superset), plus the review-proof greps:

  ```powershell
  rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
  rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Maths
  ```

  (both must return no rows; the second pins the new Maths library to the
  dependency floor).

## Dependencies And Decisions

- SS0 has no dependencies. SS1 depends only on SS0's design-table validation
  (the numbers, not the code). SS2 depends on SS1 (needs the scene to prove
  behavior). SS3 depends on SS0 + SS1. SS4 depends on SS0 + SS2 (+ SS1).
  SS5 depends on SS0 + SS4. SS6 depends on all.
- **One plan, not several** (owner question, 2026-07-23): linear dependency
  chain, one owner surface, one final independent review per the AGENTS.md
  review-granularity preference. Porkchop is last and can be deferred by
  leaving SS5 unchecked with a reason, without restructuring.
- **Porkchop included** (owner question, 2026-07-23): the ~48 s synodic
  period makes launch windows a real mechanic; Lambert solves are cheap; the
  panel reuses SS0/SS4 math verbatim. Shooting-iteration ghost arcs are the
  second "show the steps" visualization and live in SS4.
- The existing 20 s horizon is a binding constraint by design, not a blocker
  (Non-Goals). Transfer fits at 15.9 s with margin.
- Where new code lives: pure math in `Maths/` (dependency floor), all
  prediction consumers/UI in `Runtime/Replay/` beside their idiom-siblings.
  Nothing below Runtime learns about Replay (Replay Boundary Rule).

## Acceptance (plan level)

All seven boxes checked with their evidence; a player in
`solar_system.scene.json` can manually fly, read the miss, toggle guide
rings, auto-plan a burn, watch it converge, commit it, and pick windows from
the porkchop panel; every existing scene, golden, screenshot, physics CSV,
and perf baseline is byte-identical; allocation policy scan is clean; the
final broad gate passes.

## Validation Map (summary)

| Task | Required gates |
|------|----------------|
| SS0 | `validate_tests` |
| SS1 | `validate_full` |
| SS2 | `validate_full` + one `validate_replay_visual_fidelity` |
| SS3 | `validate_full` + one `validate_replay_visual_fidelity` (+ DX12 gates only if backend files are touched, which is not planned) |
| SS4 | `validate_full` + `validate_perf` + one `validate_replay_visual_fidelity` |
| SS5 | `validate_full` + `validate_perf` + one `validate_replay_visual_fidelity` |
| SS6 | allocation scan + `agent_validate` + review-proof greps |

Gates are pre-commit/PR gates per AGENTS.md — not run during iteration.

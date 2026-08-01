# Contact Energy And Warm-Start Integrity

Date: 2026-08-01
Status: REGISTERED — 0/7 phases complete
Impact area: Physics contact solving, collision diagnostics, tests, and deterministic scenes
Owner: Physics contact solver
Priority: High

## Problem And Evidence

The current solver has no complete invariant proving that a finished contact
solve cannot add unexplained energy. Existing coverage proves one elastic
sphere pair approximately preserves kinetic energy, terrain rebound speed stays
within restitution, and fluid damping is dissipative. Production diagnostics
only log a broad frame-level kinetic-energy spike above 50% or 10,000 units;
they neither fail validation nor attribute the gain to contact impulses.

The controlled `SkullbonezData/scenes/box_vibration_t0.scene.json` fixture
already demonstrates that the standing policy is inaccurate at small scale:
four dynamic bricks produce 566 meaningful downward-to-upward velocity flips
over frames 300-1199, all 900 measured frames reach the 12-iteration cap, and
none of the four bricks settles.

The superseded 2026-07-29 campaign proves both the promising direction and the
failure mode that this plan must not repeat:

- suppressing restitution on a persistent object contact alone reduced the
  controlled result from 566 flips to zero, eliminated all measured cache
  misses, and restored a one-iteration early-out;
- the later cumulative package also changed SAT selection, replaced the working
  terrain support seed, expanded terrain cache admission, changed friction
  coupling, and divided positional repair by manifold size;
- that package changed 35,091 of 44,401 varied-scene lines and made multiple
  boxes gain large vertical speed, remain awake, and visibly launch like
  popcorn; and
- the owner rejected the transition and restored the pre-`536e0a60` source and
  physics oracle.

Authoritative evidence:

- `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv0-t0.md`
- `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv1.md`
- `Agentic/Reports/2026-07-31/pre-536-physics-oracle-restoration.md`
- `Agentic/Reports/2026-07-30/persistent-contact-convergence-early-out-closure.md`

## Goal

Correct object-contact persistence and restitution so contact solving cannot
create unexplained translational or rotational energy, stable stacks retain
their prior-frame support solution, deliberate topples dissipate rather than
relaunch, and the result proves itself on isolated collisions, the four-brick
reproduction, a new giant tower, and the existing 200-box topple workload.

## Tower-Height Ruling

Tower height measures serial constraint depth, not scene capacity. The engine's
8,192-body ceiling and previously validated 200/520/1,000/2,000-body workloads
make a 64-body scene inexpensive, but the current 12-iteration PGS solver still
has to propagate load through every vertical contact.

The pre-implementation forecast is deliberately separated from acceptance:

| Solver state | Reasonable expectation |
|---|---|
| Current authoritative solver | Only 2-3 dynamic levels are trustworthy; four levels remain standing but demonstrably vibrate, and 8-16 levels are expected to be visibly unreliable. |
| Corrected solver, required floor | A centered 32-level slab tower settles without unexplained energy gain or upward launch. |
| Corrected solver, giant-scene target | A centered 64-level slab tower settles under the unchanged production gravity, friction, restitution, timestep, and 12-iteration cap. |
| Stretch measurement | 128 levels are measured and reported, but are not a closure requirement unless ES0 evidence justifies promoting them before implementation. |

ES0 replaces the forecasts with current executable measurements. No later phase
may lower the 32/64 targets or widen its energy tolerance merely to admit the
implementation. A physically induced topple is not itself a failure, but the
required centered 32- and 64-level fixtures are authored with a wide, stable
footprint and no disturbance so remaining upright and reaching sleep are valid
expectations rather than an assertion about an inherently unstable needle.

## Non-Goals

- Do not remove or retune the existing `1.0f` resting-terrain and `0.35f`
  shoreline support seeds as part of the behavioral correction. Their numerical
  behavior is retained working feed-forward policy, not the demonstrated
  vibration cause; clearer diagnostic labels remain allowed when simulation
  bytes stay identical.
- Do not increase the 12-iteration cap, loosen the convergence threshold, lower
  gravity, add damping, raise friction, lower restitution, enlarge sleep
  tolerances, or change the fixed timestep to make a fixture pass.
- Do not reinstate the rejected 25% SAT cross-family hysteresis, broad terrain
  cache admission, row-derived terrain first-touch estimate, or manifold-wide
  position-correction divisor without new isolated evidence and an explicit
  owner decision in its owning phase.
- Do not assert that kinetic energy decreases after every individual PGS row.
  Warm-start application and sequential rows may be temporarily non-monotonic;
  the invariant belongs to a complete controlled solve and to the scene-level
  external-work ledger.
- Do not replace the solver with TGS, a block solver, shock propagation, soft
  constraints, or a general constraint-graph rewrite.
- Do not replace a tracked physics, SkullScope, Replay, or visual golden without
  the owner's explicit approval of the exact final transition. Generating,
  inspecting, hashing, and retaining candidate artifacts outside their tracked
  destinations is authorized and required before that decision.

## Energy Contract

The plan distinguishes three measurements instead of treating frame-to-frame
kinetic energy as universally monotonic:

1. **Closed collision solve.** With gravity, authored impulses, motors, buoyancy,
   and penetration bias disabled, a completed contact solve with restitution in
   `[0, 1]` must conserve linear momentum and must not increase total
   translational-plus-rotational kinetic energy beyond a precision-derived
   tolerance.
2. **Biased contact solve.** Baumgarte and direct position repair may supply
   bounded separation work. Diagnostics must report that explicit budget
   separately from restitution, friction, cached impulse reuse, and synthetic
   terrain support; unexplained positive work is a failure.
3. **Gravity scene.** The four-brick, giant-tower, and 200-box workloads use
   total mechanical energy—kinetic plus gravitational potential—and explicit
   authored/external work. Contact response may dissipate energy or exchange
   kinetic and potential energy, but may not create an unattributed positive
   spike, repeat upward-launch cycle, or prevent eventual settling in a fixture
   authored to settle.

Every tolerance is fixed in ES0/ES1 from floating-point precision, controlled
same-state runs, and planted negative controls before production behavior
changes. An expected-output golden is not a substitute for these invariants.

## Phases

- [ ] **ES0 — Measure the authoritative solver and lock the acceptance
  envelope.** From the current owner-approved executable, rerun the exact
  four-brick BV0 trace and the existing 200-box topple. Generate a temporary
  same-geometry tower sweep at 4, 8, 16, 32, 64, and 128 dynamic levels without
  committing a behavioral golden. Record, per height, deterministic repeat
  equality, peak kinetic and mechanical energy, energy delta after contacts,
  maximum vertical launch speed and height gain, penetration, contact/cache
  churn, solver iterations, supported/sleeping rows, time-to-sleep, and final
  topology. State the highest current height that actually settles. Define and
  lock the local and scene-level acceptance tolerances, and prove with planted
  over-restitution/over-impulse controls that the proposed checks fail when
  energy is injected. No production solver source changes in ES0.

- [ ] **ES1 — Install complete-solve energy and momentum oracles.** Add a
  Physics-owned test calculation for total translational and rotational kinetic
  energy using the same mass and world-inertia contracts as the solver. Cover
  dynamic/dynamic and dynamic/fixed sphere, box face, off-center box, friction,
  restitution `0/0.5/1`, rotated anisotropic inertia, and a two-frame cached
  contact. Zero-force/zero-bias cases must conserve momentum and satisfy the
  non-gain bound after the complete solve. Biased cases must expose and stay
  within their explicit separation-work budget. Include negative controls that
  fail for an oversized normal impulse, stale cached impulse applied through
  incompatible geometry, and restitution above one.

- [ ] **ES2 — Add the giant tower and 200-box semantic gate.** Commit a schema-
  current `contact_energy_tower_64.scene.json` containing a fixed foundation and
  64 centered wide slab boxes under unchanged production contact settings. Add
  one semantic checker and compact SkullScope questions for the tower, the
  existing four-brick fixture, and
  `SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json`. The tower
  checker owns energy, launch, penetration, support, cache, iteration, and sleep
  facts; the 200-box checker owns bounded mechanical energy and dissipative
  topple/settling without requiring a particular chaotic final pose. Record the
  current solver's expected failures before arming the corrected result as a
  mandatory gate.

- [ ] **ES3 — Correct fresh-impact versus persistent-contact restitution.** Use
  ES0 attribution to implement the smallest object-only policy that applies
  restitution to a genuine new impact and never repeatedly to a loaded resting
  contact whose closing speed comes from rocking or `omega × r`. Re-evaluate the
  successful BV1 direction, but do not use cache membership alone if a stable
  pair/manifold lifecycle can state the invariant more honestly. Terrain
  response remains byte-exact. Land no SAT, terrain-seed, friction, position-
  correction, or iteration change in this phase. ES1, the four-brick metric,
  the tower sweep, and the 200-box energy query must all improve or remain
  bounded before ES3 can close.

- [ ] **ES4 — Repair only residual warm-start identity or validity defects.** If
  ES3 leaves material cache churn or energy gain, attribute the exact rows and
  decide whether the defect is feature identity, cache admission, or cached
  impulse compatibility with changed normals/contact arms. Prefer a local
  object-manifold identity or geometry-validity correction. Do not restore a
  global SAT challenger margin or admit unstable terrain/shoreline rows merely
  to raise hit rate. If ES3 already satisfies the evidence, close ES4 as a
  measured no-change decision rather than manufacturing work.

- [ ] **ES5 — Prove scale, determinism, and visible behavior.** Run the final
  4/8/16/32/64/128 tower matrix twice and across the repository's supported
  worker-count witnesses. The 32- and 64-level towers must remain supported,
  enter sleep, avoid upward-launch cycles, and satisfy the locked mechanical-
  energy envelope without changing solver iterations or settings. Report 128
  honestly whether it passes or fails. Run the 200-box topple to completion and
  prove it dissipates without popcorn launches, body loss, non-finite state, or
  unexplained energy. Capture waited visible DX12 evidence of the 64-level tower
  and 200-box topple for the final decision packet; no owner interaction is
  required to finish this phase.

- [ ] **ES6 — Finish engineering and prepare the owner baseline decision.** Run
  focused tests, the new semantic gate, `validate_tests`, `validate_fast`,
  `validate_physics`, `validate_physics_deep`, `validate_perf`, the single
  required `validate_replay_visual_fidelity` generation for the 200-box scene,
  and `validate_full`. A baseline-sensitive command that reaches only the exact
  expected old-oracle mismatch is evidence for the decision packet, not an
  unfinished engineering blocker. If such a command exits early, run its
  remaining baseline-independent constituent gates directly so the mismatch
  cannot conceal another failure. Generate every candidate baseline into a
  separate retained staging location, compare every changed artifact completely,
  and keep tracked goldens untouched. Audit every touched source-bearing file,
  run all seven ownership inventories, and obtain an independent read-only
  review covering energy math, cache validity, test sensitivity, hot-path cost,
  and baseline governance. Finish all source, test, scene, diagnostic, comment,
  review, performance, determinism, and visible-evidence work before asking the
  owner whether the exact staged baseline transition may replace the tracked
  artifacts.

## Terminal Owner Checkpoint

The implementation agent is expected to work through ES0-ES6 without waiting
for a baseline decision. When it contacts the owner, the correction is already
engineering-complete: no source, test, scene, checker, comment, review, or
measurement work remains, every baseline-independent gate passes, and every
baseline-sensitive failure is reduced to the enumerated old-versus-candidate
artifact transition.

The decision packet must contain the exact changed-artifact list, old and
candidate hashes, complete semantic/line-difference summaries, four-brick,
32/64/128-tower and 200-box energy evidence, deterministic repeats,
performance results, waited visible captures, and the independent review. The
single question is whether those exact staged candidates may replace the
tracked baselines.

- If approved, replace only the reviewed tracked artifacts, rerun the
  baseline-sensitive and final gates, commit the closure report, and remove the
  completed plan under inventory rule 4.
- If approval is withheld, leave the tracked baselines untouched and preserve
  the completed implementation branch plus its decision packet for owner
  disposition. Do not roll back or distort the demonstrated correction merely
  to reproduce the old oracle; any requested physics change is new owner
  direction.

ES6 may be checked and reported as **7/7 implementation complete — owner
baseline decision pending** once that packet is presented. This state is not a
technical blocker and does not authorize a baseline write. Repository closure
still waits for the owner's decision and the corresponding short follow-through.

## Dependencies And Decisions

- ES0 precedes every production edit; post-change thresholds are invalid.
- ES1 precedes ES3 so the solver cannot be changed before the independent
  collision-energy oracle exists.
- ES2 precedes ES3 so the broad failure is visible before the implementation is
  selected.
- ES3 is the only initially authorized behavior change. ES4 is conditional and
  cannot become a bundle of the rejected BV2/BV3/BV5 work.
- The persistent-contact convergence closure remains authoritative: honest
  dense-wall use of all 12 iterations is not itself a warm-start failure.
- The historical terrain support seed remains authoritative throughout this
  plan. Diagnostic vocabulary may distinguish `cachedWarmStart` from
  `terrainSupportSeed`, but any such change must preserve simulation bytes.
- No phase carries standing bounded-divergence or baseline-refresh authority.
  ES0-ES6 nevertheless run to engineering completion without owner interaction;
  the owner decides only the exact staged oracle transition presented at the
  terminal checkpoint.

## Acceptance

Engineering is complete before the terminal owner checkpoint when isolated
contact solves prove momentum conservation
and bounded kinetic energy, biased solves account for their permitted work, the
four-brick fixture has no sustained velocity reversals and reaches sleep, the
32- and 64-level towers settle without unexplained energy or upward launch, the
200-box topple dissipates without popcorn behavior, 128-level behavior is
reported honestly, all baseline-independent gates pass, and the only remaining
baseline-sensitive differences are the exact reviewed candidate transition.
The plan closes after the owner rules on that transition and the matching short
follow-through completes. Passing by retuning the scene, solver cap, material
values, sleep policy, or baseline is forbidden.

## Validation

- Focused collision-energy, restitution, friction, warm-start, and inertia tests
- New tower/contact-energy semantic checker with planted negative controls
- Exact four-brick BV0 SkullScope query
- 4/8/16/32/64/128 tower sweep, two repeats and supported worker counts
- Existing 200-box topple with mechanical-energy and launch queries
- `tools\validate_tests.bat`
- `tools\validate_fast.bat`
- `tools\validate_physics.bat`
- `tools\validate_physics_deep.bat`
- `tools\validate_perf.bat`
- `tools\validate_replay_visual_fidelity.bat` exactly once when required
- `tools\validate_full.bat`
- Seven ownership inventories and touched-source comment audit
- Independent read-only closure review

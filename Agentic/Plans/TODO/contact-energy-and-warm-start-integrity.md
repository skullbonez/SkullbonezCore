# Contact Energy And Warm-Start Integrity

Date: 2026-08-02
Status: ACTIVE — 7/7 implementation complete; owner baseline decision pending
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
create unexplained translational or rotational energy, deliberate topples
dissipate rather than relaunch, and the result proves itself on isolated
collisions, the four-brick reproduction, and the existing 200-box topple
workload. Deep serial stack convergence is now owned by the parked follow-up
plan rather than this plan's acceptance.

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

ES0 replaced the forecasts with current executable measurements. On 2026-08-02
the owner explicitly deferred stacking so the non-stacking energy work can
continue. The 32/64/128 fixtures, checker, and reports remain diagnostic assets,
but they are no longer acceptance gates for this plan and their failure must not
be described as fixed. Future stack work is parked in
`../WNF/contact-stack-stability-techniques.md` and may resume only by explicit
owner decision.

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
- Do not perform stack-specific solver work in this plan. Bullet split impulse,
  Box2D patch/position solving, Soft Step, and island-local adaptive iterations
  belong to the owner-parked stacking follow-up.
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
3. **Gravity scene.** The four-brick and 200-box workloads use
   total mechanical energy—kinetic plus gravitational potential—and explicit
   authored/external work. Contact response may dissipate energy or exchange
   kinetic and potential energy, but may not create an unattributed positive
   spike, repeat upward-launch cycle, or prevent eventual settling in a fixture
   authored to settle.

Every tolerance is fixed in ES0/ES1 from floating-point precision, controlled
same-state runs, and planted negative controls before production behavior
changes. An expected-output golden is not a substitute for these invariants.

## Phases

- [x] **ES0 — Measure the authoritative solver and lock the acceptance
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
  Complete: no measured tower height settles; 4/8 complete awake, 16/32/64
  fail deterministic candidate-list capacity contracts, and 128 fails its
  spatial-entry reservation before frame zero. Four-brick and 6,800-frame
  200-box repeats are byte-identical, while the wall exposes one lost striker
  and a mechanical-energy gain above the locked precision envelope. Exact
  metrics, hashes, tolerances, sensitive planted controls, and SkullScope
  accounting are in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es0.md`.

- [x] **ES1 — Install complete-solve energy and momentum oracles.** Add a
  Physics-owned test calculation for total translational and rotational kinetic
  energy using the same mass and world-inertia contracts as the solver. Cover
  dynamic/dynamic and dynamic/fixed sphere, box face, off-center box, friction,
  restitution `0/0.5/1`, rotated anisotropic inertia, and a two-frame cached
  contact. Zero-force/zero-bias cases must conserve momentum and satisfy the
  non-gain bound after the complete solve. Biased cases must expose and stay
  within their explicit separation-work budget. Include negative controls that
  fail for an oversized normal impulse, stale cached impulse applied through
  incompatible geometry, and restitution above one.
  Complete: Physics owns allocation-free total kinetic-energy and world
  momentum measurement using the production mass and world-inertia frames.
  Five Debug/Profile cases (87 assertions each) cover every required collision,
  cache, bias, and planted-failure branch. The complete fast gate and 3/3
  touched-source comment audit pass. Evidence is in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es1.md`.

- [x] **ES2 — Add the giant tower and 200-box semantic gate.** Commit a schema-
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
  Complete: the exact 64-level scene is tracked and runtime-admitted, and one
  semantic checker owns the tower, four-brick, and wall energy/launch/support/
  cache/sleep contracts through bounded SkullScope questions. Sensitive planted
  controls are wired into deep Physics validation. The unchanged solver's exact
  failure sets are recorded without changing capacity or behavior. Deep Physics,
  fast validation, and the 2/2 comment audit pass. Evidence is in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es2.md`.

- [x] **ES3 — Correct fresh-impact versus persistent-contact restitution.** Use
  ES0 attribution to implement the smallest object-only policy that applies
  restitution to a genuine new impact and never repeatedly to a loaded resting
  contact whose closing speed comes from rocking or `omega × r`. Re-evaluate the
  successful BV1 direction, but do not use cache membership alone if a stable
  pair/manifold lifecycle can state the invariant more honestly. Terrain
  response remains byte-exact. Land no SAT, terrain-seed, friction, position-
  correction, or iteration change in this phase. ES1, the four-brick metric,
  the tower sweep, and the 200-box energy query must all improve or remain
  bounded before ES3 can close.
  Complete: object restitution now follows the loaded exact-contact-feature
  lifecycle, which is also the warm-start compatibility key. A no-contact solve
  resets the lifecycle, and elastic mutual-gravity contacts retain their
  explicit response. Four bricks lose every meaningful vertical flip, leave the
  iteration cap, and sleep permanently. A later broad body-pair probe was
  rejected because repeatable performance evidence showed it retained excess
  rows; the exact-feature lookup restores the pre-change hot-path cost while
  preserving the correction. Terrain remains byte-exact.
  Evidence is in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es3.md`.

- [x] **ES4 — Close residual warm-start identity or validity work.** If
  ES3 leaves material cache churn or energy gain, attribute the exact rows and
  decide whether the defect is feature identity, cache admission, or cached
  impulse compatibility with changed normals/contact arms. Prefer a local
  object-manifold identity or geometry-validity correction. Do not restore a
  global SAT challenger margin or admit unstable terrain/shoreline rows merely
  to raise hit rate. If ES3 already satisfies the evidence, close ES4 as a
  measured no-change decision rather than manufacturing work.

  Initially blocked on 2026-08-02. The Physics contact-solver owner remains responsible,
  but the authorized identity/validity surface is exhausted. Canonical body-
  owned box-face/corner identity plus a cache-stable two-row anchor makes the
  8-level tower sleep with zero tail churn; the same canonical IDs remain stable
  with 2-4 warm starts across the formerly flipping loaded 64-level pair. The
  required 32- and 64-level towers nevertheless collapse with large penetration,
  launch speed, energy gain, and body loss. Multiple bounded SAT, row-selection,
  solve-order, friction, positional, and support-seed probes worsened the result.
  Independent review therefore rejects further identity/cache tuning without a
  newly observed discontinuity immediately preceding an energy spike. The exact
  evidence and rejected experimental surface are preserved in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es4-blocker.md`.

  Exact unblock condition: the owner must authorize a separately bounded stack
  load-propagation/convergence phase beyond ES4 identity/validity, or explicitly
  revise the binding 32/64 acceptance target. The former is the recommended
  direction; no target reduction is implied or authorized here. ES5 and ES6
  remain affected dependents. The verified count stays 4/7, no experimental
  source is staged, and every tracked baseline remains untouched.

  Owner ruling on 2026-08-02: the global production cap remains 12 iterations.
  The owner authorized a bounded feasibility investigation against the actual
  Bullet and Box2D implementations and asked that futile scalar-PGS tuning stop
  if the discrete 32/64-level problem cannot converge within that budget. The
  investigation concludes that the current 12-sweep scalar path is not a
  credible cold-start solution at those serial depths; no further identity,
  SAT, row-retention, seed, friction, or global-order experiment is authorized
  for the tower target. Bullet separates penetration correction and processes
  islands; Box2D 2.4 couples two-point patches and iterates position correction;
  current Box2D uses Soft Step substeps and relaxation. Those behavior changes
  remain outside this plan's current authorization.

  The owner is open to hearing about, but has not authorized implementing, one
  deterministic bounded alternative: keep 12 sweeps for every island, then
  extend only an anchored deep contact island whose own residual remains
  material, subject to a hard extra row-visit/performance budget. The exact
  findings, reduced-chain feasibility evidence, source comparison, design
  boundary, and stop condition are preserved in
  `../../Reports/2026-08-02/contact-energy-stack-stability-reference-investigation.md`.
  Owner disposition on 2026-08-02: stacking is deferred to
  `../WNF/contact-stack-stability-techniques.md`; the current plan continues
  without a 32/64/128 acceptance requirement. ES4 therefore closes as a measured
  no-change decision. The production identity experiment was removed, no new
  discontinuity justifies more cache work, and the ES3 restitution correction
  remains the only behavior change. The verified count advances to 5/7 without
  source or baseline movement.

- [x] **ES5 — Prove non-stacking determinism and visible behavior.** Repeat the
  four-brick reproduction and 200-box topple across the repository's supported
  worker-count witnesses. Prove the four bricks retain zero sustained launch
  reversals and reach sleep. Run the 200-box topple to completion and prove it
  dissipates without popcorn launches, body loss, non-finite state, or
  unexplained energy; do not require a particular chaotic final pose. Capture
  waited visible DX12 evidence of the four-brick settled state and 200-box topple
  for the final decision packet. Tower fixtures may be reported only as explicit
  deferred diagnostics and cannot pass or fail ES5.

  Complete: four bricks have zero post-frame-300 reversals and sleep 4/4 by
  frame 132. The 200-box workload remains within 12 iterations, retains all 211
  dynamic bodies, records zero invalid samples and zero repeated full-height
  popcorn cycles, and sleeps 211/211 by frame 3286. The owner explicitly
  authorized one fixed catcher beyond the impact area so the post-demo striker
  cannot leave the terrain; it changes no dynamic count or primary impact.
  Final CSVs match the automatic/worker-zero witnesses byte for byte, waited
  DX12 captures were inspected, and the semantic SQL now plants masked-energy,
  transient-invalid-state, and repeated-early-relaunch controls. Evidence is in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es5.md`.

- [x] **ES6 — Finish engineering and prepare the owner baseline decision.** Run
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

  Complete: every baseline-independent gate passes. `validate_physics` and the
  Physics phase of `validate_full` stop only at the enumerated old-oracle
  transition; the remaining deep constituents pass directly. The single
  200-box replay generation passes the complete oracle and every offline
  false-pass control against staged candidates, while the tracked manifest
  rejects only the authorized scene-provenance change. Performance passes, all
  seven ownership inventories are current, the touched-source comment audit is
  7/7, and independent review is CLEAN. Five exact candidates remain outside
  tracked destinations. The complete hashes, semantic differences, energy,
  determinism, visual, performance, and validation evidence are in
  `../../Reports/2026-08-02/contact-energy-and-warm-start-integrity-es6.md`.

## Terminal Owner Checkpoint

The 2026-08-02 owner disposition closes ES4 as a measured no-change phase,
parks stacking separately, and restores the no-pause expectation for ES5/ES6.

The implementation agent is expected to work through ES0-ES6 without waiting
for a baseline decision. When it contacts the owner, the correction is already
engineering-complete: no source, test, scene, checker, comment, review, or
measurement work remains, every baseline-independent gate passes, and every
baseline-sensitive failure is reduced to the enumerated old-versus-candidate
artifact transition.

The decision packet must contain the exact changed-artifact list, old and
candidate hashes, complete semantic/line-difference summaries, four-brick and
200-box energy evidence, deterministic repeats,
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
  The owner has supplied the exact ES4 disposition: stack work is parked and
  ES5/ES6 resume. Baseline authority remains reserved for the staged oracle
  transition at the terminal checkpoint.

## Acceptance

Engineering is complete before the terminal owner checkpoint when isolated
contact solves prove momentum conservation
and bounded kinetic energy, biased solves account for their permitted work, the
four-brick fixture has no sustained velocity reversals and reaches sleep, the
200-box topple dissipates without popcorn behavior or body loss, all baseline-
independent gates pass, and the only remaining
baseline-sensitive differences are the exact reviewed candidate transition.
The plan closes after the owner rules on that transition and the matching short
follow-through completes. Passing by retuning the solver cap, impact geometry,
material values, sleep policy, or baseline is forbidden. The owner-authorized
fixed far-edge catcher is the sole scene-boundary exception: it retains the
post-demo striker and may not affect the primary topple or dynamic-body count.

## Validation

- Focused collision-energy, restitution, friction, warm-start, and inertia tests
- Contact-energy semantic checker with planted negative controls; tower lanes
  remain deferred diagnostics rather than acceptance
- Exact four-brick BV0 SkullScope query
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

# At-Rest Ball Stability

Date: 2026-08-18
Status: Registered; 0/8 phases complete
Impact area: Physics terrain contacts, persistent contact solving, rolling and
sliding response, sleep eligibility/transition, SkullScope diagnostics, tests,
known-issue baselines, and performance
Owner: Physics contact and sleep owners
Priority: Third in the active binding order, after `ORBIT_FORECAST`
Commit name: `REST_STABILITY`

## Goal

Diagnose and fix the visible failure of balls in
`SkullbonezData/scenes/at_rest.scene.json` to settle credibly. Balls currently
roll back and forth for too long, oscillate vertically while rolling, slide
before coming to rest, and struggle to enter the sleeping state.

The repair must be evidence-led. Use SkullScope to identify whether each
symptom comes from terrain manifold/support churn, normal bias or restitution,
friction/rolling response, warm-start/cache behavior, island support, or the
sleep transition. Do not tune an unrelated global constant until bounded trace
evidence names the responsible owner and a focused negative control proves the
measurement would catch the old behavior.

The completed scene must preserve physical wake behavior and deterministic
fixed-step execution while reaching a quiet, supported sleep state without a
solver-local velocity snap.

## Owner Direction

- Keep the authored `at_rest` masses, radii, restitution, gravity, timestep,
  playback length, and initial poses as the primary witness. Do not make the
  scene easier to pass to hide an engine defect.
- Friction coefficients and sleep thresholds are owner-gated fallback
  hypotheses, not assumptions. Diagnose whether the existing values are truly
  insufficient, but first exhaust contact geometry, solver response, cache,
  support, and counter-reset defects that can manufacture the same symptoms.
- Non-production one-variable A/B probes may vary terrain/object/rolling/spin
  friction or `PhysicsSleepConfig.linearSpeed`, `angularSpeed`, and `frames` to
  measure sensitivity. No production policy value may change without an
  evidence packet and the owner's explicit approval.
- If a policy value is the only explanation consistent with the evidence,
  report the exact requested value/delta, affected consumers, before/after
  metrics, cross-scene consequences, and rejected proper fixes. Block that
  production edit until the owner says yes.
- `PhysicsSleepController` remains the only owner that transitions a body into
  sleep and zeroes residual velocities. Contact solving may publish support and
  quieting evidence, but it may not reintroduce a solver-local hard snap.
- Treat vertical vibration, tangential sliding, rolling reversals, and delayed
  sleep as separate measured symptoms. One fix may share a root cause, but a
  passing sleep time alone does not prove the motion is visually or physically
  stable.
- Preserve before/after executables, CSVs, SkullScope traces/caches, and bounded
  query outputs. No physics, replay, or known-issue baseline refresh is
  authorized until the owner reviews the candidate behavior.

## Current Evidence

- The authored witness runs 1,800 fixed-step frames at `timeScale: 10` and
  contains three dynamic balls (`ball_a`, `ball_b`, `ball_c`) plus three boxes.
  The boxes are useful control bodies; a ball-only fix must not destabilize
  them.
- `tools/validate_physics_deep.bat` already produces
  `Debug/physics_known_at_rest.csv`. The committed known-issue signature calls
  out mixed resting jitter/interpenetration, but an exact file hash is only a
  change detector, not a semantic settling oracle.
- SkullScope already records body sleep/support/inhibition state and contact
  slip speed, rolling residual, friction limit, warm-start state, and sleep
  support. Named queries cover summary, events, energy, rolling, solver,
  contacts, bodies, islands, stacks, and the `why_not_resting` question pack.
- Terrain rest policy applies rolling resistance and support eligibility in
  `PersistentContactSolver.cpp`; the sleep owner consumes support and quiet-
  frame policy in `PhysicsSleepController`. These responsibilities must not be
  collapsed into a second retained rest-state owner.

## SkullScope Diagnostic Contract

Generate the baseline trace from the unchanged scene and a preserved Debug
executable:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData\scenes\at_rest.scene.json --physics-diag Debug\at_rest_before.physicsdiag.ndjson
```

Start broad and narrow to named balls, frames, contacts, and islands:

```bat
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson summary
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson questions why_not_resting
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep,rolling_slip,friction_saturation,energy_spike,penetration_sustained,penetration_growing --limit 40
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson rolling --frames 0:1800 --limit 30
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson solver --frames 0:1800 --include-convergence --limit 30
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson energy --frames 0:1800
```

For each ball, follow suspicious windows with bounded `body`, `contacts`, and
`island` queries. Never ingest the full NDJSON or SQLite cache into model
context. Every phase handoff that uses SkullScope records the exact generation
command, raw trace/cache byte sizes, every query, per-query GPT-read output
size, total GPT-read size, and whether output was truncated, as required by
`Agentic/Skills/skore-skullscope/skill.md`.

Capture these semantic measures for each ball from first sustained terrain
support through sleep or end-of-run:

- time of last material impact, first sustained support, and sleep transition;
- sleep-counter resets, inhibition reasons, island membership, and support
  churn;
- vertical position/speed envelope and contact penetration/bias/normal impulse;
- tangential slip distance/speed, friction saturation, accumulated tangent
  impulse, spin, rolling residual, and direction-reversal count;
- warm-start/cache continuity and solver convergence around each reversal or
  vertical oscillation; and
- kinetic-energy decay, any late energy injection, and wake events.

RS0 ratifies exact completion thresholds from engine units and the unchanged
baseline. They must require all three balls to settle materially sooner after
their last real impact, eliminate visible tail vibration and repeated rolling
reversals, bound pre-sleep slip, and enter supported sleep before the authored
run ends. Thresholds cannot merely sit just beyond the old maximum.

## Phases

### RS0 — Preserve And Diagnose The Unchanged Scene

- [ ] Preserve the baseline Debug CORE/TESTS executables, known-issue CSV,
  SkullScope trace/cache, and exact hashes before any behavior-bearing edit.
- [ ] Run the broad SkullScope packet, then bounded per-ball body/contact/island
  queries around first support, reversals, vertical peaks, sleep-counter resets,
  and final motion.
- [ ] Record the causal sequence for each symptom and ratify semantic numeric
  acceptance thresholds. Distinguish initial impact motion from the late tail.
- [ ] Record box control-body metrics and 0/1/4-worker hashes.

Evidence: complete SkullScope size/query accounting, baseline semantic table,
artifact hashes, and named first suspicious frame/body/contact for each symptom.

### RS1 — Add Semantic Oracles Before The Fix

- [ ] Add a bounded at-rest stability analyzer or focused test harness that
  reports the RS0 measures without treating the full CSV hash as success.
- [ ] Pin all three balls individually plus aggregate scene completion. Keep
  box behavior as a non-regression control.
- [ ] Add planted negative controls for vertical impulse oscillation, excessive
  slip, rolling reversal, and sleep-counter reset so each oracle demonstrably
  fails.
- [ ] Keep query output bounded and preserve the existing generic SkullScope
  schema unless diagnosis proves a missing fact.

Evidence: focused tests/analyzer fixtures, four failing negative controls, and
the unchanged baseline failing the new semantic target for the expected reason.

### RS2 — Adjudicate The Root Causes

- [ ] Use trace evidence and one-variable diagnostic A/Bs to classify terrain
  contact geometry/support, normal bias/restitution, tangent friction, rolling
  resistance, warm-start/cache continuity, and sleep policy separately.
- [ ] For every proposed edit, name the invariant-owning function/stage and the
  exact symptom/metric it owns. Reject broad global tuning without causal proof.
- [ ] Treat friction-coefficient and sleep-threshold changes as diagnostic
  probes only while proper solver/contact/support fixes remain viable. If the
  evidence isolates a policy deficiency, prepare the owner-decision packet and
  stop before changing production values.
- [ ] Record why each non-owning candidate was ruled out and whether the three
  balls share one cause or need distinct repairs.
- [ ] Update the plan with the selected repair behavior before implementation.

Evidence: bounded before/A/B query comparisons, selected owner rulings, rejected
alternatives, and independent read-only diagnosis review.

### RS3 — Remove Normal-Axis Vibration

- [ ] Repair only the diagnosed normal/support/manifold/cache cause of vertical
  tail oscillation.
- [ ] Preserve legitimate restitution for material impacts; resting contacts
  must not repeatedly manufacture bounce energy below the impact threshold.
- [ ] Prove penetration, position correction, bias, normal impulse, energy, and
  support classification remain bounded for spheres and box controls.
- [ ] Add focused terrain-sphere regressions that fail the pre-RS3 behavior.

Evidence: per-ball vertical envelopes and energy traces, focused solver tests,
and unchanged initial-impact behavior where the repair should be inactive.

### RS4 — Stop Excess Sliding And Rolling Reversals

- [ ] Repair the diagnosed tangent/rolling response without injecting generic
  damping or forcing angular velocity to zero in the contact solver.
- [ ] Keep static/dynamic friction and rolling resistance dimensionally and
  timestep consistent; preserve credible no-slip rolling before rest.
- [ ] Preserve existing friction coefficients unless isolated A/B evidence
  proves they are insufficient and the owner explicitly approves the recorded
  production change.
- [ ] Prove reduced tail slip, bounded friction saturation, monotonic late
  energy decay, and no repeated back-and-forth direction changes after the last
  material impact.
- [ ] Cover slopes/moving contacts or other existing friction cases so the
  at-rest repair does not pin legitimately moving balls.

Evidence: slip/rolling residual tables, reversal counts, focused friction and
rolling tests, and cross-scene motion controls.

### RS5 — Make Supported Quiet Balls Sleep Reliably

- [ ] Keep quiet thresholds, support propagation, inhibition, and final
  transition under `PhysicsSleepController` ownership.
- [ ] Fix spurious motion and counter-reset causes before proposing any change
  to the existing linear/angular thresholds or quiet-frame count. If policy is
  still the isolated cause, block the edit on explicit owner approval.
- [ ] Ensure all three supported balls accumulate consecutive quiet frames and
  sleep within the RS0 target after their last material impact.
- [ ] Prove unsupported, externally forced, materially impacted, or island-
  connected bodies do not sleep early and wake exactly once when required.
- [ ] Add focused counter-reset and supported-transition tests that fail the old
  delayed-sleep sequence.

Evidence: per-ball support/counter/sleep timeline, wake regressions, island
tests, and no solver-local velocity snap.

### RS6 — Integrated Scene Attribution And Determinism

- [ ] Generate a fresh `at_rest_after` SkullScope trace and run the exact RS0
  query set plus `compare` against the preserved baseline.
- [ ] Run the semantic at-rest oracle twice and prove byte-identical candidate
  outputs plus 0/1/4-worker equality.
- [ ] Preserve before/after CORE/TESTS executables, CSVs, traces/caches, bounded
  query outputs, hashes, and the first attributed behavior difference.
- [ ] Check stacking, terrain-contact, slope/friction, and existing sleep scenes
  for regressions; measure physics hot-path cost and allocations.

Evidence: all-ball and box-control tables, deterministic repeat/worker hashes,
cross-scene attribution, performance, and complete SkullScope accounting.

### RS7 — Close The Plan

- [ ] Run the cumulative mapped gates after implementation and an independent
  physics/ownership/test-strength review.
- [ ] Audit every touched source-bearing file against the comment-style guide
  and reconcile its tracked-file checklist.
- [ ] Present the candidate semantic improvements and preserved artifacts for
  owner assessment. Refresh no golden without explicit approval.
- [ ] Run `tools\agent_validate.bat --plan-completion` once after review, update
  MASTER/Session ledgers, and delete this completed plan under repository
  convention.

Evidence: gate logs, review verdict, comment-audit counts, preserved artifact
hashes, baseline decision, and terminal plan-completion output.

## Acceptance Criteria

- [ ] All three balls meet the RS0 semantic targets for vertical stability,
  slip, reversal count, supported sleep latency, and final sleeping state.
- [ ] The boxes remain stable controls and no slope/moving-contact case is
  falsely pinned or slept.
- [ ] Late kinetic energy decays without solver-induced spikes, repeated resting
  restitution, or sustained/growing penetration.
- [ ] Sleep occurs only through `PhysicsSleepController` after consecutive quiet
  supported frames; wake behavior remains exact.
- [ ] Friction coefficients and sleep thresholds/frame counts are unchanged
  unless the final evidence packet records the owner's explicit approval for an
  exact policy change and its cross-scene validation scope.
- [ ] Candidate runs and 0/1/4-worker results are deterministic.
- [ ] The semantic oracle includes planted false-pass controls and does not use
  the known-issue hash as its only correctness signal.
- [ ] Every SkullScope use reports commands and raw/GPT-read size accounting.
- [ ] Physics/replay/known-issue baselines change only after explicit owner
  acceptance, with before/after executables retained.

## Validation Mapping

| Changed scope | Required pre-commit gate |
|---|---|
| Focused solver/sleep tests | `tools\validate_tests.bat` |
| Contact, terrain, friction, or sleep behavior | `tools\validate_physics.bat` and `tools\validate_physics_deep.bat` |
| Replay-visible solver values or sleep timing | `tools\validate_replay_visual_fidelity.bat` |
| Physics hot path | `tools\validate_perf.bat` plus allocation evidence |
| SkullScope diagnostic/query schema | focused query regression plus `tools\validate_physics.bat` |
| Final combined source state | `tools\agent_validate.bat --plan-completion` once at RS7 |

## Non-Goals

- Do not change the scene's masses, radii, restitution, poses, gravity,
  timestep, playback length, or time scale to manufacture a pass.
- Do not add unconditional global damping, a contact-solver velocity snap, or a
  second retained rest/sleep state owner.
- Do not change terrain/object/rolling/spin friction coefficients or any sleep
  speed/frame threshold without isolated causal evidence and explicit owner
  approval. Diagnostic-only probes do not authorize a production edit.
- Do not disable warm starting, CCD, partial-TOI integration, support
  classification, or wake propagation to reduce visible motion.
- Do not tune material response or sleep policy merely to move the pass line
  around defective contact, solver, cache, support, or counter behavior.
- Do not accept “eventually sleeps by frame 1800” while visible sliding,
  vertical vibration, or repeated rolling reversals remain.
- Do not ingest raw NDJSON/SQLite artifacts into model context or refresh a
  baseline before owner review.

## Reference Sites

- `SkullbonezData/scenes/at_rest.scene.json`
- `SkullbonezSource/Core/Config.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`
- `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `SkullbonezTests/TestSleepController.cpp`
- `SkullbonezTests/TestPhysicsStageState.cpp`
- `tools/physics_query.bat`
- `tools/physics_query.py`
- `tools/check_physics_known_issue_regression.py`
- `tools/validate_physics_deep.bat`
- `Agentic/Skills/skore-skullscope/skill.md`
- `Agentic/Reference/physics-query-reference.md`

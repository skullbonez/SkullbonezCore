# Replay Prediction Fidelity Probe — Predicted Future Must Match The Actual Future

Date: 2026-07-13
Status: Live — 0/5 tasks complete
Impact area: replay prediction capture, interaction automation asserts, doctest
unit suite, replay scrub validation lane
Owner: replay subsystem

Ledger note: NOT yet registered in `Agentic/Plans/MASTER-PLAN.md` — owner
instruction 2026-07-13: MASTER is in active use by another agent. Before the
first plan-runner commit against this plan, add the ledger row
(`replay-prediction-fidelity-probe`, 5 tasks), recompute the portfolio
denominator in that same commit, and delete this note.

## Problem And Evidence

Forward prediction is mechanically "replay of the future": the private
prediction engine is copy-assigned from the live `PhysicsEngine`, gets the same
runtime config, and restores the captured solver snapshot so "the next fixed
step reproduces" (`Runtime/Replay/RunReplayTools.cpp:3655-3674`,
`Physics/PhysicsEngine.h:158-160`
`CaptureReplaySolverSnapshot`/`RestoreReplaySolverSnapshot`).

Coverage today:

- `tools/check_replay_prediction_determinism.py` (step 3 of
  `tools/validate_replay_scrub.bat`) proves **run-to-run reproducibility**:
  two clean-process launches of the same prediction script must produce
  identical FNV-1a trajectory fingerprints and identical submitted ribbon
  vertex bytes over a 120-frame steady window.
- The `liveSolverHashStableAcrossPrediction` assert
  (`Runtime/InteractionAutomationController.cpp:417`) proves **isolation**:
  the live solver hash is unchanged while prediction steps.

The gap: **nothing compares the predicted future against the actual future.**
If solver snapshot capture/restore silently omits state that influences the
next fixed step (impulse cache rows, sleep counters, island state, accumulated
policy state), prediction diverges from what live simulation then actually
does — and every current gate still passes, because both determinism runs are
*identically wrong*. This is precisely the regression class a replay
re-architecture (`replay-monolith-decomposition.md`) could introduce, so this
plan is a binding prerequisite of that one.

## Goal

Two enforced fidelity contracts:

1. **Engine-level (unit test):** a snapshot-seeded engine copy stepped N fixed
   ticks produces per-tick state byte-identical to the original engine stepped
   the same N ticks.
2. **Runtime-level (automation probe):** with fixed-step and zero interaction
   during the horizon, the published prediction hash for tick T+k equals the
   retained live solver-track hash for tick T+k, for every k in the horizon.

## Non-Goals

- No change to prediction scheduling, publish-prefix protocol, or seeding
  semantics — this plan only observes and asserts.
- No soft-tolerance comparison: the contract is hash-exact or failed. A
  legitimate future feature that makes prediction intentionally approximate
  must change this contract explicitly, not weaken it quietly.
- No new standalone test executable (the unit case joins SKULLBONEZ_TESTS, so
  no umbrella registration is needed).

## Tasks

- [ ] **F1 — Engine-level snapshot completeness unit test.** Add
  `SkullbonezTests/TestReplayPredictionFidelity.cpp` (doctest, registered in
  `SKULLBONEZ_TESTS.vcxproj` + `.filters` in the same commit). Test shape:
  - Build a small deterministic `PhysicsEngine` fixture (a handful of bodies
    with contacts and at least one sleeping body; reuse the fixture style of
    `TestReplayRecorder.cpp`).
  - Step it K warm-up fixed ticks so contact caches and sleep state are
    non-trivial. `CaptureReplaySolverSnapshot` into a
    `ReplaySolverWorldSnapshot`, and capture per-body state.
  - Copy-assign the engine (the same seeding path prediction uses), apply the
    captured body state, `RestoreReplaySolverSnapshot`.
  - Step BOTH engines N further ticks (N ≥ 32, crossing at least one
    sleep-state transition); after every tick, compare full body state
    byte-exactly (memcmp of position/orientation/velocity records or the
    solver-sample hash — see F2 helper reuse).
  - Failure output must name the first divergent tick, body index, and field.
  Acceptance: test fails when a snapshot field is deliberately zeroed in a
  scratch mutation (prove the detector catches an incomplete restore, then
  remove the mutation), passes clean afterwards. Validation:
  `tools\validate_tests.bat`.
- [ ] **F2 — Per-tick prediction solver hash capture.** The retained live
  solver track already stores `solverHash` per sample
  (`Runtime/Replay/ReplayRecorder.h:382`; FNV-1a helpers in
  `ReplayRecorder.cpp`). Prediction build frames do not. Changes:
  - Export the existing FNV-1a solver-sample hash helpers from
    `ReplayRecorder.cpp` through a small internal header so prediction capture
    reuses the *identical* field walk — do not duplicate the hash code (a
    divergent field list would make the probe compare different things).
  - Add `uint64_t solverHash` to `RunReplayPredictionFrame`
    (`ReplayRuntime.h:647` `buildFrames`); fill it inside
    `CaptureReplayPredictionFrame` (`RunReplayTools.cpp:3678`) from the same
    per-body fields the live recorder hashes. Fixed-size field, no allocation;
    the worker writes it into its pre-sized frame row (single-writer protocol
    unchanged).
  Acceptance: with prediction active on a fixed-step scene and no interaction,
  logged predicted hash for tick T+1 equals `sourceSolverHash`'s successor
  live hash (manual spot check recorded); zero warnings. Validation deferred
  to F4's probe run plus `tools\validate_tests.bat` (recorder tests still
  pass).
- [ ] **F3 — `predictionMatchesLiveHorizon` automation assert.** In
  `Runtime/InteractionAutomationController.{h,cpp}`:
  - Add `PredictionMatchesLiveHorizon` to
    `RunInteractionAutomationAssertKind` (enum at
    `InteractionAutomationController.h:97`), JSON spelling
    `predictionMatchesLiveHorizon` with an integer minimum-tick-count operand,
    following the existing `predictionDivergenceMin` parse/dispatch pattern.
  - Evaluation: after the live simulation has advanced ≥ operand ticks past
    the prediction seed tick T, walk k = 1..operand and compare the published
    prediction frame hash for T+k against the retained live solver sample hash
    for T+k. Preconditions asserted inside the evaluator: fixed-step active,
    prediction full horizon complete, no interaction commands issued inside
    the window, and the retention window still holds tick T+1 (fail with a
    named precondition message otherwise — a silently vacuous pass is a
    defect).
  - On mismatch: probe failure (Lane P) reporting first divergent k, both
    hashes, and the seed tick.
  Acceptance: deliberately perturbing one predicted body velocity in a scratch
  mutation makes the assert fail at k=1 with the correct report; removing the
  mutation passes. Validation: `tools\validate_tests.bat` (interaction policy
  parse coverage if applicable) + F4 runtime run.
- [ ] **F4 — Fidelity interaction script and scrub-lane wiring.** Add
  `SkullbonezData/interaction/prediction_fidelity_probe.json`: fixed-step
  scene (reuse `prediction_ragdoll_wall_200.scene.json` or a smaller
  deterministic scene if runtime cost matters), enable prediction with a
  bounded horizon (e.g. 2.0s), set a path target, wait for
  `predictionFullHorizonComplete`, let live simulation run the horizon out
  with zero further input, then assert `predictionMatchesLiveHorizon` with the
  horizon's tick count. Add `tools/check_replay_prediction_fidelity.py`
  (mirror the structure, logging, and bounded-output rules of
  `check_replay_prediction_determinism.py`) and call it as step [4/4] of
  `tools/validate_replay_scrub.bat`. Update `tools/README.md`. Acceptance: the
  scrub gate fails when F3's scratch mutation is reapplied, passes clean.
  Validation: `tools\validate_fast.bat` then the changed script
  (`tools\validate_replay_scrub.bat`) per the tools file-to-gate rule.
- [ ] **F5 — Closure.** Rerun `tools\validate_tests.bat`,
  `tools\validate_replay_scrub.bat`, and `tools\validate_physics.bat`
  (byte-exact — F2 touches no live solver code, prove it) from final source.
  Record commands and key result lines. Confirm the plan in
  `replay-monolith-decomposition.md` can now cite this gate as its
  regression detector. Delete this plan per inventory rule 4 once the ledger
  row is registered and closed, with closure evidence under
  `Agentic/Reports/`.

## Dependencies And Decisions

- No dependency on other live plans; this plan should land BEFORE
  `replay-monolith-decomposition.md` starts (that plan lists this one as a
  binding prerequisite).
- Decision recorded: hash reuse over re-derivation (F2) — one field walk owned
  by the recorder, exported, never duplicated.
- Decision recorded: the probe asserts hash equality of the *stored sample
  fields*, which is the same contract past-replay restore already depends on;
  it does not hash raw engine internals.
- Hazard: retention-window sizing — the probe horizon must fit inside the
  solver-track retention window at the probe scene's tick rate, or live
  samples for early k are evicted before comparison. F4 must size horizon and
  retention together and assert the precondition rather than passing
  vacuously.

## Validation

Per-task gates named above. Plan-level: `validate_tests` (new doctest case),
`validate_replay_scrub` (new step 4), `validate_physics` byte-exact (no live
solver behavior change allowed), `validate_fast` for the tools edits.

## Definition Of Done

- A doctest case proves snapshot capture/restore completeness at the engine
  level and demonstrably catches an induced incomplete restore.
- Prediction frames carry the same solver hash the live recorder computes,
  via one shared field walk.
- `predictionMatchesLiveHorizon` is a real automation assert with enforced
  preconditions and first-divergence reporting.
- `tools\validate_replay_scrub.bat` fails on prediction/live divergence and
  passes clean, wired as a permanent step.
- Physics CSV baseline remains byte-exact against committed artifacts.

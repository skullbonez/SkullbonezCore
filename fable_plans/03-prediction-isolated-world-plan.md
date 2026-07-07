# Prediction Isolated World Plan

Date: 2026-07-06
Status: Phase 2 and Phase 4 complete on 2026-07-07; Phase 3 worker-job stepping deferred until after soak
Impact area: replay prediction, physics stepping; determinism-sensitive
Validation for this document: none (documentation-only)

## Problem

2026-07-07 update: the live mutation window described below has been removed.
Prediction now seeds and steps a replay-owned private `PhysicsEngine`, and
phase 4 added a static guardrail preventing prediction restore calls against
live physics. The historical problem statement remains here to explain why
PHYS-035 was split out from the wider physics-ownership knot.

Replay prediction does not simulate a copy of the world — it simulates the
future **inside the live physics stores** and puts the present back afterward.
Per `RunReplayPredictionVisualizer.inl` (`StepReplayPredictionJob`), every
render-frame slice does:

1. Capture live body state + solver snapshot (`liveRestoreBodies` /
   `liveRestoreWorld`).
2. Apply the prediction's saved state **onto the live stores**.
3. Step real physics some ticks (`StepReplayPredictionPhysicsTick`).
4. Capture the advanced state back out.
5. Restore the live snapshot.

The code is candid about the danger: *"Hazard: prediction owns live physics
state until the RestoreLive block below"*, fail-closed cancels on every exit
path, `_DEBUG`-only diagnostics suppression so the mutation window doesn't
pollute physics diagnostics, and a reserved "mutation budget"
(`REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS`) so restore never gets
starved mid-window. One missed restore path silently corrupts the live
simulation and breaks byte-exact determinism — the repo's most protected
invariant. Every piece of that machinery is servicing an inverted design.

## Goal / Definition of Done

- Prediction steps a **prediction-owned store bundle** (copied body store,
  solver scratch, world snapshot; shared read-only collider/terrain data).
  Live stores are never written by prediction.
- `liveRestoreBodies`, `liveRestoreWorld`, the RestoreLive block, mutation
  reserve, and diagnostics suppression are **deleted**, not bypassed.
- Prediction stepping runs as a `WorkerPool` job publishing completed frame
  prefixes; the overlay reads published snapshots only (the frames-vector
  publish pattern already exists and stays).
- A guardrail forbids prediction code from calling
  `RestoreReplaySolverSnapshot`/`ApplyReplayPredictionBodyState` against live
  stores.

## Dependencies

- **`authoritative-plan-02-physics-store-authority`** is the enabler: the
  physics step must be callable against an explicit store bundle instead of
  reaching through `GameModelCollection`/`PhysicsEngine` to *the* stores.
- This same parameterization is the core requirement of
  `physics-standalone-strict-goal-checklist.md` — the two goals are one work
  item. A step function of the form
  `StepPhysicsWorld( PhysicsWorldStores&, const PhysicsStepInputs&, dt )`
  serves live simulation, prediction, and standalone builds identically.

## Phased slices

### Phase 1 — parameterize the step path (the hard part)

- Introduce a `PhysicsWorldStores` bundle (body store, collider store, solver
  persistent-contact state, broadphase grid, RNG/iteration state — everything
  `CaptureReplaySolverSnapshot` currently snapshots is the checklist of what
  belongs in it).
- Live simulation constructs one instance and passes it through; behavior
  byte-identical. Gate: `validate_physics` byte-exact CSV plus
  `validate_perf` (hot-path signature changes).

### Phase 2 — prediction owns a second bundle

- Done 2026-07-07: `BeginReplayPredictionJob` seeds a private
  `RunReplayPredictionState::predictionEngine` under the existing
  `replay_prediction_working_set` reserve owner. The 256 MiB byte cap accounts
  for the copied physics working set and retained prediction data.
- Done 2026-07-07: `StepReplayPredictionJob` steps the private
  `predictionEngine` directly. The live apply/restore choreography and the
  mutation-window hazard comments are deleted.
  Environmental inputs (world forces, tornado state) are captured by value at
  begin time — prediction of a paused moment should not read drifting live
  environment state anyway.
- Still main-thread and budget-sliced at this phase; the important change is
  *which* engine is stepped. Gate passed: `validate_physics`, live-hash
  interaction proof, `validate_perf`, and `validate_full`.

### Phase 3 — move stepping to a worker job

- The slice loop becomes a `WorkerPool` job: step ticks, publish frame
  prefixes through the existing build-frames/committed-frames handoff (keep
  the existing single-writer publish discipline; the overlay already tolerates
  growing prefixes).
- Frame-loop budget logic shrinks to "consume published frames"; the
  `REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS` constant and
  `SetDiagnosticsSuppressed` calls are deleted.
- Hazard to design for: scene mutation during an in-flight job (body add/
  remove, branch restore) → cancel token checked between ticks; job holds no
  raw pointers into live stores (value copies only).

### Phase 4 — guardrails and cleanup

- Done 2026-07-07: `tools/check_runtime_boundaries.py` forbids `RestoreReplaySolverSnapshot`
  and live-store apply calls in prediction files (textual, with self-test).
- Done 2026-07-07: deleted the "Mutation window" glossary entries and updated
  `Agentic/Reference/runtime-reference.md`.
- Unit tests (plan 01 phase 4): snapshot/restore losslessness on the bundle;
  copied-bundle step N ticks == reference sequence.

## Risks

- The step path today may read state outside the snapshot set (statics,
  environment singletons — see plan 02). Phase 1 will surface each as a
  compile/test failure when stepping a second bundle; treat every one as a
  determinism bug already latent in today's snapshot/restore design (if
  restore doesn't cover it, the mutation window was already leaking it).
- Memory: a second body/solver bundle for large scenes. Bounded, accounted
  under the existing replay reserve owner, and paid once per prediction begin
  instead of two captures + two applies per render frame.
- Threading: physics code must be audited for hidden shared scratch (worker
  hashes like `REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH` already exist for
  parallel capture; the same discipline applies to the step job).

## Validation map

| Slice | Validation |
|-------|-----------|
| Phase 1 signature/bundle work | `validate_physics` + `validate_perf` |
| Phase 2 prediction-side swap | `validate_physics` + live-hash interaction proof + `prediction_ragdoll_wall_200_predict` proof |
| Phase 3 worker job | `validate_full` + 3 consecutive `validate_dx12_renderer` runs (frame pacing) + `validate_perf` |
| Phase 4 guardrails/docs | `validate_fast`, then run the changed checker |

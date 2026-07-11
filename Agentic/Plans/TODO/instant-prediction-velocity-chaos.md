# Instant 20s Prediction While Dragging Velocity (N-body chaos playground)

Date: 2026-07-10
Status: Not started — 0%. Feature backlog.
Impact area: Replay prediction scheduling, replay velocity-edit interaction,
scene system (one new scene), worker pool usage, prediction overlay UX.

## Decisions locked with owner (2026-07-10)

1. **Scene type:** dedicated N-body mutual-gravity scene (no terrain/water
   contact churn) is the primary target.
2. **Drag cadence:** continuous latest-wins — rebuild the full 20 s horizon
   whenever a newer velocity arrives; stale in-flight builds are cancelled.
3. **Fidelity:** full engine step. The predicted future must be the same
   deterministic 120 Hz solver the live sim runs, so play-after-drag matches
   the preview exactly.
4. **Cutoff policy:** auto-measured probe. Time a short tick burst at
   prediction-seed time, extrapolate to the full horizon, and enter instant
   mode when the projected rebuild fits a configured wall-clock budget.
   Fall back to the existing amortized unfold above the budget.

## Goal

While the replay velocity gizmo is being dragged on one body, the full
20-second future of every body updates effectively instantaneously
(1–2 render frames behind the mouse), so the user can play with chaotic
divergence in real time. Below the measured cutoff the rebuild is "instant
mode"; above it, current amortized behavior is unchanged.

## Current state (verified against source, 2026-07-10)

The feature is mostly built; the blocker is scheduling policy, not
architecture.

- Velocity gizmo already applies the drag to the live body and marks
  prediction dirty: `Runtime/Replay/RunReplayVelocityEdit.cpp`,
  `ReplayInteractionController.cpp:226` (`MarkPredictionDirty()`).
- Prediction already runs in a private, isolated `Physics::PhysicsEngine`
  seeded from live state (`ReplayRuntime.h:630`,
  `SeedReplayPredictionEngine` at `RunReplayTools.cpp:3510`), stepped with
  `PHYSICS_FIXED_DT` (1/120 s).
- Horizon is already 20 s: `REPLAY_FUTURE_BUFFER_SECONDS = 20.0f`
  (`ReplayRecorder.h:73`) → 2,400 ticks.
- The "butterfly baseline" (`ReplayPredictionBaselineSnapshot`) already
  retains the pre-nudge future for divergence display.

Why it is not instantaneous today:

- Worker throughput cap: `REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT = 8`
  (`RunReplayTools.cpp:423`); one bounded chunk per render pass →
  2,400 ticks ≈ 300 render frames ≈ ~5 s wall time per rebuild.
- Every drag sample restarts the build from tick 1 via dirty → cancel →
  `BeginReplayPredictionJob` (`RunReplayTools.cpp:3810`), re-running
  topology repair, reserve checks, and publication reset. During a
  continuous drag the future never gets past its first fraction.
- The reveal clock intentionally animates the causal unfold instead of
  presenting the completed future at once.
- `REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0` (`RunInternal.h:99`) is
  the main-thread coordination budget only. It is not the limiter and is
  not the right knob; the solve belongs on the worker.

Cost model (to be confirmed by the phase-1 probe): tiny scenes are
dominated by fixed per-step overhead, roughly 2–10 µs per tick, so the full
2,400-tick horizon is ~5–25 ms of worker compute for ≤ 8 free-flying
bodies. Mutual gravity is O(n²) but negligible below hundreds of bodies;
contact/solver iterations are the real scaling cost, which the dedicated
N-body scene avoids.

## Shape

### Phase 1 — Calibration probe and mode selection

- At prediction-engine seed time (after `SeedReplayPredictionEngine`), step
  the private engine ~50 ticks, time it, and extrapolate to
  `targetTickCount`. These 50 ticks are real work: keep the captured frames
  so the probe is not thrown away.
- New config values in `EngineConfig` (+ `engine.cfg`):
  `replayPredictionInstantBudgetMs` (default 30.0) and
  `replayPredictionProbeTicks` (default 50).
- Store the measured ticks-per-ms and the chosen mode
  (`Instant` / `Amortized`) on `RunReplayPredictionBuildState`; surface both
  in the replay tracer stats and a profiler marker so the cutoff is
  observable, not guessed.
- Re-probe whenever the prediction engine is reseeded (scene load, branch,
  body count change) — never mid-drag.

### Phase 2 — Instant-mode full-horizon worker job

- When mode == Instant, submit the entire remaining horizon as one
  `AmortizedTask` budget (`workerTask->SetBudget( targetTickCount )` instead
  of 8) so a single worker submission steps all 2,400 ticks and captures
  frames. Keep the existing release/acquire `buildFrameCount` publication —
  readers are already correct for any prefix size.
- Bypass the reveal clock in instant mode: present the full committed
  future the frame the build completes (reveal cursor jumps to the end).
- Main-thread `REPLAY_PREDICTION_MAX_WORK_MILLISECONDS` stays at 5.0 and
  keeps governing begin-job/future-node/draw work only.
- Allocation policy: no new growth paths. Build frames are already
  pre-sized for the whole horizon under
  `REPLAY_PREDICTION_RESERVE_OWNER` (256 MB cap); instant mode reuses the
  same reservation. Any new scratch must route through
  `RequestReplayPredictionReserveGrowth`.

### Phase 3 — Latest-wins drag coalescing

- Add a small pending-edit slot to the prediction build state: the newest
  velocity-edit-driven dirty request supersedes any older pending one
  (velocity value itself lives on the live body already; the slot only
  coalesces rebuild requests).
- While a build is in flight and a newer request exists: cancel, honoring
  the existing hazard — cancellation must wait for the in-flight worker
  slice before clearing buildFrames/trajectory slots/private engine
  (`ReplayRuntime.h` build-state invariants) — then begin from the newest
  live state. Net effect at ~10–30 ms per build: one rebuild every 1–2
  render frames during a drag.
- Slim the per-restart begin path for same-target refreshes: skip
  `RepairPhysicsBodyAndColliderTopology()` and buffer re-reservation when
  target, body count, and capacities are unchanged since the last begin
  (they cannot change mid-drag; assert that).
- Keep `REPLAY_PREDICTION_REFRESH_SECONDS` auto-refresh behavior unchanged
  for the amortized path.

### Phase 4 — Dedicated N-body chaos scene

- Author `SkullbonezData/scenes/nbody_chaos.scene.json`: three spheres with
  mutual gravity enabled (scene-level support already exists —
  `m_mutualGravityForces`, `WorldEnvironment`, `TestSceneParser` mutual
  gravity fields), world gravity zeroed, fluid disabled, bodies placed well
  clear of terrain so no terrain manifolds generate, sleep disabled for
  the scene (chaotic orbits must never doze).
- Scene defaults: prediction enabled, velocity-edit tool active, 20 s
  horizon, butterfly baseline comparison on, so the scene opens directly
  into the playground.
- Variant seeds (e.g. figure-eight-ish initial conditions plus one wilder
  set) as scene options if cheap; otherwise one authored setup.

### Phase 5 — UX polish

- While a drag is active, freeze the butterfly baseline to the pre-drag
  future (capture once on drag start, not per rebuild) so the ghost shows
  cumulative divergence across the whole gesture; release re-arms it.
- Draw all-body trajectories in this scene, not just the target's ribbon:
  the existing per-body frame samples already carry every body; confirm
  ribbon quota (`REPLAY_PATH_MAX_SEGMENTS`) is sufficient for 3 bodies ×
  2,400 frames after stride decimation, and raise the stride rather than
  the quota if not.
- HUD line showing current mode (Instant/Amortized), measured ticks/ms, and
  last rebuild wall time.

### Phase 6 — Tests and validation

- Unit tests (`SkullbonezTests`): probe extrapolation math and mode
  selection; latest-wins coalescing state machine (pending supersedes,
  cancel waits, no lost final edit — the released velocity must always be
  the one that produced the final committed future).
- Interaction-automation script: load `nbody_chaos`, scripted gizmo drag,
  assert a completed full-horizon prediction exists within N frames of
  drag end and that its final-frame solver hash matches a straight live
  run of the same edit (fidelity check, lane P probe on failure).
- Determinism guard: instant mode must produce byte-identical prediction
  frames to amortized mode for the same seed state — assert equal solver
  hashes for a fixed scenario in Debug.

First useful slice: phases 1–3 with the existing replay scenes (small body
counts already trigger instant mode) — chaos scene and polish can follow.

## Preconditions / risks

- Worker cancellation: the latest-wins loop leans hard on the existing
  cancel-waits-for-slice invariant. In instant mode a slice is the whole
  horizon (~10–30 ms), so cancel latency equals build latency; acceptable
  at these sizes, but the coalescer must not spin-wait on the main thread —
  reuse the existing completion check per frame.
- Frame-capture cost scales with scene body count, not just moving bodies;
  the probe measures step+capture together so this is priced in.
- If the amortized path's `buildPresentationFrameCount` preserve-committed
  logic interacts badly with per-drag restarts, prefer full replacement in
  instant mode (fresh future each rebuild) — it completes fast enough that
  preserve-committed complexity buys nothing.
- Do not touch `PHYSICS_FIXED_DT`, live stepping, or solver code; the live
  physics CSV baselines must remain byte-exact.

## Validation

- Phases 1–3, 5 (runtime/replay code): `tools\validate_full.bat` (files
  under `Runtime/*` map to full), plus `tools\validate_tests.bat` when the
  unit tests land.
- Phase 4 (new `*.scene.json`): `tools\validate_full.bat`.
- Perf-sensitive check after phase 2/3: `tools\validate_perf.bat` to prove
  the live frame loop's hot path is unaffected while a drag-storm rebuild
  runs (prediction work is worker-side; main-thread budget unchanged).
- Physics untouched by design; if any `Physics/*` file is edited after all,
  run `tools\validate_physics.bat`.

# Progress: Prediction Isolated World (plan 03)

Source plan: `fable_plans/03-prediction-isolated-world-plan.md`
Status: not started
Last updated: 2026-07-07

## How to work this file

- Do items in order. Each checkbox is one small, verifiable action.
- Tick a box only with the named evidence in hand; paste the key evidence line
  (command + result) under the box when you tick it.
- If an item fails twice, mark it `[B]` with a one-line reason and stop this
  phase; do not improvise around it.
- Anchors are given as file + exact search string (line numbers drift). Use
  `rg -n "<anchor>" <file>` to locate.
- Comment quality gate applies to every touched source file
  (`Agentic/Reference/comment-style-guide.md`).

## Verified facts (do not re-derive)

- The prediction tick is `StepReplayPredictionPhysicsTick(...)` in
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` (anchor:
  `bool StepReplayPredictionPhysicsTick`). It calls
  `physicsEngine.Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount )`
  plus three GameModelCollection presentation side effects:
  `TickContactHighlights`, `FillPhysicsDiagnosticsNames` (_DEBUG only),
  `NotifyFixedContact`.
- Snapshot chain: `GameModelCollection::CaptureReplaySolverWorldSnapshot`
  (GameModelCollection.cpp, anchor `m_physicsEngine.CaptureReplaySolverSnapshot`)
  → `PhysicsEngine::CaptureReplaySolverSnapshot` (PhysicsEngine.cpp, delegates
  to `m_scene`) → `PhysicsScene::CaptureReplaySolverSnapshot` (delegates to
  `m_world`) → `PhysicsWorld::CaptureReplaySolverSnapshot`.
- `ReplaySolverWorldSnapshot` is defined at
  `SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h` (anchor
  `struct ReplaySolverWorldSnapshot`): sleep state/counters/islands, tornado
  config + timers, persistent contacts + contact cache, solver stats, debug
  contacts, pipeline trace, collision cell keys. Body poses/velocities are NOT
  in it — they live in `RunReplayPredictionBodyBackup` vectors captured by
  `CaptureReplayPredictionBodyState` / applied by
  `ApplyReplayPredictionBodyState` (RunReplayPredictionHelpers.inl).
- The mutation window lives in
  `SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl`,
  `StepReplayPredictionJob` (anchor
  `Hazard: everything after liveRestoreBodies`). Its parts:
  `liveRestoreBodies`/`liveRestoreWorld` fields (ReplayRuntime.h, anchor
  `liveRestoreBodies`), the ApplyJobState / CaptureJobState / RestoreLive
  blocks, `REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS`
  (RunReplayTools.cpp, anchor of same name),
  `ReplayPredictionMutationReserveSpent` (RunReplayPredictionHelpers.inl), and
  `SetDiagnosticsSuppressed` calls (_DEBUG, visualizer).
- `Core/AmortizedTask.h` provides the repo-native sliced worker job shape:
  `SubmitTick( WorkerPool& )`, `IsComplete()`, `IsInFlight()`, `Reset()`,
  `SetBudget( itemsPerTick )`.
- Allocation policy: all prediction storage goes through
  `ReplayPredictionReserveOwner()` / `ReserveReplayPredictionVector(...)`
  (RunReplayPredictionHelpers.inl) under the `replay_prediction_working_set`
  256 MiB byte cap.

## Phase 0 — discovery (read-only; record answers inline here)

- [ ] D1. Record the `PhysicsEngine` constructor + ownership shape.
  Command: `rg -n "PhysicsEngine\(" SkullbonezSource/Physics/PhysicsEngine.h SkullbonezSource/Physics/PhysicsEngine.cpp`
  Record: ctor signature; whether PhysicsEngine is default-constructible;
  which members allocate (m_scene → PhysicsScene members `m_world`,
  `m_bodyStore`, `m_colliderStore`, `m_renderInstanceStore` — see
  PhysicsScene.h anchor `m_world`).
- [ ] D2. Record store sizing/reserve APIs.
  Command: `rg -n "Reserve|reserve\(" SkullbonezSource/Physics/PhysicsBodyStore.h SkullbonezSource/Physics/ColliderStore.h SkullbonezSource/Physics/PhysicsWorld.h`
  Record: how a second engine gets pre-sized to the live body count without
  gameplay-phase allocation (candidate: mirror what
  `GameModelCollection::ReserveForActiveGameModelCapacity` does — anchor in
  GameModelCollection.cpp).
- [ ] D3. Enumerate `PhysicsWorld.h` members NOT covered by
  `ReplaySolverWorldSnapshot`. Command:
  `rg -n "^\s+(std::|int|float|bool|uint|Physics|Math)" SkullbonezSource/Physics/PhysicsWorld.h`
  Cross out every member that appears in the snapshot struct or is per-step
  scratch (rebuilt each Step). Anything left is hidden state: list it here and
  decide copy vs rebuild for each. THIS LIST GATES PHASE 2.
- [ ] D4. Record what `PhysicsEngine::Step` reads outside its parameters.
  Command: `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Physics/PhysicsWorld.cpp SkullbonezSource/Physics/PhysicsScene.cpp SkullbonezSource/Physics/PhysicsEngine.cpp`
  Expected: zero hits on the step path (physics is store-based). Any hit is a
  blocker to log against plan 02.
- [ ] D5. Record how `ApplyReplayPredictionBodyState` /
  `CaptureReplayPredictionBodyState` reach the body store (anchor both in
  RunReplayPredictionHelpers.inl). Record whether they take
  `GameModelCollection&` and internally use `GetPhysicsBodyStore()` — phase 1
  parameterizes exactly this.
- [ ] D6. Confirm collider immutability during prediction: check whether
  `PhysicsEngine::Step` can mutate `ColliderStore` rows (search
  `rg -n "m_colliderStore\." SkullbonezSource/Physics/PhysicsWorld.cpp`).
  If immutable during stepping, the prediction engine can hold a copied
  collider set once per Begin (no per-slice copy).

## Phase 1 — engine-parameterized capture/apply (behavior identical)

- [ ] P1.1 Change `CaptureReplayPredictionBodyState` and
  `ApplyReplayPredictionBodyState` (RunReplayPredictionHelpers.inl) to take
  `Physics::PhysicsBodyStore&` (or `PhysicsEngine&`) instead of reaching
  through `GameModelCollection`. Keep existing call sites working by passing
  `modelCollection.GetPhysicsBodyStore()` / `GetPhysicsEngine()` at each call.
  Evidence: `tools\validate_build.bat Profile` 0 warnings/errors.
- [ ] P1.2 Add an engine-parameterized prediction tick alongside the current
  one, with NO GameModelCollection side effects:
  ```cpp
  // Concept: prediction stepping is pure physics. Contact-highlight and
  // diagnostics-name presentation belongs to the live tick only.
  bool StepPredictionEngineTick( Physics::PhysicsEngine& engine,
                                 float fixedDt,
                                 const EngineConfig& config,
                                 const Physics::PhysicsWorldForces& worldForces,
                                 Threading::WorkerPool& workerPool )
  {
      RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
          RuntimeAllocation::RuntimeAllocationPhase::Replay );
      engine.Step( fixedDt, config, worldForces, workerPool, nullptr, 0 );
      return true;
  }
  ```
  Place next to `StepReplayPredictionPhysicsTick` in RunReplayTools.cpp. Not
  yet called. Evidence: Profile build 0/0. (If /W4 flags it unused, call it
  from the next item in the same commit.)
- [ ] P1.3 PR gate for the slice: `tools\validate_physics.bat` byte-exact
  (nothing behavioral changed) + `tools\validate_perf.bat` if signatures on
  the live tick changed. Commit.

## Phase 2 — prediction-owned engine; delete the mutation window

- [ ] P2.1 Add to `RunReplayPredictionState` (ReplayRuntime.h):
  ```cpp
  // Concept: prediction simulates the future in its own engine. Live stores
  // are never written by prediction; the mutation window is gone.
  // Lifetime: constructed lazily at first prediction Begin, pre-sized to the
  // live body count under the replay reserve owner, reused across builds.
  std::unique_ptr<Physics::PhysicsEngine> predictionEngine;
  ```
  Allocation policy note: construction happens inside
  `RuntimeAllocationScope( Replay )` and is a registered replay-owner cost;
  add the policy comment naming owner `replay_prediction_working_set`, reason,
  deletion condition (none — this is the end-state design), and cap.
- [ ] P2.2 In `BeginReplayPredictionJob` (visualizer): after the existing
  reserve calls, (a) lazily construct + size `predictionEngine` (per D1/D2
  answers), (b) copy live → prediction: apply `predictionBodies` backup INTO
  `predictionEngine`'s body store (P1.1 API), copy collider rows (per D6),
  `RestoreReplaySolverSnapshot` INTO the prediction engine from the captured
  live snapshot. Capture `worldForces` by value into prediction state at Begin
  (add `Physics::PhysicsWorldForces predictionWorldForces;` field) so a paused
  moment's future does not read drifting live environment.
- [ ] P2.3 Rewrite `StepReplayPredictionJob` (visualizer): the tick loop calls
  `StepPredictionEngineTick( *predictionEngine, ... )` and
  `CaptureReplayPredictionFrame` sampling FROM the prediction engine's stores
  (parameterize `CaptureReplayPredictionFrame`'s body source the same way as
  P1.1). DELETE: the liveRestore capture block, ApplyJobState block,
  CaptureJobState block, RestoreLive block, both `SetDiagnosticsSuppressed`
  calls, and the `Hazard: everything after liveRestoreBodies...` comment.
- [ ] P2.4 Delete now-dead members and helpers:
  `liveRestoreBodies`, `liveRestoreWorld` (ReplayRuntime.h +
  `CancelPredictionJob` anchor `m_prediction.liveRestoreBodies.clear()`),
  `REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS` (RunReplayTools.cpp),
  `ReplayPredictionMutationReserveSpent` (helpers — replace remaining callers
  with `ReplayPredictionBudgetExpired`). Grep-verify zero references remain:
  `rg -n "liveRestore|MutationReserve" SkullbonezSource` → no hits.
- [ ] P2.5 Update file-header Mental model/Glossary in the visualizer and
  helpers: delete "Mutation window" entries; describe the prediction engine.
- [ ] P2.6 Live-isolation proof: add an interaction assert that the live
  solver hash is unchanged across a prediction build. Extend
  `RunInteractionAutomation.cpp` with assert name
  `liveSolverHashStableAcrossPrediction` (capture
  `Solver().LatestSample()->solverHash` at predict-click, compare at a later
  frame while paused). Add it to
  `SkullbonezData/interaction/prediction_ragdoll_wall_200_predict.json`.
  Evidence: proof run `ok=1` with the new assert listed.
- [ ] P2.7 PR gate: `tools\validate_physics.bat` byte-exact +
  `prediction_ragdoll_wall_200_predict` proof + `tools\validate_perf.bat`
  (memory: second engine shows in replay reserve accounting, not gameplay).
  Commit.

## Phase 3 — worker-job stepping (optional; only after Phase 2 soaks)

- [ ] P3.1 Wrap the tick loop in `Core/AmortizedTask` (`SubmitTick(pool)`,
  `SetBudget(ticksPerSubmit)`), state owned by `RunReplayPredictionState`.
  The frame loop submits when `building`, consumes published `buildFrameCount`
  prefix growth exactly as today. Single-writer rule: only the job writes
  buildFrames/buildFrameCount; the render thread reads count-then-rows (the
  existing publish discipline).
- [ ] P3.2 Cancellation: `CancelPredictionJob` must wait for or invalidate an
  in-flight task before clearing state (`IsInFlight()` → wait; document why).
- [ ] P3.3 Scene-mutation guard: begin/branch/scene-load paths call
  `CancelPredictionJob` already — verify each (grep call sites) and add a
  `Hazard:` comment that the prediction engine must never hold pointers into
  live stores (values only).
- [ ] P3.4 PR gate: `tools\validate_full.bat` + 3 consecutive
  `tools\validate_dx12_renderer.bat` runs (frame pacing) +
  `tools\validate_perf.bat` + both prediction proofs. Commit.

## Phase 4 — guardrails and closure

- [ ] G1. `tools/check_runtime_boundaries.py`: add a rule that
  `RunReplayPrediction*` files may not call `RestoreReplaySolverSnapshot` /
  `ApplyReplayPredictionBodyState` against the live collection/engine
  (allowlist: the prediction-engine call sites). Include a checker self-test.
  Evidence: checker passes; self-test proves the rule fires on a synthetic
  violation.
- [ ] G2. Update `Agentic/Reference/runtime-reference.md` prediction section
  and `fable_plans/03-prediction-isolated-world-plan.md` status.
- [ ] G3. Mark PHYS-035 done in the plan-02 CSV (or its Done successor).

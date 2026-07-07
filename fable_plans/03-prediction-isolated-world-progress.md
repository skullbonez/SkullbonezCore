# Progress: Prediction Isolated World (plan 03)

Source plan: `fable_plans/03-prediction-isolated-world-plan.md`
Status: in progress
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

- [x] D1. Record the `PhysicsEngine` constructor + ownership shape.
  Command: `rg -n "PhysicsEngine\(" SkullbonezSource/Physics/PhysicsEngine.h SkullbonezSource/Physics/PhysicsEngine.cpp`
  Record: ctor signature; whether PhysicsEngine is default-constructible;
  which members allocate (m_scene → PhysicsScene members `m_world`,
  `m_bodyStore`, `m_colliderStore`, `m_renderInstanceStore` — see
  PhysicsScene.h anchor `m_world`).
  Evidence, 2026-07-07: command returned
  `SkullbonezSource\Physics\PhysicsEngine.h:63:    PhysicsEngine() = default;`.
  `PhysicsEngine` is default-constructible and owns a `PhysicsScene`; the scene
  owns `m_world`, `m_bodyStore`, `m_colliderStore`, and
  `m_renderInstanceStore`.
- [x] D2. Record store sizing/reserve APIs.
  Command: `rg -n "Reserve|reserve\(" SkullbonezSource/Physics/PhysicsBodyStore.h SkullbonezSource/Physics/ColliderStore.h SkullbonezSource/Physics/PhysicsWorld.h`
  Record: how a second engine gets pre-sized to the live body count without
  gameplay-phase allocation (candidate: mirror what
  `GameModelCollection::ReserveForActiveGameModelCapacity` does — anchor in
  GameModelCollection.cpp).
  Evidence, 2026-07-07: the exact header command returned no reserve APIs in
  the body/collider/world headers. Current sizing path is
  `GameModelCollection::ReserveForActiveGameModelCapacity`, which calls
  `m_physicsEngine.ReserveAuthoredBodyCapacity(capacity)` and
  `m_physicsEngine.ReserveRenderPresentationCapacity(capacity)`. Engine reserve
  forwards to `PhysicsScene::ReserveAuthoredBodyCapacity`, which reserves only
  `m_authoredBodyDescs`. Store rows are created through
  `PhysicsBodyStore::CreateBodyRecord` and
  `ColliderStore::CreateColliderRecord`; no direct body/collider store pre-size
  API exists yet.
- [x] D3. Enumerate `PhysicsWorld.h` members NOT covered by
  `ReplaySolverWorldSnapshot`. Command:
  `rg -n "^\s+(std::|int|float|bool|uint|Physics|Math)" SkullbonezSource/Physics/PhysicsWorld.h`
  Cross out every member that appears in the snapshot struct or is per-step
  scratch (rebuilt each Step). Anything left is hidden state: list it here and
  decide copy vs rebuild for each. THIS LIST GATES PHASE 2.
  Evidence, 2026-07-07: durable restored state is covered by
  `ReplaySolverWorldSnapshot`: `m_timeRemaining`, sleep support/inhibit/state/
  counter arrays, underwater lock, tornado capture/eject arrays, collision
  visual contacts, sleep island ids/parents/ranks/flags, persistent contacts,
  persistent contact cache, persistent contact counts, debug contacts, pipeline
  trace, collision cell keys, solver stats, `m_nextSleepIslandVisualId`,
  `m_sleepEnabled`, `m_collisionVisualFrameActive`, tornado field config,
  tornado system config, and tornado elapsed seconds. Restore then clears
  rebuilt scratch: `m_candidatePairs`, `m_solverBodies`,
  `m_terrainContactManifolds`, `m_terrainDetectionCandidates`, object
  narrowphase arrays, and `m_spatialGrid`. Remaining hidden state:
  `m_seedSleepFrameCount` is config-derived policy and should be copied by
  applying runtime config to the prediction engine; point-joint sleep scratch,
  sleep visual scratch, persistent contact side effects, terrain rest applied,
  solver/system objects, diagnostics sink, and `_DEBUG m_diagnosticsSuppressed`
  are rebuilt/owned by the prediction engine rather than copied from live.
- [x] D4. Record what `PhysicsEngine::Step` reads outside its parameters.
  Command: `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Physics/PhysicsWorld.cpp SkullbonezSource/Physics/PhysicsScene.cpp SkullbonezSource/Physics/PhysicsEngine.cpp`
  Expected: zero hits on the step path (physics is store-based). Any hit is a
  blocker to log against plan 02.
  Evidence, 2026-07-07: command returned no hits.
- [x] D5. Record how `ApplyReplayPredictionBodyState` /
  `CaptureReplayPredictionBodyState` reach the body store (anchor both in
  RunReplayPredictionHelpers.inl). Record whether they take
  `GameModelCollection&` and internally use `GetPhysicsBodyStore()` — phase 1
  parameterizes exactly this.
  Evidence, 2026-07-07: `CaptureReplayPredictionBodyState` takes
  `GameModelCollection&`, reads `modelCollection.SceneEntityCount()`, then
  reaches body rows through `modelCollection.GetPhysicsEngine().BodyStore()`.
  It also reads `GameModel::GetFixedContactHighlightSeconds()`.
  `ApplyReplayPredictionBodyState` takes `GameModelCollection&` and restores
  through `TryRestoreReplayPredictionBodyState(...)`, which writes both body
  rows and the presentation timer. Phase 1 can split body-store access while
  leaving the presentation timer path on the collection.
- [x] D6. Confirm collider immutability during prediction: check whether
  `PhysicsEngine::Step` can mutate `ColliderStore` rows (search
  `rg -n "m_colliderStore\." SkullbonezSource/Physics/PhysicsWorld.cpp`).
  If immutable during stepping, the prediction engine can hold a copied
  collider set once per Begin (no per-slice copy).

  Evidence, 2026-07-07: `rg -n "m_colliderStore\." ...PhysicsWorld.cpp`
  returned no hits. Follow-up search for `ColliderStore&` in PhysicsWorld.cpp
  shows step helpers take `const ColliderStore&` and read
  `colliderStore.Records()`, so stepping treats collider rows as immutable.

## Phase 1 — engine-parameterized capture/apply (behavior identical)

- [x] P1.1 Change `CaptureReplayPredictionBodyState` and
  `ApplyReplayPredictionBodyState` (RunReplayPredictionHelpers.inl) to take
  `Physics::PhysicsBodyStore&` (or `PhysicsEngine&`) instead of reaching
  through `GameModelCollection`. Keep existing call sites working by passing
  `modelCollection.GetPhysicsBodyStore()` / `GetPhysicsEngine()` at each call.
  Evidence: `tools\validate_build.bat Profile` 0 warnings/errors.

  Evidence, 2026-07-07: `tools\validate_build.bat Profile` passed with
  `Build succeeded. 0 Warning(s), 0 Error(s).`
  `FABLE03_P1_BUILD_PROFILE_EXIT=0`,
  `FABLE03_P1_BUILD_PROFILE_ELAPSED_SECONDS=8.955`, log
  `Agentic\Reports\2026-07-07\logs\fable-03-p1-profile-build.log`.
- [x] P1.2 Add an engine-parameterized prediction tick alongside the current
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

  Evidence, 2026-07-07: added `StepPredictionEngineTick(...)` next to
  `StepReplayPredictionPhysicsTick(...)` and marked it `[[maybe_unused]]`
  because phase 2 owns the call switch. `tools\validate_build.bat Profile`
  passed with `Build succeeded. 0 Warning(s), 0 Error(s).`
- [x] P1.3 PR gate for the slice: `tools\validate_physics.bat` byte-exact
  (nothing behavioral changed) + `tools\validate_perf.bat` if signatures on
  the live tick changed. Commit.

  Evidence, 2026-07-07: `tools\validate_physics.bat` passed with
  `VALIDATE_PHYSICS: ALL PASSED`,
  `physics_regression_solver.csv (20001 lines, byte-exact match)`,
  `FABLE03_P1_VALIDATE_PHYSICS_EXIT=0`, elapsed 18.514s. No
  `validate_perf` required because `StepReplayPredictionPhysicsTick`'s live
  signature did not change. Runtime/Replay file-map gate also ran:
  `tools\validate_full.bat` passed with `DX12 validation errors: 0`, DX12
  screenshots matching committed baselines, the same physics byte-exact match,
  `FABLE03_P1_VALIDATE_FULL_EXIT=0`, elapsed 40.456s.

## Phase 2 — prediction-owned engine; delete the mutation window

- [x] P2.1 Add to `RunReplayPredictionState` (ReplayRuntime.h):
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

  Evidence, 2026-07-07: `RunReplayPredictionState` now owns
  `std::unique_ptr<Physics::PhysicsEngine> predictionEngine`,
  `PhysicsWorldForces predictionWorldForces`, and
  `predictionEngineReady`. The source comment names owner
  `replay_prediction_working_set`, reason, deletion condition, and 256 MB
  checker budget. The lazy construction spelling is allowlisted in
  `tools\allocation_policy_allowlist.json` because it occurs under the replay
  owner/growth scope and avoids startup/perf-smoke PhysicsWorld reserve cost.
- [x] P2.2 In `BeginReplayPredictionJob` (visualizer): after the existing
  reserve calls, (a) lazily construct + size `predictionEngine` (per D1/D2
  answers), (b) copy live → prediction: apply `predictionBodies` backup INTO
  `predictionEngine`'s body store (P1.1 API), copy collider rows (per D6),
  `RestoreReplaySolverSnapshot` INTO the prediction engine from the captured
  live snapshot. Capture `worldForces` by value into prediction state at Begin
  (add `Physics::PhysicsWorldForces predictionWorldForces;` field) so a paused
  moment's future does not read drifting live environment.

  Evidence, 2026-07-07: `SeedReplayPredictionEngine(...)` requests
  `RunReplayPredictionState::predictionEngine` bytes under
  `ReplayPredictionReserveOwner()`, creates the private engine inside
  `RuntimeAllocationScope(Replay)`, copies the live engine into it, applies the
  captured backup body state, restores the captured solver snapshot into the
  private engine, and captures world forces by value.
- [x] P2.3 Rewrite `StepReplayPredictionJob` (visualizer): the tick loop calls
  `StepPredictionEngineTick( *predictionEngine, ... )` and
  `CaptureReplayPredictionFrame` sampling FROM the prediction engine's stores
  (parameterize `CaptureReplayPredictionFrame`'s body source the same way as
  P1.1). DELETE: the liveRestore capture block, ApplyJobState block,
  CaptureJobState block, RestoreLive block, both `SetDiagnosticsSuppressed`
  calls, and the `Hazard: everything after liveRestoreBodies...` comment.

  Evidence, 2026-07-07: `StepReplayPredictionJob(...)` now steps
  `*predictionEngine`, captures frames from the private engine, and snapshots
  private job state only. The live restore/apply/suppression window is gone.
- [x] P2.4 Delete now-dead members and helpers:
  `liveRestoreBodies`, `liveRestoreWorld` (ReplayRuntime.h +
  `CancelPredictionJob` anchor `m_prediction.liveRestoreBodies.clear()`),
  `REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS` (RunReplayTools.cpp),
  `ReplayPredictionMutationReserveSpent` (helpers — replace remaining callers
  with `ReplayPredictionBudgetExpired`). Grep-verify zero references remain:
  `rg -n "liveRestore|MutationReserve" SkullbonezSource` → no hits.

  Evidence, 2026-07-07: deleted live restore members/helpers and the mutation
  reserve helper; final grep checks:
  `rg -n "liveRestore|MutationReserve" SkullbonezSource` had no hits and
  `rg -n "mutation window|temporarily fast-forward|temporarily swaps|RestoreLive|StepReplayPredictionPhysicsTick" SkullbonezSource\Runtime\Replay`
  had no hits.
- [x] P2.5 Update file-header Mental model/Glossary in the visualizer and
  helpers: delete "Mutation window" entries; describe the prediction engine.

  Evidence, 2026-07-07: replay prediction helper/visualizer/tool headers now
  describe private-engine prediction and no longer describe a mutation window.
- [x] P2.6 Live-isolation proof: add an interaction assert that the live
  solver hash is unchanged across a prediction build. Extend
  `RunInteractionAutomation.cpp` with assert name
  `liveSolverHashStableAcrossPrediction` (capture
  `Solver().LatestSample()->solverHash` at predict-click, compare at a later
  frame while paused). Add it to
  `SkullbonezData/interaction/prediction_ragdoll_wall_200_predict.json`.
  Evidence: proof run `ok=1` with the new assert listed.

  Evidence, 2026-07-07: interaction assertion
  `liveSolverHashStableAcrossPrediction` was added and passed at frame 165 in
  `TestOutput\interaction\prediction_ragdoll_wall_200_predict_report.json`.
  `finalState.predictionSourceSolverHash` and `finalState.liveSolverHash`
  both equal `10105770546383963666`.
- [x] P2.7 PR gate: `tools\validate_physics.bat` byte-exact +
  `prediction_ragdoll_wall_200_predict` proof + `tools\validate_perf.bat`
  (memory: second engine shows in replay reserve accounting, not gameplay).
  Commit.

  Evidence, 2026-07-07:
  - `tools\validate_physics.bat` passed, byte-exact
    `physics_regression_solver.csv (20001 lines)`,
    `FABLE03_P2_VALIDATE_PHYSICS_FINAL_EXIT=0`, elapsed 14.492s.
  - `prediction_ragdoll_wall_200_predict` proof passed with report `ok=1`,
    no allocation-guard gameplay violations, private engine reserve
    `RunReplayPredictionState::predictionEngine` granted at 161,955,200 bytes
    under `replay_prediction_working_set`,
    `FABLE03_P2_RAGDOLL_PROOF_FINAL_EXIT=0`, elapsed 3.576s.
  - `tools\validate_perf.bat` passed after an explicit current-machine perf
    baseline refresh with `tools\update_baselines.bat --perf --require`.
    The refreshed baselines are `dx12_perf` commit `bfa676c7`, Frame avg
    0.9743 ms, memory 90.62/157.61/157.61 MB, and `physics_bench_perf`
    commit `bfa676c7`, Frame avg 0.5934 ms, memory
    90.75/154.47/154.47 MB. Final gate:
    `FABLE03_P2_VALIDATE_PERF_AFTER_BASELINE_EXIT=0`, elapsed 34.162s.
  - `tools\validate_format.bat` passed,
    `FABLE03_P2_VALIDATE_FORMAT_EXIT=0`, elapsed 8.277s.
  - `tools\validate_full.bat` passed, DX12 validation errors 0, screenshots
    matched committed baselines, physics byte-exact,
    `FABLE03_P2_VALIDATE_FULL_FINAL_EXIT=0`, elapsed 47.856s.
  - Post rubber-duck follow-up fixed the private-engine growth request to reuse
    retained engine capacity instead of re-reporting `old_capacity=0` on every
    prediction rebuild. Rerun gates passed:
    `FABLE03_P2_POST_DUCK_VALIDATE_FORMAT_EXIT=0` (8.345s),
    `FABLE03_P2_POST_DUCK_BUILD_PROFILE_EXIT=0` (6.448s),
    `FABLE03_P2_POST_DUCK_RAGDOLL_PROOF_EXIT=0` (4.537s),
    `FABLE03_P2_POST_DUCK_VALIDATE_PERF_EXIT=0` (30.648s),
    `FABLE03_P2_POST_DUCK_VALIDATE_PHYSICS_EXIT=0` (13.519s), and
    `FABLE03_P2_POST_DUCK_VALIDATE_FULL_EXIT=0` (39.288s).

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

# Progress: Instant 20s Prediction While Dragging Velocity

Companion checklist for `Agentic/Plans/TODO/instant-prediction-velocity-chaos.md`.
Work through the checkboxes **in order**. Do not skip ahead: later phases
assume earlier ones landed. Tick a box only when the item is implemented,
compiles at `/W4` with zero warnings, and its acceptance check passes.

Owner decisions already locked (do not re-litigate): dedicated N-body scene,
continuous latest-wins drag cadence, full-engine deterministic fidelity,
auto-measured instant-vs-amortized cutoff.

## Hard rules for the implementing agent

- Follow the Agent Startup Contract in `AGENTS.md` before editing.
- **Never edit** `SkullbonezSource/Physics/*`, `PHYSICS_FIXED_DT`, or live
  solver stepping. This feature is scheduling + presentation only. If you
  believe you need a physics change, stop and report instead.
- No new runtime heap growth. All prediction memory routes through the
  existing `REPLAY_PREDICTION_RESERVE_OWNER` reserve
  (`Runtime/Replay/ReplayPredictionReserve.h`, 256 MB cap). New scratch must
  use `RequestReplayPredictionReserveGrowth`; new members should reuse
  already-reserved buffers where possible.
- Prediction must never write live stores. The private engine
  (`ReplayRuntime.h` — `predictionEngine`) is the only thing you step.
- Apply `Agentic/Reference/comment-style-guide.md` to every touched source
  file (learning header + local Concept/Why/Invariant/Hazard where needed).
- Validation scripts are PR gates, not iteration tools. Run them at the
  commit points listed below, on a Windows machine (they launch the exe).
- Commit at the end of each phase with a descriptive body; push to the
  feature branch. Update this file's checkboxes in the same commit.

## Key anchors (verify before starting — line numbers drift)

| What | Where |
|------|-------|
| Fixed step 1/120, horizon 20 s | `Physics/PhysicsTimestep.h`, `Runtime/Replay/ReplayRecorder.h` (`REPLAY_FUTURE_BUFFER_SECONDS = 20.0f`) |
| 8-ticks-per-frame throttle | `Runtime/Replay/RunReplayTools.cpp` (`REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT = 8`, ~line 423) |
| Main-thread 5 ms budget | `Runtime/RunInternal.h` (`REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0`) |
| Begin job (restart-from-scratch path) | `RunReplayTools.cpp` `BeginReplayPredictionJob` (~3810) |
| Per-frame worker submit | `RunReplayTools.cpp` `StepReplayPredictionJob` (~4027): `workerTask->SetBudget(8); SubmitTick(...)` |
| Worker task creation + work lambda | `RunReplayTools.cpp` ~4006 (`AmortizedTask`, `RunReplayPredictionWorkerRange`) |
| Dirty/refresh dispatcher | `RunReplayTools.cpp` `RenderReplayPredictionVisualizer` (~4300–4430) |
| Reveal clock (causal unfold pacing) | `RunReplayTools.cpp` `ReplayPredictionRevealFrameIndex` (~377), state in `ReplayRuntime.h` `RunReplayPredictionRevealClock` (~516) |
| Velocity edit → dirty | `Runtime/Replay/RunReplayVelocityEdit.cpp`; `ReplayInteractionController.cpp` `ApplyVelocityEditToBody` → `MarkPredictionDirty()` |
| Build/simulation state structs | `Runtime/Replay/ReplayRuntime.h` (`RunReplayPredictionBuildState`, `RunReplayPredictionSimulationState`, ~560–650) |
| Butterfly baseline capture | `RunReplayTools.cpp` `CaptureReplayPredictionBaselineSnapshot` call inside the dispatcher |
| Config parse macros | `Core/Config.cpp` (`CONFIG_FLOAT`/`CONFIG_INT`/`CONFIG_BOOL` table, ~line 279+) |
| Existing 3-body scene | `SkullbonezData/scenes/three_body_chaos.scene.json` (mutual gravity block under `simulation.world.mutualGravity`) |
| Interaction automation examples | `SkullbonezData/interaction/prediction_determinism_probe.json`, `replay_prediction_simple_verify.json` |

- [x] Anchors table verified against source (2026-07-10, planning session).

---

## Phase 0 — Setup

- [ ] Complete the Agent Startup Contract (`AGENTS.md`, `README.md`,
      `Agentic/README.md`, `Agentic/SessionState.md`, `git status`).
- [ ] Read the plan file `Agentic/Plans/TODO/instant-prediction-velocity-chaos.md`
      end to end.
- [ ] Create the feature branch from latest `main`
      (suggested: `feature/instant-prediction-velocity-chaos`).
- [ ] Re-verify each row of the anchors table above with `rg` (names, not
      line numbers, are the contract). If any anchor is missing or renamed,
      update this table before proceeding.

## Phase 1 — Calibration probe and mode selection

Design: the **worker measures, the frame thread decides**. No new threads,
no main-thread stepping. The first 8-tick submit doubles as the probe.

- [ ] `Core/Config.h`: add to `EngineConfig`:
      `float replayPredictionInstantBudgetMs = 30.0f;` and
      `int replayPredictionProbeTicks = 50;` (grouped near the replay/
      prediction-adjacent settings, with a short Why comment).
- [ ] `Core/Config.cpp`: wire both into the parse table using the existing
      macros: `CONFIG_FLOAT( "replay_prediction_instant_budget_ms", replayPredictionInstantBudgetMs, 0.0f, 10000.0f )`
      and `CONFIG_INT( "replay_prediction_probe_ticks", replayPredictionProbeTicks, 8, 2400 )`.
      Note: budget 0 disables instant mode (always amortized) — document
      that in the comment.
- [ ] `SkullbonezData/engine.cfg`: add both keys with defaults and a comment.
- [ ] `Runtime/Replay/ReplayRuntime.h`: add
      `enum class ReplayPredictionBuildMode : uint8_t { Undecided, Amortized, Instant };`
      near the prediction structs, and add to `RunReplayPredictionBuildState`:
      `ReplayPredictionBuildMode buildMode = ReplayPredictionBuildMode::Undecided;`,
      `std::atomic<double> measuredTicksPerMs{ 0.0 };` (worker-published,
      release/acquire like `buildFrameCount`), and
      `double lastBuildWallMs = 0.0;`. Follow the existing publication
      comment style on `buildFrameCount`.
- [ ] `RunReplayTools.cpp` — locate `RunReplayPredictionWorkerRange` (the
      work lambda body). Time the tick loop with
      `std::chrono::steady_clock` and, when at least
      `config.replayPredictionProbeTicks` ticks have accumulated since job
      start and `measuredTicksPerMs` is still 0, publish
      ticks-done / elapsed-ms with a release store. Accumulate across
      ranges (two new plain fields written only by the worker are fine —
      the worker is the sole writer, mirroring `buildFrames` ownership).
- [ ] `RunReplayTools.cpp` `StepReplayPredictionJob`: before the
      `SetBudget(REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT)` line, add mode
      decision: if `buildMode == Undecided` and `measuredTicksPerMs > 0`
      (acquire load), compute
      `projectedMs = remainingTicks / measuredTicksPerMs` and set
      `buildMode = ( budget > 0 && projectedMs <= budget ) ? Instant : Amortized`.
      Implement the decision itself as a **pure free function** in a header
      the tests can reach (suggested:
      `Runtime/Replay/ReplayPredictionScheduling.h`, new, header-only):
      `ReplayPredictionBuildMode ChooseReplayPredictionBuildMode( double measuredTicksPerMs, int remainingTicks, double instantBudgetMs );`
- [ ] Reset `buildMode`, probe accumulators, and `measuredTicksPerMs` in
      `BeginReplayPredictionJob` (every begin re-probes is WRONG — see next
      item) and wherever the prediction engine is reseeded/cancelled
      (`CancelPredictionJob`, engine reseed in `SeedReplayPredictionEngine`).
      Rule: **re-probe on reseed/scene/branch/body-count change; keep the
      measurement across same-target drag restarts** (phase 3 depends on
      this). Simplest correct form: store measurement on
      `RunReplayPredictionSimulationState` (lives with the engine), reset it
      only where `predictionEngineReady` is reset.
- [ ] Add `PROFILE_SCOPED` or extend the existing replay tracer stats
      (`ClearReplayTrajectoryStats` / `RecordReplayTrajectoryFrameStats`
      path in `Run::RenderReplayPathVisualizer`) with buildMode,
      measuredTicksPerMs, and lastBuildWallMs so the cutoff is observable.
- [ ] **Acceptance:** Debug build; load any replay scene, enable prediction;
      log/tracer shows a nonzero measuredTicksPerMs and a decided mode
      within ~2 render frames of a rebuild. Amortized behavior unchanged
      when `replay_prediction_instant_budget_ms = 0`.
- [ ] Commit phase 1 (message: what/why + config keys added). No validation
      gate yet (mid-plan), but keep the build warning-clean.

## Phase 2 — Instant-mode full-horizon build + reveal bypass

- [ ] `StepReplayPredictionJob`: when `buildMode == Instant`, call
      `workerTask->SetBudget( prediction.build.targetTickCount )` (whole
      remaining horizon in one submit) instead of the 8-tick constant.
      Amortized path keeps 8. Keep exactly one `SubmitTick` per render pass.
- [ ] `BeginReplayPredictionJob`: record a job-start
      `steady_clock::time_point` on the build state; in
      `CompleteReplayPredictionJobOnFrameThread` (or wherever
      `build.complete` flips true on the frame thread), set
      `lastBuildWallMs`.
- [ ] Reveal-clock bypass: in `ReplayPredictionRevealFrameIndex`
      (~line 377), early-return `lastAvailableFrame` when
      `buildMode == Instant`. This holds the monotonic-cursor invariant
      (cursor jumps to end once and stays). Add a Why comment: instant mode
      presents the completed future at once; the unfold animation is an
      amortized-mode affordance.
- [ ] Confirm `REPLAY_PREDICTION_MAX_WORK_MILLISECONDS` (5.0) is untouched
      and still governs only begin/future-node/draw work on the main thread.
- [ ] **Acceptance:** in a small scene (≤ 8 bodies) with prediction on, a
      dirty event (toggle predict off/on or nudge velocity once) produces a
      fully drawn 20 s future within a few render frames, not over ~5
      seconds. `lastBuildWallMs` reported in tracer. In a big scene
      (e.g. `box_pile_throw_300`), mode resolves Amortized and the unfold
      behaves exactly as before.
- [ ] Commit phase 2.

## Phase 3 — Latest-wins drag coalescing

Problem being solved: today every dirty event cancels and restarts the
build; during a continuous gizmo drag that thrashes `BeginReplayPredictionJob`
every frame and (in instant mode) a cancel would block on a whole-horizon
worker slice. Never cancel a running instant build — supersede it.

- [ ] `ReplayRuntime.h` `RunReplayPredictionBuildState`: add
      `bool pendingLatestRestart = false;` with a Concept comment (newest
      velocity-edit rebuild request; supersedes, never queues).
- [ ] Dispatcher (`RenderReplayPredictionVisualizer`, dirty-handling block
      ~4340): change the logic to:
      - if `build.dirty && build.building && buildMode == Instant`:
        set `pendingLatestRestart = true`, clear `dirty`, do **not** cancel,
        do not begin.
      - if `!build.building && ( build.dirty || pendingLatestRestart )`:
        clear `pendingLatestRestart`, run the existing begin path
        (which snapshots the **current** live body state — that is what
        makes the last-arrived velocity win).
      - Amortized mode keeps today's cancel+begin behavior unchanged.
- [ ] Completion hook: where `predictionCompletedThisPass` is computed
      (~4395), if `pendingLatestRestart`, leave it set so the next
      dispatcher pass begins immediately (budget permitting). Do not begin
      inside the completion branch itself — one begin per pass, at the top.
- [ ] Hazard check: re-read the cancellation invariant comment on
      `RunReplayPredictionBuildState::workerTask` ("cancellation must wait
      for an in-flight slice"). Verify no new path cancels while an
      instant-mode slice runs. Scene mutation / topology-change paths keep
      their existing cancel semantics (they may still block; that is
      pre-existing behavior).
- [ ] Drag-end correctness: confirm (by reading
      `RunReplayVelocityEdit.cpp`) that releasing the gizmo applies the
      final velocity to the live body **before** the last
      `MarkPredictionDirty()`. The final committed future must be built
      from the release-time velocity. Add a focused comment there naming
      this ordering as an invariant.
- [ ] Butterfly baseline over the whole gesture: on drag **start** in
      `RunReplayVelocityEdit.cpp` (pointer-capture begin), if a committed
      future exists, re-arm the baseline: set
      `baseline.comparisonActive = true; baseline.valid = false;` so the
      dispatcher's existing capture call snapshots the **pre-drag** future
      once. Verify subsequent per-drag rebuilds do NOT recapture (they
      shouldn't: capture requires `!valid`). Ghost then shows cumulative
      divergence across the gesture.
- [ ] Optional (only if profiling shows begin-path cost matters): skip
      `RepairPhysicsBodyAndColliderTopology()` and re-reservation in
      `BeginReplayPredictionJob` for instant-mode same-target restarts where
      body count and capacities are unchanged; assert unchanged in Debug.
      Skip this item if drag latency already feels instant.
- [ ] Extract the coalescer decision (given: dirty, building, mode,
      pendingRestart → action: Begin / Supersede / Nothing) as a pure free
      function in `ReplayPredictionScheduling.h` next to the mode chooser,
      and have the dispatcher call it. This is what phase 6 unit-tests.
- [ ] **Acceptance:** manual — in a small scene, hold a velocity-gizmo drag
      and sweep the mouse for several seconds: trajectories continuously
      reshape ~1–2 frames behind the mouse, no hitching of the live frame
      loop, and on release the final future corresponds to the final
      velocity (nudge once more and confirm it updates). Baseline ghost
      shows the pre-drag future throughout the gesture.
- [ ] Commit phase 3.

## Phase 4 — Interactive N-body playground scene

`three_body_chaos.scene.json` already exists but is authored as a
fixed-length capture/regression scene (`playback.frames: 360`,
`exitOnComplete: true`, screenshot capture). Derive, don't edit it.

- [ ] Create `SkullbonezData/scenes/nbody_chaos_playground.scene.json` by
      copying `three_body_chaos.scene.json`, then: remove the `capture`
      block; set `playback.exitOnComplete: false`; raise or remove the
      frame limit so the scene runs indefinitely for interactive use
      (check how other interactive/harness scenes author `playback` —
      e.g. `interaction_inspect_gizmo_harness.scene.json`); keep
      `simulation.world.mutualGravity` (enabled, gravitationalConstant,
      softeningLength, elasticCollisions) and the zeroed world gravity /
      sunk terrain / hidden water exactly as-is.
- [ ] Confirm the three bodies never sleep: with zero world gravity and no
      support contacts the sleep pass should never qualify them. Verify by
      running the scene ~30 s and checking no body sleeps (collision
      visualizer / sleep overlay). If they do sleep, author the scene's
      bodies `"sleeping": false` and report — do not touch sleep code.
- [ ] Camera: keep the authored main camera; verify the three bodies stay
      in view for at least ~20 s of default evolution. Adjust the authored
      camera position in the new scene file if not.
- [ ] **Acceptance:** launch the scene, enable prediction + velocity edit,
      drag a body's velocity: full 20 s futures for all three bodies
      update live (phase 5 confirms all-body ribbons; at minimum the
      target's does here). Scene never auto-exits.
- [ ] Commit phase 4. Scene files map to `validate_full` at the PR gate —
      note it for the final phase rather than running it now.

## Phase 5 — HUD + all-body trajectories

- [ ] All-body ribbons: load the playground scene, target one body, drag.
      If the two non-target bodies do not get drawn trajectories
      (prediction frames already sample every body; drawing may be limited
      to the target root + divergence-inferred children), extend the path
      visualizer to add every live body as a root target when
      body count ≤ a small constant (suggest 8; reuse
      `REPLAY_PATH_MAX_ROOT_TARGETS` capacity — verify it is ≥ body count).
      Anchor: the `targets` population block in
      `RenderReplayPathVisualizer` (`RunReplayTools.cpp` ~4505).
- [ ] Ribbon capacity: confirm `REPLAY_PATH_MAX_SEGMENTS` (260) and the
      per-frame stride decimation give smooth 20 s curves for 3 bodies. If
      segments run out, increase the stride (coarser sampling), not the
      quota.
- [ ] HUD line: add mode / measuredTicksPerMs / lastBuildWallMs to the
      replay overlay or debug HUD. Find the existing prediction status text
      (rg for the overlay text near the scrubber/prediction controls in
      `Runtime/Replay/ReplayOverlayRenderer.*` or `RunUiTextPass.cpp`) and
      append one line in the same style. Presentation-only; no new state.
- [ ] **Acceptance:** in the playground scene, dragging shows three live
      ribbons diverging against the frozen pre-drag ghost, plus a HUD line
      like `Prediction: Instant · 96 ticks/ms · 25 ms rebuild`.
- [ ] Commit phase 5.

## Phase 6 — Tests, validation, handoff

- [ ] Unit tests: new `SkullbonezTests/TestReplayPredictionScheduling.cpp`
      (doctest, mirror an existing Test*.cpp) covering:
      `ChooseReplayPredictionBuildMode` (budget 0 → Amortized; projected
      under/over budget; zero/negative measurement → Undecided or
      Amortized, whatever you implemented — pin it), and the coalescer
      transition function (dirty-while-building-instant → Supersede;
      idle+pending → Begin; amortized keeps cancel path; no lost final
      edit: Supersede followed by completion yields exactly one Begin).
- [ ] Add the new test file to `SKULLBONEZ_TESTS.vcxproj` and
      `SKULLBONEZ_TESTS.vcxproj.filters`.
- [ ] Determinism guard (Debug, CLI): extend the existing prediction
      determinism probe pattern
      (`SkullbonezData/interaction/prediction_determinism_probe.json` and
      its handler in `RunInteractionAutomation.cpp` /
      `RunReplayProbes.cpp`) with a check that builds the same seed-state
      future once with instant mode forced and once with amortized mode
      forced, and asserts equal final-frame solver hashes
      (`CaptureCurrentReplaySolverHash` shows the hashing pattern). Lane P:
      failure goes through the probe/report channel, no throwing.
- [ ] Interaction-automation script:
      `SkullbonezData/interaction/nbody_velocity_drag_instant.json`
      mirroring `replay_prediction_simple_verify.json`: load the playground
      scene, select a body, scripted velocity drag, then assert a completed
      full-horizon prediction exists within a bounded number of frames and
      the interaction report is `ok=true`.
- [ ] Run PR gates on Windows and paste output into the commit/PR body:
      `tools\validate_tests.bat` (new unit tests),
      `tools\validate_full.bat` (Runtime/* + scene file changes),
      `tools\validate_perf.bat` (prove the live frame loop hot path is
      unaffected during a drag-storm rebuild).
- [ ] Confirm zero `/W4` warnings and zero DX12 validation errors in the
      gate output.
- [ ] Comment-quality pass: inspect every touched source file against
      `Agentic/Skills/comment-style-audit/skill.md` before reporting done.
- [ ] Update `Agentic/SessionState.md` handoff notes; move the plan file
      out of TODO per repo convention (or mark its Status line complete)
      and set this file's remaining boxes.
- [ ] Final commit + push; report elapsed wall-clock time per the AGENTS.md
      timing rule.

---

## Deferred / explicitly out of scope

- Editing anything under `SkullbonezSource/Physics/`.
- Multi-seed scene variants (figure-eight etc.) — `three_body_figure_eight.scene.json`
  already exists for authored orbits; playground variants can come later.
- Raising the instant-mode ceiling for contact-heavy scenes (begin-path
  slimming beyond the optional phase-3 item).
- Any change to replay artifact formats, recorder, or restore paths.

# 09 — Replay Subsystem Right-Sizing

Date: 2026-07-08
Status: In Progress
Priority: P2
Owner: Runtime / Replay
Source issue: audit iss-07 (severity 3)

## Owner decision

The shadow prediction engine, future-node visualization, replay ribbons, and
cinematic reveal timing are required functionality for the Butterfly Effect
demo. This plan may refactor, split, rename, and clarify ownership around those
features, but it must not remove them, hide them behind a disabled-by-default
flag, or treat them as speculative debug leftovers.

## Problem

Replay grew into a ~17K-line feature area that serves both diagnostics and the
Butterfly Effect demo: record/replay, what-if physics prediction, future-state
visualization, replay ribbons, and wall-clock cinematic reveal timing. The
problem is not that these capabilities exist; the problem is that their record,
prediction, UI, presentation, and game-loop responsibilities are poorly
separated.

Verified evidence:

- `RunReplayPredictionState` owns
  `unique_ptr<Physics::PhysicsEngine> predictionEngine`
  ([ReplayRuntime.h](../SkullbonezSource/Runtime/Replay/ReplayRuntime.h)) with a
  256 MB reserve cap and a `steady_clock` "reveal" cursor, blending ~40-50
  fields across UI hover bools, prediction engine/world, async build cursors, a
  future-node cache, retained markers, and an animation clock — one struct, six
  concerns.
- Copy-paste twins differ only by sample type:
  `FindReplayBodyById` vs `FindReplayPredictionBodyById`, plus
  `ByModelIndex`/`FutureDepth`/`AddFutureNode`/`BuildFutureNodes` pairs — a
  single template collapses all six and is the largest driver of the 2,577-line
  `RunReplayPredictionHelpers.inl`.
- Cohesion leaks upward: ~1,800 of `RunFrame.cpp`'s 3,288 lines are replay
  checkpoint/verify probes and `RestoreReplayV2ArtifactTargetState` — code that
  belongs in `Replay/`, not the game loop.

## Goal

Re-cohere replay around three separated concerns — record/replay, what-if
prediction, and presentation — and get replay code out of `RunFrame`.

## Approach

- [x] **Phase 0 — Split `RunReplayPredictionState`** by concern: prediction sim /
  UI hover / async build / render cache / reveal clock. Each becomes its own
  small type.
- [ ] **Phase 1 — Template the twin helpers.** One template over sample type
  collapses the six `Find*`/`*ByModelIndex`/`FutureDepth`/`AddFutureNode`/
  `BuildFutureNodes` pairs.
- [ ] **Phase 2 — Evict replay from the game loop.** Move the ~1,800 lines of
  replay probes and `RestoreReplayV2ArtifactTargetState` out of `RunFrame` into
  `Replay/`. (Coordinates with plan 01's `RunFrame` shrink and plan 06's `.inl`
  work.)
- [ ] **Phase 3 — Make Butterfly Effect ownership explicit.** Preserve the 256 MB
  shadow prediction engine, future-node visualization, replay ribbons, and
  cinematic reveal behavior, but give them clear owner types and contracts so
  they are no longer blended into generic replay state or `RunFrame`.

## Risks / determinism

Replay is the one subsystem allowed to grow at runtime (via
`RuntimeReserveAllocator`); keep that contract. Record→restore must stay
bit-exact — guard with the replay scrub regression.

## Step-by-step implementation

Do steps in order; validate and commit per step. Record→restore must stay
bit-exact — the replay scrub regression is the gate.

### Phase 0 — Split `RunReplayPredictionState`

- [x] **0.1** Split `RunReplayPredictionState` (`ReplayRuntime.h`) by concern
  into separate small types: prediction sim (owned engine/world), UI hover flags,
  async build cursors, future-node render cache, reveal clock. Do **one concern
  at a time**. Gate: replay scrub regression + `validate_full`. Commit per split.
  - Progress note (2026-07-08, reveal clock): split the wall-clock causal
    reveal pacing fields into `RunReplayPredictionRevealClock`. Demo Director,
    replay prediction helpers, visualization job start, and interaction
    automation reporting now reach reveal timing through
    `RunReplayPredictionState::revealClock`; prediction simulation, UI hover,
    async build cursor, and future-node cache concerns remain in this step.
    Comment audit inspected the five touched source files with no deferred
    wording work. Validation: `tools\validate_format.bat` passed in 9.6s;
    `tools\validate_replay_scrub.bat` passed in 28.1s; `tools\validate_full.bat`
    passed in 44.9s with 0 build warnings/errors, 0 DX12 validation errors,
    matching DX12 screenshots, and `physics_regression_solver.csv` byte-exact at
    20001 lines. An initial `validate_full` attempt failed only on the
    `ReplayRuntime.h` formatting precheck and passed after the touched header was
    formatted narrowly.
  - Progress note (2026-07-08, UI hover state): split replay prediction overlay
    hover/drag memory into `RunReplayPredictionUiState`. Input cancellation,
    scrubber hit testing, overlay drawing, unavailable-scrubber reset, and loaded
    presentation arm paths now route checkbox/ragdoll/horizon hover state and
    horizon dragging through `RunReplayPredictionState::ui`; prediction
    simulation, async build cursor, and future-node cache concerns remain in
    this step. Comment audit inspected the five touched source files with no
    deferred wording work. Validation: `tools\validate_format.bat` passed in
    9.5s; `tools\validate_replay_scrub.bat` passed in 28.1s;
    `tools\validate_full.bat` passed in 44.9s with 0 build warnings/errors, 0
    DX12 validation errors, matching DX12 screenshots, and
    `physics_regression_solver.csv` byte-exact at 20001 lines.
  - Progress note (2026-07-08, future-node cache): split render-facing future
    topology, scratch storage, build cursors, validity flags, and retained marker
    storage into `RunReplayPredictionFutureNodeCache`. Replay runtime resets,
    prediction visualizer reservations/draw decisions, future-node builders,
    replay camera focus, and automation reporting now route through
    `RunReplayPredictionState::futureNodeCache`; prediction simulation and async
    build cursor concerns remain in this step. Comment audit inspected the six
    touched source files with no deferred wording work. Validation:
    `tools\validate_format.bat` passed in 9.5s;
    `tools\validate_replay_scrub.bat` passed in 25.8s after fixing one wrapped
    stale access caught by the first scrub build; `tools\validate_full.bat`
    passed in 44.8s with 0 build warnings/errors, 0 DX12 validation errors,
    matching DX12 screenshots, and `physics_regression_solver.csv` byte-exact at
    20001 lines.
  - Progress note (2026-07-08, async build state): split prediction rebuild
    dirtiness, active/completed flags, tick cursors, last-build timestamp,
    build-frame scratch storage, and published-prefix count into
    `RunReplayPredictionBuildState`. The parent `RunReplayPredictionState`
    still exposes `PublishedBuildFrameCount()` and related helpers so readers
    keep using intent-level queries while build storage is now a separate
    concern; prediction simulation ownership remains in this step. Comment audit
    inspected the nine touched source files with no deferred wording work.
    Validation: `tools\validate_format.bat` passed in 9.5s;
    `tools\validate_replay_scrub.bat` passed in 28.2s;
    `tools\validate_full.bat` passed in 44.9s with 0 build warnings/errors, 0
    DX12 validation errors, matching DX12 screenshots, and
    `physics_regression_solver.csv` byte-exact at 20001 lines.
  - Completion note (2026-07-08, prediction simulation): split prediction horizon,
    target/source identity, private prediction engine/world, solver snapshot,
    body backups, and committed prediction frames into
    `RunReplayPredictionSimulationState`. Phase 0 is complete: reveal timing,
    UI hover/drag memory, async build state, future-node render cache, and
    prediction simulation storage are each separate small types behind
    `RunReplayPredictionState`. Comment audit inspected the ten touched source
    files with no deferred wording work. Validation:
    `tools\validate_format.bat` passed in 9.5s;
    `tools\validate_replay_scrub.bat` passed in 28.3s;
    `tools\validate_full.bat` passed in 44.9s with 0 build warnings/errors, 0
    DX12 validation errors, matching DX12 screenshots, and
    `physics_regression_solver.csv` byte-exact at 20001 lines.

### Phase 1 — Template the twin helpers

- [ ] **1.1** Introduce a single template over sample type collapsing
  `FindReplayBodyById` / `FindReplayPredictionBodyById` and the
  `ByModelIndex` / `FutureDepth` / `AddFutureNode` / `BuildFutureNodes` pairs.
  Gate: replay scrub + `validate_full`. Commit.

### Phase 2 — Evict replay from the game loop

- [ ] **2.1** Move the ~1,800 lines of replay probes and
  `RestoreReplayV2ArtifactTargetState` out of `RunFrame.cpp` into `Replay/`.
  Then promote the freed replay `.inl` to real TUs (this is plan 06 step 1.1).
  Gate: `validate_full` + replay scrub. Commit.

### Phase 3 — Make Butterfly Effect ownership explicit

- [ ] **3.1** Preserve the 256 MB shadow prediction engine, future-node cache,
  replay ribbons, and cinematic reveal behavior as Butterfly Effect demo
  functionality. Refactor them behind explicit replay prediction/presentation
  owner types, add or update behavior coverage for the demo path, and keep
  record/restore bit-exact. Gate: replay scrub + Butterfly Effect interaction or
  demo regression + `validate_full`. Commit.

## Validation

`tools\check_replay_scrub_regression.py` path via its validation script;
`tools\validate_full.bat` for `RunFrame` changes.

## Acceptance (structural)

- [ ] `RunFrame.cpp` contains no replay-probe bodies (they live in `Replay/`).
- [x] `RunReplayPredictionState` is split into single-concern types.
- [ ] The six twin helpers are one template.
- [ ] Butterfly Effect prediction, future-node visualization, replay ribbons,
  and cinematic reveal behavior are preserved and have explicit owner types.
- [ ] Replay subsystem LOC is materially reduced; record/restore stays bit-exact.

# 09 — Replay Subsystem Right-Sizing

Date: 2026-07-08
Status: Proposed
Priority: P2
Owner: Runtime / Replay
Source issue: audit iss-07 (severity 3)

## Problem

A debug replay feature grew into a ~17K-line product with a speculative what-if
physics engine and wall-clock cinematic storytelling, poorly separated from the
game loop.

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

- [ ] **Phase 0 — Split `RunReplayPredictionState`** by concern: prediction sim /
  UI hover / async build / render cache / reveal clock. Each becomes its own
  small type.
- [ ] **Phase 1 — Template the twin helpers.** One template over sample type
  collapses the six `Find*`/`*ByModelIndex`/`FutureDepth`/`AddFutureNode`/
  `BuildFutureNodes` pairs.
- [ ] **Phase 2 — Evict replay from the game loop.** Move the ~1,800 lines of
  replay probes and `RestoreReplayV2ArtifactTargetState` out of `RunFrame` into
  `Replay/`. (Coordinates with plan 01's `RunFrame` shrink and plan 06's `.inl`
  work.)
- [ ] **Phase 3 — Justify or cut speculation.** Decide whether the 256 MB shadow
  prediction engine and cinematic reveal earn their complexity; gate behind a
  build flag or remove the speculative parts.

## Risks / determinism

Replay is the one subsystem allowed to grow at runtime (via
`RuntimeReserveAllocator`); keep that contract. Record→restore must stay
bit-exact — guard with the replay scrub regression.

## Step-by-step implementation

Do steps in order; validate and commit per step. Record→restore must stay
bit-exact — the replay scrub regression is the gate.

### Phase 0 — Split `RunReplayPredictionState`

- [ ] **0.1** Split `RunReplayPredictionState` (`ReplayRuntime.h`) by concern
  into separate small types: prediction sim (owned engine/world), UI hover flags,
  async build cursors, future-node render cache, reveal clock. Do **one concern
  at a time**. Gate: replay scrub regression + `validate_full`. Commit per split.

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

### Phase 3 — Justify or cut speculation (DECIDE — stop for a human)

- [ ] **3.1** Decide whether the 256 MB shadow prediction engine and cinematic
  reveal earn their complexity: gate behind a build flag or remove. A smaller
  model must **not** cut features alone — surface this to a human. Leave unchecked
  until decided.

## Validation

`tools\check_replay_scrub_regression.py` path via its validation script;
`tools\validate_full.bat` for `RunFrame` changes.

## Acceptance (structural)

- [ ] `RunFrame.cpp` contains no replay-probe bodies (they live in `Replay/`).
- [ ] `RunReplayPredictionState` is split into single-concern types.
- [ ] The six twin helpers are one template.
- [ ] Replay subsystem LOC is materially reduced; record/restore stays bit-exact.

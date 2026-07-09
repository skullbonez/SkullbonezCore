# Replay: Prediction Job, Memory Quality, Code Size

Date: 2026-07-09 (consolidated)
Status: In progress — ~35% complete (prediction isolation done; worker job,
memory tuning, and code-size work open)
Impact area: replay runtime, replay prediction, physics stepping, UI
Consolidates: `fable_plans/03-prediction-isolated-world` (open Phase 3),
`replay-memory-quality-tuning-plan.md` (compressed — full field-level design
in that file's git history), and item 15.3 of
`engine-cleanup-plans/15-review-gaps.md`.

Context: `Runtime/Replay/` measures 18,147 lines across 24 files — 12.5% of
the engine, with the repo's largest file (`RunReplayTools.cpp`, 3,699 lines).
Replay also carries the engine's only approved runtime-allocation exception.
This plan owns making replay smaller, cheaper, and fully isolated.

## A. Prediction worker job (was fable-03 Phase 3 — deferred until after soak)

Prediction already steps a private replay-owned `PhysicsEngine` (the live
mutation window is deleted and guarded against). Remaining: move stepping off
the frame loop.

- [ ] A1. Wrap the tick loop in `Core/AmortizedTask`
  (`SubmitTick(pool)`, `SetBudget(ticksPerSubmit)`), state owned by
  `RunReplayPredictionState`. Single-writer rule: only the job writes build
  frames; the frame loop consumes published `buildFrameCount` prefixes.
- [ ] A2. Cancellation: `CancelPredictionJob` waits for or invalidates an
  in-flight task before clearing state.
- [ ] A3. Scene-mutation guard: verify every begin/branch/scene-load path
  cancels the job; `Hazard:` comment that the prediction engine holds values
  only, never pointers into live stores.
- [ ] A4. Gate: `validate_full` + 3 consecutive `validate_dx12_renderer` runs
  (frame pacing) + `validate_perf` + both prediction proofs.

## B. Replay memory quality tuning (was the June draft — re-derive before building)

Goal: substantially reduce replay memory while the default visual scrubber
stays lossless in look and feel. The June 25 draft's detailed data model
predates the plan-09 right-sizing and the snapshot table-drive — **re-derive
the data model against current replay code before implementing**; the design
intent to carry forward:

- Body dictionary + visual delta frames instead of full per-frame body arrays;
  quantized visual modes as opt-in presets.
- Solver keyframes + deltas instead of dense solver snapshots.
- User-facing presets (Lossless Look / Balanced / Memory Saver / Diagnostics
  Heavy) with a hard memory budget enforced through the existing
  `RuntimeReserveAllocator` owner.

- [ ] B1. Instrument current replay memory (per-category byte accounting) —
  the measurement decides whether the rest is worth building.
- [ ] B2. Split body metadata from visual pose; add delta frames.
- [ ] B3. Compact solver keyframes/deltas; artifact compatibility for saved
  replays.
- [ ] B4. Presets + budget enforcement + UI sliders.
- Gates: `validate_full` + `validate_replay_scrub` per slice; determinism
  untouched (visual store only) or proven byte-exact.

## C. Code-size right-sizing (15.3)

- [ ] C1. Per-file responsibility inventory of `Runtime/Replay/` (what breaks
  if deleted).
- [ ] C2. Delete or merge twin/parallel helpers surfaced by C1;
  `RunReplayTools.cpp` (3,699 lines) is the first target.
- [ ] C3. After A lands, delete any frame-loop budget machinery the worker job
  obsoletes; after B lands, delete superseded snapshot paths.

## Acceptance

- [ ] Prediction stepping runs as a worker job; frame loop only consumes
  published prefixes.
- [ ] Replay memory has measured per-category accounting and an enforced
  budget; default look is unchanged.
- [ ] `Runtime/Replay/` line count materially down with scrub/restore probes
  passing.

## Validation map

| Slice | Gate |
|-------|------|
| Prediction job | `validate_full` + renderer ×3 + `validate_perf` + prediction proofs |
| Memory/data-model changes | `validate_full` + `validate_replay_scrub` |
| Code deletion slices | `validate_full` + `validate_replay_scrub` |

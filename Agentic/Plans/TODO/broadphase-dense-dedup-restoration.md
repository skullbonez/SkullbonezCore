# Broadphase Dense Dedup Restoration

Date: 2026-08-03
Owner: Physics broadphase / `SpatialGrid`
Status: 2/3 phases complete; final performance and repository closure pending

Required commit subject after BR1:

`Broadphase Dense Dedup Restoration, TASK 2 / 3, 75% OVERALL COMPLETE — restore dense pair dedup`

## Problem And Evidence

The completed Broadphase Pair Dedup Cost campaign replaced `SpatialGrid`'s
triangular `pairSeen` bitset with per-body active-bucket membership construction
and intersection. That saved a bounded worst-case allocation of 4,193,792 bytes
at the 8,192-body scene ceiling, but its own final Profile evidence measured
CandidatePairs regressions of 25.5% at 37 bodies, 91.4% at 200, 54.8% at 520,
and 45.2% at 1,000. The final Source Modernization gate later exposed the same
broadphase cost against the committed performance comparison.

The owner rejects that CPU trade. Four MiB at the scene ceiling is an accepted,
intentional price for O(1) pair deduplication.

## Goal

Restore the scene-reserved dense triangular bitset without reverting unrelated
source modernization, pair-source filtering, canonical pair staging, or later
coverage. Prove the candidate-pair CPU regression is gone, preserve byte-exact
Physics behavior, and close without moving a performance or behavior baseline.

## Non-Goals

- Do not restore deletion-bound Debug pair-stream oracles.
- Do not refresh performance, Physics, Replay, SkullScope, or visual baselines.
- Do not change candidate admission, pair-source eligibility, or output order.
- Do not resume Dense Pile Sleep Resolution.

## Phases

- [x] **BR0 — owner ruling and bounded rollback map.** Accept the 4,193,792-byte
  scene-ceiling bitset, record the measured CPU regressions, and map only the
  membership-index stores, construction/intersection path, diagnostics, and
  architecture-specific tests for removal.
- [x] **BR1 — restore dense dedup and focused proof.** Restore scene-load
  reservation, frame-local zeroing, and the normalized triangular O(1) bit test.
  Add the permanent owner comment, retain pair-source/canonical-order behavior
  coverage, and pass a zero-warning Profile build plus all focused SpatialGrid
  tests.
- [ ] **BR2 — performance and closure.** Pass `validate_physics`,
  `validate_physics_deep`, `validate_perf`, `validate_fast`, and `validate_full`
  on final source; compare the broadphase markers to the rejected membership
  measurements; run all seven ownership inventories, the touched-source comment
  audit, and an independent ownership/correctness review. Publish a permanent
  closure report, delete this plan, and resume Source Modernization MZ4.

## Acceptance

- `SpatialGrid` owns one scene-reserved triangular bitset and no per-body pair
  membership index.
- The source comment states the accepted memory cost and the measured CPU cost
  of replacing it.
- Candidate and sleep-pruned streams remain byte-exact; no baseline moves.
- The mandatory performance comparison passes and its CandidatePairs/Broadphase
  markers no longer carry the rejected regression.
- Required builds, tests, inventories, comments, and independent review pass.

## Dependencies And Decisions

- The owner's 2026-08-03 instruction is the binding memory/CPU trade ruling.
- Source Modernization MZ4 remains at 4/5 until BR2 repairs and proves the
  inherited performance blocker.
- Dense Pile Sleep Resolution remains externally blocked and excluded under
  MASTER inventory rule 4.

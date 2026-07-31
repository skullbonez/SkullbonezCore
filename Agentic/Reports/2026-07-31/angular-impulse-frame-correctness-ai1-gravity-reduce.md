# Angular Impulse Frame Correctness — AI1 Gravity Reduce

Date: 2026-07-31
Branch: `nightrunner-30th-JUL-26`
Impact area: Physics mutual-gravity hot path, deterministic tests, profiling
Phase: AI1 complete

## Outcome

The bounded mutual-gravity path now emits canonical compact pair
contributions from disjoint worker slices, compacts those slices in ascending
chunk order, and reduces one linear active-pair prefix. The reduction retains
the original per-body floating-point addition sequence, but it no longer:

- clears every triangular pair slot at the start of each step;
- re-reads cold body records and hot receive state during reduction; or
- walks pairs that were rejected because of mass or receiver state.

AI1 changes no force arithmetic, worker threshold, worker hash, chunk boundary,
large-field fallback, or returned body-force layout.

## Representation And Order Proof

`PhysicsForceStage::MutualGravityPairForce` stores:

- the exact `Vector3` force produced by the existing pair arithmetic; and
- two packed 16-bit body values whose low bits hold the model index and whose
  high bit records whether that body receives the force.

The parallel path is bounded to 512 bodies, so the receiver bit cannot overlap
a valid model index. Receiver state is sampled in the build pass and cannot
change before the synchronous reduction returns.

Each worker owns the triangular storage range beginning at its first row. It
writes only admitted pairs, densely and in nested `(i,j)` order, and publishes
one count in its unique chunk slot. After the workers join, the main thread
moves those written prefixes forward in ascending chunk order. Therefore:

1. chunk order is ascending row order;
2. pair order inside a chunk is ascending nested `(i,j)` order;
3. the compact prefix is the original triangular sequence with rejected pairs
   removed; and
4. the final linear walk performs every surviving `+= force` and `-= force` in
   the same order each destination body observed before AI1.

Forward compaction is overlap-safe because every destination index is less
than or equal to its source index. A source is never overwritten before it is
read.

## Scratch And Allocation

The fixed list retains the largest admitted triangular extent so workers can
write disjoint slices without allocation or synchronization. `ExtendDefaultTo`
replaces the per-step `assign(..., ZERO_VECTOR)`:

- the first admitted extent is constructed within scene-reserved backing;
- equal-size or smaller later steps leave the live scratch extent untouched;
  and
- only the compact written prefixes are read.

Each pair record is 16 bytes rather than the previous 12-byte force-only slot,
so the 512-body cap reserves about 2 MiB instead of about 1.5 MiB. The capacity,
growth phase, hard cap, owner name, and high-water accounting are unchanged.
This is ordinary Physics scene scratch, not a Replay growth privilege.

## Focused Exactness

The 40-body worker-count fixture now deliberately inserts adjacent fixed bodies
across eight-row chunk boundaries. Fixed/fixed pairs create holes in multiple
worker slices, so the test exercises both prefix compaction and scheduling
invariance instead of only the dense all-receiver case.

The focused mutual-gravity selection passes in Profile:

- 10/10 test cases;
- 13,362/13,362 assertions;
- serial dispatch, one worker, and four workers are bit-exact;
- the 520-body exact serial fallback remains covered; and
- fixed, sleeping, massless, softening, antisymmetry, orbit, and collision
  cases remain covered.

## Profile Measurement

The same Profile executable shape and scene were used before and after:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step \
  --shadows off --scene SkullbonezData/scenes/space_field_200.scene.json
```

An additive `Frame/Physics/MutualGravity/Reduce` marker surrounded the old
nested reduction and the final compaction-plus-linear reduction. A temporary
scene logging stanza wrote the CSVs under `TestOutput/validation/`; it was
removed after measurement, and the tracked scene is byte-identical to `HEAD`.
Both runs contain 660 fixed-step samples.

| Reduce metric | Before | After | Change |
|---|---:|---:|---:|
| Mean | 0.093166 ms | 0.079943 ms | -14.19% |
| Median | 0.0924 ms | 0.0780 ms | -15.58% |
| P95 | 0.0973 ms | 0.0924 ms | -5.04% |
| Minimum | 0.0911 ms | 0.0766 ms | -15.92% |

Maximum samples were 0.1158 ms before and 0.1307 ms after; the distribution
improved at mean, median, and P95, while one after-run outlier was slower. The
phase claims the repeated-sample central and tail improvement, not an absolute
single-frame bound.

## Validation And Audit

- Profile build: passed with zero warnings and zero errors.
- Focused Profile mutual-gravity selection: 10/10 cases and 13,362/13,362
  assertions passed.
- The first performance-gate attempt rejected direct `PhysicsFixedList::resize`
  through allocation policy. The implementation now uses the fixed-list-owned
  `ExtendDefaultTo` seam; only final-source reruns count below.
- Touched-source comment audit:
  `PhysicsForceStage.cpp`, `PhysicsForceStage.h`, and `TestDeterminism.cpp`,
  3/3 checked, zero deferred.
- Temporary `space_field_200.scene.json` logging edit: removed; no tracked scene
  diff remains.
- `validate_fast`: all nine stages passed with 458/458 cases and
  2,424,719/2,424,719 assertions; the two AI0 internal mismatches remain the
  expected `should_fail` evidence for AI2.
- `validate_physics`: passed with the 44,401-line
  `physics_regression_varied.csv` byte-exact.
- `validate_physics_deep`: passed with the varied-scene, bullet wall/object/
  terrain, shooting-reaction volley, three-body chaos, known-issue signature,
  and query artifacts byte-exact.
- `validate_perf`: allocation policy, absolute DX12 budgets, DX12 regression
  comparison, absolute `PHYSICS_BENCH` budgets, benchmark regression
  comparison, and the scale matrix passed. One post-format run encountered
  unrelated host timing noise in DX12 Frame and `PHYSICS_BENCH`; the idle-host
  rerun passed without refreshing a baseline.
- Final builds report zero warnings and zero errors. Formatting and the strict
  dependency, allocation, aggregate, extraction-scar, wide-signature,
  complexity, build-configuration, reachability, and glossary gates pass.
- Independent review: clear. It confirmed canonical `(i,j)` accumulation order,
  disjoint chunk publication, overlap-safe forward compaction, safe packed
  indices under the 512-body cap, real sparse worker coverage, retained-scratch
  ownership, and zero Replay, scene, baseline, or unrelated artifact movement.

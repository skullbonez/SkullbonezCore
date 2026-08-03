# Broadphase Pair Dedup Cost BD2 — Exact-Equivalence Implementation

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Plan phase: BD2
Result: COMPLETE — independent review accepted the implementation with no blockers

## Outcome

`SpatialGrid` now makes production first-seen decisions from scene-reserved,
per-body active-bucket membership slices. The existing active-bucket traversal
is unchanged: a normalized pair is admitted only at its earliest shared bucket
that is eligible for the current pair-source mode. The old triangular bitset is
retained only in Debug as the temporary BD2/BD3 oracle and is absent from
Profile storage and work.

The implementation is behavior-preserving. No physics, replay, SkullScope,
visual, schema, config, or golden artifact changed, and no baseline was
refreshed.

## Implemented Contract

- `ReserveSceneCapacity` registers and reserves three fixed owners during
  `SceneLoad`: ordinal rows at `entries.capacity() + overlayEntries.capacity()`,
  body offsets at `B + 1`, and reusable counts at `B`.
- The compile-time ceilings are 73,732 ordinal rows, 8,193 offsets, and 8,192
  counts. The exact dynamic storage formula is `24B + 10,244` bytes.
- A preflight derives raw per-body slice bounds from persistent ranges and
  swept-overlay rows. Ascending active-bucket traversal then fills unique
  ordinal prefixes in place. This accepted BD2 refinement is recorded in the
  BD1 owner addendum; it replaces the originally described separate fill/sort
  schedule without changing the mechanism, capacity, or exhaustion ruling.
- Every bucket contributes complete membership before any restricted-mode,
  singleton, or pair-evaluation exit. Ineligible buckets use an append-only
  walker; eligible multi-row buckets reuse coordinate-hash alias compaction.
  The resulting detached index is complete and mode-independent after the
  traversal.
- Prefix intersection ignores an earlier common ordinal only when that bucket
  is ineligible for restricted pair-source work. The current eligible bucket
  must be the tail of both prefixes before a pair can be first-seen there.
- Capacity failure is Lane F and reports owner, requested rows, logical and
  reserved capacity, high-water, admitted bodies, persistent/overlay live and
  capacity rows, and allocation phase. A Debug-only logical ceiling provides a
  deterministic child-probe seam without weakening production capacity.
- Debug evaluates the membership decision and dense-bit decision for every
  pair observation before geometry admission, fatals on the first mismatch,
  and requires equal aggregate first-seen and geometry-invocation counts.

## Capacity And Memory Evidence

| Scene bodies | Ordinals | Offsets | Counts | Replacement bytes |
|---:|---:|---:|---:|---:|
| 3 | 5,144 | 4 | 3 | 10,316 |
| 2,000 | 21,120 | 2,001 | 2,000 | 58,244 |
| 4,000 | 37,120 | 4,001 | 4,000 | 106,244 |
| 8,192 | 70,656 | 8,193 | 8,192 | 206,852 |

The three-body focused fixture measures total grid dynamic backing at 134,476
bytes in Debug (including the dense shadow) and 134,468 bytes in Profile. The
2,000-body reserve census reports 105 growth events and 111 Physics capacity
rows in Debug, versus 101 and 107 in Profile; the dense `pairSeen` owner is
Debug-only.

## Focused Proof

`TestSpatialGrid.cpp` now pins:

- complete membership for restricted buckets, including singleton and earlier
  unstamped memberships;
- a production-filtered, restricted positive pair after an earlier ineligible
  shared bucket, with one geometry admission in Debug;
- persistent-plus-swept membership in one bucket;
- exact coordinate-hash alias behavior and swept alias compaction;
- raw-row high-water 10 with unique membership counts 7 and 1;
- SceneLoad capacities, retained backing/high-water, and Debug/Profile memory;
- the planted membership-capacity fatal diagnostic.

The focused Debug SpatialGrid suite passes 27 cases and 8,646 assertions. The
full Profile executable passes 509 cases and 2,431,143 assertions.

## Validation

| Command | Result |
|---|---|
| `tools\validate_format.bat` | PASS; 587 source files and 327 headers clean |
| `tools\validate_tests.bat` | PASS; Profile build and unit harness |
| `tools\validate_physics.bat` | PASS; Debug/Profile binaries and byte-exact Physics |
| `tools\validate_build.bat Automation` | PASS; refreshed production object roots for reachability |
| `tools\validate_fast.bat` | PASS in 379 s; format, metadata, dependency, all ownership inventories, builds, and tests |
| `git diff --check` | PASS |

BD3 still owns the complete BD0 stream-oracle comparison across 0/1/4 workers,
the scale matrix, `validate_physics_deep`, and `validate_perf`. Preliminary
timing observations are deliberately not used as BD2 acceptance evidence and
no performance baseline was rewritten.

## Comment Audit

The touched-source comment audit inspected all 6 source-bearing files and
deferred none:

- `SkullbonezSource/Physics/PhysicsFixedList.h`
- `SkullbonezSource/Physics/SpatialGrid.cpp`
- `SkullbonezSource/Physics/SpatialGrid.h`
- `SkullbonezTests/TestPhysicsHandles.cpp`
- `SkullbonezTests/TestRuntimeContracts.cpp`
- `SkullbonezTests/TestSpatialGrid.cpp`

Headers and nearby comments now state the membership owner, prefix lifetime,
restricted-skip sequencing, hash-alias hazard, temporary Debug oracle, reserve
invariants, and fatal-test seam. Repository-relative `Related:` paths resolve.
A campaign-wide checklist was not required for this touched-file audit.

## Independent Review

The final read-only reviewer returned **ACCEPT — no blockers** after confirming:

- the BD1 owner addendum explicitly approves fused monotonic prefix filling;
- the filtered/restricted fixture is genuinely positive in Profile;
- unstamped memberships are appended before pair-work skips;
- eligible occupants are appended before pair evaluation;
- SceneLoad reservation, loud exhaustion, Debug-only dense state, production
  replacement storage, and memory accounting remain correct.

The reviewer intentionally left the BD3 oracle, scale, and performance evidence
for the next phase.

## Next

Run BD3 from this checkpoint: compare every BD0 stream and geometry count across
0, 1, and 4 workers, run the deep and performance gates, report exact memory and
timing deltas, and remove the temporary Debug cross-check only after the proof
passes.

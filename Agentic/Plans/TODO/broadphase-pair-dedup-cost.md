# Broadphase Pair Dedup Cost

Date: 2026-08-02
Status: IN PROGRESS — 4/5 phases complete
Impact area: Physics broadphase spatial grid, candidate pair emission, tests
Owner: Physics broadphase
Priority: Second

## Problem And Evidence

The spatial grid exists to avoid O(N²) pair work, and it carries an O(N²)
per-tick clear to do it.

`SpatialGrid::ResetCandidatePairDedup()` (`SkullbonezSource/Physics/SpatialGrid.cpp:1108`)
zeroes a dense triangular bitset covering every possible body pair at the start
of every broadphase pass. The sizing is exact:

| Quantity | Source | Value |
|---|---|---|
| `MAX_SCENE_OBJECTS` | `Core/SceneCapacity.h:38` | 8,192 |
| `DEFAULT_SCENE_OBJECT_CAPACITY` | `Core/SceneCapacity.h:37` | 4,000 |
| `MAX_PAIR_IDENTITIES` | `SpatialGrid.h:125` | 33,550,336 |
| `PAIR_WORDS` compile-time ceiling | `SpatialGrid.h:129` | 524,224 words = 4.19 MB |
| Cleared per pass at 4,000 bodies | `SpatialGrid.cpp:1118-1131` | 124,969 words ≈ 999,752 bytes |
| `MAX_CANDIDATE_PAIRS` emitted output | `SpatialGrid.h:124` | 32,768 |
| Broadphase passes per frame | `PhysicsTimestep.h:31` | up to 8 |

At the default 4,000-body capacity the pass clears roughly one megabyte to
deduplicate at most 16,000 entries — about 62 bytes of memory traffic per emitted
pair, up to eight times per frame. At the 8,192 ceiling it is 4.19 MB per pass.
The reservation is runtime-sized from actual scene capacity in
`ReserveSceneCapacity` (`SpatialGrid.cpp:155`), so this is a real per-scene cost,
not a compile-time worst case that never materializes.

The output is already being sorted into canonical order. `candidatePairSortKeys`
and `candidatePairSortScratch` are reserved alongside the bitset, and
`Stages/PhysicsBroadphaseStage.cpp` states the stage "canonicalizes solver-visible
pair order". Deduplication inside a list that is already being sorted is close to
free, which makes the dense bitset the wrong tool by a wide margin.

**The hazard that makes this non-trivial.** Deduplication is not a pure filter
here. `MarkFilteredCandidatePairFirstSeen` (`SpatialGrid.cpp:1168`) calls
`MarkCandidatePairFirstSeen` and then, on the first-seen edge only, runs
`BroadphaseCandidateGeometryCanTouch` and may emit into `sleepPrunedPairs`
(`SpatialGrid.cpp:1185-1205`). The geometric filter and the sleep-pruned
diagnostic therefore fire **at first-seen time during traversal**. Moving
deduplication to a post-traversal sort-and-unique would change when the filter
runs, how many times it runs, and the order in which `sleepPrunedPairs` is
populated — while the final candidate pair list still looked correct. The
broadphase stage header already commits that "`remove_if` predicates preserve
their diagnostic side effects in canonical solver-visible order", so both the
pair list and the diagnostic order are contract.

## Goal

Candidate pair emission produces a byte-identical pair list and a byte-identical
sleep-pruned diagnostic list at materially lower memory traffic, with the O(N²)
dedup structure and its per-pass clear removed.

## Owner Ruling — Behavior-Preserving Only

The owner ruled on 2026-08-02 that this change is behavior-preserving only. The
optimization must produce byte-identical output. **If any physics, SkullScope,
replay, or visual baseline moves, the change is wrong and is reverted.** This
plan carries no divergence authority and no baseline-refresh authority. A
candidate mechanism that would require an owner-approved baseline transition is
out of scope and must be rejected at BD1 rather than implemented and argued for
at BD3.

### BD3 Owner Refinement — Eligibility-Projected Prefixes

BD3 supersedes one construction detail in the BD1 addendum and BD2 phase record:
the final per-body ordinal prefixes are intentionally eligibility-projected per
query, not complete and mode-independent. Let `M(x)` be the active buckets that
contain body `x`, and let `E` be the buckets eligible for the current pair-source
mode. First-seen ownership is
`min(M(a) intersection M(b) intersection E)`, which is identical to
`min((M(a) intersection E) intersection (M(b) intersection E))`. An unstamped
bucket therefore cannot own or change restricted work and does not need an
ordinal write.

The same refinement omits buckets with fewer than two unique bodies because no
distinct pair can share them. Raw slice capacity remains conservatively derived
from every retained persistent and overlay row, and Debug still walks projected
raw chains for integrity accounting. This refinement changes neither pair order,
geometry invocation order, sleep diagnostics, exhaustion behavior, nor the BD1
replacement family; it removes only membership work that cannot participate in
the earliest-eligible intersection.

## Non-Goals

- No change to pair membership, pair order, geometric filtering, sleep pruning,
  swept-overlay semantics, or cell membership.
- No change to `MAX_SCENE_OBJECTS`, `MAX_CANDIDATE_PAIRS`, or any scene capacity.
- No baseline refresh of any kind.
- No new allocation path. The replacement obeys the existing static allocation
  policy and reserves at `SceneLoad` through the registered owner like every other
  grid store.
- Not a general broadphase redesign. Cell sizing, hashing, and the swept overlay
  are untouched.

## Phases

- [x] **BD0 — Measure the current cost and lock the equivalence oracle.** Record
  the exact per-pass cleared byte count and measured broadphase time for a fixed
  set of scenes spanning small, default-capacity, and dense-stress body counts,
  from the current Profile binary. Capture the complete current candidate pair
  list and the complete current `sleepPrunedPairs` list for each scene across a
  fixed tick count as the byte-exact equivalence oracle for BD3. Record how many
  times `BroadphaseCandidateGeometryCanTouch` is invoked per pass, because
  invocation count is part of the contract this plan must preserve. Also record
  the current `CollectPhysicsWorldMemoryBytes` contribution from `pairSeen`.

- [x] **BD1 — Choose and record the replacement mechanism.** Evaluate at minimum:
  (a) a generation-stamped open-addressed table sized from `MAX_CANDIDATE_PAIRS`
  rather than from N², cleared by bumping a generation counter instead of memset;
  (b) per-body sorted membership intersection that never produces a duplicate to
  reject; (c) folding dedup into the existing canonical sort. For each, state
  explicitly whether it preserves first-seen ordering, filter invocation count,
  and `sleepPrunedPairs` population order. Reject any candidate that does not —
  option (c) is expected to fail the diagnostic-order test and the plan must say
  so rather than discover it during implementation. Record the chosen mechanism,
  its exact capacity derivation, its exhaustion behavior, and why its first-seen
  semantics are identical rather than merely equivalent.

- [x] **BD2 — Implement behind an exact-equivalence proof.** Land the chosen
  mechanism with a temporary debug-only cross-check that runs both the old bitset
  and the new structure and fatals on the first divergence in pair identity,
  pair order, filter invocation, or diagnostic order. Reserve the new structure at
  `SceneLoad` through the registered `RuntimeReserveAllocator` owner with a
  concrete `PhysicsCapacityReason`, and make exhaustion a loud owner/capacity/
  high-water diagnostic. Extend `SkullbonezTests/TestSpatialGrid.cpp` with cases
  pinning first-seen semantics, generation rollover if the chosen mechanism has
  one, and capacity exhaustion.

- [x] **BD3 — Prove byte-exactness and measure the gain.** Compare against the
  BD0 oracle: pair lists byte-identical, `sleepPrunedPairs` byte-identical, filter
  invocation counts identical, across every BD0 scene and across 0, 1, and 4
  worker counts. Run `tools\validate_physics.bat` and
  `tools\validate_physics_deep.bat` and confirm every baseline is unchanged. Run
  `tools\validate_perf.bat` and report the measured broadphase delta and the
  memory delta from `CollectPhysicsWorldMemoryBytes` against BD0. Remove the BD2
  cross-check only after this phase passes, and state plainly in the phase record
  if the measured gain is smaller than expected — a correct change with a modest
  gain still closes, an unmeasured one does not.

- [ ] **BD4 — Close.** Delete the old bitset, `PAIR_WORDS`, and
  `MAX_PAIR_IDENTITIES` along with their static assertions if nothing else
  consumes them, or record why a survivor is retained. Update the `SpatialGrid.h`
  learning header and any comment that describes the retired dedup mechanism, per
  the Comment Quality Gate rule that comments asserting ownership or sequencing
  must name post-change reality. Audit every touched source-bearing file. Obtain
  an independent read-only review that specifically checks first-seen semantics,
  diagnostic ordering, allocation policy compliance, and exhaustion behavior.

BD0 completed on 2026-08-02. Uninstrumented Profile artifacts lock 37/200/520/
1,000/2,000/4,000/5,000-body timing, exact clear spans, and exact `pairSeen`
world-memory contributions. Four 360-pass Debug v2 streams preserve raw-grid,
post-augmentation, raw first-seen sleep, final solver, and final sleep lists plus
grid/total geometry-call counts. Complete files are byte-identical across 0, 1,
and 4 workers. Evidence, exact inputs, decoder, hashes, and resolved independent
review are in
`../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd0-baseline.md`.

BD1 completed on 2026-08-02. Pure generation-stamped open addressing and
post-sort dedup are rejected because emitted-pair capacity cannot bound legal
pre-filter identities or observations. The chosen mechanism keeps the existing
bucket traversal and admits a pair only from the earliest eligible active bucket
shared by both bodies, found through a scene-reserved per-body sorted membership
index. This preserves the exact geometry/sleep first-seen event without an
N-squared store. Capacity, memory, exhaustion, hash-alias, overlay, and BD2 proof
requirements are locked in
`../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd1-decision.md`.

BD2 completed on 2026-08-02. Scene-reserved per-body active-bucket membership
slices now make the production first-seen decision while the dense bitset is a
Debug-only temporary oracle. Complete mode-independent membership is appended
before restricted/singleton exits, coordinate-hash aliases compact by bucket
ordinal, and planted exhaustion reports the complete owner/capacity state.
Focused and full tests, byte-exact Physics, format, Automation, fast validation,
a 6/6 touched-source comment audit, and independent ACCEPT pass. Evidence is in
`../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd2-implementation.md`.

BD3 completed on 2026-08-02. The final eligibility-projected prefixes preserve
every BD0 pair, sleep, and geometry-count byte across all four workloads and
0/1/4 workers while omitting unstamped and singleton-only ordinal writes. The
unmodified performance gate passes without baseline refresh; exact sparse 4,000
improves Broadphase/CandidatePairs by 28.7%/41.9%, and sleeping-heavy 5,000 by
14.2%/66.7%, while the report records the smaller-scene tradeoff plainly. Core
and deep Physics, full tests, format, fast validation, a 3/3 touched-source audit,
and independent ACCEPT pass. Evidence is in
`../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd3-proof.md`.

## Dependencies And Decisions

- Runs after the completed Narrowphase Manifold And Sleep Coverage plan
  (`../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-closure.md`)
  so the physics test net is at full strength before a broadphase mechanism
  changes. The dependency is ordering only; no BD phase consumes an NM artifact.
- The owner ruling above is binding for the whole plan. It is recorded here so a
  later reviewer does not relitigate whether a canonical order change would have
  been acceptable.
- `SpatialGrid*` maps to `validate_physics` plus `validate_perf` under the
  file-to-validation table, and both are mandatory at BD3 regardless of how small
  the final diff is.
- The BD2 cross-check is temporary scaffolding and must not survive into the
  closing commit. If it proves useful enough to keep, that is a separate decision
  with its own owner, phase gate, and cost record — not a silent retention.
- If BD1 concludes that no mechanism preserves all three contracts exactly, the
  plan stops at BD1 with that finding recorded, and the O(N²) clear is retained
  with an explicit ruling explaining why. A recorded retain decision is a valid
  outcome; an unproven optimization is not.

## Acceptance

The plan closes when the O(N²) dedup structure and its per-pass clear are gone,
the candidate pair list and sleep-pruned diagnostic list are byte-identical to the
BD0 oracle across every scene and worker count, no baseline moved, the memory and
timing deltas are measured and reported, `TestSpatialGrid.cpp` pins the new
first-seen and exhaustion semantics, comments describe the post-change mechanism,
and independent review finds no ordering or allocation-policy defect.

## Validation

- BD0 oracle comparison: pair list, diagnostic list, and filter invocation counts
- `tools\validate_physics.bat` — byte-exact, no baseline movement
- `tools\validate_physics_deep.bat` — no baseline movement
- `tools\validate_perf.bat` — measured broadphase and memory delta
- `tools\validate_tests.bat`
- `tools\validate_fast.bat`
- `tools\validate_full.bat` at the closing gate
- Touched-source comment audit
- Independent read-only review

## Related

- `../../../SkullbonezSource/Physics/SpatialGrid.cpp`
- `../../../SkullbonezSource/Physics/SpatialGrid.h`
- `../../../SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- `../../../SkullbonezSource/Physics/PhysicsStageCapacity.h`
- `../../../SkullbonezTests/TestSpatialGrid.cpp`
- `../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-closure.md`
- `../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd0-baseline.md`
- `../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd1-decision.md`
- `../../Reports/2026-08-02/broadphase-pair-dedup-cost-bd2-implementation.md`

# Broadphase Pair Dedup Cost - BD1 Replacement Decision

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Main baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Task-start tip: `0a4921001ba2e4be4da3398d1280afc8c9232d93`
Plan progress: 2/5
Active/future portfolio progress: 2/20 (10%)

## Outcome

BD1 chooses a traversal-preserving, per-body sorted bucket-membership index.
The existing active-bucket and within-bucket loops remain the producer. For a
normalized pair observed in the current bucket, the new index admits the pair
only when that bucket is the earliest eligible bucket shared by both bodies.
That event is exactly where the old triangular bit changed from zero to one.

The index is linear in bounded grid-membership rows, has no pair-identity
capacity, and preserves the current event at which geometry and the raw sleep
diagnostic run. Generation-stamped open addressing and post-sort dedup are
rejected because a capacity derived from emitted pairs cannot hold every legal
pre-filter identity or observation. This plan has no authority to add that new
failure lane.

## Binding Contract

The BD0 artifacts distinguish raw identities from emitted rows. Per pass they
record 17-24, 3,210-3,626, 3,843-4,297, and exactly 3 geometry invocations for
the varied, sparse-4,000, sleepy-5,000, and sleep-order workloads respectively.
The latter three workloads can emit no solver pair at all. These are current
measurements, not capacity budgets: the replacement must remain correct for the
complete supported scene domain.

`MarkFilteredCandidatePairFirstSeen` currently marks an identity before
`BroadphaseCandidateGeometryCanTouch`. The first successful mark therefore owns
three observable facts:

1. the one geometry invocation for that identity;
2. the immediate traversal-order append to raw `sleepPrunedPairs`, if both
   bodies sleep and geometry admits them; and
3. the accepted candidate later emitted in canonical body-pair order.

Debug visits every active bucket. Profile/Release may restrict traversal to
`pairSourceGeneration`-stamped buckets. The selected predicate must use the
same eligibility rule in both modes.

## Candidate Decision

| Candidate | First-seen event and geometry count | Raw sleep order | Capacity result | Decision |
|---|---|---|---|---|
| Generation-stamped open addressing | Preserved only while insertion succeeds. | Preserved only while insertion succeeds. | An output-derived table cannot cover all legal pre-filter identities. Exact worst-case coverage is much larger than the current bitset. | Reject. |
| Per-body membership intersection, enumerated by body identity | Geometry can run once, but in canonical body order. | Changes from traversal-first to body-pair order. | Linear membership storage. | Reject this naive form. |
| Earliest-shared-bucket membership intersection inside the existing traversal | Geometry runs at the exact old first-unset-bit event. | Exact traversal-first order. | Linear membership storage with no pair-identity ceiling. | **Choose.** |
| Post-sort dedup before geometry | Geometry count is recoverable. | Changes to pair-key order. | Must stage every pre-filter observation, not emitted pairs. | Reject. |
| Filter first, then sort/unique | Invokes geometry once per cell observation. | May duplicate/change diagnostic rows. | Emitted storage is insufficient for raw observations. | Reject. |

### Why open addressing is not behavior-preserving

For admitted body capacity `B`, emitted pair capacity is
`P(B) = min(4B, 32,768)`, while the raw identity universe is
`R(B) = B(B-1)/2`.

| Bodies | `P(B)` | `R(B)` | 50%-load slots from `P(B)` | Packed 8-byte table |
|---:|---:|---:|---:|---:|
| 4,000 | 16,000 | 7,998,000 | 32,768 | 256 KiB |
| 8,192 | 32,768 | 33,550,336 | 65,536 | 512 KiB |

A legal dense cell may expose nearly `R(B)` normalized identities even when
geometry rejects all of them and the emitted list remains empty. A packed
`uint32 key + uint32 generation` table with exact 50%-load coverage would need
128 MiB at 4,000 bodies and 512 MiB at 8,192 bodies. Full-load minima are about
61 MiB and 256 MiB. The current bitset is roughly 0.95 MiB and 4 MiB.

Deterministic probing and generation rollover are implementable, but they do
not repair this capacity mismatch. Fataling at an output-derived slot ceiling
would make a currently successful scene fail, contrary to the owner ruling.

### Why post-sort dedup is not behavior-preserving

Sorting normalized observations before geometry changes the sleep-order fixture
from traversal-first `(1,2), (0,1)` to pair-key order `(0,1), (1,2)` after the
rejected `(0,2)` row is removed. Filtering before sorting invokes geometry for
duplicates. A two-sort rescue could attach a discovery ordinal, retain the
minimum ordinal for each key, then sort winners by ordinal before filtering,
but it must store every raw observation. At the identity-universe lower bound,
an 8-byte key/ordinal log plus allocation-free radix scratch needs about 122 MiB
at 4,000 bodies and 512 MiB at 8,192 bodies; cross-cell duplicates raise the
observation requirement further. That is not folding dedup into the existing
bounded canonical sort.

## Chosen Mechanism

### Index construction

Immediately before pair traversal, `SpatialGrid` builds one private detached
index from current persistent entries and current-generation swept-overlay
entries:

1. count source rows for each admitted body;
2. prefix-sum `uint32_t` body offsets;
3. fill `uint16_t` active-bucket ordinals through dedicated `uint32_t`
   count/cursor rows;
4. sort each body slice by active ordinal and collapse duplicate bucket aliases;
5. retain the slices synchronously through that candidate-collection call.

The identity is a `Bucket::activeIndex`, not `(ix,iy,iz)`. `SpatialCellKey` is a
hash and `FindBucket` compares the hash key; different coordinates can
deliberately share one conservative bucket. Exact-coordinate or `CellRange`
intersection would remove current false positives and change geometry counts.
Persistent and swept rows for the same body/bucket likewise collapse to one
membership, matching `CollectBucketObjects`.

`activeIndex` is rebuilt into the index every collection because bucket
retirement uses swap removal. The fixed bucket ceiling is 8,192, so every live
ordinal fits `uint16_t`; an explicit static assertion will own that assumption.
There is no generation counter and no rollover path.

### First-seen proof

The outer `activeBuckets` traversal and the two nested `cellIndices` loops remain
byte-for-byte authoritative. For the current normalized pair, intersect the two
sorted ordinal slices and find the first common eligible ordinal. When
pair-source restriction is enabled, a common bucket is eligible only if its
`pairSourceGeneration` is current.

`CollectBucketObjects` already emits each body at most once in one bucket.
Therefore an earlier observation of the pair exists if and only if the bodies
share an earlier eligible active ordinal. Admitting only when the first common
ordinal equals the current outer-loop ordinal is equivalent to the old first
unset triangular bit:

- geometry runs once, at the same traversal event and in the same global order;
- a sleep-pruned row appends at the same event and in the same raw order;
- accepted nodes enter the unchanged per-min-body canonical staging; and
- later fixed/joint pruning and `RecordCandidates` see unchanged lists.

### Exact capacity and memory

For admitted body capacity `B`:

```text
persistent source rows = 8B + 1,024
overlay source rows    = 4,096
membership ordinals   = 8B + 5,120
body offsets          = B + 1
count/cursor rows      = B
committed bytes        = 2(8B + 5,120) + 4(B + 1) + 4B
                       = 24B + 10,244
```

The ordinal list's compile-time ceiling is
`MAX_CELL_ENTRIES + MAX_SWEPT_CELL_ENTRIES = 73,732`; its runtime reservation is
`entries.capacity() + overlayEntries.capacity()`. Offsets have 8,193 compile-
time rows and cursors 8,192. All three stores are one invariant-owned index under
`SpatialGrid`, with explicit fixed-owner names and capacity reasons.

| Bodies | New committed bytes | Current `pairSeen` | Delta |
|---:|---:|---:|---:|
| 37 | 11,132 | 88 | +11,044 B |
| 4,000 | 106,244 | 999,752 | -893,508 B (-89.37%) |
| 8,192 | 206,852 | 4,193,792 | -3,986,940 B (-95.07%) |

The small-scene fixed overlay reservation makes committed memory modestly larger
at 37 bodies. The target default and ceiling capacities drop sharply, and no
per-pass N-squared clear remains. `CollectDynamicMemoryBytes` must remove
`pairSeen` and include each new store once.

### Exhaustion behavior

All three lists reserve at `SceneLoad` through registered
`RuntimeReserveAllocator` owners. Their logical row count is derived from the
already-reserved persistent and overlay source stores, so a valid grid cannot
exhaust the membership index independently. A mismatch still Lane-F fatals with
the index owner, requested rows, reserved capacity, high-water, persistent and
overlay live/capacity facts, admitted bodies, and runtime phase. It never drops a
row, grows during gameplay, or invents Replay growth authority.

Existing candidate and sleep-list exhaustion behavior remains unchanged. BD2
will exercise the index failure path through a narrow test-only logical-capacity
plant; production always supplies the exact source-derived reservation. This
tests the diagnostic without imposing a new legal-scene ceiling.

## Performance Risk And BD2/BD3 Proof

Index construction costs `O(M + sum(m_i log m_i))` for current membership rows.
Each raw co-bucket event replaces one bit test with an intersection bounded by
the two bodies' membership lengths. Ordinary persistent membership is expected
to remain at or below eight rows; dense single-cell scenes stop on the first
comparison. The 4,096-row swept-overlay ceiling makes oversized swept cases the
binding performance risk. BD3 must report every BD0 workload independently; a
correct but modest gain may close, while an unmeasured change may not.

BD2 will keep the dense bitset only as temporary Debug cross-check state. For
each occurrence it will compare old first-seen and new earliest-bucket decisions
before geometry, then compare complete geometry counts, raw/final candidate
lists, and raw/final sleep lists. Focused tests will cover traversal-first order,
an earlier unstamped shared bucket, deliberate spatial-hash aliasing, persistent
plus swept membership, duplicate bucket aliases, exact reservation, and planted
index exhaustion. The old bitset and its ruling disappear only after BD3 proves
all permanent streams byte-identical across 0, 1, and 4 workers.

## Ownership And Change Boundary

Implementation remains private to `SpatialGrid`; no field is added to
`PhysicsBodyRecord` or a hot body store. Expected seams are `SpatialGrid.h`,
`ReserveSceneCapacity`, `Clear`, `GetCandidatePairs`,
`GetFilteredCandidatePairsImpl`, the temporary legacy oracle, and
`CollectDynamicMemoryBytes`, plus capacity reasons and the fixed-owner/memory
tests. Cell sizing, hashing, active-bucket order, overlay admission, pair-source
stamping, canonical sorting, and stage pruning remain untouched.

## Independent Review And Validation

Three independent read-only reviews evaluated open addressing, membership
intersection, and post-sort dedup. All rejected output-derived dedup capacity;
all agreed that earliest eligible shared-bucket ownership is the only evaluated
mechanism that preserves the old first-seen event without N-squared state. No
reviewer edited the worktree.

The final independent read-only review returned `ACCEPT` with no blockers. It
rechecked the arithmetic, active-index proof, hash aliases, overlay rows,
pair-source eligibility, invariant-only exhaustion, reachability evidence, and
2/20 ledger, and confirmed the report does not overclaim BD2 implementation.

BD1 changes documentation and corrects two reachability-ruling evidence
sentences; it changes no runtime source. Final direct governance checks are
recorded below.

| Check | Result |
|---|---|
| `python tools/inventory_unreachable_symbols.py --repo . --strict` | Pass: 81 current rows are ruled; blocking diagnostics are zero. |
| `tools/reachability_rulings.json` strict JSON parse | Pass. |
| Campaign ledger reconciliation | Pass: plan 2/5 and active/future portfolio 2/20 (10%) in the plan, MASTER-PLAN, report, and SessionState. |
| `git diff --check` | Pass. |
| Runtime validation | Not required: BD1 changes documentation/governance evidence only and no runtime source. |

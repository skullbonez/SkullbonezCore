# Broadphase Capacity Right-Sizing

Date: 2026-07-29
Owner: skullbonez
State: In progress (BC0 complete)
Ledger tasks: 4 (BC0-BC3)
Branch: `nightrunner-29th-JUL-26`
PR: TBD

## Goal

Bring `SpatialGrid` onto the same scene-load reservation contract every other
physics owner already uses, so broadphase storage is sized by the loaded scene
instead of by the compile-time scene ceiling.

`SpatialGrid` is the last physics owner still built entirely from raw inline
arrays at `MAX_SCENE_OBJECTS` extent. It predates `PhysicsFixedList` and never
adopted it.

## Problem And Evidence

Derived on 2026-07-29 from the member declarations at
`SkullbonezSource/Physics/SpatialGrid.h:127-215`. BC0 confirms each figure with
a real `sizeof`; the table below is the estimate that motivates the work.

| Member | Extent | Approx bytes |
|---|---:|---:|
| `pairSeen` (triangular dedup bitset) | 524,224 words | 4,193,792 |
| `entries` | 69,636 × 40 | 2,785,440 |
| `buckets` | 8,192 × ~64 | ~524,288 |
| `bodyMemberships` | 8,192 × 32 | 262,144 |
| `candidatePairNodes` | 32,768 × 8 | 262,144 |
| `candidatePairSortKeys` + `candidatePairSortScratch` | 2 × 32,768 × 4 | 262,144 |
| `bucketHashHeads` + `activeBuckets` + `overlayActiveBuckets` | 3 × 8,192 × 4 | 98,304 |
| `overlayEntries` | 4,096 × 20 | 81,920 |
| `candidatePairHeads` + `cellObjectSeen` | 2 × 8,192 × 4 | 65,536 |
| **total** | | **~8.14 MiB** |

`PhysicsBroadphaseStage` holds it as an inline value member
(`Stages/PhysicsBroadphaseStage.h:76`), so the chain
`SpatialGrid` → `PhysicsBroadphaseStage` → `PhysicsWorld` → `PhysicsEngine` →
`SceneWorld` → `SceneController` → `Run` carries the full extent. Replay
prediction owns a second `PhysicsEngine`
(`Runtime/Prediction/ReplayPrediction.h:262`), so a running process holds
roughly **16 MiB** of broadphase tables — for a three-body determinism fixture
exactly as much as for an 8,192-body scale scene.

Two members account for 82% of the total, and both scale naturally with live
body count:

- `pairSeen` is `objectCount * (objectCount - 1) / 2` bits. At the default
  active capacity of 4,000 it needs 1.0 MiB; at the 300-body default scene it
  needs 5.6 KiB. It is committed at 4.0 MiB.
- `entries` is `MAX_SCENE_OBJECTS * 8 + MAX_SWEPT_CELL_ENTRIES + 4` on the
  stated derivation that an ordinary body occupies at most 8 cells. That
  derivation is per-body, so the extent should be per-body too.

## What This Is Not

This is a **footprint** plan, not a speed plan. Say so in the closure report.

The per-frame cost is already correctly scaled: `ResetCandidatePairDedup`
(`SpatialGrid.cpp:1048-1056`) computes `wordsNeeded` from the live
`objectCount` and clears only that prefix, so the 300-body scene does not memset
4 MiB per tick. Any measured speed change from this plan will come from cache
residency, not from removed work, and must be reported as measured rather than
claimed.

## Design Constraints

- **Reuse `PhysicsFixedList`.** Do not invent a second reservation mechanism.
  Each converted member registers a `RuntimeReserveAllocator` owner with a
  concrete `PhysicsCapacityReason` string, publishes its high-water, and fails
  loud on exhaustion — identical to `PhysicsBodyStore` and `ColliderStore`.
- **The hash table stays fixed.** `TABLE_SIZE` is 8,192 because it is a
  power-of-two hash table whose invariant block already states that exhausting
  it is a lane-F failure. `buckets`, `bucketHashHeads`, `activeBuckets`, and
  `overlayActiveBuckets` are not scene-sized and are out of scope.
- **Zero behavior change.** Every physics baseline byte must be identical. A
  differing byte means a reservation changed an index or a clear boundary.
- **Reservation happens at `SceneLoad` only.** `SpatialGrid` gets no replay
  growth privilege. It is not in the replay reserve inventory and must not enter
  it.
- **Cell-size changes already cold-clear.** `SetCellSize` performs a cold reset;
  confirm reservation interacts correctly with that path and with
  `BeginFrame`'s dense-prefix shrink.

## Ledger

- [x] BC0 — Confirm the real footprint with `sizeof` on each member and on
  `SpatialGrid` as a whole, in all three configurations. Record committed bytes
  for the 300-body default scene, the 4,000-body active capacity, and the
  8,192-body ceiling. Confirm which members are genuinely scene-sized versus
  fixed-topology, and record the reservation call site and ordering.
- [x] BC1 — Convert `pairSeen` and `entries` to reserved storage with registered
  owners and capacity reasons. These two are 82% of the total. Byte-exact
  required. The runtime capacities are the triangular pair-word count and
  `8 * bodyCapacity + 4` persistent rows; the historical additional 4,096
  persistent rows are removed because swept occupancy has its own store.
- [ ] BC2 — Convert the remaining scene-sized members: `bodyMemberships`,
  `candidatePairHeads`, `cellObjectSeen`, `candidatePairNodes`,
  `candidatePairSortKeys`, `candidatePairSortScratch`, `overlayEntries`.
  Byte-exact required.
- [ ] BC3 — Closure. Report before/after committed bytes at all three scene
  sizes and for the prediction engine, confirm the memory-tab capacity rows
  reflect the new owners, audit comments, pass independent review, and run every
  mapped gate.

## BC0 Evidence

Debug, Profile, and Release all measure `sizeof( SpatialGrid )` as exactly
8,535,792 bytes with 8-byte alignment. Current committed bytes are therefore
8,535,792 for one grid and 17,071,584 for the live plus Replay prediction grids
at 300, 4,000, and 8,192 admitted bodies alike. Arrays targeted for registered
scene-load storage account for 7,913,120 bytes; the retained inline hash
topology/state core is 622,672 bytes.

`buckets`, `bucketHashHeads`, `activeBuckets`, and `overlayActiveBuckets` are
fixed topology. Body membership, triangular pair dedup, candidate heads/nodes/
sort storage, cell-object stamps, and persistent cell entries are scene-derived.
`overlayEntries` is a fixed 4,096-row transient-work ceiling rather than a
body-count formula; BC2 still registers it but must not mislabel or shrink it
without a separate behavior proof.

The reservation chain is `SceneAuthoredSetup` -> `SceneWorld` SceneLoad scope ->
`PhysicsEngine` -> `PhysicsWorld` -> `PhysicsBroadphaseStage`. BC1 adds the grid
reservation first inside the stage reserve, before pair outputs. `SetCellSize`
cold-clears retained rows and `BeginFrame` only shrinks/resets live prefixes;
neither may reserve, decommit, or acquire Replay growth.

Permanent evidence:
`Agentic/Reports/2026-07-29/broadphase-capacity-right-sizing-bc0-census.md`.

## BC1 Evidence

`SpatialGrid.entries` and `SpatialGrid.pairSeen` now use registered
`PhysicsFixedList` backing reserved first inside
`PhysicsBroadphaseStage::ReserveSceneCapacity`. Persistent entries reserve
`8 * bodyCapacity + 4`; pair dedup reserves the triangular identity bit count
rounded up to 64-bit words. Both retain backing and high-water through
`Clear`, `SetCellSize`, and `BeginFrame`, and neither has Replay growth
privilege.

The historical extra 4,096 persistent rows are not carried into runtime
capacity: production cell-size selection bounds ordinary membership to eight
cells per body, while swept occupancy is already isolated in
`overlayEntries`. The compile-time ceiling remains unchanged. Focused tests
prove exact capacity/reason rows, deterministic append/free-list reuse,
retained backing/high-water, allocation-free dense-prefix shrink, startup
reserve denial, and a 12-row reserve whose thirteenth Physics-phase insert
fails with exact owner, capacity, high-water, and phase.

Profile-focused coverage passes SpatialGrid 19/19 cases and 8,545 assertions,
fatal contracts 1/1 and 221 assertions, reserve allocator 20/20 and 212
assertions, the determinism source suite, and the 2,000-body exact owner census
with 5,648 assertions. `tools\validate_physics.bat` remains byte-exact across
44,401 rows and `tools\validate_perf.bat` passes. Format, strict complexity at
the ratified 400/6 triggers (40/40), aggregates (85/85), the sole unrelated
ruled extraction scar, and all 12-or-more-parameter signature rulings pass.
The first performance scan caught the generic `pairSeen.resize` spelling;
using the bounded-owner `ResetDefault` API resolved the local policy issue
without a semantic or capacity change. No baseline, golden, config, schema,
allowlist, or committed runtime artifact changed.

## Dependencies

- `broadphase-canonical-order-guard` should close first. Both plans touch
  `SpatialGrid.h`/`.cpp`, and the guard is two tasks against this plan's four.

## Acceptance

- `sizeof( SpatialGrid )` falls to the fixed hash-table core; scene-sized
  storage is committed at `SceneLoad` from the loaded body count.
- Committed broadphase bytes for the 300-body default scene drop by at least an
  order of magnitude. Report the exact measured figure, not a target.
- Physics output is byte-exact across every committed baseline. No refresh.
- Every new reserved owner appears in `CollectDebugAndBroadphaseMemoryBytes`
  and in the Memory tab with an accurate capacity reason and high-water.
- Exhausting any converted list fails loud with owner, capacity, high-water, and
  phase — proved by a focused test, not asserted.
- `SpatialGrid` acquires no replay growth privilege and does not appear in the
  replay reserve inventory.
- `validate_perf` shows no regression. Any improvement is reported as measured.

## Validation

- Iteration: focused Profile build, `TestSpatialGrid`, `TestReserveAllocator`,
  `TestDeterminism`.
- BC1-BC2: `tools\validate_physics.bat` (byte-exact) and
  `tools\validate_perf.bat` — mapped: `SpatialGrid*` requires both.
- BC3: `tools\validate_all_cpu_tests.bat`,
  `python tools\check_allocation_policy.py --repo .`,
  `tools\validate_physics_deep.bat`, `tools\validate_perf.bat`, and
  `tools\validate_full.bat`.

## Comment-Audit Checklist

- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezTests/TestSpatialGrid.cpp`
- [ ] `SkullbonezTests/TestReserveAllocator.cpp`
- [x] `SkullbonezTests/TestRuntimeContracts.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`

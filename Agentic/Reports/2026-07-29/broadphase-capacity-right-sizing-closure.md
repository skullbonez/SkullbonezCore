# Broadphase Capacity Right-Sizing Closure Evidence

Date: 2026-07-29
Plan: archived under ledger rule 4 after BC0-BC3 closure
Branch: `nightrunner-29th-JUL-26`
Implementation range: `5470403b` through the closure commit

## Outcome

`SpatialGrid` now keeps only its fixed hash topology and control objects inline.
All nine scene-derived or bounded-work arrays use registered
`PhysicsFixedList` backing reserved during `SceneLoad`. The 300-body acceptance
scene falls from 8,535,792 bytes to 839,264 bytes per grid: 7,696,528 bytes
saved, a 90.167708% reduction, and 10.170568 times smaller. A process holding
the live and Replay-prediction grids falls from 17,071,584 to 1,678,528 bytes
at that capacity.

This is a footprint result, not a speed claim. The broadphase already cleared
only its live pair-dedup prefix. Final performance validation reports no
regression and lower measured process memory, but closure does not attribute a
speed improvement to the storage migration.

## Exact Layout And Commitment

The post-change layout harness measures the same result in Debug, Profile, and
Release:

- `sizeof( SpatialGrid ) = 623,256` bytes
- `alignof( SpatialGrid ) = 8`
- all three logs have SHA-256
  `85AE9A2C0EBCCA62864A858E7066D8C1EEDA70C26C5C0F1A746B062CF4C705BA`

The ignored measurement logs are:

- `TestOutput/bc3_spatial_grid_layout_debug.log`
- `TestOutput/bc3_spatial_grid_layout_profile.log`
- `TestOutput/bc3_spatial_grid_layout_release.log`

For admitted body capacity `B`, registered backing is:

```text
(8B + 32) * 40
+ 4096 * 20
+ B * 32
+ ceil((B * (B - 1) / 2) / 64) * 8
+ B * 4
+ (4B) * 8
+ (4B) * 4
+ (4B) * 4
+ B * 4
```

The fixed 32-row persistent-entry spill is corpus-derived compatibility
headroom. Deep-scene discovery measured a maximum excess of 19 rows beyond the
ordinary `8 * B` derivation: the accepted one-body oversized-shape case reaches
27 rows. The original four-row proposal failed loudly at requested row 21 with
runtime capacity 20. A temporary discovery reserve proved the corpus maximum;
the final power-of-two spill covers it without restoring the retired 4,096-row
blanket. This is not claimed as a universal per-shape bound.

| Admitted bodies | Old one grid | Final backing | Final one grid | Final two grids | Saved per grid | Reduction | Smaller |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 300 | 8,535,792 | 216,008 | 839,264 | 1,678,528 | 7,696,528 | 90.167708% | 10.170568x |
| 4,000 | 8,535,792 | 2,778,952 | 3,402,208 | 6,804,416 | 5,133,584 | 60.141859% | 2.508898x |
| 8,192 | 8,535,792 | 7,750,400 | 8,373,656 | 16,747,312 | 162,136 | 1.899484% | 1.019363x |

## Owner And Telemetry Proof

The nine registered owners are:

1. `SpatialGrid.entries`
2. `SpatialGrid.pairSeen`
3. `SpatialGrid.bodyMemberships`
4. `SpatialGrid.candidatePairHeads`
5. `SpatialGrid.candidatePairNodes`
6. `SpatialGrid.candidatePairSortKeys`
7. `SpatialGrid.candidatePairSortScratch`
8. `SpatialGrid.cellObjectSeen`
9. `SpatialGrid.overlayEntries`

Body-indexed stores reserve exactly `B`; candidate staging reserves `4B`; pair
dedup reserves the exact triangular identity word count; persistent entries
reserve `8B + 32`; the swept overlay retains its accurately labelled fixed
4,096-row transient-work ceiling. Runtime telemetry shows `scene_load` as the
initial and last growth phase for all nine owners, with `growth_limit=0`,
`replay_grows=0`, and `failed_grows=0`.

`PhysicsBroadphaseStage::CollectDynamicMemoryBytes` now includes the grid's
registered backing exactly once in the owning `PhysicsWorld` total.
`CollectDebugAndBroadphaseMemoryBytes` remains an informational subset and no
longer double-counts that backing locally. A focused exact test pins both
relationships.

The Replay reserve inventory remains exactly:

- `recorder_samples`
- `physics_solver_snapshot`
- `replay_prediction_working_set`

No `SpatialGrid` owner has Replay growth privilege. Prediction can allocate its
grid only while its existing outer prediction working-set owner is in the
allowed phase; the grid's own registered stores remain SceneLoad-only.

## Failure And Lifetime Proof

Focused coverage proves exact owner names, reasons, capacities, retained
backing and high-water, grow-only live admission, dense-prefix shrink without
allocation, deterministic free-list reuse, and lane-F exhaustion.

The persistent-entry fatal case reserves one body at capacity 40. A radius
`1.6f` body centered at `0.25f` spans a deterministic 4-by-4-by-4 cell range
and fails on requested row 41, reporting owner, runtime and compile capacities,
high-water, and Physics phase. Startup reservation denial reports requested
capacity 40. The overlay probe likewise fails at requested row 4,097 against
its fixed 4,096-row ceiling.

Additional two-to-four-body admission preserves the existing 0-1 pair and then
admits a new 2-3 pair. `Clear`, `SetCellSize`, and `BeginFrame` retain backing
and high-water and never reserve or decommit.

## Review Findings And Resolution

Independent review first found one closure blocker: grid backing appeared in
the debug subset but not in the owning broadphase dynamic total, so
`PhysicsWorld` and the Memory tab under-reported all nine allocations. The
owning total and exact-once test above resolve it.

The first deep gate then exposed the four-row persistent spill as insufficient.
The final 32-row, measured-excess design and deterministic 40/41 fatal proof
resolve it. A fresh independent read of the final source and tests returned
`ZERO SOURCE/TEST BLOCKERS`.

The reviewer also noted redundant hot-path initialization/clear opportunities.
They do not change allocation, ownership, output, or measured performance and
are not closure blockers; no speculative behavior change was made in this
footprint plan.

## Validation

- `tools\validate_all_cpu_tests.bat`: all six lanes pass; 447/447 doctest
  cases and 2,421,986/2,421,986 assertions.
- `tools\validate_physics_deep.bat`: passes on the final 32-row spill and
  remains byte-exact.
- `tools\validate_perf.bat`: both lanes pass without baseline changes. DX12
  process memory is 5.52-6.25 MiB lower and the focused Physics process is
  5.24-5.74 MiB lower across the reported samples.
- `python tools/check_allocation_policy.py --repo .`: 463 files scanned,
  36 allowlisted direct-heap findings, 85 dynamic-STL-member findings, 625
  STL-growth findings, and zero allowlist errors.
- `tools\validate_format.bat`: 571 source files and 317 headers pass.
- Ownership inventories pass: 40/40 complexity rulings at the owner-ratified
  400-line/depth-6 triggers, 85/85 aggregate rulings, the one unrelated ruled
  extraction scar, and every 12-or-more-parameter signature ruled.
- `tools\validate_full.bat`: the default gate passes; 447/447 doctest cases,
  2,421,986/2,421,986 assertions, accepted DX12 baseline comparisons, and a
  byte-exact 44,401-line Physics regression.

No physics baseline, golden, config, schema, allocation allowlist, or committed
runtime artifact changed.

## Comment Audit

All nine source-bearing files in plan scope were read against
`Agentic/Reference/comment-style-guide.md`; none are deferred:

- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezTests/TestSpatialGrid.cpp`
- [x] `SkullbonezTests/TestReserveAllocator.cpp`
- [x] `SkullbonezTests/TestRuntimeContracts.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`

Checklist result: 9 checked, 0 deferred, 0 unchecked.

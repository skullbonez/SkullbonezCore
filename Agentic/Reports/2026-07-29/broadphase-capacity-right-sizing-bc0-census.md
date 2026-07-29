# Broadphase Capacity Right-Sizing BC0 Census

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Plan: archived under ledger rule 4; closure evidence is
`Agentic/Reports/2026-07-29/broadphase-capacity-right-sizing-closure.md`
Scope: documentation and measurement only

## Result

`SpatialGrid` has the same layout in Debug, Profile, and Release:

- `sizeof( SpatialGrid ) = 8,535,792` bytes (`8.1403656006 MiB`)
- `alignof( SpatialGrid ) = 8`
- one live engine and one Replay prediction engine therefore carry
  `17,071,584` inline bytes (`16.2807312012 MiB`)
- arrays targeted for scene-load registered storage account for `7,913,120`
  bytes (`92.7052%`); this physical migration set includes the fixed-capacity
  transient overlay pool described below
- the fixed topology/state core accounts for `622,672` bytes
- `pairSeen` plus `entries` account for `6,979,232` bytes (`81.7643%`)

There is no configuration-dependent padding or debug-only member in
`SpatialGrid`.

## Measurement Method

An ignored temporary translation unit under `TestOutput/` exposed private
members only inside the measurement harness. It printed every member's
`sizeof` and `offsetof`, all nested record sizes, `sizeof( SpatialGrid )`, and
`alignof( SpatialGrid )`. It was compiled directly with the installed MSVC
toolchain and the exact first-party preprocessor/runtime shape of each
`SKULLBONEZ_CORE.vcxproj` configuration:

```powershell
cl /std:c++20 /EHsc- /GR- /W4 /WX /I. /Od /MTd /D_HAS_EXCEPTIONS=0 /DJSON_NOEXCEPTION /D_DEBUG /D_WINDOWS /D_HAS_STD_BYTE=0 TestOutput\bc0_spatial_grid_layout.cpp
cl /std:c++20 /EHsc- /GR- /W4 /WX /I. /O2 /MT /D_HAS_EXCEPTIONS=0 /DJSON_NOEXCEPTION /DNDEBUG /D_WINDOWS /DSKULLBONEZ_PROFILE_ENABLED /DSKULLBONEZ_PLATFORM_PROFILER_PIX /DUSE_PIX /DSKULLBONEZ_CAPTURE_EXECUTION /D_HAS_STD_BYTE=0 TestOutput\bc0_spatial_grid_layout.cpp
cl /std:c++20 /EHsc- /GR- /W4 /WX /I. /O2 /MT /D_HAS_EXCEPTIONS=0 /DJSON_NOEXCEPTION /DNDEBUG /D_WINDOWS /DSKULLBONEZ_CAPTURE_EXECUTION /D_HAS_STD_BYTE=0 TestOutput\bc0_spatial_grid_layout.cpp
```

All three compiles succeeded and all three executables printed the same sizes
and offsets. The temporary harness and binaries are not repository artifacts.

## Exact Layout

| Nested type | Bytes |
|---|---:|
| `CellRange` | 24 |
| `Entry` | 40 |
| `SweptOverlayEntry` | 20 |
| `Bucket` | 64 |
| `BodyMembership` | 32 |
| `CandidatePairNode` | 8 |
| `MaintenanceStats` | 28 |
| representative `PhysicsFixedList` control object | 64 |

| Member | Offset | Bytes | Classification |
|---|---:|---:|---|
| `cellSize` | 0 | 4 | fixed state |
| `inverseCellSize` | 4 | 4 | fixed state |
| `overlayGeneration` | 8 | 4 | fixed state |
| `pairSourceGeneration` | 12 | 4 | fixed state |
| `freeBucketHead` | 16 | 4 | fixed state |
| `freeEntryHead` | 20 | 4 | fixed state |
| `persistentEntryCount` | 24 | 4 | fixed state |
| `persistentEntryHighWater` | 28 | 4 | fixed state |
| `objectCount` | 32 | 4 | fixed state |
| `activeBucketCount` | 36 | 4 | fixed state |
| `overlayEntryCount` | 40 | 4 | fixed state |
| `overlayActiveBucketCount` | 44 | 4 | fixed state |
| `buckets` | 48 | 524,288 | fixed 8,192-row hash topology |
| `bucketHashHeads` | 524,336 | 32,768 | fixed 8,192-row hash topology |
| `activeBuckets` | 557,104 | 32,768 | fixed 8,192-row hash topology |
| `overlayActiveBuckets` | 589,872 | 32,768 | fixed 8,192-row hash topology |
| `entries` | 622,640 | 2,785,440 | scene-derived persistent-cell workload |
| `overlayEntries` | 3,408,080 | 81,920 | fixed transient-work ceiling, not hash topology |
| `bodyMemberships` | 3,490,000 | 262,144 | exact scene body count |
| `pairSeen` | 3,752,144 | 4,193,792 | triangular scene body-pair bit count |
| `candidatePairHeads` | 7,945,936 | 32,768 | exact scene body count |
| `candidatePairNodes` | 7,978,704 | 262,144 | candidate-pair capacity |
| `candidatePairSortKeys` | 8,240,848 | 131,072 | candidate-pair capacity |
| `candidatePairSortScratch` | 8,371,920 | 131,072 | candidate-pair capacity |
| `cellObjectGeneration` | 8,502,992 | 4 | fixed state |
| `cellObjectSeen` | 8,502,996 | 32,768 | exact scene body count |
| `maintenanceStats` | 8,535,764 | 28 | fixed state |

The measured last byte is 8,535,791, so the record has no unaccounted tail
padding.

## Current Committed Bytes By Scene Size

Today every row is inline. Scene admission changes live prefixes but not
committed broadphase bytes:

| Admitted bodies | One `SpatialGrid` | Live + Replay prediction grids |
|---:|---:|---:|
| 300 | 8,535,792 | 17,071,584 |
| 4,000 | 8,535,792 | 17,071,584 |
| 8,192 | 8,535,792 | 17,071,584 |

Exact BC1/BC2 sizing inputs:

| Bodies | Pair identities | `pairSeen` words / bytes | persistent entry base `8B + 4` / bytes | candidate rows | membership bytes |
|---:|---:|---:|---:|---:|---:|
| 300 | 44,850 | 701 / 5,608 | 2,404 / 96,160 | 1,200 | 9,600 |
| 4,000 | 7,998,000 | 124,969 / 999,752 | 32,004 / 1,280,160 | 16,000 | 128,000 |
| 8,192 | 33,550,336 | 524,224 / 4,193,792 | 65,540 / 2,621,600 | 32,768 | 262,144 |

`entries` currently adds `MAX_SWEPT_CELL_ENTRIES` (4,096 rows) to the
body-derived persistent-entry extent even though swept occupancy uses the
separate `overlayEntries` pool. BC1 must preserve the compile-time ceiling
while using measured high-water/focused exhaustion evidence before deciding
whether that historical 4,096-row persistent headroom belongs in the runtime
reservation.

`overlayEntries` is not genuinely body-count-sized under the current contract.
One body may traverse up to 2,048 swept cells and the global lane-F ceiling is
4,096 rows, so a smaller body-count multiplier would change accepted work. BC2
should still move it to registered storage as the plan requires, but reserve
the fixed 4,096-row transient-work ceiling unless a separate proof establishes
a smaller formula. Its capacity reason must say that rather than claiming
exact scene-body sizing.

## Reservation Owner And Order

The current scene-load path is:

1. `SceneAuthoredSetup::SetUpSceneEntities` counts bodies/shapes/joints and
   calls `SceneWorld::CommitPhysicsSceneCapacity` before entity creation.
2. `SceneWorld::CommitPhysicsSceneCapacity` validates admission, opens
   `RuntimeAllocationPhase::SceneLoad`, and calls
   `PhysicsEngine::ReserveAuthoredBodyCapacity`.
3. `PhysicsEngine::ReserveAuthoredBodyCapacity` reserves authored descriptors,
   body store, collider store, collider shapes, buoyancy, `PhysicsWorld`
   scratch/stages, fixed-tree wake rows, then broadphase-query scratch.
4. `PhysicsWorld::ReserveBodyScratchCapacity` reserves time remaining, point
   joints, force stage, external-force stage, `PhysicsBroadphaseStage`,
   narrowphase, contact solver, step diagnostics, sleep controller, then
   terrain.
5. `PhysicsBroadphaseStage::ReserveSceneCapacity` currently reserves only
   candidate pairs, collision-cell keys, and Debug oracle lists.

BC1 should add `m_spatialGrid.ReserveSceneCapacity(bodyCapacity)` as the first
concrete reserve inside `PhysicsBroadphaseStage::ReserveSceneCapacity`, before
pair-output buffers. This keeps every new owner under the existing SceneLoad
scope and makes backing available before entity creation. The
additional-capacity path re-enters the same chain under SceneLoad; there is no
Replay growth grant.

## `SetCellSize` And `BeginFrame`

- Construction calls `Clear`, then `SetCellSize`; changing the initial
  placeholder performs a second cold `Clear`.
- `SetCellSize` returns when unchanged. When changed, it updates the reciprocal
  and cold-clears because every integer cell range is invalid.
- `PhysicsBroadphaseStage` selects the scene-derived cell size, calls
  `SetCellSize`, then `BeginFrame(modelCount)`.
- `BeginFrame` resets the swept overlay, advances pair-source generation,
  removes memberships in a truncated dense suffix, and assigns the current
  object count. It does not reserve or grow.

After conversion, `Clear` and `SetCellSize` must reset only live/reserved rows
while retaining `PhysicsFixedList` backing and allocator ownership.
`BeginFrame` must remain allocation-free and may only shrink/reset live
prefixes. Reservation precedes both entity creation and the first
cell-size/frame transition; neither cold clear nor frame begin may acquire
Replay growth authority.

## Validation

BC0 changed documentation only. No repository validation gate was required.
The three measurement compiles used `/W4 /WX` and succeeded. Documentation
paths and whitespace were checked after report and ledger updates. No baseline,
golden, config, schema, allowlist, production source, or committed runtime
artifact changed.

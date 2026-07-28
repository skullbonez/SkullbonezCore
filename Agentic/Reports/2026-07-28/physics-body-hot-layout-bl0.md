# Physics Body Hot Layout BL0 Evidence

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Phase: BL0 — current layout and consumer census
Storage ruling: Retain structure of arrays (SoA)

## Result

The current body store is an allocation-separated SoA consumed through scalar
indexing. It is not an eight-body vectorized layout. `PhysicsFixedList` already
aligns each allocated payload to at least 32 bytes; the `alignas(32)` annotations
on `PhysicsBodyStore` members align only the list control blocks and add
intra-object padding. No production body-stage kernel uses vector intrinsics
over the hot streams.

The owner-directed five-scene measurement passes from a clean detached worktree
at executable-source commit `fa6ffd69`. BL1 must use the result below to select
only internal SoA work.

## Stream And Allocation Census

`PhysicsBodyStore` owns 18 `float` streams and two `uint8_t` flag streams. Each
`PhysicsFixedList` owns a separate allocation, so the hot layout has 20 payload
allocations.

| Group | Streams | Bytes per live body |
|---|---:|---:|
| Position X/Y/Z | 3 × `float` | 12 |
| Orientation X/Y/Z/W | 4 × `float` | 16 |
| Linear velocity X/Y/Z | 3 × `float` | 12 |
| Angular velocity X/Y/Z | 3 × `float` | 12 |
| Inverse mass | 1 × `float` | 4 |
| Inverse inertia X/Y/Z | 3 × `float` | 12 |
| Bounding radius | 1 × `float` | 4 |
| Fixed and awake | 2 × `uint8_t` | 2 |
| **Total** | **20 streams** | **74 bytes** |

The live-prefix payload lower bounds for the approved matrix are:

| Scene body count | Hot payload lower bound |
|---:|---:|
| 200 | 14,800 bytes |
| 520 | 38,480 bytes |
| 1,000 | 74,000 bytes |
| 2,000 | 148,000 bytes |
| 5,000 | 370,000 bytes |

These figures exclude `PhysicsFixedList` control blocks, allocator metadata,
capacity slack, cold records, colliders, contacts, candidate pairs, and stage
scratch.

## Alignment And Allocation Proof

- `PhysicsFixedList::STORAGE_ALIGNMENT` is `max(alignof(T), 32)`.
- `AllocateStorage` passes that alignment to aligned `operator new[]`.
- Every hot member is a distinct `PhysicsFixedList`, and
  `PhysicsBodyStore::ReserveCapacity` reserves each list separately.
- Member `alignas(32)` therefore cannot establish payload alignment. It aligns
  the small list owner object embedded in `PhysicsBodyStore`; the allocation
  routine independently establishes the useful array-start guarantee.

## Consumer Census

The views expose component spans. Scalar helpers reconstruct `Vector3`,
`Quaternion`, or complete `PhysicsBodyHotState` values by indexing the relevant
spans at one body row. Full and partial consumers include:

- force and integration stages;
- broadphase, narrowphase, terrain, and persistent-contact work;
- sleep/wake and island propagation;
- buoyancy, ragdoll, diagnostics, engine queries, and refresh/state transfer.

Some stages iterate consecutive body indices, while contact, query, and
diagnostic paths use scattered indices. All observed production consumers issue
ordinary scalar span indexing or call the scalar reconstruction helpers.

The only intrinsic search hit under `SkullbonezSource/Physics` is
`BoundingSphere.cpp`, where a four-lane vector intrinsic scales four matrix
elements. It does not consume `PhysicsBodyStore` hot streams. There is no
current eight-body load, explicit gather, or other vector-intrinsic consumer
that justifies the former header claim.

## Performance Witness

The clean final-source witness uses `tools\validate_perf.bat`, whose Physics
matrix is 200, 520, 1,000, 2,000, and sleepy-5,000 bodies. Existing settings are
retained:

- `worker_threads = -1`
- `physics_parallel = 1`
- parallel force, mutual-gravity, terrain-detect, and integrate lanes enabled
- tornado-field and narrowphase parallel lanes disabled

The final clean run completed in 154.8 seconds on an AMD Ryzen Threadripper
3970X with 64 logical processors and Windows `10.0.26200`. Every scene captured
1,140 frames.

| Scene | Total / awake bodies | `Frame/Physics` avg | P50 | P99 | Estimated logical hot bytes/body-step | Estimated logical hot bytes/step |
|---|---:|---:|---:|---:|---:|---:|
| 200 | 200 / 200 | 0.1211 ms | 0.1187 ms | 0.2020 ms | 245.0 | 49,000 |
| 520 | 520 / 520 | 0.8219 ms | 0.8179 ms | 1.0453 ms | 245.0 | 127,400 |
| 1,000 | 1,000 / 1,000 | 1.1624 ms | 1.1580 ms | 1.5884 ms | 245.0 | 245,000 |
| 2,000 | 2,000 / 2,000 | 2.0836 ms | 2.0764 ms | 3.1065 ms | 245.0 | 490,000 |
| sleepy-5,000 | 5,000 / 1,000 | 2.4299 ms | 1.3354 ms | 26.9538 ms | 82.6 | 413,000 |

The byte counter is source-modelled logical traffic: 42 bytes for the
all-body pass plus awake bookkeeping, force, and integration traffic. It
deliberately excludes cold records, contacts, allocator metadata, cache-line
amplification, and instruction fetch. Combined with the 14.8–370.0 KB live hot
payload census, it is the available cache-footprint/bandwidth witness; it is not
a hardware cache-miss counter.

`tools\validate_perf.bat` passed its build, allocation guard, structural
selected-path proof, DX12 comparison, and five Physics captures. The worktree
remained clean, and no committed baseline or artifact was refreshed.

## BL1 Decision Boundary

BL1 may select only work inside the retained SoA design. Candidate changes must
be supported by the measurements above and may include removal of inert
control-block alignment, contiguous backing for the existing streams, or a real
bulk consumer. AoS prototyping and selection are out of scope by owner ruling.

# Collision Hull Shape Instancing

Date: 2026-07-29
Owner: skullbonez
State: In progress
Ledger tasks: 4 (HS0-HS3)
Branch: `nightrunner-29th-JUL-26`
PR: TBD

## Goal

Store one copy of each distinct authored convex hull per scene instead of one
copy per collider, so narrowphase reads a small shared hot set rather than a
duplicated array.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f`.

`ColliderStore::m_hullShapes` is
`PhysicsFixedList<ConvexHullShape, MAX_SCENE_OBJECTS>` and receives one full
`ConvexHullShape` value per hull collider
(`SkullbonezSource/Physics/ColliderStore.h:236`, `AppendShape`).

`ConvexHullShape` is entirely fixed arrays
(`SkullbonezSource/Physics/ConvexHullShape.h:70-83`):

| Member | Extent | Bytes |
|---|---:|---:|
| `m_vertices` | 64 | 768 |
| `m_faces` | 96 | 1,920 |
| `m_edges` | 160 | 1,280 |
| `m_faceIndices` | 1,536 | 3,072 |
| scalars, name, pose | — | ~120 |
| **total** | | **~7,160** |

Duplication in committed content, corrected by HS0:

- 35 authored `.hull` assets. The prior 33 figure counted raw direct-scene
  token spellings; those normalize to 22 paths. Expanding every committed
  `assetInstances[]` row yields 557 hull colliders and 25 distinct used paths
  across all 145 committed scenes. Exact per-scene evidence and the correction
  method are in
  `Agentic/Reports/2026-07-29/collision-hull-shape-instancing-hs0-census.md`.
- `SkullbonezData/scenes/asd.scene.json` has 51 hull colliders drawn from a
  handful of distinct hulls — `tree_trunk_faceted` and `cedar_tier_top` appear
  8 times each.
- `convex_hull_stress_sleep.scene.json` 42, `convex_hull_stacking.scene.json` 34.

So the current committed worst case stores ~365 KB of hull payload where ~45 KB
of distinct geometry exists, and the prediction engine's
`ColliderStore::CloneReplayPredictionStorageFrom` copies all of it again. The
factor is ~5-8× today and unbounded for asset-library scenes: the engine's own
`assetlib.*` registration path exists precisely to place many instances of one
recipe.

The cost that matters is not the megabytes. It is that `ObjectContactManifold`
walks hull faces and edges per candidate pair, and identical geometry occupying
distinct cache lines defeats reuse across every pair sharing a hull.

## Why This Is Cheap To Do

The seam already exists. `CollisionShapeReference` is a non-owning typed view
carrying a per-kind storage index, and `ColliderStore::RebindShapeReferences`
already re-points every collider after backing relocation
(`SkullbonezSource/Physics/CollisionShape.h:72-80`,
`ColliderStore.h:199-203`). Nothing in narrowphase assumes a one-to-one
collider-to-hull mapping. The change is confined to `AppendShape`,
`ReplaceShape`, `RemoveShape`, and the reservation path.

## Design Constraints

- **Byte-exact.** Sharing a hull must not change one physics bit. The hull
  payload is immutable after load-time validation
  (`ConvexHullShape.h` invariant), so the shared read is the same read.
- **Deterministic identity.** Dedup keys on the authored hull identity resolved
  at the cold load boundary, never on a float comparison of vertex data and
  never on pointer identity. Two colliders share storage only when they name the
  same authored asset.
- **No refcount in the hot path.** Release-on-destroy uses the existing cold
  topology boundary. If a hull's last collider is destroyed mid-scene, either
  retain the entry for scene lifetime or compact at the same cold boundary that
  already rebinds references — decide in HS0 and state it as an invariant.
- **Reservation must stay honest.** `ReserveShapeCapacity` currently takes an
  authored hull count. After dedup it takes a distinct-hull count, and
  `PhysicsCapacityReason::HullColliders` needs its reason string corrected to
  say what it now measures.

## HS0 Decisions

- **Identity:** normalized resolved authored path plus the exact validated
  IEEE-754 bits of the cumulative X/Y/Z authored scale. Unit-scale scene rows
  share. Editor-scaled copies carry their scale variant; callers without a
  provable path-plus-scale identity remain explicitly non-shareable rather than
  aliasing on path alone.
- **Release:** retain identity rows until scene clear. Mid-scene last-user
  destruction does not refcount, erase, or compact shared hull storage; later
  recreation reuses the stable index. Unique editor variants therefore consume
  monotonic scene-lifetime capacity and fail loudly through the existing fixed
  capacity policy if exhausted.
- **Evidence:** the complete lifecycle, mutation audit, and 145-scene census are
  recorded in
  `Agentic/Reports/2026-07-29/collision-hull-shape-instancing-hs0-census.md`.

## Non-Goals

- Sharing sphere or box payloads. Both are small; duplication there is not a
  measurable cost and the indirection would not pay.
- Cross-scene or process-lifetime hull caching. Scene lifetime is the boundary.
- Changing the `.hull` format, the bake tool, or hull topology limits.

## Ledger

- [x] HS0 — Census and decision. Record distinct-versus-total hull counts for
  every committed scene, the exact call paths that create, replace, and destroy
  hull shape rows, and whether any consumer mutates a hull after load. Decide
  and record the identity key and the mid-scene release policy. Evidence:
  `Agentic/Reports/2026-07-29/collision-hull-shape-instancing-hs0-census.md`.
- [ ] HS1 — Introduce the deduplicated hull store: identity-keyed append that
  returns an existing storage index on a repeat, corrected reservation input and
  capacity reason, and rebind coverage for the shared indices. Byte-exact
  required.
- [ ] HS2 — Extend `ColliderStore` and `TestPhysicsHandles` coverage: repeat
  append shares one row, destroy/create round-trips keep every surviving
  reference valid, replay-prediction clone preserves sharing, and a scene with
  mixed shared and unique hulls loads to the expected distinct count.
- [ ] HS3 — Closure. Measure the before/after committed-scene footprint and the
  narrowphase marker on the hull-heavy scenes, audit touched comments, pass
  independent review, and run every mapped gate.

## Acceptance

- Loading `asd.scene.json`, `convex_hull_stress_sleep.scene.json`, and
  `convex_hull_stacking.scene.json` commits hull storage proportional to
  distinct hulls, not collider count. Report the exact before/after bytes.
- Physics output is byte-exact against committed baselines across all three
  scenes. No baseline is refreshed by this plan.
- `validate_perf` shows no regression, and the narrowphase marker on hull-heavy
  scenes is recorded before and after as evidence rather than assumed to improve.
- No refcount, lookup, or indirection is added inside the narrowphase or solver
  loops. Sharing is resolved entirely at the cold collider-creation boundary.
- `PhysicsCapacityReason::HullColliders` states what it actually measures.
- The replay-prediction clone shares the same way; prediction storage does not
  silently re-expand the duplication.

## Validation

- Iteration: focused Profile build, `TestConvexHull`, `TestPhysicsHandles`,
  `TestObjectContactManifold`.
- HS1-HS2: `tools\validate_physics.bat` (byte-exact) and
  `tools\validate_tests.bat`.
- HS3: `tools\validate_physics_deep.bat`, `tools\validate_perf.bat`,
  `tools\validate_replay_visual_fidelity.bat` (prediction clone path), and
  `tools\validate_full.bat`.

## Comment-Audit Checklist

- [ ] `SkullbonezSource/Physics/ColliderStore.h`
- [ ] `SkullbonezSource/Physics/ColliderStore.cpp`
- [ ] `SkullbonezSource/Physics/CollisionShape.h`
- [ ] `SkullbonezSource/Physics/ConvexHullShape.h`
- [ ] `SkullbonezSource/Physics/PhysicsFixedList.h`
- [ ] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [ ] `SkullbonezTests/TestPhysicsHandles.cpp`
- [ ] `SkullbonezTests/TestConvexHull.cpp`

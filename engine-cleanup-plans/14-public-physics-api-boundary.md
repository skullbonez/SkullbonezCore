# 14 - Public Physics API Boundary

Date: 2026-07-09
Status: In Progress
Priority: P1
Owner: Physics / Runtime API
Source issue: FAC-005 from `13-facade-retirement.md`

## Owner Decision - 2026-07-09

Create and execute a dedicated public physics API boundary cleanup. Public
physics API headers such as `PhysicsApi.h` and `PhysicsEngine.h` must expose no
`GameModel`, no raw dense `modelIndex` authority, and no solver container types.
Treat this as a deliberate physics identity/authority cleanup, not a rename or
vocabulary pass. Validate with `tools\validate_physics.bat` when implementation
reaches the PR gate.

## Problem

FAC-005 remains the only open structural facade item after Plans 01 and 10
closed the main runtime/rendering facade surfaces. The public physics API still
needs a type-level boundary pass so callers cannot depend on gameplay model
objects, dense solver indices, or internal solver containers as public
authority.

The target is structural:

- Public physics API signatures do not name `GameModel`.
- Public physics API signatures do not expose raw dense `modelIndex` authority.
- Public physics API signatures do not expose solver container types.
- Any surviving identity is a stable physics/domain identity owned by the
  physics boundary, not a raw storage slot.

## Scope

Initial headers in scope:

- `SkullbonezSource/Physics/PhysicsApi.h`
- `SkullbonezSource/Physics/PhysicsEngine.h`

Follow references from those headers only as needed to remove public boundary
leaks. Do not restart the wider GameModel authority campaign from scratch.

## Step-by-step implementation

- [x] **0.1** Inventory public signatures in `PhysicsApi.h` and
  `PhysicsEngine.h` that mention `GameModel`, dense `modelIndex`, or solver
  container types. Record the exact signatures and proposed replacement
  identity/authority shape here. No code change; documentation-only.
- [x] **1.1** Introduce or reuse the narrow public physics identity types needed
  to replace raw dense model-index authority. Keep the change scoped to the
  public boundary. Gate: `validate_physics`. Commit.
- [x] **1.2** Remove `GameModel` from public physics API signatures and update
  callers to pass the new domain identity/context. Gate: `validate_physics`.
  Commit.
- [ ] **1.3** Remove solver container types from public physics API signatures
  and keep solver storage behind physics-owned APIs. Gate: `validate_physics`.
  Commit.
- [ ] **2.1** Reconcile FAC-005 acceptance in
  `13-facade-retirement.md` and this plan after the public header grep and
  physics gate pass. Commit.

## Step 0.1 Inventory - 2026-07-10

Inventory commands:

- `codegraph status .`
- `codegraph explore "PhysicsApi.h PhysicsEngine.h public physics API signatures GameModel modelIndex PhysicsBodyStore ColliderStore SpatialGrid PhysicsDebugContact PhysicsPipelineRecord PointJointConstraint ReplaySolverWorldSnapshot"`
- `rg -n "GameModel|modelIndex|modelCount|PhysicsBodyStore|ColliderStore|SpatialGrid|std::vector|PhysicsDebugContact|PhysicsPipelineRecord|PointJointConstraint|ReplaySolverWorldSnapshot|BodyStore\(|Colliders\(|GetSpatialGrid|GetCollision|GetSleep|GetPhysics|GetPointJoint" SkullbonezSource/Physics/PhysicsApi.h SkullbonezSource/Physics/PhysicsEngine.h`

Findings:

- `GameModel` has no matches in `PhysicsApi.h` or `PhysicsEngine.h`.
- Raw dense model-row authority remains in `PhysicsEngine.h` public methods.
- Solver/store/debug containers remain visible through `PhysicsEngine.h` public
  signatures.
- `PhysicsApi.h` has one public replay view field named `modelCount`. It also
  exposes store/debug implementation types through public-header includes,
  public struct fields, and private `PhysicsStandaloneWorld` storage. The
  private members are not public method signatures, but they keep concrete
  solver storage in the public API header and should be handled with the solver
  container slice.

### Raw Dense Row Authority

| Header | Exact current signature or field | Replacement shape |
|--------|----------------------------------|-------------------|
| `PhysicsEngine.h` | `bool TryGetAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& outDesc ) const;` | Prefer `PhysicsBodyHandle` as identity. If the caller is still authoring in model order, pass an explicit `ModelRowHint` as a hint only, with the physics boundary resolving or rejecting stale topology. |
| `PhysicsEngine.h` | `bool UpdateAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& desc, int expectedModelCount );` | `PhysicsBodyHandle` plus a typed topology/count guard such as `PhysicsBodyTopologyStamp` or `PhysicsAuthoredBodyCount`; a `ModelRowHint` may be carried only as a repairable hint. |
| `PhysicsEngine.h` | `void RefreshBodyFromDescriptor( const PhysicsBodyCreateDesc& desc, int modelIndex, int expectedModelCount );` | `PhysicsBodyHandle` targeted refresh, guarded by a typed body/topology count. Do not let a bare row choose the body. |
| `PhysicsEngine.h` | `void BeginCollisionVisualFrame( int modelCount );` | Typed count/view from the physics body boundary, e.g. `PhysicsBodyCount` or a collision-visual frame view; avoid the `modelCount` name because this is not model identity. |
| `PhysicsEngine.h` | `void CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;` | Replay snapshot request/view whose count is `bodyCount`/`PhysicsBodyCount`, preferably derived from the owned body store at the physics boundary. |
| `PhysicsEngine.h` | `bool RestoreReplaySolverSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot, int modelCount );` | Replay restore request/view with typed expected body count or topology stamp; the snapshot itself should not validate against a raw caller-supplied model row count. |
| `PhysicsApi.h` | `uint32_t modelCount = 0;` in `PhysicsReplaySolverSnapshotView` | Rename and type as `bodyCount`/`PhysicsBodyCount`, or fold the count into a replay snapshot request that is validated against physics-owned body topology. |

### Solver Container And Owning STL Signatures

| Header | Exact current signature or field | Replacement shape |
|--------|----------------------------------|-------------------|
| `PhysicsEngine.h` | `bool RefreshBodyStoreFromAuthoredDescriptors( const std::vector<uint32_t>& replayBodyIds, const std::vector<int>& fixedTreeReleaseRoots, const std::vector<const char*>& diagnosticNames );` | A borrowed `PhysicsAuthoredBodyRefreshView` with pointer/count spans. Use `PhysicsBodyHandle` or typed `ModelRowHint` entries for release roots instead of raw `int` rows. |
| `PhysicsEngine.h` | `void RefreshBodyStore( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );` | A borrowed descriptor span/view, e.g. `PhysicsBodyCreateDescView`, so public signatures do not require owning STL containers. |
| `PhysicsEngine.h` | `const std::vector<int>& GetFixedContactHighlightBodies() const;` | A public immutable handle view such as `PhysicsBodyHandleCollectionView`; presentation owners can resolve handles to row hints locally. |
| `PhysicsEngine.h` | `const PhysicsBodyStore& BodyStore() const;` | Narrow body query/view methods (`PhysicsBodyCollectionView`, handle lookup, or count-only queries) instead of returning the store. |
| `PhysicsEngine.h` | `const ColliderStore& Colliders() const;` | Narrow collider query/view methods (`PhysicsColliderCollectionView`, handle lookup) instead of returning the store. |
| `PhysicsEngine.h` | `const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;` | Public broadphase query APIs such as `PhysicsBroadphaseQueryResultView`; callers should not borrow the grid. |
| `PhysicsEngine.h` | `const std::vector<int64_t>& GetCollisionCellKeys() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data. |
| `PhysicsEngine.h` | `const std::vector<uint8_t>& GetCollisionVisualContacts() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data. |
| `PhysicsEngine.h` | `const std::vector<uint8_t>& GetSleepStates() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data. |
| `PhysicsEngine.h` | `const std::vector<int>& GetSleepIslandVisualIds() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data with a typed visual-island id. |
| `PhysicsEngine.h` | `const std::vector<uint8_t>& GetSleepSupportedStates() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data. |
| `PhysicsEngine.h` | `const std::vector<uint8_t>& GetSleepInhibitedStates() const;` | Move under `PhysicsDiagnosticsView`/`PhysicsDiagnosticsSnapshot` as pointer/count data. |
| `PhysicsEngine.h` | `const std::vector<PhysicsDebugContact>& GetPhysicsDebugContacts() const;` | Public diagnostic contact view/record span. Do not expose the owning vector or solver debug record container. |
| `PhysicsEngine.h` | `const std::vector<PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const;` | Public pipeline trace view/record span. Do not expose the owning vector or solver debug record container. |
| `PhysicsEngine.h` | `const std::vector<PointJointConstraint>& GetPointJointConstraints() const;` | `PhysicsPointJointCollectionView` or a dedicated constraint diagnostics view; do not return solver constraint storage. |
| `PhysicsApi.h` | `const PhysicsDebugContact* debugContacts = nullptr;` in `PhysicsDiagnosticsSnapshot` | Convert to public diagnostic contact view records or reuse `PhysicsContactCollectionView` if the existing public contact view is enough. |
| `PhysicsApi.h` | `const PhysicsPipelineRecord* pipelineRecords = nullptr;` in `PhysicsDiagnosticsSnapshot` | Convert to public pipeline trace records owned by the API boundary, or hide behind a diagnostics query that projects solver records. |
| `PhysicsApi.h` | `#include "ColliderStore.h"` and `#include "PhysicsBodyStore.h"` | Remove public-header dependency on concrete stores after `PhysicsStandaloneWorld` storage is moved behind an internal implementation owner. |
| `PhysicsApi.h` | `PhysicsBodyStore m_bodyStore;` and `ColliderStore m_colliderStore;` in private `PhysicsStandaloneWorld` storage | Move concrete store storage to an internal implementation header/type, preserving fixed/preallocated runtime behavior and exposing only public handles/views in `PhysicsApi.h`. |
| `PhysicsApi.h` | Private `std::vector<...>` caches/scratch arrays in `PhysicsStandaloneWorld` | Keep as implementation detail outside the public header or replace with fixed/preallocated internal storage if a runtime path depends on it. |

## Step 1.1 Implementation - 2026-07-10

`PhysicsHandles.h` now carries the public boundary vocabulary needed for later
signature replacements:

- `PhysicsBodyCount`
- `PhysicsColliderCount`
- `PhysicsAuthoredBodyCount`
- `ModelRowHint::IsValid()`

The count wrappers intentionally describe topology or view size only. They do
not identify bodies, colliders, or authoring rows; later source slices should
pair them with `PhysicsBodyHandle`, `PhysicsColliderHandle`, `PhysicsSceneObjectId`,
or `ModelRowHint` depending on whether the caller has identity or only a
repairable presentation-row hint.

Touched-source comment audit:

- Inspected `SkullbonezSource/Physics/PhysicsHandles.h`.
- Checklist path: not required for a touched-file pass.
- Checked count: 1 source-bearing file.
- Deferred count: 0.

Validation:

- `tools\validate_physics.bat` passed in 00:00:44.2320777.
- Debug and Profile builds completed with 0 warnings and 0 errors.
- `physics_regression_solver.csv` matched the committed baseline byte-for-byte
  at 20001 lines.
- Ignored log: `Agentic/Reports/validate_physics_plan14_identity_types_20260710.log`.

## Step 1.2 Implementation - 2026-07-10

Step 0.1 already proved there were no `GameModel` names in `PhysicsApi.h` or
`PhysicsEngine.h`, so this source slice removed the remaining raw dense row
authority from the public physics boundary:

- `PhysicsEngine` and `PhysicsScene` now take `ModelRowHint` for repairable
  authoring-row lookups instead of `int modelIndex`.
- Public body, collider, authored-body, collision-visual, and replay snapshot
  counts now use `PhysicsBodyCount`, `PhysicsColliderCount`, or
  `PhysicsAuthoredBodyCount`.
- `PhysicsReplaySolverSnapshotView` exposes `bodyCount` as a typed physics body
  count.
- Game-model, replay recorder/restore, replay prediction, and determinism-test
  callers convert signed scene/replay counts at their owner edge with the
  public helper functions in `PhysicsHandles.h`.

Structural proof:

- `rg -n "\bmodelIndex\b|\bmodelCount\b|\bexpectedModelCount\b|GameModel" SkullbonezSource/Physics/PhysicsApi.h SkullbonezSource/Physics/PhysicsEngine.h`
  returned no matches.

Touched-source comment audit:

- Inspected 11 source-bearing files:
  `GameModelCollection.cpp`, `PhysicsApi.h`, `PhysicsEngine.cpp`,
  `PhysicsEngine.h`, `PhysicsHandles.h`, `PhysicsScene.cpp`,
  `PhysicsScene.h`, `ReplayRecorder.cpp`, `ReplayRestoreService.h`,
  `RunReplayTools.cpp`, and `TestDeterminism.cpp`.
- Checklist path: not required for a touched-file pass.
- Checked count: 11 source-bearing files.
- Deferred count: 0.

Validation:

- Focused `tools\validate_build.bat Debug` passed in 00:00:14.9914381 with
  0 warnings and 0 errors.
- `tools\validate_physics.bat` passed in 00:00:43.4524087.
- Debug and Profile builds completed with 0 warnings and 0 errors.
- `physics_regression_solver.csv` matched the committed baseline byte-for-byte
  at 20001 lines.
- Ignored log: `Agentic/Reports/validate_physics_plan14_row_authority_20260710.log`.

## Validation

- Inventory/documentation-only steps: no repository validation required.
- Source/API implementation steps: `tools\validate_physics.bat`.
- If implementation also touches runtime frame ownership or broad runtime
  wiring, use `tools\validate_full.bat` instead of physics-only.

## Acceptance

- [ ] `PhysicsApi.h` exposes no `GameModel` in public signatures.
- [ ] `PhysicsEngine.h` exposes no `GameModel` in public signatures.
- [ ] Public physics API signatures expose no raw dense `modelIndex` authority.
- [ ] Public physics API signatures expose no solver container types.
- [ ] `tools\validate_physics.bat` passes after the final source slice.
- [ ] FAC-005 in `13-facade-retirement.md` is checked only after the structural
  header checks and physics validation pass.

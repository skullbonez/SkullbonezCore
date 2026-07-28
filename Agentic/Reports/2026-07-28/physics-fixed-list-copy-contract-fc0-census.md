# Physics Fixed List Copy Contract — FC0 Census

Date: 2026-07-28
Plan: `Agentic/Plans/TODO/physics-fixed-list-copy-contract.md`
Scope: read-only source census plus documentation; no Physics behavior changed

## Binding Owner Ruling

`PhysicsFixedList` must not expose a generic clone primitive. Any required
clone belongs to a concrete aggregate owner: `PhysicsWorld`,
`PhysicsBodyStore`, `ColliderStore`, or a prediction snapshot owner.

## Container Contract

`PhysicsFixedList` currently exposes all four value-transfer special members:

| Operation | Definition | Capacity behavior | Value behavior |
|---|---|---|---|
| copy construction | `PhysicsFixedList.h:103-108` | calls `Reserve(other.m_count)` | copies the live prefix |
| copy assignment | `PhysicsFixedList.h:110-123` | calls `Reserve(other.m_count)` before clearing | copies the live prefix |
| move construction | `PhysicsFixedList.h:125-134` | calls `Reserve(other.m_count)` | element-moves the live prefix, clears the source, then transfers the capacity-publisher token |
| move assignment | `PhysicsFixedList.h:136-159` | calls `Reserve(other.m_count)` | element-moves the live prefix and clears the source; it transfers the publisher token only for the same owner when the destination has none |

Move does **not** transfer `m_values` or its committed backing. It allocates or
reuses destination backing, moves each element, and leaves the source backing
allocated with count zero. Consequently copy and move have the same hidden
phase hazard.

`Reserve` permits growth only in `SceneLoad`, using the list's registered
Physics owner, or in `Replay` while an already-approved outer owner and granted
growth scope are active (`PhysicsFixedList.h:269-323`). A transfer can happen
without growth when the destination already has enough committed capacity, but
ordinary C++ syntax does not reveal that prerequisite.

## Production Transfer

There is one actual production value transfer:

| Site | Operation | Phase and allocator owner | Classification |
|---|---|---|---|
| `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:331-400` | `predictionEngine.ReserveSceneCapacityLike(liveEngine); predictionEngine = liveEngine;` at lines 386-387 | `RuntimeAllocationPhase::Replay`, `ReplayPredictionReserveOwner()`, and its granted `RuntimeReserveGrowthScope`; registered owner `replay_prediction_working_set`, 256 MiB hard cap | Required prediction isolation, but expressed through an implicit `PhysicsEngine` copy assignment |

The private engine is retained as
`ReplayPredictionIsolatedSimulation::predictionEngine`, a
`std::unique_ptr<PhysicsEngine>` (`ReplayPrediction.h:252-265`). It is reused,
not queued or returned. `ReserveSceneCapacityLike` commits the destination
capacity before assignment (`PhysicsEngine.cpp:368-373`), so the assignment
normally copies into existing backing under the already-active Replay scope.

The compiler-generated `PhysicsEngine` copy assignment reaches 98
`PhysicsFixedList` member paths:

| Owner surface | Direct or recursive lists | Special-member status |
|---|---:|---|
| `PhysicsEngine` | 3 direct lists | copy/move are compiler-generated |
| `PhysicsWorld`, including its eight concrete stages | 53 | copy/move are compiler-generated throughout |
| `PhysicsBodyStore` | 29 | copy/move are compiler-generated |
| `ColliderStore` | 12 | copy/move are custom |
| `BuoyancySystem` | 1 | copy/move are compiler-generated |
| **Total reached by `PhysicsEngine` assignment** | **98** | |

The 53 `PhysicsWorld` paths are two direct lists plus `PhysicsForceStage` 2,
`ExternalForceStage` 2, `PhysicsBroadphaseStage` 5,
`PhysicsNarrowphaseStage` 7, `PhysicsTerrainStage` 2,
`PhysicsContactSolverStage` 10 (including five side-effect lists),
`PhysicsSleepController` 20, and `PhysicsStepDiagnostics` 3.

`ColliderStore` is the exception to implicit owner transfer. Its custom copy
and move operations transfer all twelve lists and call
`RebindShapeReferences()` afterward (`ColliderStore.cpp:112-173`), because
copied `ColliderRecord` shape references must point into the destination's
per-kind shape lists.

After the assignment, prediction rebinds the profiler and runtime settings and
restores captured prediction body/solver values. The transfer is intentional
prediction isolation, not accidental copying. The generic copyability that
enables it remains accidental API surface.

## Test-Only Transfers

| Site | Operation | Phase/owner | Classification |
|---|---|---|---|
| `SkullbonezTests/TestPhysicsHandles.cpp:663-688` | `ColliderStore` copy construction at line 667 and move construction at line 682 | explicit `SceneLoad` scope; each destination list uses its registered Physics owner | Intentional custom-store deep-copy/rebind coverage |
| `SkullbonezTests/TestReserveAllocator.cpp:708-735` | direct list move assignment at line 727 | `SceneLoad`; same-name list owner | Publisher-token transfer coverage |
| `SkullbonezTests/TestReserveAllocator.cpp:825-854` | trivial copy/move construction at lines 833-834 and non-trivial copy/move construction at lines 846-847 | `SceneLoad`; test list owners | Direct container value-transfer coverage |
| `SkullbonezTests/TestReserveAllocator.cpp:857-940` | throwing copy construction at line 880 and throwing move construction at line 917 | `SceneLoad`; test list owner | Exception-cleanup coverage for a contract that FC1 removes |
| `SkullbonezTests/TestReserveAllocator.cpp:944-998` | direct list copy construction at line 988 | explicit test Replay owner plus granted growth scope | Proves the current hidden Replay copy path; must be replaced by owner-level clone coverage |

The same-name independent publisher test at
`TestReserveAllocator.cpp:659-705` constructs two independent lists; it does
not copy either one.

## Potential Compiler-Generated Transfers

Every direct list owner except `ColliderStore` currently inherits ordinary
copy/move availability from `PhysicsFixedList`. This includes
`BuoyancySystem`, all eight Physics stages, the contact-solver side-effect
aggregate, `PhysicsBodyStore`, `PhysicsWorld`, and `PhysicsEngine`.

For a non-empty owner:

- copy/move construction starts with zero destination capacity and therefore
  needs `SceneLoad` or approved Replay growth;
- copy/move assignment needs a legal growth phase unless the destination was
  already committed to at least every source list's live count;
- move is not a cheap ownership transfer and has no production caller.

The containment chain adds two unused move candidates:

- `SceneWorld` is non-copyable because `SceneTerrain` owns a `unique_ptr`, but
  it declares no explicit move; any viable compiler-generated move would
  recursively move `PhysicsEngine`;
- `SceneController` likewise declares no explicit move and contains
  `SceneWorld`.

No source or test call moves either owner. Their complete sibling-member trait
surface is outside FC1; if either move is intended to be public later, it needs
a separate compile-time ruling. `Run` has a user-declared destructor and
non-copyable members, so it does not add a usable whole-application transfer
route.

## Returns, Parameters, Queues, And Storage

The source census found:

- no `PhysicsFixedList`, Physics stage, `PhysicsBodyStore`, `PhysicsWorld`,
  `PhysicsEngine`, or `ColliderStore` passed or returned by value;
- no queue, vector, deque, optional, variant, or array storing those owner
  values;
- one production `unique_ptr<PhysicsEngine>` retained by prediction and
  test-only `unique_ptr<ColliderStore>` destinations;
- all ordinary Physics call paths borrow owners by pointer/reference and expose
  list contents through spans or references.

## FC1 Disposition Table

| Surface | Actual need | FC1 disposition |
|---|---|---|
| `PhysicsFixedList` copy construction/assignment | none as a public container operation | delete both; add compile-time non-copyability proof |
| `PhysicsFixedList` move construction/assignment | no production need; current move still reserves and walks elements | delete both unless a concrete owner operation proves a need; do not retain it as a cheap-looking transfer |
| production `PhysicsEngine` assignment | prediction isolation | replace with a named prediction-owner operation that coordinates only permitted concrete owner clones/restores; do not add `PhysicsEngine::Clone`, a generic list clone, or a context/service bag |
| `PhysicsBodyStore` | copied only through prediction engine assignment | expose a phase-checked concrete-owner clone only if the prediction-owner implementation still requires it |
| `PhysicsWorld` | copied only through prediction engine assignment | expose a phase-checked concrete-owner clone only for solver/prediction state that is not already covered by `PhysicsSolverSnapshot` |
| `ColliderStore` | copied through prediction and directly in one test | replace ordinary special members with a named, phase-checked concrete-owner clone that preserves `RebindShapeReferences`; remove the move contract unless a caller appears |
| direct list transfer tests | test the soon-to-be-deleted API | replace with compile-time deletion checks and focused concrete-owner legal-clone/illegal-phase coverage |
| implicit stage/outer-owner special members | no callers | let deleted list transfer make them unavailable, and add explicit deletion where the public owner contract should be unmistakable |

## Repeatable Census Commands

```powershell
rg -n --glob '!ThirdPtySource/**' "PhysicsFixedList<" SkullbonezSource SkullbonezTests Agentic/Tests
rg -n "PhysicsBodyRowList<|PhysicsContactRowList<|PhysicsPipelineRowList<|PhysicsCandidatePairList|PhysicsCollisionCellKeyList" SkullbonezSource/Physics SkullbonezTests
rg -n -C 8 "predictionEngine|ReserveSceneCapacityLike|RuntimeReserveOwnerScope|RuntimeReserveGrowthScope" SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
rg -n "ColliderStore::ColliderStore|ColliderStore::operator=" SkullbonezSource/Physics/ColliderStore.cpp
rg -n "trivialCopy|trivialMove|trackedCopy|trackedMove|failedCopy|failedMove|replayClone|successor = std::move" SkullbonezTests/TestReserveAllocator.cpp
rg -n "make_unique<ColliderStore>" SkullbonezTests/TestPhysicsHandles.cpp
rg -n --glob '!ThirdPtySource/**' "(vector|deque|queue|array|optional|variant)<\s*(Physics::)?(PhysicsEngine|PhysicsWorld|PhysicsBodyStore|ColliderStore)" SkullbonezSource SkullbonezTests
```

## Questions And Blockers

No owner input is required for FC1. The existing ruling is sufficient:
prediction isolation is the only production clone, and it must be expressed at
the permitted concrete owner/prediction snapshot boundaries. No FC0 blocker was
found.

## Independent Review

Independent review reconciled the 98-path count and every listed production and
test transfer. It found zero blockers. Compile-time non-copyability and
legal/illegal phase proofs remain correctly owned by FC2.

# Scene-Sized Store Capacity — SC0 Census

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Source tip: `86dc3302`
Plan phase: `scene-sized-store-capacity` SC0

## Outcome

Current source has **90 retained dense Physics rows**, not the 65 estimated by
the originating review:

- 40 `PhysicsFixedList` members, not 25;
- 50 `std::vector` members, not 40.

The fixed-list correction comes from counting the complete body/collider handle
topology and all 20 body hot-field rows. The vector correction includes the
sleep owner's eighteenth row, four broadphase rows added after the review tip,
and the five retained `PhysicsContactSolverSideEffects` rows that the original
owner summary omitted. No row below is unclassified.

The raw sizing-site measurement remains **147** `reserve` / `resize` / `assign`
call sites under `SkullbonezSource/Physics`. That number includes local vectors,
replay snapshot values, and repeated operations as well as the 90 retained
members. The member tables below name the construction/current sizing source and
the one target quantity for every retained row; later phases must not treat the
147 call count as a store count or a frozen gate.

## Measurement Method

Declaration census:

```text
rg -n "PhysicsFixedList<" SkullbonezSource/Physics -g "*.h"
rg -n -g "*.h" "^\s+(?:mutable\s+)?std::vector<.*>\s+m_" \
  SkullbonezSource/Physics/Stages SkullbonezSource/Physics/PhysicsEngine.h \
  SkullbonezSource/Physics/PhysicsWorld.h
```

Sizing-site census:

```text
rg -n "reserve\(|resize\(|assign\(" SkullbonezSource/Physics \
  -g "*.cpp" -g "*.h"
```

Element sizes were compiled with MSVC 19.51 against the current Profile maths
library. Payload figures are `capacity * sizeof(element)` and deliberately
exclude allocator metadata and the small `std::vector` / `PhysicsFixedList`
control objects. They are therefore comparable lower bounds, not process RSS.

Symbols used below:

| Symbol | Quantity |
|---|---|
| `B` | Exact body rows required by the incoming scene |
| `C` | Exact collider rows required by the incoming scene; currently `C == B` |
| `HB`, `HC` | Body/collider handle-slot high-water, at least `B` / `C` |
| `P` | Bounded unique candidate-pair capacity: `min(B * (B - 1) / 2, 32768)` |
| `T` | Terrain manifold capacity, one row per body (`B`) |
| `K` | Committed persistent-contact row bound: `4P + 8B` (up to four object-manifold points per pair plus eight terrain points per body) |
| `J` | Point-joint allowance committed before creation: exact authored/ragdoll count for a scene load, or an explicit cold allowance for a standalone/probe engine |
| `H` | Convex-hull collider count, introduced by SC2 |

## Fixed-List Census — 40 Rows

All rows below currently embed storage for
`MAX_SCENE_OBJECTS == 8192`. “Hot” means fixed-tick traversal or mutation;
“cold” means scene topology, authoring, identity, or repair.

| Owner / declaration | Members (every member named) | Element bytes | Hot/cold | Current capacity source | Target quantity / target owner |
|---|---|---:|---|---|---|
| `PhysicsBodyStore.h:432-480` | `m_bodies` | 72 | cold record, fixed-tick read | `MAX_SCENE_OBJECTS` | `B`; `PhysicsBodyStore` |
| `PhysicsBodyStore.h:433-468` | `m_positionX`, `m_positionY`, `m_positionZ`, `m_orientationX`, `m_orientationY`, `m_orientationZ`, `m_orientationW`, `m_linearVelocityX`, `m_linearVelocityY`, `m_linearVelocityZ`, `m_angularVelocityX`, `m_angularVelocityY`, `m_angularVelocityZ`, `m_inverseMass`, `m_inverseInertiaX`, `m_inverseInertiaY`, `m_inverseInertiaZ`, `m_boundingRadius` | 4 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsBodyStore` |
| `PhysicsBodyStore.h:469-470` | `m_fixed`, `m_awake` | 1 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsBodyStore` |
| `PhysicsBodyStore.h:471` | `m_modelBodyHandles` | 8 | cold topology | `MAX_SCENE_OBJECTS` | `B`; `PhysicsBodyStore` |
| `PhysicsBodyStore.h:472-480` | `m_handleGenerations`, `m_handleAlive`, `m_handleModelIndices`, `m_handleSceneObjectIds`, `m_freeHandleSlots`, `m_assignedHandleScratch` | 4, 1, 4, 4, 4, 1 | cold identity/repair | `MAX_SCENE_OBJECTS` | `HB = max(B, retained slot high-water)`; `PhysicsBodyStore` |
| `ColliderStore.h:161` | `m_colliders` | 7,228 | hot record | `MAX_SCENE_OBJECTS` | SC1 uses `C`; SC2 replaces the inline hull payload with hot row `C` plus hull store `H`; `ColliderStore` |
| `ColliderStore.h:165` | `m_authoringRecords` | 32 | cold authoring | `MAX_SCENE_OBJECTS` | `C`; `ColliderStore` |
| `ColliderStore.h:166` | `m_modelColliderHandles` | 8 | cold topology | `MAX_SCENE_OBJECTS` | `C`; `ColliderStore` |
| `ColliderStore.h:167-175` | `m_handleGenerations`, `m_handleAlive`, `m_handleModelIndices`, `m_handleSceneObjectIds`, `m_freeHandleSlots`, `m_assignedHandleScratch` | 4, 1, 4, 4, 4, 1 | cold identity/repair | `MAX_SCENE_OBJECTS` | `HC = max(C, retained slot high-water)`; `ColliderStore` |
| `BuoyancySystem.h:61` | `m_bodyFacts` | 20 | hot | `MAX_SCENE_OBJECTS` | `B`; `BuoyancySystem` |
| `PhysicsSleepController.h:143-146` | `m_awakeBodyIndices`, `m_awakeListPositions` | 4 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsSleepController` |

Payload by fixed-list owner:

| Owner | Rows | Current payload bytes | MiB |
|---|---:|---:|---:|
| `PhysicsBodyStore` | 28 | 1,409,024 | 1.344 |
| `ColliderStore` | 9 | 59,686,912 | 56.922 |
| `BuoyancySystem` | 1 | 163,840 | 0.156 |
| `PhysicsSleepController` | 2 | 65,536 | 0.063 |
| **Total** | **40** | **61,325,312** | **58.484** |

`ColliderStore::m_colliders` alone is 59,211,776 bytes (56.469 MiB). This
confirms the plan's 7,228-byte row and 56.5 MiB headline before SC2.

## Vector Census — 50 Rows

The baseline is the default constructed `SceneWorld` state. Most vectors reserve
the absolute 8,192-body constants in their constructors. The two exceptions
whose existing generalized seam observes the default active capacity are
`PhysicsEngine::m_authoredBodyDescs` (4,000 rows) and
`PhysicsForceStage::m_mutualGravityPairForces` (the 512-body triangular cap).

| Owner / declaration | Members (every member named) | Element bytes | Hot/cold | Current capacity source | Target quantity / target owner |
|---|---|---:|---|---|---|
| `PhysicsEngine.h:237` | `m_authoredBodyDescs` | 7,304 | cold authoring | `SceneWorld::m_activeSceneObjectCapacity` = 4,000 through `ReserveAuthoredBodyCapacity` | `B`; `PhysicsEngine` |
| `PhysicsWorld.h:139` | `m_timeRemaining` | 4 | hot | constructor `MAX_SCENE_OBJECTS` | `B`; `PhysicsWorld` |
| `PhysicsWorld.h:163` | `m_pointJointConstraints` | 68 | hot retained constraint | constructor `MAX_SCENE_OBJECTS` | `J`; `PhysicsWorld` |
| `PhysicsSleepController.h:134-140` | `m_sleepSupportedThisFrame`, `m_sleepInhibitedThisFrame`, `m_sleepState`, `m_sleepCounter`, `m_underwaterSleepLocked`, `m_sleepIslandVisualId`, `m_sleepIslandAssignedVisualId` | 1, 1, 1, 1, 1, 4, 4 | hot | constructor body capacity 8,192 | `B`; `PhysicsSleepController` |
| `PhysicsSleepController.h:155` | `m_sleepSupportEdges` | 8 | hot | `MAX_SLEEP_SUPPORT_EDGES = 4 * 8192` | `min(P + 2J, 32768)`: at most one contact support edge per candidate pair plus two directed edges per point joint; `PhysicsSleepController` |
| `PhysicsSleepController.h:156-165` | `m_sleepIslandParent`, `m_sleepIslandRank`, `m_sleepIslandHasAwake`, `m_sleepIslandHasSupportAnchor`, `m_sleepIslandEligible`, `m_sleepIslandCanSleep`, `m_sleepScratchFlags`, `m_sleepVisualIslandIds`, `m_sleepVisualIslandBodies`, `m_restingWakeQueueScratch` | 4, 1, 1, 1, 1, 1, 1, 4, 4, 4 | hot / diagnostic | constructor body capacity 8,192 | `B`; `PhysicsSleepController` |
| `PhysicsNarrowphaseStage.h:123` | `m_objectNarrowphaseEvents` | 104 | hot | `PHYSICS_CANDIDATE_PAIR_RESERVE = 32768` | `P`; `PhysicsNarrowphaseStage` |
| `PhysicsNarrowphaseStage.h:124` | `m_objectNarrowphaseIslands` | 24 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsNarrowphaseStage` |
| `PhysicsNarrowphaseStage.h:125` | `m_objectNarrowphaseIslandPairIndices` | 4 | hot | pair reserve 32,768 | `P`; `PhysicsNarrowphaseStage` |
| `PhysicsNarrowphaseStage.h:126-129` | `m_objectNarrowphaseIslandWriteOffsets`, `m_objectNarrowphaseParent`, `m_objectNarrowphaseRank`, `m_objectNarrowphaseRootToIsland` | 8, 4, 1, 4 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsNarrowphaseStage` |
| `PhysicsContactSolverStage.h:148` | `m_persistentContacts` | 160 | hot | constructor `4 * 8192`; in-tick reserves at `PersistentContactSolver.cpp:805,992` can exceed it | `K = 4P + 8B`; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:149` | `m_persistentContactCache` | 24 | hot | constructor `4 * 8192`; replay restore can request snapshot size | `K`; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:151-152` | `m_persistentContactCounts`, `m_persistentRestingContactCounts` | 2 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:153` | `m_solverBodies` | 80 | hot | `MAX_SCENE_OBJECTS` | `B`; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:123` | `m_sideEffects.pipelineRecords` | 56 | diagnostic side effect | fixed 4,096 | semantic fixed ceiling 4,096; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:124` | `m_sideEffects.collisionVisualBodies` | 4 | diagnostic side effect | `8 * 8192` | `2P`; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:125` | `m_sideEffects.fixedContactBodies` | 4 | hot consequence | `MAX_SCENE_OBJECTS` | `K`: fixed/fixed candidate pairs are rejected, so each admitted positive-impulse row can append at most one fixed endpoint; `PhysicsContactSolverStage` |
| `PhysicsContactSolverStage.h:126-127` | `m_sideEffects.releaseWakeBodies`, `m_sideEffects.fixedTreeReleases` | 4, 28 | hot bounded consequence | reserve 8 each, but no semantic 8-event check exists | `B` each: `ReleaseFixedBody` prevents a body from releasing twice; `PhysicsContactSolverStage` |
| `PhysicsBroadphaseStage.h:76-77` | `m_candidatePairs`, `m_collisionCellKeys` | 8, 8 | hot / diagnostic | pair reserve 32,768 | `P`; `PhysicsBroadphaseStage` |
| `PhysicsBroadphaseStage.h:85,90-91` | `m_sleepPrunedPairs`, `m_pairOracleShadowPairs`, `m_pairOracleNormalizedDriverPairs` | 8 | Debug oracle | pair reserve 32,768 | `P`, Debug only; `PhysicsBroadphaseStage` |
| `PhysicsForceStage.h:72` | `m_mutualGravityForces` | 12 | hot | constructor 8,192 | `B`; `PhysicsForceStage` |
| `PhysicsForceStage.h:73` | `m_mutualGravityPairForces` | 12 | hot | `choose2(min(active capacity, 512))` = 130,816 | `choose2(min(B, 512))`; `PhysicsForceStage` |
| `PhysicsTerrainStage.h:91-92` | `m_detectionCandidates`, `m_contactManifolds` | 56, 320 | hot | `MAX_SCENE_OBJECTS` | `B` / `T == B`; `PhysicsTerrainStage` |
| `PhysicsStepDiagnostics.h:52` | `m_collisionVisualContacts` | 1 | diagnostic hot mirror | `MAX_SCENE_OBJECTS` | `B`; `PhysicsStepDiagnostics` |
| `PhysicsStepDiagnostics.h:54` | `m_physicsDebugContacts` | 80 | Debug diagnostic | constructor `4 * MAX_SCENE_OBJECTS`, then fixed-tick `reserve(m_persistentContacts.size())` at `PersistentContactSolver.cpp:1591` | `K`; `PhysicsStepDiagnostics` |
| `PhysicsStepDiagnostics.h:55` | `m_physicsPipelineTrace` | 56 | diagnostic | fixed 4,096 | semantic fixed ceiling 4,096; `PhysicsStepDiagnostics` |

Payload by vector owner at the current default capacities:

| Owner | Rows | Current payload bytes | MiB |
|---|---:|---:|---:|
| `PhysicsEngine` | 1 | 29,216,000 | 27.863 |
| `PhysicsWorld` | 2 | 589,824 | 0.563 |
| `PhysicsSleepController` | 18 | 548,864 | 0.523 |
| `PhysicsNarrowphaseStage` | 7 | 3,874,816 | 3.695 |
| `PhysicsContactSolverStage` including side effects | 10 | 7,241,984 | 6.906 |
| `PhysicsBroadphaseStage` (Debug) | 5 | 1,310,720 | 1.250 |
| `PhysicsForceStage` | 2 | 1,668,096 | 1.591 |
| `PhysicsTerrainStage` | 2 | 3,080,192 | 2.937 |
| `PhysicsStepDiagnostics` | 3 | 2,859,008 | 2.727 |
| **Total (Debug)** | **50** | **50,389,504** | **48.055** |

Profile/Release omit the three Debug broadphase oracle vectors, reducing this
vector payload by 786,432 bytes. The current Debug dense payload lower bound is
therefore **111,714,816 bytes (106.54 MiB) per PhysicsEngine** before object and
allocator overhead. An armed prediction engine pays the same fixed-list and most
vector costs again; SC6 must measure its actual committed capacities rather than
blindly doubling this lower bound.

## Capacity Authority And Commit Seam

The existing seam is the right one to generalize:

```text
SceneWorld::ReserveForActiveSceneObjectCapacity
  -> PhysicsEngine::ReserveAuthoredBodyCapacity
     -> PhysicsWorld::ReserveBodyScratchCapacity
        -> PhysicsForceStage::ReserveBodyScratchCapacity
```

Today that path commits the configured admission capacity (default 4,000), not
the loaded scene size, and only reaches authored descriptors plus force scratch.
SC3 should keep this path and widen its concrete owner calls; it should not add a
parallel capacity service, callback, or aggregate.

The final-count evidence is split by load kind:

- Authored scenes are completely parsed into `AuthoredScene` before setup begins
  (`SceneController.Load.cpp:1056-1057`). `SceneAuthoredSetup` owns the expansion
  rules for balls, boxes, hulls, ragdolls, and asset parts, so it is the only
  current code that can preflight the exact `B`, `C`, `H`, and `J` without
  duplicating those rules.
- Generated scenes already hold their solver ball/box counts before the loops in
  `SceneGeneratedSetup.cpp`. Their preflight can derive `B == C` directly.
- `SceneWorld` owns the one-to-one entity/body/collider topology transaction and
  already delegates capacity to `PhysicsEngine`. It is the target scene-load
  commit coordinator after the authored/generated setup owner supplies the exact
  strong scalar counts. Each store/stage remains the sole authority for its own
  retained capacity.

`J` is not an unbounded post-load promise. Authored setup can count explicit
scene joints plus deterministic ragdoll joints before mutation and commits that
exact allowance. The public `CreatePointJoint` path must fail loud when the
committed allowance is exhausted. The only current non-scene callers are startup
probes; a standalone/probe `PhysicsEngine` must explicitly commit its small cold
allowance before those calls. No fixed tick may enlarge `J`.

The commit must occur during the `SceneLoadTransaction` Load phase after parse
and expansion preflight, before the first `TryCreateSceneEntity` mutation and
before any fixed tick. Same-or-smaller counts are no-ops; larger counts grow
monotonically under one registered Physics scene-load reserve owner.

## `DEFAULT_SCENE_OBJECT_CAPACITY` Decision

Current non-plan consumers are:

- `Runtime/Scene/SceneWorld.h` — admission limit;
- `Runtime/Scene/SceneEntityStore.h` — admission/storage preflight limit;
- `Runtime/App/RunStartupState.h` — startup option default;
- `UI/UI.h` and `UI/UIWindowInteractionOwner.h` — operator-visible capacity.

These are admission/UI semantics, not dense-physics storage requirements. Per
the plan's owner-overridable default, SC3 keeps
`DEFAULT_SCENE_OBJECT_CAPACITY = 4000`; it stops using that value as the physics
allocation size. `MAX_SCENE_OBJECTS = 8192` remains the absolute fail-loud
ceiling.

## Independent Review

The read-only rubber-duck review challenged declaration completeness, payload
arithmetic, every derived bound, the load-phase authority, and downstream phase
scope. It caught and drove correction of:

- the sleep owner's eighteenth vector row (89 became 90);
- omitted broadphase and contact-side-effect scope in SC4/SC5;
- the exact existing `ReserveAuthoredBodyCapacity` seam name;
- unsafe linear pair/support assumptions, replaced by `P`, `K`, and `J`;
- dynamic terrain-point and post-load point-joint assumptions;
- understated fixed-contact/release side-effect bounds;
- the third fixed-tick reserve at `PersistentContactSolver.cpp:1591`; and
- the final fixed-contact bound, reduced from an overconservative `2K` to `K`
  after proving fixed/fixed candidates cannot enter the solver.

Final latest-source verdict: **ZERO BLOCKERS**.

## Validation

- MSVC size probe: PASS; all complex element sizes in the tables were compiled
  from current headers.
- Declaration counts: PASS; 40 fixed-list rows and 45 direct vector rows plus 5
  nested side-effect rows.
- Sizing-site census: PASS; 147 `reserve` / `resize` / `assign` sites.
- `python tools/check_related_paths.py`: PASS; 568 files scanned, 1,510
  repository paths, zero findings.
- `git diff --check`: PASS.

## SC0 Closure

- Fixed-list rows classified: 40/40.
- Vector rows classified: 50/50.
- Total retained dense rows classified: 90/90.
- Each row has an element size, current source, hot/cold classification, target
  quantity, and target owner.
- Existing commit seam: accepted for generalization.
- Final-count owner and phase: identified for authored and generated loads.
- Current per-owner payload lower bound: recorded for SC7 comparison.
- Unclassified rows: zero.

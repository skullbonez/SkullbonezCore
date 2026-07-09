# 02 — PhysicsWorld Solver Decomposition

Date: 2026-07-08
Status: In Progress
Priority: P0
Owner: Physics
Source issue: audit iss-02 (severity 5) + iss-14 (union-find copy-paste)

## Problem

[`PhysicsWorld.cpp`](../SkullbonezSource/Physics/PhysicsWorld.cpp) is 3,947
lines and buries the determinism-critical solver in one un-decomposable
function.

Verified evidence:

- [`RunSolverPhysics()`](../SkullbonezSource/Physics/PhysicsWorld.cpp:2156)
  spans L2156→~3793 (**~1,639 lines**) defining **33 local lambdas** that are
  whole pipeline stages (broadphase, narrowphase, CCD, union-find islanding,
  integrate), all mutating shared captured state — so no stage is testable in
  isolation, in the one subsystem that must be byte-exact deterministic.
- The class welds ~50 parallel `std::vector` members across unrelated concerns
  (broadphase, four sleep mechanisms, tornado gameplay, analytic buoyancy,
  union-find islanding, terrain, debug, replay serialization).
- Union-find (path-compression `find` + union-by-rank) is hand-copied **three
  times**: [L1852](../SkullbonezSource/Physics/PhysicsWorld.cpp:1852),
  [L2067](../SkullbonezSource/Physics/PhysicsWorld.cpp:2067), and the
  narrowphase island builder (~L3081) — each over its own parallel arrays.
- `CaptureReplaySolverSnapshot` / `RestoreReplaySolverSnapshot` open-code ~250
  lines of field-by-field mirroring that must be edited in lockstep with every
  member.

## Goal

The solver is a sequence of separately-testable stages over explicit buffers;
gameplay (tornado, buoyancy) lives outside the core world type; union-find
exists once; replay snapshotting is table-driven.

## Approach

- [x] **Phase 0 — Extract `DisjointSet` (quick win).** One helper (path
  compression + union-by-rank) over a caller-supplied index range; replace all
  three copies. Removes ~90 duplicated lines. **Merge order is
  determinism-sensitive — preserve it exactly.**
- [ ] **Phase 1 — Lift the 33 lambdas** into named stage functions taking
  explicit inputs (`bodyStore`, `colliderStore`, `worldForces`, `dt`) and
  returning explicit outputs. No shared captured mutable state. Each stage
  becomes unit-testable.
- [ ] **Phase 2 — Evict gameplay.** Move tornado capture/eject arrays and
  analytic sphere-cap buoyancy into their own systems; `PhysicsWorld` stops
  owning gameplay state.
- [ ] **Phase 3 — Table-drive replay snapshot.** Replace the ~250-line hand
  mirroring with a single field list / serializable record so members can't
  drift out of lockstep.

## Risks / determinism

Byte-exact determinism is a hard invariant. Every phase must be
behavior-preserving; run the byte-exact CSV gate after **each** phase, not just
at the end. Do not reorder floating-point accumulation or island-merge order.

## Step-by-step implementation

Do steps in order. After every step that changes code, run the gate named in the
step and commit before starting the next. **Byte-exact physics is a hard gate —
never commit a red `validate_physics`.** Never reorder float accumulation or
island-merge tie-breaks.

### Phase 0 — DisjointSet (execution slot 1)

- [x] **0.1** Open `PhysicsWorld.cpp` and read the three union-find copies near
  L1852-1900 (`WakePointJointIsland`), L2067-2119
  (`WakePointJointConnectedBodies`), and L3081-3117
  (`findObjectNarrowphaseRoot`/`unionObjectNarrowphaseRoots`). Write down the
  exact tie-break each uses (which root wins on equal rank) — it must be
  preserved. No code change.

  Tie-break record (2026-07-08):
  - `WakePointJointIsland`: `findIsland(a)` becomes `rootA` and
    `findIsland(b)` becomes `rootB`; roots swap only when
    `rank[rootA] < rank[rootB]`. Equal rank keeps `rootA` as the parent,
    attaches `rootB` under it, then increments `rank[rootA]`.
  - `WakePointJointConnectedBodies`: same parent/rank rule as above over
    `m_sleepIslandParent` / `m_sleepIslandRank`; equal rank keeps the first
    argument's root as parent.
  - `buildObjectNarrowphaseIslands`: same parent/rank rule over
    `m_objectNarrowphaseParent` / `m_objectNarrowphaseRank`; equal rank keeps
    the first argument's root as parent.
- [x] **0.2** Add a `DisjointSet` helper (suggested: `Physics/DisjointSet.h`)
  operating on caller-supplied `parent`/`rank` buffers sized to a passed count:
  `find(i)` with path compression, `unite(a,b)` union-by-rank using the **same
  tie-break** from 0.1. No callers yet. Build only (`validate_build Profile`).
  Commit.

  Validation: `cmd.exe /c tools\validate_build.bat Profile` passed on
  2026-07-08 with 0 warnings and 0 errors; mirrored log:
  `Agentic\Logs\cleanup-02-step-0.2-validate-build-profile.log`.
- [x] **0.3** Replace copy 1 (`WakePointJointIsland`) with the helper over
  `m_sleepIslandParent`/`m_sleepIslandRank`. Keep all surrounding logic
  identical. Gate: `validate_physics` byte-exact. Commit.

  Validation: `cmd.exe /c tools\validate_physics.bat` passed on 2026-07-08;
  `physics_regression_solver.csv` matched the baseline byte-exact
  (20001 lines), with 0 warnings and 0 errors. Mirrored log:
  `Agentic\Logs\cleanup-02-step-0.3-validate-physics.log`.
- [x] **0.4** Replace copy 2 (`WakePointJointConnectedBodies`). Gate:
  `validate_physics`. Commit.

  Validation: `cmd.exe /c tools\validate_physics.bat` passed on 2026-07-08;
  `physics_regression_solver.csv` matched the baseline byte-exact
  (20001 lines), with 0 warnings and 0 errors. Mirrored log:
  `Agentic\Logs\cleanup-02-step-0.4-validate-physics.log`.
- [x] **0.5** Replace copy 3 (narrowphase island builder) over
  `m_objectNarrowphaseParent`/`Rank`. Gate: `validate_physics`. Commit.

  Validation: `cmd.exe /c tools\validate_physics.bat` passed on 2026-07-08;
  `physics_regression_solver.csv` matched the baseline byte-exact
  (20001 lines), with 0 warnings and 0 errors. Mirrored log:
  `Agentic\Logs\cleanup-02-step-0.5-validate-physics.log`.
- [x] **0.6** `rg -n "= find|union.*Root|findIsland" SkullbonezSource/Physics` —
  confirm no inline union-find remains. Tick Phase 0.

  Result: the first 0.6 search found one additional sleep-island inline copy in
  `RunSolverPhysics`; it was replaced with `DisjointSet` under the same
  equal-rank first-root tie-break. `cmd.exe /c tools\validate_physics.bat`
  passed on 2026-07-08 after that replacement, with
  `physics_regression_solver.csv` byte-exact (20001 lines), 0 warnings, and 0
  errors. The final `rg -n "= find|union.*Root|findIsland"
  SkullbonezSource\Physics` returned no matches. Mirrored log:
  `Agentic\Logs\cleanup-02-step-0.6-validate-physics.log`.

> After 0.6, STOP and move to the next plan in the run order (12). Return here for
> Phase 1 at execution slot 9.

### Phase 1 — Lift the 33 lambdas (execution slot 9)

- [x] **1.1** In `RunSolverPhysics` (L2156→~3793) list the 33 lambdas and group
  them by stage (broadphase candidate / sweep pair / narrowphase island build /
  terrain detect / wake-sleep / integrate). Paste the grouped list here as a
  sub-checklist. No code change.

  Completed 2026-07-09. A brace-bounded scan of the current
  `PhysicsWorld::RunSolverPhysics` body found 35 lambda expressions in lines
  2116-3699. The older 33-lambda count is stale against current source; this
  inventory records the exact current set so extraction can preserve behavior.

  Driver accessors / force integration:
  - [x] L2131 `bodyIsFixed`: body fixed-state accessor.
  - [x] L2132 `bodyPosition`: body position accessor.
  - [x] L2134 `bodyRadius`: collider broadphase-radius accessor.
  - [x] L2165 `applyForcesAt`: per-body force application.

  Broadphase candidate build and pruning:
  - [x] L2225 `broadphaseCandidateCanTouch`: swept bounding-sphere pair filter.
  - [x] L2307 `appendCandidatePairIfMissing`: append unique conservative pair.
  - [x] L2335 `isFastSmallSweepBody`: fast-small-body classifier.
  - [x] L2354 `sweptSegmentTouchesExpandedBody`: conservative segment/body test.
  - [x] L2404 anonymous fixed/fixed `remove_if` predicate.
  - [x] L2419 anonymous point-joint pair `remove_if` predicate.
  - [x] L2474 anonymous sleep/sleep `remove_if` predicate with trace emission.

  Wake and contact view helpers:
  - [x] L2499 `hasWakeEnergy`: awake-neighbor wake-energy test.
  - [x] L2508 `wakeSleepingModel`: sleep-state clear plus immediate force apply.
  - [x] L2527 `contactBodyViewAtTime`: object contact pose view at candidate time.
  - [x] L2536 `terrainContactBodyViewForIndex`: terrain contact pose/material view.
  - [x] L2552 `hasPersistentWakeContact`: exact persistent overlap wake test.

  Object/object sweep and CCD:
  - [x] L2577 `hasObjectContactAtTime`: exact object contact query at time.
  - [x] L2601 `refineObjectSweepContactTime`: contact-window refinement search.
  - [x] L2654 `sweepObjectPair`: swept object contact query.
  - [x] L2676 `objectPairHasPersistentContactCache`: cache-prefix lookup.
  - [x] L2690 anonymous `lower_bound` cache-key comparator.
  - [x] L2696 `objectPairNeedsSweptCcd`: settled-pair CCD bypass decision.

  Object narrowphase events and dispatch:
  - [x] L2739 `recordObjectNarrowphaseEvent`: stage-record copy into event buffer.
  - [x] L2749 `emitObjectCollisionTimeEvent`: collision-time event fields.
  - [x] L2758 `markObjectVisualEvent`: visual-contact event fields.
  - [x] L2765 `writeObjectCollisionCellEvent`: hashed collision-cell event fields.
  - [x] L2778 `commitObjectNarrowphaseEvent`: serial side-effect application.
  - [x] L2814 `processObjectNarrowphasePair`: pair CCD/wake processing.
  - [x] L3014 `processObjectNarrowphaseIsland`: island-local pair loop.
  - [ ] L3025 `processObjectNarrowphasePairsSerial`: serial pair loop.
  - [ ] L3036 `buildObjectNarrowphaseIslands`: pair island staging.
  - [ ] L3138 anonymous island sort comparator.

  Terrain detection:
  - [ ] L3197 `detectTerrainAt`: per-body swept terrain candidate.
  - [ ] L3220 `commitTerrainCandidate`: terrain hit/manifold side effects.

  Remaining-time integration and sleep:
  - [ ] L3319 `integrateRemainingAt`: per-body remaining-time integration.
- [ ] **1.2** For **one stage at a time**: extract its lambda(s) into a named
  `static` free function taking explicit parameters (`bodyStore`,
  `colliderStore`, `worldForces`, `dt`, plus the specific arrays it uses) instead
  of captures. Do not change computation order. Gate: `validate_physics`
  byte-exact. Commit. Repeat until all stages are extracted.

  Progress 2026-07-09:
  - Extracted the `applyForcesAt` lambda into `ApplyForcesForSolverBody` plus
    `ApplyForcesStageContext`, preserving the serial and worker dispatch paths.
  - Moved `PhysicsPipelineStageName` inline in
    `Physics/Debug/PhysicsDebugVisualizer.h` so SkullScope and replay can use the
    diagnostics label without depending on the debug visualizer `.cpp`.
  - Added `SkullbonezSource\Core\SkullScope.cpp` to `SKULLBONEZ_TESTS.vcxproj`
    because the Debug physics gate links `PhysicsDiagnosticsSink.cpp`.
  - Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_apply_forces_stage_validate_physics_attempt3_20260709_0922.log`
    (44.14s; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extra mapped gate for the test-project linkage update:
    `tools\validate_tests.bat` passed in
    `TestOutput\agent_logs\plan02_apply_forces_stage_validate_tests_20260709_0924.log`
    (3.02s; project filters 0 errors, Profile test build 0 warnings and 0
    errors, 59/59 doctest cases and 1532/1532 assertions passed).
  - Extracted the `bodyIsFixed`, `bodyPosition`, and `bodyRadius` accessors into
    `IsSolverBodyFixed`, `SolverBodyPosition`, and `SolverBodyRadius`. Gate
    evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_solver_accessors_validate_physics_20260709_0928.log`
    (43.88s; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `broadphaseCandidateCanTouch` lambda and its filter context
    into `BroadphaseCandidateCanTouch` plus `BroadphaseCandidateFilterContext`,
    sharing the existing solver body accessors for radius and position reads.
    Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_broadphase_candidate_filter_validate_physics_20260709_0929.log`
    (28.5s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `appendCandidatePairIfMissing` lambda into
    `AppendCandidatePairIfMissing`, keeping pair normalization, broadphase
    filter reuse, linear duplicate suppression, and append order unchanged.
    Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_append_candidate_pair_validate_physics_20260709_0934.log`
    (28.0s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `isFastSmallSweepBody` lambda into `IsFastSmallSweepBody`,
    keeping the fixed-body rejection, radius threshold, and displacement
    threshold unchanged. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_fast_small_sweep_validate_physics_20260709_0938.log`
    (28.2s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `sweptSegmentTouchesExpandedBody` lambda into
    `SweptSegmentTouchesExpandedBody`, preserving the closest-point projection,
    raw `config.contactEpsilon` input, and expanded-radius threshold. Gate
    evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_swept_segment_validate_physics_20260709_0940.log`
    (27.9s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the anonymous fixed/fixed `remove_if` predicate into
    `IsFixedSolverCandidatePair` plus `FixedSolverCandidatePairPredicate`,
    preserving bounds checks before body-record reads. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_fixed_pair_predicate_validate_physics_20260709_0942.log`
    (28.1s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the anonymous point-joint `remove_if` predicate into
    `IsPointJointBodyPair`, `IsPointJointCandidatePair`, and
    `PointJointCandidatePairPredicate`; `PhysicsWorld::IsPointJointPair` now
    delegates to the same helper. Gate evidence: `tools\validate_physics.bat`
    passed in
    `TestOutput\agent_logs\plan02_point_joint_predicate_validate_physics_20260709_0944.log`
    (27.8s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the anonymous sleep/sleep `remove_if` predicate with trace
    emission into `IsSleepPrunedCandidatePair`,
    `TryRecordSleepPrunedCandidatePair`, and
    `SleepPrunedCandidatePairPredicate`, preserving the capped
    `SleepPrunedPair` trace append for erased pairs only. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_sleep_prune_predicate_validate_physics_20260709_0946.log`
    (28.1s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `hasWakeEnergy` lambda into `HasWakeEnergy`, threading the
    locally computed squared sleep thresholds explicitly to preserve the
    config-derived sleep policy. First gate attempt failed because the helper
    could not see `RunSolverPhysics`-local threshold constants; attempt 2 fixed
    the signature. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_wake_energy_validate_physics_attempt2_20260709_0949.log`
    (28.6s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `wakeSleepingModel` lambda into `WakeSleepingSolverBody`,
    preserving the narrower solver wake mutation and immediate force
    application instead of reusing `WakeDynamicBodyState`, which also clears
    underwater locks and persistent contact cache state. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_wake_sleeping_model_validate_physics_20260709_0951.log`
    (Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `contactBodyViewAtTime` lambda into
    `ObjectContactBodyViewAtTime`, threading `bodyRecords` explicitly and
    preserving the candidate-time pose projection used by exact contact checks
    and object/object sweep setup. Gate evidence: `tools\validate_physics.bat`
    passed in
    `TestOutput\agent_logs\plan02_contact_body_view_validate_physics_20260709_0958.log`
    (28s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `terrainContactBodyViewForIndex` lambda into
    `TerrainContactBodyViewForIndex`, threading `bodyRecords` and `config`
    explicitly while preserving the terrain pose, material, threshold, radius,
    and fixed-body fields consumed by terrain sweep and manifold building. Gate
    evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_terrain_contact_view_validate_physics_20260709_1001.log`
    (29.2s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `hasPersistentWakeContact` lambda into
    `HasPersistentWakeContact`, threading `bodyRecords`, `colliderRecords`, and
    `contactEpsilon` explicitly while preserving the exact persistent-overlap
    manifold check that wakes a sleeping body after an awake body's correction
    step. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_persistent_wake_contact_validate_physics_20260709_1004.log`
    (27.9s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `hasObjectContactAtTime` lambda into
    `HasObjectContactAtTime`, threading `bodyRecords`, `colliderRecords`, and
    `contactEpsilon` explicitly while preserving the non-mutating
    candidate-time manifold query used by CCD refinement. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_object_contact_at_time_validate_physics_20260709_1007.log`
    (28s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `refineObjectSweepContactTime` lambda into
    `RefineObjectSweepContactTime`, threading `bodyRecords`, `colliderRecords`,
    and `contactEpsilon` explicitly while preserving the conservative
    time-of-impact refinement's 48-step forward walk and 12-iteration binary
    search. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_refine_object_sweep_validate_physics_20260709_1010.log`
    (28.3s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `sweepObjectPair` lambda into `SweepObjectPair`, threading
    `bodyRecords` and `colliderRecords` explicitly while preserving default
    collision-time initialization, bounds checks, and the `SweepObjectContact`
    call shape. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_sweep_object_pair_validate_physics_20260709_1014.log`
    (28s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `objectPairHasPersistentContactCache` lambda into
    `ObjectPairHasPersistentContactCache` and named its anonymous
    `lower_bound` comparator as `PersistentContactCacheEntryPrecedesKey`,
    preserving the object/object cache-key prefix, feature-id masking, sorted
    cache search, and cache-hit result. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_persistent_cache_lookup_validate_physics_20260709_1021.log`
    (28.9s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `objectPairNeedsSweptCcd` lambda into
    `ObjectPairNeedsSweptCcd`, threading `bodyRecords`, `colliderRecords`,
    `m_persistentContactCache`, `availableTime`, and `contactSkin` explicitly
    while preserving the settled-pair CCD bypass thresholds and early returns.
    Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_object_pair_needs_ccd_validate_physics_20260709_1025.log`
    (30.8s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `recordObjectNarrowphaseEvent` lambda into the private static
    `PhysicsWorld::RecordObjectNarrowphaseEvent`, preserving the event kind,
    pipeline record copy, and `hasPipelineRecord` flag assignment without
    widening the private event type. First gate attempt failed because a free
    helper could not name the private nested event type; attempt 2 fixed the
    helper ownership. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_record_object_event_validate_physics_attempt2_20260709_1029.log`
    (39.9s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `emitObjectCollisionTimeEvent` lambda into the private static
    `PhysicsWorld::EmitObjectCollisionTimeEvent`, preserving the collision-time
    emission flag, body ids, collision time, and available-time fields while
    keeping the private event type scoped to `PhysicsWorld`. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_emit_collision_time_event_validate_physics_20260709_1032.log`
    (40.2s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `markObjectVisualEvent` lambda into the private static
    `PhysicsWorld::MarkObjectVisualEvent`, preserving the visual-contact flag
    and visual body id assignments while keeping event storage private to
    `PhysicsWorld`. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_mark_visual_event_validate_physics_20260709_1035.log`
    (41.1s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `writeObjectCollisionCellEvent` lambda into the private static
    `PhysicsWorld::WriteObjectCollisionCellEvent`, threading `bodyRecords` and
    `invCellSize` explicitly while preserving midpoint calculation, cell-floor
    coordinates, hash constants, and `hasCollisionCellKey` assignment. Gate
    evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_collision_cell_event_validate_physics_20260709_1038.log`
    (39.9s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `commitObjectNarrowphaseEvent` lambda into the private member
    `PhysicsWorld::CommitObjectNarrowphaseEvent`, threading diagnostic name and
    CSV-writer inputs explicitly while preserving the serial application order
    for pipeline records, object collision-time diagnostics, visual-contact
    marks, and collision-cell keys. Gate evidence: `tools\validate_physics.bat`
    passed in
    `TestOutput\agent_logs\plan02_commit_object_event_validate_physics_20260709_1043.log`
    (40.6s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `processObjectNarrowphasePair` lambda into
    `PhysicsWorld::ProcessObjectNarrowphasePair` plus
    `ObjectNarrowphasePairStageContext`, preserving sleeper wake decisions,
    object/object CCD timing, staged event emission, serial/parallel event
    commit order, and explicit borrowed buffer ownership for the narrowphase
    pair pass. Gate evidence: `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_process_object_pair_validate_physics_20260709_1048.log`
    (39.6s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
  - Extracted the `processObjectNarrowphaseIsland` worker lambda into
    `PhysicsWorld::ProcessObjectNarrowphaseIsland` plus the
    `ObjectNarrowphaseIslandStage` callable, preserving island pair-index
    traversal, per-pair event writes, and the existing worker dispatch shape
    without keeping a local lambda for the island stage. Gate evidence:
    `tools\validate_physics.bat` passed in
    `TestOutput\agent_logs\plan02_process_object_island_validate_physics_20260709_1052.log`
    (39.7s shell runtime; Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`).
- [ ] **1.3** `RunSolverPhysics` is now a short driver calling named stages;
  confirm it is under ~300 lines.
- [ ] **1.4** Add a unit test for at least one now-pure stage (e.g. broadphase
  candidate) — coordinate with plan 05. Gate: `validate_tests`. Commit.

### Phase 2 — Evict gameplay

- [ ] **2.1** Move tornado capture/eject arrays + methods out of `PhysicsWorld`
  into a `TornadoGameplay` system that `PhysicsWorld` calls. Gate:
  `validate_physics`. Commit.
- [ ] **2.2** Move analytic buoyancy (`RefreshUnderwaterSubmersionForBall`) into
  a buoyancy system. Gate: `validate_physics`. Commit.

### Phase 3 — Table-drive the replay snapshot

- [ ] **3.1** Replace the ~250-line field-by-field
  `CaptureReplaySolverSnapshot`/`RestoreReplaySolverSnapshot` with one field list
  (X-macro or a serialisable record) so members cannot drift out of lockstep.
  Gate: `validate_physics` + replay scrub regression. Commit.

## Validation

`tools\validate_physics.bat` (byte-exact CSV diff) after every phase;
`tools\validate_physics_deep.bat` before sign-off.

## Acceptance (structural)

- [x] `DisjointSet` appears exactly once; the three inline copies are gone.
  Evidence (2026-07-09): `Physics/DisjointSet.h` owns the helper; current source
  greps show only `PhysicsWorld.cpp` call sites and no remaining inline
  `findIsland` / `unionObjectNarrowphaseRoots` copies.
- [ ] No physics function exceeds ~300 lines; `RunSolverPhysics` is a short
  driver calling named stages.
- [ ] Solver stages have unit tests exercising them in isolation.
- [ ] Replay snapshot capture/restore is driven by one field list.
- [ ] `tools\validate_physics.bat` byte-exact diff stays green throughout.

# PhysicsWorld Ownership Map

Date: 2026-07-15  
Certified source: `687734c69d4059b9fd02e44d3c532e680c8a2156`  
Campaign: `physicsworld-stage-owner-decomposition` P0

## Certification

The starting tree passed both required certification gates without a baseline
refresh:

| Gate | Result | Evidence |
|---|---|---|
| `tools\validate_physics.bat` | PASS in 57.3 s | Debug and Profile builds completed with 0 warnings and 0 errors; standalone physics/runtime-handle smoke passed; `physics_regression_varied.csv` matched byte-exactly at 44,401 lines; `VALIDATE_PHYSICS: ALL PASSED`. |
| `tools\validate_perf.bat` | PASS in 78.5 s | Allocation-policy scan reported 337 files, 39 direct-heap findings, 139 dynamic-STL-member findings, 657 STL-growth findings, and 0 allowlist errors. The `perf_1000` guard reported 231,580 allocations / 207,413,425 bytes before steady gameplay and 0 gameplay violations. Selected-ball structure, absolute DX12 budgets, DX12 comparison, absolute physics-benchmark budgets, and physics-benchmark comparison all passed. |

The P0 performance reference to compare again in P10 is:

| Marker | Previous median (ms) | Certified median (ms) | Change |
|---|---:|---:|---:|
| DX12 `Frame/Physics` | 0.2248 | 0.1731 | -23.0% |
| DX12 `Frame/Physics/Step` | 0.2247 | 0.1730 | -23.0% |
| Physics bench `Frame/Physics` | 0.0712 | 0.0613 | -13.9% |
| Physics bench `Frame/Physics/Step` | 0.0711 | 0.0612 | -13.9% |

Both comparisons used the same frame counts as their baselines: 1,940 DX12
frames and 2,340 physics-benchmark frames. These figures are a noise-sensitive
reference, not new committed performance baselines.

## Binding Delegation Ruling

Public `PhysicsWorld` delegation is accepted domain-facade behavior. It is not
the banned forwarding pattern from the god-object closure rule because each
delegated operation terminates at a concrete stage owner that owns its state and
accepts typed value/borrowed-span inputs. A stage must never store a
`PhysicsWorld*`, `PhysicsWorld&`, broad callback pack, or other reach-back path.
`PhysicsWorld` may sequence the stages and preserve its stable public API, but
it may not continue to decide stage business behavior behind nominal wrappers.

## Data-Member Ownership

Every data member declared directly by `PhysicsWorld` at the certified source
is assigned exactly once below. A stage owner may expose a typed view or bounded
output to a sibling; that does not transfer ownership.

| Target owner | Exact members | Boundary note |
|---|---|---|
| `PhysicsBroadphaseStage` | `m_spatialGrid`; `m_candidatePairs`; `m_collisionCellKeys` | Owns grid configuration/build output, deterministic pair order, and collision-cell keys. |
| `PhysicsForceStage` | `m_mutualGravityForces`; `m_mutualGravityPairForces`; `m_mutualGravityPairHighWater` | Owns all preallocated mutual-gravity scratch. Tornado remains a sibling owner passed through the facade. |
| `PhysicsNarrowphaseStage` | `m_objectNarrowphaseEvents`; `m_objectNarrowphaseIslands`; `m_objectNarrowphaseIslandPairIndices`; `m_objectNarrowphaseIslandWriteOffsets`; `m_objectNarrowphaseParent`; `m_objectNarrowphaseRank`; `m_objectNarrowphaseRootToIsland` | Owns bounded pair/island work and ordered event output. |
| `PhysicsTerrainStage` | `m_terrainContactManifolds`; `m_terrainDetectionCandidates`; `m_terrainRestApplied` | Owns parallel detection scratch, serial commit order, and the terrain-rest mask. |
| `PhysicsContactSolverStage` | `m_persistentContacts`; `m_persistentContactCache`; `m_persistentContactSolverStats`; `m_persistentContactCounts`; `m_persistentRestingContactCounts`; `m_solverBodies`; `m_persistentContactSideEffects`; `m_contactSolver` | Wraps the existing solver and owns all persistent-row state, solver-body scratch, statistics, cache, and bounded side-effect queues. |
| `PhysicsSleepController` | `m_sleepSupportedThisFrame`; `m_sleepInhibitedThisFrame`; `m_sleepState`; `m_sleepCounter`; `m_underwaterSleepLocked`; `m_sleepIslandVisualId`; `m_sleepIslandAssignedVisualId`; `m_nextSleepIslandVisualId`; `m_sleepEnabled`; `m_seedSleepFrameCount`; `m_sleepSupportEdges`; `m_sleepIslandParent`; `m_sleepIslandRank`; `m_sleepIslandHasAwake`; `m_sleepIslandHasSupportAnchor`; `m_sleepIslandEligible`; `m_sleepIslandCanSleep`; `m_sleepPointJointBody`; `m_sleepIslandHasPointJoint`; `m_sleepIslandPointJointsRelaxed`; `m_sleepVisualIslandIds`; `m_sleepVisualIslandBodies`; `m_restingWakeVisitedScratch`; `m_restingWakeQueueScratch`; `m_sleepIslandSystem` | Owns persisted working sleep state, island construction/identity, wake traversal scratch, and the existing island algorithm. Diagnostics may borrow read-only sleep views. |
| `PhysicsStepDiagnostics` | `m_collisionVisualContacts`; `m_collisionVisualFrameActive`; `m_physicsDebugContacts`; `m_physicsPipelineTrace`; `m_diagnostics` | Owns bounded debug/pipeline records and collision-visual frame state. The sleep controller owns sleep-island ids; diagnostics only borrows them. |
| stays on `PhysicsWorld` | `m_timeRemaining`; `m_tornadoGameplay`; `m_pointJointConstraints`; Debug-only `m_diagnosticsSuppressed` | `m_timeRemaining` is deliberately cross-stage CCD time written by narrowphase and terrain. Tornado and point-joint collections are already cohesive sibling domain owners outside this extraction order. The suppression flag remains the facade's scoped Debug override. |

Reconciliation: 58 direct data members mapped; 58 unique names; 0 duplicates;
0 omissions.

## Private Method And Functor Ownership

Every private method and callable functor declared in `PhysicsWorld` is assigned
exactly once. Nested plain-data types follow the owner of the methods that use
them.

| Target owner | Exact private methods / callables | Nested types carried with the owner |
|---|---|---|
| `PhysicsBroadphaseStage` | `BuildSolverBroadphaseCandidatePairs` | none |
| `PhysicsForceStage` | `PrepareMutualGravityForces`; `ApplyTornadoGameplay` | force-stage dispatch contexts/functors currently local to `PhysicsWorld.cpp` move in P1/P3 |
| `PhysicsNarrowphaseStage` | `RecordObjectNarrowphaseEvent`; `EmitObjectCollisionTimeEvent`; `MarkObjectVisualEvent`; `WriteObjectCollisionCellEvent`; `ProcessObjectNarrowphasePair`; `ProcessObjectNarrowphaseIsland`; `BuildObjectNarrowphaseIslands`; `ObjectNarrowphaseIslandStage::operator()`; `ObjectNarrowphaseIslandPrecedesByMinPairIndex` | `ObjectNarrowphaseEventKind`; `ObjectNarrowphaseEvent`; `ObjectNarrowphasePairStageContext`; `ObjectNarrowphaseIslandStage`; `ObjectNarrowphaseIsland` |
| `PhysicsTerrainStage` | `DetectTerrainAt`; `TerrainDetectionStage::operator()`; `CommitTerrainCandidate` | `TerrainDetectionCandidate`; `TerrainDetectionStageContext`; `TerrainDetectionStage`; `TerrainCandidateCommitContext` |
| `PhysicsContactSolverStage` | `CreatePersistentContactSolverContext`; `PreparePersistentContactSideEffects`; `ApplyPersistentContactSideEffects`; `ForgetPersistentContactCacheForBody` | solver context/side-effect seam types move out of the facade in P1/P6 |
| `PhysicsSleepController` | `RunSleepIslandStage`; `ApplySleepIslandTransitions`; `CreateSleepSupportPropagationContext`; `EnsureUnderwaterSleepLockBuffer`; `LockUnderwaterSleeperIfReady`; `IsUnderwaterSleepLocked`; `PropagateSleepSupport`; `AppendPointJointSupportEdges`; private six-argument `WakeModel`; `SeedModelAsleep`; `WakeDynamicBodyState`; `WakeSleepVisualIsland`; `WakePointJointIsland`; `WakeRestingContactIsland`; `IsPointJointPair`; `WakePointJointConnectedBodies` | sleep propagation seam types move out of the facade in P1/P7 |
| `PhysicsStepDiagnostics` | `EmitPhysicsCollisionTime`; `CanRecordPhysicsPipelineStage`; `RecordPhysicsPipelineStage`; `EnsureCollisionVisualBuffers`; `MarkCollisionVisualContact` | diagnostics contexts remain typed values/borrows |

P4 amendment: `CommitObjectNarrowphaseEvent` and the serial pair-order commit
loop remain on the `PhysicsWorld` sequencer until P7. Serial processing commits
each event immediately before the next pair, while parallel processing commits
completed pair slots in ascending candidate order. Moving that cross-domain
diagnostics/presentation commit into the stage now would either alter observable
timing or give the stage authority over owners scheduled for later extraction.
| stays on `PhysicsWorld` | `RunSolverPhysics` | top-level fixed-step sequencer only |

Reconciliation: 43 private method/callable declarations mapped; 43 unique
signatures; 0 duplicates; 0 omissions.

## Public Facade Routing

All public methods remain declared on `PhysicsWorld` for API stability. The
table freezes where each operation must terminate after extraction; entries
listed as “facade sequence” may call more than one concrete owner but may not
retain the stage's business state.

| Public facade surface | Terminal owner / role |
|---|---|
| `PhysicsWorld`; `ApplyRuntimeConfig`; `Clear`; `ReserveBodyScratchCapacity` | facade construction/configuration and per-owner lifecycle/reserve delegation |
| `RunPhysics` | facade sequence; `RunSolverPhysics` remains its internal sequencer |
| `ShouldEmitStepDiagnostics`; `ShouldEmitCollisionTimeDiagnostics`; `EmitStepDiagnostics`; `BeginCollisionVisualFrame`; `EndCollisionVisualFrame`; `GetCollisionVisualContacts`; `GetPhysicsDebugContacts`; `GetPhysicsPipelineTrace`; Debug-only path/run-id setters | `PhysicsStepDiagnostics` |
| both public `WakeModel` overloads; public `SeedModelAsleep`; `SetPhysicsSleepEnabled`; `IsPhysicsSleepEnabled`; `GetSleepStates`; `GetSleepIslandVisualIds`; `GetSleepSupportedStates`; `GetSleepInhibitedStates` | `PhysicsSleepController` |
| `ClearPointJointConstraints`; `DestroyPointJointsForBody`; `CreatePointJoint`; `GetPointJointConstraints` | facade-owned point-joint collection; wake connectivity is borrowed by `PhysicsSleepController` |
| tornado field/system setters/getters and `GetTornadoSystemElapsedSeconds` | existing facade-owned `TornadoGameplay` sibling owner |
| `CaptureReplaySolverSnapshot`; `RestoreReplaySolverSnapshot` | facade sequence across contact, sleep, point-joint, and tornado owners with typed snapshot values |
| `GetDiagnosticsView`; `CollectMemoryBytes`; `CollectDebugAndBroadphaseMemoryBytes` | facade aggregation of read-only owner views/counters |
| `GetSpatialGrid`; `GetCollisionCellKeys` | `PhysicsBroadphaseStage` |
| `GetFixedContactHighlightBodies`; `GetFixedTreeReleaseEvents` | `PhysicsContactSolverStage` side-effect view |
| Debug-only `SetDiagnosticsSuppressed` | facade-owned scoped suppression flag; forwards effective state to diagnostics without transferring flag ownership |

## Required Extraction Order

The map is frozen for P1-P10. Changes require an explicit amendment in the
owning task commit with a stay-behind reason and must preserve this order:

1. P1 prepares typed seams without moving behavior.
2. P2 broadphase.
3. P3 forces.
4. P4 narrowphase.
5. P5 terrain.
6. P6 contact solver.
7. P7 sleep.
8. P8 diagnostics.
9. P9 reduces `PhysicsWorld` to the fixed-step sequence and facade.
10. P10 performs the independent whole-module ownership review and final gates.

Campaign-wide invariants remain zero behavior change, byte-exact physics after
every task, no baseline refresh, no runtime allocation growth, and no stage
reach-back into `PhysicsWorld`.

# Contact Solve CS0 Census

Date: 2026-07-29
Source commit: `4819ffd0`
Branch: `nightrunner-29th-JUL-26`
Scope: `PhysicsContactSolverStage::Solve`

## Result

CS0 records the existing solve exactly as it stands before extraction:
1,721 inclusive body lines, maximum brace depth 7, 28 lexical closures, and
thirteen ordered phase boundaries. No source or runtime behavior changed.

The future transaction must own the phase cursor, exclusive mutation of the
solver-body working set, and the only impulse-application path. Persistent
contacts, prior/next-frame cache rows, statistics, and consequence batches
remain owned by `PhysicsContactSolverStage`; the transaction may borrow them
synchronously for the current solve but must retain no owner pointer or
cross-call reference.

## Closure Census

`[&]` below is expanded to the values actually used by the closure. `this`
names the specific stage fields reached through the implicit object capture.

| # | Line | Closure | Effective captures | Classification | Owning phase |
|---:|---:|---|---|---|---|
| 1 | 141 | `CanRecordPhysicsPipelineStage` | `pipelineTraceCanRecord` | Side-effect publication | Entry/setup; used across publishing phases |
| 2 | 143 | `RecordPhysicsPipelineStage` | `sideEffects.pipelineRecords`, `pipelineRecordCapacity`, `pipelineTraceCanRecord` | Side-effect publication | Entry/setup; used across publishing phases |
| 3 | 154 | `MarkCollisionVisualContact` | `sideEffects.collisionVisualBodies` | Side-effect publication | `BuildManifolds` |
| 4 | 156 | `MarkFixedContact` | `sideEffects.fixedContactBodies` | Side-effect publication | `DebugContacts` |
| 5 | 158 | `QueueReleaseWake` | `sideEffects.releaseWakeBodies` | Side-effect publication | `FixedContactRelease` |
| 6 | 160 | `QueueFixedTreeRelease` | `sideEffects.fixedTreeReleases` | Side-effect publication | `FixedContactRelease` |
| 7 | 190 | `isFixedBody` | `hotFields.fixed` | Shared arithmetic/predicate | Setup, row construction, post-solve |
| 8 | 261 | `makeKey` | none | Shared arithmetic | `BuildManifolds`, `TerrainRows`; called by cache lookup |
| 9 | 283 | `hasCachedImpulse` | `makeKey`, `this->m_persistentContactCache` | Shared arithmetic | `BuildManifolds` row reduction |
| 10 | 288 | cache lower-bound comparator | none | Pass-local | Inside `hasCachedImpulse` |
| 11 | 348 | pre-lookup cache sort comparator | none | Pass-local | Entry/setup |
| 12 | 353 | debug sortedness comparator | none | Pass-local | Entry/setup, Debug-only assertion |
| 13 | 364 | `applyInvInertia` | `this->m_solverBodies` | Shared arithmetic | `Precompute`, `SolveRows` through impulse application |
| 14 | 394 | `applyImpulse` | `this->m_solverBodies`, `applyInvInertia` | Shared arithmetic | `Precompute`, `SolveRows` |
| 15 | 409 | `conservativeContactRadius` | none | Shared arithmetic | `BuildManifolds`, `PointSupportInstability` |
| 16 | 429 | `contactBodyViewForIndex` | `hotRead` | Pass-local | `BuildManifolds` |
| 17 | 444 | `appendSleepSupportEdge` | `sleepSupportEdges`, `hotRead`, `CanRecordPhysicsPipelineStage`, `RecordPhysicsPipelineStage` | Side-effect publication | `BuildManifolds` |
| 18 | 498 | `deterministicTangentAxis` | none | Pass-local | `PointSupportInstability` |
| 19 | 536 | `applyPointSupportInstability` | `this->m_solverBodies`, `this->m_persistentRestingContactCounts`, `modelCount`, `isFixedBody`, `sleepState`, `colliderRecords`, `sleepSupportedThisFrame`, `stepPolicy`, `conservativeContactRadius`, `bodyRecords`, `dt`, `deterministicTangentAxis` | Pass-local | `PointSupportInstability` |
| 20 | 646 | `objectContactRowsAreQuiet` | `this->m_solverBodies`, `stepPolicy` | Pass-local | `BuildManifolds` |
| 21 | 674 | `reduceObjectContactRows` | `hasCachedImpulse` | Pass-local | `BuildManifolds` |
| 22 | 677 | `betterPenetrationTie` | `manifold` parameter | Pass-local | Inside `reduceObjectContactRows` |
| 23 | 1129 | `applyInvInertiaA` | current `c`, `applyInvInertia` | Pass-local adapter | `Precompute` |
| 24 | 1130 | `applyInvInertiaB` | current `c`, `applyInvInertia` | Pass-local adapter | `Precompute` |
| 25 | 1264 | warm-cache lower-bound comparator | none | Pass-local | `Precompute` |
| 26 | 1501 | collision-shape radius visitor | none | Pass-local | `TerrainRestPolicy` |
| 27 | 1764 | next-cache sort comparator | none | Pass-local | `CacheStore` |
| 28 | 1771 | `releaseFixedContactBody` | `modelCount`, `bodyRecords`, `hotRead`, `hotFields`, `bodyStore`, `isFixedBody`, `QueueReleaseWake`, `QueueFixedTreeRelease` | Side-effect publication | `FixedContactRelease` |

The five named shared-arithmetic targets from the plan are present exactly:
`makeKey`, `hasCachedImpulse`, `applyInvInertia`, `applyImpulse`, and
`conservativeContactRadius`. `isFixedBody` is the additional shared read-only
predicate. The two `applyInvInertiaA/B` closures are immediate pass-local
adapters; preserving them as one-call extracted helpers would not satisfy the
complexity or extraction-scar rules.

## Cross-Phase State

| State | First authority | Later consumers/mutators |
|---|---|---|
| `m_solverBodies` | `BodySetup` creates the per-body velocity, inverse-mass/inertia, and orientation working set | Read by manifold quietness and `Precompute`; mutated by warm start, `SolveRows`, point-support instability, and terrain rest policy; consumed by `WriteBack` |
| `m_persistentContacts` | Cleared in entry/setup; object rows appended in `BuildManifolds`, terrain rows in `TerrainRows` | Row fields filled in `Precompute`, impulses accumulated in `SolveRows`, then read by every post-solve phase |
| `m_persistentContactCache` | Prior-frame cache sorted/read by setup, row reduction, and `Precompute` | Destroyed and rebuilt only by `CacheStore`; retained for the next fixed tick |
| `m_persistentContactCounts` | Reset in setup; incremented by object row construction | Read by `Precompute` to divide object friction mass |
| `m_persistentRestingContactCounts` | Reset in setup; incremented by stable object rows | Read by `PointSupportInstability` to reject already stable bodies |
| `m_persistentContactSolverStats` | Reset in setup with previous-cache count | Updated by `Precompute`, `SolveRows`, and `PositionCorrection`; published after the solve |
| `hotFields` / `hotRead` | Synchronous body-store view acquired in setup | Pose, mass-property, fixed-state, and velocity reads span phases; velocity writes occur in `WriteBack`, position writes in `PositionCorrection`, fixed release in the final phase |
| `bodyRecords` | Synchronous mutable record span acquired in setup | World-inertia and mass reads feed setup/precompute; rest and release policy reads feed post-solve; final fixed release mutates through `PhysicsBodyStore` |
| `colliderRecords` | Synchronous const collider span acquired in setup | Geometry in `BuildManifolds`, restitution in `Precompute`, hull/radius policy in point support and terrain rest |
| `sleepState` | Borrowed at entry | Gates body setup, row construction, point support, terrain rest, velocity writeback, and position correction |
| `terrainContactManifolds` | Borrowed at entry | Converted to rows in `TerrainRows`, then reused for `TerrainRestPolicy` |
| `terrainRestApplied` | Borrowed at entry | Cleared and written only by `TerrainRestPolicy` |
| `sleepSupportedThisFrame` | Borrowed at entry | Read by `PointSupportInstability` |
| `sleepSupportEdges` | Borrowed at entry | Appended by `BuildManifolds` through `appendSleepSupportEdge` |
| `physicsDebugContacts` | Borrowed from diagnostics at entry | Cleared on early exits and rebuilt by `DebugContacts` |
| `m_sideEffects` and `pipelineTraceCanRecord` | Batch cleared and capacity-checked in entry/setup | Deterministic append-only publication across construction, solve, writeback, correction, cache, debug, and release phases |
| Step-policy scalars and `dt` | Normalized/derived in entry/setup | Read by construction, precompute, solve, point-support, terrain-rest, and correction phases without later mutation |

## Phase Read/Write Map

The required legal order is:

`EntryPolicySetup -> BodySetup -> BuildManifolds -> TerrainRows -> Precompute
-> SolveRows -> PointSupportInstability -> TerrainRestPolicy -> WriteBack
-> DebugContacts -> PositionCorrection -> CacheStore -> FixedContactRelease`.

| Phase | Reads | Writes |
|---|---|---|
| `EntryPolicySetup` | body/collider counts and views, step policy, `dt`, prior cache, candidate/manifold emptiness, diagnostics capacity | clears/prepares consequence batch; resets stats and count arrays; clears current rows; may clear cache/debug rows and return |
| `BodySetup` | body records, hot pose/velocity/inverse fields, fixed flags, sleep state | assigns `m_solverBodies` |
| `BuildManifolds` | candidate pairs, collider shapes, hot poses, solver-body quietness, prior cache, fixed/sleep state, contact policy | appends object rows; increments both count arrays; appends pipeline/collision/sleep-support consequences |
| `TerrainRows` | terrain manifolds, body mass, sleep state, gravity policy, `dt` | appends terrain rows and pipeline records |
| `Precompute` | rows, solver bodies, prior cache, body/collider data, contact counts, policy scalars, hot poses | fills tangent/effective-mass/bias/friction/cache/pre-solve row fields; applies warm impulses to solver bodies; updates cache stats and pipeline records |
| `SolveRows` | prepared rows, solver bodies, iteration/friction policy, hot poses | accumulates normal/tangent impulses; mutates solver velocities; updates iteration stats and pipeline records |
| `PointSupportInstability` | solved rows, solver bodies, body/collider data, rest counts, fixed/sleep/support state, policy, `dt` | deterministic angular nudge in selected solver bodies |
| `TerrainRestPolicy` | terrain manifolds, solver bodies, body/collider data, fixed/sleep state, rolling/gravity policy, `dt` | clears/sets `terrainRestApplied`; damps or zeros solver-body velocity |
| `WriteBack` | solver bodies, hot poses, fixed/sleep state | writes hot linear/angular velocity fields and pipeline records |
| `DebugContacts` | solved rows, hot poses, fixed flags | rebuilds debug-contact rows; appends fixed-contact consequences |
| `PositionCorrection` | solved rows, hot positions/inverse mass/fixed flags, sleep state, slop/correction policy | writes hot positions; updates correction stats and pipeline records |
| `CacheStore` | final accumulated row impulses and hot poses | clears/rebuilds/sorts next-frame cache; appends cache pipeline records |
| `FixedContactRelease` | final object rows, body records, hot velocity/radius, fixed/release policy | releases fixed bodies through `PhysicsBodyStore`; appends wake and fixed-tree release consequences |

The root profiler literal remains
`Frame/Physics/Narrowphase/PersistentContacts`. Every named phase scope above,
the two `Frame/Physics/Terrain` parent scopes, and the three nested
`BuildManifolds` scopes (`ExactObjectManifold`, `AddRows`,
`ContactRowReduction`) are byte-sensitive instrumentation that must move with
their bodies and retain their exact strings.

## Transaction Authority Decision

CS1 must install one non-copyable, stack-lifetime transaction with:

- a phase cursor whose legal states are the thirteen phases above plus
  completion, with lane-F fatal on every out-of-order transition;
- owned `SolverBodyStateList` working storage and the only methods allowed to
  apply inverse inertia or impulses to that storage;
- shared key/cache/radius arithmetic as transaction methods or honest
  invariant-owning collaborators, never courier aggregates or one-call helpers;
- synchronous phase-method borrows for stage-retained rows, cache, counts,
  statistics, body/collider views, sleep/terrain spans, diagnostics, and
  consequence outputs; and
- no retained `PhysicsContactSolverStage`, `PhysicsWorld`, store, span, callback
  pack, or participant-slice pointer after a phase method returns.

`PhysicsContactSolverStage` remains the persistent owner of current/next-frame
rows, cache, statistics, counts, and consequence storage. `PhysicsWorld`
remains the caller/sequencer that commits the published consequence batch after
`Solve` returns.

## CS0 Byte-Exact Oracle

`tools\validate_physics.bat` built and ran the final Debug executable, completed
the standalone lifecycle smoke, generated two deterministic varied-scene runs,
and matched both runs byte-for-byte to the committed 44,401-row baseline.

The generated CS0 artifact is preserved locally at
`TestOutput/validation/contact_solve_cs0/physics_regression_varied.csv`. It is
ignored validation output, not a baseline refresh. Later CS1-CS4 task boundaries
must regenerate the Debug output and match this hash as well as pass the
committed-baseline checker.

| Artifact | Bytes | Rows | SHA-256 |
|---|---:|---:|---|
| `Debug/SKULLBONEZ_CORE.exe` | 12,367,872 | n/a | `4b39f3fe974298caeaa81ed5bbfaf1f5a6c27c3438c2d83c14c2a60b9cc9e749` |
| CS0 two-run output | 12,660,434 | 88,802 | `8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387` |
| committed one-run baseline | 6,330,217 | 44,401 | `dc273c8d6cba688e71967a100d0c65a084591f78a252b5289f213b9bc8d4afe9` |
| `physics_bench_varied.scene.json` | 13,875 | n/a | `86c2bfed7f9333e528324dcab62027773000ad4f19b0988a760d89a172d6e994` |
| `SkullbonezData/engine.cfg` | 11,886 | n/a | `541816eec32f361ccfeb1ad9b6719f8db0d70cd75d1f10b10db253f577bac83d` |

Validation result: `tools\validate_physics.bat` PASS in 24.4 seconds on the
successful run, with zero build warnings/errors and no baseline changes.

The first invocation was terminated by its five-second command-wrapper timeout.
Its child build briefly retained the Debug DXC DLLs, so the immediate retry
reported a post-build copy lock. No source or artifact was changed to work
around it; after the timed-out process unwound, the same gate passed normally.

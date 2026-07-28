# Physics Fixed List Copy Contract — FC1 Owner Clones

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Status: FC1 complete; plan 2/3, Principal Engineer Feedback Campaign 2/16 (13%)

## Outcome

`PhysicsFixedList` no longer exposes copy construction, copy assignment, move
construction, or move assignment. `PhysicsEngine` and `ColliderStore` also make
their non-transferable public contracts explicit.

Replay prediction no longer performs
`predictionEngine = liveEngine`. It calls the narrowly named
`PhysicsEngine::SeedReplayPredictionStorageFrom()` while the existing
Replay-prediction allocation phase and exact
`replay_prediction_working_set` reserve owner are active. When the seed must
expand retained backing, that expansion also occurs inside the allocator's
granted growth scope. The seed coordinates private concrete-owner operations;
there is no public list clone, generic `PhysicsEngine::Clone`, context bag,
service bag, or forwarding compatibility seam.

## Owner And Phase Contract

Every seed/clone entry checks all of the following before touching backing
storage:

- the calling-thread allocation phase is `Replay`;
- a reserve owner is active;
- that owner is registered to the Replay subsystem; and
- the owner is permitted Replay growth; and
- the registered owner name is exactly
  `replay_prediction_working_set`.

Production reaches the operation only from `SeedReplayPredictionEngine()` under
`ReplayPredictionReserveOwner()`. The name check is an exact allocator-registry
policy identity, not an unforgeable capability: the allocator's public
registration surface remains the authority that binds the canonical name to
its descriptor. Same-capacity reseeds do not claim a granted growth scope.
When capacity must expand, the production caller enters the granted scope and
`PhysicsFixedList::Reserve()` independently rechecks growth authority. A
same-instance engine seed is fatal.

The concrete operations are private to `PhysicsEngine`:

- `PhysicsBodyStore` clones its cold record, all aligned SoA fields, and stable
  handle maps as one owner transaction;
- `ColliderStore` clones all hot, authoring, handle, and per-kind shape rows,
  then calls `RebindShapeReferences()` so no prediction row points into the live
  engine;
- `BuoyancySystem` clones the fluid-fact rows aligned with those body/collider
  rows; this is concrete subsystem state, not a new generic clone surface;
- `PhysicsWorld` clones only terrain/topology and sequencing values outside
  `PhysicsSolverSnapshot`, including point-joint state. The existing snapshot
  restore remains authoritative for time, broadphase keys, sleep, diagnostics,
  persistent contacts, and transient solver/stage rows.

The engine seed also copies authored body descriptors and engine-owned policy,
force, wake, and query values. It preserves the persistent topology and policy
content prediction requires without restoring implicit whole-engine value
semantics. Snapshot restore supplies solver-owned state, while explicit
transient reset/rebuild owns fresh or reused stage scratch and Debug-oracle tick
continuity.

The seed is synchronous but intentionally partial. Replay prediction keeps
`predictionEngineReady` false across concrete-owner seeding, body-value
restore, and solver-snapshot restore; worker consumers test that readiness
fence, and publication occurs only after both restores succeed. No intermediate
engine can escape to a consumer, so FC1 did not add a transaction type.

## Deleted Test-Only Transfer Surfaces

The custom `ColliderStore` copy/move test, direct fixed-list copy/move tests,
publisher-token move test, and copy/move exception-cleanup paths were removed
because they exercised the deleted API. The remaining relocation exception
test still proves strong cleanup during backing growth.

FC2 owns the permanent compile-time trait assertions, legal seed-content and
collider-rebind proof, and child-process fatal coverage for missing/wrong phase
authority. FC1 already proves that a different valid Replay growth owner
(`replay_solver_snapshot`) is rejected by the exact-owner guard. The focused
FC1 legal seed test constructs the canonical allocator row from the shared
production owner name/hard cap and the production descriptor because the CPU
test binary does not link `ReplayPredictionReserve.cpp`. FC2 must exercise
`SeedReplayPredictionEngine()` through the production
`ReplayPredictionReserveOwner()`/growth-request adapter so that coordinator
coverage can detect policy drift.

## Validation

Focused validation was selected for FC1; the plan maps broad Physics, Replay,
performance, and full gates to FC2 closure.

| Check | Result |
|---|---|
| `tools\validate_build.bat Profile` before formatting | Pass, 33.3 s, zero warnings/errors |
| Final `tools\validate_build.bat Profile` | Pass, 31.5 s, zero warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe -tc=PhysicsFixedList:*` | Pass, 6/6 cases and 68 assertions |
| `Profile\SKULLBONEZ_TESTS.exe -tc=Prediction physics seed*` | Pass, 1/1 case and 11 assertions |
| `Profile\SKULLBONEZ_TESTS.exe -tc=Collider shape stores:*` | Pass, 1/1 case and 31 assertions |
| `Profile\SKULLBONEZ_TESTS.exe -tc=Runtime contracts: invalid broadphase and task lifetimes terminate in child probes` | Pass, 1/1 case and 175 assertions, including wrong-Replay-owner fatal proof |
| `tools\validate_project_filters.bat` | Pass, 785/785 project/filter items and zero errors |
| Authority-free aggregate inventory | Pass, 86/86 gated candidates ruled; 0 unruled |
| Extraction-scar inventory | Pass, 1/1 finding ruled |
| Wide-signature inventory | Pass |
| Allocation-policy repository scan | Pass; 0 allowlist errors |
| Exact touched-file formatting pipelines | Pass for 18 touched source/test files, including 9 headers |
| `git diff --check` | Pass |

The aggregate inventory required one mechanical location refresh for the
existing `BuoyancyBodyFacts` ruling after a forward declaration shifted its
line from 47 to 48; its owner verdict and reason are unchanged.

An early read-only global format check also reported the three intentionally
uncommitted warm-start files plus then-unformatted FC1 files. FC1 formatting
was corrected with exact-path tools only. The warm-start files were neither
formatted nor otherwise touched:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PersistentContactSolver.h`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`

No baseline or golden was refreshed.

## Comment Audit

The touched-source audit checked all 18 edited source/test files against
`Agentic/Reference/comment-style-guide.md` and
`Agentic/Skills/comment-style-audit/skill.md`: 18 checked, 0 deferred.
Learning headers and nearby clone ownership, phase, snapshot, shape-reference,
sleep-restore, readiness-fence, and identity invariants describe the post-change
owners. The audit also corrected two stale whole-engine-copy descriptions in
`ReplayPrediction.h` and `PhysicsSleepController.h`.

Independent review first found that a broad Replay-owner check would let an
unrelated growth owner authorize prediction seeding. FC1 replaced the repeated
checks with one Physics-internal exact canonical-owner guard and added the
wrong-owner fatal probe. Repeat review found the code contract correct; final
report and comment-truth review then caught and corrected the stale wording
recorded above.

## Questions And Blockers

No owner input is required for FC2. The FC0 owner ruling remains sufficient.

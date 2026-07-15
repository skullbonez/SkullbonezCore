# PhysicsWorld Stage-Owner Decomposition — Concrete Stage Owners, Zero Behavior Change

Date: 2026-07-15
Status: Draft — NOT yet registered in `MASTER-PLAN.md` (owner decision 2026-07-15:
plan written without modifying MASTER; task P0 performs registration when the
runner activates this plan). 0/11 tasks complete.
Impact area: `Physics/PhysicsWorld.{h,cpp}`, new `Physics/Stages/*` owners,
allocation-policy allowlist rows, physics determinism gates
Owner: physics

---

## Problem And Evidence

`PhysicsWorld.cpp` is 3,954 lines (2026-07-15 tip `2da6b219`) and the fixed-tick
step path (`RunPhysics` → `RunSolverPhysics`) runs as one continuous function
through force application, broadphase, narrowphase, terrain, the persistent
contact solver, point joints, integration, and sleep islands. State for every
one of those stages lives flat on one class: ~45 `m_*` vectors spanning at
least seven unrelated concerns (see the ownership map in P0). Stage boundaries
already exist as value structs (`ApplyForcesStageContext`,
`ObjectNarrowphasePairStageContext` with 19 fields,
`TerrainDetectionStageContext`, `TerrainCandidateCommitContext`,
`IntegrateRemainingStageContext`) but they are *parameter bundles*, not owners:
every stage's scratch is still `PhysicsWorld` member state and every stage
function is still a `PhysicsWorld` method.

This is the same disease the `Run` and `ReplayRuntime` campaigns cured:
one class owning the union of its stages' state. The 2026-07-15 adversarial
review (finding 1, god objects) named `PhysicsWorld.cpp` the largest remaining
mass after the round-4 signature work.

Why this campaign is safe to run: the byte-exact 44,401-line
`physics_regression_varied.csv` gate detects any behavioral drift totally and
cheaply (~60-85 s per run). Every task below ends with that gate and no task
is allowed to change what the bytes say.

## Goal

`PhysicsWorld` becomes the physics domain façade and step sequencer. Each
pipeline stage is a concrete owner type under `SkullbonezSource/Physics/Stages/`
that owns its scratch buffers, exposes one typed entry point consuming
explicit inputs and producing explicit outputs, and holds no pointer or
reference back to `PhysicsWorld`. `RunSolverPhysics` reads as a sequence of
stage calls. `PhysicsWorld.cpp` drops below ~1,000 lines. Physics baselines
remain byte-exact throughout — zero refresh authorized at any task.

## Non-Goals

- **No behavior change of any kind.** No float reordering, no loop-order
  change, no threshold change, no profiler-marker renames, no diagnostics
  emission-order change. Byte-exact CSV at every task is the proof.
- **No SoA/SIMD restructuring** (future lane; this plan produces the stage
  shells that lane will fill).
- **No TU split of the `PhysicsWorld` class across sibling files.** One class
  smeared over `PhysicsWorld.Broadphase.cpp`-style TUs is the anti-pattern
  this plan exists to avoid (AGENTS god-object closure rule: a mechanical
  translation-unit split is a closure failure).
- No public physics API break: `PhysicsScene`/`PhysicsApi`/runtime callers of
  `RunPhysics`, `WakeModel`, `SeedModelAsleep`, point-joint and
  collision-visual APIs keep their signatures; `PhysicsWorld` delegates to the
  concrete owners (delegation at the domain façade is the accepted owner
  pattern, not a banned forwarding relay — record this in P0 so review does
  not re-litigate it).
- No new inheritance, callbacks, `*Sink`/`*Bridge` interfaces, or `void*` on
  the hot path (hot-path data rule applies in full).

## Binding Design Rules (every task must respect all of these)

1. **Stage owners are plain structs/classes with value-context inputs.** Shape:
   `struct NarrowphaseStage { <owned scratch vectors>; Outputs Run( const Inputs&, WorkerPool& ); }`.
   Inputs reference stores/config/records for the duration of the call only;
   nothing is retained. `static_assert` nothing — just never store a reference
   member to `PhysicsWorld` or another stage.
2. **Scratch moves with its stage.** A vector may move out of `PhysicsWorld`
   only into the stage that is its sole writer. If two stages write it, it is
   cross-stage frame state and stays on `PhysicsWorld` (documented with an
   `Invariant:` comment naming both writers). Known cross-stage state that
   STAYS on `PhysicsWorld`: `m_timeRemaining` (written by CCD, terrain, and
   integration), the sleep flag mirror `m_sleepState` (read by force/
   narrowphase/gravity, written by sleep) — P7 decides its final home.
3. **Identical iteration and emission order.** Extracted code keeps loop
   bounds, `assign`/`clear` patterns, worker-dispatch thresholds, chunk
   constants, profiler marker strings, and diagnostics emission points
   character-identical unless the task explicitly says otherwise (none does).
4. **Allocation policy follows ownership.** Any vector that moves owners
   updates the matching row in `tools/allocation_policy_allowlist.json` in the
   same commit (owner string changes from `PhysicsWorld ...` to the stage
   owner), and that commit runs
   `python tools\check_allocation_policy.py --self-test` and `--repo .` per
   the file-to-gate mapping.
5. **One stage owner per task, one commit per task.** Every task ends with
   `tools\validate_physics.bat` passing byte-exact against the unchanged
   committed baseline, pasted into the commit body. A failed byte-compare
   reverts the task — do not "fix forward" a float diff; it means the
   extraction was not mechanical.
6. **Comment standard applies**: each new stage header/impl gets the learning
   header plus `Concept:`/`Invariant:`/`Lifetime:`/`Hazard:` comments; run the
   touched-file comment audit before each commit.
7. **No baseline refresh under any circumstances.** If a task cannot pass
   byte-exact, the task is wrong, not the baseline.

## Task Checklist

- [ ] **P0 — Certification, ownership map, and MASTER registration.**
      (a) Register this plan in `MASTER-PLAN.md`: ledger row (0/11), round/
      section entry, and critical-path placement after the round-5 lane;
      update `SessionState.md`. (b) Certify the starting tree: run
      `tools\validate_physics.bat` and `tools\validate_perf.bat` on the
      unmodified tip and record outputs (P10 compares against these).
      (c) Commit the frozen ownership map as
      `Agentic/Reports/2026-07-15/physicsworld-ownership-map.md`: every
      `PhysicsWorld` member and private method assigned to exactly one target
      owner from the table below, including the deliberate stay-behind list
      (`m_timeRemaining`, façade API, `m_diagnosticsSuppressed`). Every later
      task works strictly from this map; discovering an unmapped member
      mid-task means stop, amend the map in its own commit, then continue.
      (d) Record the delegation ruling from Non-Goals so the P10 review has
      the owner decision in writing.

      | Target owner (new file under `Physics/Stages/`) | Members (from tip `2da6b219`) | Methods/functors |
      |---|---|---|
      | `PhysicsBroadphaseStage` | `m_spatialGrid`, `m_candidatePairs`, `m_collisionCellKeys` | `BuildSolverBroadphaseCandidatePairs` |
      | `PhysicsForceStage` | `m_mutualGravityForces`, `m_mutualGravityPairForces`, `m_mutualGravityPairHighWater` | `PrepareMutualGravityForces`, `ApplyForcesStageContext` dispatch, `ApplyTornadoGameplay` call (owns nothing of `m_tornadoGameplay` — it stays a sibling owner on `PhysicsWorld`) |
      | `PhysicsNarrowphaseStage` | `m_objectNarrowphaseEvents`, `m_objectNarrowphaseIslands`, `m_objectNarrowphaseIslandPairIndices`, `m_objectNarrowphaseIslandWriteOffsets`, `m_objectNarrowphaseParent`, `m_objectNarrowphaseRank`, `m_objectNarrowphaseRootToIsland` | `ProcessObjectNarrowphasePair(sSerial)`, `ProcessObjectNarrowphaseIsland`, `BuildObjectNarrowphaseIslands`, `RecordObjectNarrowphaseEvent`, `EmitObjectCollisionTimeEvent`, `WriteObjectCollisionCellEvent`, island ordering predicate |
      | `PhysicsTerrainStage` | `m_terrainDetectionCandidates`, `m_terrainContactManifolds`, `m_terrainRestApplied` | `DetectTerrainAt`, `TerrainDetectionStage` functor, `CommitTerrainCandidate` |
      | `PhysicsContactSolverStage` | `m_persistentContacts`, `m_persistentContactCache`, `m_persistentContactCounts`, `m_persistentRestingContactCounts`, `m_solverBodies`, `m_persistentContactSideEffects`, `m_persistentContactSolverStats` (wraps existing `m_contactSolver`) | `CreatePersistentContactSolverContext`, `PreparePersistentContactSideEffects`, `ApplyPersistentContactSideEffects`, `ForgetPersistentContactCacheForBody` |
      | `PhysicsSleepController` | `m_sleepState`, `m_sleepCounter`, `m_sleepSupportedThisFrame`, `m_sleepInhibitedThisFrame`, `m_underwaterSleepLocked`, `m_sleepSupportEdges`, `m_sleepIsland*` (parent/rank/hasAwake/hasSupportAnchor/eligible/canSleep/visual ids), `m_sleepPointJointBody`, `m_sleepIslandHasPointJoint`, `m_sleepIslandPointJointsRelaxed`, `m_sleepVisualIsland*`, `m_nextSleepIslandVisualId`, `m_sleepEnabled`, `m_seedSleepFrameCount` (wraps existing `m_sleepIslandSystem`) | `RunSleepIslandStage`, `ApplySleepIslandTransitions`, `PropagateSleepSupport`, `AppendPointJointSupportEdges`, all `Wake*`/`SeedModelAsleep` internals, underwater-lock methods, `IsPointJointPair`, `WakePointJointConnectedBodies` |
      | `PhysicsStepDiagnostics` | `m_collisionVisualContacts`, `m_collisionVisualFrameActive`, `m_sleepIslandVisualId` mirror duties as mapped, `m_physicsDebugContacts`, `m_physicsPipelineTrace` (wraps existing `m_diagnostics`) | `EmitPhysicsCollisionTime`, `EmitStepDiagnostics` plumbing, `CanRecord/RecordPhysicsPipelineStage`, `EnsureCollisionVisualBuffers`, `MarkCollisionVisualContact`, `BeginEndCollisionVisualFrame` internals |
      | stays on `PhysicsWorld` | `m_timeRemaining`, `m_tornadoGameplay`, `m_pointJointConstraints`, `m_restingWake*Scratch` (P7 may claim), `m_diagnosticsSuppressed` | `RunPhysics`, `RunSolverPhysics` (sequencer), public façade API, `ApplyRuntimeConfig`, `Clear`, `ReserveBodyScratchCapacity` (delegates per-owner reserves) |

- [ ] **P1 — Mechanical seam preparation (no ownership moves).** Create
      `SkullbonezSource/Physics/Stages/` and move the stage *context structs
      and functors* currently declared in `PhysicsWorld.h`'s private section
      (`ApplyForcesStageContext`, `ObjectNarrowphasePairStageContext`,
      `ObjectNarrowphaseIslandStage`, `TerrainDetectionStageContext`/`Stage`,
      `TerrainCandidateCommitContext`, `IntegrateRemainingStageContext`,
      narrowphase event/island structs) into per-stage headers
      (`Stages/PhysicsStageContexts.h` or one header per future owner —
      choose one layout and record it). `PhysicsWorld` includes them; zero
      logic edits; vcxproj/filters updated in the same commit. Purpose: later
      tasks move code between files that already exist, keeping every
      ownership diff small and reviewable.
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P2 — Extract `PhysicsBroadphaseStage`.** New owner holds the spatial
      grid, candidate-pair vector, and cell-key scratch;
      `Run( bodyStore, bodyRecords, colliderRecords, config, modelCount, dt, contactSkin )`
      returns `std::span<const std::pair<int,int>>` over its own storage,
      valid until the next `Run`. `PhysicsWorld` keeps a
      `PhysicsBroadphaseStage m_broadphase;` member and passes the span to
      narrowphase exactly where `m_candidatePairs` flowed before. Grid
      accessors used elsewhere (`GetCellSize` for `invCellSize`) get a
      const accessor on the stage. Move the allowlist rows; run the
      allocation-policy self-test/repo check.
      Hazard: `SpatialGrid` files themselves are NOT edited (their mapped
      gate adds `validate_perf`; avoid triggering it in this task).
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P3 — Extract `PhysicsForceStage`.** Owns the mutual-gravity buffers
      and high-water counter; entry points
      `PrepareMutualGravityForces(...)` (same signature shape, minus `this`)
      and `ApplyForces( const ApplyForcesStageContext&, WorkerPool&, const ExecutionToggles& )`
      preserving the exact parallel/serial dispatch, thresholds
      (`MUTUAL_GRAVITY_MAX_BODIES/ROWS_PER_CHUNK/MAX_CHUNKS/PARALLEL_MIN_BODIES`,
      `PHYSICS_PARALLEL_MIN_BODIES`), worker-hash constants, and marker
      strings. **Dependency:** the round-5
      `mutual-gravity-large-scene-fallback` plan edits this same function —
      that plan lands FIRST and this task moves the post-fallback code (the
      serial >512-body path moves with it). Move the gravity allowlist rows.
      Gate: `tools\validate_physics.bat` byte-exact; also rerun the 0/1/4-worker
      mutual-gravity determinism test target.

- [ ] **P4 — Extract `PhysicsNarrowphaseStage`.** Owns all seven
      `m_objectNarrowphase*` buffers plus island build/dispatch. Critical
      boundary: `CommitObjectNarrowphaseEvent` currently mutates sleep/wake
      and visual state — that is cross-domain output, not narrowphase scratch.
      Shape: workers fill the stage-owned `ObjectNarrowphaseEvent` array
      exactly as today; the *commit loop stays in `PhysicsWorld`* (sequencer)
      in this task, iterating events in pair order and calling the same
      wake/visual/cache internals. (P7 later re-homes those internals into
      `PhysicsSleepController`; the commit loop then calls sleep-owner APIs.
      Do not attempt both moves in one task.) Island worker enable constants,
      min/max pair thresholds, and the min-pair-index ordering predicate move
      verbatim. Serial fallback path preserved.
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P5 — Extract `PhysicsTerrainStage`.** Owns detection candidates,
      terrain manifolds, and `m_terrainRestApplied`; `Detect(...)` runs the
      parallel/serial candidate pass, `Commit(...)` runs the serial
      model-order commit producing manifolds and sleep-support marks. The
      sleep-support outputs (`m_sleepSupportedThisFrame`/`m_sleepInhibitedThisFrame`)
      are NOT terrain state — they remain `PhysicsWorld`-owned until P7, and
      the commit context keeps referencing them exactly as today.
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P6 — Extract `PhysicsContactSolverStage`.** Wraps the existing
      `PersistentContactSolver` with its feeding state: persistent rows,
      cache, counts, solver bodies, side-effect queue, stats.
      `Solve( bodyStore, colliderStore, config, worldForces, dt )` performs
      today's Prepare → `m_contactSolver.Solve` → Apply sequence internally;
      `ForgetPersistentContactCacheForBody` becomes a stage API called by the
      façade's body-retirement path. Side-effect application order and the
      wake calls it makes are unchanged (wake internals still live where P4
      left them until P7). Move the matching allowlist rows.
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P7 — Extract `PhysicsSleepController` (the big one — budget it as the
      longest task).** All ~20 sleep vectors, the island system wrapper, wake/
      seed internals, underwater locks, point-joint sleep metadata, and
      support propagation move behind one owner with explicit APIs:
      `MirrorFlagsFrom( bodyStore )`, `WakeModel`, `SeedModelAsleep`,
      `LockUnderwaterSleeperIfReady`, `PropagateSupport`, `RunIslandStage`,
      `ApplyTransitions`, plus const views for the read-only consumers
      (`m_sleepState` reads in force/gravity/narrowphase stage contexts become
      `std::span<const uint8_t>` inputs supplied by the sequencer from this
      owner — same values, same time of read). Public `PhysicsWorld::WakeModel`
      / `SeedModelAsleep` / `SetPhysicsSleepEnabled` delegate. P4's deferred
      commit-loop internals re-home here now. `m_restingWake*Scratch` moves
      here (sole writer). This task may NOT also shrink or reorganize sleep
      logic internally — move only.
      Gate: `tools\validate_physics.bat` byte-exact, plus
      `tools\validate_physics_deep.bat` once (sleep/island behavior is the
      highest-risk move; the deep gate's sleep-sensitive scenes are the extra
      tripwire).

- [ ] **P8 — Extract `PhysicsStepDiagnostics`.** Collision-visual buffers and
      frame flag, debug contacts, pipeline trace, and the emit/record helpers
      move behind the diagnostics owner (absorbing or wrapping the existing
      `PhysicsDiagnosticsSink`). Emission call sites in the stages/sequencer
      keep their exact positions and argument values; `m_diagnosticsSuppressed`
      stays on the façade and is passed as a bool input. Debug/Profile-only
      behavior differences must be preserved exactly (`#ifdef` boundaries move
      verbatim).
      Gate: `tools\validate_physics.bat` byte-exact (Debug artifacts included —
      the CSV writer path runs in Debug).

- [ ] **P9 — Sequencer cleanup and residue audit.** `RunSolverPhysics` now
      reads as: mirror sleep flags → force stage → broadphase → narrowphase
      (+ commit via sleep owner) → terrain → contact solver → point joints →
      integrate remaining (`IntegrateRemainingStageContext` dispatch may stay
      façade-owned or join the force stage — decide from the P0 map) → sleep
      islands → diagnostics flush. Verify against the P0 map that every
      member left on `PhysicsWorld` is on the stay-behind list with a
      documented reason; measure and record final line counts
      (`PhysicsWorld.cpp` target ≤ ~1,000; each stage TU ≤ ~700). Update
      `Agentic/Reference/physics-overview.md` stage diagram/text.
      Gate: `tools\validate_physics.bat` byte-exact.

- [ ] **P10 — Mandatory independent ownership review + full closure gates.**
      Independent rubber-duck review across the whole logical surface
      (`PhysicsWorld` + all stage owners as one module) against the god-object
      closure rule: no reach-back references, no authority bag, no forwarding
      that hides retained authority, no stage that absorbed unrelated domains.
      Any credible finding reopens the owning task. Then final gates from the
      closed source: `tools\validate_full.bat` (Common/Runtime-reachable
      header churn ⇒ broad gate), `tools\validate_perf.bat` compared against
      the P0 certification numbers (no regression beyond noise; record the
      physics-step average), and `python tools\check_allocation_policy.py --repo .`.
      Comment audit over every touched file (expect ~15-20). Closure report
      under `Agentic/Reports/`, MASTER/SessionState updated, plan deleted per
      inventory rule 4.

## Dependencies And Decisions

- **Blocked behind the round-5 lane**: `mutual-gravity-large-scene-fallback`
  must land before P3 (same function); `fp-envelope-hardening` should land
  before P0 certification so the perf/determinism reference numbers are taken
  under the pinned envelope. `math-fatal-survey-restoration` is unrelated.
- Owner ruling 2026-07-15 (recorded here for P0): stage owners with typed
  value boundaries, NOT a TU split of the class; façade delegation on the
  public physics API is accepted; `m_timeRemaining` and the tornado owner
  stay on the façade as documented cross-stage state.
- This plan deliberately produces the owner shells the future SoA/SIMD lane
  needs; do not pre-build SoA layouts here.
- Commit subjects follow the plan-runner progress-header contract once P0
  registers the ledger row; the P0 commit itself establishes the denominator.

## Acceptance

- Every P0-map row is realized or has a P0-amended stay-behind reason; no
  `m_*` member on `PhysicsWorld` lacks a mapped owner or documented reason.
- No stage owner holds a reference/pointer to `PhysicsWorld` or another stage
  (grep + review proof).
- `physics_regression_varied.csv` byte-exact at every single task; zero
  baseline refreshes in the whole campaign (git history is the proof).
- P10 independent review records zero credible ownership findings.
- `validate_perf` shows no physics-step regression vs the P0 certification.

## Validation

- Per task: `tools\validate_physics.bat` byte-exact (P7 adds one
  `tools\validate_physics_deep.bat`; allocation-moving tasks add the
  allocation-policy self-test/repo checks).
- Closure: `tools\validate_full.bat` + `tools\validate_perf.bat` + comment
  audit + independent review, outputs pasted in the P10 commit/report.

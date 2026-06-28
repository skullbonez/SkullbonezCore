# Carmack Physics Standalone Boundary Plan

Date: 2026-06-28
Status: In progress
Impact area: physics, runtime, scene system, replay, diagnostics, tests
Validation note: plan-only edits require no validation. PR-bound implementation
must use the smallest matching gate from `AGENTS.md`; physics-visible behavior
requires `tools\validate_physics.bat`, with `tools\validate_physics_deep.bat`
when SkullScope baselines or broad physics diagnostics change.

## Completed Slices

- [x] 2026-06-28: Added `GameModelCollectionPhysicsAdapter` as the named
  runtime/game-object compatibility bridge for physics commands. The adapter
  maps legacy model indices and `PhysicsSceneObjectId` values to
  `PhysicsBodyHandle`, rejects invalid or ambiguous identities before entering
  `PhysicsEngine`, and keeps current `GameModelCollection` wake, seed-asleep,
  immediate impulse, and pending impulse methods behind one deletion target.
  This does not complete replay/editor/runtime storage migration: those callers
  still present model indices until a later slice moves their state to durable
  physics handles. Rubber-duck review by Plato found no blockers and one
  non-blocking ambiguity risk: duplicate replay-derived scene object ids would
  silently select the first matching model. The adapter now fails closed on
  duplicate scene ids before final validation. Comment-style audit inspected
  `GameModelCollectionPhysicsAdapter.h`, `GameModelCollectionPhysicsAdapter.cpp`,
  `GameModelCollection.h`, `GameModelCollection.cpp`, and
  `tools\validate_project_filters.py`; the touched source files now carry the
  bridge/deletion-target vocabulary, invalid/ambiguous identity invariant, and
  old-wrapper compatibility rationale. Targeted pre-review checks:
  `git diff --check` passed
  aside from expected project-file line-ending warnings,
  `python tools\validate_project_filters.py` passed with `0 errors`,
  `python tools\check_runtime_boundaries.py` passed with `0 errors`, and
  `tools\validate_build.bat Profile` passed with `0 Warning(s)` and
  `0 Error(s)`. Final validation: `tools\validate_fast.bat` passed with source
  formatting clean, project filters `0 errors`, runtime boundaries `0 errors`,
  Profile/Debug builds at `0 Warning(s)` and `0 Error(s)`, and
  `VALIDATE_FAST: ALL PASSED`
  (`TestOutput\validation\agent_logs\game_model_physics_adapter_validate_fast_final.log`);
  `python tools\validate_project_filters.py` passed with `0 errors`
  (`TestOutput\validation\agent_logs\game_model_physics_adapter_validate_project_filters_final.log`);
  `tools\validate_physics.bat` passed with standalone smoke
  `lifecycle_checks=pass hash=0x32EC17812CDAA435`, byte-exact
  `physics_regression_solver.csv`, Profile/Debug builds ready, and
  `VALIDATE_PHYSICS: ALL PASSED`
  (`TestOutput\validation\agent_logs\game_model_physics_adapter_validate_physics_final.log`).
- [x] 2026-06-28: Added standalone handle-based activation commands to the
  public physics API beachhead. `PhysicsStandaloneWorld` now applies
  `PhysicsActivationCommand` without model-index lookup for `WakeBody`,
  `SeedBodyAsleep`, and `SetSleepEnabled`; fixed, stale, or invalid body
  targets fail without mutating storage, disabling sleep wakes all live bodies
  and makes later create/update/step/query paths treat bodies as awake, and
  `SleepEnabled()` exposes the standalone sleep gate. The standalone smoke
  now proves invalid command rejection, fixed-body rejection, seed-asleep
  velocity clearing, sleep-disable wake behavior, disabled-sleep seed rejection,
  sleep-disabled create/update/step/query behavior, re-enable/reseed/wake
  behavior, activation-body deletion, and stale-command rejection while
  preserving the main body's deterministic final state. Rubber-duck review
  initially found blocking durable sleep-gate and command-contract clarity
  gaps; both were fixed before commit.
  Comment-style audit: inspected `PhysicsApi.h` and `PhysicsApi.cpp`; the new
  glossary entries, API comments, and local invariant comment name the
  handle-only command path, sleep/wake vocabulary, `SetSleepEnabled` body-field
  contract, fixed/stale failure behavior, and sleep-disable wake semantics.
  Final
  validation: `tools\validate_fast.bat` passed with formatting clean, project
  filters `0 errors`, runtime boundaries `0 errors`, and Profile/Debug builds
  at `0 Warning(s)`/`0 Error(s)`
  (`TestOutput\validation\agent_logs\physics_standalone_activation_validate_fast_final.log`);
  `tools\validate_physics.bat` passed with standalone smoke
  `lifecycle_checks=pass hash=0x32EC17812CDAA435`, byte-exact
  `physics_regression_solver.csv`, and Profile/Debug builds at
  `0 Warning(s)`/`0 Error(s)`
  (`TestOutput\validation\agent_logs\physics_standalone_activation_validate_physics_final.log`).
- [x] 2026-06-28: Added standalone conservative query methods to the public
  physics API beachhead. `PhysicsStandaloneWorld::RayCast` now scans live
  collider bounding spheres in deterministic collider-slot order and returns
  `PhysicsBodyHandle`, `PhysicsColliderHandle`, scene object id, distance,
  point, and normal without exposing model indices. `QueryBroadphaseCells`
  returns deterministic body-handle candidates for bodies or colliders whose
  conservative spheres overlap an AABB. Conservative envelopes now include
  local shape-offset length, rotate collider offsets through body orientation,
  and clamp caller-provided collider broadphase radii upward when descriptors
  understate the safe radius. The standalone smoke now proves an oriented,
  local-offset collider with an intentionally undersized supplied broadphase
  radius is visible through both query paths; the hash records query hit/count,
  while lifecycle checks verify exact handles, point, normal, and scene id.
  Rubber-duck review initially found blocking false-negative risks in offset
  geometry and undersized cached radii; both were fixed before commit.
  Comment-style audit: inspected
  `PhysicsApi.h` and `PhysicsApi.cpp`; the new glossary/API comments name the
  conservative broadphase semantics, deterministic ordering, scratch-view
  lifetime, and future shape-specific replacement point. Final validation:
  `tools\validate_fast.bat` passed with formatting clean, project filters
  `0 errors`, runtime boundaries `0 errors`, and Profile/Debug builds at
  `0 Warning(s)`/`0 Error(s)`
  (`TestOutput\validation\agent_logs\physics_standalone_queries_validate_fast_final.log`);
  `tools\validate_physics.bat` passed with standalone smoke
  `lifecycle_checks=pass hash=0xF32B90EB03C9D5F0`, byte-exact
  `physics_regression_solver.csv`, and Profile/Debug builds at
  `0 Warning(s)`/`0 Error(s)`
  (`TestOutput\validation\agent_logs\physics_standalone_queries_validate_physics_final.log`).
- [x] 2026-06-28: Extended the standalone public physics API slice with
  collider handles and immutable collider views. `PhysicsStandaloneWorld` now
  supports collider create, masked update, destroy, single-collider query, and
  deterministic collection query without `GameModelCollection`; invalid body
  handles return invalid collider handles, direct collider deletion advances the
  collider generation, and body deletion tombstones child colliders so stale
  collider handles fail predictably. The standalone smoke now exercises body
  and collider stale-handle behavior and preserves the exact body final state
  while updating the deterministic smoke hash to `0xB7270DA8DDE3289E`.
  Comment-style audit: inspected `PhysicsApi.h` and `PhysicsApi.cpp`; the
  learning headers already describe standalone handles, views, stale-handle
  generations, and deterministic smoke expectations, and the new collider
  methods carry local lifecycle comments.
  Targeted implementation check:
  `tools\validate_build.bat Debug` passed with 0 warnings/errors
  (`TestOutput\validation\agent_logs\physics_standalone_collider_validate_build_debug_during_impl.log`);
  `Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke` passed with
  `lifecycle_checks=pass`
  (`TestOutput\validation\agent_logs\physics_standalone_collider_smoke_during_impl.log`).
  Rubber-duck review found no blockers and suggested tightening smoke evidence
  for stale body collider creation plus all updated collider fields; both were
  added before commit. Final validation:
  `tools\validate_fast.bat` passed
  (`TestOutput\validation\agent_logs\physics_standalone_collider_validate_fast_final.log`);
  `tools\validate_physics.bat` passed with byte-exact
  `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\physics_standalone_collider_validate_physics_final.log`).
- [x] 2026-06-28: Added the first model-free standalone physics API smoke path.
  `PhysicsStandaloneWorld` now supports deterministic body create, update,
  delete, query, and fixed-step integration through `PhysicsApi` without
  `Run`, renderer setup, scene parsing, `GameModel`, or `GameModelCollection`.
  `--physics-standalone-smoke` runs before worker/window/renderer startup and
  validates create/update/delete/query/step behavior through a four-step sample
  with an exact final body state, lifecycle checks, and a printed deterministic
  hash. `tools\validate_physics.bat` now runs this smoke before the existing
  byte-exact `physics_regression_solver.csv` gate. This is a standalone
  ownership beachhead, not full solver independence: collision, contacts, sleep
  islands, and compatibility `PhysicsModelAccess` migration remain open below.
  Rubber-duck review found no blockers; its two non-blocking nits were fixed
  before commit by keeping motion-kind and inverse-mass updates consistent and
  expanding the smoke to cover update/delete/stale-handle checks. Validation
  evidence:
  `tools\validate_fast.bat` passed with 0 filter/runtime-boundary errors and
  Profile/Debug builds ready
  (`TestOutput\validation\agent_logs\physics_standalone_smoke_validate_fast_after_rubber_duck_fixes.log`);
  `tools\validate_physics.bat` passed, including
  `lifecycle_checks=pass`, `hash=0xFF0FDFDB66F68C05`, exact final state, and
  byte-exact `physics_regression_solver.csv`
  (`TestOutput\validation\agent_logs\physics_standalone_smoke_validate_physics_after_rubber_duck_fixes.log`).
- [x] 2026-06-28: Removed `GameModelCollection*` from
  `SimulationTickInput`; `SimulationSystem` now borrows a
  `SimulationPhysicsStep` context and calls `PhysicsEngine::Step(...)` through
  `PhysicsModelAccess`. Runtime `RunFrame` remains the adapter that binds the
  current collection-backed compatibility access, and replay restore/prediction
  helper stepping now uses the same context instead of
  `GameModelCollection::RunPhysics`. The runtime-boundary allowlist entries for
  `SimulationSystem` were removed. Validation evidence:
  `tools\validate_fast.bat` passed; `python tools\check_runtime_boundaries.py`
  passed with 0 errors; `tools\validate_physics.bat` passed with byte-exact
  `physics_regression_solver.csv`.
- [x] 2026-06-28: Added a public-physics-facade guardrail to
  `tools\check_runtime_boundaries.py` so `PhysicsApi.h` and `PhysicsEngine.h`
  reject new `GameModelCollection`, raw `GameModel`, or
  `std::vector<GameModel>&` dependencies. The rule still permits the temporary
  `PhysicsModelAccess` compatibility boundary while forcing new public facade
  work toward handles, descriptors, immutable views, or a deliberately named
  adapter. Rubber-duck review found no blockers and two non-blocking wording
  and synthetic-test nits, both addressed before commit. Validation evidence:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.85s
  (`TestOutput\validation\agent_logs\physics_public_facade_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 17.87s
  (`TestOutput\validation\agent_logs\physics_public_facade_guardrail_validate_fast.log`).
- [x] 2026-06-28: Completed the compatibility-borrower inventory against the
  Carmack standalone physics problem statement. The inventory below records the
  exact search scope, reconciles the current guardrail allowlists, and splits
  borrowers into step-critical, diagnostics, replay, scene setup, editor/tool,
  and render mirror groups. Validation was not run because this is a plan-only
  documentation slice. Rubber-duck review found no blocker: `SimulationSystem`
  is already off the `GameModelCollection*` step boundary, but
  `PhysicsModelAccess` and named compatibility vector borrowers still represent
  the migration front for standalone embedding.

## Problem Statement

The Carmack-test verdict blocked standalone physics suitability because the
physics step and public commands are still compatibility-shaped around
`GameModelCollection` and model-index ownership. The engine has deterministic
physics evidence, but a physics user cannot embed the solver without also
dragging runtime/game-object ownership and some scene/editor assumptions.

## Goal

Make the physics package usable through stable physics handles, descriptors,
commands, and read-only views, with no normal physics step dependency on
`GameModelCollection`, renderer state, editor tools, or scene UI.

## Success Bar

- `PhysicsEngine` can create, step, query, wake, sleep, constrain, and destroy a
  physics world through `PhysicsApi` handles and descriptors.
- `SimulationSystem` no longer accepts `GameModelCollection*`.
- Runtime/game-object code becomes an adapter around the physics API, not the
  authority that physics depends on.
- Deterministic physics CSV output stays byte-exact unless a behavior change is
  intentional, documented, and regenerated through the required final gate.

## Related Plans

- `Agentic/Plans/physics-game-model-authority-plan.md` is the active
  implementation track for physics/store authority. Use this Carmack plan as the
  standalone-embedding acceptance checklist for that work.
- `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md` is
  historical context for the earlier evaluation slice; prefer the current active
  plan and source inventory over old assumptions.

## Implementation Checklist

### Inventory And Slicing

- [x] Run `rg "GameModelCollection|PhysicsModelAccess|MutablePhysicsModelsForCompatibility|PhysicsModelsForCompatibility" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`.
- [x] Reconcile every hit against `tools\check_runtime_boundaries.py` allowlists.
- [x] List every current compatibility borrower in this plan before modifying source.
- [x] Split borrowers into `step-critical`, `diagnostics`, `replay`, `scene setup`, `editor/tool`, and `render mirror` groups.
- [x] Choose one borrower group for the first PR-bound slice; do not combine all groups in one change.

#### Compatibility Borrower Inventory, 2026-06-28

Search used:
`rg "GameModelCollection|PhysicsModelAccess|MutablePhysicsModelsForCompatibility|PhysicsModelsForCompatibility" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`.

Guardrail reconciliation:

| Guardrail | Current status |
|-----------|----------------|
| Public facade check | `PhysicsApi.h` and `PhysicsEngine.h` reject new `GameModelCollection`, raw `GameModel`, and `std::vector<GameModel>&` facade dependencies. |
| `PHYSICS_GAME_MODEL_COLLECTION_ALLOWLIST` | Allows only physics debug visualizers and `Ragdoll::AddSimpleHumanoid(...)` creation signatures inside physics-facing files. |
| `PHYSICS_MODELS_ACCESS_ALLOWLIST` | Empty; the old neutral `PhysicsModels()` name is fully blocked. |
| `PHYSICS_MODELS_COMPAT_ACCESS_ALLOWLIST` | Contains the two named compatibility accessor definitions plus the current runtime/replay/editor vector borrowers. New calls fail until another borrower is removed or a count is deliberately updated. |

Borrower groups:

| Group | Current borrowers | Standalone risk | Suggested next slice |
|-------|-------------------|-----------------|----------------------|
| Step-critical | `SimulationSystem` now receives `SimulationPhysicsStep` and `PhysicsEngine::Step(PhysicsModelAccess&, ...)`; `PhysicsWorld`, `PhysicsBodyStore`, `ColliderStore`, `PersistentContactSolver`, and `SleepIslandSystem` still use `PhysicsModelAccess`. | Solver internals still read/write legacy `GameModel` ranges through the compatibility interface. | Move one store-owned datum from `GameModel` access into `PhysicsBodyStore` or `ColliderStore`, then rerun `validate_physics`. |
| Diagnostics and SkullScope | `PhysicsDiagnosticsSink`, `PhysicsWorld::EmitPhysicsDiagnosticsFrame`, `EmitPhysicsCollisionTime`, `GameModelCollection::EmitSkullScopeFrame`, `RuntimeDiagnostics`, and `DiagnosticsRuntime`. | Diagnostics still ask for model-backed state instead of immutable physics views. | Introduce a diagnostics sink/view boundary before touching SkullScope baseline output. |
| Replay | `ReplayRuntime`, `ReplayRecorder`, `RunReplayPredictionHelpers.inl`, `RunReplayPredictionVisualizer.inl`, `RunReplayVelocityEdit.inl`, `RunReplayCauseTreeTools.inl`, and `RunReplayQueryTools.inl`. | Replay samples and restores model-index state directly; handle lifetime and deterministic stale-handle behavior are not yet proven. | Count-guard replay compatibility vector borrowers, then migrate one replay restore/read path to physics handles or body views. |
| Scene setup | `RunScene.cpp`, `SceneAuthoredSetup`, `SceneGeneratedSetup`, `SceneRuntimeGeneratedControls`, `SceneRuntimeStyle`, and `Ragdoll::AddSimpleHumanoid(...)`. | Scene creation still treats `GameModelCollection` as the place where physics bodies are born. | Add a runtime/game-object adapter that maps scene objects to physics handles while keeping scene JSON behavior unchanged. |
| Editor/tool | `RunMousePickupTools.inl`, `RunEditorTools.cpp`, `EditorOverlayTools`, `RuntimeTools`, launcher tools, and interaction commands. | Editor and launcher commands still store model indices for physics actions. | Keep model indices presentation-only, then route actual physics commands through handles. |
| Render mirror and presentation | `GameModelCollection` render streams, `RuntimeRenderHost`, `RunRender.cpp`, `RunPasses.cpp`, `RunUiTextPass.cpp`, `RuntimeViewModel`, and `EngineContext`. | Render state is intentionally a mirror, but it still shares the model collection that physics mutates. | Treat render streams as post-step mirrors and avoid pulling renderer concepts into physics API work. |
| Physics debug visualizers | `CollisionVisualizer` and `PhysicsDebugVisualizer` are explicitly allowlisted. | Visualization currently reads model collection state for colors, contacts, sleep, and wireframes. | Migrate debug visualizers after immutable body/contact/island views exist. |

First PR-bound borrower group already chosen and completed: `step-critical`
simulation stepping. The completed 2026-06-28 SimulationSystem slice removed
`GameModelCollection*` from the normal step input while preserving
`PhysicsModelAccess` as the temporary solver/store compatibility boundary.

### Public Physics API

- [ ] Extend `SkullbonezSource/Physics/PhysicsApi.h` with any missing create, update, query, delete, and step descriptors needed by runtime callers.
- [x] Add standalone collider create, update, delete, query, and immutable
  collection views to the public physics API beachhead.
- [x] Add standalone point-joint create, update, delete, query, and immutable
  collection views keyed by `PhysicsConstraintHandle`.
- [x] Add standalone conservative ray-cast and broadphase AABB query methods
  that return physics handles instead of model indices.
- [x] Add standalone wake, seed-asleep, and sleep-enable commands that target
  physics handles instead of model indices.
- [ ] Add or expose a `PhysicsWorldHandle` or equivalent owner token if multiple isolated worlds are needed.
- [x] Keep command targets as `PhysicsBodyHandle`, `PhysicsColliderHandle`,
  and `PhysicsConstraintHandle`; do not add model-index fields to new API
  structs.
  - [x] 2026-06-28 deterministic-order documentation slice confirmed
    `PhysicsApi.h` command/update descriptors target physics handles, not
    model indices; `PhysicsSceneObjectId` remains descriptive identity
    metadata, not a storage offset.
- [ ] Add immutable body, collider, contact, island, and diagnostic view structs that do not expose solver-private vectors.
- [x] Document deterministic ordering rules for body creation, deletion,
  queries, and replay hash output.
  - [x] 2026-06-28 `PhysicsApi.h` now names deterministic order in the public
    glossary and invariants: standalone views and broadphase candidates iterate
    slot order, deletion tombstones slots and advances generations, reuse is
    deterministic through the tombstone free list, ray-cast equal-distance
    candidates keep the earlier collider slot, smoke hashes derive from stable
    handle/slot assignment, and standalone body ordering is the public ordering
    for future replay snapshots plus count/query smoke evidence.
  - [x] Rubber-duck review by Sartre found the ordering claims matched
    implementation, but blocked commit on comment-style acronyms. `PhysicsApi.h`
    now defines `AABB` and `STL` in the glossary; no behavior validation is
    required for this comment-only slice.

### Step Boundary

- [x] Replace `SimulationTickInput::models` with a narrow physics step service or context.
- [x] Route fixed-step and variable-step simulation through `PhysicsEngine::Step(...)` without requiring `GameModelCollection*`.
- [ ] Move fixed-tree release behavior behind an explicit physics event sink or runtime adapter.
- [ ] Move SkullScope frame emission behind an explicit diagnostics sink that receives physics views, not broad model storage.
- [ ] Remove direct solver reads of `GameModel` fields after equivalent body/collider store data exists.
- [ ] Preserve `PHYSICS_FIXED_DT`, max-step behavior, and deterministic time-scale handling.

### Store Authority

- [ ] Make `PhysicsBodyStore` the authoritative owner for pose, velocity, mass, sleep, and solver-visible body state.
- [ ] Make `ColliderStore` the authoritative owner for shape, material response, broadphase radius, and collision metadata.
- [ ] Make point joints and ragdoll constraints refer to physics handles rather than model indices.
  - [x] Standalone point-joint records now refer to `PhysicsBodyHandle` endpoints
    and stale through `PhysicsConstraintHandle`; legacy scene/ragdoll
    `PointJointConstraint` migration remains.
- [ ] Route body deletion through one deterministic path that invalidates handles, collider rows, constraints, contacts, and replay references.
  - [x] Standalone body deletion tombstones child colliders and connected point
    joints so stale handles fail predictably.
- [ ] Add a deterministic tombstone or generation policy so stale handles fail predictably.
  - [x] Standalone body, collider, and point-joint slots use generation
    tombstones and deterministic alive-slot iteration.
- [ ] Keep render instance updates as a mirror after physics mutation, not a dependency used during step.

### Runtime Adapter

- [x] Introduce a named runtime/game-object adapter that maps scene objects to physics handles.
- [x] Move existing `GameModelCollection` compatibility methods behind that adapter or delete them as call sites migrate.
- [ ] Make replay/editor/runtime callers store physics handles where they currently store model indices for physics commands.
- [ ] Keep model indices only for UI selection or render presentation where they are genuinely presentation concepts.
- [x] Add migration comments to any remaining temporary model-index bridge.

### Guardrails

- [x] Tighten `tools\check_runtime_boundaries.py` so new public physics facade headers cannot accept `GameModelCollection&`, `GameModelCollection*`, raw `GameModel&`, or raw `std::vector<GameModel>&`.
- [x] Count-guard any temporary compatibility adapter call sites and lower the count after each slice.
  2026-06-28 note: `PHYSICS_MODELS_COMPAT_ACCESS_ALLOWLIST` already guards
  exact `*PhysicsModelsForCompatibility()` borrower lines and rejects duplicate
  or new compatibility vector access in synthetic tests.
- [x] Add synthetic positive and negative tests for the boundary checker.
- [x] Teach project-filter validation about any new physics API or adapter files.

### Tests And Evidence

- [x] Add a small standalone physics smoke command or tool that constructs a physics world without `Run`, renderer, scene parser, or `GameModelCollection`.
- [x] Add a deterministic fixed-step standalone sample with a known final body state or hash.
- [x] Extend standalone smoke evidence to cover point-joint create, update,
  direct delete, connected-body delete, and stale-handle rejection.
  - [x] 2026-06-28 point-joint slice also rejects same-body joints, rejects
    invalid endpoint updates, proves valid endpoint moves, proves old-endpoint
    body deletion does not stale a moved joint, and proves new-endpoint deletion
    stales the connected constraint.
  - [x] Final smoke evidence:
    `lifecycle_checks=pass hash=0xE3F090306CC1FE70`.
- [x] Extend standalone smoke evidence to cover conservative ray-cast and
  broadphase AABB query results against the final live body/collider.
  - [x] 2026-06-28 query slice also covers an oriented body, a local-offset
    collider shape, and an intentionally too-small supplied collider
    broadphase radius, proving the conservative query envelope is clamped and
    rotated rather than trusting caller-provided radius data.
- [x] Extend standalone smoke evidence to cover wake, seed-asleep,
  sleep-disable, sleep-enable, fixed-body rejection, invalid-command rejection,
  durable sleep-disabled create/update/step/query behavior, and stale
  activation command rejection.
- [ ] Add a runtime integration sample proving scene objects still mirror physics body state after a step.
- [ ] Add focused replay restore evidence if handles replace model indices in replay state.
- [ ] If SkullScope output changes, update the query baseline only from final Debug artifacts and report query-size accounting.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [x] For physics step, store, collision, solver, sleep, or rigid-body changes: run `tools\validate_physics.bat`.
  - [x] 2026-06-28 activation slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_activation_validate_fast_final.log`
    reported source formatting clean, project filters `0 errors`, runtime
    boundaries `0 errors`, Profile/Debug builds `0 Warning(s)` and
    `0 Error(s)`, and `VALIDATE_FAST: ALL PASSED`.
  - [x] 2026-06-28 activation slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_activation_validate_physics_final.log`
    reported standalone smoke `lifecycle_checks=pass
    hash=0x32EC17812CDAA435`, `physics_regression_solver.csv (20001 lines,
    byte-exact match)`, Profile/Debug builds `0 Warning(s)` and `0 Error(s)`,
    and `VALIDATE_PHYSICS: ALL PASSED`.
  - [x] 2026-06-28 query slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_queries_validate_fast_final.log`
    reported source formatting clean, project filters `0 errors`, runtime
    boundaries `0 errors`, Profile/Debug builds `0 Warning(s)` and
    `0 Error(s)`, and `VALIDATE_FAST: ALL PASSED`.
  - [x] 2026-06-28 query slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_queries_validate_physics_final.log`
    reported standalone smoke `lifecycle_checks=pass
    hash=0xF32B90EB03C9D5F0`, `physics_regression_solver.csv (20001 lines,
    byte-exact match)`, Profile/Debug builds `0 Warning(s)` and `0 Error(s)`,
    and `VALIDATE_PHYSICS: ALL PASSED`.
  - [x] 2026-06-28 point-joint slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_point_joint_validate_fast_final.log`
    reported source formatting clean, project filters `0 errors`, runtime
    boundaries `0 errors`, Profile/Debug builds `0 Warning(s)` and
    `0 Error(s)`, and `VALIDATE_FAST: ALL PASSED`.
  - [x] 2026-06-28 point-joint slice validation:
    `TestOutput\validation\agent_logs\physics_standalone_point_joint_validate_physics_final.log`
    reported standalone smoke `lifecycle_checks=pass
    hash=0xE3F090306CC1FE70`, `physics_regression_solver.csv (20001 lines,
    byte-exact match)`, Profile/Debug builds `0 Warning(s)` and `0 Error(s)`,
    and `VALIDATE_PHYSICS: ALL PASSED`.
- [ ] For SkullScope baseline/query changes: run `tools\validate_physics_deep.bat`.
- [ ] For broad runtime integration changes: run `tools\validate_full.bat`.
- [ ] For hot-path storage or iteration changes: run `tools\validate_perf.bat` and document any warnings.
- [ ] Quote the relevant validation output in the handoff; do not claim success without command output.

## Independent Review Checklist

- [ ] Ask a rubber-duck reviewer to check for any remaining physics dependency on runtime, renderer, editor, scene UI, or `GameModelCollection`.
- [ ] Ask the reviewer to verify that deterministic ordering is explicit and validation-visible.
- [ ] Ask the reviewer to inspect handle lifetime, stale-handle behavior, and body deletion.
- [ ] Record blocking and non-blocking findings in a report or this plan.
  - [x] 2026-06-28 activation slice rubber-duck review by Avicenna initially
    found blocking gaps: sleep-disable was a one-shot wake instead of a durable
    gate across create/update/step/query, `SetSleepEnabled` did not clearly
    document that it ignores the body handle, and glossary coverage did not
    define activation/sleep/wake vocabulary. The slice now makes sleep-disabled
    bodies behave awake across those paths, documents the global command
    contract, expands glossary/API comments, and smoke-tests the durable
    sleep-disabled create/update/step/query edge. Follow-up review found no
    blockers. Non-blocking notes: the `SleepEnabled()` comment was broadened to
    match the durable gate behavior, and detailed activation subchecks are
    source-backed lifecycle checks while the validation log prints only
    `lifecycle_checks=pass hash=0x32EC17812CDAA435`.
  - [x] 2026-06-28 query slice rubber-duck review by Chandrasekhar initially
    found blocking false-negative risks for local-offset/oriented colliders and
    undersized cached broadphase radii. The slice now rotates local offsets,
    clamps conservative radii, and smoke-tests an oriented local-offset collider
    with an intentionally too-small supplied radius. Follow-up review found no
    remaining blockers. Non-blocking notes: the candidate envelope is safely
    more conservative than a tight shape query, and detailed oriented-offset
    proof lives in lifecycle checks/source while the validation log prints only
    `lifecycle_checks=pass hash=0xF32B90EB03C9D5F0`.
  - [x] 2026-06-28 point-joint slice rubber-duck review by Feynman found no
    blockers. Non-blocking findings were resolved before commit: same-body
    point joints are rejected, endpoint-update smoke coverage was added, and
    validation evidence/hash was recorded here.
  - [x] 2026-06-28 runtime-adapter slice rubber-duck review by Plato found no
    blockers. One non-blocking duplicate-identity risk was resolved before
    commit by making `BodyHandleForSceneObjectId` return an invalid handle when
    multiple models map to the same replay-derived scene object id. Missing
    evidence reminders: final validation still had to run after review, and no
    focused adapter-unit evidence exists yet. Final validation passed after the
    duplicate-id fix.
- [ ] Resolve blocking review findings before committing PR-bound code.
  - [x] No blocking activation slice findings remained before commit.
  - [x] No blocking query slice findings remained before commit.
  - [x] No blocking point-joint slice findings remained before commit.
  - [x] No blocking runtime-adapter slice findings remained before commit.

## Definition Of Done

- [ ] `GameModelCollection` is no longer on the normal physics step boundary.
- [ ] Standalone physics can be constructed and stepped from a small harness.
- [ ] Runtime scene objects adapt to physics handles instead of serving as solver authority.
- [ ] Boundary guardrails reject reintroducing broad game-object ownership into physics.
- [ ] Required validation passes with byte-exact physics baselines or a documented intentional baseline refresh.

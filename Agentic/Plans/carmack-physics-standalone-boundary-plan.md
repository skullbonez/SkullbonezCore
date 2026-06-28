# Carmack Physics Standalone Boundary Plan

Date: 2026-06-28
Status: In progress
Impact area: physics, runtime, scene system, replay, diagnostics, tests
Validation note: plan-only edits require no validation. PR-bound implementation
must use the smallest matching gate from `AGENTS.md`; physics-visible behavior
requires `tools\validate_physics.bat`, with `tools\validate_physics_deep.bat`
when SkullScope baselines or broad physics diagnostics change.

## Completed Slices

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
- [ ] Add or expose a `PhysicsWorldHandle` or equivalent owner token if multiple isolated worlds are needed.
- [ ] Keep command targets as `PhysicsBodyHandle`, `PhysicsColliderHandle`, and `PhysicsConstraintHandle`; do not add model-index fields to new API structs.
- [ ] Add immutable body, collider, contact, island, and diagnostic view structs that do not expose solver-private vectors.
- [ ] Document deterministic ordering rules for body creation, deletion, queries, and replay hash output.

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
- [ ] Route body deletion through one deterministic path that invalidates handles, collider rows, constraints, contacts, and replay references.
- [ ] Add a deterministic tombstone or generation policy so stale handles fail predictably.
- [ ] Keep render instance updates as a mirror after physics mutation, not a dependency used during step.

### Runtime Adapter

- [ ] Introduce a named runtime/game-object adapter that maps scene objects to physics handles.
- [ ] Move existing `GameModelCollection` compatibility methods behind that adapter or delete them as call sites migrate.
- [ ] Make replay/editor/runtime callers store physics handles where they currently store model indices for physics commands.
- [ ] Keep model indices only for UI selection or render presentation where they are genuinely presentation concepts.
- [ ] Add migration comments to any remaining temporary model-index bridge.

### Guardrails

- [x] Tighten `tools\check_runtime_boundaries.py` so new public physics facade headers cannot accept `GameModelCollection&`, `GameModelCollection*`, raw `GameModel&`, or raw `std::vector<GameModel>&`.
- [ ] Count-guard any temporary compatibility adapter call sites and lower the count after each slice.
- [x] Add synthetic positive and negative tests for the boundary checker.
- [ ] Teach project-filter validation about any new physics API or adapter files.

### Tests And Evidence

- [ ] Add a small standalone physics smoke command or tool that constructs a physics world without `Run`, renderer, scene parser, or `GameModelCollection`.
- [ ] Add a deterministic fixed-step standalone sample with a known final body state or hash.
- [ ] Add a runtime integration sample proving scene objects still mirror physics body state after a step.
- [ ] Add focused replay restore evidence if handles replace model indices in replay state.
- [ ] If SkullScope output changes, update the query baseline only from final Debug artifacts and report query-size accounting.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [x] For physics step, store, collision, solver, sleep, or rigid-body changes: run `tools\validate_physics.bat`.
- [ ] For SkullScope baseline/query changes: run `tools\validate_physics_deep.bat`.
- [ ] For broad runtime integration changes: run `tools\validate_full.bat`.
- [ ] For hot-path storage or iteration changes: run `tools\validate_perf.bat` and document any warnings.
- [ ] Quote the relevant validation output in the handoff; do not claim success without command output.

## Independent Review Checklist

- [ ] Ask a rubber-duck reviewer to check for any remaining physics dependency on runtime, renderer, editor, scene UI, or `GameModelCollection`.
- [ ] Ask the reviewer to verify that deterministic ordering is explicit and validation-visible.
- [ ] Ask the reviewer to inspect handle lifetime, stale-handle behavior, and body deletion.
- [ ] Record blocking and non-blocking findings in a report or this plan.
- [ ] Resolve blocking review findings before committing PR-bound code.

## Definition Of Done

- [ ] `GameModelCollection` is no longer on the normal physics step boundary.
- [ ] Standalone physics can be constructed and stepped from a small harness.
- [ ] Runtime scene objects adapt to physics handles instead of serving as solver authority.
- [ ] Boundary guardrails reject reintroducing broad game-object ownership into physics.
- [ ] Required validation passes with byte-exact physics baselines or a documented intentional baseline refresh.

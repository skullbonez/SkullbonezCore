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

- [ ] Run `rg "GameModelCollection|PhysicsModelAccess|MutablePhysicsModelsForCompatibility|PhysicsModelsForCompatibility" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`.
- [ ] Reconcile every hit against `tools\check_runtime_boundaries.py` allowlists.
- [ ] List every current compatibility borrower in this plan before modifying source.
- [ ] Split borrowers into `step-critical`, `diagnostics`, `replay`, `scene setup`, `editor/tool`, and `render mirror` groups.
- [ ] Choose one borrower group for the first PR-bound slice; do not combine all groups in one change.

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

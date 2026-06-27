# Engine Evaluation Fix 02: Physics Data Boundary

Date: 2026-06-27
Status: Completed Night Runner implementation
Source finding: physics depended on `GameModelCollection` as the authoritative
object store at the step boundary, which made determinism, replay, rendering,
parallelism, and diagnostics harder to reason about.
Impact area: physics, game object storage, runtime tools, scene setup, replay,
diagnostics, validation tooling

## Completed Goal

Remove `GameModelCollection&` from the active physics stepping, wake, sleep, and
impulse boundary without changing solver math or deterministic model order.

The completed shape is:

```text
GameModelCollection
  Temporary compatibility facade. It creates PhysicsModelView and body handles.

PhysicsModelView
  Narrow borrowed model-order boundary for legacy storage, cache invalidation,
  fixed-tree release, and SkullScope callback emission.

PhysicsEngine / PhysicsScene / PhysicsWorld
  Receive PhysicsModelView and PhysicsBodyHandle for step, wake, sleep, and
  impulse work. They no longer receive GameModelCollection& on the step path.

PhysicsBodyStore / ColliderStore / RenderInstanceStore
  Existing handle-bearing store boundaries remain the migration footholds for
  the next deeper authority split.
```

This plan completes the boundary slice that could be validated without changing
physics baselines, solver ordering, render output, or replay artifacts.

## Non-Goals Honored

- [x] Do not change solver math while moving the boundary.
- [x] Do not refresh physics baselines.
- [x] Do not remove `GameModelCollection` entirely in this slice.
- [x] Do not introduce a general ECS framework.
- [x] Do not add worker parallelism before store authority is explicit.
- [x] Do not combine this with render-graph execution work.

## Phase 0: Inventory And Baseline

- [x] Run the Agent Startup Contract from `AGENTS.md`.
- [x] Run `git status --short --branch` and treat existing dirty files as
      user-owned.
- [x] Read this plan, `Agentic/Plans/game-model-data-boundary-plan.md`, and
      `Agentic/Reference/physics-overview.md`.
- [x] Reuse and refresh the existing authority inventory in
      `Agentic/Reports/2026-06-25/game-model-data-boundary-phase0-inventory.md`.
- [x] Inventory `GameModelCollection&` dependencies in the active physics step
      boundary.
- [x] Inventory APIs that expose mutable `GameModel&` or mutable model vectors
      and record remaining compatibility use in the report.
- [x] Record which touched call sites need commands and which remain read-only
      queries.
- [x] Record current physics iteration order and model-index compatibility
      assumptions.
- [x] Record current replay id, body handle, and model-index mapping
      assumptions.
- [x] Write the dated implementation report:
      `Agentic/Reports/2026-06-27/physics-data-boundary-night-runner-report.md`.

Validation checklist:

- [x] Documentation-only inventory itself required no validation.
- [x] Source and validation-tooling changes were covered by `tools\validate_fast.bat`.
- [x] SkullScope was not used; no trace commands or query-size accounting were
      required.

## Phase 1: Stable Handles And Mapping

- [x] Confirm stable physics handles already exist in
      `SkullbonezSource/Physics/PhysicsHandles.h`.
- [x] Use `PhysicsBodyHandle` for wake, sleep, pending impulse, and apply
      impulse operations.
- [x] Preserve handle generation checks through
      `PhysicsBodyStore::ModelIndexForHandle`.
- [x] Keep legacy model-index to handle mapping through
      `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore`.
- [x] Keep reverse lookup only in compatibility paths that still need model
      indices.
- [x] Preserve replay body id storage on body, collider, and render-instance
      records.
- [x] Route touched editor/runtime/scene command call sites through facade
      commands instead of direct physics-layer collection APIs.
- [x] Validate handle and mapping behavior through build, physics, full, and
      perf gates rather than adding a new standalone test binary.

Validation checklist:

- [x] `tools\validate_fast.bat`
- [x] `tools\validate_physics.bat`
- [x] Replay artifact validation was not required because replay artifact
      identity and serialized replay format did not change.

## Phase 2: Command And Query Boundary

- [x] Add `GameModelCollection` facade commands for body impulse and pending
      impulse.
- [x] Route wake/sleep/impulse calls through `PhysicsBodyHandle` inside the
      physics facade.
- [x] Keep read-only store views available through existing
      `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` APIs.
- [x] Keep `PhysicsApi.h` as the documented command/query target for future
      scene, replay, renderer, and tool migration.
- [x] Replace direct mutation in touched physics call sites with command facade
      calls.
- [x] Replace direct runtime and scene setup calls to collection-taking physics
      APIs with `GameModelCollection` command helpers.
- [x] Keep the compatibility adapter small and explicitly named
      `PhysicsModelView`.
- [x] Tighten `tools/check_runtime_boundaries.py` so removed
      `PhysicsEngine` and `PhysicsWorld` `GameModelCollection&` signatures are
      rejected by synthetic self-tests.

Validation checklist:

- [x] `tools\validate_fast.bat`
- [x] `tools\validate_physics.bat`
- [x] `tools\validate_full.bat`

## Phase 3: Physics Step Boundary

- [x] Add `SkullbonezSource/Physics/PhysicsModelView.h`.
- [x] Convert `PhysicsEngine` refresh, step, wake, sleep, and impulse methods to
      receive `PhysicsModelView&`.
- [x] Convert `PhysicsEngine` wake, sleep, and impulse methods to receive
      `PhysicsBodyHandle`.
- [x] Convert `PhysicsScene` to resolve body handles through
      `PhysicsBodyStore`.
- [x] Convert `PhysicsWorld::RunPhysics` and its helper families from
      `GameModelCollection&` to `PhysicsModelView&`.
- [x] Convert `PersistentContactSolver`, `SleepIslandSystem`,
      `PhysicsDiagnosticsSink`, and `Ragdoll::SolvePointJoints` to
      `PhysicsModelView&`.
- [x] Preserve current deterministic body iteration order.
- [x] Preserve sleep propagation order and island behavior.
- [x] Preserve fixed-step timing and pending impulse order.
- [x] Preserve terrain response, fluid force, drag, tornado, gravity, and
      launcher impulse semantics.
- [x] Keep compatibility writeback through `PhysicsBodyStore` for non-migrated
      render, replay, and tool callers.
- [x] Remove `GameModelCollection&` from the active `PhysicsWorld` step helper
      families.

Validation checklist:

- [x] `tools\validate_physics.bat`
- [x] `tools\validate_perf.bat`
- [x] SkullScope was not needed because deterministic physics validation stayed
      byte-exact.
- [x] `TestOutput/baselines/physics_regression_solver.csv` was not updated.

## Phase 4: Render, Collider, Scene, And Replay Compatibility

- [x] Preserve the existing `ColliderStore` handle-bearing snapshot boundary.
- [x] Preserve shape dispatch behavior for sphere, box, hull, terrain, and
      mixed pairs by avoiding narrowphase math changes.
- [x] Preserve contact feature ids and persistent contact keys by avoiding
      manifold key changes.
- [x] Preserve static/dynamic filtering and terrain support classification.
- [x] Preserve ragdoll and compound-object relationships.
- [x] Preserve the existing `RenderInstanceStore` projection boundary.
- [x] Leave production rendering behavior unchanged and validate it through the
      DX12 renderer gate inside `validate_full`.
- [x] Preserve registered asset instance behavior and scene serialization by
      keeping creation ownership on the compatibility facade.
- [x] Preserve replay restore behavior; no replay artifact format or id
      migration was performed in this slice.

Validation checklist:

- [x] `tools\validate_dx12_renderer.bat` ran through `tools\validate_full.bat`.
- [x] `tools\validate_full.bat`
- [x] Scene JSON and asset data were not changed.

## Phase 5: Boundary Guardrails

- [x] Remove `PhysicsEngine` APIs that take `GameModelCollection&`.
- [x] Remove active `PhysicsWorld` helpers that take `GameModelCollection&`.
- [x] Keep `GameModelCollection::PhysicsModels()` only as a named legacy
      compatibility path for non-migrated callers.
- [x] Keep mutable `GameModel&` access outside the active physics step path
      unchanged unless touched command call sites required migration.
- [x] Add active validation rules that fail new non-allowlisted physics-layer
      includes or signatures involving `GameModelCollection`.
- [x] Add synthetic validation rules that fail old `PhysicsEngine::Step` and
      `PhysicsWorld::RunPhysics` `GameModelCollection&` signatures.
- [x] Document the remaining compatibility model and follow-up authority split
      in the dated report.

Validation checklist:

- [x] `tools\validate_physics.bat`
- [x] `tools\validate_full.bat`
- [x] `tools\validate_perf.bat`
- [x] Confirm zero warnings at `/W4`.
- [x] Confirm physics CSV remains byte-exact.

## Final Acceptance Checklist

- [x] `PhysicsWorld::RunPhysics` no longer takes `GameModelCollection&`.
- [x] `PhysicsEngine` no longer requires `GameModelCollection&` to step, wake,
      sleep, impulse, or restore touched body command state.
- [x] Body, collider, render, and scene metadata have separate named store or
      compatibility boundaries for this migration slice.
- [x] Production renderer output is unchanged and validated against DX12
      baselines.
- [x] Replay restore behavior is unchanged; no replay id fallback or artifact
      format change was introduced.
- [x] Touched editor/runtime/scene tools mutate wake/sleep/impulse state through
      command facade helpers rather than raw physics-layer collection APIs.
- [x] Boundary validation rejects removed physics dependencies on
      `GameModelCollection`.
- [x] Comment-style audit was run for every touched source-bearing file.
- [x] Final PR-bound validation includes physics, renderer/full, fast, and perf
      gates.

## Agent Do-Not-Miss Checklist

- [x] Do not change solver math while moving storage boundaries.
- [x] Do not reorder bodies, contacts, islands, or impulses.
- [x] Do not let replay ids silently fall back to a new index scheme.
- [x] Do not make render transform the authoritative physics pose.
- [x] Do not make physics body data own scene/editor metadata.
- [x] Do not update physics baselines as a shortcut around divergence.
- [x] Do not ingest whole physics CSV or SkullScope artifacts into the model.
- [x] Do not skip `validate_perf` for the storage-boundary concern.

## Validation Evidence

- [x] `tools\validate_build.bat Profile`:
      `TestOutput\validation\night_runner_physics_boundary_profile_build_02.log`,
      exit 0, 17.006s.
- [x] `python tools\check_runtime_boundaries.py --repo .` after allowlist
      tightening:
      `TestOutput\validation\night_runner_physics_boundary_runtime_boundaries_final.log`,
      exit 0, 3.143s.
- [x] `tools\validate_fast.bat`:
      `TestOutput\validation\night_runner_physics_boundary_validate_fast_final.log`,
      exit 0, 16.091s.
- [x] `tools\validate_physics.bat`:
      `TestOutput\validation\night_runner_physics_boundary_validate_physics.log`,
      exit 0, 75.437s.
- [x] `tools\validate_full.bat`:
      `TestOutput\validation\night_runner_physics_boundary_validate_full.log`,
      exit 0, 27.737s.
- [x] `tools\validate_perf.bat`:
      `TestOutput\validation\night_runner_physics_boundary_validate_perf.log`,
      exit 0, 22.270s. The gate completed with warnings, not clean perf
      evidence; the warning acceptance is recorded in
      `Agentic/Reports/2026-06-27/physics-data-boundary-night-runner-report.md`.
- [x] Repeat `tools\validate_perf.bat`:
      `TestOutput\validation\night_runner_physics_boundary_validate_perf_rerun.log`,
      exit 0, 21.796s. The repeat reproduced the same non-boundary warning
      shape while `Frame/Physics` stayed within noise.

## Follow-Up Roadmap

The next data-boundary plan should make the stores fully authoritative rather
than compatibility-backed:

- Move body transform, velocity, mass, inertia, sleep, force, and impulse
  authority completely out of `GameModel`.
- Move exact shape/material collision authority completely into `ColliderStore`.
- Move production rendering to consume `RenderInstanceStore` directly.
- Split scene/entity metadata out of `GameModel`.
- Migrate replay artifacts and editor selection to stable entity/body/render
  handles.
- Delete `PhysicsModelView`, `GameModelCollection::PhysicsModels()`, and the
  remaining debug/creation compatibility allowlist only after those callers are
  migrated.

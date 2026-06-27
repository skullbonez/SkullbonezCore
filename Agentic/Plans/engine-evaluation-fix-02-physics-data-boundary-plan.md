# Engine Evaluation Fix 02: Physics Data Boundary

Date: 2026-06-27
Status: Draft implementation plan
Source finding: physics still depends on `GameModelCollection` as an
authoritative object store, which makes determinism, replay, rendering,
parallelism, and diagnostics harder to reason about.
Impact area: physics, game object storage, render instances, scene system,
replay, editor tools, diagnostics
Validation for this document-only change: none required

## Goal

Remove `GameModelCollection` from the physics stepping boundary.

The final shape should make ownership explicit:

```text
SceneEntityStore
  Stable entity ids, names, authored asset metadata, grouping, editor labels.

PhysicsBodyStore
  Authoritative transform, velocity, mass, inertia, force, sleep, island, and
  impulse state.

ColliderStore
  Authoritative collision shapes, material/friction data, and narrowphase
  metadata.

RenderInstanceStore
  Renderer-facing transform, material, visibility, instance kind, and shadow
  participation.

GameModelCollection
  Temporary compatibility facade, not the authoritative data model.

PhysicsWorld
  Steps body/collider stores and command buffers. It does not receive
  `GameModelCollection&`.
```

This plan follows `Agentic/Plans/game-model-data-boundary-plan.md` and turns the
second engine-evaluation issue into a stricter execution checklist.

## Why This Matters

Physics is one of the engine's strongest features, but the current data boundary
keeps unrelated concepts tied together:

- rendering and physics both flow through `GameModel`,
- solver state is refreshed from a render/game-object collection,
- replay identity can accidentally depend on vector indices,
- worker parallelism is risky because ownership and iteration order are not
  fully explicit,
- diagnostics and editor tools can mutate physics through broad object access.

The migration must preserve deterministic physics first. Storage cleanup is not
allowed to become an accidental solver rewrite.

## Current Anchors To Inspect

- `SkullbonezSource/GameObjects/GameModel.h`
- `SkullbonezSource/GameObjects/GameModel.cpp`
- `SkullbonezSource/GameObjects/GameModelCollection.h`
- `SkullbonezSource/GameObjects/GameModelCollection.cpp`
- `SkullbonezSource/GameObjects/GameModelStreams.h`
- `SkullbonezSource/GameObjects/GameModelSoACache.h`
- `SkullbonezSource/Physics/PhysicsEngine.h`
- `SkullbonezSource/Physics/PhysicsEngine.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsBodyStore.h`
- `SkullbonezSource/Physics/ColliderStore.h`
- `SkullbonezSource/Rendering/RenderInstanceStore.h`
- `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`

## Non-Goals

- Do not change solver math while moving ownership.
- Do not refresh physics baselines unless behavior intentionally changes.
- Do not remove `GameModelCollection` in the first slice.
- Do not introduce a general ECS framework before the body/collider/render
  boundary is stable.
- Do not mix this with runtime shell extraction except for unavoidable call-site
  updates.
- Do not add worker parallelism until authoritative store ownership and
  iteration order are explicit.

## Design Rules

- Determinism beats cleanup. Preserve iteration order, body ids, and command
  application order.
- Handles come before storage movement.
- Command APIs mutate state; query APIs expose read-only views.
- Physics body state is not render instance state.
- Scene authoring metadata is not physics hot-loop state.
- Replay identity must not depend on vector indices after this migration.
- Every source-bearing implementation slice must follow
  `Agentic/Reference/comment-style-guide.md`.
- Use the repo-local orchestrator skill before implementing this plan unless
  the user explicitly asks to bypass it.

## Phase 0: Authority Inventory And Determinism Baseline

Purpose: make every field's owner explicit before moving storage.

Checklist:

- [ ] Run the Agent Startup Contract from `AGENTS.md`.
- [ ] Run `git status --short --branch` and record pre-existing dirty files as
      user-owned.
- [ ] Read this plan, `Agentic/Plans/game-model-data-boundary-plan.md`, and
      `Agentic/Reference/physics-overview.md`.
- [ ] Create or refresh an authority table for every `GameModel` field:
      physics body, collider, render instance, scene metadata, replay identity,
      tool/editor metadata, diagnostics, compatibility-only, or transient.
- [ ] Inventory every `GameModelCollection&` dependency in physics, rendering,
      scene loading, replay, diagnostics, tools, and UI.
- [ ] Inventory every API that returns mutable `GameModel&` or mutable
      `std::vector<GameModel>&`.
- [ ] Record which call sites need commands and which need read-only queries.
- [ ] Record current physics iteration order and any sorting assumptions.
- [ ] Record current replay id, body id, and model-index mapping assumptions.
- [ ] Write or update a dated report under `Agentic/Reports/`.

Validation checklist:

- [ ] Documentation-only inventory needs no repository validation.
- [ ] If any diagnostic/tooling code changes, run the smallest matching gate
      from `AGENTS.md`.
- [ ] If SkullScope is used, report the exact trace command, every query, and
      GPT-read size accounting.

## Phase 1: Stabilize Handles Before Moving Data

Purpose: stop future code from assuming vector index equals identity.

Checklist:

- [ ] Define or promote stable ids for `EntityId`, `PhysicsBodyId`,
      `ColliderId`, and `RenderInstanceId`.
- [ ] Give each id a generation or validity check if stale references are
      possible after scene rebuild or replay restore.
- [ ] Add mapping tables from legacy model index to the new handles.
- [ ] Add reverse lookup only where a compatibility caller still needs it.
- [ ] Route replay body identity through stable handles.
- [ ] Route editor selection and diagnostic labels through entity/body handles.
- [ ] Add debug assertions that mapping tables stay valid after:
      scene load, scene reset, generated scene rebuild, object deletion,
      replay restore, and physics trimming.
- [ ] Add focused tests for handle validity and stale-handle rejection.

Validation checklist:

- [ ] `tools\validate_fast.bat`
- [ ] `tools\validate_physics.bat`
- [ ] Add `tools\validate_replay_v2_artifact.bat` if replay artifact identity
      or restore mapping changes.

## Phase 2: Introduce Command And Query Boundaries

Purpose: make access intent explicit before changing storage owners.

Checklist:

- [ ] Add physics commands for wake, impulse, force, teleport, body enable,
      sleep override, fixed-contact behavior, and pending launcher impulses.
- [ ] Add read-only physics queries for body pose, velocity, mass, collider
      shape, sleep/island state, and diagnostics.
- [ ] Add render queries that expose render instances without exposing
      `GameModel`.
- [ ] Add scene/entity queries for names, authored asset source, grouping, and
      editor labels.
- [ ] Replace direct mutation in physics call sites with command APIs.
- [ ] Replace direct read access in diagnostics/replay/rendering with query
      APIs.
- [ ] Keep compatibility adapters small and clearly named.
- [ ] Add boundary checks or source-search checks for new direct physics-layer
      dependencies on `GameModelCollection`.

Validation checklist:

- [ ] `tools\validate_physics.bat`
- [ ] `tools\validate_fast.bat` if only boundary tooling and tests change.
- [ ] `tools\validate_full.bat` if scene load, replay restore, or render state
      can change.

## Phase 3: Make `PhysicsBodyStore` Authoritative

Purpose: remove body state authority from `GameModel`.

Checklist:

- [ ] Move authoritative position, orientation, linear velocity, angular
      velocity, mass, inverse mass, inertia, sleep, body flags, accumulated
      forces, impulses, and island ids into `PhysicsBodyStore`.
- [ ] Ensure the solver reads and writes body store data directly.
- [ ] Preserve the current deterministic body iteration order.
- [ ] Preserve sleep propagation order and island membership behavior.
- [ ] Preserve fixed-step timing and pending impulse application order.
- [ ] Preserve terrain response, fluid force, drag, tornado, gravity, and
      launcher impulse semantics.
- [ ] Keep a temporary compatibility writeback to `GameModel` only where
      non-migrated callers still need it.
- [ ] Remove `GameModelCollection&` from one `PhysicsWorld` helper family per
      slice.

Validation checklist:

- [ ] `tools\validate_physics.bat`
- [ ] `tools\validate_perf.bat` for store layout or hot-loop iteration changes.
- [ ] Use SkullScope focused queries if determinism or sleep/island behavior
      diverges; report query costs in the handoff.
- [ ] Do not update `TestOutput/baselines/physics_regression_solver.csv` unless
      an intentional behavior change is approved.

## Phase 4: Make `ColliderStore` Authoritative

Purpose: decouple collision shape ownership from render/game objects.

Checklist:

- [ ] Move authoritative collision shape, material/friction, restitution,
      sensor/static flags, and hull metadata into `ColliderStore`.
- [ ] Route narrowphase and broadphase through collider handles and body
      handles.
- [ ] Preserve shape dispatch for sphere, box, hull, terrain, and mixed pairs.
- [ ] Preserve contact feature ids and persistent contact keys.
- [ ] Preserve static/dynamic filtering and terrain support classification.
- [ ] Preserve ragdoll and compound-object collider relationships.
- [ ] Keep debug visualizers reading collider/body views, not `GameModel`.

Validation checklist:

- [ ] `tools\validate_physics.bat`
- [ ] `tools\validate_physics_deep.bat` if broadphase, SkullScope diagnostics,
      bullet sweep, or query baselines change.
- [ ] `tools\validate_perf.bat` if broadphase/spatial grid iteration changes.

## Phase 5: Make Render Instances A Projection

Purpose: stop production rendering from depending on physics/game object storage.

Checklist:

- [ ] Make `RenderInstanceStore` the source for renderer-facing transform,
      material, visibility, mesh kind, tint, and shadow participation.
- [ ] Update `GameModelRenderer` and `IRenderSceneView` paths to consume render
      instance views.
- [ ] Keep physics debug overlays reading body/collider views rather than
      render instance internals.
- [ ] Move replay render pose override/restore through body and render handles.
- [ ] Move scene setup and generated object creation to populate body, collider,
      render, and entity stores explicitly.
- [ ] Temporarily compare legacy render projection against the new store where
      practical.
- [ ] Delete compatibility writeback only after render/replay/tool callers have
      migrated.

Validation checklist:

- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_full.bat` if scene load, replay render state, or runtime
      lifecycle can change.
- [ ] `tools\validate_replay_v2_artifact.bat` if replay render pose restore
      changes.

## Phase 6: Split Scene Entity Metadata

Purpose: keep authoring and editor metadata out of physics and render hot data.

Checklist:

- [ ] Add or promote a scene/entity metadata store for names, authored type,
      asset source, collection kind, root/child relationships, editor
      selection labels, and diagnostic names.
- [ ] Move `GameModelCollectionKind`, scene-only tags, asset source, and editor
      labels out of `GameModel`.
- [ ] Update scene snapshot writing to query entity metadata plus physics,
      collider, and render stores.
- [ ] Update editor tools to use entity/body/render handles instead of mutable
      `GameModel&`.
- [ ] Preserve registered asset instance behavior and scene serialization.
- [ ] Preserve compound object and tree/group identity semantics.

Validation checklist:

- [ ] `tools\validate_full.bat`
- [ ] `tools\validate_scene_loads.bat` if scene loading or serialized output
      changes.
- [ ] If asset scene JSON changes, follow the asset/scene validation map in
      `AGENTS.md`.

## Phase 7: Retire Compatibility Paths

Purpose: make the boundary hard to regress.

Checklist:

- [ ] Remove `PhysicsEngine` APIs that take `GameModelCollection&`.
- [ ] Remove `PhysicsWorld` helpers that take `GameModelCollection&`.
- [ ] Remove `GameModelCollection::PhysicsModels()` from production paths.
- [ ] Remove mutable `GameModel&` access from physics, render, replay, and tool
      code.
- [ ] Add validation rules that fail new physics-layer includes of
      `GameModelCollection.h`.
- [ ] Add validation rules that fail new `GameModelCollection&` parameters in
      `PhysicsWorld` and `PhysicsEngine`.
- [ ] Collapse `GameModel` to a compatibility view or delete it if no caller
      needs it.
- [ ] Update architecture docs and session state with the new ownership model.

Validation checklist:

- [ ] `tools\validate_physics.bat`
- [ ] `tools\validate_dx12_renderer.bat`
- [ ] `tools\validate_perf.bat`
- [ ] `tools\validate_full.bat`
- [ ] Confirm zero warnings at `/W4`.
- [ ] Confirm physics CSV remains byte-exact unless an intentional baseline
      refresh was approved and rerun through the matching gate.

## Final Acceptance Checklist

- [ ] `PhysicsWorld::RunPhysics` or equivalent step path no longer takes
      `GameModelCollection&`.
- [ ] `PhysicsEngine` no longer requires `GameModelCollection&` to step, wake,
      impulse, or restore bodies.
- [ ] Body, collider, render, and scene metadata authority are separate.
- [ ] Production renderer paths consume render instances, not `GameModel`.
- [ ] Replay restore maps through stable handles.
- [ ] Editor tools mutate through commands and handles, not raw model refs.
- [ ] Boundary validation rejects physics dependencies on `GameModelCollection`.
- [ ] Comment-style audit was run for every touched source-bearing file.
- [ ] Final PR-bound validation includes the required physics, renderer, perf,
      and full gates listed above.

## Agent Do-Not-Miss Checklist

- [ ] Do not change solver math while moving storage.
- [ ] Do not reorder bodies, contacts, islands, or impulses accidentally.
- [ ] Do not let replay ids silently fall back to vector indices.
- [ ] Do not make render transform the authoritative physics pose.
- [ ] Do not make physics body data own scene/editor metadata.
- [ ] Do not update physics baselines as a shortcut around divergence.
- [ ] Do not ingest whole physics CSV or SkullScope artifacts into the model;
      query them narrowly and report query costs.
- [ ] Do not skip `validate_perf` when hot-loop storage or broadphase iteration
      changes.


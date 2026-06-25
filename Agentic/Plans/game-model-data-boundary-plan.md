# Game Model Data Boundary Plan

Date: 2026-06-25
Status: Draft follow-up plan
Impact area: physics, game object storage, rendering streams, replay, scene system, diagnostics
Validation for this document-only change: none required

## Goal

Move authoritative engine state out of `GameModel` and `GameModelCollection`.

The current engine works, but the core world model is still a transitional
object pile: `GameModelCollection` owns `std::vector<GameModel>`, implements a
render scene view, forwards physics, exposes render streams, owns replay-facing
ids, and provides diagnostics. `GameModel` itself holds collision shape, rigid
body state, borrowed world/terrain pointers, render material, terrain-response
mailboxes, fixed-contact behavior, and replay metadata.

A professional engine needs authoritative body, collider, render-instance, and
entity/tool data with clear ownership. `GameModel` can remain as a compatibility
facade for a while, but it should stop being the storage root.

Target outcome:

```text
SceneEntityStore
  Owns stable entity ids, names, authoring metadata, and scene grouping.

PhysicsBodyStore
  Owns authoritative transforms, velocities, masses, sleep state, forces,
  impulses, and body handles.

ColliderStore
  Owns authoritative collision shapes and narrowphase metadata.

RenderInstanceStore
  Owns render transforms, materials, visibility, and renderer-facing instance
  metadata.

GameModelCollection
  Becomes a compatibility facade over those stores, then shrinks or disappears
  as callers migrate.
```

## Current Evidence

- `SkullbonezSource/GameObjects/GameModelCollection.h` stores
  `std::vector<GameModel> m_gameModels` and a `Physics::PhysicsEngine` in the
  same class.
- `GameModelCollection` implements `Rendering::IRenderSceneView` while also
  exposing `RunPhysics`, physics body/collider/render stores, scene snapshots,
  sleep/debug diagnostics, point joints, tornado settings, and replay snapshot
  restore.
- `SkullbonezSource/GameObjects/GameModel.h` combines collision shape,
  `RigidBody`, borrowed world/terrain pointers, terrain response mailbox,
  render tint/material, fixed-contact behavior, replay id, and collection
  metadata.
- `SkullbonezSource/Physics/PhysicsEngine.h` still takes
  `GameModelCollection&` for store refresh, stepping, waking, impulse
  application, and pending body impulse setup.
- `SkullbonezSource/Physics/PhysicsWorld.h` still passes
  `GameModelCollection&` through solver, diagnostics, sleep, tornado, and wake
  helpers.

## Design Rules

1. Preserve deterministic physics first. Any storage migration must keep
   `physics_regression_solver.csv` byte-exact unless a behavior change is
   intentional.
2. Introduce handles before moving data. Stable ids make migration observable
   and testable.
3. Do not change solver math while moving storage ownership.
4. Do not make render instances the source of truth for physics.
5. Do not make physics bodies the source of truth for authoring metadata.
6. Keep SoA/cache layers explicit: cache data can accelerate hot loops, but it
   must not hide who owns authoritative state.
7. Keep `GameModelCollection` compiling as a facade until call sites have moved.

## Non-Goals

- Do not rewrite the solver algorithm.
- Do not redesign scene file syntax.
- Do not remove replay exporters.
- Do not introduce a full ECS framework before body/collider/render ownership
  is clean.
- Do not combine this with broad `Run` extraction except where call sites must
  be updated.

## Phase 0: Inventory Authority And Callers

Purpose: make the migration concrete before changing storage.

Tasks:

1. Create an authority table for every `GameModel` field:
   - physics body,
   - collider,
   - render instance,
   - scene authoring/entity metadata,
   - replay identity,
   - compatibility-only/transient.
2. List every direct `GameModelCollection&` dependency in physics, renderer,
   scene, replay, diagnostics, and tools.
3. Identify which call sites need command APIs and which need query APIs.
4. Add temporary logging or assertions only if they answer a specific migration
   risk; do not add broad diagnostics noise.

Validation:

- Documentation-only phase: no validation required.

## Phase 1: Stabilize Handles And Store Boundaries

Purpose: separate identity from vector index before moving data.

Tasks:

1. Introduce or promote stable handles:
   - `EntityId`
   - `PhysicsBodyId`
   - `ColliderId`
   - `RenderInstanceId`
2. Add mapping tables between legacy model indices and new handles.
3. Ensure replay body ids map through handles instead of assuming model index
   stability.
4. Add debug checks that verify handle mappings stay consistent after scene
   load, reset, replay restore, trimming, and generated-scene rebuilds.

Validation:

- `tools\validate_fast.bat`
- `tools\validate_physics.bat`

## Phase 2: Make Physics Stores Authoritative

Purpose: stop `PhysicsWorld` from stepping `GameModelCollection`.

Tasks:

1. Move transform, velocity, mass, inertia, sleep, force, and impulse authority
   into `PhysicsBodyStore`.
2. Move exact shape authority into `ColliderStore`.
3. Change `PhysicsEngine::Step()` to consume physics stores and command buffers,
   not `GameModelCollection&`.
4. Replace `WakeBody`, `ApplyBodyImpulse`, and pending impulse APIs with
   handle-based commands.
5. Keep a compatibility sync path that writes back to `GameModel` only while
   render/replay/tool callers still need it.
6. Remove `GameModelCollection&` from `PhysicsWorld` private helper signatures
   phase by phase.

Validation:

- `tools\validate_physics.bat`
- `tools\validate_perf.bat` when hot-loop layout or store iteration changes.

## Phase 3: Make Render Instances A Projection

Purpose: prevent renderer-facing data from being tied to the legacy object
container.

Tasks:

1. Make `RenderInstanceStore` the renderer-facing source for transforms,
   material, visibility, instance kind, and shadow participation.
2. Have scene setup and replay restore update render instances through explicit
   APIs.
3. Update `GameModelRenderer` and `IRenderSceneView` usage so production render
   paths read `RenderInstanceStore` instead of `std::vector<GameModel>`.
4. Keep collision/debug overlays using physics/collider views, not render
   instance internals.
5. Make replay render pose override/restore operate through body/render handles.

Validation:

- `tools\validate_dx12_renderer.bat`
- `tools\validate_full.bat` when replay render state or scene load behavior is
  touched.

## Phase 4: Split Scene Entity Metadata

Purpose: keep authoring/tool names and grouping out of physics/render hot data.

Tasks:

1. Add a scene/entity metadata store for names, authored type, asset source,
   collection kind, root model/entity relationship, editor selection metadata,
   and diagnostic labels.
2. Move `GameModelCollectionKind`, names, collection root index, and scene-only
   tags out of `GameModel`.
3. Update scene snapshot writing to query entity metadata plus body/collider and
   render stores.
4. Update editor tools to operate on entity/body/render handles instead of
   direct `GameModel&` mutation.

Validation:

- `tools\validate_full.bat`

## Phase 5: Retire The Compatibility Facade

Purpose: make the new boundary hard to regress.

Tasks:

1. Remove `PhysicsEngine` APIs that take `GameModelCollection&`.
2. Remove `PhysicsWorld` private helpers that take `GameModelCollection&`.
3. Remove `GameModelCollection::PhysicsModels()` from production paths.
4. Collapse `GameModel` to a thin compatibility/view object or delete it if no
   longer needed.
5. Add a boundary validator rule that fails on new physics-layer dependencies on
   `GameModelCollection`.

Validation:

- `tools\validate_physics.bat`
- `tools\validate_dx12_renderer.bat`
- `tools\validate_perf.bat`
- `tools\validate_full.bat`

## Success Criteria

- Physics stepping does not take `GameModelCollection&`.
- Renderer production paths consume render instances, not `GameModel`.
- Scene snapshot and replay restore use stable handles across physics, collider,
  render, and entity metadata.
- `GameModelCollection` is no longer the authoritative world data store.
- Physics remains deterministic and byte-exact at every phase gate.

## Risks

| Risk | Mitigation |
|------|------------|
| Physics determinism changes from iteration order or sync timing | Introduce handles first and validate with `validate_physics` after every behavior-touching slice. |
| Render output changes because instance projection misses material/visibility state | Keep old/new projection comparison temporarily and run DX12 renderer validation. |
| Replay restore loses identity after trimming or generated scene rebuild | Map replay ids through stable handles before moving storage. |
| Editor tools mutate stale compatibility objects | Migrate editor commands to handle-based APIs before deleting writeback paths. |

## Handoff Notes

Implement this in small slices through the repo-local orchestrator skill. Treat
physics baseline changes as behavior changes, not storage refactor fallout.

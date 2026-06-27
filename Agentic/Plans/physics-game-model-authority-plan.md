# Physics / GameModel Authority Plan

Date: 2026-06-27
Status: Draft plan
Impact areas: physics, game model data ownership, scene system, replay, rendering projection, tests
Validation for this plan edit: Documentation-only. No repository validation required.

## Goal

Make physics and game-object ownership explicit so production simulation no longer depends on mutable `GameModel` records as the source of truth.

The target shape is:

- `PhysicsBodyStore` owns body state: pose, velocity, angular velocity, mass, inertia, sleep state, accumulated forces, impulses, and fixed/dynamic flags.
- `ColliderStore` owns collision state: exact shape handles, material/contact parameters, bounds, broadphase keys, and collision filtering metadata.
- `RenderInstanceStore` owns render projection state: visible render instances, render transforms, material/render handles, flags, and per-frame instance data.
- Scene/entity metadata owns names, collection grouping, root/child relationships, asset instance identity, editor identity, and replay identity.
- `GameModelCollection` becomes a construction, compatibility, and scene-authoring facade during migration, then loses production physics/render authority.
- `GameModel` shrinks to compatibility metadata or disappears from production hot paths once stores and handles are authoritative.
- `MakePhysicsModelView()` and `PhysicsModelView` are deleted early. They are compatibility debt, not an acceptable long-term boundary.

This plan continues the completed `engine-evaluation-fix-02-physics-data-boundary-plan.md` slice. That slice introduced `PhysicsModelView`; this plan treats that adapter as the first thing to remove, then continues the deeper store-authority migration.

## Non-Goals

- [ ] Do not change solver math while moving ownership unless a separate bug is proven and documented.
- [ ] Do not refresh physics baselines unless behavior intentionally changes and the matching physics gate is rerun after the baseline update.
- [ ] Do not introduce a broad ECS rewrite. Use the smallest registry/store shape that fits current engine data and call sites.
- [ ] Do not migrate every editor, replay, scene, and render caller in one oversized diff.
- [ ] Do not replace model indices with unstable raw pointers.
- [ ] Do not hide behavior changes behind compatibility writeback.
- [ ] Do not add parallel physics execution until ownership, ordering, and determinism are already stable.
- [ ] Do not replace `MakePhysicsModelView()` with another per-frame adapter over `m_gameModels`; that would keep the same bad boundary under a new name.

## Phase 0 - Startup, Inventory, and Slice Choice

- [ ] Follow the repository Agent Startup Contract before editing.
- [ ] Confirm the current branch and dirty state with `git status --short --branch`; treat pre-existing dirty files as user-owned.
- [ ] Read this plan and the current handoff in `Agentic/SessionState.md`.
- [ ] Read `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`.
- [ ] Read `Agentic/Plans/engine-architecture-next-steps-plan.md`.
- [ ] Skim `Agentic/Plans/game-model-data-boundary-plan.md` for older context, but prefer current source over old assumptions.
- [ ] Choose one implementation slice only; do not try to complete every phase in one PR unless explicitly requested.
- [ ] State the selected impact area before editing: physics, scene system, rendering projection, tests, or documentation.
- [ ] State the deferred PR-gate validation command before editing.

Inventory checklist:

- [ ] List every authoritative-looking field in `SkullbonezSource/GameObjects/GameModel.h`.
- [ ] List every mutating production API in `SkullbonezSource/GameObjects/GameModelCollection.h` and `.cpp`.
- [ ] List all `GameModelCollection::PhysicsModels()` call sites.
- [ ] List all `PhysicsModelView` call sites and the compatibility behavior they still rely on.
- [ ] List every `MakePhysicsModelView()` call and classify it as step, wake, impulse, store refresh, diagnostics, ragdoll, sleep, or debug.
- [ ] List all places that read or mutate physics body pose, velocity, mass, inertia, sleep, force, and impulse state.
- [ ] List all places that read or mutate collider shape, material, filtering, and bounds state.
- [ ] List all places that use model indices for physics commands, replay, editor selection, scene persistence, or diagnostics.
- [ ] List all rendering callers that still read renderable state through `GameModelCollection` or `IRenderSceneView`.
- [ ] Record the inventory in a short handoff note under `Agentic/Reports/` if the slice is not completed in one sitting.

## Required First Track - Bootstrap Handles, Then Delete `MakePhysicsModelView()`

`MakePhysicsModelView()` is a contrived compatibility factory. It rebuilds a borrowed view at each call site, keeps physics aware of legacy model order and SoA invalidation, and hides scene callbacks behind generic function pointers. The first implementation slice should remove this boundary instead of polishing it.

Prerequisite handle-bootstrap slice:

- [ ] Promote the existing `PhysicsBodyHandle` path out of pure model-index compatibility before deleting the view.
- [ ] Add or reuse a deterministic body handle mapping owned by the physics/body store layer.
- [ ] Ensure the mapping can resolve old model-index callers only at explicit compatibility boundaries.
- [ ] Ensure new physics APIs accept stable handles or store rows, not raw `GameModel` indices.
- [ ] Document any remaining compatibility handle conversion with a deletion target in this plan.
- [ ] Prove the bootstrap does not change body iteration order or replay ordering.

Acceptable endpoint:

- [ ] `GameModelCollection::RunPhysics()` no longer constructs a `PhysicsModelView`.
- [ ] `PhysicsEngine::Step()` receives authoritative stores or a named step context built from authoritative stores.
- [ ] Wake, seed-asleep, impulse, and pending-impulse APIs take `PhysicsBodyHandle` plus the required store/context references.
- [ ] Diagnostics and SkullScope emission use explicit diagnostics context, not `PhysicsModelView`.
- [ ] Attached fixed-tree release uses an explicit scene callback/service, not a generic `void*` callback hidden in `PhysicsModelView`.
- [ ] `GameModelCollection` does not expose a new per-frame view over `m_gameModels` as a substitute.

Implementation checklist:

- [ ] Create a short call-site table for every `MakePhysicsModelView()` call in `GameModelCollection.cpp`.
- [ ] Create a short parameter table for every `PhysicsModelView&` parameter in `PhysicsEngine`, `PhysicsScene`, `PhysicsWorld`, diagnostics, ragdoll, sleep, and solver code.
- [ ] Split `PhysicsModelView` responsibilities into named dependencies: body store, collider store, render store, deterministic model/body ordering if still needed, fixed-tree release callback, and diagnostics callback.
- [ ] Introduce a narrowly named step context only if passing the stores separately becomes noisy; the context must not own or expose `std::vector<GameModel>&`.
- [ ] Convert `PhysicsEngine::RefreshStores`, `RefreshPhysicsStores`, `RefreshBodyStore`, `RefreshColliderStore`, and `RefreshRenderStore` to store/source inputs that do not require `PhysicsModelView`.
- [ ] Convert `PhysicsEngine::Step` and `PhysicsScene::RunPhysics` away from `PhysicsModelView`.
- [ ] Convert wake and impulse helpers away from `PhysicsModelView`.
- [ ] Convert physics diagnostics away from `PhysicsModelView`.
- [ ] Convert ragdoll and sleep-island code away from `PhysicsModelView`.
- [ ] Delete `GameModelCollection::MakePhysicsModelView()` declaration and definition.
- [ ] Delete `SkullbonezSource/Physics/PhysicsModelView.h` once all includes are gone.
- [ ] Remove stale comments that describe `PhysicsModelView` as the migration boundary.
- [ ] Add a guardrail or boundary check that fails if `MakePhysicsModelView` or `PhysicsModelView` returns after deletion.

Do-not-miss checklist:

- [ ] The replacement path does not allocate or rebuild a model view each physics operation.
- [ ] The replacement path does not expose mutable `std::vector<GameModel>&` to physics.
- [ ] The replacement path does not use model indices in new production APIs.
- [ ] Store refresh order remains deterministic.
- [ ] Solver, sleep, ragdoll, diagnostics, and fixed-tree release behavior are covered by the chosen validation gate.
- [ ] Search results are clean for `MakePhysicsModelView` before the slice is reported done.
- [ ] Search results are clean for `PhysicsModelView` before the slice is reported done.

## Phase 1 - Stable Identity and Mapping

Create durable handles before moving ownership. The stores cannot be authoritative if callers still smuggle identity through vector indices.

- [ ] Define or reuse stable entity identity for scene/game objects.
- [ ] Define or reuse stable body handles for physics bodies.
- [ ] Define or reuse stable collider handles for colliders.
- [ ] Define or reuse stable render instance handles for renderable projections.
- [ ] Define the relationship between entity id, body handle, collider handle, render instance handle, asset instance id, scene object id, and replay id.
- [ ] Add generation or validity checks where handles can outlive removed objects.
- [ ] Preserve deterministic iteration order for physics stepping and replay output.
- [ ] Replace new or touched model-index APIs with stable handles.
- [ ] Keep temporary model-index adapters only at old call boundaries.
- [ ] Add comments documenting handle lifetime, ownership, and invalidation rules.
- [ ] Add focused tests or assertions for stale handle rejection if the codebase has a suitable local test path.

Done when:

- [ ] New work can address bodies, colliders, render instances, and scene metadata without requiring a mutable `GameModel*`.
- [ ] Old model-index paths are identified as compatibility, not expanded.
- [ ] Deterministic ordering is documented and guarded.

## Phase 2 - Move Body Authority to `PhysicsBodyStore`

Move one body-state group at a time. Keep compatibility writeback narrow and temporary.

- [ ] Make `PhysicsBodyStore` the authoritative owner of body pose.
- [ ] Make `PhysicsBodyStore` the authoritative owner of linear and angular velocity.
- [ ] Make `PhysicsBodyStore` the authoritative owner of mass, inverse mass, inertia, and inverse inertia.
- [ ] Make `PhysicsBodyStore` the authoritative owner of fixed/dynamic state.
- [ ] Make `PhysicsBodyStore` the authoritative owner of sleep and wake state.
- [ ] Make `PhysicsBodyStore` the authoritative owner of accumulated forces and impulses.
- [ ] Change physics stepping to consume body handles/store views rather than `GameModelCollection&`.
- [ ] Route body creation through a single registration path that creates the entity/body mapping.
- [ ] Route body deletion through a single path that invalidates handles and removes store rows deterministically.
- [ ] Keep any required `GameModel` writeback behind an explicitly named compatibility function.
- [ ] Add a temporary comparison/assertion path if old and new state coexist during the slice.
- [ ] Remove old writes as soon as the final reader migrates.

Do-not-miss checklist:

- [ ] Gravity and external forces still apply in deterministic order.
- [ ] Sleep thresholds and wake events still use the same units and semantics.
- [ ] Fixed bodies cannot accidentally accumulate dynamic velocities.
- [ ] Impulses are cleared exactly once per step.
- [ ] Body transforms used by collision, replay, and rendering refer to the same frame of simulation.
- [ ] Replay CSV output remains byte-exact unless the slice intentionally changes behavior.
- [ ] Comments explain any temporary dual-write or writeback path and name its planned removal phase.

## Phase 3 - Move Collider Authority to `ColliderStore`

Colliders should own exact collision data. `GameModel` must not remain the hidden source for shapes or materials.

- [ ] Make `ColliderStore` own collision shape handles or value records.
- [ ] Make `ColliderStore` own material/contact parameters such as friction, restitution, density, and collision flags.
- [ ] Make `ColliderStore` own broadphase bounds and dirty flags.
- [ ] Make `ColliderStore` own body-to-collider and collider-to-body mapping.
- [ ] Move shape creation from `GameModel` construction into a collider registration path.
- [ ] Move shape mutation into explicit collider update commands.
- [ ] Update broadphase code to read collider bounds from `ColliderStore`.
- [ ] Update narrowphase code to read exact shapes from `ColliderStore`.
- [ ] Preserve persistent contact keys across the migration where possible.
- [ ] Preserve collision filtering behavior.
- [ ] Preserve hull metadata and baked hull assumptions.
- [ ] Delete or mark old `GameModel` collider fields as compatibility once all production readers move.

Do-not-miss checklist:

- [ ] Compound shapes and asset-authored hulls still resolve through registered assets and baked hull metadata.
- [ ] Scene-created colliders and asset-instance colliders use the same registration path.
- [ ] Broadphase cache invalidation happens when collider shape, body pose, or scale changes.
- [ ] Collision material defaults match existing config behavior.
- [ ] Debug drawing and SkullScope diagnostics can still name or identify colliders.
- [ ] Any changed `.hull` file is baked with `tools\bake_hulls.py --write` before validation.

## Phase 4 - Move Render Projection Out of `GameModelCollection`

Rendering should consume render projection stores, not production physics/game-object containers.

- [ ] Identify all production render paths that consume `GameModelCollection` or `IRenderSceneView`.
- [ ] Make `RenderInstanceStore` the authoritative owner of visible render instance records.
- [ ] Add or reuse an update path that projects final body/entity transforms into render instances after simulation.
- [ ] Route render material, mesh, visibility, and transform updates through render instance handles.
- [ ] Migrate the main runtime renderer to consume `RenderInstanceStore` directly.
- [ ] Migrate shadow, reflection, debug, terrain/object, and DXR paths only when their dependencies are understood.
- [ ] Keep editor-only wrappers separate from production render submission.
- [ ] Remove `GameModelCollection : Rendering::IRenderSceneView` only after all production render callers migrate.

Do-not-miss checklist:

- [ ] Rendering reads a stable post-simulation transform snapshot.
- [ ] Physics interpolation or replay pose override behavior is preserved.
- [ ] Hidden, deleted, sleeping, and fixed objects keep their previous visibility semantics.
- [ ] Render instance deletion cannot leave stale GPU instance data.
- [ ] Renderer validation is selected for any behavior-changing render projection slice.

## Phase 5 - Split Scene, Editor, and Replay Metadata

Scene/editor/replay metadata should not force physics or render ownership to stay inside `GameModel`.

- [ ] Identify all metadata fields in `GameModel` that are not required for physics stepping.
- [ ] Move object names and labels to scene/entity metadata.
- [ ] Move collection grouping and hierarchy/root information to scene/entity metadata.
- [ ] Move asset instance identity to scene/entity metadata.
- [ ] Move editor selection identity to stable entity or render handles.
- [ ] Move replay identity to stable entity/body handles.
- [ ] Update scene load to create metadata, body, collider, and render records through one coordinated creation path.
- [ ] Update scene save to serialize from authoritative stores and metadata, not stale compatibility fields.
- [ ] Update replay capture to read from body handles and stable replay ids.
- [ ] Update replay playback or diagnostics to resolve through stable handles.
- [ ] Preserve old file compatibility where required by existing scene assets.

Do-not-miss checklist:

- [ ] Scene round-trip does not reorder objects unexpectedly unless documented.
- [ ] Asset instances still serialize through `assetInstances[]` when reusable assets are involved.
- [ ] Editor selection survives object creation, deletion, duplication, and scene reload.
- [ ] Replay artifacts remain deterministic and do not depend on transient vector indices.
- [ ] Any schema migration is documented in the plan handoff or commit notes.

## Phase 6 - Remove Compatibility Layers

Only remove compatibility after callers have moved and validation has covered the behavior surface.

- [ ] Verify `MakePhysicsModelView()` was already deleted by the required first slice.
- [ ] Verify `PhysicsModelView` was already deleted by the required first slice.
- [ ] Delete `GameModelCollection::PhysicsModels()` after production physics no longer uses it.
- [ ] Delete compatibility writeback from body store to `GameModel` after final reader migrates.
- [ ] Delete compatibility collider fields from `GameModel` after final reader migrates.
- [ ] Delete production render reliance on `GameModelCollection` after render callers migrate.
- [ ] Remove temporary allowlists that permitted compatibility reads or writes.
- [ ] Add search guardrails for banned production calls, including `MakePhysicsModelView`, `PhysicsModelView`, and any remaining direct production `GameModelCollection::PhysicsModels()` usage.
- [ ] Update comments and learning headers in every touched source-bearing file.
- [ ] Run `Agentic/Skills/comment-style-audit/skill.md` over every touched source-bearing file before reporting done.

Searches to run before declaring compatibility gone:

- [ ] `rg "MakePhysicsModelView" SkullbonezSource`
- [ ] `rg "PhysicsModels\(" SkullbonezSource`
- [ ] `rg "PhysicsModelView" SkullbonezSource`
- [ ] `rg "GameModelCollection.*IRenderSceneView|IRenderSceneView" SkullbonezSource`
- [ ] `rg "GetModelAtIndex|model index|modelIndex|ModelIndex" SkullbonezSource`
- [ ] `rg "GameModel" SkullbonezSource/Physics SkullbonezSource/Runtime SkullbonezSource/Rendering`

## Validation Plan

Repository validation scripts are PR/commit gates. Do not run them repeatedly while iterating unless the user explicitly asks for validation.

- [ ] Documentation-only changes: no repository validation required.
- [ ] Body, collider, solver, command buffer, or physics determinism changes: run `tools\validate_physics.bat` before PR-bound commit.
- [ ] Broad physics diagnostics, SkullScope baselines, query baselines, or deep fixture changes: run `tools\validate_physics_deep.bat`.
- [ ] Render projection or render instance behavior changes: run `tools\validate_dx12_renderer.bat`.
- [ ] Storage/hot-loop changes that may affect per-frame allocations or broadphase cost: run `tools\validate_perf.bat`.
- [ ] Runtime lifecycle, scene/replay, or mixed broad-scope changes: run `tools\validate_full.bat`.
- [ ] Tooling script changes: run `tools\validate_fast.bat`, then run the changed script.
- [ ] If unsure at PR gate: run `tools\agent_validate.bat`.

Validation evidence checklist:

- [ ] Capture the exact command.
- [ ] Capture meaningful output lines.
- [ ] Capture the log path if output is mirrored to a file.
- [ ] Confirm zero warnings for builds.
- [ ] Confirm physics CSV byte-exact match when physics validation is required.
- [ ] Confirm zero DX12 validation errors when renderer validation is required.
- [ ] Report any skipped required validation as an explicit blocker, not as success.

## Final Acceptance Checklist

- [ ] Body state authority lives in `PhysicsBodyStore`.
- [ ] Collider authority lives in `ColliderStore`.
- [ ] Render projection authority lives in `RenderInstanceStore`.
- [ ] `MakePhysicsModelView()` is deleted and not replaced by another per-frame model-vector adapter.
- [ ] `PhysicsModelView` is deleted.
- [ ] Scene/entity metadata is separate from simulation body state.
- [ ] New production APIs use stable handles rather than model vector indices.
- [ ] Production physics stepping no longer requires `GameModelCollection&`.
- [ ] Production rendering no longer requires `GameModelCollection` as the scene view.
- [ ] Replay capture and diagnostics use stable entity/body identity.
- [ ] Temporary compatibility layers are either removed or explicitly documented with a removal phase.
- [ ] All touched source-bearing files pass the repository comment quality gate.
- [ ] Required validation for the actual code slice has been run and reported.
- [ ] `git status --short --branch` has been checked before handoff or commit.

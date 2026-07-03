# Physics / GameModel Authority Plan

Date: 2026-06-27
Status: In progress
Impact areas: physics, game model data ownership, scene system, replay, rendering projection, tests
Validation for latest source slice: `tools\validate_full.bat` passed on
2026-07-03 after deleting the physics inheritance/event-sink surface and
turning `PhysicsModelAccess` into a concrete stack-owned facade.

## Completed Slices

- [x] 2026-06-27: Deleted `GameModelCollection::MakePhysicsModelView()` and `SkullbonezSource/Physics/PhysicsModelView.h`.
- [x] 2026-06-27: Replaced per-call `PhysicsModelView` construction with persistent `PhysicsModelAccess` ranges plus explicit body-store handle mapping.
- [x] 2026-06-27: Converted store refresh, step, wake, seed-asleep, immediate impulse, pending impulse, diagnostics, ragdoll, and sleep-island call paths away from `PhysicsModelView`.
- [x] 2026-06-27: Added project-filter and runtime-boundary guardrails so `MakePhysicsModelView` or `PhysicsModelView` cannot return unnoticed.
- [x] 2026-06-27: Validated the deleted-view slice with `tools\validate_fast.bat`, `tools\validate_physics.bat`, and `tools\validate_perf.bat`.
- [x] 2026-06-27: Added a counted runtime-boundary allowlist so current `PhysicsModels()` compatibility callers are explicit and any new direct caller fails validation.
- [x] 2026-06-28: Deleted the neutral `GameModelCollection::PhysicsModels()` API name; remaining vector borrowers now call explicit compatibility accessors.
- [x] 2026-06-28: Added a counted guardrail for the named physics model vector compatibility accessors so the temporary seam cannot grow accidentally.
- [x] 2026-06-28: Added `GameModelCollectionPhysicsAdapter` as the explicit
  compatibility boundary for existing model-index physics commands. The old
  `WakeModel`, `SeedModelAsleep`, `ApplyBodyImpulse`, and
  `SetPendingBodyImpulse` entry points now resolve `PhysicsBodyHandle` through
  the adapter before calling `PhysicsEngine`, and the adapter also provides a
  `PhysicsSceneObjectId` lookup path for scene/runtime migration. Duplicate
  replay-derived scene object ids fail closed instead of choosing the first
  vector slot. This is a bridge, not final authority: replay, editor, and
  runtime callers still need durable handle storage in later slices. Validation
  evidence is recorded in
  `Agentic/Plans/IN PROGRESS/carmack-physics-standalone-boundary-plan.md`.
- [x] 2026-07-03: Replaced the temporary `PersistentContactSolverContext`
  event/writeback sink split with compact solver side-effect queues. Persistent
  contact solving now appends plain pipeline records, visual body indices, fixed
  contact body indices, model-mirror writebacks, release wake bodies, and fixed
  tree release events into `PersistentContactSolverSideEffects`; `PhysicsWorld`
  applies those owner-side consequences after `Solve()`. The checker now blocks
  `PhysicsBodyWritebackSink` and any model/event/world callback reference inside
  `PersistentContactSolverContext`. Validation: `tools\validate_build.bat
  Profile`, `tools\check_runtime_boundaries.py --repo .`, and
  `tools\validate_physics.bat` passed.
- [x] 2026-07-03: Removed `GameModelCollection` inheritance from
  `PhysicsModelAccess` and the deleted `PhysicsBodyEventSink`. Runtime and
  replay stepping now construct a stack-owned `PhysicsModelAccess` facade, and
  `PhysicsWorld` calls explicit owner commands for fixed-contact highlights and
  fixed-tree release instead of virtual event callbacks.

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
- [x] 2026-07-04: Listed all remaining runtime render callers that went through
  `IRenderSceneView`; they were confined to `RenderFrameContext`,
  `ShadowPass`, `ReflectionPass`, `ObjectPass`, and `DebugOverlayPass`.
  `GameModelCollection` is still the concrete legacy render projection until
  `RenderInstanceStore` becomes authoritative.
- [ ] Record the inventory in a short handoff note under `Agentic/Reports/` if the slice is not completed in one sitting.

## Required First Track - Bootstrap Handles, Then Delete `MakePhysicsModelView()`

`MakePhysicsModelView()` is a contrived compatibility factory. It rebuilds a borrowed view at each call site, keeps physics aware of legacy model order and SoA invalidation, and hides scene callbacks behind generic function pointers. The first implementation slice should remove this boundary instead of polishing it.

Deleted-view call-site table from the completed first slice:

| Deleted call site | Classification | Replacement |
| --- | --- | --- |
| `RefreshBodyStore()` | body store refresh | `PhysicsModelAccess` passed directly to `PhysicsEngine::RefreshBodyStore()` |
| `RefreshColliderStore()` | collider store refresh | `PhysicsModelAccess` passed directly to `PhysicsEngine::RefreshColliderStore()` |
| `RefreshRenderStore()` | render store refresh | `PhysicsModelAccess` passed directly to `PhysicsEngine::RefreshRenderStore()` |
| `RunPhysics()` | physics step | `PhysicsEngine::Step(PhysicsModelAccess&, dt)` |
| `WakeModel()` | wake command | `PhysicsBodyHandle` plus `PhysicsModelAccess` |
| `SeedModelAsleep()` | sleep seed command | `PhysicsBodyHandle` plus `PhysicsModelAccess` |
| `ApplyBodyImpulse()` | immediate impulse command | `PhysicsBodyHandle` plus `PhysicsModelAccess` |
| `SetPendingBodyImpulse()` | pending impulse command | `PhysicsBodyHandle` plus `PhysicsModelAccess` |

Deleted-view responsibility split:

| Old `PhysicsModelView` responsibility | Current replacement |
| --- | --- |
| Borrow mutable model order | mutable `PhysicsModelAccess::Models()` range |
| Borrow const model order | `PhysicsModelAccess::Models()` range |
| Borrow body stream cache | `PhysicsModelAccess::GetBodyStream()` and `InvalidatePhysicsStreams()` |
| Hidden fixed-tree callback | `PhysicsModelAccess::ReleaseAttachedFixedTreeParts()` |
| Hidden SkullScope callback | `PhysicsModelAccess::EmitSkullScopeFrame()` |
| Model-index body identity | `PhysicsBodyStore` compatibility handle map via `PhysicsBodyHandle` |

Deleted-view parameter surface:

| Old `PhysicsModelView&` parameter group | Replacement surface |
| --- | --- |
| `PhysicsEngine` refresh, step, wake, seed-asleep, impulse, and pending-impulse methods | `PhysicsModelAccess&`; command targets use `PhysicsBodyHandle` |
| `PhysicsScene` refresh, run, wake, seed-asleep, impulse, and pending-impulse methods | `PhysicsModelAccess&`; command targets resolve through `PhysicsBodyStore` |
| `PhysicsWorld` solver, contact, diagnostics, underwater sleep, fixed-contact, tornado, wake-island, and step methods | `PhysicsModelAccess&` plus explicit `PhysicsBodyStore&` where body rows are required |
| `PersistentContactSolverContext` fixed-contact/writeback/release callbacks | `PersistentContactSolverSideEffects&` output queues applied by `PhysicsWorld` after `Solve()` |
| `Ragdoll::SolvePointJoints()` | `PhysicsModelAccess&` plus `PhysicsBodyStore&` |
| `SleepIslandSystem::PropagateSupport()` | `PhysicsModelAccess&` plus sleep support context |
| `PhysicsDiagnosticsSink` frame/collision-time emission | `PhysicsModelAccess&` |

Prerequisite handle-bootstrap slice:

- [x] Bootstrap the existing `PhysicsBodyHandle` path as an explicit compatibility handle layer before deleting the view.
- [ ] Promote the existing `PhysicsBodyHandle` path out of model-index compatibility.
- [x] Add or reuse a deterministic compatibility body handle mapping owned by the physics/body store layer.
- [ ] Ensure the mapping can resolve old model-index callers only at explicit compatibility boundaries.
  - [x] 2026-06-28 adapter slice routes touched model-index command callers
    through `GameModelCollectionPhysicsAdapter`, giving the compatibility
    mapping one named deletion target.
- [ ] Ensure new physics APIs accept stable handles or store rows, not raw `GameModel` indices.
- [x] Document any remaining compatibility handle conversion with a deletion target in this plan.
- [x] Prove the bootstrap does not change body iteration order or replay ordering.

Acceptable endpoint:

- [x] `GameModelCollection::RunPhysics()` no longer constructs a `PhysicsModelView`.
- [ ] `PhysicsEngine::Step()` receives authoritative stores or a named step context built from authoritative stores.
- [x] Wake, seed-asleep, impulse, and pending-impulse APIs take `PhysicsBodyHandle` plus the required store/context references.
- [x] Diagnostics and SkullScope emission use explicit diagnostics context, not `PhysicsModelView`.
- [x] Attached fixed-tree release uses an explicit scene callback/service, not a generic `void*` callback hidden in `PhysicsModelView`.
- [x] `GameModelCollection` does not expose a new per-frame view over `m_gameModels` as a substitute.

Implementation checklist:

- [x] Create a short call-site table for every `MakePhysicsModelView()` call in `GameModelCollection.cpp`.
- [x] Create a short parameter table for every `PhysicsModelView&` parameter in `PhysicsEngine`, `PhysicsScene`, `PhysicsWorld`, diagnostics, ragdoll, sleep, and solver code.
- [x] Split `PhysicsModelView` responsibilities into named dependencies: body store, collider store, render store, deterministic model/body ordering if still needed, fixed-tree release callback, and diagnostics callback.
- [ ] Introduce a narrowly named step context only if passing the stores separately becomes noisy; the context must not own or expose `std::vector<GameModel>&`.
- [x] Convert `PhysicsEngine::RefreshStores`, `RefreshPhysicsStores`, `RefreshBodyStore`, `RefreshColliderStore`, and `RefreshRenderStore` to store/source inputs that do not require `PhysicsModelView`.
- [x] Convert `PhysicsEngine::Step` and `PhysicsScene::RunPhysics` away from `PhysicsModelView`.
- [x] Convert wake and impulse helpers away from `PhysicsModelView`.
- [x] Convert physics diagnostics away from `PhysicsModelView`.
- [x] Convert ragdoll and sleep-island code away from `PhysicsModelView`.
- [x] Delete `GameModelCollection::MakePhysicsModelView()` declaration and definition.
- [x] Delete `SkullbonezSource/Physics/PhysicsModelView.h` once all includes are gone.
- [x] Remove stale comments that describe `PhysicsModelView` as the migration boundary.
- [x] Add a guardrail or boundary check that fails if `MakePhysicsModelView` or `PhysicsModelView` returns after deletion.

Do-not-miss checklist:

- [x] The replacement path does not allocate or rebuild a model view each physics operation.
- [x] The replacement path does not expose mutable `std::vector<GameModel>&` to physics.
- [ ] The replacement path does not use model indices in new production APIs.
- [x] Store refresh order remains deterministic.
- [x] Solver, sleep, ragdoll, diagnostics, and fixed-tree release behavior are covered by the chosen validation gate.
- [x] Search results are clean for `MakePhysicsModelView` before the slice is reported done.
- [x] Search results are clean for `PhysicsModelView` before the slice is reported done.

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
  - [x] 2026-06-28 adapter slice keeps the temporary model-index command bridge
    at the old `GameModelCollection` entry points instead of adding another
    physics-facing model-index API.
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
  - [x] 2026-07-03 production stepping signatures no longer accept
    `GameModelCollection&` or rely on `GameModelCollection` inheriting physics
    interfaces. The remaining bridge is the concrete `PhysicsModelAccess`
    facade, which still forwards to the collection until body/render/replay
    readers migrate to store-owned state.
- [ ] Route body creation through a single registration path that creates the entity/body mapping.
- [ ] Route body deletion through a single path that invalidates handles and removes store rows deterministically.
- [ ] Keep any required `GameModel` writeback behind an explicitly named compatibility function.
  - [x] 2026-07-03 persistent contact solving no longer reaches through broad
    `PhysicsModelAccess` or a virtual writeback sink from inside the solver.
    It queues body mirror writebacks as plain side-effect body indices, then
    `PhysicsWorld` applies the existing model-owner writeback after the solve
    until render, replay, and diagnostics consume physics-owned body rows
    directly.
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

- [x] 2026-07-04: Identified the production render paths that consumed
  `IRenderSceneView`; the inheritance/interface was deleted and the remaining
  production debt is direct `GameModelCollection` render projection use.
- [ ] Make `RenderInstanceStore` the authoritative owner of visible render instance records.
- [ ] Add or reuse an update path that projects final body/entity transforms into render instances after simulation.
- [ ] Route render material, mesh, visibility, and transform updates through render instance handles.
- [ ] Migrate the main runtime renderer to consume `RenderInstanceStore` directly.
- [ ] Migrate shadow, reflection, debug, terrain/object, and DXR paths only when their dependencies are understood.
- [ ] Keep editor-only wrappers separate from production render submission.
- [x] 2026-07-04: Removed `GameModelCollection : Rendering::IRenderSceneView`
  and deleted the one-implementation migration interface. Production rendering
  still reads the concrete collection, so full `RenderInstanceStore` migration
  remains open above.

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

- [x] Verify `MakePhysicsModelView()` was already deleted by the required first slice.
- [x] Verify `PhysicsModelView` was already deleted by the required first slice.
- [x] Delete `GameModelCollection::PhysicsModels()` after production physics no longer uses it. The vector compatibility seam remains under explicit `*PhysicsModelsForCompatibility()` accessors.
- [ ] Delete compatibility writeback from body store to `GameModel` after final reader migrates.
  - [x] 2026-07-03 persistent contact solver writeback is no longer a solver
    callback or virtual sink; the remaining model mirror update is an owner-side
    post-solve application step. Full deletion is still pending final reader
    migration.
- [ ] Delete compatibility collider fields from `GameModel` after final reader migrates.
- [ ] Delete production render reliance on concrete `GameModelCollection` after
  render callers migrate to `RenderInstanceStore`.
- [ ] Remove temporary allowlists that permitted compatibility reads or writes.
- [x] Add search guardrails for banned production calls, including `MakePhysicsModelView`, `PhysicsModelView`, and any remaining direct production `GameModelCollection::PhysicsModels()` usage.
  - [x] 2026-07-03 added a guardrail that rejects model/event/world callback
    references inside `PersistentContactSolverContext` and blocks the deleted
    `PhysicsBodyWritebackSink` type from source.
  - [x] 2026-07-03 added guardrails that block the deleted
    `PhysicsBodyEventSink` type and reject classes deriving from
    `PhysicsModelAccess`/`PhysicsBodyEventSink`.
- [ ] Update comments and learning headers in every touched source-bearing file.
- [ ] Run `Agentic/Skills/comment-style-audit/skill.md` over every touched source-bearing file before reporting done.

Searches to run before declaring compatibility gone:

- [x] `rg "MakePhysicsModelView" SkullbonezSource`
- [x] `rg "PhysicsModels\(" SkullbonezSource`
- [x] `rg "PhysicsModelView" SkullbonezSource`
- [x] `rg "GameModelCollection.*IRenderSceneView|IRenderSceneView" SkullbonezSource`
- [ ] `rg "GetModelAtIndex|model index|modelIndex|ModelIndex" SkullbonezSource`
- [ ] `rg "GameModel" SkullbonezSource/Physics SkullbonezSource/Runtime SkullbonezSource/Rendering`

## Validation Plan

Repository validation scripts are PR/commit gates. Do not run them repeatedly while iterating unless the user explicitly asks for validation.

- [ ] Documentation-only changes: no repository validation required.
- [x] Body, collider, solver, command buffer, or physics determinism changes: run `tools\validate_physics.bat` before PR-bound commit.
- [ ] Broad physics diagnostics, SkullScope baselines, query baselines, or deep fixture changes: run `tools\validate_physics_deep.bat`.
- [x] Render projection or render instance behavior changes: run `tools\validate_dx12_renderer.bat`.
  - [x] 2026-07-04 `tools\validate_dx12_renderer.bat` passed; log mirrored to
    `TestOutput\agent_validate_dx12_renderer_render_scene_view.log`, DX12
    InfoQueue errors were 0, and screenshots matched committed baselines.
- [x] Storage/hot-loop changes that may affect per-frame allocations or broadphase cost: run `tools\validate_perf.bat`.
- [ ] Runtime lifecycle, scene/replay, or mixed broad-scope changes: run `tools\validate_full.bat`.
- [x] Tooling script changes: run `tools\validate_fast.bat`, then run the changed script.
  - [x] 2026-07-04 `tools\validate_fast.bat` passed; log mirrored to
    `TestOutput\agent_validate_fast_render_scene_view.log`. The changed
    `tools\check_runtime_boundaries.py --repo .` checker also passed with 0
    errors in `TestOutput\agent_runtime_boundaries_render_scene_view.log`.
- [ ] If unsure at PR gate: run `tools\agent_validate.bat`.

Validation evidence checklist:

- [x] Capture the exact command.
- [x] Capture meaningful output lines.
- [x] Capture the log path if output is mirrored to a file.
- [x] Confirm zero warnings for builds.
- [x] Confirm physics CSV byte-exact match when physics validation is required.
- [x] Confirm zero DX12 validation errors when renderer validation is required.
- [ ] Report any skipped required validation as an explicit blocker, not as success.

## Final Acceptance Checklist

- [ ] Body state authority lives in `PhysicsBodyStore`.
- [ ] Collider authority lives in `ColliderStore`.
- [ ] Render projection authority lives in `RenderInstanceStore`.
- [x] `MakePhysicsModelView()` is deleted and not replaced by another per-frame model-vector adapter.
- [x] `PhysicsModelView` is deleted.
- [ ] Scene/entity metadata is separate from simulation body state.
- [ ] New production APIs use stable handles rather than model vector indices.
- [ ] Production physics stepping no longer requires `GameModelCollection&`.
- [ ] Production rendering no longer requires `GameModelCollection` as the scene view.
- [ ] Replay capture and diagnostics use stable entity/body identity.
- [ ] Temporary compatibility layers are either removed or explicitly documented with a removal phase.
- [x] All touched source-bearing files pass the repository comment quality gate.
- [x] Required validation for the actual code slice has been run and reported.
- [ ] `git status --short --branch` has been checked before handoff or commit.

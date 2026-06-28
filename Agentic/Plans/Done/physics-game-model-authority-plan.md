# Physics / GameModel Authority Plan

Date: 2026-06-27
Status: Deferred to `Agentic/Plans/IN PROGRESS/TODO.md`; archived plan is not source-complete.
Impact areas: physics, game model data ownership, scene system, replay, rendering projection, tests
Validation for this plan edit: Documentation-only. No repository validation required.

## 2026-06-29 Deferred Archive Disposition

This plan is being archived as a deferred coordination record, not as
source-complete implementation evidence. The remaining checklist items are too
broad for the current plan-clearing pass and remain source-backed. Their active
owner is now `Agentic/Plans/IN PROGRESS/TODO.md` under
`2026-06-29 Deferred Plan Owner Index / Physics / GameModel Authority`.

Any checked item below that says `Transferred to TODO 2026-06-29` means the work
was moved to that TODO owner. It does not mean the underlying source migration
is implemented.

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

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not change solver math while moving ownership unless a separate bug is proven and documented.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not refresh physics baselines unless behavior intentionally changes and the matching physics gate is rerun after the baseline update.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not introduce a broad ECS rewrite. Use the smallest registry/store shape that fits current engine data and call sites.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not migrate every editor, replay, scene, and render caller in one oversized diff.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not replace model indices with unstable raw pointers.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not hide behavior changes behind compatibility writeback.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not add parallel physics execution until ownership, ordering, and determinism are already stable.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Do not replace `MakePhysicsModelView()` with another per-frame adapter over `m_gameModels`; that would keep the same bad boundary under a new name.

## Phase 0 - Startup, Inventory, and Slice Choice

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Follow the repository Agent Startup Contract before editing.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Confirm the current branch and dirty state with `git status --short --branch`; treat pre-existing dirty files as user-owned.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read this plan and the current handoff in `Agentic/SessionState.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Read `Agentic/Plans/engine-architecture-next-steps-plan.md`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Skim `Agentic/Plans/game-model-data-boundary-plan.md` for older context, but prefer current source over old assumptions.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Choose one implementation slice only; do not try to complete every phase in one PR unless explicitly requested.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) State the selected impact area before editing: physics, scene system, rendering projection, tests, or documentation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) State the deferred PR-gate validation command before editing.

Inventory checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) List every authoritative-looking field in `SkullbonezSource/GameObjects/GameModel.h`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List every mutating production API in `SkullbonezSource/GameObjects/GameModelCollection.h` and `.cpp`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all `GameModelCollection::PhysicsModels()` call sites.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all `PhysicsModelView` call sites and the compatibility behavior they still rely on.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List every `MakePhysicsModelView()` call and classify it as step, wake, impulse, store refresh, diagnostics, ragdoll, sleep, or debug.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all places that read or mutate physics body pose, velocity, mass, inertia, sleep, force, and impulse state.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all places that read or mutate collider shape, material, filtering, and bounds state.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all places that use model indices for physics commands, replay, editor selection, scene persistence, or diagnostics.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) List all rendering callers that still read renderable state through `GameModelCollection` or `IRenderSceneView`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Record the inventory in a short handoff note under `Agentic/Reports/` if the slice is not completed in one sitting.

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
| `PersistentContactSolverContext` fixed-contact and wake callbacks | `PhysicsModelAccess&` callback surface |
| `Ragdoll::SolvePointJoints()` | `PhysicsModelAccess&` plus `PhysicsBodyStore&` |
| `SleepIslandSystem::PropagateSupport()` | `PhysicsModelAccess&` plus sleep support context |
| `PhysicsDiagnosticsSink` frame/collision-time emission | `PhysicsModelAccess&` |

Prerequisite handle-bootstrap slice:

- [x] Bootstrap the existing `PhysicsBodyHandle` path as an explicit compatibility handle layer before deleting the view.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Promote the existing `PhysicsBodyHandle` path out of model-index compatibility.
- [x] Add or reuse a deterministic compatibility body handle mapping owned by the physics/body store layer.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure the mapping can resolve old model-index callers only at explicit compatibility boundaries.
  - [x] 2026-06-28 adapter slice routes touched model-index command callers
    through `GameModelCollectionPhysicsAdapter`, giving the compatibility
    mapping one named deletion target.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Ensure new physics APIs accept stable handles or store rows, not raw `GameModel` indices.
- [x] Document any remaining compatibility handle conversion with a deletion target in this plan.
- [x] Prove the bootstrap does not change body iteration order or replay ordering.

Acceptable endpoint:

- [x] `GameModelCollection::RunPhysics()` no longer constructs a `PhysicsModelView`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `PhysicsEngine::Step()` receives authoritative stores or a named step context built from authoritative stores.
- [x] Wake, seed-asleep, impulse, and pending-impulse APIs take `PhysicsBodyHandle` plus the required store/context references.
- [x] Diagnostics and SkullScope emission use explicit diagnostics context, not `PhysicsModelView`.
- [x] Attached fixed-tree release uses an explicit scene callback/service, not a generic `void*` callback hidden in `PhysicsModelView`.
- [x] `GameModelCollection` does not expose a new per-frame view over `m_gameModels` as a substitute.

Implementation checklist:

- [x] Create a short call-site table for every `MakePhysicsModelView()` call in `GameModelCollection.cpp`.
- [x] Create a short parameter table for every `PhysicsModelView&` parameter in `PhysicsEngine`, `PhysicsScene`, `PhysicsWorld`, diagnostics, ragdoll, sleep, and solver code.
- [x] Split `PhysicsModelView` responsibilities into named dependencies: body store, collider store, render store, deterministic model/body ordering if still needed, fixed-tree release callback, and diagnostics callback.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Introduce a narrowly named step context only if passing the stores separately becomes noisy; the context must not own or expose `std::vector<GameModel>&`.
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
- [x] (Transferred to TODO 2026-06-29; not source-complete.) The replacement path does not use model indices in new production APIs.
- [x] Store refresh order remains deterministic.
- [x] Solver, sleep, ragdoll, diagnostics, and fixed-tree release behavior are covered by the chosen validation gate.
- [x] Search results are clean for `MakePhysicsModelView` before the slice is reported done.
- [x] Search results are clean for `PhysicsModelView` before the slice is reported done.

## Phase 1 - Stable Identity and Mapping

Create durable handles before moving ownership. The stores cannot be authoritative if callers still smuggle identity through vector indices.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define or reuse stable entity identity for scene/game objects.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define or reuse stable body handles for physics bodies.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define or reuse stable collider handles for colliders.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define or reuse stable render instance handles for renderable projections.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Define the relationship between entity id, body handle, collider handle, render instance handle, asset instance id, scene object id, and replay id.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add generation or validity checks where handles can outlive removed objects.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Preserve deterministic iteration order for physics stepping and replay output.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Replace new or touched model-index APIs with stable handles.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Keep temporary model-index adapters only at old call boundaries.
  - [x] 2026-06-28 adapter slice keeps the temporary model-index command bridge
    at the old `GameModelCollection` entry points instead of adding another
    physics-facing model-index API.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add comments documenting handle lifetime, ownership, and invalidation rules.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add focused tests or assertions for stale handle rejection if the codebase has a suitable local test path.

Done when:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) New work can address bodies, colliders, render instances, and scene metadata without requiring a mutable `GameModel*`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Old model-index paths are identified as compatibility, not expanded.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Deterministic ordering is documented and guarded.

## Phase 2 - Move Body Authority to `PhysicsBodyStore`

Move one body-state group at a time. Keep compatibility writeback narrow and temporary.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of body pose.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of linear and angular velocity.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of mass, inverse mass, inertia, and inverse inertia.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of fixed/dynamic state.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of sleep and wake state.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `PhysicsBodyStore` the authoritative owner of accumulated forces and impulses.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Change physics stepping to consume body handles/store views rather than `GameModelCollection&`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Route body creation through a single registration path that creates the entity/body mapping.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Route body deletion through a single path that invalidates handles and removes store rows deterministically.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Keep any required `GameModel` writeback behind an explicitly named compatibility function.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add a temporary comparison/assertion path if old and new state coexist during the slice.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove old writes as soon as the final reader migrates.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Gravity and external forces still apply in deterministic order.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Sleep thresholds and wake events still use the same units and semantics.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Fixed bodies cannot accidentally accumulate dynamic velocities.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Impulses are cleared exactly once per step.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Body transforms used by collision, replay, and rendering refer to the same frame of simulation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Replay CSV output remains byte-exact unless the slice intentionally changes behavior.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Comments explain any temporary dual-write or writeback path and name its planned removal phase.

## Phase 3 - Move Collider Authority to `ColliderStore`

Colliders should own exact collision data. `GameModel` must not remain the hidden source for shapes or materials.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `ColliderStore` own collision shape handles or value records.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `ColliderStore` own material/contact parameters such as friction, restitution, density, and collision flags.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `ColliderStore` own broadphase bounds and dirty flags.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `ColliderStore` own body-to-collider and collider-to-body mapping.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move shape creation from `GameModel` construction into a collider registration path.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move shape mutation into explicit collider update commands.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update broadphase code to read collider bounds from `ColliderStore`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update narrowphase code to read exact shapes from `ColliderStore`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Preserve persistent contact keys across the migration where possible.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Preserve collision filtering behavior.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Preserve hull metadata and baked hull assumptions.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Delete or mark old `GameModel` collider fields as compatibility once all production readers move.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Compound shapes and asset-authored hulls still resolve through registered assets and baked hull metadata.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Scene-created colliders and asset-instance colliders use the same registration path.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Broadphase cache invalidation happens when collider shape, body pose, or scale changes.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Collision material defaults match existing config behavior.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Debug drawing and SkullScope diagnostics can still name or identify colliders.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Any changed `.hull` file is baked with `tools\bake_hulls.py --write` before validation.

## Phase 4 - Move Render Projection Out of `GameModelCollection`

Rendering should consume render projection stores, not production physics/game-object containers.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify all production render paths that consume `GameModelCollection` or `IRenderSceneView`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Make `RenderInstanceStore` the authoritative owner of visible render instance records.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Add or reuse an update path that projects final body/entity transforms into render instances after simulation.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Route render material, mesh, visibility, and transform updates through render instance handles.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Migrate the main runtime renderer to consume `RenderInstanceStore` directly.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Migrate shadow, reflection, debug, terrain/object, and DXR paths only when their dependencies are understood.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Keep editor-only wrappers separate from production render submission.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove `GameModelCollection : Rendering::IRenderSceneView` only after all production render callers migrate.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Rendering reads a stable post-simulation transform snapshot.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Physics interpolation or replay pose override behavior is preserved.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Hidden, deleted, sleeping, and fixed objects keep their previous visibility semantics.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Render instance deletion cannot leave stale GPU instance data.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Renderer validation is selected for any behavior-changing render projection slice.

## Phase 5 - Split Scene, Editor, and Replay Metadata

Scene/editor/replay metadata should not force physics or render ownership to stay inside `GameModel`.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Identify all metadata fields in `GameModel` that are not required for physics stepping.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move object names and labels to scene/entity metadata.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move collection grouping and hierarchy/root information to scene/entity metadata.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move asset instance identity to scene/entity metadata.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move editor selection identity to stable entity or render handles.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move replay identity to stable entity/body handles.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update scene load to create metadata, body, collider, and render records through one coordinated creation path.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update scene save to serialize from authoritative stores and metadata, not stale compatibility fields.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update replay capture to read from body handles and stable replay ids.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update replay playback or diagnostics to resolve through stable handles.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Move fixed-tree release ownership out of `PhysicsModelAccess` /
  `GameModelCollection` and into a stable scene/entity or runtime adapter path
  that can be found independently of the superseded Carmack plan.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Preserve old file compatibility where required by existing scene assets.

Do-not-miss checklist:

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Scene round-trip does not reorder objects unexpectedly unless documented.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Asset instances still serialize through `assetInstances[]` when reusable assets are involved.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Editor selection survives object creation, deletion, duplication, and scene reload.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Replay artifacts remain deterministic and do not depend on transient vector indices.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Any schema migration is documented in the plan handoff or commit notes.

## Phase 6 - Remove Compatibility Layers

Only remove compatibility after callers have moved and validation has covered the behavior surface.

- [x] Verify `MakePhysicsModelView()` was already deleted by the required first slice.
- [x] Verify `PhysicsModelView` was already deleted by the required first slice.
- [x] Delete `GameModelCollection::PhysicsModels()` after production physics no longer uses it. The vector compatibility seam remains under explicit `*PhysicsModelsForCompatibility()` accessors.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Delete compatibility writeback from body store to `GameModel` after final reader migrates.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Delete compatibility collider fields from `GameModel` after final reader migrates.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Delete production render reliance on `GameModelCollection` after render callers migrate.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Remove temporary allowlists that permitted compatibility reads or writes.
- [x] Add search guardrails for banned production calls, including `MakePhysicsModelView`, `PhysicsModelView`, and any remaining direct production `GameModelCollection::PhysicsModels()` usage.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Update comments and learning headers in every touched source-bearing file.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Run `Agentic/Skills/comment-style-audit/skill.md` over every touched source-bearing file before reporting done.

Searches to run before declaring compatibility gone:

- [x] `rg "MakePhysicsModelView" SkullbonezSource`
- [x] `rg "PhysicsModels\(" SkullbonezSource`
- [x] `rg "PhysicsModelView" SkullbonezSource`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "GameModelCollection.*IRenderSceneView|IRenderSceneView" SkullbonezSource`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "GetModelAtIndex|model index|modelIndex|ModelIndex" SkullbonezSource`
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `rg "GameModel" SkullbonezSource/Physics SkullbonezSource/Runtime SkullbonezSource/Rendering`

## Validation Plan

Repository validation scripts are PR/commit gates. Do not run them repeatedly while iterating unless the user explicitly asks for validation.

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Documentation-only changes: no repository validation required.
- [x] Body, collider, solver, command buffer, or physics determinism changes: run `tools\validate_physics.bat` before PR-bound commit.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Broad physics diagnostics, SkullScope baselines, query baselines, or deep fixture changes: run `tools\validate_physics_deep.bat`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Render projection or render instance behavior changes: run `tools\validate_dx12_renderer.bat`.
- [x] Storage/hot-loop changes that may affect per-frame allocations or broadphase cost: run `tools\validate_perf.bat`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Runtime lifecycle, scene/replay, or mixed broad-scope changes: run `tools\validate_full.bat`.
- [x] Tooling script changes: run `tools\validate_fast.bat`, then run the changed script.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) If unsure at PR gate: run `tools\agent_validate.bat`.

Validation evidence checklist:

- [x] Capture the exact command.
- [x] Capture meaningful output lines.
- [x] Capture the log path if output is mirrored to a file.
- [x] Confirm zero warnings for builds.
- [x] Confirm physics CSV byte-exact match when physics validation is required.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Confirm zero DX12 validation errors when renderer validation is required.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Report any skipped required validation as an explicit blocker, not as success.

## Final Acceptance Checklist

- [x] (Transferred to TODO 2026-06-29; not source-complete.) Body state authority lives in `PhysicsBodyStore`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Collider authority lives in `ColliderStore`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Render projection authority lives in `RenderInstanceStore`.
- [x] `MakePhysicsModelView()` is deleted and not replaced by another per-frame model-vector adapter.
- [x] `PhysicsModelView` is deleted.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Scene/entity metadata is separate from simulation body state.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) New production APIs use stable handles rather than model vector indices.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Production physics stepping no longer requires `GameModelCollection&`.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Production rendering no longer requires `GameModelCollection` as the scene view.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Replay capture and diagnostics use stable entity/body identity.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) Temporary compatibility layers are either removed or explicitly documented with a removal phase.
- [x] All touched source-bearing files pass the repository comment quality gate.
- [x] Required validation for the actual code slice has been run and reported.
- [x] (Transferred to TODO 2026-06-29; not source-complete.) `git status --short --branch` has been checked before handoff or commit.

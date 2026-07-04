# Physics Standalone Strict Goal Checklist

Date: 2026-07-02
Status: In progress
Impact area: physics, runtime adapter, scene setup, replay, diagnostics, tests
Validation for this plan edit: Documentation-only. No repository validation required.

## Goal

Finish the standalone physics goal from the Carmack-test verdict: physics should
be usable through stable handles, descriptors, commands, immutable views, and
deterministic diagnostics without requiring `GameModelCollection`, renderer
state, editor tools, scene UI, or broad runtime ownership on the normal physics
step boundary.

This file is the precise execution checklist. It does not replace the broader
authority plans; it turns their remaining open pieces into ordered work with
validation and evidence requirements.

Related source-of-truth plans:

- `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`
- `Agentic/Plans/physics-game-model-authority-plan.md`
- `Agentic/Plans/game-model-data-boundary-plan.md`

Companion agent queue:

- `Agentic/Plans/In_Progress/physics-standalone-agent-workqueue.csv`

## Overnight Agent Protocol

Use the CSV queue for unattended execution. The Markdown file is the human
overview and rationale; the CSV is the resumable work queue.

- Load the CSV, not this whole Markdown file, when choosing work.
- Pick the first `todo` row whose `depends_on` rows are `done`.
- Mark exactly one row `active` before editing source.
- Load only the `read_scope` for that row plus any direct compiler or test
  evidence needed to finish it.
- Keep each source diff to the active row's area. If another area is required,
  mark the row `blocked` with the reason and choose the next unblocked row.
- After completing a row, update its `status`, keep `notes` short, and put
  detailed evidence in the implementation handoff or commit notes.
- Stop phase progression on a red validation gate. Do not refresh physics
  baselines just to make a storage migration pass.
- Before stopping for the night, run `git status --short --branch` and update
  the final handoff row in the CSV so the next agent can resume without chat
  context.

Suggested one-row loader:

```powershell
Import-Csv Agentic\Plans\In_Progress\physics-standalone-agent-workqueue.csv |
    Where-Object { $_.status -eq 'todo' } |
    Sort-Object {[int]$_.order} |
    Select-Object -First 1
```

## Current Blocking Facts

- `PhysicsEngine::Step()` still takes `PhysicsModelAccess&` and forwards it to
  `PhysicsScene::RunPhysics()`.
  Evidence: `SkullbonezSource/Physics/PhysicsEngine.cpp:77`.
- `PhysicsScene::RunPhysics()` still loads from model-backed state, solves
  through stores, then writes back to model-backed state every step.
  Evidence: `SkullbonezSource/Physics/PhysicsScene.cpp:147`.
- `PhysicsBodyStore` still exposes compatibility load/writeback helpers through
  `PhysicsModelAccess`.
  Evidence: `SkullbonezSource/Physics/PhysicsBodyStore.cpp:192`,
  `SkullbonezSource/Physics/PhysicsBodyStore.cpp:266`.
- `ColliderStore` still refreshes collider rows from `GameModel` or
  `PhysicsModelAccess` and preserves compatibility model order.
  Evidence: `SkullbonezSource/Physics/ColliderStore.cpp:55`,
  `SkullbonezSource/Physics/ColliderStore.cpp:94`.
- `PhysicsStandaloneWorld::Contacts()` and `PhysicsStandaloneWorld::Islands()`
  still return stable empty public views because standalone collision and sleep
  island generation have not migrated yet.
  Evidence: `SkullbonezSource/Physics/PhysicsApi.cpp:793`,
  `SkullbonezSource/Physics/PhysicsApi.cpp:803`.
- `GameModelCollectionPhysicsAdapter` is still the named compatibility bridge
  from model index or scene object id to `PhysicsBodyHandle`.
  Evidence: `SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp:48`,
  `SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp:60`.
- Authored scene setup still receives both `GameModelCollection&` and
  `PhysicsEngine&`, so scene creation still builds through runtime/game-object
  storage rather than a standalone physics creation API.
  Evidence: `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h:87`.

## Definition Of Done

- [ ] `PhysicsEngine::Step()` can step an owned physics world without
  `PhysicsModelAccess`, `GameModelCollection`, or `GameModel`.
- [ ] `PhysicsBodyStore` owns pose, velocity, mass, inertia, sleep, force, and
  impulse state for the standalone path.
- [ ] `ColliderStore` owns shapes, material response, broadphase radius,
  collision metadata, and body/collider mapping for the standalone path.
- [ ] Public contact and island views return real standalone data for a
  collision/sleep sample, not only empty views.
- [ ] Runtime scene objects adapt to physics handles at explicit boundaries.
- [ ] Replay/editor/runtime command paths use physics handles for physics
  commands; model indices remain presentation-only where still needed.
- [ ] Boundary guardrails reject new physics-layer dependencies on
  `GameModelCollection`, raw `GameModel`, or mutable model vectors.
- [ ] Deterministic physics validation remains byte-exact unless a behavior
  change is intentional, documented, and regenerated through the required final
  physics gate.

## Phase 0 - Baseline And Inventory

- [x] Run `git status --short --branch` and protect unrelated dirty work.
- [x] Run `python tools/check_runtime_boundaries.py --repo .` and record the
  current boundary result before touching source.
- [x] Run these searches and paste the counts into the implementation handoff:
  - [x] `rg -n "PhysicsModelAccess" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`
    baseline count: 132.
  - [x] `rg -n "GameModelCollection" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`
    baseline count: 498.
  - [x] `rg -n "ModelIndex|modelIndex|GetModelAtIndex" SkullbonezSource/Physics SkullbonezSource/GameObjects SkullbonezSource/Runtime`
    baseline count: 661.
  - [x] `rg -n "Contacts\\(|Islands\\(" SkullbonezSource/Physics`
    baseline count: 47.
- [x] Reconcile the search results against `tools/check_runtime_boundaries.py`
  allowlists before choosing a source slice.
- [x] Pick exactly one first implementation slice from Phase 1, 2, 3, 4, or 5.
  Do not combine body authority, collider authority, replay, and diagnostics in
  one diff.
- [x] State the selected validation gate before editing. Most source slices here
  require at least `tools\validate_physics.bat`.

## Phase 1 - Strict Standalone Step Surface

Target: add a store-owned step path while keeping the existing compatibility
step alive until runtime call sites migrate.

- [x] Add a standalone step input descriptor,
  `PhysicsStandaloneStepDesc`, that contains only deterministic physics inputs
  for the current standalone body integration surface: `deltaSeconds`, world
  acceleration, traceable fixed-step metadata, frame id, and the scene-physics
  enable gate. Solver, contact, sleep, and diagnostics inputs stay out until
  later phases consume them.
- [x] Add `PhysicsStandaloneWorld::Step(const PhysicsStandaloneStepDesc&)` for
  current body integration without model-backed storage. Collision/contacts and
  island/sleep authority remain scoped to Phases 3-5, where their standalone
  storage is introduced.
- [x] Keep `PhysicsEngine::Step(PhysicsModelAccess&, float)` as compatibility
  only, with comments naming the deletion target.
- [x] Add a second internal step path in `PhysicsScene` only if needed. Not
  needed for this Phase 1 slice because `PhysicsStandaloneWorld` already owns
  the public store step; `RunPhysics(PhysicsModelAccess&, float)` remains the
  named compatibility path until the body/collider authority phases move runtime
  stores behind the same boundary.
- [x] Make the standalone path impossible to call with `GameModelCollection`,
  `GameModel`, or `PhysicsModelAccess`.
- [x] Add smoke coverage proving the standalone step path advances at least two
  dynamic bodies without runtime/window/renderer startup.
- [x] Validation for this phase:
  - [x] `tools\validate_physics.bat`
  - [x] `tools\validate_fast.bat` not required; no guardrail or project files
    changed.
  - [x] `tools\validate_format.bat`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] Touched-file comment audit: `PhysicsApi.h`, `PhysicsApi.cpp`,
    `PhysicsEngine.cpp`, and `Runtime/Init.cpp`; no deferred source files.

## Phase 2 - Body Store Authority

Target: `PhysicsBodyStore` becomes the source for body state on the standalone
path, then compatibility writeback shrinks behind explicit adapters.

- [x] Inventory every body field still loaded from `GameModel`:
  pose, orientation, linear velocity, angular velocity, mass, inverse mass,
  rotational inertia, inverse inertia, fixed/dynamic state, sleeping, forces,
  immediate impulse, pending impulse, drag, and replay body id.
- [x] Add or confirm a `PhysicsBodyCreateDesc` field for every body property
  needed to create the store row without reading `GameModel`.
- [ ] Add or confirm a `PhysicsBodyUpdateDesc` field and update mask for every
  runtime-editable body property.
- [x] Route standalone body creation through `PhysicsStandaloneWorld` and
  `PhysicsBodyStore`, not through a model vector.
- [x] Route body deletion through one deterministic tombstone/generation path:
  - [x] body handle becomes stale,
  - [x] child colliders become stale,
  - [x] connected constraints become stale,
  - [x] contacts and island membership are invalidated deterministically,
  - [x] replay-facing ids stay queryable for diagnostics if appropriate.
- [x] Move force and impulse application into body-store commands keyed by
  `PhysicsBodyHandle`.
- [ ] Add a temporary compatibility assertion comparing old writeback state to
  store-owned state for one validation scene, if that helps catch drift.
- [ ] Remove one `PhysicsBodyStore::*ModelAccess*` usage per slice and lower the
  guardrail count in the same commit.
- [x] Delete the standalone `PhysicsBodyView`/generation/liveness mirror and add
  a runtime-boundary guardrail that blocks it from returning.
- [ ] Validation for this phase:
  - [x] `tools\validate_physics.bat`
  - [ ] `tools\validate_perf.bat` if hot-loop layout, reserve behavior, or
    iteration order changes.

Strict-step authority slice `PHY-0207H`: contact-highlight ticking moved out of
`PhysicsWorld::RunPhysics` and into `PhysicsScene::RunPhysics`. The tick is
model-owned presentation state, so `PhysicsWorld` no longer reaches through
`PhysicsModelAccess` for that timer while stepping body/collider stores. The
boundary checker now rejects `modelAccess.TickContactHighlights(...)` in
`PhysicsWorld.cpp`. Evidence: py_compile and runtime-boundary checks passed with
0 errors; focused Debug build passed in 4.4s with 0 warnings/errors;
`tools\validate_fast.bat` passed in 24.9s; `tools\validate_physics.bat` passed in
14.3s with byte-exact `physics_regression_solver.csv`; `tools\validate_perf.bat`
passed on rerun in 21.5s after an initial +0.06 MB memory-threshold miss.

Strict-step authority slice `PHY-0207I`: post-step model stream
invalidation moved out of `PhysicsWorld::RunPhysics` and into
`PhysicsScene::RunPhysics` immediately after the world step returns. Other
explicit `PhysicsWorld` wake/release paths may still invalidate after their
side effects; the new checker blocks only `InvalidatePhysicsStreams()` inside
`PhysicsWorld::RunPhysics`. Evidence: py_compile and runtime-boundary checks
passed with 0 errors; focused Debug build passed in 4.8s with 0 warnings/errors;
`tools\validate_fast.bat` passed on rerun in 27.3s after targeted formatting;
`tools\validate_physics.bat` passed in 14.3s with byte-exact
`physics_regression_solver.csv`; `tools\validate_perf.bat` passed in 22.6s with
no DX12 or PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207J`: bulk compatibility writeback
moved out of `PhysicsWorld::RunPhysics` and into `PhysicsScene::RunPhysics`.
`PhysicsWorld` now finishes solver state and emits Debug diagnostics from the
stores; `PhysicsScene` then mirrors `PhysicsBodyStore` to GameModel for the
remaining editor/replay presentation consumers and invalidates model streams.
The new checker blocks `WriteBackPhysicsBodies()` from returning to
`PhysicsWorld::RunPhysics`. Evidence: py_compile and runtime-boundary checks
passed with 0 errors; focused Debug build passed in 4.4s with 0 warnings/errors;
`tools\validate_fast.bat` passed in 24.9s; `tools\validate_physics.bat` passed in
14.2s with byte-exact `physics_regression_solver.csv`; `tools\validate_perf.bat`
passed in 22.6s with no DX12 or PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207K`: fixed-contact highlight
notification moved out of `PhysicsWorld` side-effect application. The persistent
solver already fills `fixedContactBodies`; `PhysicsWorld` now exposes that queue
read-only and `PhysicsScene` applies `NotifyFixedContact()` at the compatibility
edge without adding a per-frame copy. The new checker blocks
`NotifyFixedContact()` from returning to `PhysicsWorld.cpp`. Evidence:
py_compile and runtime-boundary checks passed with 0 errors; focused Debug build
passed in 8.4s with 0 warnings/errors; `tools\validate_fast.bat` passed in
28.0s; `tools\validate_physics.bat` passed in 13.8s with byte-exact
`physics_regression_solver.csv`; `tools\validate_perf.bat` passed in 22.5s with
no DX12 or PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207L`: persistent-contact fixed-tree
release moved from `PhysicsWorld` model-owner side effects to the
`PhysicsScene` store edge. `GameModelCollection` still supplies tree grouping
metadata, but the release now writes live motion state to `PhysicsBodyStore`
directly, wakes released bodies from the scene edge, emits Debug diagnostics
after scene-side store effects, then performs the single compatibility
writeback. The checker blocks `modelAccess.ReleaseAttachedFixedTreeParts(...)`
from returning to `PhysicsWorld::ApplyPersistentContactSideEffects` and blocks
Debug diagnostics from returning to `PhysicsWorld::RunPhysics`. Evidence:
py_compile and runtime-boundary checks passed with 0 errors; focused Debug build
passed in 8.6s with 0 warnings/errors; `tools\validate_fast.bat` passed on rerun
in 34.6s after targeted header alignment; `tools\validate_physics.bat` passed in
13.9s with byte-exact `physics_regression_solver.csv`; `tools\validate_perf.bat`
passed in 22.1s with no DX12 or PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207M`: tornado fixed-tree release no
longer mirrors a source body into `GameModel`, calls the legacy model-owned tree
release, then reloads the full `PhysicsBodyStore`. `PhysicsBodyStore` now owns a
shared `ReleaseFixedRecord()` transition that restores inverse mass/inertia,
clears sleep, and seeds velocities directly on the live record. The persistent
contact solver, store-owned tree release, and tornado release all use that
transition. `PhysicsWorld::ApplyTornadoField` applies attached-tree release
through the store overload with a reused wake list and relies on the scene-edge
compatibility writeback. The checker blocks
`WriteBackPhysicsBody`, legacy `ReleaseAttachedFixedTreeParts`,
`ReloadPhysicsBodies`, and `InvalidatePhysicsStreams` from returning to the
tornado release path while allowing the store overload. Evidence: diff check,
py_compile, runtime-boundary checks, and `tools\validate_format.bat` passed;
focused Debug build passed in 8.7s with 0 warnings/errors;
`tools\validate_physics.bat` passed in 20.9s with byte-exact
`physics_regression_solver.csv`; `tools\validate_fast.bat` passed in 22.0s;
`tools\validate_perf.bat` first failed on PHYSICS_BENCH memory at +5.34 MB
against a +5.0 MB threshold, then rerun passed in 21.3s with no DX12 or
PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207N`: store-owned sleep seeding no
longer rebuilds `GameModelBodyStream` or invalidates GameModel stream caches
inside `PhysicsWorld`. The public store overload now reads `PhysicsBodyStore`
records directly and reuses the same sleep-state mutation locally, while
`PhysicsScene::SeedBodyAsleep(PhysicsModelAccess&, PhysicsBodyHandle)` owns the
remaining one-body compatibility writeback and explicit cache invalidation. The
checker blocks store/body-record `PhysicsWorld::SeedModelAsleep` overloads from
touching `GameModelBodyStream`, `GetBodyStream`, or
`modelAccess.InvalidatePhysicsStreams()` while leaving the legacy model-stream
overload visible as remaining debt. Evidence: diff check, py_compile,
runtime-boundary checks, and `tools\validate_format.bat` passed; focused Debug
build passed in 7.8s with 0 warnings/errors; `tools\validate_physics.bat` passed
in 20.7s with byte-exact `physics_regression_solver.csv`;
`tools\validate_fast.bat` passed in 21.9s; `tools\validate_perf.bat` first
failed on DX12 memory at +5.02 MB against a +5.0 MB threshold, then rerun passed
in 21.3s with no DX12 or PHYSICS_BENCH regressions.

Strict-step authority slice `PHY-0207O`: store-owned wake propagation no
longer carries `PhysicsModelAccess` through `PhysicsWorld` just to invalidate
GameModel streams. The store `WakeModel` overloads, store island wake helpers,
point-joint connected wake propagation, and persistent-contact side-effect wake
fan-out now operate on `PhysicsBodyStore`/body records only. Explicit
`PhysicsScene::WakeBody(PhysicsModelAccess&, PhysicsBodyHandle)` owns the
remaining one-body compatibility writeback plus invalidation, and
`PhysicsScene::RunPhysics()` remains the single post-step compatibility
invalidation for solver/tornado/release wake propagation. The checker blocks
store wake overloads from taking `PhysicsModelAccess`, rebuilding
`GameModelBodyStream`, or calling `modelAccess.InvalidatePhysicsStreams()`
inside `PhysicsWorld`, while leaving legacy model-stream wake overloads visible
as remaining debt. Evidence: diff check, py_compile, runtime-boundary checks,
and `tools\validate_format.bat` passed; focused Debug build first caught a dead
`modelAccess` parameter, then passed with 0 warnings/errors;
`tools\validate_physics.bat` passed in 23.8s with byte-exact
`physics_regression_solver.csv`; `tools\validate_fast.bat` passed in 22.1s;
`tools\validate_perf.bat` first failed on PHYSICS_BENCH frame and memory
thresholds, then rerun passed in 21.5s with no DX12 or PHYSICS_BENCH
regressions.

Latest strict-step authority slice `PHY-0207P`: the dead
`PhysicsWorld` model-stream wake/seed path is deleted instead of left as visible
debt. `PhysicsWorld` no longer exposes or implements `WakeModel` or
`SeedModelAsleep` overloads that take `PhysicsModelAccess` or
`GameModelBodyStream`, and the helper wake-island variants now exist only in the
store-owned body-record form. The checker blocks `GameModelBodyStream`,
`GetBodyStream`, and public model-access wake/seed signatures from returning to
`PhysicsWorld.cpp` or `PhysicsWorld.h`, while allowing the scene edge to own
remaining compatibility writeback/cache invalidation. Evidence: diff check,
py_compile, runtime-boundary checks, and `tools\validate_format.bat` passed;
focused Debug build passed in 8.0s with 0 warnings/errors;
`tools\validate_physics.bat` passed in 20.7s with byte-exact
`physics_regression_solver.csv`; `tools\validate_fast.bat` passed in 21.8s;
`tools\validate_perf.bat` passed in 22.3s with no DX12 or PHYSICS_BENCH
regressions.

## Phase 3 - Collider Store Authority

Target: `ColliderStore` owns the standalone collision shape and metadata surface.

- [x] Inventory every collider field still loaded from `GameModel`:
  shape kind, exact shape payload, bounding radius, restitution, friction,
  contact material id, projected surface area, drag coefficient, replay body id,
  scene object id, body handle, and legacy model index.
- [x] Add or confirm `PhysicsColliderCreateDesc` fields for exact shape data and
  material/contact response.
- [x] Add or confirm `PhysicsColliderUpdateDesc` masks for shape, material
  response, broadphase values, and local offsets.
- [x] Make standalone collider creation attach to a live `PhysicsBodyHandle`.
- [x] Make collider deletion and body deletion tombstone collider handles in one
  deterministic path.
- [x] Move broadphase candidate generation to `ColliderStore` plus body-store
  transforms on the standalone path.
- [ ] Move narrowphase shape reads to collider records on the standalone path.
  - [x] Sphere/sphere standalone contact generation reads `BoundingSphere`
    shapes from `ColliderStore` records and body transforms.
  - [x] Sphere/box standalone contact generation reads `BoundingSphere` and
    `BoundingBox` shapes from `ColliderStore` records and body transforms.
  - [ ] Remaining standalone shape pairs still need exact collider-record
    narrowphase.
- [x] Preserve existing conservative query semantics while exact shape tests are
  added.
- [x] Add smoke coverage with at least:
  - [x] sphere/sphere contact,
  - [x] sphere/box contact,
  - [x] fixed body plus dynamic body contact,
  - [x] deleted collider stale-handle rejection,
  - [x] material/restitution copied into the resulting contact view.

Current Phase 3 progress: standalone collider creation/update/delete, raycast,
and broadphase queries now use `ColliderStore` records directly. Sphere/sphere
and sphere/box contact generation now read collider-record shape/material rows
and body-store transforms, then expose public contact views with material,
restitution, and friction evidence. Remaining shape pairs are future contact
coverage, not blockers for the Phase 3 smoke row. Prior slice evidence:
`python tools\check_runtime_boundaries.py --repo .` passed with 0 errors, and
`tools\validate_physics.bat` passed with byte-exact
`physics_regression_solver.csv`. Latest sphere/box slice evidence: standalone
smoke reports `contacts=2` and `contact_hash=0x5DBDF5257E90EA9B`; repeated
smoke reports were byte-identical; boundary checker passed with 0 errors; and
`tools\validate_physics.bat` passed with byte-exact
`physics_regression_solver.csv`.
- [ ] Validation for this phase:
  - [x] `tools\validate_physics.bat`
  - [ ] `tools\validate_physics_deep.bat` if SkullScope/query baselines change.

## Phase 4 - Real Standalone Contacts

Target: `PhysicsStandaloneWorld::Contacts()` returns deterministic immutable
contact rows for standalone collision samples.

- [x] Define standalone contact storage owned by the physics world or a contact
  store, not by `GameModelCollection`.
- [x] Reuse the existing public `PhysicsContactView` shape unless a documented
  gap requires an additive field. Added material, restitution/friction, and
  feature-id fields because the existing view could not carry the required
  diagnostics/replay payload.
- [ ] Generate contact rows from standalone broadphase/narrowphase using body
  and collider handles.
  - [x] Sphere/sphere rows are generated from standalone body and collider
    handles in collider-store order.
  - [x] Sphere/box rows are generated from standalone body and collider handles
    in collider-store order.
  - [ ] Later shape pairs still need exact row generation.
- [ ] Include stable deterministic ordering:
  - [x] body pair order,
  - [x] collider slot order,
  - [x] feature id order,
  - [ ] terrain/fixed-body ordering if terrain enters standalone scope.
- [x] Include enough contact data for diagnostics and replay:
  body handles, collider handles, point, normal, penetration, normal impulse
  when solved, material ids, and feature id.
- [x] Keep solver-private manifolds private; public views are immutable copies
  or stable read-only spans with documented lifetime.
- [x] Extend `--physics-standalone-smoke` to require nonzero contact count for a
  deterministic collision sample and hash the contact rows.

Current Phase 4 progress: `PhysicsStandaloneWorld` owns a contact-view vector
rebuilt from standalone collider/body records after `Step()`. The smoke now
requires fixed/dynamic sphere/sphere plus sphere/box contacts and hashes both
contact rows. This is not the full shape-coverage story yet: later shape pairs
remain open and terrain ordering is out of scope until terrain enters standalone
contacts. Slice validation passed with the boundary checker at 0 errors and
`tools\validate_physics.bat` byte-exact.
- [ ] Validation for this phase:
  - [x] `tools\validate_physics.bat`
  - [ ] `tools\validate_physics_deep.bat` if query baselines or diagnostics
    outputs change.

## Phase 5 - Real Standalone Islands And Sleep

Target: `PhysicsStandaloneWorld::Islands()` returns deterministic island rows,
and sleep authority is not hidden in the legacy world path.

- [ ] Move sleep state and support propagation data needed by standalone bodies
  into store-owned structures.
  - [x] Standalone island generation reads body-store sleep/fixed state.
  - [ ] Full support propagation data remains future work.
- [x] Define `PhysicsIslandView` data that is sufficient for diagnostics:
  island id, body handles, sleeping/awake state, support state, and reason flags
  if already available.
- [x] Generate island membership from standalone contacts/constraints in
  deterministic body-handle order.
- [x] Make `SetSleepEnabled`, `WakeBody`, and `SeedBodyAsleep` update the same
  store-owned sleep state used by island generation.
- [x] Extend the standalone smoke with:
  - [x] two bodies in one island,
  - [x] one isolated body in a separate island,
  - [x] wake propagation,
  - [x] sleep-disable behavior,
  - [x] stale body exclusion.

Current Phase 5 progress: `PhysicsStandaloneWorld::Islands()` now returns
store-owned island rows after `Step()`. Island rows point into a flat
body-handle buffer owned by the world; generation uses live body rows, contact
rows, and point-joint endpoints in deterministic body-store order. The
standalone smoke now requires connected, isolated, wake, sleep-disable, and
stale-exclusion island evidence. Slice validation passed with the boundary
checker at 0 errors and `tools\validate_physics.bat` byte-exact.
- [ ] Validation for this phase:
  - [x] `tools\validate_physics.bat`
  - [ ] `tools\validate_physics_deep.bat` if SkullScope sleep/island query
    baselines change.

## Phase 6 - Constraints And Joints

Target: constraints are handle-owned and participate in standalone stepping,
islands, deletion, and diagnostics.

Latest evidence: standalone point-joint descriptors, updates, and views are
already `PhysicsBodyHandle` / `PhysicsConstraintHandle` backed. Runtime ragdoll
construction, authored scene point joints, and the runtime handle smoke now
create joints through `PhysicsPointJointCreateDesc`; `PhysicsWorld` is the only
place that converts that descriptor into a `PointJointConstraint` solver row.
The old raw-row `AddPointJointConstraint` facade/scene/world/collection wrapper
was deleted. The smoke also includes a constraint-only island case where two
colliderless bodies merge through a live point joint and split after
`DestroyConstraint()`.

- [x] Confirm point-joint standalone records already use `PhysicsBodyHandle`
  endpoints and `PhysicsConstraintHandle` lifetime.
- [x] Move legacy scene/ragdoll `PointJointConstraint` use toward handle-backed
  descriptors.
- [x] Make same-body, stale-body, and deleted-body failures part of smoke
  evidence for every public constraint command.
- [x] Include constraints in island generation.
- [x] Add query/view coverage for live constraints if diagnostics or replay need
  it.
- [x] Validation for this phase:
  - [x] `tools\validate_physics.bat`

## Phase 7 - Runtime Adapter Migration

Target: runtime/game-object code adapts to physics handles, and compatibility
model indices stop entering physics commands directly.

- [ ] Keep `GameModelCollectionPhysicsAdapter` as the only model-index to body
  handle bridge while migration is underway.
- [ ] For each legacy model-index physics command, migrate one caller group:
  - [x] scene setup,
  - [x] editor tools,
  - [x] mouse pickup tools,
  - [x] launcher tools,
  - [x] replay velocity edit,
  - [x] replay restore/prediction,
  - [x] replay/editor transform restore wake,
  - [x] ragdoll start-asleep seed,
  - [x] diagnostics and debug overlays.
- [ ] Store `PhysicsBodyHandle` or stable scene object id at the caller where the
  command is created, not at the last moment inside physics.
- [ ] Keep model indices only for UI selection and render presentation while
  those surfaces still use model order.
- [ ] Add count guardrails for adapter call sites and lower the count after each
  migrated group.
- [x] Delete old `GameModelCollection` wrapper methods only after all callers in
  that group migrate.
- [x] Validation for this phase:
  - [x] `tools\validate_fast.bat` for guardrail/project changes.
  - [x] `tools\validate_physics.bat` for physics command behavior.
  - [x] `tools\validate_full.bat` for replay/editor/scene lifecycle breadth.

Current Phase 7 progress: authored and generated scene setup now resolves
`PhysicsBodyHandle` at construction time through the existing adapter, then
calls handle-keyed `PhysicsEngine` pending-impulse and sleep-seed commands.
`tools/check_runtime_boundaries.py` now rejects direct
`context.models.SetPendingBodyImpulse`, `SeedModelAsleep`, `WakeModel`, or
`ApplyBodyImpulse` calls in Runtime/Scene setup code. Evidence for this slice:
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_fast.bat`, and `tools\validate_full.bat` all passed.

Editor placement and gizmo transform wake/sleep commands now keep model indices
as editor selection identity only. The command boundary resolves a
`PhysicsBodyHandle` through editor-local helpers, then calls
`PhysicsEngine::WakeBody` or `PhysicsEngine::SeedBodyAsleep` with that handle.
The boundary checker now rejects direct `context.models` or `collection`
model-index physics wrappers in the migrated editor files. Evidence for this
slice: `python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_full.bat` all passed.

Mouse pickup now keeps its model index as interaction identity only. The
physics step revalidates the picked model, resolves a `PhysicsBodyHandle`
through the runtime tool boundary, and applies the impulse through
`PhysicsEngine::ApplyBodyImpulse`. The boundary checker now rejects
`m_cGameModelCollection` model-index physics wrappers from returning in
`RunMousePickupTools.inl`. Evidence for this slice:
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_physics.bat` all passed.

Launcher laser impact and projectile spawn/wake now keep model indices as
launcher hit/spawn identity only. The launcher boundary resolves
`PhysicsBodyHandle` and calls `PhysicsEngine::ApplyBodyImpulse` or
`PhysicsEngine::WakeBody`. The boundary checker now rejects `collection`
model-index physics wrappers from returning in `RuntimeTools.cpp`. Evidence for
this slice: `python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_full.bat` all passed.

Replay velocity edit now keeps model indices as replay/UI selection identity
only. The drag command resolves a `PhysicsBodyHandle`, then calls
`PhysicsEngine::SetBodyVelocity`; `PhysicsBodyStore` owns the handle-only
velocity mutation and `PhysicsScene` performs one compatibility row writeback
for current presentation consumers. The boundary checker now rejects
`GameModel` velocity writes and `GameModelCollection` model-index physics
wrappers from returning in `RunReplayVelocityEdit.inl`. Evidence for this
slice: `python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_full.bat` all passed.

Replay render-pose restore/prediction overlays now stay presentation-only:
`TrySetReplayRenderPose` updates the model transform for drawing and no longer
recaptures that pose into `PhysicsBodyStore`. The boundary checker blocks
`TrySetReplayRenderPose` from calling `CommitEditedModelPhysicsState`,
`RefreshBodyFromModel`, or `GetPhysicsBodyStore`. Evidence for this slice:
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_full.bat` all passed.

Replay prediction body restore now backs up mass and inertia with pose,
velocity, and fixed state, then restores through
`PhysicsEngine::RestoreReplayBodyState` instead of recapturing `GameModel`
state with `CommitEditedModelPhysicsState`. Existing full replay solver restore
already used the store-owned restore path, so replay restore/prediction no
longer has a model-to-body-store refresh in the migrated functions. Evidence for
this slice: `python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_format.bat`, `tools\validate_fast.bat`, and
`tools\validate_full.bat` all passed.

Ragdoll construction no longer calls the `GameModelCollection::SeedModelAsleep`
model-index wrapper for start-asleep parts. It reuses the existing body handles
already required for joint creation and enters physics through
`PhysicsEngine::SeedBodyAsleep`. The boundary checker now rejects direct
`collection.SeedModelAsleep`, `WakeModel`, `ApplyBodyImpulse`, or
`SetPendingBodyImpulse` calls in `Ragdoll.cpp`. Diagnostics/debug overlays were
audited against the existing store-owned diagnostics path from `PHY-0207C`:
`PhysicsDiagnosticsSink` and SkullScope consume `PhysicsBodyStore` and
`ColliderStore` data for body/collider state, while the checker rejects the old
model-state mirror read. Evidence for this slice: py_compile, runtime-boundary,
focused Debug build, `tools\validate_format.bat`, `tools\validate_fast.bat`,
and `tools\validate_physics.bat` all passed. Direct wrapper deletion remains
open until the final caller scan and compiler prove the old collection wrappers
are dead.

Replay/editor transform restore no longer calls `GameModelCollection::WakeModel`
after applying the saved transform event. The restore path still uses the saved
model index as replay event identity, commits the edited model state at the
existing compatibility boundary, resolves the current `PhysicsBodyHandle` from
the body store, and wakes through `PhysicsEngine::WakeBody`. The boundary
checker now rejects `m_cGameModelCollection.WakeModel(...)` in `RunFrame.cpp`.
Evidence for this slice: py_compile, runtime-boundary, focused Debug build,
`tools\validate_fast.bat`, `tools\validate_physics.bat`, and
`tools\validate_full.bat` all passed; the full gate reported DX12 InfoQueue 0
errors, screenshots matching committed baselines, and byte-exact
`physics_regression_solver.csv`.

Wrapper deletion is complete for the obsolete `GameModelCollection` model-index
physics command wrappers: `WakeModel`, `SeedModelAsleep`, `ApplyBodyImpulse`,
and `SetPendingBodyImpulse` were removed from `GameModelCollection.h/.cpp`.
The only collection-internal self-call was fixed-tree release, which now calls
`GameModelCollectionPhysicsAdapter` directly while that model-owned release
path remains compatibility debt. CodeGraph and a source sweep found no external
callers before deletion, and the checker now rejects reintroducing those
collection wrappers. Evidence for this slice: py_compile, runtime-boundary,
focused Debug build, `tools\validate_fast.bat`, `tools\validate_physics.bat`,
and `tools\validate_full.bat` all passed; the full gate reported DX12 InfoQueue
0 errors, screenshots matching committed baselines, and byte-exact
`physics_regression_solver.csv`.

## Phase 8 - Diagnostics And SkullScope

Target: diagnostics consume physics views and bounded query output, not raw
model-backed ownership.

- [x] Define a diagnostics sink input made of immutable physics views:
  bodies, colliders, contacts, islands, constraints, broadphase stats, solver
  stats, and deterministic frame ids. Current design uses
  `PhysicsDiagnosticsFrameInput` plus `PhysicsDiagnosticsNameView`: body and
  collider facts come from physics stores, world facts come from
  `PhysicsDiagnosticsView`, point-joint constraints are named in that view, and
  presentation names are a cold pointer table supplied at the model/scene edge.
- [x] Move `PhysicsDiagnosticsSink` and SkullScope frame emission away from
  direct model vector reads one view at a time.
  - [x] Regression CSV emission builds rows from `PhysicsBodyStore`,
    `ColliderStore`, `PhysicsDiagnosticsView`, and the name view instead of
    calling `PhysicsModelAccess::TryGetPhysicsDiagnosticsModel`.
  - [x] SkullScope frame emission consumes `PhysicsDiagnosticsFrameInput` and
    uses the shared store-owned diagnostics row builder. The full
    `PhysicsModelAccess`/`GameModelCollection` diagnostics-record/view bridge
    is deleted.
- [x] Keep raw NDJSON/SQLite out of model context; use `tools\physics_query.bat`
  queries for investigation.
- [x] If output schema changes, update query baselines only from final Debug
  artifacts.
- [x] Report SkullScope query cost in final handoff whenever SkullScope is used:
  trace bytes, SQLite bytes, query commands, per-query output size, total
  GPT-read size.
- [ ] Validation for this phase:
  - [x] `tools\validate_physics_deep.bat`
  - [x] `tools\validate_physics.bat` if core deterministic CSV behavior is
    touched.

Current Phase 8 progress: `PHY-0801` defined the immutable diagnostics frame
input, `PHY-0802` moved the regression CSV row builder off the full
model-access diagnostics record, and `PHY-0803` moved `SkullScope::EmitFrame`
to the same store-owned frame input. `PhysicsWorld` now gathers the cold
presentation name table only when a Debug diagnostics output is enabled, holds a
local `PhysicsDiagnosticsView` for the emission call, and sends both regression
CSV and SkullScope through `PhysicsDiagnosticsFrameInput`. The deleted
`PhysicsModelAccess::TryGetPhysicsDiagnosticsModel`,
`PhysicsModelAccess::GetPhysicsDiagnosticsView`, and matching
`GameModelCollection` accessors cannot return in `PhysicsDiagnosticsSink.cpp`
or `SkullScope.cpp` because the boundary checker rejects
`modelAccess.TryGetPhysicsDiagnosticsModel(...)` there.

Validation evidence: diff check, py_compile, runtime-boundary checks, focused
Debug build, and `tools\validate_fast.bat` passed; `tools\validate_physics.bat`
passed with byte-exact `physics_regression_solver.csv`; `tools\validate_physics_deep.bat`
passed with exact `physics_query_varied.json` match. SkullScope accounting for
the deep gate: trace command is `Debug\SKULLBONEZ_CORE.exe --renderer dx12
--vsync off --shadows off --scene
SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag
Debug\physics_query_varied.physicsdiag.ndjson`; query packet is
`python tools\check_physics_query_regression.py`, which runs 21 bounded
`tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson ...`
queries. Raw artifacts: NDJSON 104,766,944 bytes, SQLite cache 51,146,752
bytes. Bounded packet size: `TestOutput\baselines\physics_query_varied.json`
180,019 bytes; summed per-query JSON payload sizes 113,637 characters. Raw
NDJSON/SQLite were not loaded into model context. Detailed command accounting:
`Agentic/Reports/2026-07-04/physics-skullscope-phase8-query-accounting.md`.
Follow-up focused gate: `tools\validate_physics_query.bat` passed and rebuilt
Debug with 0 warnings/0 errors.

## Phase 9 - Guardrails

- [x] Extend `tools/check_runtime_boundaries.py` to reject new
  `GameModelCollection`, raw `GameModel`, or mutable model-vector dependencies
  in public physics API and standalone physics implementation files.
- [x] Add negative synthetic tests for:
  - [x] public API header includes `GameModelCollection`,
  - [x] standalone physics source calls `modelAccess.Models()`,
  - [x] new model-index command field in a public physics descriptor,
  - [x] new compatibility accessor borrower without an allowlist update.
- [x] Add positive synthetic tests for:
  - [x] explicit runtime adapter use,
  - [x] diagnostics-only view consumption,
  - [x] test/tool-only fixture use.
- [x] Keep allowlists counted and exact by file/label/line group where possible.
- [x] Lower counts in the same commit that removes a compatibility borrower.
- [ ] Validation for this phase:
  - [x] `tools\validate_fast.bat`
  - [x] direct changed-script run if the guardrail script changes.

Current Phase 9 progress: public physics facade headers already rejected raw
`GameModelCollection`/`GameModel` types; this slice added descriptor-level
model-index field checks for public `*Desc` structs and scoped standalone
implementation checks for `PhysicsApi.cpp` so raw `GameModel`,
`std::vector<GameModel>`, or `modelAccess.Models()` cannot enter the standalone
proof path. Positive tests keep runtime adapter code, diagnostics-only views,
and test/tool fixtures out of that narrow standalone scan. The phase also
ratcheted deleted body-mirror names and added a solver hot-path per-body
writeback fence; future deletion commits must keep lowering exact counts or add
equivalent focused guardrails in the same slice. Evidence: CodeGraph-first
lookup followed by targeted source reads, comment-style audit of
`tools/check_runtime_boundaries.py`, `git diff --check`,
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .` with 0 errors,
`tools\validate_fast.bat`, and intermittent `tools\validate_physics.bat`.

## Phase 10 - Step Boundary / Store Authority Beachheads

- [x] Remove the pending-only `PhysicsScene::SetPendingBodyImpulse` model
  mirror so handle-keyed pending impulses mutate `PhysicsBodyStore` without a
  per-command `GameModel` writeback or model-stream invalidation.
- [x] Keep `ApplyBodyImpulse` scoped to the existing wake/presentation
  compatibility edge; do not broaden this slice into wake writeback deletion.
- [x] Add runtime-boundary self-tests that reject the deleted
  pending-impulse `WriteBackPhysicsBody`/`InvalidatePhysicsStreams` shape while
  allowing store-only pending impulses and the remaining wake compatibility
  path.
- [x] Move replay prediction body-state backup off `GameModel` physics getters:
  pose, orientation, velocity, mass, inertia, fixed state, and replay id now
  come from `PhysicsBodyStore` records; `GameModel` remains only for the
  fixed-contact presentation timer.
- [x] Add runtime-boundary self-tests that reject `GameModel` physics getters
  and the model-refreshing `GetPhysicsBodyStore()` accessor inside prediction
  body capture, while allowing direct `PhysicsEngine::BodyStore()` reads.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_physics.bat`

Current Phase 10 progress: pending impulses are still accepted through the
legacy `PhysicsModelAccess` overload when runtime callers need that signature,
but the command now stops after the authoritative store mutation. That deletes
one full-body compatibility writeback and one stream invalidation from the
pending-only command path without changing the explicit `ApplyBodyImpulse`
wake/presentation edge. Evidence logs: `TestOutput\agent_validate_fast_pending_impulse_mirror.log`
and `TestOutput\agent_validate_physics_pending_impulse_mirror.log`; the physics
gate passed with byte-exact `physics_regression_solver.csv`.

Follow-up slice `PHY-1002`: replay prediction backup now reads simulation state
from the current `PhysicsEngine::BodyStore()` rather than from `GameModel`
physics getters or the model-refreshing `GetPhysicsBodyStore()` helper. This
removes the immediate prediction dependency that made
`PhysicsScene::SetBodyVelocity` keep a one-body compatibility writeback after
velocity edits. Evidence logs:
`TestOutput\agent_validate_fast_prediction_body_capture.log` and
`TestOutput\agent_validate_physics_prediction_body_capture.log`; the physics
gate passed with byte-exact `physics_regression_solver.csv`. The next narrow
store-authority deletion should remove the velocity command's compatibility
writeback/invalidation if caller review still shows no model-state reader left
on that path.

## Required Evidence For Each Source Slice

- [ ] Exact source files touched.
- [ ] Which phase and checklist items were advanced.
- [ ] `git status --short --branch` before edits and before handoff/commit.
- [ ] CodeGraph or targeted source evidence for the changed boundary.
- [ ] Comment-style audit for every touched source-bearing file.
- [ ] Guardrail delta: counts removed, added, or intentionally retained.
- [ ] Validation command, log path, meaningful pass/fail lines, and elapsed time.
- [ ] If physics behavior changes: baseline update reason, final Debug artifact
  source, and rerun physics gate after the baseline update.

## Suggested First Three Implementation Slices

1. Standalone contacts beachhead:
   implement real contact rows for a small sphere/sphere standalone sample,
   keep the legacy runtime solver untouched, extend smoke/hash evidence, and run
   `tools\validate_physics.bat`.
2. Body-store authority beachhead:
   move one non-behavioral body state group out of compatibility writeback for
   standalone-only world creation, add guardrails, and run
   `tools\validate_physics.bat`.
3. Adapter count ratchet:
   pick one caller group currently using model-index command wrappers, store
   physics handles earlier, lower the adapter allowlist count, and run
   `tools\validate_fast.bat` plus the matching physics/full gate.

## Do Not Do

- [ ] Do not rewrite the solver while moving ownership.
- [ ] Do not refresh physics baselines to hide storage migration drift.
- [ ] Do not add another generic adapter over `std::vector<GameModel>`.
- [ ] Do not make render instances authoritative for physics.
- [ ] Do not let replay identity depend on transient vector index order after a
  handle-backed path exists.
- [ ] Do not mark standalone physics complete while contacts or islands are
  still empty by construction.

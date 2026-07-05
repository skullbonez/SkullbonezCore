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

- The normal runtime and replay prediction fixed-step edges call
  `PhysicsEngine::Step()` directly. The normal step now keeps topology repair,
  contact-highlight presentation, and Debug diagnostics name tables at the
  runtime edge without copying solved body rows back into `GameModel`.
  Evidence: `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`.
- `PhysicsScene::RunPhysics()` now steps `PhysicsBodyStore`/`ColliderStore`
  directly and no longer owns model writeback, but `PhysicsModelAccess` remains
  as the model-owner authoring/topology refresh facade.
  Evidence: `SkullbonezSource/Physics/PhysicsScene.cpp`,
  `SkullbonezSource/Physics/PhysicsModelAccess.h`.
- `PhysicsBodyStore` still exposes compatibility load/writeback helpers through
  `PhysicsModelAccess`.
  Evidence: `SkullbonezSource/Physics/PhysicsBodyStore.cpp:192`,
  `SkullbonezSource/Physics/PhysicsBodyStore.cpp:266`.
- `ColliderStore` owns dense live collider rows and only refreshes body
  identity from `PhysicsBodyStore`; every `AddGameModel()` append path now
  passes `PhysicsColliderCreateDesc` directly at creation, and runtime config
  applies material scalars directly to existing collider rows. The remaining
  cold compatibility edge captures descriptors from `GameModel` only for
  same-count editor shape edits or topology drift until explicit collider update
  commands replace model-field recapture. Evidence: `PHY-1062`, `PHY-1063`,
  `PHY-1064`, `PHY-1065`,
  `SkullbonezSource/Physics/PhysicsScene.cpp`,
  `SkullbonezSource/Physics/ColliderStore.cpp`,
  `SkullbonezSource/GameObjects/GameModelCollection.cpp`.
- `PhysicsStandaloneWorld::Contacts()` and `PhysicsStandaloneWorld::Islands()`
  return real public rows for the current standalone smoke coverage, but
  remaining exact shape-pair contacts and full support propagation are still
  future work.
  Evidence: `SkullbonezSource/Physics/PhysicsApi.cpp`.
- `GameModelCollectionPhysicsAdapter` is deleted; remaining model-index identity
  debt lives at owner/tool/replay selection boundaries, not behind a reusable
  adapter bridge.
  Evidence: `PHY-1035` removed the adapter source/header/project entries and
  `tools/check_runtime_boundaries.py` now blocks the adapter type, resolver
  names, and project entries from returning.
- Authored scene setup still receives both `GameModelCollection&` and
  `PhysicsEngine&`, so scene creation still builds through runtime/game-object
  storage rather than a standalone physics creation API. Primitive and hull
  collider facts now cross that boundary as descriptors instead of being
  recaptured from `GameModel`.
  Evidence: `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h:87`.

## Definition Of Done

- [ ] `PhysicsEngine::Step()` can step an owned physics world without
  `PhysicsModelAccess`, `GameModelCollection`, or `GameModel`.
- [ ] `PhysicsBodyStore` owns pose, velocity, mass, inertia, sleep, force, and
  impulse state for the standalone path.
- [x] `ColliderStore` owns shapes, material response, broadphase radius,
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

Strict-step authority slice `PHY-1053`: the remaining normal-step bulk
`PhysicsBodyStore`-to-`GameModel` mirror is deleted. `StepRuntimePhysicsTick()`
now steps the store, applies fixed-contact presentation feedback, and leaves
post-step pose/velocity/sleep state in the body, collider, render, and
diagnostics stores. `GameModelCollection::WriteBackPhysicsBodies()`,
`PhysicsBodyStore::WriteBackToModels()`, and the compatibility record writer
are removed; the checker blocks those deleted bulk mirror surfaces from
returning to runtime/physics source. Evidence: residue scan, diff check,
py_compile, runtime-boundary checks with 0 errors, and focused Debug build with
0 warnings/errors. Final gate passed: `tools\validate_full.bat` passed in
42.9s with Project Filters/runtime boundaries clean, Profile/Debug builds at 0
warnings/errors, DX12 InfoQueue 0 errors, DX12 screenshots matching baselines,
and byte-exact `physics_regression_solver.csv`; intermittent
`tools\validate_physics.bat` passed in about 12.6s with standalone/runtime
handle smoke and byte-exact solver CSV; `tools\validate_perf.bat` completed in
21.4s with absolute DX12/PHYSICS_BENCH budgets passing, no perf regressions, and
DX12 `Frame/Physics` improving from 0.4347ms to 0.2444ms.

Strict-step authority slice `PHY-1054`: attached-camera target identity now
stores `PhysicsBodyHandle` and `PhysicsColliderHandle` as the live physics
identity. The cached model index remains a UI/presentation hint and stale-handle
recovery aid; replay id and name are fallback recovery keys only after the handle
lookup fails. Follow, orbit, selection seeding, pinning, and ragdoll-eye sampling
resolve the target through `PhysicsBodyStore`/`ColliderStore`, then read pose,
velocity, rotation, and broad radius from those stores. The old
`TryResolveAttachedCameraPhysicsTarget(..., modelIndex, ...)` shape is deleted
and blocked by runtime-boundary self-tests. Evidence: CodeGraph mapped the
attached-camera flow; residue scan found no old resolver source calls;
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py`, `git diff --check`, and a focused
Debug build passed; touched-file comment audit inspected `RunInput.cpp`,
`RunState.h`, `Run.h`, and `tools/check_runtime_boundaries.py`;
`tools\validate_fast.bat` passed in 36.1s; intermittent
`tools\validate_physics.bat` passed in about 13.8s with standalone/runtime handle
smoke and byte-exact `physics_regression_solver.csv`.

Strict-step authority slice `PHY-1055`: replay restore physics commands are now
handle-keyed below `GameModelCollection`. The collection edge still verifies the
model-slot replay id for the presentation mirror, but it resolves the current
`PhysicsBodyHandle`/`PhysicsBodyRecord`, restores the `PhysicsBodyStore` row
first, then updates `GameModel` presentation state. `PhysicsEngine`,
`PhysicsScene`, and `PhysicsBodyStore` no longer accept `int modelIndex` for
`RestoreReplayBodyState`; `tools/check_runtime_boundaries.py` blocks model-index
restore signatures in those physics APIs and self-tests the handle-keyed shape.
Evidence: CodeGraph traced the restore chain and blast radius; residue scan
found no `RestoreReplayBodyState(int modelIndex/index)` in
`SkullbonezSource/Physics`; `python -m py_compile
tools\check_runtime_boundaries.py`, `python tools\check_runtime_boundaries.py`,
`git diff --check`, and a focused Debug build passed; touched-file comment audit
inspected `GameModelCollection.cpp`, `PhysicsEngine.h/cpp`,
`PhysicsScene.h/cpp`, `PhysicsBodyStore.h/cpp`, and
`tools/check_runtime_boundaries.py`; `tools\validate_fast.bat` passed in 33.0s;
intermittent `tools\validate_physics.bat` passed in 14.0s with
standalone/runtime handle smoke and byte-exact `physics_regression_solver.csv`.

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
`PhysicsScene::SeedBodyAsleep(PhysicsModelAccess&, PhysicsBodyHandle)` still
owned the remaining one-body compatibility writeback and explicit cache
invalidation at this earlier checkpoint. That overload was later deleted in
`PHY-1004`; sleep seeding now targets `PhysicsBodyStore` and `PhysicsWorld` by
handle only. The checker blocks store/body-record
`PhysicsWorld::SeedModelAsleep` overloads from touching `GameModelBodyStream`,
`GetBodyStream`, or `modelAccess.InvalidatePhysicsStreams()`. Evidence: diff check, py_compile,
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
- [x] Make compatibility append-time model creation register the paired
  `ColliderStore` row immediately after the new `PhysicsBodyStore` row.
- [x] Make same-count collider refresh store-owned: `ColliderStore` now
  preserves dense shape/material rows during body-binding refresh, editor shape
  changes replace one row in place, runtime config updates material scalars in
  place, and topology drift is the only path that
  rebuilds collider fields from `GameModel`.
- [x] Route compatibility authored collider registration/update through
  `PhysicsColliderCreateDesc` so `PhysicsScene`, not `GameModelCollection`,
  owns descriptor-to-`ColliderRecord` conversion and shape-kind derivation.
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
restitution, and friction evidence. Compatibility appends now create one body
row and one paired collider row at `GameModelCollection::AddGameModel()` instead
of waiting for a later collider topology refresh. Same-count collider refresh no
longer scans `GameModel` or a duplicate authoring sidecar: `ColliderStore`
rebases body identity only, runtime config updates material scalars in place,
and append/edit/topology boundaries capture a `PhysicsColliderCreateDesc` and
let `PhysicsScene` derive the live `ColliderRecord`. Remaining shape pairs and
the final move of `GameModel` shape/material authoring into scene/entity collider
descriptors are future
contact/authoring coverage, not blockers for the Phase 3 smoke row. Latest
collider-store authority evidence: runtime-boundary checker passed with 0
errors, `tools\validate_fast.bat` passed, standalone smoke reported
`contacts=2` and `contact_hash=0x5DBDF5257E90EA9B`, runtime handle smoke
reported `collider_refresh=pass`, and `tools\validate_physics.bat` passed with
byte-exact `physics_regression_solver.csv`.
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

- [x] Delete `GameModelCollectionPhysicsAdapter` after all live model-index
  physics command callers move to append-time handles or owner-side
  `PhysicsBodyStore` lookup.
- [x] For each legacy model-index physics command, migrate one caller group:
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

Replay render-pose restore/prediction overlays now stay presentation-only as
queued render-instance value overrides. Replay apply paths validate
`ReplayBodyId`, queue pose values, and leave live `GameModel` pose untouched;
`RenderInstanceStore` rebuilds the one-frame draw matrix from the queued pose
and `ColliderStore`. The boundary checker blocks deleted backup/restore API
names, render-apply `GameModel` pose reads/writes, and RenderInstanceStore
model-pose overrides from returning. Evidence for this slice:
`python -m py_compile tools\check_runtime_boundaries.py`,
`python tools\check_runtime_boundaries.py --repo .`, focused Debug build,
`tools\validate_fast.bat`, intermittent `tools\validate_physics.bat`, and
`tools\validate_dx12_renderer.bat` all passed.

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

`PhysicsBodyStore` command wrappers are now handle-only: `WakeBody`,
`SeedBodyAsleep`, `SetPendingBodyImpulse`, and `ApplyBodyImpulse` no longer
provide `int modelIndex` overloads. The runtime handle smoke seeds reorder state
through the saved `PhysicsBodyHandle`, and `PhysicsWorld` wake propagation writes
the already-selected dense `PhysicsBodyRecord` row directly instead of bouncing
through `HandleForModelIndex`. The boundary checker rejects those deleted
`PhysicsBodyStore` int command overloads from returning. Evidence for this
slice: CodeGraph traced wrappers/callers; residue scan found no deleted int
command overloads in `PhysicsBodyStore.h/.cpp`; `git diff --check`,
`python -m py_compile tools\check_runtime_boundaries.py`, and
`python tools\check_runtime_boundaries.py --repo .` passed; focused Debug build
passed with 0 warnings/errors in 9.1s; touched-file comment audit inspected
`Init.cpp`, `PhysicsBodyStore.h/.cpp`, `PhysicsWorld.cpp`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed in 30.9s; and
`tools\validate_physics.bat` passed in 13.4s with standalone/runtime handle
smoke and byte-exact `physics_regression_solver.csv`.

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
- [x] Remove the `PhysicsScene::SetBodyVelocity` one-body model mirror so
  handle-keyed velocity edits mutate `PhysicsBodyStore` and optional wake state
  without a per-command `GameModel` writeback or model-stream invalidation.
- [x] Add runtime-boundary self-tests that reject the deleted velocity-edit
  `WriteBackPhysicsBody`/`InvalidatePhysicsStreams` shape while allowing the
  remaining explicit `WakeBody` compatibility mirror.
- [x] Delete the `SeedBodyAsleep(PhysicsModelAccess&, PhysicsBodyHandle)`
  overload and route ragdoll/editor/adapter sleep seeding through the
  store-owned handle command.
- [x] Preserve solver-side sleep counters by having the store-owned sleep seed
  command seed both `PhysicsBodyStore` and `PhysicsWorld`, without a
  per-command `GameModel` writeback or model-stream invalidation.
- [x] Add runtime-boundary self-tests that reject the deleted sleep-seed
  model-access overload and caller spelling while allowing `SeedBodyAsleep(body)`.
- [x] Delete the `SetPendingBodyImpulse(PhysicsModelAccess&, PhysicsBodyHandle, ...)`
  overload and route adapter/`ApplyBodyImpulse` enqueue through the store-owned
  handle command.
- [x] Preserve the existing `ApplyBodyImpulse` wake edge in this row; the
  `WakeBody` compatibility mirror remains a separate follow-up.
- [x] Add runtime-boundary self-tests that reject the deleted pending-impulse
  model-access overload and caller spelling while allowing
  `SetPendingBodyImpulse(body, impulse, point)`.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_physics.bat`

Current Phase 10 progress: pending impulses are now enqueued through the
store-owned handle command without a `PhysicsModelAccess` overload. The earlier
pending-only mirror slice deleted one full-body compatibility writeback and one
stream invalidation from the pending-only command path without changing the
explicit `ApplyBodyImpulse` wake/presentation edge. Evidence logs:
`TestOutput\agent_validate_fast_pending_impulse_mirror.log` and
`TestOutput\agent_validate_physics_pending_impulse_mirror.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Follow-up slice `PHY-1002`: replay prediction backup now reads simulation state
from the current `PhysicsEngine::BodyStore()` rather than from `GameModel`
physics getters or the model-refreshing `GetPhysicsBodyStore()` helper. This
removes the immediate prediction dependency that made
`PhysicsScene::SetBodyVelocity` keep a one-body compatibility writeback after
velocity edits. Evidence logs:
`TestOutput\agent_validate_fast_prediction_body_capture.log` and
`TestOutput\agent_validate_physics_prediction_body_capture.log`; the physics
gate passed with byte-exact `physics_regression_solver.csv`.

Follow-up slice `PHY-1003`: replay velocity edits now stop at the authoritative
`PhysicsBodyStore` mutation plus optional wake state. `PhysicsScene::SetBodyVelocity`
no longer performs a one-body `GameModel` writeback or model-stream invalidation;
prediction reads the store directly, and the normal step boundary remains the
presentation projection. Evidence logs:
`TestOutput\agent_validate_fast_velocity_command_mirror.log` and
`TestOutput\agent_validate_physics_velocity_command_mirror.log`; the physics
gate passed with byte-exact `physics_regression_solver.csv`.

Follow-up slice `PHY-1004`: sleep seeding no longer has a model-access overload.
Ragdoll construction, editor placement, and the model-index adapter now call
`PhysicsEngine::SeedBodyAsleep(body)`. The store-owned command seeds
`PhysicsBodyStore` and `PhysicsWorld` sleep counters directly, then leaves
presentation projection to the normal step boundary. Evidence logs:
`TestOutput\agent_validate_fast_sleep_seed_mirror.log` and
`TestOutput\agent_validate_physics_sleep_seed_mirror.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Follow-up slice `PHY-1005`: pending impulse enqueue no longer has a model-access
overload. The model-index adapter and `ApplyBodyImpulse` now call
`SetPendingBodyImpulse(body, impulse, point)` directly, while `ApplyBodyImpulse`
keeps the existing `WakeBody(modelAccess, body)` compatibility edge for a later
row. Evidence logs: `TestOutput\agent_validate_fast_pending_impulse_overload.log`
and `TestOutput\agent_validate_physics_pending_impulse_overload.log`; the
physics gate passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1006`: remove the one-body `GameModel` writeback and
model-stream invalidation from `PhysicsScene::WakeBody` after store/world wake
propagation. Keep the `WakeBody(PhysicsModelAccess&, PhysicsBodyHandle)`
signature and topology/collider refresh behavior in this row; deleting that
remaining signature belongs with the broader step-boundary migration.

- [x] Delete the wake command's per-command `WriteBackPhysicsBody` and
  `InvalidatePhysicsStreams` calls.
- [x] Add runtime-boundary self-tests that reject the deleted wake mirror shape
  while allowing store/world wake propagation and the existing
  `WakeBody(modelAccess, body)` caller edge.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_physics.bat`

Follow-up slice `PHY-1006`: wake propagation now stops at
`PhysicsBodyStore`/`PhysicsWorld` sleep and island state. `PhysicsScene::WakeBody`
no longer performs a one-body `GameModel` writeback or model-stream
invalidation; it still keeps the current model-access signature so topology and
collider refresh behavior remain bounded until the broader step-boundary row.
Evidence logs: `TestOutput\agent_validate_fast_wake_command_mirror.log` and
`TestOutput\agent_validate_physics_wake_command_mirror.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1007`: delete the remaining
`WakeBody(PhysicsModelAccess&, PhysicsBodyHandle)` and
`ApplyBodyImpulse(PhysicsModelAccess&, PhysicsBodyHandle, ...)` command
signatures. Legacy model-index callers may still use
`GameModelCollectionPhysicsAdapter`, but only for count-gated body/collider
topology refresh before entering the handle-owned physics commands.

- [x] Add a count-gated adapter refresh path for body/collider topology before
  legacy model-index wake/apply commands enter physics.
- [x] Delete the model-access wake/apply overloads from `PhysicsEngine` and
  `PhysicsScene`.
- [x] Route runtime/editor/replay callers through body-handle wake/apply
  commands or the existing model-index adapter.
- [x] Add runtime-boundary self-tests that reject the deleted wake/apply
  model-access overloads and caller spellings while allowing body-only commands.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_physics.bat`

Follow-up slice `PHY-1007`: wake and apply-impulse commands no longer borrow
`PhysicsModelAccess`. `PhysicsEngine`/`PhysicsScene` now expose
`WakeBody(body)` and `ApplyBodyImpulse(body, impulse, point)`, while legacy
model-index callers use `GameModelCollectionPhysicsAdapter` for count-gated
body/collider topology refresh before entering those commands. Evidence logs:
`TestOutput\agent_validate_fast_wake_apply_signatures.log` and
`TestOutput\agent_validate_physics_wake_apply_signatures.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1008`: delete the remaining
`SetBodyVelocity(PhysicsModelAccess&, PhysicsBodyHandle, ...)` command
signature. Replay velocity edits may still resolve from model order, but they
must request a wake-ready `PhysicsBodyHandle` from
`GameModelCollectionPhysicsAdapter` before entering the handle-owned physics
command.

- [x] Add an adapter handle resolver for velocity commands so wake-sensitive
  collider topology refresh stays count-gated at the model-index edge.
- [x] Delete the model-access velocity overload from `PhysicsEngine` and
  `PhysicsScene`.
- [x] Route replay velocity edits through `SetBodyVelocity(body, linearVelocity,
  angularVelocity, wakeIfMoving)` without constructing `PhysicsModelAccess`.
- [x] Add runtime-boundary self-tests that reject the deleted velocity
  model-access overload and caller spelling while allowing body-only commands.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_physics.bat`

Follow-up slice `PHY-1008`: velocity edit commands no longer borrow
`PhysicsModelAccess`. `PhysicsEngine`/`PhysicsScene` now expose
`SetBodyVelocity(body, linearVelocity, angularVelocity, wakeIfMoving)`, while
replay velocity edits use `GameModelCollectionPhysicsAdapter` for count-gated
body/collider topology refresh before entering the command. Evidence logs:
`TestOutput\agent_validate_fast_velocity_signatures.log` and
`TestOutput\agent_validate_physics_velocity_signatures.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1009`: move Debug step-diagnostics presentation-name collection out
of `PhysicsWorld` and into the `PhysicsScene` compatibility edge. `PhysicsWorld`
now emits regression/SkullScope step diagnostics from store views plus an
optional cold names pointer/count, without borrowing `PhysicsModelAccess` or
owning name scratch.

- [x] Move the Debug diagnostics name scratch vector from `PhysicsWorld` to
  `PhysicsScene`.
- [x] Add `PhysicsWorld::ShouldEmitStepDiagnostics()` so `PhysicsScene` only
  fills names when Debug diagnostics are actually enabled.
- [x] Change `PhysicsWorld::EmitStepDiagnostics` to accept store views plus
  diagnostic names, not `PhysicsModelAccess`.
- [x] Delete the unused
  `EmitPhysicsDiagnosticsFrame(PhysicsModelAccess&, ...)` helper.
- [x] Add runtime-boundary self-tests that reject the old step-diagnostics
  model-access signature, helper, and `FillPhysicsDiagnosticsNames` call inside
  `PhysicsWorld`.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Follow-up slice `PHY-1009`: step diagnostics no longer borrow
`PhysicsModelAccess` from inside `PhysicsWorld`. `PhysicsScene` owns the
Debug-only presentation-name scratch and only fills it when
`PhysicsWorld::ShouldEmitStepDiagnostics()` says regression or SkullScope frame
logs are enabled; `PhysicsWorld::EmitStepDiagnostics` receives body/collider
stores plus a names pointer/count. Evidence logs:
`TestOutput\agent_validate_fast_step_diagnostics_names.log` and
`TestOutput\agent_validate_physics_step_diagnostics_names.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1010`: move collision-time diagnostics name lookup out of
`PhysicsWorld` and `PhysicsDiagnosticsSink` model-access paths. The scene edge
now fills the existing Debug-only names table before the world step whenever
collision-time or step diagnostics need presentation names.

- [x] Fill `PhysicsScene` diagnostics names before `PhysicsWorld::RunPhysics`
  when collision-time logging or step diagnostics are enabled.
- [x] Thread the cold names pointer/count through `PhysicsWorld::RunPhysics`,
  `RunSolverPhysics`, and `EmitPhysicsCollisionTime`.
- [x] Change `PhysicsDiagnosticsSink::EmitCollisionTime` to consume names
  pointer/count instead of `PhysicsModelAccess`.
- [x] Remove the diagnostics sink's now-unused `PhysicsModelAccess.h` include.
- [x] Add runtime-boundary self-tests that reject the old collision-time
  model-access signatures and `TryGetPhysicsDiagnosticsModelName` call in the
  world/sink paths.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Follow-up slice `PHY-1010`: collision-time diagnostics no longer borrow
`PhysicsModelAccess` inside `PhysicsWorld` or `PhysicsDiagnosticsSink`.
`PhysicsScene` fills the same cold Debug presentation-name table before the
world step when collision-time logging or step diagnostics need it, and the
world/sink paths consume a names pointer/count. Evidence logs:
`TestOutput\agent_validate_fast_collision_time_names.log` and
`TestOutput\agent_validate_physics_collision_time_names.log`; the physics gate
passed with byte-exact `physics_regression_solver.csv`.

Slice `PHY-1011`: fixed-tree release now has a store-owned step path. The
compatibility refresh copies releasable-tree grouping into `PhysicsBodyRecord`
as `fixedTreeReleaseRootIndex`, and `PhysicsBodyStore::ReleaseAttachedFixedTreeParts`
uses that metadata plus live store positions to release attached fixed parts.
`PhysicsWorld::RunPhysics`, `RunSolverPhysics`, and `ApplyTornadoField` no
longer take `PhysicsModelAccess`; tornado release remains immediate before the
same-frame tornado force pass, while persistent-contact release is still applied
at the `PhysicsScene` store edge. A bounded `GameModelCollection` model command
remains for runtime ray tools that edit `GameModel` before the next store
refresh. The checker blocks `PhysicsModelAccess&` from returning to those
`PhysicsWorld` step signatures.

- [x] Add fixed-tree release-group metadata to `PhysicsBodyRecord` during
  `PhysicsBodyStore::LoadFromModels`.
- [x] Move `PhysicsFixedTreeReleaseEvent` out of `PhysicsModelAccess.h` and into
  the body-store domain.
- [x] Move attached fixed-tree release application to
  `PhysicsBodyStore::ReleaseAttachedFixedTreeParts`.
- [x] Remove `PhysicsModelAccess` from `PhysicsWorld::RunPhysics`,
  `RunSolverPhysics`, and `ApplyTornadoField`.
- [x] Keep the runtime ray-tool model-owned release edge explicitly bounded.
- [x] Add runtime-boundary self-tests that reject the old `PhysicsWorld`
  model-access step signatures.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_store_fixed_tree.log`,
`TestOutput\agent_validate_format_fixed_tree_store_release.log`,
`TestOutput\agent_validate_fast_fixed_tree_store_release.log`, and
`TestOutput\agent_validate_physics_fixed_tree_store_release.log`. The physics
gate passed with `physics_regression_solver.csv` at 20,001 lines and a byte-exact
match.

Slice `PHY-1012`: retire the step-time `PhysicsModelAccess` signatures from
`PhysicsEngine::Step` and `PhysicsScene::RunPhysics`. `GameModelCollection` now
owns the remaining model-side step work: count-gated body/collider topology
repair, contact-highlight presentation timers, Debug diagnostics names, fixed
contact presentation feedback, bulk post-step writeback, and model stream
invalidation. `PhysicsEngine` and `PhysicsScene` step owned stores only.
`SimulationSystem` is now a tick-count scheduler; `RunFrame`, replay prediction,
and replay restore probes execute returned ticks through
`GameModelCollection::RunPhysics` instead of constructing `SimulationPhysicsStep`
or `PhysicsModelAccess`.

- [x] Remove `PhysicsModelAccess&` from `PhysicsEngine::Step`.
- [x] Remove `PhysicsModelAccess&` from `PhysicsScene::RunPhysics`.
- [x] Move model-owner pre/post step work to `GameModelCollection::RunPhysics`.
- [x] Make `SimulationSystem` return committed tick counts without borrowing
  `PhysicsEngine`, `PhysicsModelAccess`, worker pools, or world forces.
- [x] Route runtime frame stepping, replay prediction stepping, and replay
  restore probe stepping through the collection-owned step.
- [x] Add runtime-boundary self-tests that reject the deleted engine/scene step
  signatures and any future `SimulationSystem` owner-borrow step context.
- [x] Run an intermittent `tools\validate_physics.bat` checkpoint immediately
  after the scheduler/step-owner change.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent and final `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_step_store_boundary.log`,
`TestOutput\agent_validate_physics_step_store_boundary_intermediate.log`,
`TestOutput\agent_validate_fast_step_store_boundary.log`, and
`TestOutput\agent_validate_physics_step_store_boundary.log`.

Slice `PHY-1013`: trim the now-dead step-era surface from
`PhysicsModelAccess`. After `PHY-1012`, the facade only exists for
model-owned refresh/import work while `GameModelCollection` owns the remaining
model-side step envelope. Body-stream access, post-step writeback, stream
invalidation, presentation feedback, diagnostics-name lookup, `Count`, and
`size` are no longer exposed through the physics facade.

- [x] Delete unused `PhysicsModelAccess` declarations and definitions for body
  stream, writeback, invalidation, presentation feedback, diagnostics names,
  `Count`, and `size`.
- [x] Keep `PhysicsModelAccess` restricted to `ModelCount`,
  `ReloadPhysicsBodies`, `RefreshPhysicsBodyFromModel`,
  `RefreshPhysicsColliders`, and `RefreshRenderInstances`.
- [x] Document the remaining facade owner, reason, deletion condition, and
  checker budget.
- [x] Add runtime-boundary self-tests that reject the deleted facade surface but
  allow the refresh-only facade and comment-only historical mentions.
- [x] Comment-audit the touched source files.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_model_access_facade_trim.log`,
`TestOutput\agent_validate_fast_model_access_facade_trim.log`, and
`TestOutput\agent_validate_physics_model_access_facade_trim.log`.

Slice `PHY-1014`: move renderer-facing object pose consumers onto the
physics-backed `RenderInstanceStore` snapshot. Object drawing, shadow caster
batching, object shadow bounds, and DXR matrix upload no longer read the
GameModel render/body streams or recompute model matrices from `GameModel`
pose. Replay scrub/prediction render poses stay presentation-only by applying a
one-frame override to the prepared render snapshot after the physics-backed
refresh.

- [x] Make `GameModelRenderer` consume `RenderInstances()` for transforms,
  material highlights, fixed flags, shape kind, and shadow bounds.
- [x] Make `CopyDxrModelMatrices` copy prepared render-instance matrices, with a
  cold-call refresh instead of falling back to `GameModel::GetModelMatrix`.
- [x] Carry conservative bounds radius and render shape kind in
  `RenderInstanceRecord`.
- [x] Preserve convex-hull draw transform behavior while moving matrix authority
  to the render snapshot.
- [x] Queue replay render-pose overrides and apply them only to
  `RenderInstanceStore`.
- [x] Add runtime-boundary guardrails for renderer stream/model-pose/shape reads
  and DXR GameModel matrix recomputation.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`
  - [x] `tools\validate_dx12_renderer.bat`
  - [ ] `tools\validate_perf.bat`

Perf note: `tools\validate_perf.bat` was updated to use
`--no-contact-audio` for deterministic headless perf runs, which removes the
audio sample memory drift from this gate. The current tree still fails only
`PHYSICS_BENCH` `Frame/Render.avg` against the old baseline, while p50, memory,
DX12 perf, and absolute budgets pass. A clean detached HEAD no-audio isolation
run also failed the same old baseline and was slower (`Frame/Render.avg`
0.6540 ms at HEAD versus 0.5768-0.5813 ms after this slice), so the remaining
red result is pre-existing perf-baseline/restart-frame drift rather than a
regression from `PHY-1014`.

Evidence logs: `TestOutput\agent_validate_fast_render_instance_store_shape_kind_fixed.log`,
`TestOutput\agent_validate_physics_render_instance_store_shape_kind.log`,
`TestOutput\agent_validate_dx12_render_instance_store_shape_kind.log`,
`TestOutput\agent_validate_perf_render_instance_store_shape_kind.log`, and
`TestOutput\agent_validate_perf_render_instance_store_shape_kind_rerun.log`.

Slice `PHY-1015`: stop read-only runtime presentation helpers from depending on
post-step `GameModel` body mirrors. `GetPhysicsBodyStore()` now repairs topology
only when body count drift is detected; same-count reads preserve
`PhysicsBodyStore` authority. Object-follow camera focus and UI scene-energy
sampling read body records directly, so those presentation paths no longer need
the now-deleted bulk compatibility writeback to keep `GameModel` physics fields
fresh.

- [x] Make `GetPhysicsBodyStore()` count-gated instead of unconditionally
  reloading body rows from `GameModel`.
- [x] Make `GetModelPosition()` return the store-owned body position.
- [x] Make `GetSceneKineticEnergy()` compute from `PhysicsBodyStore` records.
- [x] Add runtime-boundary guardrails and self-tests blocking unconditional
  body-store reloads and model-field reads from returning to those helpers.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_body_store_read_authority.log`,
`TestOutput\agent_validate_fast_body_store_read_authority.log`, and
`TestOutput\agent_validate_physics_body_store_read_authority.log`.

Slice `PHY-1016`: move centralized runtime picking off `GameModel` body-state
reads. `RuntimePickService` should borrow `PhysicsBodyStore` and `ColliderStore`
for exact-shape hit tests, while replay and editor callers may still use
`GameModel` for cold presentation data such as names and ragdoll-root mapping.
`GetColliderStore()` must refresh collider shape/material snapshots without
calling the full collider refresh path that reloads same-count body rows.

- [x] Replace `RuntimePickRequest::models` with borrowed physics body/collider
  stores.
- [x] Make `RuntimePickService::TryPickModel()` scan store records for fixed
  state, transforms, and exact collision shapes.
- [x] Update editor selection, interaction automation, attached camera,
  manipulator pickup, and replay path picking to provide store snapshots.
- [x] Make `GetColliderStore()` preserve body-store authority while refreshing
  collider snapshots.
- [x] Add runtime-boundary guardrails and self-tests blocking GameModel-backed
  picking and full collider-store refresh from returning.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_full.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_runtime_pick_store_authority.log`,
`TestOutput\agent_validate_fast_runtime_pick_store_authority.log`,
`TestOutput\agent_validate_physics_runtime_pick_store_authority.log`, and
`TestOutput\agent_validate_full_runtime_pick_store_authority.log`.

Slice `PHY-1017`: move scene snapshot serialization of live physics state off
post-step `GameModel` body mirrors. Owner: `SceneSnapshotWriter`; reason:
saved scene state must serialize the authoritative simulation snapshot, not
whatever compatibility writeback last copied into `GameModel`; deletion
condition: when all remaining presentation/editor/save readers use stores or
metadata, bulk post-step body writeback can be removed; checker budget:
`tools/check_runtime_boundaries.py` blocks GameModel-backed snapshot physics
reads from returning.

- [x] Make `SceneSnapshotWriter` borrow `PhysicsBodyStore` and `ColliderStore`
  records for pose, velocity, angular velocity, inertia, fixed/sleep state,
  mass, restitution, and shape serialization.
- [x] Keep `GameModel` reads only for names, contact material labels, render
  materials, runtime collection metadata, and other cold scene metadata.
- [x] Add runtime-boundary guardrails and self-tests blocking snapshot physics
  fields from being read from `GameModel` again.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] `tools\validate_full.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_scene_snapshot_store_authority.log`,
`TestOutput\agent_validate_fast_scene_snapshot_store_authority.log`,
`TestOutput\agent_validate_physics_scene_snapshot_store_authority.log`, and
`TestOutput\agent_validate_full_scene_snapshot_store_authority.log`.

Slice `PHY-1018`: delete the dead model-side force integration bridge. Owner:
`PhysicsBodyStore`; reason: active world-force and pending-impulse integration is
store-owned, so keeping `GameModel`/`RigidBody` force accumulators and
`WorldEnvironment::AddWorldForces(GameModel&)` only preserves an unused path full
of scattered model physics reads/copies; deletion condition: `GameModel` exposes
no model-side apply/world/impulse force integration surface and bulk
compatibility writeback no longer mirrors pending impulses into `GameModel`;
checker budget: `tools/check_runtime_boundaries.py` blocks the deleted
model-side force bridge from returning.

- [x] Delete `GameModel::ApplyForces`, `ApplyWorldForces`, `SetWorldForce`,
  `SetImpulseForce`, and `ClearImpulseForce` declarations/definitions.
- [x] Delete `WorldEnvironment::AddWorldForces(GameModel&)` plus private helper
  declarations/definitions used only by that path, leaving scalar
  `PhysicsWorldForces` as the water/drag force export.
- [x] Remove dead `RigidBody` world/impulse-force accumulators and integration
  methods while preserving pose, velocity, inertia, mass, friction, and
  restitution state still used by compatibility storage.
- [x] Stop `PhysicsBodyStore::WriteRecordToCompatibilityModel` from copying
  pending impulse state into `GameModel`; pending impulses remain in
  `PhysicsBodyStore`.
- [x] Add runtime-boundary guardrails and self-tests blocking the deleted
  model-side force bridge from returning.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_model_force_bridge_deletion.log`,
`TestOutput\agent_validate_fast_model_force_bridge_deletion.log`, and
`TestOutput\agent_validate_physics_model_force_bridge_deletion.log`.

Slice `PHY-1019`: delete the dead `GameModel` SoA stream cache and stale
render-stream prep boundary. Owner: `RenderInstanceStore`/`PhysicsBodyStore`
for live draw/physics snapshots; reason: render and physics consumers no longer
use `GameModelBodyStream` or `GameModelRenderStream`, so keeping
`GameModelSoACache` preserves an unused model-derived copy path and stale
hot-path vocabulary; deletion condition: source and project files expose no
`GameModelSoACache`, `GameModelStreamProvider`, `GameModelBodyStream`,
`GameModelRenderStream`, `GetBodyStream`, `GetRenderStream`,
`GetPhysicsBodyStream`, `PrepareRenderStreams`, `InvalidatePhysicsStreams`, or
`soaCacheBytes`; checker budget: `tools/check_runtime_boundaries.py` blocks the
deleted source names and project-file entries from returning.

- [x] Delete `SkullbonezSource/GameObjects/GameModelSoACache.cpp/.h` and
  `SkullbonezSource/GameObjects/GameModelStreams.cpp/.h`.
- [x] Remove the `GameModelCollection` SoA member and dead stream/invalidation
  API surface.
- [x] Rename render prep from `PrepareRenderStreams()` to
  `PrepareRenderInstances()` so render frames name the store-backed snapshot
  they actually refresh.
- [x] Remove `soaCacheBytes` from memory stats, JSON dump output, and profiler
  UI text.
- [x] Remove the deleted source/header files from `SKULLBONEZ_CORE.vcxproj` and
  `.filters`.
- [x] Update current reference docs that still described the stream provider as
  live.
- [x] Add runtime-boundary guardrails and self-tests blocking the deleted stream
  cache/API/project entries from returning.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`
  - [x] `tools\validate_dx12_renderer.bat`
  - [x] `tools\validate_perf.bat`

Evidence logs: `TestOutput\agent_build_debug_delete_model_stream_cache.log`,
`TestOutput\agent_validate_fast_delete_model_stream_cache.log`,
`TestOutput\agent_validate_physics_delete_model_stream_cache.log`,
`TestOutput\agent_validate_dx12_delete_model_stream_cache.log`, and
`TestOutput\agent_validate_perf_delete_model_stream_cache.log`.

Slice `PHY-1020`: delete the `RenderInstanceStore` model-only refresh overloads
and stale topology fallback. Owner: `RenderInstanceStore` for render projection
records derived from `PhysicsBodyStore` and `ColliderStore`; reason: the old
fallback rebuilt render matrices, fixed flags, replay ids, bounds, and shape
kind from `GameModel` when topology was wrong, which hid store-refresh bugs and
kept a post-solve mirror path alive; deletion condition: source exposes no
model-only `RenderInstanceStore::Refresh` overloads or
`Refresh(models, modelCount)` fallback, while replay presentation overrides
remain explicitly scoped; checker budget: `tools/check_runtime_boundaries.py`
blocks deleted overload declarations/definitions and the fallback call from
returning.

- [x] Delete `RenderInstanceStore::Refresh(std::vector<GameModel>&)` and
  `RenderInstanceStore::Refresh(GameModel*, int)`.
- [x] Make store-backed refresh assert on body/collider/model count mismatch in
  Debug and clear the render snapshot in release rather than rebuilding from
  `GameModel`.
- [x] Delete `OverridePoseFromModel()` and replay render-pose model
  backup/restore; replay/prediction presentation overrides queue pose values
  that are consumed by `RenderInstanceStore`.
- [x] Add runtime-boundary guardrails and self-tests blocking the deleted
  overloads, declarations, and fallback call from returning.
- [x] Reset profiler pass state when a scene perf log opens so appended perf
  passes use the existing profiler warmup consistently and do not average
  scene-restart upload noise into steady-state CSV stats.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_perf.bat`
  - [x] `tools\validate_full.bat` including DX12 renderer validation and
    intermittent `tools\validate_physics.bat`

Evidence logs: `TestOutput\agent_build_debug_render_instance_fallback_deletion.log`,
`TestOutput\agent_validate_fast_render_instance_fallback_deletion.log`,
`TestOutput\agent_validate_physics_render_instance_fallback_deletion.log`,
`TestOutput\agent_validate_dx12_render_instance_fallback_deletion.log`,
`TestOutput\agent_validate_perf_render_instance_fallback_deletion.log`,
`TestOutput\agent_validate_perf_render_instance_fallback_deletion_rerun.log`,
`TestOutput\agent_validate_perf_clean_head_probe.log`,
`TestOutput\agent_validate_perf_profiler_schedule_reset.log`, and
`TestOutput\agent_validate_full_render_instance_fallback_deletion.log`.

Slice `PHY-1021`: keep `GameModelCollection::RunPhysics` from constructing
`PhysicsModelAccess` during steady-state frames. Owner: `GameModelCollection`
for the temporary compatibility topology-repair edge; reason: the store-owned
step should not borrow the model owner unless body/collider counts actually
drift; deletion condition: the top-level `RunPhysics` frame step has no
steady-state `PhysicsModelAccess modelAccess(*this)` declaration, while
topology-repair branches remain allowed until model-order compatibility is
deleted; checker budget: `tools/check_runtime_boundaries.py` blocks top-level
step facade construction from returning.

- [x] Create `PhysicsModelAccess` only inside the body/collider topology-repair
  branch in `GameModelCollection::RunPhysics`.
- [x] Add runtime-boundary guardrails and self-tests blocking a top-level
  steady-frame `PhysicsModelAccess` declaration in `RunPhysics`.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: runtime-boundary summary reported 0 errors;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors;
`tools\validate_physics.bat` passed standalone smoke, runtime handle smoke, and
byte-exact `physics_regression_solver.csv` comparison.

Slice `PHY-1022`: remove the remaining mouse-pickup `GameModel` body-state
read/write path after runtime picking already moved onto store-backed records.
Owner: runtime mouse pickup plus `RuntimePickService`; reason: manipulator drag
is a per-frame physics command path and should not depend on post-step
compatibility mirrors or model-index impulse bridges; deletion condition:
pickup capture, step, and restore use `PhysicsBodyHandle` plus
`PhysicsBodyStore` records for simulation state, with `modelIndex` retained only
for interaction identity/stale-slot validation; checker budget:
`tools/check_runtime_boundaries.py` blocks mouse-pickup `GameModel` body reads
and model-index physics commands from returning.

- [x] Return the picked `PhysicsBodyHandle` from `RuntimePickService`.
- [x] Store the picked handle in `RunMousePickupState`.
- [x] Read pickup position, fixed state, linear velocity, and preserved angular
  velocity from `PhysicsBodyStore` records.
- [x] Restore angular velocity and apply drag impulses through
  `PhysicsEngine` handle commands.
- [x] Delete the now-unused mouse-pickup model-index impulse helper.
- [x] Add runtime-boundary guardrails and self-tests for the deleted body-read
  and model-index command shapes.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: runtime-boundary summary reported 0 errors; touched-file comment
audit confirmed all touched source-bearing files have learning headers and the
new handle/model-index contract is explained; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; intermittent `tools\validate_physics.bat` passed standalone
smoke, runtime handle smoke, and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1023`: remove launcher use of
`GameModelCollectionPhysicsAdapter` model-index command wrappers while keeping
the wake-aware, count-gated handle resolver. Owner: `RuntimeTools` launcher
helpers; reason: launcher laser/projectile commands are runtime physics
commands and should call `PhysicsEngine` with handles directly instead of
hiding mutation behind adapter command wrappers; deletion condition:
`RuntimeTools.cpp` resolves a wake-ready `PhysicsBodyHandle` with
`BodyHandleForVelocityCommand(modelIndex, true)` and calls
`PhysicsEngine::ApplyBodyImpulse` / `WakeBody`; checker budget:
`tools/check_runtime_boundaries.py` blocks launcher adapter command wrappers
from returning.

- [x] Resolve launcher laser impulse targets to `PhysicsBodyHandle` before
  calling `PhysicsEngine::ApplyBodyImpulse`.
- [x] Resolve projectile spawn wake targets to `PhysicsBodyHandle` before
  calling `PhysicsEngine::WakeBody`.
- [x] Preserve wake-aware count-gated body/collider topology refresh through
  the existing adapter handle resolver.
- [x] Add runtime-boundary guardrails and self-tests for the deleted launcher
  adapter command-wrapper shape.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: runtime-boundary summary reported 0 errors; touched-file comment
audit confirmed `RuntimeTools.cpp` and `tools/check_runtime_boundaries.py`
meet the guide for this slice; `tools\validate_physics.bat` passed standalone
smoke, runtime handle smoke, and byte-exact `physics_regression_solver.csv`;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors.

Slice `PHY-1024`: remove editor wake use of
`GameModelCollectionPhysicsAdapter` model-index command wrappers while keeping
the wake-aware, count-gated handle resolver. Owner: runtime editor transform
helpers; reason: editor transform reset is a physics command path and should
call `PhysicsEngine` with a validated `PhysicsBodyHandle` instead of hiding
mutation behind an adapter command wrapper; deletion condition:
`WakeEditorPhysicsBody` resolves a wake-ready `PhysicsBodyHandle` with
`BodyHandleForVelocityCommand(modelIndex, true)` and calls
`PhysicsEngine::WakeBody`; checker budget: `tools/check_runtime_boundaries.py`
blocks editor adapter command wrappers from returning.

- [x] Resolve editor wake targets to `PhysicsBodyHandle` before calling
  `PhysicsEngine::WakeBody`.
- [x] Preserve wake-aware count-gated body/collider topology refresh through
  the existing adapter handle resolver.
- [x] Add runtime-boundary guardrails and self-tests for the deleted editor
  adapter command-wrapper shape.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: runtime-boundary summary reported 0 errors; touched-file comment
audit confirmed `RunEditorTools.cpp` and `tools/check_runtime_boundaries.py`
meet the guide for this slice; `tools\validate_physics.bat` passed standalone
smoke, runtime handle smoke, and byte-exact `physics_regression_solver.csv`;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors.

Slice `PHY-1025`: remove replay editor-transform restore wake use of
`GameModelCollectionPhysicsAdapter` model-index command wrappers while keeping
the wake-aware, count-gated handle resolver. Owner: `RunFrame` replay restore;
reason: replay events store model identity for deterministic event validation,
but physics wake mutation should call `PhysicsEngine` with a validated
`PhysicsBodyHandle`; deletion condition: the `ReplayEventKind::EditorTransform`
restore path resolves a wake-ready `PhysicsBodyHandle` with
`BodyHandleForVelocityCommand(event.value0, true)` and calls
`PhysicsEngine::WakeBody`; checker budget: `tools/check_runtime_boundaries.py`
blocks RunFrame replay adapter wake wrappers from returning.

- [x] Resolve replay editor-transform wake targets to `PhysicsBodyHandle`
  before calling `PhysicsEngine::WakeBody`.
- [x] Preserve wake-aware count-gated body/collider topology refresh through
  the existing adapter handle resolver.
- [x] Add runtime-boundary guardrails and self-tests for the deleted RunFrame
  replay adapter wake-wrapper shape.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_physics.bat`
  - [x] `tools\validate_full.bat`

Evidence: runtime-boundary summary reported 0 errors; touched-file comment
audit confirmed `RunFrame.cpp` and `tools/check_runtime_boundaries.py` meet the
guide for this slice; narrow `RunFrame.cpp` clang-format fixed the only
formatting issue found by the first `tools\validate_full.bat` attempt;
`tools\validate_physics.bat` passed standalone smoke, runtime handle smoke, and
byte-exact `physics_regression_solver.csv`; rerun `tools\validate_full.bat`
passed DX12 InfoQueue with 0 errors, DX12 screenshots matching committed
baselines, and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1026`: delete the
`GameModelCollectionPhysicsAdapter` model-index command-wrapper API after all
runtime/editor/replay callers moved to explicit handle-keyed
`PhysicsEngine` commands. Owner: `GameModelCollectionPhysicsAdapter` as a
temporary legacy identity resolver; reason: command wrappers hide mutation
behind the migration adapter and keep an old model-index command surface alive;
deletion condition: no `WakeBodyForModelIndex`,
`SeedBodyAsleepForModelIndex`, `ApplyBodyImpulseForModelIndex`, or
`SetPendingBodyImpulseForModelIndex` names remain under `SkullbonezSource`;
checker budget: `tools/check_runtime_boundaries.py` blocks those adapter
command-wrapper names from returning anywhere under source-bearing files.

- [x] Move `ReleaseAttachedFixedTreeParts` off the final live
  `WakeBodyForModelIndex` caller.
- [x] Delete adapter command-wrapper declarations and definitions.
- [x] Keep `GameModelCollectionPhysicsAdapter` only as a legacy
  model-index/scene-object-id to `PhysicsBodyHandle` resolver.
- [x] Add runtime-boundary guardrails and self-tests for deleted adapter command
  declarations, definitions, and calls.
- [x] Confirm no adapter command-wrapper names remain under `SkullbonezSource`.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph plus targeted residue scans confirmed the last live
adapter command-wrapper caller before the edit and zero adapter command-wrapper
names remaining under `SkullbonezSource` after the edit; runtime-boundary
summary reported 0 errors; touched-file comment audit confirmed the adapter
comments now describe handle resolution rather than a command bridge;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors;
`tools\validate_physics.bat` passed standalone smoke, runtime handle smoke, and
byte-exact `physics_regression_solver.csv`.

Slice `PHY-1027`: remove authored/generated scene construction use of
`GameModelCollectionPhysicsAdapter` handle lookup after model append. Owner:
`GameModelCollection` append boundary plus `PhysicsBodyStore`; reason:
post-insert adapter lookup can reload the whole body store for every created
object and keeps model-index construction commands alive; deletion condition:
`SceneAuthoredSetup.cpp` and `SceneGeneratedSetup.cpp` no longer instantiate
`GameModelCollectionPhysicsAdapter` or call `BodyHandleForModelIndex`; checker
budget: `tools/check_runtime_boundaries.py` blocks scene setup adapter lookups
from returning.

- [x] Make `GameModelCollection::AddGameModel` append the matching
  `PhysicsBodyStore` body record once and return the new `PhysicsBodyHandle`.
- [x] Keep `PhysicsEngine` free of raw `GameModel` public facade dependencies by
  passing a `PhysicsBodyRecord` value into `RegisterAuthoredBody`.
- [x] Move authored scene initial impulses and sleep seeding to the returned
  append-time body handle.
- [x] Move generated scene and solver-demo initial impulses to the returned
  append-time body handle.
- [x] Add runtime-boundary guardrails and self-tests blocking scene setup
  adapter handle lookup regression.
- [x] Confirm no scene setup adapter lookup names remain in authored/generated
  setup.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph identified the authored/generated post-insert adapter
lookup pattern; targeted residue scan found no `GameModelCollectionPhysicsAdapter`
or `BodyHandleForModelIndex` in `SceneAuthoredSetup.cpp` or
`SceneGeneratedSetup.cpp` after the edit; runtime-boundary summary reported 0
errors; focused Debug build passed with 0 warnings/errors; touched-file comment
audit passed for every source/tool file touched; `tools\validate_fast.bat`
passed formatting, project filters, runtime boundaries, and Profile/Debug builds
with 0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime
handle smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1028`: move attached-camera follow target state off post-step
`GameModel` body mirrors. Owner: runtime attached-camera input/follow solve;
reason: camera follow is a normal per-frame runtime reader of pose, orientation,
velocity, and radius, so reading those facts through `GameModel` kept the bulk
compatibility mirror on the hot path; deletion condition: attached-camera
capture/orbit/tick code no longer calls `GameModel` body-state accessors or the
deleted model-vector helper names; checker budget:
`tools/check_runtime_boundaries.py` blocks those attached-camera regressions.

- [x] Resolve attached-camera target state from `PhysicsBodyStore` and
  `ColliderStore` with `GameModel` retained only for cold identity, name, replay
  id, and ragdoll grouping.
- [x] Move fixed-offset capture, orbit capture, orbit wheel clamp, regular
  follow, velocity-forward follow, and ragdoll-eyes follow to store-sampled
  position/orientation/velocity/radius.
- [x] Add runtime-boundary guardrails and self-tests blocking deleted
  attached-camera `GameModel` helper/body-read regressions.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: targeted residue scan found no deleted attached-camera model helper
names or direct `target`/`head`/`model` `GetPosition()`/`GetVelocity()` body
reads in `RunInput.cpp`; runtime-boundary summary reported 0 errors; touched
source/tool comment audit passed for `RunInput.cpp`, `Run.h`, and
`tools/check_runtime_boundaries.py`; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1029`: delete object-contact `GameModel` manifold overloads and move
required scene-contact exact checks to store snapshots. Owner: object/object
narrowphase API plus runtime required contact gates; reason: production solver
manifolds already use `ObjectContactBodyView` and `ColliderRecord::shape`, so
the leftover `GameModel` overload kept a stale shape/pose path alive and made
scene automation depend on the post-step model mirror; deletion condition:
`ObjectContactManifold.h/.cpp` expose only pose-view/shape-value manifold
entry points and `Run::UpdateRequiredSceneContacts()` reads
`PhysicsBodyStore`/`ColliderStore`; checker budget:
`tools/check_runtime_boundaries.py` blocks the old overload/helper and scene
gate model reads.

- [x] Remove `GameModel` forward declaration/include/using and both
  `BuildObjectContactManifold(GameModel...)` overloads from the object manifold
  API.
- [x] Build required scene-contact body views from `PhysicsBodyRecord` pose and
  pass exact `ColliderRecord::shape` snapshots into the manifold builder.
- [x] Add runtime-boundary guardrails and self-tests blocking deleted
  object-contact overload/helper and required scene-contact `GameModel` reads.
- [x] Run a focused Debug build for the changed physics/scene files.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph found the remaining `GameModel::GetCollisionShape()` route
inside the object-contact overload and the only live `GameModel` manifold caller
in `Run::UpdateRequiredSceneContacts()`; targeted residue scan found no deleted
manifold overload/helper or scoped scene-contact model path after the edit;
runtime-boundary summary reported 0 errors; focused Debug build passed with 0
warnings/errors; touched source/tool comment audit passed for
`ObjectContactManifold.cpp`, `ObjectContactManifold.h`, `RunScene.cpp`, and
`tools/check_runtime_boundaries.py`; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1035`: delete the
`GameModelCollectionPhysicsAdapter` compatibility artifact after every live
caller moved to append-time handles or direct `PhysicsBodyStore` lookup. Owner:
physics/GameModel authority migration; reason: the adapter no longer had
production callers and its existence preserved a reusable model-index bridge
that could hide store refresh work; deletion condition: no adapter type,
resolver name, source/header, project entry, or filter entry remains in live
source/project files; checker budget: `tools/check_runtime_boundaries.py` blocks
the deleted adapter type/resolver names from source and blocks stale Visual
Studio project entries.

- [x] Remove `GameModelCollectionPhysicsAdapter.h` and `.cpp`.
- [x] Remove the `GameModelCollection` friend/forward declaration.
- [x] Remove Visual Studio project and filter entries.
- [x] Add runtime-boundary guardrails and self-tests for source/project
  resurrection.
- [x] Confirm no live source/project adapter residues remain.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph plus targeted residue scans showed only the adapter's own
files, the collection friend/forward declaration, and project/filter entries
remained before deletion; targeted live residue scan found no
`GameModelCollectionPhysicsAdapter`, `BodyHandleForModelIndex`,
`BodyHandleForSceneObjectId`, `BodyHandleForVelocityCommand`, or
`BodyHandleForWakeCommand` in `SkullbonezSource`, `SKULLBONEZ_CORE.vcxproj`, or
`SKULLBONEZ_CORE.vcxproj.filters` after the edit; runtime-boundary summary
reported 0 errors; touched-file comment audit passed; `tools\validate_fast.bat`
passed formatting, project filters, runtime boundaries, and Profile/Debug
builds with 0 warnings/errors; `tools\validate_physics.bat` passed
standalone/runtime handle smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1036`: stop collider convenience reads and same-count collider edit
commits from reloading model-owned body data. Owner: `GameModelCollection`
compatibility readers/edit commits; reason: `GetColliderStore()` is used by
picks, saves, setup, and smoke checks, so rebuilding collider metadata on every
read creates cache-hostile GameModel import work, while collider edits should
commit explicitly at the owner edge; deletion condition: `GetColliderStore()`
only repairs collider topology drift and `CommitEditedModelPhysicsState(...,
true)` refreshes collider metadata without calling the full
`RefreshColliderStore()` body+collider reload path; checker budget:
`tools/check_runtime_boundaries.py` blocks the deleted unconditional reader
refresh and full collider edit commit shapes.

- [x] Make `GetColliderStore()` count-gate collider snapshot refreshes.
- [x] Make `CommitEditedModelPhysicsState(..., true)` repair body topology only
  when body count drift exists, then call `RefreshColliderSnapshot()`.
- [x] Update runtime handle smoke so same-count collider authoring edits call
  the explicit collider edit commit before reading collider records.
- [x] Add runtime-boundary guardrails and self-tests for unconditional
  read-side collider snapshot refresh and full collider edit commit regression.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph traced `GetColliderStore()` through
`RefreshColliderSnapshot()` into `ColliderStore::Refresh()`, showing the old
read path copied shape/material metadata from `GameModel`; the first physics
checkpoint caught the old smoke assumption, then the smoke was moved to the
explicit commit contract; runtime-boundary summary reported 0 errors; touched
file comment audit passed for `GameModelCollection.cpp`, `Init.cpp`, and
`tools/check_runtime_boundaries.py`; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke including `collider_refresh=pass` and byte-exact
`physics_regression_solver.csv`.

Slice `PHY-1037`: delete the full collider refresh facade. Owner:
`PhysicsEngine`/`PhysicsScene` refresh surface plus launcher/editor/fixed-tree
topology repair callers; reason: `RefreshColliderStore()` always reloaded body
rows before refreshing collider records, so a caller that only needed collider
topology could duplicate model-owned body import work; deletion condition: no
live `RefreshColliderStore()` declaration, definition, or call remains under
`SkullbonezSource`; checker budget: `tools/check_runtime_boundaries.py` blocks
the deleted name as a migration artifact and self-tests source/comment cases.

- [x] Replace `GameModelCollection::ReleaseAttachedFixedTreeParts()` use of
  `RefreshColliderStore()` with `RefreshColliderSnapshot()` after explicit body
  topology repair.
- [x] Replace launcher ray-hit topology repair with explicit body-count repair
  followed by collider snapshot refresh.
- [x] Replace editor wake topology repair with explicit body-count repair
  followed by collider snapshot refresh.
- [x] Delete `PhysicsEngine::RefreshColliderStore()` and
  `PhysicsScene::RefreshColliderStore()` declarations/definitions.
- [x] Add runtime-boundary guardrails and self-tests blocking the deleted full
  collider refresh facade from returning.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: CodeGraph found three live `RefreshColliderStore()` callers before
the edit: fixed-tree release, launcher impulse, and editor wake topology repair;
targeted residue scan found no `RefreshColliderStore(` under
`SkullbonezSource` after deletion; runtime-boundary summary reported 0 errors;
focused Debug build passed with 0 warnings/errors; touched-file comment audit
passed for all touched source/tool files; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1038`: move runtime/editor topology repair off tool-side
`PhysicsModelAccess` construction. Owner: `GameModelCollection` model-order
topology repair methods plus launcher/editor command helpers; reason: launcher
and editor tools only need count-gated body/collider topology repair before
resolving a `PhysicsBodyHandle`, and constructing `PhysicsModelAccess` in tool
code spread the model-owner import facade outside its owner; deletion condition:
`RuntimeTools.cpp` and `RunEditorTools.cpp` contain no `PhysicsModelAccess`,
`RefreshBodyStore(modelAccess)`, or `RefreshColliderSnapshot(modelAccess)`
topology repair; checker budget: `tools/check_runtime_boundaries.py` blocks
runtime/editor tool-side `PhysicsModelAccess` repair and keeps
comment-only/owner-method allow cases.

- [x] Add `GameModelCollection::RepairPhysicsBodyTopology()` and
  `RepairPhysicsBodyAndColliderTopology()` owner methods.
- [x] Move `AddGameModel()`, `GetPhysicsBodyStore()`, `GetColliderStore()`,
  fixed-tree release, and `RunPhysics()` topology repair through the owner
  methods.
- [x] Move launcher and editor wake/sleep helpers off direct
  `PhysicsModelAccess` construction and direct refresh calls.
- [x] Remove the redundant editor sleep-helper `PhysicsEngine&` parameter so
  placement code cannot repair one owner while commanding another engine.
- [x] Add runtime-boundary guardrails and self-tests blocking runtime/editor
  tool-side topology repair from rebuilding `PhysicsModelAccess`.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: targeted residue scan found no `PhysicsModelAccess`,
`RefreshBodyStore(modelAccess)`, or `RefreshColliderSnapshot(modelAccess)` in
`RuntimeTools.cpp` or `RunEditorTools.cpp`; editor placement now calls the
collection-owned sleep helper without passing a separate engine; runtime-boundary summary reported 0
errors; focused Debug build passed with 0 warnings/errors; touched-file comment
audit passed for all touched source/tool files; `tools\validate_fast.bat`
passed formatting, project filters, runtime boundaries, and Profile/Debug
builds with 0 warnings/errors; `tools\validate_physics.bat` passed
standalone/runtime handle smoke and byte-exact `physics_regression_solver.csv`.

Slice `PHY-1039`: move replay velocity-edit body reads off the post-step
`GameModel` mirror. Owner: replay velocity edit input and overlay drawing;
reason: the tool already applies edited velocities through `PhysicsEngine`
handle commands, but hit tests, drag-start velocities, and visible gizmo drawing
still read fixed state, pose, linear velocity, angular velocity, shape, and
radius from `GameModel`; deletion condition: `RunReplayVelocityEdit.inl`
contains no live `GameModel` `IsFixed()`, `GetPosition()`, `GetVelocity()`, or
`GetAngularVelocity()` body reads and no stale
`ApplyReplayVelocityEditToModel` helper name; checker budget:
`tools/check_runtime_boundaries.py` blocks replay velocity `GameModel` body
reads and the stale helper name while allowing store-backed
`PhysicsBodyRecord`/`ColliderRecord` reads.

- [x] Add a local `ReplayVelocityBodyView` resolved from
  `PhysicsBodyStore`/`ColliderStore` after collection-owned topology repair.
- [x] Move replay velocity hit tests, drag ray math, drag-start velocities, and
  overlay drawing to the store-backed view.
- [x] Split `RunEditorTracer::AddSelectionOutline()` so replay velocity can draw
  the same shape outline from explicit position/orientation/shape values.
- [x] Rename `ApplyReplayVelocityEditToModel()` to
  `ApplyReplayVelocityEditToBody()`.
- [x] Add runtime-boundary guardrails and self-tests blocking replay velocity
  `GameModel` body reads and the stale helper name.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: targeted residue scan found no live `GameModel` body reads or
`ApplyReplayVelocityEditToModel` in `RunReplayVelocityEdit.inl`; runtime-boundary
summary reported 0 errors; focused Debug build passed with 0 warnings/errors;
touched-file comment audit passed for all touched source/tool files;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors;
`tools\validate_physics.bat` passed standalone/runtime handle smoke and
byte-exact `physics_regression_solver.csv`.

Slice `PHY-1040`: delete the `GameModelCollection::RunPhysics()` fixed-step
wrapper. Owner: runtime and replay prediction fixed-step edges; reason: the
normal step call should visibly enter `PhysicsEngine::Step()` after explicit
model-owner prep instead of hiding store-owned stepping and model mirror
writeback behind a collection method; deletion condition:
`GameModelCollection` exposes no `RunPhysics` declaration/definition and runtime
or replay prediction code contains no collection `RunPhysics()` call; checker
budget: `tools/check_runtime_boundaries.py` blocks wrapper declarations,
definitions, and call sites in `GameModelCollection`, `RunFrame`, and replay
prediction.

- [x] Delete the `GameModelCollection::RunPhysics()` declaration and
  implementation.
- [x] Move the normal runtime fixed-step edge to
  `RunFrame.cpp::StepRuntimePhysicsTick()`: count-gated topology repair,
  contact-highlight ticking, Debug name-table setup, direct
  `PhysicsEngine::Step()`, fixed-contact presentation, and, after `PHY-1053`,
  no bulk body mirror writeback.
- [x] Move replay prediction stepping to a local
  `StepReplayPredictionPhysicsTick()` helper with the same explicit edge.
- [x] Add runtime-boundary guardrails and self-tests blocking wrapper
  resurrection and call sites.
- [x] Run the intermittent physics regression checkpoint for the slice.
- [x] Validation run for this slice:
  - [x] `git diff --check`
  - [x] `python -m py_compile tools\check_runtime_boundaries.py`
  - [x] `python tools\check_runtime_boundaries.py --repo .`
  - [x] focused Debug build
  - [x] touched-file comment audit
  - [x] `tools\validate_fast.bat`
  - [x] intermittent `tools\validate_physics.bat`

Evidence: targeted residue scan found no `GameModelCollection::RunPhysics`,
`void RunPhysics(` declaration, `m_cGameModelCollection.RunPhysics`, or
`modelCollection.RunPhysics` in the touched runtime/replay sources; runtime
boundary summary reported 0 errors; focused Debug build passed with 0
warnings/errors; touched-file comment audit passed for all source/tool files
touched; `tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors;
`tools\validate_physics.bat` passed standalone/runtime handle smoke and
byte-exact `physics_regression_solver.csv`.

Slice `PHY-1057`: replay velocity-edit target identity now resolves through
`PhysicsBodyStore::HandleForReplayBodyId()` instead of scanning
`GameModel::GetReplayBodyId()` across `GameModelCollection::Models()`. Owner:
replay velocity edit and the body store handle maps; reason: after a
handle-backed path exists, replay identity should not depend on transient vector
order except as a staleable fast hint; deletion condition:
`RunReplayVelocityEdit.inl` has no `ResolveVelocityEditModelIndex()` or
`collection.Models()` target lookup, `ReplayRuntime` returns a
`PhysicsBodyHandle`, and `ApplyReplayVelocityEditToBody()` receives that handle
directly; checker budget: `tools/check_runtime_boundaries.py` blocks the deleted
vector-`GameModel` replay-id resolver and collection `Models()` call shape while
allowing `PhysicsBodyStore` replay-id handle lookup.

Evidence: residue scan found no deleted velocity target lookup in the
velocity-edit path; diff-check, boundary-checker Python compile, and
runtime-boundary validation passed with runtime-boundary summary reporting 0 errors;
focused Debug build passed with 0 warnings/errors; touched-file comment audit
inspected all touched source/tool files; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke and byte-exact 20,001-line `physics_regression_solver.csv`.

Slice `PHY-1058`: replay cause-tree and focus-mask identity now resolve
`ReplayBodyId` to model index through `PhysicsBodyStore::HandleForReplayBodyId()`
instead of scanning `GameModel::GetReplayBodyId()`. Owner: replay cause-tree UI,
focus mask construction, and the body store handle maps; reason: cause/focus
rows can retain model indices for UI row selection and solver-artifact contacts,
but replay identity must not depend on transient model vector order; deletion
condition: `BuildCauseTreeRows()` receives `PhysicsBodyStore`,
`BuildFocusModelMask()` receives `PhysicsBodyStore` plus model count, and neither
builder reads `GameModel` replay ids; checker budget:
`tools/check_runtime_boundaries.py` blocks the deleted GameModel replay-id scans
and old focus-mask `GameModelCollection` parameter while allowing display-name
metadata reads.

Evidence: residue scan found no live cause/focus GameModel replay-id lookup
outside checker self-tests; diff-check, boundary-checker Python compile, and
runtime-boundary validation passed with runtime-boundary summary reporting
0 errors; focused Debug build passed with 0 warnings/errors; touched-file comment
audit inspected all touched source/tool files; `tools\validate_fast.bat` passed
formatting, project filters, runtime boundaries, and Profile/Debug builds with
0 warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke and byte-exact 20,001-line `physics_regression_solver.csv`.

Slice `PHY-1059`: replay path/query/prediction target identity now resolves
`ReplayBodyId` through `PhysicsBodyStore` rows and
`PhysicsBodyStore::HandleForReplayBodyId()` instead of reading
`GameModel::GetReplayBodyId()` from replay target UI code. Owner: replay path
target picking, replay prediction target setup, retained path markers, focus
markers, and the body store handle maps; reason: replay identity should use the
store-owned id table while `GameModel` stays limited to display names and
ragdoll presentation grouping; deletion condition: `RunReplayQueryTools.inl`,
`RunReplayPredictionVisualizer.inl`, and the retained/focus marker sections of
`RunReplayTools.cpp` contain no live `GameModel` replay-id lookup or
`TryGetModel()` target scan; checker budget:
`tools/check_runtime_boundaries.py` blocks deleted GameModel replay-id reads in
those replay target files while allowing direct body-store record/handle lookup.

Evidence: residue scan found no `GetReplayBodyId()` or `TryGetModel()` in the
touched replay target source files outside checker self-tests; diff-check,
boundary-checker Python compile, and runtime-boundary validation passed with
runtime-boundary summary reporting 0 errors; focused Debug build passed with 0
warnings/errors; touched-file comment audit inspected all touched source/tool
files; `tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors; intermittent
`tools\validate_physics.bat` passed standalone/runtime handle smoke and
byte-exact 20,001-line `physics_regression_solver.csv`.

Slice `PHY-1060`: editor selection/gizmo frame math now uses
`PhysicsBodyStore` and `ColliderStore` rows for live pose, orientation, shape,
and radius instead of `GameModel` pose/shape mirrors. Owner: runtime editor
selection frame, transform gizmo hit testing, drag-start snapshots, and replay
transform-change recording; reason: the editor can still use model-order slots
and `GameModel` names for UI identity/grouping, but interactive physics geometry
should be sourced from the store rows the solver and renderer already trust;
deletion condition: `TryGetEditorSelectionFrame` has no model-only signature,
`RunEditorGizmoTools.inl` passes body/collider stores when building selection
frames, and `CaptureEditorGizmoDragGroupState` contains no live
`GameModel::GetPosition()`, `GetOrientation()`, or `GetCollisionShape()` reads;
checker budget: `tools/check_runtime_boundaries.py` blocks the old model-only
helper/call shape and scoped GameModel body reads with rejecting, allowing, and
comment-only self-tests.

Evidence: residue scan found no old editor selection-frame GameModel body reads
outside checker self-tests; `git diff --check`, boundary-checker Python compile,
and runtime-boundary validation passed with runtime-boundary summary reporting 0
errors; focused Debug build passed with 0 warnings/errors; touched-file comment
audit inspected `RunEditorTools.cpp`, `RunEditorGizmoTools.inl`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; intermittent `tools\validate_physics.bat` passed
standalone/runtime handle smoke and byte-exact 20,001-line
`physics_regression_solver.csv`.

Slice `PHY-1063`: authored/generated scene primitive and hull creation now
passes `PhysicsColliderCreateDesc` directly into `GameModelCollection::AddGameModel()`.
Owner: scene setup owns parsed/generated shape facts while `PhysicsScene` owns
live collider rows; reason: scene setup already knows radius, half-extents,
hull, restitution, and contact material, so rereading those values from
`GameModel` only preserves migration work and cache-hostile authority drift;
deletion condition: ragdoll/editor creation also supplies descriptors, runtime
config no longer recaptures descriptors, and the topology-drift
`CaptureAuthoredColliderDesc()` fallback is deleted from `GameModelCollection`;
checker
budget: `tools/check_runtime_boundaries.py` rejects bare scene setup
`AddGameModel(std::move(...))` calls and checks the append helper for direct
collider registration.

Evidence: `git diff --check`, boundary-checker Python compile, and
runtime-boundary validation passed with runtime-boundary summary reporting 0
errors; focused Profile build passed with 0 warnings/errors; a targeted header
format pass plus `tools\validate_format.bat` passed after the first
`validate_fast` attempt stopped on header alignment; touched-file comment audit
inspected `GameModelCollection.h`, `GameModelCollection.cpp`,
`SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke with `collider_refresh=pass` and byte-exact 20,001-line
`physics_regression_solver.csv`.

Slice `PHY-1064`: all append-time creation paths now pass
`PhysicsColliderCreateDesc` into `GameModelCollection::AddGameModel()`.
Owner: ragdoll/editor/runtime/scene creation owns primitive shape facts while
`PhysicsScene` owns live collider rows; reason: every append site already has
radius, half-extents, hull, restitution, and contact-material facts before
model handoff, so keeping a public bare append path made `GameModel` a redundant
collider fact cache; deletion condition: later slices remove same-count editor
shape recapture and topology-drift model-field recapture; checker budget:
`tools/check_runtime_boundaries.py` rejects bare `AddGameModel(std::move(...))`
in scene, editor, launcher, runtime smoke, and ragdoll construction files.

Evidence: `git diff --check`, boundary-checker Python compile, CSV parse check,
and runtime-boundary validation passed with runtime-boundary summary reporting 0
errors; focused Profile build passed with 0 warnings/errors after one namespace
typo fix; touched-file comment audit inspected `GameModelCollection.h`,
`GameModelCollection.cpp`, `PhysicsApi.h`, `Ragdoll.cpp`,
`RunEditorObjectPlacement.inl`, `RunEditorTools.cpp`, `Init.cpp`,
`SceneAuthoredSetup.cpp`, `SceneGeneratedSetup.cpp`, `RuntimeTools.cpp`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke with `collider_refresh=pass` and byte-exact 20,001-line
`physics_regression_solver.csv`.

Slice `PHY-1065`: runtime config collider material now updates dense
`ColliderStore` rows directly instead of recapturing full collider descriptors
from `GameModel`. Owner: `GameModelCollection` owns compatibility policy
application, while `ColliderStore` owns live collider friction and sphere-drag
scalars; reason: config changes material policy, not shape authoring, so
rebuilding every shape descriptor from the model mirror is unnecessary copying
and keeps the wrong authority alive; deletion condition:
`GameModelCollection::ApplyRuntimeConfig()` contains no
`UpdateColliderStoreFromModel()` call, and `CaptureAuthoredColliderDesc()`
remains only for same-count editor shape edits or topology drift until explicit
collider update commands replace it; checker budget:
`tools/check_runtime_boundaries.py` rejects config-time
`UpdateColliderStoreFromModel()` recapture with negative and positive self-tests.

Evidence: CodeGraph mapped the old
`ApplyRuntimeConfig -> UpdateColliderStoreFromModel -> CaptureAuthoredColliderDesc`
path; `git diff --check`, boundary-checker Python compile, and
runtime-boundary validation passed with 0 errors; focused Profile build passed
with 0 warnings/errors; touched-file comment audit inspected
`GameModelCollection.cpp`, `ColliderStore.h/cpp`, `PhysicsEngine.h/cpp`,
`PhysicsScene.h/cpp`, and `check_runtime_boundaries.py`;
`tools\validate_fast.bat` passed formatting, project filters, runtime
boundaries, and Profile/Debug builds with 0 warnings/errors;
`tools\validate_physics.bat` passed standalone/runtime handle smoke with
`collider_refresh=pass` and byte-exact 20,001-line
`physics_regression_solver.csv`.

Slice `PHY-1066`: same-count editor/replay collider shape edits now pass an
explicit `PhysicsColliderCreateDesc` into `CommitEditedModelColliderState()`
instead of using `CommitEditedModelPhysicsState(..., true)` to recapture shape
facts from `GameModelCollection`. Owner: editor/replay scale code owns the edited
shape value; `GameModelCollection` fills body identity and current material
policy before updating the stable `ColliderStore` handle; reason: the old bool
commit hid body-vs-collider ownership and let same-count editor shape edits
reopen `CaptureAuthoredColliderDesc()`; deletion condition:
`CommitEditedModelPhysicsState` is absent from `SkullbonezSource`, editor/replay
scale commits pass descriptors, and the next slice deletes the topology-drift
`CaptureAuthoredColliderDesc()` fallback; checker budget: `tools/check_runtime_boundaries.py` rejects
the deleted bool API source-wide and still rejects full collider-store refresh
inside the explicit collider edit command.

Evidence: CodeGraph traced the old same-count editor recapture path; residue
scan found no `CommitEditedModelPhysicsState` under `SkullbonezSource`;
`git diff --check`, boundary-checker Python compile, CSV parse check, and
runtime-boundary validation passed with 0 errors; focused Profile build passed
with 0 warnings/errors; touched-file comment audit inspected `GameModel.cpp`,
`GameModel.h`, `GameModelCollection.cpp`, `GameModelCollection.h`,
`RunEditorTools.cpp`, `RunEditorGizmoTools.inl`, `RunFrame.cpp`, `Init.cpp`,
and `check_runtime_boundaries.py`; targeted clang-format/header alignment fixed
`Init.cpp` and `GameModel.h`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke with `collider_refresh=pass` and byte-exact 20,001-line
`physics_regression_solver.csv`.

Slice `PHY-1067`: collider topology drift now fails closed instead of
recapturing shape/material facts from `GameModel`. Owner: `ColliderStore` owns
live dense collider rows and `PhysicsScene` owns body-to-collider rebinding;
reason: missing collider rows mean a creation/editor command failed to create
shape data, so resizing rows and rereading model fields creates hidden copies,
default rows, and cache-hostile authority drift; deletion condition:
`CaptureAuthoredColliderDesc`, `UpdateColliderStoreFromModel`,
`GameModelCollection::RefreshPhysicsColliders`, and
`PhysicsModelAccess::RefreshPhysicsColliders` are absent from
`SkullbonezSource`; checker budget: `tools/check_runtime_boundaries.py` blocks
the deleted names, `RefreshColliderSnapshot(modelAccess)`, and
`ColliderStore::RefreshBodyBindings()` resizing/creating collider rows.

Evidence: residue scan found no deleted source spellings; `git diff --check`,
boundary-checker Python compile, CSV parse check, and runtime-boundary validation
passed with 0 errors; focused Profile build passed with 0 warnings/errors;
`tools\validate_format.bat` passed; touched-file comment audit inspected
`GameModelCollection.cpp`, `GameModelCollection.h`, `ColliderStore.cpp`,
`ColliderStore.h`, `PhysicsEngine.cpp`, `PhysicsEngine.h`,
`PhysicsModelAccess.h`, `PhysicsScene.cpp`, `PhysicsScene.h`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; `tools\validate_physics.bat` passed standalone/runtime handle
smoke with `collider_refresh=pass` and byte-exact 20,001-line
`physics_regression_solver.csv`. Logs:
`TestOutput\validation\physics_store_authority\validate_fast_collider_topology_fail_closed.log`
and
`TestOutput\validation\physics_store_authority\validate_physics_collider_topology_fail_closed.log`.

Slice `PHY-1068`: persistent replay identity now belongs to
`PhysicsBodyStore` rows after append instead of a `GameModelCollection`
scene-order sidecar. Owner: `PhysicsBodyStore` owns stored replay ids and
`GameModelCollection` retained only the explicit follow-up allocator/reload
scratch that `PHY-1069` removes; reason: the deleted sidecar duplicated a
store-owned scalar, added
memory/stat surface area, and made the migration look cleaner than it was while
still carrying collection-order authority; deletion condition:
`SkullbonezSource` has no persistent collection replay-id sidecar or replay-id
memory stat, append passes the reserved id directly into
`RegisterAuthoredBody`, and body reload builds only a local cold scratch id
stream from existing body-store rows plus new ids for missing rows; checker
budget: `tools/check_runtime_boundaries.py` blocks the deleted sidecar/stat
spellings and self-tests the allowed reload scratch.

Evidence: residue scan found no deleted sidecar/stat names under
`SkullbonezSource`; `git diff --check`, boundary-checker Python compile,
workqueue CSV parse, and runtime-boundary validation passed with 0 errors;
focused Profile build passed with 0 warnings/errors; `tools\validate_format.bat`
passed; touched-file comment audit inspected `MainMemoryStats.h`,
`GameModelCollection.cpp`, `GameModelCollection.h`, `PhysicsBodyStore.cpp`,
`PhysicsBodyStore.h`, `DiagnosticsRuntime.cpp`, and
`check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors in 38.4s; `tools\validate_physics.bat` passed
standalone/runtime handle smoke with `collider_refresh=pass` and byte-exact
20,001-line `physics_regression_solver.csv` in 13.9s. Logs:
`TestOutput\validation\physics_store_authority\validate_fast_replay_id_store_authority.log`
and
`TestOutput\validation\physics_store_authority\validate_physics_replay_id_store_authority.log`.

Slice `PHY-1069`: scene/editor/runtime creation now owns body/collider scene
identity, and `GameModelCollection` no longer has a replay-id allocator. Owner:
`RunSceneState` owns the per-load `PhysicsSceneObjectId` cursor and re-bases it
from `PhysicsBodyStore` rows after replay restore trims; `GameModelCollection`
only appends model order and forwards the caller-supplied id to the body and
collider rows. Reason: collection-side `ReserveReplayBodyId` was a migration
artifact that kept creation identity hidden behind the model container and could
drift after replay rewinds if moved naively. Deletion condition:
`SkullbonezSource` has no `m_nextReplayBodyId`,
`ReserveReplayBodyId`, `RebuildNextReplayBodyIdFromBodyStore`, or optional
`AddGameModel(..., uint32_t replayBodyId)` shape; ragdoll consumes a caller
reserved contiguous id range; runtime projectiles allocate from scene state; and
checker budget: `tools/check_runtime_boundaries.py` rejects the deleted allocator
and old append signature while allowing local cold reload scratch.

Evidence: `git diff --check`, boundary-checker Python compile, workqueue CSV
parse, and runtime-boundary validation passed with 0 errors; focused Profile
build passed with 0 warnings/errors after one namespace fix; `tools\validate_format.bat`
passed after targeted formatting/header alignment; touched-source comment audit
inspected all touched source/tool files; `tools\validate_full.bat` passed Project
Filters/runtime boundaries, Profile and Debug builds at 0 warnings/errors, DX12
InfoQueue 0 errors, DX12 screenshots matching baselines, and byte-exact
20,001-line `physics_regression_solver.csv`. Log:
`TestOutput\validation\physics_store_authority\validate_full_scene_owned_identity.log`.

Slice `PHY-1070`: `PhysicsModelAccess` no longer exposes a generic
`ModelCount()` query. Owner: `GameModelCollection` owns model order and passes
the expected count into body/render refresh calls; `PhysicsScene` only compares
store counts against that caller-owned value before asking the facade to refresh
rows. Reason: a model-count query made the facade look like a general model-order
view again, inviting future physics code to ask model-owner questions through a
compatibility object. Deletion condition: `PhysicsModelAccess.h` and its
definitions expose no `ModelCount`, `Count`, or `size` style query; checker
budget: `tools/check_runtime_boundaries.py` rejects the deleted count query
alongside the older step facade methods.

Evidence: CodeGraph traced `RepairPhysicsBodyAndColliderTopology()` through
`RefreshBodyStore()` and the render refresh call sites; residue scan found no
`PhysicsModelAccess::ModelCount` or `modelAccess.ModelCount()` source calls;
`git diff --check`, boundary-checker Python compile, and runtime-boundary
validation passed with 0 errors; focused Profile build passed with 0
warnings/errors after fixing the render refresh count path; touched-source
comment audit inspected `GameModelCollection.cpp`, `PhysicsModelAccess.h`,
`PhysicsEngine.h/cpp`, `PhysicsScene.h/cpp`, and
`tools/check_runtime_boundaries.py`; `tools\validate_fast.bat` passed formatting,
project filters, runtime boundaries, and Profile/Debug builds with 0
warnings/errors; `tools\validate_physics.bat` passed standalone/runtime smoke and
byte-exact 20,001-line `physics_regression_solver.csv`. Logs:
`TestOutput\validation\physics_store_authority\validate_fast_model_access_count_query.log`
and
`TestOutput\validation\physics_store_authority\validate_physics_model_access_count_query.log`.

Slice `PHY-1071`: render projection no longer routes through
`PhysicsModelAccess`. Owner: `GameModelCollection` owns the remaining
model-order presentation facts (`m_gameModels`, material, and highlight state)
and performs the one cold `RenderInstanceStore` fill after `PhysicsScene` and
`PhysicsEngine` prepare body/collider rows. Reason: putting
`RefreshRenderInstances()` on `PhysicsModelAccess` made the model-owner facade
look like a general render bridge and invited a generic callback/adapter shape
back into the render path. Deletion condition: `PhysicsModelAccess.h` and its
definitions expose no `RefreshRenderInstances()` method; render projection uses
`GameModelCollection::RefreshRenderInstances()` privately, and the mutable
render-store accessor is only accepted in the collection owner edge plus
`PhysicsEngine`/`PhysicsScene` forwarding. Checker budget:
`tools/check_runtime_boundaries.py` rejects the deleted
`PhysicsModelAccess::RefreshRenderInstances()` facade and any
`MutableRenderInstances()` caller outside the approved owner/forwarding files.

Evidence: CodeGraph traced the old
`GameModelCollection::PrepareRenderInstances()` ->
`PhysicsEngine::RefreshRenderStore()` -> `PhysicsScene::RefreshRenderStore()` ->
`PhysicsModelAccess::RefreshRenderInstances()` path; residue scan found no
`PhysicsModelAccess::RefreshRenderInstances` in source; `git diff --check`,
boundary-checker Python compile, and runtime-boundary validation passed with 0
errors; touched-source comment audit inspected `GameModelCollection.cpp`,
`GameModelCollection.h`, `PhysicsModelAccess.h`, `PhysicsEngine.cpp`,
`PhysicsEngine.h`, `PhysicsScene.cpp`, `PhysicsScene.h`, and
`check_runtime_boundaries.py`; `tools\validate_full.bat` passed project filters,
runtime boundaries, Profile/Debug builds at 0 warnings/errors, DX12 InfoQueue 0
errors, DX12 screenshots matching baselines, and byte-exact 20,001-line
`physics_regression_solver.csv`. Log:
`TestOutput\validation\physics_store_authority\validate_full_render_projection_facade_shrink.log`.

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

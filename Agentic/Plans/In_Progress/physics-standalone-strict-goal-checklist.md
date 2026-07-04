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
  - [ ] replay restore/prediction,
  - [ ] diagnostics and debug overlays.
- [ ] Store `PhysicsBodyHandle` or stable scene object id at the caller where the
  command is created, not at the last moment inside physics.
- [ ] Keep model indices only for UI selection and render presentation while
  those surfaces still use model order.
- [ ] Add count guardrails for adapter call sites and lower the count after each
  migrated group.
- [ ] Delete old `GameModelCollection` wrapper methods only after all callers in
  that group migrate.
- [ ] Validation for this phase:
  - [ ] `tools\validate_fast.bat` for guardrail/project changes.
  - [ ] `tools\validate_physics.bat` for physics command behavior.
  - [ ] `tools\validate_full.bat` for replay/editor/scene lifecycle breadth.

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
`tools\validate_full.bat` all passed. The replay restore/prediction checkbox
remains unchecked because `TryRestoreReplayPredictionBodyState` still uses
`CommitEditedModelPhysicsState`.

## Phase 8 - Diagnostics And SkullScope

Target: diagnostics consume physics views and bounded query output, not raw
model-backed ownership.

- [ ] Define a diagnostics sink input made of immutable physics views:
  bodies, colliders, contacts, islands, constraints, broadphase stats, solver
  stats, and deterministic frame ids.
- [ ] Move `PhysicsDiagnosticsSink` and SkullScope frame emission away from
  direct model vector reads one view at a time.
- [ ] Keep raw NDJSON/SQLite out of model context; use `tools\physics_query.bat`
  queries for investigation.
- [ ] If output schema changes, update query baselines only from final Debug
  artifacts.
- [ ] Report SkullScope query cost in final handoff whenever SkullScope is used:
  trace bytes, SQLite bytes, query commands, per-query output size, total
  GPT-read size.
- [ ] Validation for this phase:
  - [ ] `tools\validate_physics_deep.bat`
  - [ ] `tools\validate_physics.bat` if core deterministic CSV behavior is
    touched.

## Phase 9 - Guardrails

- [ ] Extend `tools/check_runtime_boundaries.py` to reject new
  `GameModelCollection`, raw `GameModel`, or mutable model-vector dependencies
  in public physics API and standalone physics implementation files.
- [ ] Add negative synthetic tests for:
  - [ ] public API header includes `GameModelCollection`,
  - [ ] standalone physics source calls `modelAccess.Models()`,
  - [ ] new model-index command field in a public physics descriptor,
  - [ ] new compatibility accessor borrower without an allowlist update.
- [ ] Add positive synthetic tests for:
  - [ ] explicit runtime adapter use,
  - [ ] diagnostics-only view consumption,
  - [ ] test/tool-only fixture use.
- [ ] Keep allowlists counted and exact by file/label/line group where possible.
- [ ] Lower counts in the same commit that removes a compatibility borrower.
- [ ] Validation for this phase:
  - [ ] `tools\validate_fast.bat`
  - [ ] direct changed-script run if the guardrail script changes.

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

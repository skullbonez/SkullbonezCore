# Physics Authority And Stable Identity

Date: 2026-07-11 (completed and source reconciled)
Status: Complete — 16/16 implementation items and 5/5 closure proofs verified
Impact area: physics, game object storage, scene creation/reset, replay,
editor tools
Owner: physics/scene boundary

## Goal

- `PhysicsBodyStore` owns body state; `ColliderStore` owns collision state;
  `RenderInstanceStore` owns render projection; scene/entity metadata owns
  names, grouping, asset-instance identity, and authored hierarchy.
- `GameModelCollection` does not own `PhysicsEngine` or act as the production
  physics/render integration surface.
- Persistent state stores stable identity only: `PhysicsBodyHandle` for live
  bodies, `ReplayBodyId` for replay, and `PhysicsSceneObjectId` for authored
  identity. A dense row is a frame-local cursor or typed `ModelRowHint`.

## Reconciled Current State

Verified complete in current source: `GameModel` stores transient contact
feedback only; `PhysicsModelAccess` and the old model-index physics adapter are gone;
public physics APIs no longer expose `GameModel`; physics implementation files
do not depend on `GameModel`; body/collider/render stores and stable handles
exist; replay ids live on physics rows.

Completed: `SceneController` owns `PhysicsEngine`; scene metadata, physics,
collider, and render stores have explicit coordinated creation/deletion edges;
authoring mutations and queued interaction identity use stable handles; retained
dense rows are typed `ModelRowHint` caches rather than object identity.

## Binding Owner Decision

Promote `PhysicsScene` as the scene-lifetime physics owner held through the
promoted `SceneController`; `Run` wires it, and `GameModelCollection` receives
narrow body/collider/render creation commands. Scene load/reset owns
construction and teardown order. Replay restore enters through an explicit
physics command and never reconstructs authority through presentation rows.

Before A2 source changes, record the exact construction/reset sequence and
stale-handle behavior in the phase notes and covering tests; the owner itself is
no longer an open question.

## Checklist

### A. Body authority and physics ownership

- [x] A1. Audit external writes to sleep/wake, force, impulse, pose, mass, and
  authored descriptors; route every mutation through handle-based physics
  commands. `GameModel` fields are already clean and are not part of this row.
  Evidence (2026-07-11): `PhysicsEngine::UpdateAuthoredBody` and its coordinated
  body/collider variant resolve a `PhysicsBodyHandle`, preserve current live
  state for unchanged fields, and update the cold descriptor plus live row in
  one physics-owned command. The collection edit packet, raw descriptor-row
  mutators, editor/replay collection facades, and unused replay restore facades
  were deleted. The handle smoke proves surviving-handle mass/velocity mutation
  and stale-handle rejection.
- [x] A2. Move `PhysicsEngine` ownership out of `GameModelCollection`; runtime
  stepping and diagnostics borrow the physics owner directly.
  Evidence (2026-07-11): `SceneController` now physically owns the
  scene-lifetime `PhysicsEngine`; `GameModelCollection` requires one borrowed
  engine at construction and exposes no physics-owner getter. Live stepping,
  replay target stepping/restore/probes, renderer frame views, editor tools,
  automation, generated/authored setup, and runtime diagnostics receive the
  scene-owned engine explicitly. The standalone handle smoke owns a separate
  cold validation engine and still proves atomic entity/body/collider/render
  creation. `tools\validate_fast.bat` passed zero-warning Profile/Debug builds
  in 40.7s, `tools\validate_all_cpu_tests.bat` passed 125/125 doctest cases and
  2,708 assertions plus every CPU lane in 11.0s,
  `tools\validate_physics.bat` preserved the 20,001-line byte-exact baseline in
  16.7s, `tools\validate_perf.bat` passed allocation and performance checks in
  46.7s, and `tools\validate_full.bat` passed zero-warning builds, zero DX12
  InfoQueue errors, matching screenshots, standalone physics smoke, and the
  byte-exact physics baseline in 52.3s. The touched-source comment audit
  inspected 30/30 files with 0 deferred.
- [x] A3. Provide one coordinated body registration path and one deterministic
  deletion path that invalidates handles and removes paired rows.
  Evidence (2026-07-11): split body/collider registration is deleted;
  `PhysicsAuthoredBodyRegistration` publishes both handles or rolls back.
  `DestroySceneEntity` coordinates physics descriptor/body/collider/joint,
  scene entity, presentation, and render swap-last removal. The runtime handle
  smoke proves atomic failed creation, handle invalidation, paired row counts,
  constraint removal, moved-handle preservation, and preservation of a
  surviving live-only replay pose without a cold descriptor reload.

### B. Collider authority

- [x] B1. Broadphase/narrowphase physics implementation reads store records and
  has no `GameModel` dependency. Re-verify during each physics slice.
- [x] B2. `GameModel` no longer carries collider compatibility fields.

### C. Scene/entity metadata split

- [x] C0. Inventory the complete `assetInstances[]` parse/create/save path and
  bind the durable owner design. Evidence:
  `Agentic/Reports/scene_asset_roundtrip_design_20260710.md`.
- [x] C1a. Preserve parsed asset library, asset, instance, ordered part, and
  override provenance. Compose compound transforms with rotated offsets and
  quaternion multiplication, retain exact shape-row sources, and reject name
  collisions across explicit objects, asset parts, and deferred ragdoll parts.
  Evidence (2026-07-10): the standalone parser suite passes a two-library,
  two-instance mixed-shape fixture with nonzero provenance indices plus
  duplicate/partial-failure rollback cases. Runtime ragdoll construction now
  preflights every fixed-buffer part name before its first append. The final
  source passed `tools\validate_scene_parser_tests.bat`, the mandatory
  `tools\validate_all_cpu_tests.bat` umbrella, `tools\validate_physics.bat`
  with a 20,001-line byte-exact solver baseline, and `tools\validate_full.bat`
  with zero DX12 validation errors and matching screenshots. The subsequent
  performance gate exposed eight legacy duplicate ball/box names in
  `physics_bench_varied.scene.json`; the ball rows now have unique authored
  names, and the final perf/deep/full gates pass without weakening
  duplicate-name rejection. The deterministic signature proof, exact
  SkullScope trace/query commands, artifact sizes, and bounded model-read
  accounting are recorded in
  `Agentic/Reports/2026-07-10/keyboard-router-skullscope-baseline-evidence.md`.
- [x] C1b. Add schema-versioned explicit `PhysicsSceneObjectId` values for
  authored objects and per-instance parts, reject duplicate/zero ids, and feed
  those ids into the creation transaction instead of allocation by shape-section
  order. Version 1 remains readable through one deterministic upgrade path.
  Evidence (2026-07-10): schema v2 requires nonzero `sceneObjectId` fields on
  direct physics objects and ordered `{name, sceneObjectId}` part records on
  each asset instance. The parser rejects missing, zero, duplicate,
  wrong-version, part-count, and part-name identity input before publishing the
  private scene. Version 1 performs one post-parse upgrade in the exact legacy
  runtime section order, then asset provenance resolves back to those stored
  ids. Authored setup forwards parsed ids unchanged and rebases the runtime
  allocation cursor above the highest sparse id; no authored creation loop
  calls `AllocateSceneObjectId*`. The final source passed
  `tools\validate_scene_parser_tests.bat` (6/6 contracts, 8.1s),
  `tools\validate_all_cpu_tests.bat` (114 doctest cases/2,096 assertions plus
  all standalone CPU targets, 11.7s), `tools\validate_physics.bat` (20,001-line
  byte-exact baseline, 26.9s), and `tools\validate_full.bat` (zero warnings,
  zero DX12 InfoQueue errors, matching screenshots, byte-exact physics, 75.3s).
- [x] C2. Extract a preallocated, scene-owned `SceneEntityStore` for display
  names, durable render material intent, asset affiliation, and stable ids.
  Remove those ownership duties from `GameModel`/collection order.
  Evidence (2026-07-10): `SceneController` now owns `SceneEntityStore`, whose
  pre-scene reservation holds the stable id/live body join, fixed display name,
  durable `RenderMaterial`, and exact library/asset/instance/part affiliation.
  All seven production creation call paths submit `SceneEntityCreateDesc`;
  authored shape rows resolve the parser's exact source/index provenance before
  commit, body-store rebuilds refresh handles only when stable ids match, and
  replay, save, style, launcher, selection, and automation consumers read the
  scene owner. `GameModel` is reduced to transient contact-highlight timers;
  its name/material/tint fields and the collection display-name facades are
  deleted. The store reserves only configured cold metadata pages before scene
  population and retains the reservation across clears; commit cannot grow it.
  This corrected an initial +5.28/+5.38 MB performance-gate regression from an
  eagerly zeroed `MAX_GAME_MODELS` array without weakening the capacity ceiling.
  `TestSceneEntityStore.cpp` covers metadata, duplicates, capacity, trim,
  stable-id handle refresh, and reservation reuse. Allocation-policy self-test
  and repository scan passed (306 files, 0 allowlist errors, 7.3s); the final
  source passed `tools\validate_all_cpu_tests.bat` (116 doctest cases/2,122
  assertions plus all standalone CPU targets, 13.8s),
  `tools\validate_fast.bat` (format/project metadata and zero-warning
  Profile/Debug builds, 23.3s), `tools\validate_physics.bat` (20,001-line
  byte-exact baseline, 16.6s), `tools\validate_perf.bat` (32.9s), and
  `tools\validate_full.bat` (zero warnings, zero DX12 InfoQueue errors,
  matching screenshots, byte-exact physics, 49.2s). The touched-source comment
  audit inspected 32/32 files with 0 deferred.
- [x] C3. One preflighted creation transaction commits scene metadata, body,
  collider, and render rows or commits none of them. Capacity/duplicate authored
  input is Lane R; owner topology divergence is Lane F.
  Evidence (2026-07-10): all seven production creation functions enter
  `TryCreateSceneEntity`; the former `AddGameModel`, private append wrapper, and
  separately supplied scene-id parameter are deleted. Each caller sets the
  entity's single authoritative id. The command verifies aligned entity,
  transient feedback, behavior-group, authored-descriptor, body, collider,
  render-presentation, and render-instance counts, plus physics/render
  reservations, before mutation. Capacity, duplicate id, invalid group, and
  body/entity-id mismatch return Lane R; preflight reservation or topology
  divergence is Lane F and is no longer silently repaired during creation.
  Commit appends body/collider rows, publishes the entity/body join, and creates
  render presentation/instance/handle rows from existing reservations, then
  rechecks every count. Clear now removes render rows and proves zero topology.
  The standalone runtime smoke attempts a duplicate id and verifies unchanged
  entity, descriptor, body, collider, and render counts; its waited report says
  `creation_atomic=pass`. `TestSceneEntityStore.cpp` also verifies the three
  render-side rows publish together. The probe's nine fixed-array standalone
  worlds and reorder store moved from the bounded launcher stack to explicit
  cold validation ownership after a waited Profile run exposed stack overflow.
  Allocation-policy self-test/repository scan passed (306 files, 0 allowlist
  errors, 7.4s). Final source passed `tools\validate_all_cpu_tests.bat` (117
  doctest cases/2,129 assertions plus all standalone CPU targets, 15.7s),
  `tools\validate_fast.bat` (format/project metadata and zero-warning
  Profile/Debug builds, 32.2s), `tools\validate_physics.bat`
  (`creation_atomic=pass` and 20,001-line byte-exact baseline, 16.6s),
  `tools\validate_perf.bat` (32.7s), and `tools\validate_full.bat` (zero
  warnings, zero DX12 InfoQueue errors, matching screenshots, atomic creation
  smoke, and byte-exact physics, 49.7s). The touched-source comment audit
  inspected 15/15 files with 0 deferred.
- [x] C4. Save through borrowed `SceneSaveView`/`SceneSaveRequest` owner data,
  emit version-2 `assetInstances[]` per-part live state, and delete silent row
  skipping plus the collection save facade.
  `SceneSnapshotWriter` now borrows explicit entity/body/collider/group/joint
  owner views plus scalar world/camera request values and returns `SbResult` for
  file failures. It resolves rows through stable entity/body/collider identity;
  count, identity, asset-part topology, behavior-root, or joint disagreement is
  fatal instead of skipped. The collection facade is deleted and all three
  callers assemble synchronous borrowed views. Schema v2 groups asset-backed
  rows by stable asset root, emits exact library/asset/instance/ordered-part
  affiliation, and stores authoritative per-part pose, velocity, angular
  velocity, sleep/fixed state, mass, inertia, restitution, contact material,
  shape values, release intent, display name, and explicit id. Asset convex
  hulls retain the recipe's authored hull path because live shapes expose only
  a diagnostic name. The parser accepts identity-only v2 parts or typed live
  state, routes saved hull parts through `SceneConvexHullState`, and preserves
  affiliation during recreation. The new no-`Run` writer/parser test covers a
  mixed box/sphere/hull asset, sparse ids, explicit false overrides, a direct
  non-asset row, render materials, and contact-release intent (118 doctest
  cases/2,159 assertions overall). A waited production
  `building_assets_showcase` save/reload probe passed with a 466,775-byte
  snapshot. Allocation policy scanned 306 files with 0 allowlist errors.
  `tools\validate_all_cpu_tests.bat` passed all four CPU suites in 18.1s;
  `tools\validate_full.bat` passed formatting/project metadata, zero-warning
  Profile/Debug builds, the CPU umbrella, zero DX12 InfoQueue errors with
  matching screenshots, atomic creation smoke, and the 20,001-line byte-exact
  physics baseline in 87.2s. The touched-source comment audit inspected 13/13
  files with 0 deferred.
- [x] C5. Replace `rootModelIndex` behavior grouping with stable root object id.
  Keep asset affiliation and behavior group as separate metadata dimensions.
  Evidence (2026-07-10): `SceneEntityStore` owns a separate
  `SceneBehaviorGroup {kind, rootObjectId, partIndex}` beside asset
  affiliation. Creation validates self-root part zero or an already committed
  compatible stable root before mutation; collection physics compatibility
  derives a dense row only at cold boundaries. Authored ragdolls, releasable
  trees, editor placement, attached-camera fallback, render preparation, and
  scene snapshots consume stable roots. The collection-owned group sidecar,
  its create/record/kind types, fourth creation argument, memory accounting,
  and row-root APIs are deleted; the scoped source contains no behavior
  `rootModelIndex` or `rootObjectIndex` spelling.

  The required C1-C5 adversarial review initially blocked completion because a
  missing `objectGroup.root` could publish id zero and the no-`Run` fixture
  stopped after reparsing. The correction validates final explicit/legacy root
  topology after includes and version-1 id upgrade, adds a recoverable malformed
  root test, and recreates fresh entity/body/collider owners from the saved
  scene. The 444-assertion fixture compares by sparse stable id across dense-row
  reorder, including quaternion-equivalent orientation, complete hull geometry,
  durable material name/alpha/response/flags, and one row carrying independent
  asset and behavior roots. The one permitted follow-up adversarial pass found
  no remaining blocking, non-blocking, or missing-evidence findings.

  Final source passed `tools\validate_scene_parser_tests.bat` (all six
  contracts, 6.5s), `tools\validate_all_cpu_tests.bat` (120 doctest cases/2,633
  assertions plus all standalone CPU targets, 11.0s),
  `tools\validate_physics.bat` (atomic creation smoke and 20,001-line
  byte-exact baseline, 27.8s), `tools\validate_perf.bat` (32.7s), and
  `tools\validate_full.bat` (zero-warning Profile/Debug builds, zero DX12
  InfoQueue errors with matching captures, and byte-exact physics, 49.6s).
  Allocation policy scanned 306 files with 0 allowlist errors. The touched-file
  comment audit inspected 17/17 source-bearing files with 0 deferred.

### D. Stable identity storage

- [x] D1. Runtime interaction command payloads capture `PhysicsBodyHandle` at
  enqueue time.
  Evidence (2026-07-11): `RuntimeInteractionCommand` no longer contains a model
  index; every non-clear selection producer captures body/collider handles and
  preparation derives a typed row only after both handles resolve as a pair.
- [x] D2. Stored index members in runtime state, interaction, attached-camera,
  and pick-service headers become handles or explicitly typed row hints.
  Evidence (2026-07-11): gestures and prepared gizmo plans retain body handles;
  attach-camera and pick results retain `ModelRowHint`; camera tracking and
  reset snapshots use `ModelRowHint`. The standalone interaction policy tests
  assert gesture handle generation as well as slot identity.
- [x] D3. Replay live-state hints become `ModelRowHint`; recorded sample rows
  remain row-at-record-time data keyed by authoritative replay id. Complete
  2026-07-11 with CPU, scrub, interaction, physics, DX12, and full evidence in
  `Agentic/Reports/replay_r4_live_owner_identity_20260711.md`.
- [x] D4. Delete redundant range-validation branches and reconcile glossaries.
  Acceptance: no header stores a bare model-index integer as persistent identity.
  Evidence (2026-07-11): selection preparation derives rows from validated
  handles, picker callers no longer revalidate the picker-owned row against the
  same store, and handle resolvers no longer repeat store-owned range checks.
  Interaction, picker, attach-camera, and handle glossaries now distinguish
  stable identity from typed synchronous row caches.

## Cross-Plan Dependencies

- Scene ownership/creation coordinates with `runtime-shell-decomposition.md`
  extraction 3.
- Replay identity coordinates with
  `replay-architecture-and-right-sizing.md` R4.
- The binding scene/asset design is recorded in
  `Agentic/Reports/scene_asset_roundtrip_design_20260710.md`; final
  `assetInstances[]` round-trip evidence comes from
  `behavioral-test-depth.md` P3.
- Broad gate claims depend on `validation-gate-integrity.md` V2.

## Acceptance

- [x] Physics ownership is outside `GameModelCollection`.
- [x] Production step, replay restore, editor commands, and diagnostics use
  handle/store APIs without a collection physics facade.
- [x] Scene/entity metadata is separate from simulation state.
- [x] Creation/deletion/reset preserve row pairing and stale-handle rejection.
- [x] Persistent headers contain no untyped model-index identity.

Closure evidence (2026-07-11): `tools\validate_runtime_interaction_policy.bat`
passed Debug and Release; all five `tools\validate_interaction_clicks.bat`
scenarios passed in 14.0s; `tools\validate_full.bat` passed in 70.0s with
131/131 doctest cases and 2,814 assertions, every standalone CPU lane,
zero-warning Profile/Debug builds, zero DX12 InfoQueue errors and matching
captures, the expanded handle smoke, and the 44,401-line varied physics CSV
byte-exact. The touched-source comment audit inspected 45/45 files with zero
deferred. The plan-level adversarial review found and fixed a cold-descriptor
reload that could teleport surviving live bodies during deletion; the required
repeat review was clean. See
`Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md`.

## Validation

| Slice | Gate |
|---|---|
| Body/collider/solver | CPU tests + `tools\validate_physics.bat` |
| Scene creation/reset/serialization | parser round-trip + physics + full gate |
| Replay identity | CPU replay tests + replay scrub + full gate |
| Editor/tool identity | interaction-policy tests + interaction clicks + fast/full gate as mapped |
| Ownership closure | full gate after CPU umbrella integration + final independent review |

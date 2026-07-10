# Physics Authority And Stable Identity

Date: 2026-07-10 (source reconciled)
Status: In progress — 2/12 current checklist items verified complete; the
scene-lifetime physics owner decision is binding
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

Verified complete in current source: `GameModel` stores presentation metadata
only; `PhysicsModelAccess` and the old model-index physics adapter are gone;
public physics APIs no longer expose `GameModel`; physics implementation files
do not depend on `GameModel`; body/collider/render stores and stable handles
exist; replay ids live on physics rows.

Still open: `GameModelCollection` physically owns `PhysicsEngine`, scene
creation/reset commits model/physics/render rows as a collection transaction,
scene grouping/name metadata remains collection/model ordered, and runtime/
replay headers still store many bare model-index hints.

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

- [ ] A1. Audit external writes to sleep/wake, force, impulse, pose, mass, and
  authored descriptors; route every mutation through handle-based physics
  commands. `GameModel` fields are already clean and are not part of this row.
- [ ] A2. Move `PhysicsEngine` ownership out of `GameModelCollection`; runtime
  stepping and diagnostics borrow the physics owner directly.
- [ ] A3. Provide one coordinated body registration path and one deterministic
  deletion path that invalidates handles and removes paired rows.

### B. Collider authority

- [x] B1. Broadphase/narrowphase physics implementation reads store records and
  has no `GameModel` dependency. Re-verify during each physics slice.
- [x] B2. `GameModel` no longer carries collider compatibility fields.

### C. Scene/entity metadata split

- [ ] C1. Move display names/labels, grouping/hierarchy, and asset-instance
  identity from `GameModel`/collection-order storage to scene/entity metadata.
- [ ] C2. One creation transaction builds scene metadata, body, collider, and
  render rows; scene save serializes from authoritative owners.
- [ ] C3. Replace `rootModelIndex` group identity with stable scene/entity
  grouping identity.

### D. Stable identity storage

- [ ] D1. Runtime interaction command payloads capture `PhysicsBodyHandle` at
  enqueue time.
- [ ] D2. Stored index members in runtime state, interaction, attached-camera,
  and pick-service headers become handles or explicitly typed row hints.
- [ ] D3. Replay live-state hints become `ModelRowHint`; recorded sample rows
  remain row-at-record-time data keyed by authoritative replay id.
- [ ] D4. Delete redundant range-validation branches and reconcile glossaries.
  Acceptance: no header stores a bare model-index integer as persistent identity.

## Cross-Plan Dependencies

- Scene ownership/creation coordinates with `runtime-shell-decomposition.md`
  extraction 3.
- Replay identity coordinates with
  `replay-architecture-and-right-sizing.md` R4.
- `assetInstances[]` round-trip evidence comes from
  `behavioral-test-depth.md` P3.
- Broad gate claims depend on `validation-gate-integrity.md` V2.

## Acceptance

- [ ] Physics ownership is outside `GameModelCollection`.
- [ ] Production step, replay restore, editor commands, and diagnostics use
  handle/store APIs without a collection physics facade.
- [ ] Scene/entity metadata is separate from simulation state.
- [ ] Creation/deletion/reset preserve row pairing and stale-handle rejection.
- [ ] Persistent headers contain no untyped model-index identity.

## Validation

| Slice | Gate |
|---|---|
| Body/collider/solver | CPU tests + `tools\validate_physics.bat` |
| Scene creation/reset/serialization | parser round-trip + physics + full gate |
| Replay identity | CPU replay tests + replay scrub + full gate |
| Editor/tool identity | interaction-policy tests + interaction clicks + fast/full gate as mapped |
| Ownership closure | full gate after CPU umbrella integration + final independent review |

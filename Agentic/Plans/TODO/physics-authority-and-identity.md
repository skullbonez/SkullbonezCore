# Physics Authority And Stable Identity

Date: 2026-07-09 (consolidated)
Status: In progress — ~55% complete
Impact area: physics, game object storage, scene system, replay, editor tools
Consolidates: `physics-game-model-authority-plan.md` (remaining phases),
`fable_plans/06-stable-identity` (C2–C5, Z1–Z4), and the PHYS-* rows from the
2026-07-07 overnight blocker ledger. Full completed-slice history lives in the
git history of those files.

## Goal

- `PhysicsBodyStore` owns all body state; `ColliderStore` owns all collision
  state; `RenderInstanceStore` owns render projection; scene/entity metadata
  owns names, grouping, and authored identity.
- `GameModelCollection` loses production physics/render authority;
  `GameModel` shrinks to presentation/contact metadata or disappears from hot
  paths.
- **One identity storage rule:** persistent state stores stable identity only
  (`PhysicsBodyHandle` for live bodies, `ReplayBodyId` for replay,
  `sceneObjectId` for authored identity). `modelIndex` is a frame-local cursor
  or an explicitly-typed `ModelRowHint` — never bare stored identity.

## Already done (summary — details in git history)

`MakePhysicsModelView()`/`PhysicsModelView` deleted; the model-index
`GameModelCollectionPhysicsAdapter` deleted; bulk post-step `GameModel`
writeback deleted; production render submission consumes
`RuntimeRenderModelFrameView` (store-backed), not the concrete collection; all
`AddGameModel()` appends require `PhysicsColliderCreateDesc`; contact-audio,
launcher, attached-camera, replay probes, and editor wake/sleep read from
store records; central identity resolvers exist and the C1 RuntimeTools
cluster stores handles; replay ids live on `PhysicsBodyStore` rows;
`RunSceneState` allocates `PhysicsSceneObjectId`s.

## Remaining work

### A. Body authority completion

- [ ] A1. `PhysicsBodyStore` owns sleep/wake state and accumulated
  forces/impulses as the single authority (audit remaining `GameModel`-side
  reads/writes; delete or route through store rows).
- [ ] A2. Production stepping consumes store views without `PhysicsModelAccess`
  forwarding to the collection — retire the facade once body/render/replay
  readers are store-native.
- [ ] A3. Single body registration path (creates entity/body mapping) and
  single deletion path (invalidates handles, removes rows deterministically).

### B. Collider authority completion

- [ ] B1. Broadphase reads bounds from `ColliderStore`; narrowphase reads exact
  shapes from `ColliderStore` (not `GameModel`), preserving persistent contact
  keys and filtering behavior.
- [ ] B2. Delete or mark remaining `GameModel` collider compatibility fields
  once B1's readers move.

### C. Scene/entity metadata split

- [ ] C1. Move object names/labels, grouping/hierarchy, and asset-instance
  identity out of `GameModel` into scene/entity metadata.
- [ ] C2. One coordinated creation path: scene load creates metadata, body,
  collider, and render records together; scene save serializes from
  authoritative stores.
- [ ] C3. `GameModelCollection.h` `rootModelIndex` rows → scene/entity grouping
  metadata (was fable-06 C5; gate `validate_physics`).

### D. Stable identity storage rule (was fable-06)

- [ ] D1. `RuntimeInteractionCommands.h` payloads carry `PhysicsBodyHandle`
  (capture at enqueue sites in `RunInput.cpp`). Gate: `validate_fast` +
  interaction proofs (`memory_overlay_f6_toggle`,
  `replay_branch_restore_live_edge`).
- [ ] D2. `RunState.h`, `RuntimeInteractionController.h`,
  `RuntimePickService.h` stored-index members → handles or `ModelRowHint`.
- [ ] D3. Replay cluster (`ReplayRuntime.h` hint members) → `ModelRowHint`;
  `targetId` stays authoritative. Do NOT convert recorded sample rows — they
  are replay data keyed by row-at-record-time; annotate `Lifetime:` only.
  Gate: `validate_full` + `prediction_ragdoll_wall_200_predict` proof.
- [ ] D4. Closure: delete ad hoc `modelIndex >= 0 && ... < modelCount`
  validation branches made redundant by resolvers; update the
  `RuntimeTools.h`/`ReplayRuntime.h` glossaries (deleting "validated before
  use" is the acceptance test). The old ratchet-budget step is superseded by
  engine-cleanup plan 03 (ratchets are being deleted, not lowered).

### E. Public physics API boundary

Completed via engine-cleanup Plan 14 on 2026-07-10; the plan file was deleted
per MASTER convention. FAC-005 is closed: `PhysicsApi.h`/`PhysicsEngine.h`
public signatures expose no `GameModel`, no raw dense `modelIndex`, and no
public `PhysicsEngine` solver-container accessors. Broader physics authority
work remains in sections A-D and the hard-blocker table below.

## Known hard blockers (from the 2026-07-07 overnight ledger)

The overnight machine deferred these as needing a human-awake ownership
design; they gate A2/A3/C2:

| Row | Knot |
|-----|------|
| PHYS-004/009/020 | `PhysicsEngine` is still owned by `GameModelCollection`; `GetPhysicsEngine` has ~124 references; promoting ownership needs a designed runtime physics owner. |
| PHYS-025/026/027 | Scene setup/reset still passes models + physics together; needs the scene creation pipeline split (owner-specific builders, explicit reset contracts). |
| PHYS-016/018 | `AppendGameModelAndPhysicsRows`/`Clear` are one collection-owned transaction; split follows the creation-path design. |
| PHYS-021/022 | `PhysicsWorld` hot scratch and contact/diagnostics coupling split follows PHYS-020. |
| PHYS-012 | Capacity policy relocation is cosmetic until the storage split exists. |

**Design decision needed before the next big slice:** who owns
`PhysicsEngine` at runtime (a `Run`-owned physics owner? `PhysicsScene`
promotion?). Decide once, then the blocked rows become mechanical.

## Acceptance (open items only)

- [ ] Body state authority lives in `PhysicsBodyStore`; collider authority in
  `ColliderStore`.
- [ ] Production stepping no longer requires `GameModelCollection&` or the
  `PhysicsModelAccess` forwarding facade.
- [ ] Scene/entity metadata is separate from simulation body state.
- [ ] No header stores a bare `int *ModelIndex*` member as identity.
- [ ] Replay capture and diagnostics use stable entity/body identity.

## Validation map

| Slice | Gate |
|-------|------|
| Body/collider/solver/determinism | `validate_physics` (byte-exact CSV) |
| Replay identity conversions | `validate_full` + prediction proof |
| Editor/tools conversions | `validate_fast` |
| Broad ownership moves | `validate_full` |

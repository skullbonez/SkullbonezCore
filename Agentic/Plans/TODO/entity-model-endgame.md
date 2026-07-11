# Entity Model Endgame

Date: 2026-07-11
Status: Not started — 0% (destination document; unblocks
`TODO/physics-authority-and-identity.md`)
Impact area: game object storage, physics ownership, scene system, runtime
shell, replay identity
Origin: 2026-07-11 architecture gap review. The store migration had no
written destination: the physics plan's goal hedged between `GameModel`
"shrinks or disappears", `GameModelCollection` still owns `PhysicsEngine`
(~124 `GetPhysicsEngine` references; blocker rows PHYS-004/009/020), and
identity is split across per-subsystem handles. The owner resolved all three
on 2026-07-11.

## Owner decisions (binding, 2026-07-11 — do not re-litigate)

1. **`GameModel` is deleted entirely.** The endgame representation of a
   world object is: rows in `PhysicsBodyStore`/`ColliderStore`/
   `RenderInstanceStore` (+ replay rows) plus one scene/entity metadata
   record for name, asset recipe, and grouping. No successor object bag.
2. **A new Run-owned `SimulationController` owns `PhysicsEngine`**, the
   fixed-step loop, and scene physics lifecycle. This is also the RUN-015
   answer in `TODO/runtime-shell-decomposition.md` — one decision, two
   plans unblocked.
3. **Unified `EntityId`.** One stable id per world object; a fixed-capacity
   registry maps `EntityId` → `PhysicsBodyHandle`, collider rows, render
   instance, `ReplayBodyId`, `sceneObjectId`. Per-subsystem handles remain
   the hot-path currency; `EntityId` is the cross-system and persistent
   name.

## Relationship to existing plans

- `TODO/physics-authority-and-identity.md` sections A–D remain the *how*
  for hot-path authority; this plan is the *where it ends*. Its blocker
  table (PHYS-004/009/020, PHYS-025/026/027, PHYS-016/018) becomes
  mechanical under decision 2 and the N1 registry.
- `TODO/runtime-shell-decomposition.md` C1/C2 (scene lifecycle) and RUN-015
  execute against decision 2. Coordinate so `SimulationController` is
  extracted once.
- `TODO/editor-undo-redo.md` U3 (delete/re-create identity) should key
  history entries on `EntityId` once N1 lands.
- Hot-path rules apply throughout: the registry is cold/cross-system
  plumbing; solver, broadphase, render submission keep dense rows and
  handles, never `EntityId` lookups inside hot loops.

## Phases

### N1 — EntityId registry

Fixed-capacity `EntityRegistry` (preallocated, generation-checked ids like
the existing handle pattern): allocate on the single creation path, resolve
to subsystem handles, invalidate on the single deletion path. This is the
same work as physics plan A3 (single registration/deletion path) — build
them together, not sequentially. Unit tests: allocate/resolve/stale-id/
capacity-exhaustion (exhaustion follows pool policy: assert Profile/Debug,
fatal Release with owner/capacity diagnostics).
Gate: `validate_tests` + `validate_physics` (creation-path adjacency).

### N2 — SimulationController extraction

New `Runtime` owner holding `PhysicsEngine`, the fixed-step accumulator
loop, and physics scene lifecycle (create/reset/teardown ordering). `Run`
keeps only frame-order calls into it. Migrate `GetPhysicsEngine` callers to
the controller (or better, to narrower store/query facets they actually
need — count each caller's real need before forwarding). Resolves
PHYS-004/009/020 and RUN-015; executes with runtime-shell C1/C2 where the
scene lifecycle overlaps.
Gate: `validate_physics` (byte-exact) per slice + `validate_full` for the
loop-ownership move; determinism proof is the acceptance evidence RUN-015
asked for.

### N3 — Cold-path GameModel evacuation

Move every remaining `GameModel` field to its owner: names/grouping/asset
identity → scene/entity metadata (physics plan C1), contact/presentation
metadata → domain records beside the stores, anything unread → deleted.
Inventory table maintained in this plan, one commit per field family.
Gate: per the file-to-validation map (`validate_physics` for body/state
fields, `validate_full` for scene metadata moves).

### N4 — Delete GameModel and GameModelCollection

Delete both classes and their creation/append transaction
(PHYS-016/018/025/026/027 close here via the N1 creation path). Acceptance
is structural: `git grep -l "GameModel"` returns only comments/history
references, and no compatibility spelling replaces the deleted shape (the
migration-cleanup review rule applies with full force on this slice).
Gate: `validate_full` + `validate_perf` (creation-path and frame-loop
adjacency) + rubber-duck review.

### N5 — Closure

Comment audit; SessionState/MASTER-PLAN updates; physics plan and
runtime-shell blocker tables updated to point here; delete plan on
completion.

## Acceptance

- [ ] No `GameModel`/`GameModelCollection` types in source; no renamed
      successor bag.
- [ ] `SimulationController` owns `PhysicsEngine` and the fixed-step loop;
      `GetPhysicsEngine` on the collection is gone.
- [ ] Every world object has an `EntityId` resolving to all subsystem
      handles; spawn/despawn maintain the registry through one path.
- [ ] Hot loops contain no `EntityId` resolution (review + existing
      hot-path rules).
- [ ] `physics_regression_solver.csv` byte-exact at every slice; DX12
      screenshots unchanged.

## Validation map

| Slice | Gate |
|-------|------|
| Registry + creation path | `validate_tests` + `validate_physics` |
| SimulationController / loop move | `validate_physics` + `validate_full` |
| Metadata moves | `validate_full` |
| Final deletion | `validate_full` + `validate_perf` |

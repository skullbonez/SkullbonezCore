# Entity Model Endgame

Date: 2026-07-11 (reconciled same day after the overnight completions and a
final owner ruling)
Status: Not started — 0/4
Impact area: game object storage, scene metadata, contact feedback, docs
Origin: 2026-07-11 architecture gap review, reconciled against the completed
`physics-authority-and-identity` work (16/16) and the owner's definitive
2026-07-11 rulings.

## Owner decisions (binding, 2026-07-11 — final; do not re-litigate)

Critical-path position: execute immediately after the completed
[instant-prediction closure](../../Reports/2026-07-11/instant-prediction-velocity-chaos-closure.md).
Close this plan before interpolation,
editor undo/redo, or another feature retains identity or scene APIs from
`GameModel`/`GameModelCollection`.

1. **`GameModel` and `GameModelCollection` are deleted entirely.** No
   successor object bag. A world object is: store rows
   (`PhysicsBodyStore`/`ColliderStore`/`RenderInstanceStore`/replay rows)
   plus scene/entity metadata. The completed physics work already reduced
   `GameModel` to transient contact feedback only, so this is now a small
   closure, not a migration.
2. **No `SimulationController` — definitive NO.** The implemented ownership
   stands: `SimulationSystem` owns fixed-step pacing, `SceneController`
   owns the scene-lifetime `PhysicsEngine` and step execution, `Run` keeps
   only top-level frame order. A `SimulationController` facade was also
   previously retired by cleanup plan 13; the name must not return.
3. **No unified `EntityId` registry — definitive NO.** Per-subsystem stable
   identity stands (`PhysicsBodyHandle` live bodies, `ReplayBodyId` replay,
   `PhysicsSceneObjectId` authored identity). Instead,
   **`PhysicsSceneObjectId` is promoted by rule to the engine's single
   cross-system object identity**: undo history, scene save, picking,
   logging, and any future cross-system feature key on it. A separate
   entity-id concept is reconsidered only if a future feature needs
   identity for objects with no scene presence.

## Remaining phases

- [ ] N1. **Identity rule documentation.** Record decision 3 where agents
      will hit it: the identity paragraph in `AGENTS.md`'s rules (one
      sentence), the `PhysicsSceneObjectId` declaration comment, and
      `Agentic/Reference/runtime-reference.md` if it discusses identity.
      Documentation-only; no validation.
- [ ] N2. **Contact-feedback relocation + `GameModel` deletion.** Move the
      remaining transient contact feedback to a store-adjacent value record
      owned by the contact/audio consumer that reads it; delete `GameModel`.
      Gate: `validate_physics` (byte-exact) + `validate_all_cpu_tests`.
- [ ] N3. **`GameModelCollection` closure.** It now only relays narrow
      body/collider/render creation commands; inventory those call sites,
      move them onto `SceneController`/store creation paths directly, and
      delete the class. The migration-cleanup review rule applies: no
      renamed compatibility spelling may replace it.
      Gate: `validate_full` + `validate_perf` (creation-path adjacency).
- [ ] N4. **Closure.** Structural proof (`git grep` shows no
      `GameModel`/`GameModelCollection` types in source), comment audit,
      single rubber-duck review, `validate_full`, SessionState/MASTER-PLAN
      updates, delete plan.

## Acceptance

- [ ] `AGENTS.md` names `PhysicsSceneObjectId` as the single cross-system
      object identity.
- [ ] No `GameModel`/`GameModelCollection` types remain; no successor bag.
- [ ] `physics_regression_solver.csv` byte-exact at every slice; DX12
      screenshots unchanged.

## Validation map

| Slice | Gate |
|-------|------|
| N1 docs | None (documentation-only) |
| N2 contact relocation | `validate_physics` + `validate_all_cpu_tests` |
| N3 collection deletion | `validate_full` + `validate_perf` |
| N4 closure | `validate_full` |

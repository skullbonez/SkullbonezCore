# Physics Boundary Rubber-Duck Review

Date: 2026-06-29

Expected outcome: the normal physics step should be driven by physics-owned
handles, stores, solver data, and narrow event/diagnostic interfaces. Runtime
scene objects may adapt data into those stores, but renderer/editor/scene UI
state must not be solver authority.

## Scope Reviewed

- `PhysicsWorld`, `PersistentContactSolver`, `PhysicsBodyStore`, `ColliderStore`
  integration, and `ObjectContactManifold` shape/material access.
- Runtime adapter paths for point joints, ragdoll constraints, authored scene
  setup, scene snapshots, and the standalone smoke command.
- Boundary guard evidence from `tools/check_runtime_boundaries.py` and the
  focused standalone/runtime smoke log.

## Findings

- Blocking: none found in the reviewed slice.
- Non-blocking: `PersistentContactSolver` still reads `GameModel` pose and
  transform state while resolving object contacts. Shape, restitution, mass,
  inertia, fixed/release policy, and runtime metadata now come from
  `PhysicsBodyStore`/`ColliderStore`, so this is a compatibility pose bridge
  rather than broad game-object solver authority. A later phase can move pose
  snapshots fully into physics-owned records if the project wants a stricter
  standalone kernel.

## Determinism And Lifetime Notes

- Body and collider records preserve the existing model-index order, so solver
  pair ordering remains validation-visible instead of being hidden behind a new
  unordered container.
- `DestroyBody` tombstones dependent colliders and constraints; the standalone
  smoke reports `lifecycle_checks=pass`.
- Runtime mirror evidence is explicit in the same smoke:
  `runtime_mirror_checks=pass`, `store_handles=pass`, `render_mirror=pass`, and
  `joint_handles=pass`.

## Validation State

- Focused build and smoke evidence exist for this slice.
- PR-gate validation is still pending until the whole handoff is ready:
  `tools\validate_physics.bat`, `tools\validate_full.bat`, and
  `tools\validate_perf.bat`.

# Angular Impulse Frame Correctness AI3 Convention Sweep

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/angular-impulse-frame-correctness.md`
Phase: AI3
Branch: `nightrunner-1st-AUG-26`

## Outcome

The investigation found no remaining direct production division of a
world-space torque by `PhysicsBodyRecord::rotationalInertia`. The two direct
`TryDivided(record.rotationalInertia, ...)` spellings are caller-supplied body-
diagonal operations inside the shared AI2 conversion. The contact solver uses
the same conversion with cached inverse inertia. Ragdoll retains a separate but
currently equivalent `R^T -> inverse diagonal -> R` implementation.

The sweep did find three convention defects and one public-contract gap. AI3
changes none of their behavior. The five-phase follow-up is registered at
`Agentic/Plans/TODO/vector-frame-contract-closure.md`.

## Findings And Baseline Blindness

| Finding | Implemented contract | Baseline assessment | Disposition |
|---|---|---|---|
| General angular-drag clamp | `angularDragTorque` and `hot.angularVelocity` are world-space, but lines 963-970 clamp their X/Y/Z components with body-principal `rotationalInertia.x/y/z` before `ApplyWorldImpulse` performs the correct world/body conversion. The wet sphere-only branch is safe because its inertia is isotropic. | A byte-exact artifact can faithfully lock the mixed-axis result whenever a rotated anisotropic scene activates the clamp. No focused cross-frame oracle currently distinguishes correctness from repeatability. | VF1 |
| Public Physics descriptors | Current consumers agree: shape offsets, joint anchors, and rotational inertia are body-local; orientation maps body to world; pose, linear/angular velocity, ray/AABB inputs, and hit point/normal are world-space. `PhysicsApi.h` does not state this matrix next to the fields. | Existing scenes exercise only a subset of plausible frame combinations. Exact bytes cannot prevent a future caller from supplying a wrong-frame value that happens to be deterministic. | VF0 |
| Authored `forcePosition` | Runtime consumption is a world-space offset from center of mass. The schema/parser names it as a position. Across 56 occurrences in 23 scenes, only `ragdoll_playground.scene.json::wake_ball` exactly copies its absolute position `(515, 28, 492)` into the field; the next-largest component magnitude is 10. | No validation script or mapped artifact references `ragdoll_playground`, so the outlier is completely invisible to committed baselines. | VF2 |
| `VectorReflect` | The formula `2 n dot(n, incident) - incident` preserves the normal component and reverses the tangent: reflection about the normal axis. Its comment uses incident/surface-normal vocabulary associated with plane reflection, and its only first-party caller is the unit test that pins the axis-mirror result. | Production physics, replay, and visual baselines cannot observe an unused inline helper. The unit test locks the current convention but does not resolve whether the name or formula is intended. | VF3 |

## Physics API Frame Matrix

| Value family | Frame confirmed from consumers |
|---|---|
| `PhysicsBodyCreateDesc` / `PhysicsBodyUpdateDesc` position, linear velocity, angular velocity | World |
| body orientation | Body-to-world rotation |
| collision-shape position/geometry | Body-local |
| rotational inertia | Body-principal diagonal; `usesWorldInertia` selects rotation through orientation for anisotropic shapes |
| point-joint anchors | Body-local for each referenced body; solver multiplies by each body's rotation |
| ray origin/direction and broadphase AABB min/max | World |
| ray hit point/normal | World |
| hull cumulative scale | Authoring/local geometry scale rather than a runtime coordinate frame |

No current producer/consumer disagreement was found in these descriptors after
AI2 renamed the pending impulse value as `worldApplicationOffset` /
`pendingImpulseWorldOffset`.

## Other Conversion Sites

- `Ragdoll.cpp::ApplyRecordInvInertia` is arithmetically frame-correct today but
  duplicates the shared helper's conversion. VF0 will either share the helper
  without changing operation order or record a concrete owner reason to retain
  the duplicate.
- buoyancy righting selects a body-local inertia/thickness axis and rotates the
  chosen axis into world space before building its torque; no mismatch found.
- terrain rolling friction intentionally uses one average-inertia scalar, so it
  is approximate but frame-neutral rather than an X/Y/Z frame mix.
- sphere spin damping multiplies world components by an isotropic diagonal;
  orientation cannot change the result.

## Method And Validation

CodeGraph was current and used first for the inertia/torque, `VectorReflect`,
and `PhysicsApi.h` call paths. Findings were then confirmed against the source,
all direct `rotationalInertia` arithmetic, every descriptor consumer, all 56
authored `forcePosition` values, and validator/fixture references.

AI3 is documentation-only. No repository validation is required for this phase;
the already-landed AI2 source remains covered by its Profile build, 453-case /
2,422,921-assertion unit gate, core Physics, and deep Physics results. No source,
scene, schema, config, or baseline artifact changed in AI3.

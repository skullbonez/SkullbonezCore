# Vector Frame Contract Closure — VF0 Frame Matrix

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Impact area: Physics public contracts, queries, and point-joint inertia response
Phase: VF0 complete

## Outcome

Every public vector, quaternion, and collision-shape field in `PhysicsApi.h`
now states its coordinate frame next to the field. Two focused rotated-body
oracles distinguish the documented contract from plausible wrong-frame input:
one covers shape offsets plus ray/AABB queries, and one covers point-joint
anchors plus the Ragdoll inverse-inertia path.

`Ragdoll.cpp::ApplyRecordInvInertia` no longer owns a separate anisotropic
`R^T -> inverse diagonal -> R` spelling. Its anisotropic branch delegates that
frame conversion to `TryApplyWorldInertiaResponse`; its infallible diagonal
multiply remains local, and its direct isotropic branch is unchanged.

The pre-change prediction for VF1-VF3 is **zero changed committed artifact
bytes**. No baseline refresh is authorized or expected.

## Public Frame Matrix

| Public value | Contract | Producer/consumer proof | Focused oracle |
|---|---|---|---|
| `PhysicsBodyCreateDesc::shape` | Geometry and center offset are body-local. | Authored/generated setup supplies local geometry; collider queries rotate the shape center by body orientation before adding body position. | Rotated sphere center appears at `(10, 24, 30)`, while the plausible unrotated center `(14, 20, 30)` is absent from the world AABB query. |
| `PhysicsBodyCreateDesc::position` | World-space center of mass. | Creation copies it directly into `PhysicsBodyHotState::position`; queries add rotated local shape offsets to it. | Stored state equals `(10, 20, 30)` and the rotated local offset produces the expected world query center. |
| `PhysicsBodyCreateDesc::orientation` | Body-to-world rotation. | Shape queries and Ragdoll anchors multiply local values by the orientation matrix. | A +90-degree Z rotation maps local `(4, 0, 0)` to world `(0, 4, 0)`. |
| `PhysicsBodyCreateDesc::linearVelocity` | World-space distance per second. | Creation copies it directly into the hot world-motion state. | The non-axis-equal value `(1, 2, 3)` survives registration exactly. |
| `PhysicsBodyCreateDesc::angularVelocity` | World-space radians per second. | World force and solver paths consume the hot value before rotating inertia operations into body axes. | The non-axis-equal value `(0.25, 0.5, 0.75)` survives registration exactly. |
| `PhysicsBodyCreateDesc::rotationalInertia` | Body-principal diagonal. | `usesWorldInertia` paths rotate world torque into body axes before applying the diagonal. | Rotated anisotropic point-joint response traverses the shared conversion. |
| `PhysicsBodyUpdateDesc` pose, velocities, and inertia | Same frames as the corresponding create fields. | Update masks replace the same hot/cold values used by creation. | The shared inline field contract prevents create/update vocabulary drift; the behavioral paths are the same registered state and query consumers. |
| `PhysicsColliderCreateDesc::shape` | Geometry and center offset are body-local. | Collider storage combines the shape with its owning body's world pose. | The ray and AABB results observe the rotated local sphere offset. |
| `PhysicsPointJoint{Create,Update}Desc::localAnchorA/B` | Each anchor is an offset from its referenced body's center in that body's local axes. | `Ragdoll::SolvePointJoints` multiplies each anchor by its body's rotation, then adds the world center. | Correct local anchors coincide and leave state unchanged; an absolute world point used as an anchor creates both linear and angular response. |
| `PhysicsRayCastDesc::origin` | World-space segment origin. | Ray traversal compares it with world collider bounds. | A ray from world `(10, 24, 20)` reaches the rotated collider. |
| `PhysicsRayCastDesc::direction` | World-space direction; normalization is internal. | `RayCast` normalizes before distance traversal. | Direction `(0, 0, 2)` reports the same world distance as a unit direction would. |
| `PhysicsRayCastDesc::maxDistance` | World-space segment length after direction normalization. | Candidate rejection compares normalized traversal distance with this value. | The 20-unit segment reports a 5.5-unit hit. |
| `PhysicsRayCastHit::point/normal/distance` | World-space point, unit outward world normal, and world distance from origin. | Query conversion publishes world collider intersection facts. | The rotated-offset sphere reports point `(10, 24, 25.5)`, normal `(0, 0, -1)`, and distance `5.5`. |
| `PhysicsBroadphaseCellQueryDesc::min/max` | World-space axis-aligned bounds. | Broadphase cell lookup consumes them without a body transform. | A world AABB at the rotated center returns the body; one at the unrotated local offset returns none. |

`MakeShareableHullShapeIdentity` also accepts a `Vector3 cumulativeScale`, but
that is a function parameter rather than a public descriptor field. It is an
authoring/local geometry scale encoded into identity bits, not a runtime
coordinate-frame value.

## Ragdoll Duplicate Recensus

The local helper has four operational uses:

- body A angular response in `ApplyConstraintImpulse`;
- body B angular response in `ApplyConstraintImpulse`;
- body A rotational term in point-joint effective mass; and
- body B rotational term in point-joint effective mass.

Before VF0 its anisotropic branch independently performed:

```text
body_value = R^T * world_value
body_response = inverse_inertia_body * body_value
world_response = R * body_response
```

VF0 preserves that operation order through `TryApplyWorldInertiaResponse`.
The local lambda remains an infallible component multiply by the cached inverse
body-principal diagonal. The `usesWorldInertia == false` branch still performs
its original direct `VectorMultiply`, so isotropic arithmetic and hot-path work
do not change.

## Pre-Change Artifact Prediction

| Future phase | Reachability assessment | Predicted committed artifact movement |
|---|---|---:|
| VF1 angular-drag clamp | The mapped regression scenes have zero effective angular-drag density for relevant bodies. `physics_bench_varied`, `physics_regression_solver`, `shooting_reaction_volley`, `three_body_chaos`, `stacking`, and `at_rest` set fluid density to zero; gas density defaults to zero. `terrain_contact_probe_debug` uses density `1.0`, but its fluid plane is at `-500` below the bodies, leaving the fluid contribution zero. The sphere-only wet-spin branch is frame-neutral. | 0 bytes |
| VF2 authored impulse-offset schema | The sole absolute-position outlier is `ragdoll_playground.scene.json::wake_ball`. No current validation script or committed artifact maps that scene. Renaming equivalent offsets elsewhere changes schema text but not generated committed physics output; the outlier correction changes only manual scene behavior. | 0 physics/replay/visual baseline bytes |
| VF3 `VectorReflect` | The helper has no first-party production caller; its only first-party caller is its unit test. | 0 production artifact bytes |

This prediction is a causal reachability ruling, not baseline-refresh authority.
Any mapped artifact delta during VF1 or VF2 blocks the phase for complete
inspection.

## Focused Tests

`SkullbonezTests/TestPhysicsApi.cpp` adds:

- `Physics API frames: body-local shape offsets project into world queries`;
- `Physics API frames: point-joint anchors are body-local rather than world positions`.

The Profile selection `--test-case=*Physics API frames*` passes 2 cases / 43
assertions. The first test independently observes stored pose/velocity,
body-local shape projection, normalized-ray distance, world hit facts, and
world AABB semantics. The second uses coincident rotated local anchors as the
zero-response oracle and a plausible absolute-world input as the wrong-frame
counterexample.

## Comment Audit

Checklist path: this report.

- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/Ragdoll.cpp`
- [x] `SkullbonezTests/TestPhysicsApi.cpp`

Checked: 3. Deferred: 0. Unchecked: none.

All three touched source files have ownership-bearing summaries, live
invariants, and resolving `Related:` paths. The new `Frame matrix` glossary
term is local to `PhysicsApi.h`; the strict glossary inventory reports 965
unique definitions, zero multi-file terms, zero drift, and zero diagnostics.
The Ragdoll comment accurately identifies the shared conversion owner and the
unchanged isotropic branch. No human-approved wording remains outstanding.

## Method And Validation

The current CodeGraph index (1,104 files, 33,951 nodes, 101,060 edges) was used
first for the API and Ragdoll call paths, then every relevant source field and
consumer was confirmed directly. The source census found no uncommented
vector, quaternion, or collision-shape field in the public API.

Completed for the VF0 commit:

- Profile build: pass;
- focused frame tests: 2 cases / 43 assertions, pass;
- strict glossary inventory: pass, zero diagnostics;
- formatting and project-filter validation: pass;
- build-configuration inventory: 1,653 compile rows, zero dropped
  inheritance, zero blocking diagnostics;
- `validate_fast`: 455 cases / 2,422,964 assertions, all pass; and
- `validate_physics`: lifecycle/runtime-handle smoke passes and both generated
  44,401-line varied-scene runs match the committed baseline byte-for-byte.

VF0 changes no intended simulation behavior and authorizes no golden update.

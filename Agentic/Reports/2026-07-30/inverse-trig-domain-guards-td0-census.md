# Inverse Trig Domain Guards — TD0 Census

Date: 2026-07-30
Base: `2e76dbed` (`maths-surface-reachability` complete)
Scope: first-party source under `SkullbonezSource/`

## Result

The post-deletion tree contains seven inverse-trig code calls: five `acosf`,
one `asinf`, and one `std::acos`. The eighth search hit is the historical
`acosf` mention in the `Matrix4::ShadowFromNormal` comment. The deleted
`GeometricMath::GetHeightFromPlane` site is absent.

The accompanying census contains 95 `sqrtf` calls, down from the plan's
pre-deletion count of 96, and three `atan2f` calls. Of the `sqrtf` calls, 80
have non-negative radicands by construction, 14 are protected by a dominating
domain guard, and one is open to caller-authored negative restitution. That
open site is documented below and is not silently changed by this inverse-trig
plan.

## Inverse-Trig Classification

| Site at base | TD0 classification | Reachability and ruling |
|---|---|---|
| `Maths/OrbitalMechanics.cpp:302` | Guarded by file-local `ClampUnit` | Shipping Maths path. Migrate to the shared spelling without changing the expression. |
| `Physics/Ragdoll.cpp:247` | Guarded by inline `std::clamp` | Shipping fixed-tick solver path. Migrate byte-exactly to the shared spelling. |
| `Runtime/Camera/AttachedCameraController.cpp:855` | Guarded by inline `std::clamp` | Shipping attach-camera path. Migrate byte-exactly to the shared spelling. |
| `Runtime/Camera/Camera.cpp:463` | Open | Reachable when a normalized view and up vector round to a dot greater than `1.0f`; `acosf` returns NaN and the cap returns the raw request. Clamp before `acosf`. |
| `Runtime/Camera/Camera.cpp:465` | Open | Same reachable failure at the opposite pole. Clamp before `acosf`. |
| `Maths/Matrix4.cpp:406` | Upper-pole guard only | The `acosf` implementation exists only under `_DEBUG`. `ShadowFromNormal` has no source callers at this base, but the fully inverted normal `(0,-1,0)` is a valid public input and divides by a zero axis magnitude in both Debug and shipping implementations. Handle the antiparallel pole explicitly. |
| `Runtime/Editor/EditorPlacementAssets.cpp:1774` | Explicitly guarded in the current tree | Contrary to the plan's preflight table, commit `ab24c368` already placed `std::clamp` on the dot, and the older antiparallel branch chooses world X before the zero-axis return. Preserve that behavior, migrate it to the shared spelling, and expose only the pure numerical policy for focused testing. |

The concrete Camera reproducer uses the finite vector
`(-398.8823547, -559.8487549, -648.9411621)`. Independent scalar-float
normalization produces a self-dot of `1.000000119f`. A camera whose negated
view and up use that vector reaches the failure through
`CameraCollection::RotatePrimary`.

`Matrix4::ShadowFromNormal` has no current caller according to both CodeGraph
and the exact symbol search. Its Debug branch performs the
`acosf -> degrees -> RotateAxis` reference calculation; the non-Debug branch
uses the fused Rodrigues form. The antiparallel zero-axis defect exists in
both branches. Matrix4 is therefore not a live physics call path, but this plan
still requires `validate_physics.bat` because the shared-spelling migration
touches the live `Ragdoll` solver.

## `sqrtf` Census

Legend:

- **P** — radicand is non-negative by construction: a sum/product of squares,
  a magnitude-squared value, or a previously produced square root.
- **G** — a dominating comparison or clamp proves the derived radicand is
  non-negative.
- **O** — open for a reachable finite input.

Every one of the 95 calls is listed below. Mixed rows identify exact lines by
classification.

| File | Lines and classification |
|---|---|
| `World/Terrain.cpp` | `1355` P |
| `Gameplay/TornadoGameplay.cpp` | `290` P |
| `Gameplay/TornadoField.cpp` | `57, 341` P |
| `Maths/Vector3.h` | `92, 107, 338, 356` P |
| `Maths/Quaternion.cpp` | `70, 91` P |
| `Maths/Matrix4.cpp` | `252, 403, 439` P |
| `Physics/ConvexHullShape.cpp` | `193, 1008, 1039` P; `203` G |
| `Physics/ContactSolverCommon.h` | `96` P |
| `Physics/BoundingSphere.cpp` | `117, 315, 355` P; `136, 326, 366` G |
| `Physics/PhysicsBodyStore.cpp` | `355, 741, 774, 986` P; `780, 877` G |
| `Physics/PhysicsDiagnosticsSink.cpp` | `237, 238` P |
| `Rendering/Text.cpp` | `494, 495` P; EDT arrays store squared distances |
| `Physics/BoundingBox.cpp` | `149, 215, 263, 303` P; `226, 274, 314` G |
| `Runtime/Editor/MousePickupTools.cpp` | `82, 167, 273` P |
| `Physics/PersistentContactSolver.cpp` | `232, 1139, 1320, 1566` P; `1032` O |
| `Physics/ObjectContactManifold.cpp` | `380, 477, 540, 615, 1288, 1303, 1503, 1651, 1875` P |
| `Runtime/Editor/LauncherTools.cpp` | `118, 183, 440, 441` P; `172` G |
| `Runtime/Editor/LauncherLaser.cpp` | `79, 310` P |
| `Runtime/Editor/EditorTracer.cpp` | `1170, 1172` P |
| `Physics/PhysicsEngine.cpp` | `244` G |
| `Runtime/Tools/RuntimeTools.cpp` | `664, 822, 835` P; `308` G |
| `Runtime/App/ReplayScrubberTools.cpp` | `204, 209` P |
| `Rendering/RenderInstanceRenderer.cpp` | `902` P |
| `Runtime/Editor/EditorInteractionTools.cpp` | `1877` P |
| `Physics/Ragdoll.cpp` | `148` P |
| `Runtime/Editor/EditorGizmoTools.cpp` | `220, 304` P |
| `Runtime/Startup/StartupProbeHarnesses.cpp` | `641` P |
| `Rendering/PrimitiveMeshBuilder.h` | `146, 266` P |
| `Physics/Stages/PhysicsForceStage.cpp` | `221, 312` P |
| `Physics/Stages/ExternalForceStage.cpp` | `66, 103, 220` P |
| `Runtime/Interaction/RuntimePickGeometry.cpp` | `76` G |
| `Runtime/Camera/AttachedCameraController.cpp` | `89, 837` P |
| `Runtime/Prediction/ReplayAuthoringCauseTree.cpp` | `67, 674` P |
| `Runtime/Prediction/ReplayPredictionPublication.cpp` | `194` P |
| `Runtime/Replay/ReplayAuthoringVelocity.cpp` | `465` P |
| `Runtime/Replay/ReplayPresentation.cpp` | `158` G |

### Open `sqrtf` Site

`PersistentContactSolver.cpp:1032` computes
`sqrtf(restitutionA * restitutionB)`. `PhysicsColliderCreateDesc::restitution`
is a public float copied directly into `PhysicsColliderRecord`, and authored
scene parsing reads restitution as a float without a non-negative range gate.
One negative and one positive restitution therefore reach a negative radicand
during an ordinary two-body contact. The state is runtime-achievable.

Changing contact-material policy is outside this plan's Camera + Maths
orientation ownership and would not be byte-exact. TD0 records the defect; it
does not disguise it with an unrelated clamp.

## `atan2f` Census

| Site | Classification |
|---|---|
| `Runtime/Camera/AttachedCameraController.cpp:856` | The distance fallback repairs a degenerate/non-finite offset before yaw capture. A vertical offset may pass `(0,0)` to `atan2f`, which MSVC defines as finite zero; no range restriction exists for other finite pairs. |
| `Runtime/Editor/EditorGizmoTools.cpp:308` | `radial` is normalized after a positive magnitude guard and projected onto an orthonormal plane basis, so both arguments cannot be zero for finite input. |
| `Runtime/Replay/ReplayAuthoringVelocity.cpp:469` | Same normalized-radial and orthonormal-basis proof as the editor gizmo path. |

## TD0 Decision

Proceed with one shared `Math::ClampUnit`, explicit Camera zero-up fallback,
explicit Matrix4 antiparallel handling, and a separately testable pure editor
terrain-orientation policy. Preserve all in-domain results and require the
physics byte-exact gate because `Ragdoll.cpp` is a live solver input even
though the Matrix4 site itself is not physics-reachable.

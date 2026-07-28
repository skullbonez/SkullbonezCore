# Vector Dot-Product API VD0 Census

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/TODO/vector-dot-product-api.md`
Scope: documentation-only inventory; no repository validation required

## Result

The tracked first-party tree contains one owned vector type,
`Math::Vector::Vector3`; there is no owned `Vector2` or `Vector4` source API.
The type-aware census found **171** calls to
`Vector3::operator*( const Vector3& )` in **34** tracked files:

| Area | Calls | Disposition |
|---|---:|---|
| Physics | 96 | Byte-exact Physics-sensitive; migrate one expression at a time |
| Runtime | 56 | Interaction, editor, Replay, and presentation math |
| Maths | 10 | Shared primitives and the existing file-local `Dot` adapter |
| Gameplay | 1 | Squared-distance test |
| Tests | 8 | Direct dot-product assertions |
| **Total** | **171** | All are true dot products |

Of these, 163 are production calls and 8 are test calls. There is one
vector-vector overload definition at `SkullbonezSource/Maths/Vector3.h:308-311`.
Its arithmetic order is:

```cpp
return x * v.x + y * v.y + z * v.z;
```

VD1 must preserve that exact multiply/add association in the new named API.

## Method And Exclusions

- `git ls-files` supplied the tracked inventory, including tracked files under
  names that ordinary ignored-directory searches could miss.
- CodeGraph first mapped `Vector3`, the overload definitions, the existing
  `Dot` helper, and their blast radius.
- A Clang AST matcher selected only `CXXOperatorCallExpr` nodes whose resolved
  callee is the `float`-returning `Vector3` member overload. The compile
  database covered all 309 tracked first-party C++ translation units; 211
  translation units with transitive `Vector3.h` reachability were parsed.
- The 417 raw matches include repeated inline-header instantiations. Canonical
  tracked path plus line and column deduplication produces the 171 rows below.
- An independent sweep of all 309 tracked first-party translation units, with
  the Clang-only narrowing diagnostic suppressed, produced 425 raw instantiated
  hits, zero diagnostics, and the same 171 canonical calls across the same 34
  files.
- A supplementary standalone sweep of all 145 first-party headers with
  transitive `Vector3.h` reachability found 8 of the 10 canonical header rows
  and zero additional header-only calls. The two absent rows are the dependent
  template expressions at `ContactSolverCommon.h:134-135`; they instantiate
  only when normal translation units supply the inverse-inertia callables, and
  both the primary and independent translation-unit sweeps capture them.
  Fifteen isolated-header diagnostics came from two headers that require owner
  include context; both are parsed through their normal translation units, and
  all four TerrainSupportClassifier dot rows are present below.
- `ReplayV2Artifact.cpp` emitted 11 Clang-only narrowing diagnostics under the
  MSVC-derived command. A repeat with `-Wno-c++11-narrowing` produced the same
  match ordering and no census delta; that file owns no vector dot call.
- `ThirdPtySource/**` is third-party and excluded. The generated shader
  reflection/data artifacts do not define or call the owned vector API.
  First-party files named `SceneGenerated*` remained in scope.

The type filter deliberately excludes these unrelated multiplication contracts:

- `Vector3::operator*( float )` and free `operator*( float, Vector3 )`: scalar
  scaling, retained.
- `VectorMultiply( Vector3, Vector3 )`: explicit component-wise multiplication,
  retained.
- `RotationMatrix::operator*( Vector3 )`: matrix transform, retained.
- `Matrix4::operator*( Matrix4 )`: matrix composition, retained.
- `Quaternion::operator*( Quaternion )`: quaternion composition, retained.
- `Ray::operator*( float )` and `Camera::operator*( float )`: unrelated scalar
  scaling, retained.

## Named API Ownership

The only existing `Dot` spelling is a file-local helper at
`SkullbonezSource/Maths/OrbitalMechanics.cpp:55-58`. It delegates to `a * b`;
it is not a shared Maths contract. Existing shared vector operations
(`CrossProduct`, `VectorMag`, `VectorMagSquared`, `VectorReflect`) are inline
free functions in `Math::Vector`.

VD1 should therefore add:

```cpp
inline float Dot( const Vector3& lhs, const Vector3& rhs )
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}
```

to `Math::Vector` in `Vector3.h`, delete the OrbitalMechanics-local adapter,
import/use the shared spelling there, migrate every row below, and delete
`Vector3::operator*( const Vector3& )` in the same campaign. No compatibility
operator, macro, or forwarding wrapper remains.

For every census entry below, the intended replacement is exactly
`Dot(<left operand>, <right operand>)`, copying both printed operand
subexpressions and their order unchanged. No row authorizes reassociation,
operand swapping, common-subexpression extraction, temporary aggregation,
precision promotion, loop/SIMD substitution, or fused multiply-add.

The few operands that contain calls (`CrossProduct`, inverse-inertia
application, rotation transforms, and `EditorAxisVector`) are pure value
expressions. No matched operand contains assignment, increment/decrement,
queue consumption, or another observable side effect. The named free call
therefore introduces no sequencing dependency, but VD1 must still preserve
left/right order exactly.

## Exact Migration Surface

- `SkullbonezSource/Gameplay/TornadoField.cpp` (1)
  - `329:38` — `delta * delta`
- `SkullbonezSource/Maths/Frustum.cpp` (1)
  - `84:38` — `plane.normal * center`
- `SkullbonezSource/Maths/GeometricMath.cpp` (4)
  - `83:24` — `triangle.v1 * plane.m_normal`
  - `101:14` — `plane.m_normal * point`
  - `217:25` — `plane.m_normal * ray.vector3`
  - `225:19` — `plane.m_normal * ray.origin`
- `SkullbonezSource/Maths/Matrix4.cpp` (3)
  - `197:23` — `s * eye`
  - `198:23` — `u * eye`
  - `199:22` — `f * eye`
- `SkullbonezSource/Maths/OrbitalMechanics.cpp` (1)
  - `57:12` — `a * b`
- `SkullbonezSource/Maths/Vector3.h` (1)
  - `324:29` — `normal * incident`
- `SkullbonezSource/Physics/BoundingBox.cpp` (3)
  - `218:25` — `d * moveDir`
  - `266:25` — `d * moveDir`
  - `306:25` — `d * moveDir`
- `SkullbonezSource/Physics/BoundingSphere.cpp` (5)
  - `87:32` — `relativeMovement * relativeMovement`
  - `96:30` — `difference * difference`
  - `108:24` — `difference * relativeMovement`
  - `318:25` — `d * moveDir`
  - `358:25` — `d * moveDir`
- `SkullbonezSource/Physics/ContactSolverCommon.h` (4)
  - `70:28` — `tangent1 * normal`
  - `117:31` — `axis * Math::Vector::CrossProduct( invInertiaTerm, r )`
  - `134:43` — `axis * Math::Vector::CrossProduct( applyInvInertiaA( rAxAxis ), rA )`
  - `135:21` — `axis * Math::Vector::CrossProduct( applyInvInertiaB( rBxAxis ), rB )`
- `SkullbonezSource/Physics/ConvexHullShape.cpp` (3)
  - `195:31` — `d * moveDir`
  - `1041:16` — `normal * ( centroid - a )`
  - `1047:33` — `normal * a`
- `SkullbonezSource/Physics/ObjectContactManifold.cpp` (40)
  - `374:25` — `d * tangent0`
  - `375:25` — `d * tangent1`
  - `576:39` — `box.axes[0] * axis`
  - `576:89` — `box.axes[1] * axis`
  - `577:39` — `box.axes[2] * axis`
  - `597:29` — `centerDelta * axis`
  - `623:25` — `centerDelta * axis`
  - `711:22` — `( prev.point - planePoint ) * inwardNormal`
  - `717:25` — `( cur.point - planePoint ) * inwardNormal`
  - `813:21` — `incidentBox.axes[axis] * refNormal`
  - `826:29` — `incidentBox.axes[axis] * refNormal`
  - `861:23` — `refBox.axes[referenceAxis] * refNormal`
  - `888:28` — `( clipped[i].point - refFaceCenter ) * refNormal`
  - `932:19` — `box.axes[side0] * towardNormal`
  - `933:19` — `box.axes[side1] * towardNormal`
  - `960:15` — `d1 * d1`
  - `961:15` — `d2 * d2`
  - `962:15` — `d2 * r`
  - `979:19` — `d1 * r`
  - `987:23` — `d1 * d2`
  - `1114:24` — `normal * poly.vertices[indices[0]]`
  - `1226:28` — `face.normal * out.vertices[out.faceIndices[face.firstIndex]]`
  - `1249:25` — `poly.vertices[i] * axis`
  - `1264:12` — `poly.faces[edge.faceA].normal * axis`
  - `1265:12` — `poly.faces[edge.faceB].normal * axis`
  - `1327:25` — `centerDelta * axis`
  - `1409:22` — `( prev.point - planePoint ) * inwardNormal`
  - `1415:25` — `( cur.point - planePoint ) * inwardNormal`
  - `1452:27` — `incident.faces[f].normal * refNormal`
  - `1471:27` — `reference.faces[f].normal * refNormal`
  - `1508:14` — `( faceCenter - a ) * inward`
  - `1513:14` — `( point - a ) * inward`
  - `1533:26` — `( p - a ) * ab`
  - `1571:40` — `face.normal * sphereCenter`
  - `1656:14` — `( refCenter - a ) * inward`
  - `1680:34` — `( workA[i].point - refPlanePoint ) * refNormal`
  - `1716:26` — `polyA.faces[faceA].normal * finalNormal`
  - `1717:26` — `polyB.faces[faceB].normal * -finalNormal`
  - `1840:40` — `hullWorld.faces[f].normal * sphereCenter`
  - `1858:40` — `hullWorld.faces[f].normal * closestPoint`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp` (21)
  - `582:31` — `body.linearVelocity * body.linearVelocity`
  - `583:31` — `body.angularVelocity * body.angularVelocity`
  - `592:62` — `supportArm * supportNormal`
  - `627:33` — `axis * axis`
  - `634:33` — `body.angularVelocity * axis`
  - `743:42` — `pointDelta * manifold.normal`
  - `879:55` — `( rotation * Vector3( 1.F, 0.F, 0.F ) ) * supportNormal`
  - `880:55` — `( rotation * Vector3( 0.F, 1.F, 0.F ) ) * supportNormal`
  - `881:55` — `( rotation * Vector3( 0.F, 0.F, 1.F ) ) * supportNormal`
  - `1154:24` — `( velB - velA ) * c.normal`
  - `1303:41` — `relVel * c.normal`
  - `1305:38` — `relVel * c.tangent1`
  - `1306:38` — `relVel * c.tangent2`
  - `1378:28` — `( velB - velA ) * c.normal`
  - `1393:29` — `( velB - velA ) * c.tangent1`
  - `1394:29` — `( velB - velA ) * c.tangent2`
  - `1490:32` — `body.angularVelocity * body.angularVelocity`
  - `1545:20` — `body.linearVelocity * body.linearVelocity`
  - `1546:20` — `body.angularVelocity * body.angularVelocity`
  - `1801:58` — `otherVelocity * releaseDir`
  - `1804:70` — `otherVelocity * releaseDir`
- `SkullbonezSource/Physics/PhysicsEngine.cpp` (2)
  - `228:39` — `originToCenter * rayDirection`
  - `229:34` — `originToCenter * originToCenter`
- `SkullbonezSource/Physics/Ragdoll.cpp` (3)
  - `141:25` — `value * value`
  - `231:39` — `headUp * torsoUp`
  - `452:34` — `( velB - velA ) * axis`
- `SkullbonezSource/Physics/SolverBroadphaseStage.h` (1)
  - `116:18` — `relativeStart * relativeDisplacement`
- `SkullbonezSource/Physics/SpatialGrid.cpp` (1)
  - `917:30` — `displacement * displacement`
- `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp` (3)
  - `95:31` — `value * value`
  - `260:46` — `velocity * tangent`
  - `313:32` — `sample * sample`
- `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` (1)
  - `169:18` — `relativeStart * relativeDisplacement`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp` (1)
  - `394:16` — `delta * delta`
- `SkullbonezSource/Physics/TerrainContactManifold.cpp` (4)
  - `448:61` — `contactWorldPos * planeNormal`
  - `471:57` — `worldVerts[v] * planeNormal`
  - `514:57` — `worldVerts[v] * planeNormal`
  - `554:25` — `body.linearVelocity * planeNormal`
- `SkullbonezSource/Physics/TerrainSupportClassifier.h` (4)
  - `123:28` — `axisX * terrainNormal`
  - `124:34` — `axisY * terrainNormal`
  - `131:34` — `axisZ * terrainNormal`
  - `242:37` — `worldNormal * terrainNormal`
- `SkullbonezSource/Runtime/Camera/Camera.cpp` (2)
  - `463:35` — `vNegatedView * this->m_upVector`
  - `465:37` — `vNegatedView * -this->m_upVector`
- `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp` (17)
  - `132:29` — `normal * rayDirection`
  - `139:30` — `normal * ( origin - rayOrigin )`
  - `148:69` — `radial * normal`
  - `184:21` — `axisVector * rayDirection`
  - `185:21` — `axisVector * w`
  - `186:21` — `rayDirection * w`
  - `205:52` — `rayDirection * axisVector`
  - `212:44` — `fallback * axisVector`
  - `241:25` — `rayDirection * planeNormal`
  - `248:26` — `( planeOrigin - rayOrigin ) * planeNormal`
  - `256:16` — `( hitPoint - planeOrigin ) * EditorAxisVector( axis )`
  - `282:25` — `normal * rayDirection`
  - `289:26` — `normal * ( origin - rayOrigin )`
  - `297:26` — `radial * normal`
  - `298:31` — `radial * radial`
  - `309:24` — `radial * basisB`
  - `309:41` — `radial * basisA`
- `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` (6)
  - `907:32` — `segment * segment`
  - `912:46` — `toPoint * rayDirection`
  - `917:21` — `rayDirection * rayDirection`
  - `918:21` — `rayDirection * segment`
  - `920:21` — `rayDirection * w0`
  - `921:21` — `segment * w0`
- `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp` (1)
  - `1748:35` — `up * terrainNormal`
- `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp` (3)
  - `87:29` — `clampedRayDirection * cameraNormal`
  - `94:32` — `( this->m_mousePickup.planePoint - clampedRayOrigin ) * cameraNormal`
  - `170:39` — `( grabPoint - cameraEye ) * cameraNormal`
- `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp` (4)
  - `61:21` — `m * rayDirection`
  - `62:23` — `m * m`
  - `142:59` — `face.normalLocal * localOrigin`
  - `143:35` — `face.normalLocal * localDirection`
- `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp` (1)
  - `51:12` — `value * value`
- `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp` (18)
  - `211:32` — `segment * segment`
  - `216:46` — `toPoint * rayDirection`
  - `221:21` — `rayDirection * rayDirection`
  - `222:21` — `rayDirection * segment`
  - `224:21` — `rayDirection * w0`
  - `225:21` — `segment * w0`
  - `369:29` — `normal * rayDirection`
  - `376:30` — `normal * ( origin - rayOrigin )`
  - `390:69` — `radial * normal`
  - `416:21` — `axisVector * rayDirection`
  - `417:21` — `axisVector * w`
  - `418:21` — `rayDirection * w`
  - `442:25` — `normal * rayDirection`
  - `449:26` — `normal * ( origin - rayOrigin )`
  - `457:26` — `radial * normal`
  - `458:31` — `radial * radial`
  - `469:24` — `radial * basisB`
  - `469:41` — `radial * basisA`
- `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp` (2)
  - `143:33` — `offset * rayDirection`
  - `144:36` — `offset * offset`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` (2)
  - `293:21` — `m * rayDirection`
  - `294:23` — `m * m`
- `SkullbonezTests/TestOrbitalMechanics.cpp` (1)
  - `157:23` — `miss * miss`
- `SkullbonezTests/TestQuaternion.cpp` (3)
  - `197:23` — `x * y`
  - `198:23` — `x * z`
  - `199:23` — `y * z`
- `SkullbonezTests/TestVector3.cpp` (4)
  - `94:14` — `xAxis * yAxis`
  - `95:14` — `xAxis * xAxis`
  - `190:12` — `reflected * normal`
  - `190:51` — `incident * normal`

## Determinism And Test Map

All 96 calls under `SkullbonezSource/Physics/**` are byte-exact
Physics-sensitive. In particular:

- `ContactSolverCommon.h` and `PersistentContactSolver.cpp` feed effective
  mass, relative velocity, restitution, friction, sleep, and release decisions.
- `ObjectContactManifold.cpp`, `TerrainContactManifold.cpp`,
  `TerrainSupportClassifier.h`, and the bounds/hull files feed SAT, clipping,
  separation, support, and time-of-impact branches.
- broadphase, spatial-grid, external-force, wake, and ragdoll calls feed
  ordering, activation, and solver work.

VD1 may change spelling only. VD2 must run the plan's byte-exact Physics gate
from final source and must not refresh its baseline.

Existing direct tests that themselves migrate are:

- `TestVector3.cpp`: orthogonal/self dot identities and reflected/incident
  normal components.
- `TestQuaternion.cpp`: rotated basis orthogonality.
- `TestOrbitalMechanics.cpp`: Lambert miss-distance squared.

Behavioral coverage for the production surface also lives in `TestBounds.cpp`,
`TestConvexHull.cpp`, `TestObjectContactManifold.cpp`,
`TestPersistentContactSolver.cpp`, `TestPhysicsStageState.cpp`,
`TestSpatialGrid.cpp`, `TestTerrain.cpp`, `TestDeterminism.cpp`, and the Runtime
Replay/editor tests. VD2 should make `TestVector3.cpp` name the new shared
`Dot` API directly, add an order-sensitive mixed-sign value if needed, run a
source proof that the vector-vector overload and ambiguous calls are absent,
then run `validate_tests`, `validate_physics`, `validate_perf`, and
`validate_full`.

## Warm-Start Working-Tree Constraint

`PersistentContactSolver.cpp` contains 21 migration rows. The protected
owner-review working-tree diff is confined to current lines 257-279; it does
not overlap any dot row (the first is current line 582). VD1 can edit the dot
rows, but its commit must use partial staging so the warm-start hunks remain
unstaged and uncommitted. The other protected warm files contain no matched
vector dot call.

## Questions

No owner input is required for VD1. Every expression's mathematical intent is
established by its resolved overload, local call path, and tests.

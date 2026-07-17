# Math Fatal Call-Site Survey

Date: 2026-07-15
Plan: `math-fatal-survey-restoration` T1-T2
Surveyed commit: `6c5b4d3ead09419849e862e9e64005e4e55715cf`

## Method

The survey was regenerated from the current tree, not reconstructed from the
round-4 commit message. CodeGraph was current at 519 files, 16,058 nodes, and
45,751 edges and provided the initial API/caller map. The map was reconciled
against these mechanical source searches:

```text
rg -n --glob '*.cpp' --glob '*.h' --glob '*.hpp' --glob '*.inl' \
  '\.(Normalise|TryNormalise|TryNormalised|TryDivided|TryDivide)\s*\(' SkullbonezSource
rg -n --glob '*.cpp' --glob '*.h' --glob '*.hpp' --glob '*.inl' \
  '\bNormalise\s*\(' SkullbonezSource
rg -n --glob '*.cpp' --glob '*.h' --glob '*.hpp' --glob '*.inl' '/=' SkullbonezSource
rg -n --glob '*.cpp' --glob '*.h' --glob '*.hpp' --glob '*.inl' '[^/] / [^/]' SkullbonezSource
```

The division results were type-checked against each file's `Vector3`
declarations and surrounding guards. Scalar arithmetic, path concatenation,
comments, and the definitions inside `Vector3.h` are not call sites. The
implementation call at `Vector3.h:112` is recorded here but excluded from the
engine-caller totals. Quaternion `Normalise` calls are enumerated separately:
they share the spelling, but `Quaternion::Normalise` already has an identity
fallback and was never part of the Vector3 fatal contract.

## Result

- 23 external Vector3 named-API calls: 11 guarded plain normalizations and 12
  reachable-degenerate Try calls.
- Zero external calls to `TryNormalised` or mutating `TryDivide`; four calls use
  `TryDivided`.
- 52 Vector3 `/` or `/=` calls, all protected by a positive magnitude/count/time
  guard or by a row-specific owner invariant below.
- 24 Quaternion `Normalise` calls, all out of the Vector3 contract and protected
  by Quaternion's deterministic identity fallback.
- Zero reachable-degenerate Vector3 rows remain on a plain API without a
  specific proof. T2 therefore changes no source.

## Vector3 normalization and Try-API calls

| File:line | Call | Classification | Disposition / specific reason |
|---|---|---|---|
| `Maths/GeometricMath.cpp:57` | `m_normal.TryNormalise()` | Reachable-degenerate | Degenerate triangle edges return `ZERO_VECTOR`, allowing the query to miss deterministically. |
| `Maths/Matrix4.cpp:156` | `f.Normalise()` | Invariant-safe plain API | The immediately preceding `VectorMag(f) < 1e-6f` branch returns identity for a coincident eye and target. |
| `Maths/Matrix4.cpp:159` | `u.TryNormalise()` | Reachable-degenerate | A zero authored up vector becomes world +Y. |
| `Maths/Matrix4.cpp:173` | `s.Normalise()` | Invariant-safe plain API | A near-zero first cross product is rebuilt from a basis selected to be nonparallel to normalized `f`. |
| `Physics/PhysicsBodyStore.cpp:382` | `velocityVector.Normalise()` | Invariant-safe plain API | `IsCloseToZero()` returns `ZERO_VECTOR` before normalization; remaining components produce nonzero magnitude squared. |
| `Physics/PhysicsBodyStore.cpp:750` | `worldImpulse.TryDivided(mass, ...)` | Reachable-degenerate | Invalid zero mass consumes the linear impulse; no partial velocity write escapes. |
| `Physics/PhysicsBodyStore.cpp:757` | `local impulse.TryDivided(rotationalInertia, ...)` | Reachable-degenerate | Any zero inertia component consumes the angular impulse atomically. |
| `Physics/PhysicsBodyStore.cpp:773` | `pendingImpulse.TryDivided(mass, ...)` | Reachable-degenerate | Invalid zero mass consumes the pending linear impulse before the pending record is cleared. |
| `Physics/PhysicsBodyStore.cpp:779` | `torque.TryDivided(rotationalInertia, ...)` | Reachable-degenerate | Any zero inertia component consumes the pending angular impulse atomically. |
| `Physics/Ragdoll.cpp:237` | `torsoUp.Normalise()` | Invariant-safe plain API | A rotation matrix applied to authored world +Y preserves a nonzero basis direction. |
| `Physics/Ragdoll.cpp:238` | `headUp.Normalise()` | Invariant-safe plain API | Same rotation-basis proof as the torso row, for the head body. |
| `Physics/Ragdoll.cpp:251` | `correctionAxis.Normalise()` | Invariant-safe plain API | A small cross product is replaced with the torso rotation's world +X basis before this call. |
| `Runtime/Camera.cpp:60` | `m_upVector.TryNormalise()` | Reachable-degenerate | Zero authored up remains the explicit zero sentinel; downstream basis queries own their fallback. |
| `Runtime/Camera.cpp:457` | `vRight.TryNormalise()` | Reachable-degenerate | Coincident/parallel camera axes use world +X. |
| `Runtime/Camera.cpp:522` | `vView.TryNormalise()` | Reachable-degenerate | A camera looking at its own position uses conventional forward `(0,0,-1)`. |
| `Runtime/CameraCollection.cpp:273` | `selectedCamera.up.TryNormalise()` | Reachable-degenerate | Zero editor/external input uses world +Y. |
| `Runtime/CameraCollection.cpp:481` | `tweenCamera.up.TryNormalise()` | Reachable-degenerate | Opposed tween endpoints may cancel; the midpoint uses world +Y. |
| `Runtime/Render/RuntimeRenderPasses.cpp:294` | `value.Normalise()` | Invariant-safe plain API | The normalization helper returns its caller-supplied fallback when magnitude squared is at most `1e-8`. |
| `Runtime/Render/RuntimeRenderPasses.cpp:359` | `lightDirectionWorld.Normalise()` | Invariant-safe plain API | A direction shorter than `1e-5` is replaced with a fixed nonzero sun direction. |
| `Runtime/RuntimeTuning.cpp:252` | `direction.Normalise()` | Invariant-safe plain API | The spherical construction has unit-length algebra for every clamped azimuth/elevation. |
| `World/Terrain.cpp:146` | `m_flatSlopeNormal.Normalise()` | Invariant-safe plain API | The constructed vector is `(-slopeX, 1, -slopeZ)`, so its Y component is always one. |
| `World/Terrain.cpp:559` | `normal.Normalise()` | Invariant-safe plain API | The cross-product magnitude-squared guard returns world +Y before this call. |
| `World/Terrain.cpp:1289` | `post.normal.TryNormalise()` | Reachable-degenerate | A fully cancelling/degenerate neighborhood uses world +Y. |

`Vector3.h:112` calls `candidate.TryNormalise()` inside the implementation of
`TryNormalised`; it is not an external call site and its false result leaves the
out parameter untouched.

### U2 reachable-degeneracy verification matrix (2026-07-16)

The survey's 12 reachable-degenerate rows include the four `TryDivided`
call sites; they are not 16 distinct rows. The Physics case below drives all
four production calls through immediate world-force and pending-impulse owner
paths. Each test names this survey in its learning header or local related
context, making the mapping searchable in both directions.

| Reachable-degenerate production row | Named behavioral test |
|---|---|
| `GeometricMath::ComputePlane` degenerate triangle | `GeometricMath: a degenerate triangle produces the zero plane fallback` |
| `Matrix4::LookAt` zero authored up | `Matrix4: coincident look-at falls back to identity and zero up falls back to world Y` |
| Immediate world impulse / zero mass | `Physics impulses: zero mass and inertia absorb immediate and pending components` |
| Immediate world torque / zero inertia | `Physics impulses: zero mass and inertia absorb immediate and pending components` |
| Pending impulse / zero mass | `Physics impulses: zero mass and inertia absorb immediate and pending components` |
| Pending torque / zero inertia | `Physics impulses: zero mass and inertia absorb immediate and pending components` |
| `Camera::SetAll` zero authored up | `Camera: authored zero up remains the SetAll sentinel` |
| `Camera::GetRightVector` coincident/parallel axes | `Camera: parallel view and up axes move right along world plus X` |
| `Camera::GetViewVectorNormalised` coincident eye/view | `Camera: coincident eye and target move forward along world minus Z` |
| `CameraCollection::SetPrimaryUp` zero input | `CameraCollection: SetPrimaryUp repairs zero input to world up` |
| `CameraCollection::SetCamera` opposed tween up vectors | `CameraCollection: opposed tween up vectors cancel to world up` |
| `Terrain::GenerateNormals` collapsed neighborhood | `Terrain: collapsed height-map posts publish world-up render normals` |

Negative-space verification is recorded by
`Vector3: plain zero normalise asserts in Debug and propagates IEEE values otherwise`.
The Profile/Release branch executes the representative divide-by-zero and
asserts non-finite IEEE propagation. The Debug contract was manually verified
at source level on 2026-07-16: `Vector3::Normalise` retains its nonzero-magnitude
CRT assertion. The Debug test branch deliberately does not trigger the dialog
inside doctest because the harness cannot intercept it safely.

## Vector3 division operators

| File:line | Expression | Classification | Disposition / specific reason |
|---|---|---|---|
| `Physics/BoundingBox.cpp:208,252,290` | `totalMovement / totalMovementMag` | Invariant-safe plain API | Each swept test returns early when `totalMovementSq` is below its collision tolerance, so the square root is positive. |
| `Physics/BoundingSphere.cpp:291,329` | `totalMovement / totalMovementMag` | Invariant-safe plain API | Each sphere sweep has the same preceding nonzero movement-squared branch. |
| `Physics/ContactSolverCommon.h:73` | `tangent1 /= tangentMag` | Invariant-safe plain API | The tangent basis helper replaces a short first tangent; the cross product with its chosen nonparallel basis has positive magnitude. |
| `Physics/ConvexHullShape.cpp:122` | `v / sqrtf(magSq)` | Invariant-safe plain API | The helper returns its supplied fallback when `magSq <= TOLERANCE²`. |
| `Physics/ConvexHullShape.cpp:199` | `totalMovement / totalMovementMag` | Invariant-safe plain API | The hull sweep returns before division when movement squared is within tolerance. |
| `Physics/ConvexHullShape.cpp:915` | `centroid /= m_vertexCount` | Invariant-safe plain API | A hull reaching mass-property calculation has passed the nonempty baked-vertex validation. |
| `Physics/ObjectContactManifold.cpp:341` | `tangent0 /= sqrtf(tangent0MagSq)` | Invariant-safe plain API | The branch executes only when `tangent0MagSq > TOLERANCE²`; otherwise a fixed tangent basis is selected. |
| `Physics/ObjectContactManifold.cpp:432` | `delta / dist` | Invariant-safe plain API | The conditional expression divides only when `dist > TOLERANCE`. |
| `Physics/ObjectContactManifold.cpp:501` | `boxToSphere / dist` | Invariant-safe plain API | The enclosing branch is `dist > TOLERANCE`; the inside-box case chooses a face normal. |
| `Physics/ObjectContactManifold.cpp:575,1242,1262` | `axisRaw / sqrtf(magSq)` | Invariant-safe plain API | Each SAT/axis helper rejects an axis whose squared magnitude is at or below its local epsilon before division. |
| `Physics/ObjectContactManifold.cpp:1441` | `faceCenter /= face.indexCount` | Invariant-safe plain API | The selected hull face is validated to contain at least three indices before clipping. |
| `Physics/ObjectContactManifold.cpp:1453,1598` | `inward /= sqrtf(magSq)` | Invariant-safe plain API | Degenerate edges `continue`; only squared magnitudes above `1e-8` divide. |
| `Physics/ObjectContactManifold.cpp:1586` | `refCenter /= refFace.indexCount` | Invariant-safe plain API | The reference face comes from the same validated nonempty hull-face table. |
| `Physics/ObjectContactManifold.cpp:1849` | `centerToClosest / dist` | Invariant-safe plain API | The branch divides only when `dist > TOLERANCE`; otherwise the selected feature normal remains authoritative. |
| `Physics/PersistentContactSolver.cpp:439,447` | `axis /= axisMag` | Invariant-safe plain API | Both branches test `axisMag > TOLERANCE`; the unnormalizable seed keeps the existing deterministic axis. |
| `Physics/PersistentContactSolver.cpp:1388` | `angularVelocity / omegaMag` | Invariant-safe plain API | This branch is entered only when angular speed exceeds the positive body cap; zero speed takes the zero/clamped path. |
| `Physics/PersistentContactSolver.cpp:1609` | `releaseDir /= dirMag` | Invariant-safe plain API | A direction at or below tolerance returns before the release impulse is built. |
| `Physics/PhysicsApi.cpp:843` | `delta / distance` | Invariant-safe plain API | The conditional divides only when `distance > 0`; coincident spheres use world +X. |
| `Physics/PhysicsApi.cpp:909` | `boxToSphereNormalInBox /= distance` | Invariant-safe plain API | The enclosing branch requires `distanceSquared > 0`; the inside-box branch selects a face normal. |
| `Physics/PhysicsApi.cpp:1156` | `desc.direction / directionLength` | Invariant-safe plain API | A zero-length query direction returns an empty hit before division. |
| `Physics/PhysicsApi.cpp:1188` | `closestHit.normal / normalLength` | Invariant-safe plain API | The conditional divides only for positive length and otherwise uses the already normalized opposite ray direction. |
| `Physics/PhysicsBodyStore.cpp:453` | `weightedSum / wetWeight` | Invariant-safe plain API | The volume sampler returns its fallback when `wetWeight <= 1e-6`. |
| `Physics/PhysicsBodyStore.cpp:701` | `candidateWorldAxis /= sqrtf(axisLengthSq)` | Invariant-safe plain API | Candidate axes at or below `TOLERANCE²` are skipped before scoring. |
| `Physics/PhysicsBodyStore.cpp:732` | `correctionAxis /= error` | Invariant-safe plain API | An error squared at or below `TOLERANCE²` returns zero torque first. |
| `Physics/PhysicsBodyStore.cpp:830` | `linearDampingImpulse / deltaSeconds` | Invariant-safe plain API | `ApplyForces` is a fixed-step owner boundary; nonpositive delta is rejected before world-force application. |
| `Physics/PhysicsBodyStore.cpp:849` | `dampingImpulse / deltaSeconds` | Invariant-safe plain API | Same fixed-step positive-delta owner invariant as the linear damping row. |
| `Physics/PhysicsWorld.cpp:3083` | `delta / deltaMag` | Invariant-safe plain API | The conditional divides only above `TOLERANCE`; coincident diagnostic pairs use world +Y. |
| `Physics/Ragdoll.cpp:517` | `error / distance` | Invariant-safe plain API | The joint correction divides only when `distance > TOLERANCE`; otherwise it keeps world +X. |
| `Physics/TornadoField.cpp:269` | `delta / distance` | Invariant-safe plain API | The mutual-force direction divides only when `distanceSq > TOLERANCE²`; coincident bodies receive a deterministic phase direction. |
| `Runtime/Debug/PhysicsDebugVisualizer.cpp:221` | `dir /= len` | Invariant-safe plain API | A line at or below tolerance returns without an arrowhead. |
| `Runtime/Debug/PhysicsDebugVisualizer.cpp:230` | `side /= sideLen` | Invariant-safe plain API | A degenerate side basis returns without an arrowhead. |
| `Runtime/Editor/RunEditorPlacementAssets.cpp:1722` | `terrainNormal /= normalMag` | Invariant-safe plain API | A terrain normal at or below tolerance returns identity orientation. |
| `Runtime/Editor/RunEditorPlacementAssets.cpp:1743` | `axis /= axisMag` | Invariant-safe plain API | A parallel/antiparallel axis at or below tolerance returns the already selected orientation. |
| `Runtime/Editor/RunEditorTools.cpp:481` | `origin /= count` | Invariant-safe plain API | The selection-bounds function rejects an empty/invalid selection before accumulating `count` bodies. |
| `Runtime/Editor/RunEditorTools.cpp:543` | `origin /= count` | Invariant-safe plain API | The overlay-bounds function independently rejects an empty/invalid selection before accumulation. |
| `Runtime/Editor/RunEditorTracer.cpp:297` | `dir /= len` | Invariant-safe plain API | An arrow at or below tolerance returns before normalization. |
| `Runtime/Editor/RunEditorTracer.cpp:306` | `side /= sideLen` | Invariant-safe plain API | A degenerate side basis returns before division. |
| `Runtime/Editor/RunEditorTracer.cpp:1095` | `direction /= magnitude` | Invariant-safe plain API | The impulse marker returns when magnitude squared is at or below `TOLERANCE²`. |
| `Runtime/Render/RuntimeRenderPasses.cpp:2081` | `field / speed` | Invariant-safe plain API | Zero/tiny field speed is filtered before emitting the vector-field arrow. |
| `Runtime/Render/RuntimeRenderPasses.cpp:2089` | `side /= sideMag` | Invariant-safe plain API | The branch requires `sideMag > TOLERANCE`; otherwise world +X is used. |
| `Runtime/Replay/ReplayAuthoringCauseTree.cpp:68` | `value /= sqrtf(magSq)` | Invariant-safe plain API | The helper returns its explicit fallback at or below `TOLERANCE²`. |
| `Runtime/Replay/ReplayAuthoringCauseTree.cpp:231` | `value /= sqrtf(magSq)` | Invariant-safe plain API | The second cause-tree normalization helper has the same local magnitude guard and caller-specific fallback. |
| `Runtime/Replay/ReplayAuthoringCauseTree.cpp:617` | `centroid /= pointCount` | Invariant-safe plain API | Empty contact groups `continue`; the divisor is a strictly positive accumulated point count. |
| `Runtime/Replay/ReplayPrediction.cpp:736` | `value /= sqrtf(magSq)` | Invariant-safe plain API | The prediction helper returns its explicit fallback at or below `TOLERANCE²`. |
| `Runtime/Replay/ReplayPredictionDrawing.cpp:1726` | `value /= sqrtf(magSq)` | Invariant-safe plain API | The drawing helper returns its explicit fallback at or below `TOLERANCE²`. |

## Quaternion `Normalise` spelling audit

The following 24 matches are Quaternion calls, not Vector3 calls. They are
still listed so the mechanical `Normalise(` search is fully reconciled.
`Quaternion::Normalise` checks magnitude squared against `1e-12` and resets a
degenerate value to identity, so none can reach the retired Vector3 fatal path.

| File:line(s) | Input owner |
|---|---|
| `Maths/Quaternion.h:121` | normalized interpolation result |
| `Maths/Quaternion.cpp:111` | post-axis-rotation self-normalization (bare member call) |
| `Scene/TestSceneParserSchema.h:127` | Euler-to-quaternion composition |
| `Scene/TestSceneParserAssets.cpp:644` | asset instance/part composition |
| `Runtime/Editor/RunEditorPlacementAssets.cpp:433,445` | editor Euler and part-orientation composition |
| `Runtime/Replay/ReplayPrediction.cpp:743,1540,1543,1970,1987,2007,2183,2353` | solver samples, baseline poses, retained markers, and trails |
| `Runtime/Replay/ReplayPredictionDrawing.cpp:1481` | replay trail entry pose |
| `Runtime/Replay/ReplayPresentation.cpp:839,909,977,1070,1128` | replay body poses and ghost requests |
| `Runtime/Replay/ReplayValidation.cpp:913` | parsed validation pose |
| `Runtime/Scene/RunScene.cpp:130` | scene object orientation |
| `Runtime/Scene/SceneAuthoredSetup.cpp:112,682` | authored and hull orientations |

## T2 closure

Every reachable-degenerate Vector3 normalization or component/scalar division
already uses a Try API with an explicit deterministic fallback. Every remaining
plain call has a local positive-divisor guard or a distinct construction/owner
invariant recorded above. No source migration is required, so T3 is
documentation-only and no repository validation or baseline refresh is
authorized.

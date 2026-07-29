# Quaternion Convention Normalization QN0 Census

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Task: QN0
Decision: **PROCEED**

## Owner Checkpoint

The owner ratified continuation and directed the campaign to be orchestrated to
completion. The representation cleanup therefore proceeds despite the current
engine being behaviorally self-consistent. Local count drift or a repairable
site is evidence to record and resolve, not a reason to stop the campaign.

No baseline, golden, schema, config, or committed runtime artifact changed in
QN0.

## Characterization Before Core Changes

Five representation-independent cases were added to
`SkullbonezTests/TestQuaternion.cpp`:

1. composed world rotations retain API call order;
2. a positive Z world rotation maps +X to +Y;
3. the `R * I^-1 * R^T` world-inertia mapping preserves rotated principal
   axes;
4. the contact tangent basis is deterministic for world +Y;
5. the attached-camera target basis maps local to world and back.

These tests assert physical vectors only. They do not inspect quaternion
components or encode multiplication signs. The pre-change Profile binary passes
all 15 `Quaternion*` cases and 66 assertions.

## Live Census Reconciliation

The plan's estimates were measured against an earlier tree. The current source
contains:

| Surface | Plan estimate | Live result | Disposition |
|---|---:|---:|---|
| Production `GetOrientationMatrix` calls | 55 | 35 | Superseded by the exhaustive table below. |
| Production `TransposeMultiply` calls | 14 | 9 | Superseded by the exhaustive table below. |
| `GetQtnRotatedAboutX/Y/Z` sites | 6 | 6 declarations/definitions, zero consumers | The six lexical sites are the API declaration/definition pairs, not call sites. |
| Committed scenes with raw `orientation` arrays | 23 | 23 | Confirmed. |
| Authored Euler composers | 1 | 4 composers plus one child-composition site | The same convention-sensitive construction was duplicated after the estimate. |

### Orientation-matrix consumers

All 35 calls are **neutral consumers**. QN1-QN3 must preserve the physical
matrix produced for a migrated or newly constructed orientation, so no consumer
may add a transpose or conjugation locally. A failure in any consumer is fixed
at its producing convention or persistence boundary.

| File | Lines | Calls | Classification |
|---|---|---:|---|
| `Scene/AuthoredSceneParserAssets.cpp` | 716 | 1 | neutral |
| `Physics/BuoyancySystem.cpp` | 222 | 1 | neutral |
| `Physics/ObjectContactManifold.cpp` | 224, 241, 504, 1199 | 4 | neutral |
| `Physics/PersistentContactSolver.cpp` | 311, 636 | 2 | neutral |
| `Physics/PhysicsBodyStore.cpp` | 166, 212, 433, 673, 801 | 5 | neutral |
| `Physics/PhysicsEngine.cpp` | 220 | 1 | neutral |
| `Physics/Ragdoll.cpp` | 116, 364, 374 | 3 | neutral |
| `Physics/TerrainContactManifold.cpp` | 54 | 1 | neutral |
| `Physics/Stages/PhysicsSleepController.cpp` | 667, 668 | 2 | neutral |
| `Runtime/Camera/AttachedCameraController.cpp` | 115 | 1 | neutral |
| `Runtime/Interaction/RuntimePickGeometry.cpp` | 47 | 1 | neutral |
| `Runtime/Scene/SceneAuthoredSetup.cpp` | 222, 808 | 2 | neutral |
| `Runtime/Editor/EditorInteractionTools.cpp` | 1979 | 1 | neutral |
| `Runtime/Editor/EditorObjectPlacement.cpp` | 210, 509 | 2 | neutral |
| `Runtime/Editor/EditorPlacementAssets.cpp` | 565, 655, 669 | 3 | neutral |
| `Runtime/Editor/EditorTracer.cpp` | 509, 622, 926, 971 | 4 | neutral |
| `Runtime/Editor/LauncherTools.cpp` | 361 | 1 | neutral |

### Transpose-matrix consumers

All nine calls are **neutral inverse-basis consumers**. They ask a physical
rotation matrix to map world values back to local space. They must remain
unchanged when the quaternion and matrix conventions are corrected together.

| File | Lines | Calls | Use |
|---|---|---:|---|
| `Physics/ObjectContactManifold.cpp` | 505 | 1 | world sphere center to box-local space |
| `Physics/PersistentContactSolver.cpp` | 239 | 1 | world impulse to principal-inertia space |
| `Physics/PhysicsBodyStore.cpp` | 804 | 1 | world torque to principal-inertia space |
| `Physics/Ragdoll.cpp` | 128 | 1 | world vector to body-local space |
| `Runtime/Interaction/RuntimePickGeometry.cpp` | 204, 205, 211, 214 | 4 | world ray to collider-local space |
| `Runtime/Camera/AttachedCameraController.cpp` | 127 | 1 | world vector to followed-target space |

### Axis constructors

`Quaternion.h:89-91` and `Quaternion.cpp:165-185` are the six lexical
`GetQtnRotatedAboutX/Y/Z` sites. There are **zero callers**. The definitions are
classified **compensation/core** because their positive-sine values must match
the canonical Hamilton representation after QN1; no downstream site needs a
compatibility path.

## Convention-Sensitive Construction And Composition

| Site | Classification | QN1/QN2 action |
|---|---|---|
| `Maths/Quaternion.cpp:102-146` | compensation/core | Canonical axis delta, active matrix, and Hamilton product. |
| `Maths/Matrix4.cpp:285-339` | compensation | Emit the canonical active matrix used by rendering. |
| `Scene/AuthoredSceneParserSchema.h:119-132` | compensation | Preserve authored Euler physical order under Hamilton multiplication. |
| `Runtime/Scene/SceneController.Load.cpp:176-190` | compensation | Preserve legacy scene-controller Euler behavior. |
| `Runtime/Scene/SceneAuthoredSetup.cpp:105-119` | compensation | Preserve authored setup Euler behavior. |
| `Runtime/Editor/EditorPlacementAssets.cpp:467-490` | compensation | Preserve editor Euler and placement/part composition behavior. |
| Other `RotateAboutAxis` callers | neutral | They express positive world-axis rotations through the public API. |
| Quaternion interpolation and component equality/hash sites | neutral | Conjugating both endpoints preserves interpolation; hashes may change only as representation evidence. |

## Persistence And Raw-Component Boundaries

| Boundary | Classification | Evidence and required migration |
|---|---|---|
| Authored state parsing | persisted | `AuthoredSceneParserBodies.cpp:263-367` reads ball, box, and convex-hull quaternion arrays. Legacy versions require exact conjugation at load. |
| Authored asset expansion | persisted/compensation | `AuthoredSceneParserAssets.cpp:579,906,1125` and `AuthoredSceneParserSchema.h:135-142` materialize/write exact quaternion arrays. They must emit the current convention. |
| Editable scene writer | persisted | `SceneSnapshotWriter.cpp:105-112,260,330` writes raw components and currently stamps scene v2. The writer must stamp the new current version. |
| Replay sample capture | neutral carrier | `ReplayRecorder.cpp` copies live components into presentation and solver samples. It should continue to carry the current in-memory convention without local conversion. |
| Replay artifact | persisted | `ReplayV2Artifact.cpp:236-241,410,685,1212,1312,1832` writes/reads raw floats. The artifact header is currently v4 and must migrate legacy orientations deterministically. |
| Replay presentation/restore | neutral consumers | `ReplayPresentation.cpp` and `ReplayRestoreService.h` construct live quaternions from already-migrated artifact samples. |
| Replay prediction archive | persisted nested payload | `ReplayPredictionArchive.cpp:124-134,219-232` writes/reads quaternions inside the `RVPD` replay chunk. Its archive schema must distinguish and migrate the legacy representation. |
| Replay event quaternion hex | representation diagnostic | `ReplayRecorder.cpp:504,1781` records component evidence. Expected text/hash fixtures may change, but no reader treats it as a pose source. |
| Replay validation pose input | persisted test/probe input | `Runtime/App/ReplayValidation.cpp:298` constructs a quaternion from recorded numeric fields and must be audited with the artifact-version boundary. |

## Committed Raw-Orientation Scene Inventory

Exactly 23 tracked scene files contain an `orientation` field:

1. `aaa_ragdoll_clean_sky.scene.json`
2. `aaa_ragdoll_graphite_focus.scene.json`
3. `aaa_ragdoll_soft_pale.scene.json`
4. `aaa_ragdoll_sunset_showcase.scene.json`
5. `asd.scene.json`
6. `box_pile_throw_300.scene.json`
7. `box_slide_surface_compare.scene.json`
8. `buoyancy_inertia_orientation.scene.json`
9. `cause_effect_marble_run.scene.json`
10. `editor_brick_house_stability.scene.json`
11. `nbody_chaos_playground.scene.json`
12. `physics_scale_sleepy_5000.scene.json`
13. `prediction_ragdoll_wall_200.scene.json`
14. `qqq.scene.json`
15. `simdog.scene.json`
16. `solar_system.scene.json`
17. `solar_system_mars_slingshot.scene.json`
18. `space_field_200.scene.json`
19. `three_body_chaos.scene.json`
20. `three_body_figure_eight.scene.json`
21. `tornado_alley_showcase.scene.json`
22. `tornado_village_rampage.scene.json`
23. `trees.scene.json`

QN3 owns exact sign-bit conjugation for every raw orientation and the
legacy/current/future/writer tests. QN4 still owns hands-on initial-frame
acceptance before any baseline regeneration.

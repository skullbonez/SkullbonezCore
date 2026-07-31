# Angular Impulse Frame Correctness — AI0 Census

Date: 2026-07-31
Branch: `nightrunner-30th-JUL-26`
Impact area: Physics angular impulse response, tests, baseline prediction
Phase: AI0 complete

## Outcome

The pending-impulse application point is a **world-space offset from the
body's center of mass**, not an absolute world point and not a body-local
vector. AI2 must rename/document the API and stored field in that direction;
it must not rotate callers into body space.

The current pending path is wrong for a rotated body with anisotropic inertia.
A focused expected-failure test now compares the real
`PhysicsBodyStore::ApplyForces` pending path with
`PersistentContactSolveTransaction::ApplyImpulse`. It records:

| Path | Angular velocity after the equivalent impulse |
|---|---|
| Pending gameplay | `(-2.35, 0.96, 0.55)` |
| Contact solver | `(-1.13982, 0.808092, 0.55)` |

The mapped committed artifact prediction is **zero changed bytes**. Existing
baselines reproduce only frame-neutral cases: isotropic spheres, or generated
anisotropic boxes whose pending impulse is consumed while orientation is
identity. The only scripted launcher fixture targets a sphere.

## Exact Arithmetic Census

Let:

- `J_world` be a world-space linear impulse;
- `r_world` be the world-space center-relative application offset;
- `tau_world = r_world × J_world`;
- `R` map body-frame axes into world space;
- `I_body` be the diagonal body-frame rotational inertia.

### 1. World-force impulse path

`PhysicsBodyStore.cpp::ApplyWorldImpulse` performs:

```text
delta_v = J_world / mass
tau_body = R^T * tau_world
delta_omega_body = tau_body / I_body
delta_omega_world = R * delta_omega_body
```

Therefore:

```text
delta_omega_world = R * I_body^-1 * R^T * tau_world
```

The current implementation applies this rotation for every shape. For a sphere
the diagonal is isotropic, so the result is mathematically the same as direct
component division.

### 2. Persistent-contact path

`PersistentContactSolveTransaction::ApplyImpulse` forms `r × J` in world
space. `ApplyInverseInertia` then performs:

```text
usesWorldInertia == false:
    delta_omega_world = I_body^-1 * tau_world

usesWorldInertia == true:
    tau_body = R^T * tau_world
    delta_omega_world = R * (I_body^-1 * tau_body)
```

`SetupBodies` copies `PhysicsBodyRecord::usesWorldInertia`; dynamic non-sphere
bodies also copy their current orientation matrix. Body A subtracts the solver
row impulse and body B adds it, but both use the same inverse-inertia
conversion.

### 3. Pending gameplay path

`PhysicsBodyStore.cpp::ApplyPendingImpulse` currently performs:

```text
delta_v = J_world / mass
tau_world = pendingImpulseApplicationPoint × J_world
delta_omega_world = tau_world / I_body
```

It consults neither orientation nor `usesWorldInertia`. The operation is
correct only when the inertia diagonal is isotropic or body axes equal world
axes.

## Caller And Contract Census

Every direct production and test caller was inspected.

| Caller | Supplied application value | Frame finding |
|---|---|---|
| `RuntimeTools.cpp` launcher | `hitPoint - PhysicsBodyPosition(...)` | Definitive world-space center-relative offset. Both operands are world positions. |
| `MousePickupTools.cpp` | zero | Frame-neutral; deliberately adds no angular impulse. |
| `SceneGeneratedSetup.cpp` mixed, sphere, and box paths | sampled `(+/-1, +/-1, +/-1)` | Lever offset consumed at spawn. Generated orientation is explicitly identity. |
| `SceneAuthoredSetup.cpp` | scene `forcePosition` | Authored lever offset; all committed nonzero-force authored bodies are spheres. |
| `StartupProbeHarnesses.cpp` lifecycle probe | zero | Frame-neutral wake/linear-impulse probe. |
| `StartupProbeHarnesses.cpp` reorder probe | `(0.25, 0, 0)` | Storage-preservation value; never used as an integrated behavior oracle. |
| `TestDeterminism.cpp` sleep wake | zero | Frame-neutral wake proof. |
| `TestPhysicsHandles.cpp` reorder | arbitrary `(1, 2, 3)` | Storage-preservation value; no frame claim. |
| AI0 focused test | `(0.75, -0.4, 1.1)` | Explicit world offset shared with the contact row. |

`PhysicsEngine::{ApplyBodyImpulse,SetPendingBodyImpulse}` and
`PhysicsBodyStore::SetPendingBodyImpulse` only forward/store the value; they
introduce no conversion.

The historical `localApplicationPoint` name was introduced while preserving a
launcher calculation that already subtracted two world positions. That name is
therefore stale documentation, not proof of a body-local contract.

### Authored outlier

`ragdoll_playground.scene.json` gives `wake_ball` a `forcePosition` exactly
equal to its absolute position `(515, 28, 492)`. That is inconsistent with the
center-relative contract and with every small authored/generated lever value.
It is a scene-authoring defect, not evidence for an absolute-point API: the
runtime launcher already supplies an offset, and the stored path has no body
position with which to convert an absolute point. AI2 does not silently change
this scene while repairing the frame transform; AI3 must register the
authoring/schema follow-up.

## Scene And Fixture Census

### Mapped committed physics artifacts

| Gate artifact | Pending-path shape | Orientation when consumed | Predicted AI2 bytes |
|---|---|---|---:|
| `physics_regression_varied.csv` and `physics_query_varied.json` | Authored spheres only | Identity | 0 |
| `physics_regression_solver.csv` / known-issue signature | 15 generated spheres and 5 generated boxes; all five boxes are anisotropic for seed 42 | Identity | 0 |
| `bullet_sweep_{wall,object,terrain}.csv` | Authored sphere, center impulse | Identity | 0 |
| `shooting_reaction_volley.csv` | Authored spheres, center impulses | Identity | 0 |
| `space_three_body_chaos.csv` | No pending impulse | N/A | 0 |
| `physics_bench_perf.json` | Same authored sphere pending rows as varied | Identity | 0 |

The five seed-42 solver-box half extents are:

```text
(1.62, 1.80, 2.34)
(1.32, 1.20, 0.84)
(0.66, 0.60, 0.42)
(1.32, 1.20, 0.84)
(1.62, 1.80, 1.98)
```

All are anisotropic and all are created with
`IDENTITY_QUATERNION`. `PhysicsForceStage` consumes the pending impulse before
pose integration can rotate them.

### Interaction and replay fixtures

- `launcher_fire_click.json` targets `path_striker` in
  `interaction_replay_prediction_harness.scene.json`. The target is a sphere;
  its isotropic inertia makes orientation irrelevant.
- Mouse-pickup interaction uses a zero application offset.
- `prediction_ragdoll_wall_200.scene.json` is built from state-bearing bodies
  and does not seed a pending impulse. The replay visual-fidelity manifests
  therefore have no expected AI2 movement.
- Performance scale scenes are generated spheres. The selected-ball path
  fixture does not use a nonzero application offset.

### Repository-wide exposure outside mapped baselines

Generated scenes with `solverBoxes > 0` reach the anisotropic pending path at
identity: `physics_bench_solver`, `physics_bench_solver_boxes`,
`physics_regression_solver`, `replay_v2_generated_topology`, `settle_test`,
`standing_box_repro`, and `ui_controls`. None creates a rotated pending box.

Across all committed authored scenes, every nonzero `force` belongs to a
`ball`; none combines nonzero force with nonidentity Euler orientation. The
live launcher remains the production path that can apply a nonzero off-center
impulse to an already-rotated anisotropic box or hull.

## AI4 Pre-change Oracle

AI4 must compare actual output against this prediction:

1. AI1 changes **zero bytes** in every physics artifact.
2. AI2 changes **zero bytes** in all currently committed physics, query,
   performance, interaction, screenshot, and replay-visual baselines.
3. The focused AI0 test changes from expected failure to an ordinary passing
   test, with the gameplay result equal to the contact result.
4. Any committed artifact movement is unexplained divergence and blocks
   closure while reopening AI0 and AI2.

The AI4 owner gate remains binding even though the predicted delta is empty.
This plan authorizes no baseline regeneration.

## Validation And Audit

- Debug solution build: passed, zero warnings and zero errors.
- Focused characterization:
  `Debug\SKULLBONEZ_TESTS.exe --test-case=Pending?gameplay?* --no-skip --success`
  exited 0.
- Doctest observed two deliberate mismatches and classified the case
  `Failed as expected`; five assertions passed and two failed inside the
  expected-failure case.
- `tools\validate_tests.bat`: passed in Debug and Profile. The Profile suite
  covered 458 test cases and 2,424,719 assertions; the only two internal
  assertion failures were the expected mismatches above, so doctest returned
  success.
- `tools\validate_fast.bat`: all nine stages passed, including Debug/Profile
  build readiness and compiled-symbol reachability.
- Touched-source comment audit: `SkullbonezTests/TestPersistentContactSolver.cpp`
  1/1 checked, zero deferred. The new comments state the solver sign convention,
  current defect, and AI2 removal condition next to the test.
- Independent read-only review: clear after reconciling all campaign summaries
  to the binding zero-delta/no-regeneration ruling and correcting report
  whitespace.
- Production code and baselines are unchanged in AI0.

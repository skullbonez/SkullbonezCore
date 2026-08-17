# Deterministic Trigonometry Adoption

Date: 2026-08-17
Status: Owner-parked 2026-08-18; 0/8 tasks complete
Owner: Engine owner
Priority: Not selectable until the owner reactivates it in `MASTER-PLAN.md`
Commit name: `TRIG_DETERMINISM`

## Goal

Replace direct CPU production calls to the C/C++ runtime sine and cosine
families only where the repository-owned approximation proves that it preserves
the accuracy, convergence, physics, presentation, and performance its caller
needs. A platform `sin`/`cos` call may remain when measured evidence shows that
the deterministic approximation cannot satisfy that contract.

This is not a mechanical search-and-replace. The existing deterministic
`ComputeCosSin(float)` is a bounded binary32 approximation. Some current call
sites are double-precision iterative orbital algorithms, some establish
physics initial conditions, and others are visual tessellation. They do not
share one acceptable error envelope.

The closure state is:

- every direct `std::sin`, `std::cos`, `sinf`, or `cosf` call under
  `SkullbonezSource/` is either migrated to a named Maths owner with a certified
  input domain and measured error contract, or retained under an exact current-
  source ruling because the approximation failed the caller's measured
  contract;
- every retained platform call has a nearby source comment naming the owning
  algorithm, the failed approximation/error or convergence requirement, and
  the evidence that justifies platform trig;
- exact test-only reference/oracle uses remain explicitly classified and
  cannot become production dependencies;
- a static gate scans every first-party CPU production root, permits only the
  exact reviewed retained sites, and rejects every new or changed spelling,
  alias, pointer, macro, and line-break form already covered by the determinism-
  policy scanner's negative fixtures; and
- the solar-system fast-forward and orbital-planning evidence demonstrates
  that the migration did not trade reproducibility for unacceptable numerical
  drift.

## Why This Plan Exists

Tier-2 determinism closed the Physics-reachable transcendental set, but it did
not establish a repository-wide sine/cosine policy. The current checker scans
`SkullbonezSource/Physics` and `SkullbonezSource/Maths`. Calls in Gameplay,
Scene, Rendering, and Runtime are outside that scan, while the retained
`OrbitalMechanics.cpp` rulings only prove that those calls are not reachable
from Physics. They do not prove that changing the calls is numerically safe.

The risk is caller-dependent:

- scene Euler conversion can alter the initial orientation supplied to
  collision and inertia code;
- tornado trig chooses force-field motion, fallback directions, and spawned
  body positions;
- orbital trig participates in Newton and Lambert convergence and includes a
  double-precision Stumpff path that the float owner cannot replace;
- camera, debug, editor, and generated-mesh trig is presentation-only but can
  move screenshots, picking geometry, and interaction captures; and
- deterministic-math tests deliberately call platform trig as an independent
  error reference. Replacing those references with the implementation under
  test would manufacture a false pass.

## Evidence Basis — 2026-08-17

The inventory was produced from tracked first-party C/C++ source after removing
comments, using the same call family recognized by
`tools/check_determinism_math_policy.py`.

Current CPU inventory:

- production: **120 calls across 15 files**;
- production spelling: **18 `std::sin`/`std::cos` calls** and **102 global
  `sinf`/`cosf` calls**;
- tests: **20 calls across 5 files** — 14 `std::sin`/`std::cos`, 6 global;
- HLSL: **23 `sin`/`cos` intrinsic calls across 6 shaders**, classified
  separately below; and
- current determinism-policy scan: 95 Maths/Physics files, 29 ruled
  implementation-defined calls, 0 stale, 0 unruled, 0 errors. Of those, the
  sine/cosine rows are the 18 orbital calls and 2 RotationMatrix calls.

The inventory command intentionally includes `sinf`/`cosf`: global C spellings
have the same platform-library risk as `std::sin`/`std::cos`, and leaving them
outside the plan would turn the policy into a spelling loophole. Retained calls
remain numerical exceptions, not a count allowance: the gate matches exact
reviewed sites and rejects additions.

## Current Production Inventory And Risk

Risk describes replacement risk, not current defect severity.

| Risk | File and call count | Owner and use | Why replacement can fail | Required evidence before migration |
|---|---:|---|---|---|
| Critical | `SkullbonezSource/Maths/OrbitalMechanics.cpp` — 18 `std` calls | Planning orbital elements, Kepler propagation, Lambert Stumpff functions, and orbit polylines | Trig is inside iterative convergence. Two calls consume and return `double`; narrowing through the float owner can change convergence/no-solution classification and transfer velocities. This path does not drive live n-body Physics, but it drives guide arcs, trip planning, and porkchop results. | High-precision oracle sweep; current-vs-candidate A/B across solar states, eccentricity/horizon grid, Lambert residuals, convergence status, trip/porkchop hashes, and long-horizon propagation invariants. |
| High | `SkullbonezSource/Scene/AuthoredSceneParserSchema.h` — 6; `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` — 6; `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` — 6; `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp` — 6 | Four Euler-to-quaternion construction paths | These values can become authored body orientation and therefore collision geometry, world inertia, replay state, and physics baselines. Four duplicated implementations can also drift independently. | One shared conversion owner; exhaustive special-angle and randomized quaternion comparison; bit checks across all four entry paths; initial body-store snapshot comparison; focused collision scenes; physics and replay gates. |
| High | `SkullbonezSource/Gameplay/TornadoField.cpp` — 6; `SkullbonezSource/Gameplay/TornadoGameplay.cpp` — 2 | Moving force-field centre, fallback force direction, and body spawn ring | Results directly affect gameplay forces and body initial positions. Small differences can enter chaotic rigid-body evolution and worker-count comparisons. | Per-tick force-frame and spawned-state hashes; serial/1/4-worker equality; fixed-seed long run; tornado physics scenarios; before/after physics artifacts and performance. |
| Medium | `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h` — 40 | Sphere/capsule-style generated vertex positions and normals | Dense repeated calls amplify approximation error into mesh seams, normal length, silhouettes, picking, and visual baselines. It is not a Physics shape owner. | Vertex/index byte hashes, seam closure, unit-normal and radius error, representative DX12 captures, and mesh-build timing. |
| Medium | `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — 5; `SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.cpp` — 4; `SkullbonezSource/Maths/RotationMatrix.h` — 2 | Camera slerp, inspection orbit construction, and arbitrary-axis camera/editor rotation | Presentation-only today, but differences can move click targets, screenshots, capture hashes, and causal-inspection framing. `CameraCollection` also depends on inverse trig, so changing sine alone does not make the entire path deterministic. | Direct pose tests including nearly parallel/opposite vectors; click/orbit interaction automation; replay camera capture; visual comparison; no Physics reachability. |
| Medium | `SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` — 4 | Authored sun direction | Changes lighting and every affected visual baseline, but not the simulated world. | Direction-length/angular-error tests plus representative scene captures and DX12 validation. |
| Low | `SkullbonezSource/Gameplay/TornadoVisualPass.cpp` — 7 | Ribbon and funnel presentation geometry | Visual-only oscillation and geometry. Approximation can still create visible phase/amplitude changes. | Geometry hash/error envelope, tornado screenshot, and render stress. |
| Low | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` — 6; `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` — 2 | Debug circles, rings, and overlays | Diagnostic presentation only. A visible discontinuity or malformed ring would reduce debugging value but cannot alter Physics. | Ring closure/radius tests and debug-overlay screenshots. |

### Function-Level Orbital Split

The 18 orbital calls must not be treated as one replacement:

| Function group | Calls | Precision | Specific risk |
|---|---:|---|---|
| `RotatePerifocal` | 6 | float | Orientation basis orthogonality and accumulated position/velocity rotation error. |
| `SolveEccentricAnomaly` | 3 | float | Newton residual and derivative; a small error can change iteration count or `NotConverged`. |
| positive-`z` `Stumpff` | 2 | double | Cancellation near zero and Lambert residual accuracy; the float API is categorically unsuitable. |
| `ElementsFromState` | 3 | float | Element reconstruction and wrap behavior; coupled to `atan2`/`acos`, which remain separate policy entries. |
| `PropagateToTime` | 2 | float | Position/velocity phase drift over long horizons. |
| `SampleOrbitPolyline` | 2 | float | Presentation geometry only; lower risk than propagation despite sharing the file. |

### Test-Only Inventory

| File and calls | Disposition |
|---|---|
| `SkullbonezTests/TestDeterministicMath.cpp` — 4 global calls | Retain only as independent platform-reference/error-measurement code, or replace with committed high-precision oracle bits. Never route these through the implementation under test. |
| `SkullbonezTests/TestOrbitalMechanics.cpp` — 6 `std` calls | Replace fixture construction with exact committed vectors or a separately generated high-precision oracle so production and expected values do not share an implementation. |
| `SkullbonezTests/TestReplayPorkchopPanel.cpp` — 4 `std` calls | Same fixture rule; expected planner behavior must not be seeded by the production trig owner. |
| `SkullbonezTests/TestReplayTripPlanner.cpp` — 4 `std` calls | Same fixture rule; preserve an independent expected trajectory. |
| `SkullbonezTests/TestPersistentContactSolver.cpp` — 2 global calls | Replace the edge-contact fixture with committed constants or independently generated values; do not make the solver test depend on the trig approximation it may indirectly exercise. |

### Explicitly Outside The CPU Replacement

- The 23 HLSL `sin`/`cos` intrinsics in six shader files remain GPU shader
  operations. CPU `DeterministicMath` cannot replace them, and shader output is
  governed by DX12 validation and visual baselines. The static CPU gate must not
  pretend to enforce shader transcendental reproducibility.
- Third-party source is not modified. The scanner excludes it by physical root.
- `atan`, `atan2`, `acos`, `tan`, hyperbolic functions, and other
  transcendental calls remain governed by the broader determinism-math policy.
  This plan may add a shared double trig facility needed by orbital code, but it
  does not claim repository-wide closure of every transcendental family.

## Numerical Contracts To Establish

### Float Contract

Before adoption expands, re-certify `Math::Deterministic::ComputeCosSin(float)`
for every new caller domain rather than relying on the earlier Physics envelope.
Record:

- exact accepted input interval and the measured maximum input for every caller
  family;
- maximum absolute and ULP error for sine and cosine;
- angular error of the normalized `(cos, sin)` pair;
- odd/even symmetry, quadrant signs, continuity across every reduction
  boundary, and unit-length error;
- behavior for signed zero, subnormal input, NaN, infinity, and out-of-domain
  values; and
- MSVC, Clang, and GCC bit-oracle results under the repository floating-point
  contract.

The current `[-64*pi, 64*pi]` assertion is not automatically valid for
unbounded gameplay phases or authored Euler values. Callers either reduce into
a certified interval through the shared owner or reject out-of-domain input at
their existing recoverable boundary.

### Double Orbital Contract

Do not cast the Lambert Stumpff argument to float. DT2 must choose and certify
one of these honest outcomes:

1. implement a deterministic binary64 cosine/sine owner with bounded,
   documented range reduction and accuracy adequate for Lambert convergence;
2. algebraically reformulate the Stumpff evaluation around a deterministic
   series/rational owner with measured truncation and cancellation bounds; or
3. retain the two double runtime calls if the candidate cannot meet the
   accuracy/convergence contract, with a nearby source explanation and an exact
   current-source gate ruling that becomes stale if the site changes.

Outcome 3 is an allowed numerical result. It is not permission to add another
platform call: a plan runner may not silently narrow to float, declare
presentation-only code numerically irrelevant, or weaken the exact-site gate.

## Solar-System Fast-Forward Evidence

The existing scene `timeScale` slider is not the test instrument. In fixed-step
mode `SimulationSystem` caps execution at five physics ticks per presented
frame and discards additional requested whole ticks. Raising `timeScale` can
therefore drop simulated time instead of advancing the same simulation faster.

DT1 adds a headless diagnostic/test fast-forward lane with this contract:

- load an authored scene through the production scene-loading path;
- execute an exact requested number of ordinary `PHYSICS_FIXED_DT` ticks in a
  tight loop, without changing `dt` and without render/presentation work;
- never route through the five-tick frame cap or discard requested ticks;
- accept deterministic checkpoint intervals and emit a bounded summary rather
  than per-tick logs;
- record physics-state hashes, elapsed simulation time, wall time, ticks per
  second, and peak memory; and
- make the lane callable by focused tests/validation without becoming a second
  simulation scheduler or retained runtime owner.

Two distinct solar suites are required because they answer different questions.

### Live N-Body Suite

Use `SkullbonezData/scenes/solar_system_mars_slingshot.scene.json` and the
normal mutual-gravity Physics path. Compare current and candidate executables
from the same authored bytes at fixed checkpoints. Measure:

- byte hash of every body position, orientation, linear/angular velocity, and
  awake state;
- barycentre and total linear-momentum drift;
- total energy and angular-momentum drift relative to the initial state;
- maximum/minimum parent-relative distance for all 22 existing moon probes;
- rocket/Earth and rocket/Mars closest approach;
- finite-state and maximum-system-radius bounds; and
- 0/1/4-worker equality.

Start with the existing 120-second/14,400-tick case. Calibrate the headless lane
and then add bounded medium and long horizons selected by measured CI runtime;
the plan targets at least 10x and 100x the current simulated horizon without
changing `dt`. If 100x exceeds the focused-gate time budget, keep it as an
explicit long diagnostic while the largest bounded horizon joins CI.

The live n-body equations do not call `OrbitalMechanics.cpp`; they are still
required because scene-orientation and shared-Maths migrations can alter their
initial or per-tick state. An orbital-planning-only change is expected to leave
the live Physics hash byte-identical. Any live-state change must be attributed,
not dismissed as expected approximation drift.

### Analytic Planning Suite

From recorded solar states, compare current platform trig, candidate
deterministic trig, and a committed high-precision oracle across:

- circular through high-but-valid eccentricities;
- short, medium, and long propagation horizons, including forward/backward
  round trips;
- element reconstruction followed by propagation;
- Lambert departure/arrival geometry on both sides of convergence and
  no-solution boundaries;
- trip-planner intercept position/velocity and delta-v;
- every porkchop cell's status and finite values; and
- orbit-polyline closure, radius envelope, and orientation.

Record distributions, not just maxima: median, p90, p99, p99.9, maximum, and
the exact input that produced the maximum. Separately report any changed
iteration count or status classification because an average error cannot make
a convergence flip acceptable.

## Tasks

### DT0 — Make The Inventory And Enforcement Repository-Wide

- [ ] Extend `tools/check_determinism_math_policy.py` and its exact ruling data
  to scan every first-party CPU production root under `SkullbonezSource/`.
- [ ] Preserve negative fixtures for split-line calls, global qualification,
  namespace aliases, function pointers, same-line and continued macro aliases,
  while excluding member-access false positives.
- [ ] Classify test roots separately so independent reference calls are visible
  and exact, but never become production allowances.
- [ ] Add exact current-source rulings for retained production calls. Each
  ruling must match the file/site and call identity, point to the nearby source
  explanation, and fail strict validation when the call moves, changes, or a
  new unruled call appears. Do not use a count budget.
- [ ] Reproduce the 120-production/20-test inventory or explain every delta from
  this dated evidence.

Evidence: scanner self-test, strict repository scan, exact current rulings, and
an independent review that the widened roots have no bypass.

### DT1 — Build A/B Oracles And Exact-Tick Fast-Forward Before Replacements

- [ ] Add current-platform and candidate-backend comparison seams in tests;
  production continues using the current backend during this task.
- [ ] Add the bounded headless exact-tick solar fast-forward lane and checkpoint
  summary.
- [ ] Capture current reference artifacts for live n-body, scene quaternion,
  tornado, mesh, camera, and orbital-planning owners.
- [ ] Prove a negative control: perturb one trig result and demonstrate that
  each affected oracle fails.

Evidence: artifact hashes, wall time and memory for each horizon, negative
control output, and no production behavior change.

### DT2 — Certify Float And Double Deterministic Owners

- [ ] Extend the float domain/error evidence to all caller families.
- [ ] Implement and certify the chosen binary64/Stumpff strategy without
  narrowing to float, or record that it failed the measured contract and retain
  the affected platform calls under source explanations plus exact rulings.
- [ ] Add high-precision committed oracle vectors, reduction-boundary neighbors,
  special values, symmetry, quadrant, continuity, and cross-toolchain bit tests.
- [ ] Measure scalar cost and representative bulk cost against platform trig.

Evidence: exact oracle inputs/outputs, distribution report, MSVC/Clang/GCC
results, and performance table. No caller migrates until its input domain fits
one certified owner.

### DT3 — Migrate Presentation And Geometry Callers

- [ ] Migrate RotationMatrix, camera, scene style, primitive mesh generation,
  tornado visuals, editor tracing, and Physics debug visualization.
- [ ] Reuse pair-returning cosine/sine results so a site never computes the same
  angle twice.
- [ ] Preserve presentation-only ownership; do not move Runtime/Rendering types
  into Maths or make Maths depend upward.

Evidence: focused pose/mesh/ring tests, geometry and camera hashes, DX12 visual
gate, crash-free graphics stress, and measured performance.

### DT4 — Consolidate And Migrate Authored Euler Conversion

- [ ] Replace the four duplicated Euler-to-quaternion formulas with one
  dependency-correct Maths/Scene value operation owned by the authored
  orientation invariant.
- [ ] Prove all parser, scene load, generated setup, and editor placement paths
  produce the same normalized quaternion for the same authored input.
- [ ] Compare body-store initial snapshots before the first physics tick and run
  rotated collision/regression scenes.

Evidence: special/random angle sweep, entry-path equality, initial-state hashes,
focused parser/editor tests, replay tests, and physics gate. Preserve both
executables if physics output moves.

### DT5 — Migrate Tornado Simulation

- [ ] Migrate force-field drift, fallback direction, and spawn-ring trig.
- [ ] Keep visual migration attribution separate from gameplay force/state
  attribution even if both use the shared owner.
- [ ] Prove fixed-seed serial/1/4-worker force frames and physics state remain
  deterministic.

Evidence: force/spawn hashes, long fixed-seed scenario, worker-count oracle,
physics and performance gates, and an attributed before/after artifact diff.

### DT6 — Migrate Orbital Mechanics

- [ ] Migrate float orbital groups one function group at a time, recording
  accuracy and convergence evidence after each group.
- [ ] Migrate the double Stumpff path only if the certified binary64/series
  owner chosen in DT2 meets its convergence contract; otherwise retain the
  platform calls with the required source explanation and exact gate rulings.
- [ ] Run the full analytic planning suite after every convergence-sensitive
  group and the live n-body suite after the final source state.
- [ ] Update trip, porkchop, and orbital tests so their expected inputs remain
  independent of production trig.

Evidence: high-precision error distributions, convergence/status diff, planner
and porkchop hashes, live fast-forward metrics/hashes, and performance.

### DT7 — Close The Production Surface

- [ ] Strict scan reports no unruled direct sine/cosine call under
  `SkullbonezSource/`; every retained call matches its exact current-source
  ruling and nearby explanation, and tests contain only exact reviewed oracle
  calls.
- [ ] Rulings for migrated calls are deleted rather than left stale; rulings for
  retained calls and non-sine/cosine transcendental calls remain exact and
  accurate.
- [ ] Run all cumulative focused gates, then one independent numerical,
  ownership, and test-strength review.
- [ ] Run `tools\agent_validate.bat --plan-completion` exactly once, only after
  DT0-DT6 and independent review are complete.
- [ ] Record final performance/memory deltas and update the master/session
  ledgers before deleting this completed plan under repository convention.

Evidence: final classified-call inventory, exact retained-site rulings, focused
gate outputs, independent review verdict, single terminal full-plan validation
output, and final artifact hashes.

## Acceptance Criteria

- [ ] Every production call in the dated inventory is either migrated or
  retained because measured approximation evidence failed that caller's
  contract; no spelling is silently omitted.
- [ ] Every retained production call has a nearby source explanation and an
  exact current-source gate ruling; strict validation rejects any unreviewed
  addition or changed retained site.
- [ ] Tests do not compute expected values through the production implementation
  they are meant to verify.
- [ ] Float and double owners each have a certified input domain, exact
  cross-toolchain bit oracles, and measured accuracy/performance evidence.
- [ ] Authored quaternion paths are consolidated and initial physics state is
  tested before stepping.
- [ ] Tornado gameplay remains worker-count deterministic and has attributed
  physics evidence.
- [ ] Solar live n-body fast-forward preserves fixed `dt`, executes every
  requested tick, and reports bounded checkpoint evidence.
- [ ] Orbital planning has no unexplained convergence/status flip and satisfies
  the recorded propagation/Lambert accuracy envelope.
- [ ] Presentation migrations pass focused geometry/camera tests, DX12 visual
  validation, and graphics stress.
- [ ] The strict production scanner cannot be bypassed by supported C++ call,
  pointer, alias, macro, or line-break forms.
- [ ] Performance and memory regressions are measured and reported. Small
  regressions are acceptable under owner direction, but an unbounded hot-loop
  cost or allocation is not.
- [ ] Full validation runs once at terminal plan completion, never for an
  intermediate task or documentation-only edit.

## Baseline And Artifact Policy

- Establish current and candidate artifacts before each behavior-bearing
  migration can overwrite the prior executable.
- A physics mismatch does not become acceptable merely because deterministic
  math produced it. Attribute the first changed state and preserve both
  executables and their summaries for owner review.
- The owner's standing acceptance of small performance/memory regressions does
  not waive correctness, convergence, worker determinism, DX12 validation, or
  allocation policy.
- Do not refresh a visual, replay, physics, orbital, or planner golden until
  focused evidence demonstrates the intended transition and the applicable
  owner approval is recorded.

## Non-Goals

- Do not change the physics fixed timestep or enlarge `dt` to simulate time
  faster.
- Do not turn the runtime `timeScale` slider into the long-horizon test.
- Do not replace HLSL trig with CPU code.
- Do not modify third-party source.
- Do not claim that eliminating sine/cosine also eliminates all platform
  transcendental behavior; the broader checker continues to report other
  families.
- Do not narrow orbital double intermediates to float.
- Do not introduce a virtual math backend, callback pack, retained service bag,
  or per-call dynamic dispatch. A/B selection belongs in tests/build seams;
  production calls one concrete owner.
- Do not run full validation before terminal DT7 closure.

## Decisions Required During Execution

These are evidence decisions, not scope questions to answer by preference:

1. Which deterministic binary64/Stumpff strategy meets the measured Lambert
   contract, or which exact platform calls must remain because no candidate
   does?
2. What bounded medium/long solar horizons fit hosted CI after DT1 measures
   ticks per second? Keep the 100x horizon as a diagnostic if it is too slow for
   every PR.
3. Which test-only platform references remain exact scanner exceptions, and
   which become committed high-precision vectors?
4. Are any visual differences material enough to require approximation
   refinement before baseline review?

The plan runner records each answer with measurements in this file before the
dependent task is checked.

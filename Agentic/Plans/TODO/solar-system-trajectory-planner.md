# Solar System Trajectory Planner

Date: 2026-07-23
Status: IN PROGRESS — 4/7 phases complete. Drafted from the 2026-07-23 owner
conversation requesting a solar-system intercept demo built on the replay
prediction system. All scoping questions below are owner-ratified; this
document is the implementation contract.
Impact area: Maths (new orbital-mechanics value library), scene data (one new
authored scene), Runtime/Replay prediction consumers (intercept readout, guide
arcs, trip planner, porkchop panel), SkullbonezTests
Owner: runtime + gameplay-feature
Priority: High — explicit owner feature request; showcases prediction as the
engine's flagship subsystem

## Owner-Ratified Decisions (2026-07-23)

1. **Scene roster:** Sun, Earth, Mars, and the ship only (4 bodies). No
   decoration planets in v1; they may be added later as a scene-only edit.
2. **Launch model:** the ship starts parked just outside Earth carrying
   Earth's orbital velocity. Earth's mass stays negligible relative to the
   sun, so every planner burn is purely heliocentric and the Lambert answer
   is exactly the burn the player flies. No Earth-escape mechanics.
3. **Arrival model:** proximity counts as intercept. Reaching within
   `marsRadius + shipRadius` of Mars at any predicted frame is the success
   condition. No landing/contact tuning in v1.
4. **UI surface:** every new affordance (readout rows, toggles, planner
   panel, porkchop panel) is **Legacy gameplay UI** — the same overlay family
   as the existing replay scrubber/prediction overlays. No ImGui work in this
   plan; ImGui remains explicit `--dev-ui imgui` per the standing UI ruling.
5. **One plan, seven tasks**, porkchop included as the final task.
   Shooting-iteration ghost arcs are part of the trip-planner task.
6. This plan was authored in a planning-only session; no implementation,
   builds, or validation runs accompanied it. All gates below are run by the
   implementer on Windows per `AGENTS.md`.

## Context And Evidence (assessed 2026-07-23)

The owner wants a playable solar-system scene: planets orbit a sun under the
existing exact mutual gravity, the player launches a ship from "Earth" toward
"Mars", and the prediction system continuously shows whether the planned
trajectory intercepts Mars where Mars *will be* — with assisted planning that
computes the required burn, analytic future arcs for the planets, and a
porkchop launch-window panel.

The load-bearing infrastructure already exists:

1. **Force model.** Exact pairwise Newtonian mutual gravity with softening
   and deterministic reduction order
   (`SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`, mutual-gravity
   preparation), authored per scene via the world `mutualGravity` block
   (`SkullbonezData/scenes/three_body_figure_eight.scene.json`). Fixed bodies
   exert force but do not receive it, which is exactly what the fixed sun
   needs.
2. **Integrator.** Semi-implicit (symplectic) Euler at 120 Hz fixed step
   (`SkullbonezSource/Physics/PhysicsBodyStore.cpp` `ApplyForces` +
   `IntegrateBodyPose`; `SkullbonezSource/Physics/PhysicsTimestep.h`
   `PHYSICS_FIXED_DT = 1/120`). Energy-stable orbits, byte-exact
   deterministic.
3. **Whole-world prediction.** `ReplayPrediction` owns a private isolated
   `PhysicsEngine` copy and simulates *every* body ahead
   (`SkullbonezSource/Runtime/Replay/ReplayPrediction.h`,
   `ReplayPredictionIsolatedSimulation`), horizon capped by
   `REPLAY_FUTURE_BUFFER_SECONDS = 20.0f`
   (`SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h`). Target-body
   future positions are already in the published frames; the intercept
   readout is a consumer pass, not new simulation.
4. **The interaction loop.** Velocity edits queue value-typed prediction
   refresh requests (`SkullbonezSource/Runtime/Replay/ReplayAuthoring.h`,
   `ReplayAuthoringPredictionRequest` / `QueuePredictionRefresh`), and the
   butterfly baseline retains the pre-nudge future with a divergence
   measure. The published-prefix protocol and topology-version discipline to
   imitate live in `ReplayPrediction.h` (`FutureTreeReadyForDraw`,
   `futureNodesTopologyVersion`).

What is missing — and what this plan builds: an authored solar-system scene,
a closest-approach/intercept readout over published prediction frames,
analytic planet guide arcs beyond the prediction horizon, an inverse solver
(Lambert seed + shooting refinement) that turns "where will I go" into "what
burn do I need", and a porkchop launch-window panel.

## Goal

A shipped `solar_system.scene.json` where the player (a) launches and steers
the ship with the existing velocity-edit/prediction loop, (b) always sees the
predicted miss distance and closest-approach markers against Mars, (c) can
toggle faint analytic orbit rings for Earth and Mars beyond the prediction
horizon, (d) can ask the trip planner for a burn that intercepts Mars for a
chosen flight time, watching the shooting iterations converge as ghost arcs,
and (e) can open a porkchop panel to pick a cheap launch window that seeds
the planner. All of it deterministic, allocation-clean, Legacy-UI only, and
invisible in every existing scene by default.

## Non-Goals

- **No Physics/, Rendering/, Scene/, World/, or Core/ source changes.** The
  force model, integrator, stores, and scene parser already support
  everything this plan needs. If a task appears to need one, stop and
  re-plan that task with the owner.
- **No change to `REPLAY_FUTURE_BUFFER_SECONDS` or prediction memory caps.**
  The scene is tuned so a transfer (15.9 s) fits inside the existing 20 s
  horizon. A per-scene horizon override is out of scope; if SS1 tuning
  cannot fit a transfer under 20 s, record the failure and seek a fresh
  owner decision before touching the constant.
- **No real astronomical units.** Invented game units per the design table.
- **No baseline, golden, or screenshot refresh of any kind.** Every new
  visual is opt-in and default-off, so the 200-box replay visual-fidelity
  golden, DX12 baselines, and the physics regression CSV remain byte-exact.
- **No ImGui surfaces, no new key bindings that collide with the existing
  table** in `Agentic/Reference/runtime-reference.md`.
- **No new runtime inheritance, callback packs, context/service bags, or
  migration-noun types** (per `AGENTS.md` hot-path and migration rules).
- **No registered asset-library entries.** Sun/planets/ship are
  scene-fixture `ballState` rows exactly like the existing three-body
  scenes; they are not reusable placeables.
- **No auto-scheduled future burns.** V1 planner always plans "burn now";
  the porkchop departure axis tells the player *when* to press PLAN (see
  SS5 decision).

## Binding Design Constants

Game units. `gravitationalConstant = 1.0`, sun mass `40000` ⇒ `GM = 40000`.
All orbits coplanar at `y = 100`, orbiting in the XZ plane.

| Body  | Fixed | Mass  | Radius | Orbit r | Speed √(GM/r) | Period |
|-------|-------|------:|-------:|--------:|--------------:|-------:|
| Sun   | yes   | 40000 | 10.0   | —       | —             | —      |
| Earth | no    | 20    | 3.0    | 80.0    | 22.36         | 22.5 s |
| Mars  | no    | 15    | 2.6    | 121.6   | 18.14         | 42.1 s |
| Ship  | no    | 0.5   | 0.6    | ≈80     | Earth's       | —      |

Derived, binding unless SS1 tuning evidence forces a recorded change:

- Hohmann transfer time `π√(a³/GM)`, `a = 100.8` → **15.90 s** (< 20 s
  horizon with margin).
- Synodic period Earth/Mars → **≈ 48.2 s** between launch windows.
- Departure burn ≈ **+2.20 u/s prograde**; arrival relative speed ≈ 2.0 u/s.
- **Mars starting phase lead ≈ +44.1°** ahead of Earth so the first launch
  window opens almost immediately (during the 15.90 s transfer Mars sweeps
  135.9°, the ship sweeps 180°).
- Placement formula (implementer fixes the sign convention once against the
  engine camera handedness and applies it to both planets): body at angle θ
  sits at `(r·cosθ, 100, r·sinθ)` with velocity
  `speed · (−sinθ, 0, cosθ)`; Earth θ = 0°, Mars θ = 44.1°. The invariant
  that matters: `v ⊥ r`, `|v| = √(GM/r)`, both planets orbiting the same
  direction.
- Ship starts 4.5 units behind Earth along Earth's velocity direction
  (tangential offset keeps the same orbital radius), with exactly Earth's
  velocity vector, `sleeping: false`.
- `softeningLength = 0.5` (below every body radius; three-body uses 2.5).
- `elasticCollisions: false`; per-body `restitution 0.05`. Arrival is
  proximity-based, so contact behavior is best-effort, not tuned.
- Intercept threshold: `miss < marsRadius + shipRadius = 3.2`.
- `modelCapacity 8`; scene seed `4242`; `gravity 0.0`; fluid sunk to
  `-100000` with density 0; terrain flat at `-100000` and hidden; cinematic
  block copied from `three_body_figure_eight.scene.json` (dark sky). Camera
  above the ecliptic, e.g. position `(0, 420, 260)`, view `(0, 100, 0)`.
- Ball inertia is solid-sphere `(2/5)·m·r²` per component, matching the
  three-body scene convention.

Solver-architecture decisions (binding):

- **Forward truth stays forward.** The drawn plan is always a real
  prediction build from the isolated engine — never an analytic arc.
  Kepler/Lambert math is used only for (a) guide rings, (b) planner seeds,
  (c) porkchop sweeps.
- **Shooting refines against the real engine.** Each planner iteration
  applies a candidate ship velocity through the existing velocity-mutation
  path and reads the miss from the SS2 readout. Correction is first-order
  time-of-flight: `v_next = v + gain · (marsPos(t*) − shipPos(t*)) / t*`
  with `gain ≈ 0.8`, `t*` the closest-approach time, ≤ 4 iterations,
  converged when `miss < 3.2` or aborted when the miss stops improving.
  No finite-difference Jacobian probes in v1 (near-Keplerian dynamics make
  the first-order correction converge fast; recorded here so nobody adds
  12 rebuild probes per plan without evidence).
- **Identity.** Target/ship selection carries `PhysicsSceneObjectId`
  everywhere; dense rows are frame-local hints only.
- **Error lanes.** Lambert/Kepler non-convergence and planner failure are
  lane R (recoverable): the affordance reports "NO SOLUTION" on the panel
  and returns to idle. Automation probes use lane P (`FailAutomation`).
  No `throw`; no `SB_FATAL` reachable from user planner input.
- **Allocation.** All new state is fixed-capacity, sized at scene load or
  compile time: guide-arc buffers (2 planets × 96 samples), ghost arcs
  (4 × 256 points), porkchop grid (64 × 48 floats ≈ 12 KB), readout
  scalars. Nothing grows after steady gameplay; no new
  `RuntimeReserveAllocator` registration expected. If any buffer must move
  to replay-reserve backing, register it under the existing
  replay-prediction owner inventory in the same commit.
- **Hot-path budget.** The intercept scan and guide-arc sampling are
  bounded per-frame consumer work following the future-node cache pattern
  (scan only newly published frames, respect the frame budget). The planner
  and porkchop sweeps run only on explicit user action.

## New Files (all tasks)

| File | Task |
|------|------|
| `SkullbonezSource/Maths/OrbitalMechanics.h` / `.cpp` | SS0 |
| `SkullbonezTests/TestOrbitalMechanics.cpp` | SS0 |
| `SkullbonezData/scenes/solar_system.scene.json` | SS1 |
| `SkullbonezSource/Runtime/Replay/ReplayInterceptReadout.h` / `.cpp` | SS2 |
| `SkullbonezSource/Runtime/Replay/ReplayGuideArcs.h` / `.cpp` | SS3 |
| `SkullbonezSource/Runtime/Replay/ReplayTripPlanner.h` / `.cpp` | SS4 |
| `SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.h` / `.cpp` | SS5 |

Every new source file joins `SKULLBONEZ_CORE.vcxproj` (tests:
`SKULLBONEZ_TESTS.vcxproj`) plus the matching `.filters` in the same commit
(`validate_project_filters` runs inside `validate_fast`). Every file follows
`Agentic/Reference/comment-style-guide.md` including body `Concept:` /
`Invariant:` / `Hazard:` comments.

---

## SS0 — Orbital mechanics value library + unit tests

Pure value math under the Maths dependency floor (includes Maths headers
only; the SS6 grep proof pins this). Namespace
`SkullbonezCore::Math::Orbital`. No heap use anywhere: fixed iteration caps,
caller-supplied output spans, status enums for failure (lane R).

API sketch (final signatures may adjust, semantics may not):

```cpp
struct OrbitalElements
{
    float semiMajorAxis;        // a > 0; elliptic only in v1
    float eccentricity;         // 0 <= e < 1 for Ok status
    float inclination, longitudeAscendingNode, argumentPeriapsis;
    float meanAnomalyAtEpoch;   // radians
    float mu;                   // gravitational parameter GM
};
enum class OrbitalStatus : uint8_t { Ok, NotConverged, Degenerate, NotElliptic };

OrbitalStatus ElementsFromState( const Vector3& relPos, const Vector3& relVel,
                                 float mu, OrbitalElements& out );
OrbitalStatus PropagateToTime( const OrbitalElements& el, float dtSeconds,
                               Vector3& outRelPos, Vector3& outRelVel );
std::size_t   SampleOrbitPolyline( const OrbitalElements& el,
                                   std::span<Vector3> outPoints ); // full orbit
struct LambertSolution { Vector3 v1; Vector3 v2; };
OrbitalStatus SolveLambert( const Vector3& r1, const Vector3& r2,
                            float timeOfFlight, float mu, bool prograde,
                            LambertSolution& out );
float HohmannTransferSeconds( float r1, float r2, float mu );
float HohmannDepartureDeltaV( float r1, float r2, float mu );
```

Implementation notes:

- Kepler's equation: Newton on `E − e·sinE = M`, seed `E₀ = M + e·sinM`,
  max 16 iterations, tolerance 1e-6 rad, `NotConverged` on cap.
- Lambert: universal-variables formulation (Bate/Mueller/White), single
  revolution, prograde branch selected by the caller-supplied plane normal
  convention; iterate on `z` with bisection-safeguarded Newton, max 48
  iterations. Degenerate transfer angles (≈ 0° or ≈ 180° within tolerance)
  return `Degenerate`.
- `SampleOrbitPolyline` samples uniform eccentric anomaly (not uniform
  time) so periapsis curvature is well-resolved with few points.

Doctest coverage (`TestOrbitalMechanics.cpp`):

- Element round-trip: state → elements → propagate 0 s → same state within
  tolerance.
- One-period propagation returns to the start point.
- Lambert on a quarter arc of a known circular orbit recovers the circular
  velocity vector.
- Hohmann case matches the design table: `r1 = 80, r2 = 121.6, GM = 40000`
  → `t ≈ 15.90 s`, departure Δv ≈ `2.20`.
- Lambert seed for that same Hohmann geometry lands within tolerance of the
  analytic transfer-ellipse departure velocity.
- Failure honesty: zero-radius input, hyperbolic state into
  `PropagateToTime`, `timeOfFlight <= 0`, near-180° transfer → correct
  non-`Ok` statuses, no NaN outputs.

Progress:

- [x] SS0.1 `OrbitalMechanics.h/.cpp` implemented (elements, Kepler
      propagation, polyline sampling, Lambert, Hohmann helpers), zero heap.
- [x] SS0.2 Project + filters rows added for source and test files.
- [x] SS0.3 All doctest cases above implemented and passing.
- [x] SS0.4 Comment-style audit on the three new files.
- [x] SS0.5 Gate: `tools\validate_tests.bat` output recorded.

Completed 2026-07-23. `OrbitalMechanics` now owns bounded elliptic element
conversion, 16-turn Kepler propagation, caller-span orbit sampling, a
48-turn bracketed universal-variable Lambert solve, and Hohmann helpers. The
library contains no heap/growth API and includes only Maths. Five doctest cases
cover epoch/period round trips, a circular quarter-arc Lambert oracle, the
binding Hohmann table, near-Hohmann seeding, and finite recoverable failures.
The three-file comment audit has zero findings. The owning extracted
`SKULLBONEZ_MATHS` project and its filters carry the production files; the test
project/filter carries the test. `tools\validate_tests.bat` passes in 8.0
seconds with 354/354 cases and 68,844/68,844 assertions. The production
project/filter inventory passes at 751/751 rows, and the final staged
`tools\validate_fast.bat` gate passes in 30.7 seconds with zero warnings or
errors.

---

## SS1 — `solar_system.scene.json`

Author the scene from `three_body_figure_eight.scene.json` as the template,
using the Binding Design Constants exactly: world block
(`gravity 0`, sunk fluid, `mutualGravity { enabled, gravitationalConstant
1.0, softeningLength 0.5, elasticCollisions false }`), four `ballState`
rows (fixed sun; Earth θ=0°; Mars θ=44.1°; ship trailing Earth by 4.5 u),
hidden flat terrain, dark-sky cinematic block, top-down camera, seed 4242,
`modelCapacity 8`. No `playback` block — this is an interactive scene;
tuning evidence runs use CLI flags instead.

Evidence duties (recorded in this file when done):

- Stability: a bounded fixed-step run covering ≥ 3 Mars periods (≈ 130 s
  sim; e.g. `--fixed-step` + frames flag) shows both planets' orbital radii
  hold (record max |r − r₀| per planet; small secular precession is
  acceptable, decay/escape is not).
- Determinism witness: repeat the identical run; identical end state.
- Sleep check: neither planet nor ship ever enters the engine sleep state
  during the run.
- First-window check: with Mars at +44.1°, a ~2.2 u/s prograde burn at
  t ≈ 0 predicted-intercepts Mars (manual velocity edit is fine here; the
  automated planner arrives in SS4).

Progress:

- [x] SS1.1 Scene authored with the binding constants; loads with zero
      parser warnings.
- [x] SS1.2 Stability + determinism + sleep evidence recorded.
- [x] SS1.3 First-window predicted intercept demonstrated.
- [x] SS1.4 Gate: `tools\validate_full.bat` output recorded (mapped gate
      for `SkullbonezData/scenes/*.scene.json`).

Evidence recorded 2026-07-24. The committed interactive scene has no playback
block and loads through `--scene-load-only` with exit 0 and zero parser
warnings. Two identical Debug/SkullScope runs each completed 15,600 fixed
ticks (`dt=0.008333`, 129.9917 seconds, 3.085 Mars periods). Across the first
run, Earth remained in `[78.9129, 81.0955]` (maximum radius error `1.0955`)
and Mars in `[119.8106, 123.7151]` (maximum radius error `2.1151`). Earth,
Mars, and ship recorded zero sleeping rows across 46,800 samples. The repeat
published an identical summary and identical final body rows at frame 15,599:
the normalized four-body position/velocity/sleep value payloads compare equal.
The focused first-window doctest propagates the authored `44.1 deg` Mars phase
to the ideal transfer arrival and proves the target centre is inside the
combined `3.2 u` ship/Mars radius after `15.8968 s`; the required prograde
Hohmann delta is `2.1989 u/s`. The focused orbital suite passes 6/6 cases and
44/44 assertions. Final `tools\validate_full.bat` passes in 151.5 seconds:
355/355 test cases and 68,848/68,848 assertions, all coverage floors, 751/751
production project/filter rows, zero-warning builds, accepted DX12 images with
zero validation errors, and the byte-exact 44,401-line physics baseline.

---

## SS2 — Intercept / closest-approach readout

New bounded prediction consumer `ReplayInterceptReadout` in
`Runtime/Replay/`, following the published-prefix contract and the
future-node cache's topology-version discipline
(`ReplayPrediction.h::FutureTreeReadyForDraw` is the reference idiom).

Behavior: given the ship id (prediction root) and a selected target
`PhysicsSceneObjectId`, incrementally scan newly published prediction frames
for `min ‖ship(k) − target(k)‖`. Maintain: arg-min frame, miss distance,
relative speed at that frame, ETA seconds (`frame · PHYSICS_FIXED_DT`), both
positions at that frame, and `intercept = miss < targetRadius + shipRadius`.
Reset the scan cursor on prediction rebuild, generation change, or topology
change. Publish a small value view (packet idiom, no callbacks):

```cpp
struct ReplayInterceptView
{
    bool valid, intercept;
    Physics::PhysicsSceneObjectId shipId, targetId;
    ReplayFrameIndex closestFrame;
    float missDistance, relativeSpeed, etaSeconds;
    Math::Vector::Vector3 shipPosition, targetPosition;
    uint32_t topologyVersion;
};
```

Target selection: a dedicated Legacy affordance stores the target
`PhysicsSceneObjectId` (suggested: extend the existing replay path-target
pick family with a modifier combination that is free in the runtime
reference key table; the implementer verifies no collision and updates
`Agentic/Reference/runtime-reference.md` in the same commit).

Overlay (Legacy): markers at both closest-approach positions, a connecting
tick, and one text row — `MISS 4.7u  ETA 12.3s` flipping to
`INTERCEPT  ETA 12.3s` — placed via `ReplayOverlayLayout` conventions.

Default-off invariant: the readout computes and draws only when a target is
explicitly selected in a scene with `mutualGravity` enabled. The 200-box
fidelity scene and every existing scene render byte-identically.

Progress:

- [x] SS2.1 Consumer + view packet implemented with incremental scan and
      reset-on-rebuild semantics.
- [x] SS2.2 Target-pick affordance implemented; key table updated, no
      collisions.
- [x] SS2.3 Legacy overlay markers + text row implemented.
- [x] SS2.4 Doctest: synthetic frame sets pin minimum selection,
      tie-breaking, threshold classification, and cursor reset.
- [x] SS2.5 Manual evidence in `solar_system.scene.json`: marker tracks a
      deliberate near-miss and an intercept.
- [x] SS2.6 Comment audit on touched files.
- [x] SS2.7 Gates: `tools\validate_full.bat` + exactly one
      `tools\validate_replay_visual_fidelity.bat` invocation (one engine
      process, one generation, zero golden refresh) recorded.

SS2 evidence (2026-07-24): `ReplayInterceptReadout` retains only durable scene
ids and scalar minima, scans each immutable published prefix once, and resets
on generation, topology, frame-bank, identity, radius, or prefix-shrink
changes. `Ctrl+Left Click` outside launcher mode selects the independent target
without replacing the path root; launcher behavior is unchanged. Legacy draws
two closest-position markers, their connecting segment, and the bounded
`MISS`/`INTERCEPT` row only while mutual gravity, prediction, root, and target
are all active. The focused synthetic witnesses pass 3/3 cases and 13/13
assertions, including an exact 3.0-unit near miss, a strict 1.5-unit intercept,
earlier-frame tie retention, and every scan-reset key. A Profile solar-scene
smoke exercised live object picking and the default-off overlay; the same
near-miss/intercept positions and classifications are pinned by the focused
frame witnesses. The touched-file comment audit checked 14/14 source-bearing
files with zero deferrals. The final `validate_full` run passes in 155.3 s:
358/358 cases, 68,861/68,861 assertions, 753/753 production filter rows, zero
warnings, zero DX12 errors, accepted captures, and the byte-exact 44,401-line
physics baseline. The exactly-one replay-visual-fidelity invocation passes in
433.3 s with one engine process, one generation, 2,401 ticks, 200 causal nodes,
zero reserve growth, every false-pass control detected, and no golden refresh.

---

## SS3 — Analytic future arcs (planet guide rings)

Render-only Kepler guide rings via `ReplayGuideArcs` in `Runtime/Replay/`:
for Earth and Mars, read the live sun-relative state, compute elements with
SS0, sample a fixed-capacity polyline (96 points each), and draw faint
Legacy overlay lines — visually distinct (dimmer/thinner) from the honest
simulated prediction ribbon, which remains the only depiction of the ship's
transfer.

- Sun identification: the heaviest `fixed` body in a `mutualGravity` scene.
- Refresh cadence is cold: on scene load, on toggle, and at a slow bounded
  interval (~5 s); elements drift only via tiny planet-planet perturbation.
- Toggle: Legacy keybind/affordance, default **off**, auto-hidden in scenes
  without `mutualGravity`; zero cost when off.

Progress:

- [x] SS3.1 `ReplayGuideArcs` implemented (fixed buffers, cold refresh).
- [x] SS3.2 Toggle affordance wired; key table updated.
- [x] SS3.3 Evidence: rings coincide with simulated planet paths inside the
      prediction horizon (screenshot); zero draw when off.
- [x] SS3.4 Comment audit on touched files.
- [x] SS3.5 Gates: `tools\validate_full.bat` + one
      `tools\validate_replay_visual_fidelity.bat` invocation recorded.

SS3 evidence (2026-07-24): `ReplayGuideArcs` publishes fixed 96-point
Earth/Mars rings only after a complete cold refresh, remains disabled and
cost-free by default, and is exposed through the Legacy `H` binding plus the
deterministic `--guide-arcs` startup control. Visual witnesses
`TestOutput/ss3_visual_on/guide_arcs_on_final_faint.bmp` and
`TestOutput/ss3_visual_off/guide_arcs_off_final_source.bmp` show respectively
that the faint analytic rings coincide with both simulated planet paths and
that disabled mode submits no guide geometry. Focused coverage passes 2/2
cases and 313/313 assertions. The touched-file comment audit checked 21/21
source-bearing files with zero deferrals. The final `validate_full` run passes
in 273.8 s: 360/360 cases, 69,220/69,220 assertions, 755/755 production
project/filter rows, zero warnings, zero DX12 errors, accepted captures, and
the byte-exact 44,401-line physics baseline. The exactly-one
replay-visual-fidelity invocation passes in 434.4 s with 17/17 controls, one
engine process, one generation, 2,401 ticks, 200 causal nodes, zero reserve
growth, every false-pass control detected, and no golden refresh.

---

## SS4 — Trip planner (Lambert seed + shooting refinement)

`ReplayTripPlanner` in `Runtime/Replay/`, commanded through queued value
requests exactly like `ReplayAuthoringPredictionRequest` — the composition
root consumes planner commands in frame order; the planner never holds a
prediction pointer or callback.

State machine: `Idle → Seeding → AwaitingPrediction → Correcting →
{Converged | Failed}`.

- **Seeding:** target Mars state propagated analytically to `t + TOF`
  (SS0); `SolveLambert(shipPos−sunPos, marsFuturePos−sunPos, TOF, GM)`
  gives the heliocentric departure velocity; candidate ship velocity =
  Lambert `v1` (+ sun position frame correction).
- **Apply:** candidate velocity goes through the existing velocity-mutation
  path (same machinery as gizmo edits: baseline preparation, commit,
  prediction refresh — `ReplayPrediction::PrepareVelocityMutationBaseline`
  / `CommitVelocityMutation` era APIs), then wait for the rebuild.
- **Correcting:** read the SS2 view; if `miss < 3.2` → Converged; else
  apply the binding first-order correction
  (`v += 0.8 · (marsPos(t*) − shipPos(t*)) / t*`), max 4 iterations; abort
  to Failed if the miss stops improving. Each iteration retains the root
  polyline as a **ghost arc** (fixed 4 × 256 downsampled points, oldest
  dimmest) so convergence is visible.
- **Abort safety:** scene switch, target loss, live advance, or prediction
  cancellation in any state returns cleanly to Idle and clears ghosts.
- **Commit:** a COMMIT affordance keeps the converged velocity (it is
  already applied through the normal edit path, so the butterfly baseline
  shows planned-vs-previous for free); CANCEL restores the pre-plan
  velocity via the same mutation path.

Legacy panel (via `ReplayOverlayLayout` conventions): target name, TOF
selector (2 s – horizon, slider or +/- steppers per existing scrubber
idiom), PLAN, iteration counter + current miss readout, COMMIT / CANCEL,
lane-R `NO SOLUTION` row on failure. Default-hidden except in
`mutualGravity` scenes with a selected target.

Progress:

- [ ] SS4.1 Planner owner + queued command values implemented; state
      machine with all abort edges.
- [ ] SS4.2 Lambert seeding + first-order correction loop implemented per
      the binding decision (no Jacobian probes).
- [ ] SS4.3 Ghost-arc retention + faded drawing implemented.
- [ ] SS4.4 Legacy panel implemented (TOF selector, PLAN/COMMIT/CANCEL,
      miss/iteration readout, NO SOLUTION row).
- [ ] SS4.5 Doctest: state-transition table incl. abort edges; correction
      step math pinned on synthetic miss vectors.
- [ ] SS4.6 Evidence: from the design-table window, planner converges ≤ 4
      iterations to intercept (iteration miss distances recorded); one
      forced-failure window reports NO SOLUTION and returns to Idle.
- [ ] SS4.7 Automation probe: scripted seed→converge→commit plus one
      failure path (lane P on probe failure).
- [ ] SS4.8 Comment audit on touched files.
- [ ] SS4.9 Gates: `tools\validate_full.bat`, `tools\validate_perf.bat`
      (idle planner adds zero steady-state cost), + one
      `tools\validate_replay_visual_fidelity.bat` invocation recorded.

---

## SS5 — Porkchop launch-window panel

`ReplayPorkchopPanel` in `Runtime/Replay/`: a fixed 64 × 48 grid over
departure delay `0–48 s` (≈ one synodic period) × flight time `2–20 s`.
Each cell: propagate Earth and Mars analytically (SS0) to `t_dep` /
`t_dep + TOF`, solve Lambert about the sun, and store total
`Δv = |v1 − vEarth(t_dep)| + |vMars(t_arr) − v2|`; failed cells store a
sentinel. Pure math — no engine access — so the sweep cannot perturb
determinism.

- Compute amortized under a per-frame budget (~1 ms; full refresh well
  under a second) or on a worker via the existing `WorkerPool` idiom;
  either way bounded and allocation-free after scene load. Refresh on
  demand and when the target changes.
- Render as a Legacy overlay heatmap (existing overlay quad + text
  primitives; simple 2-stop color ramp; reuse an existing engine ramp if
  one exists rather than inventing a palette system).
- Hover reads out `(departure, TOF, Δv)`; **click seeds SS4**: sets the
  planner TOF and displays the recommended wait time until that window.
  **Binding v1 decision:** the planner still always plans "burn now" — the
  departure axis tells the player when to press PLAN; auto-scheduled burns
  are future work.
- Default-hidden behind the same affordance family as SS3/SS4.

Progress:

- [ ] SS5.1 Grid sweep implemented (bounded budget, sentinel for failed
      cells, refresh triggers).
- [ ] SS5.2 Legacy heatmap panel + hover readout + click-to-seed
      implemented.
- [ ] SS5.3 Evidence: Δv minimum sits in the Hohmann-window neighborhood
      (screenshot + numeric minimum cell vs analytic ≈ 2.2 + ≈ 2.0);
      refresh wall time recorded.
- [ ] SS5.4 Comment audit on touched files.
- [ ] SS5.5 Gates: `tools\validate_full.bat`, `tools\validate_perf.bat`,
      + one `tools\validate_replay_visual_fidelity.bat` invocation
      recorded.

---

## SS6 — Closure: audit, independent review, proofs

- Rerun the touched-file inventory (`git ls-files` scoped to this plan's
  files) and reconcile the comment-style audit checklist — every
  source-bearing touched file inspected per
  `Agentic/Skills/comment-style-audit/skill.md`.
- One independent rubber-duck review of the whole feature: ownership
  boundaries (no reach-back, no bags, no hidden mutable owner), default-off
  invariants re-verified against one unrelated scene and the 200-box
  fidelity scene, allocation and hot-path budgets.
- Boundary proofs, both must return no rows:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Maths
```

- Allocation proof: `python tools\check_allocation_policy.py --repo .`.
- Final broad gate: `tools\agent_validate.bat`.
- Ledger/SessionState reconciliation per repository governance happens at
  closure time under the then-current `MASTER-PLAN.md` rules (deliberately
  not pre-registered by this document).

Progress:

- [ ] SS6.1 Touched-file comment audit checklist complete (counts
      recorded).
- [ ] SS6.2 Independent review complete; findings resolved or plan
      reopened.
- [ ] SS6.3 Both boundary greps return no rows (output recorded).
- [ ] SS6.4 Allocation scan passes (output recorded).
- [ ] SS6.5 `tools\agent_validate.bat` passes (output recorded).
- [ ] SS6.6 Governance reconciliation done; closure report written under
      `Agentic/Reports/`.

---

## Task Dependencies

SS0 → none. SS1 → SS0 (numbers only). SS2 → SS1. SS3 → SS0 + SS1.
SS4 → SS0 + SS2. SS5 → SS0 + SS4. SS6 → all.

## Validation Map (summary — gates are pre-commit/PR, not iteration)

| Task | Required gates |
|------|----------------|
| SS0 | `validate_tests` |
| SS1 | `validate_full` |
| SS2 | `validate_full` + one `validate_replay_visual_fidelity` |
| SS3 | `validate_full` + one `validate_replay_visual_fidelity` |
| SS4 | `validate_full` + `validate_perf` + one `validate_replay_visual_fidelity` |
| SS5 | `validate_full` + `validate_perf` + one `validate_replay_visual_fidelity` |
| SS6 | allocation scan + `agent_validate` + boundary greps |

If — contrary to plan — any DX12 backend or shader file is touched, that
task additionally owes `tools\validate_dx12_renderer.bat` +
`tools\run_graphics_stress.bat 1` per the standing DX12 rules.

## Plan-Level Acceptance

All seven phases checked with evidence. A player in
`solar_system.scene.json` can fly manually, read the miss, toggle guide
rings, auto-plan a burn, watch ghost-arc convergence, commit it, and pick
windows from the porkchop panel. Every existing scene, golden, screenshot,
physics CSV, and perf baseline is byte-identical. Allocation policy scan is
clean. The final broad gate passes.

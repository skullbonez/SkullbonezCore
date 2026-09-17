# Terrain Driving Vehicle

Date: 2026-09-18
Status: WNF — owner-parked 2026-09-18; 0/8 phases complete.
Owner: Gameplay vehicle feature, with Physics-owned ground queries and dynamics
Impact area: Physics, Gameplay, scene/assets, input, camera, presentation,
replay/prediction, automation, tests, and performance
Priority: Parked; excluded from active portfolio totals
Commit name: `TERRAIN_VEHICLE`

## Owner Direction And Activation

The owner requested a plan for making a vehicle drive over terrain and directed
that it be placed in WNF. This is documentation only. Do not begin implementation
until the owner explicitly reactivates it and moves it to `TODO/` with its ledger
entry. Activation follows `Agentic/Skills/orchestrator/SKILL.md` and the current
repository contract. This plan does not authorize a PR or merge.

## Goal

Deliver one reusable four-wheel buggy that the user can place in a scene, drive
over the existing heightfield, steer, brake, reverse, jump, land, and reset. Use
one rigid chassis, four simulated suspension contacts, and visible wheels whose
poses follow the simulated state. Provide a follow camera and a small authored
course with flat ground, slopes, bumps, a crest, and a jump.

The first playable milestone is TV0-TV4. TV5-TV7 complete dependable scene,
history, acceptance, and performance integration. A playable checkpoint is not
completion of this plan.

## Inspected Starting Point

Source inspection on 2026-09-18, HEAD
`94a2cf6966fa4f18ebcc6a220ddf5d55a406e78a`; no vehicle runtime experiment was run.
Refresh these findings at activation rather than treating this parked document
as proof about a future checkout.

| Existing foundation | Evidence and limitation |
|---|---|
| Rendered, editable heightfield and analytic slopes | `SkullbonezSource/World/Terrain.h`; terrain save/load and shared render/collision tests in `SkullbonezTests/TestTerrain.cpp` |
| Physics-owned terrain sampling | `SkullbonezSource/Physics/PhysicsTerrainView.h` supplies heights and triangle planes; arbitrary suspension-direction casts still need implementation |
| Chassis dynamics and point impulses | `SkullbonezSource/Physics/PhysicsEngine.h` exposes body impulses with application offsets; a multi-wheel fixed-tick force path still needs explicit sequencing and accumulation |
| Public body raycast | `SkullbonezSource/Physics/PhysicsEngine.cpp`, `PhysicsEngine::RayCast`, tests conservative collider bounding spheres and does not include terrain; it is unsuitable as the exact suspension query |
| Soft point joints | `SkullbonezSource/Physics/PointJointConstraint.h` and `PointJointSettings.h`; these are not wheel hinges, motors, or suspension travel constraints |
| Attached camera modes | `SkullbonezSource/Runtime/Camera/AttachedCameraValues.h` includes fixed-relative and velocity-forward following |
| Fixed-tick automation | `tools/skarness.py` and `Agentic/Skills/skarness/SKILL.md`; vehicle commands and state must be added if absent |

No vehicle, tyre, steering, or drivetrain implementation was found in the
inspected production source. The current hull plan retains owner visual/cost
acceptance work; this plan does not claim that work is complete or activate
parked solver experiments. A primitive box chassis avoids making the first
driving milestone depend on detailed convex vehicle geometry.

## Scope And Design Decisions

- Start with terrain-only suspension contacts. Body collisions remain the
  chassis collider's responsibility. Wheels do not support the vehicle on
  arbitrary props, bridges, moving platforms, or other vehicles in this plan;
  those require exact shape queries and reaction forces in a later extension.
- Cast along each wheel's chassis-relative suspension axis. Use the actual
  terrain triangle planes, finite travel, wheel radius, consistent normals,
  and explicit misses outside the heightfield. A vertical height lookup alone
  is insufficient when the chassis pitches or rolls.
- Implement compression, spring force, damping from contact-point velocity,
  travel limits, and bounded bump response. Ground support cannot pull the
  chassis into the ground; an airborne wheel supplies no ground force.
- Apply wheel forces at their actual chassis offsets so pitch, roll, and weight
  transfer follow rigid-body dynamics. Do not substitute orientation locking,
  terrain snapping, or direct velocity setting for handling.
- Use a simple load-limited combined longitudinal/lateral grip model with
  explicit low-speed behaviour. Support throttle, brake, reverse, steering
  rate/angle limits, and rolling resistance. Freeze the initial driven axle
  choice and numerical handling targets in TV0.
- Keep the ordinary timestep, gravity, contact solver, friction defaults, and
  sleep policy unchanged outside the new vehicle feature. Wake a controlled
  chassis when input requires it; allow a parked vehicle to settle without
  repeated artificial waking from negligible suspension corrections.
- Detailed tyre deformation, gears/clutch/differentials, articulated axles,
  physical wheel bodies, damage, racing AI, networking, terrain streaming,
  deformable ground, gamepad support, and production vehicle art are follow-ups.

## Ownership And Integration

Physics owns exact terrain queries and the deterministic vehicle dynamics state
needed to advance, clone, restore, and predict a chassis. Store vehicle-specific
state in a bounded side store associated with stable body handles; do not add
wheel fields to every `PhysicsBodyRecord` without the required review decision.
Accumulate every wheel's linear and angular contribution before application;
the single pending point-impulse command must not silently replace earlier
wheel contributions. Define the force stage relative to integration, contacts,
CCD, sleep, and any partial time advancement before implementing it.

Gameplay owns vehicle intent and authored handling choices. Runtime's existing
input owner routes controls; scene/assets own construction and persistence;
camera owns following; presentation consumes detached chassis/wheel poses.
`Runtime/App` composes these owners without retaining vehicle business state.
Physics must not include World, Gameplay, Runtime, UI, or Replay. Use the
existing Physics terrain value view, not a World callback. Any newly required
Runtime dependency row needs the normal owner decision and mechanical proof.

Resolve `PhysicsSceneObjectId` at boundaries and typed body handles in simulation.
Deleting, resetting, or replacing a chassis must invalidate its controls,
camera target, wheel state, and diagnostics together. Preallocate all vehicle
and query storage at scene load; define capacities, high-water reporting, and
the repository's required exhaustion behaviour.

Register the reusable buggy through `SkullbonezData/assets/` and instantiate it
through scene `assetInstances[]`. Version any changed authored format and test
legacy/current/future inputs and writer round trips. New files and schema
details are selected during TV0; the paths above are existing evidence, not
claims that proposed implementation already exists.

## Phases

- [ ] **TV0 — Freeze the implementation contract and course.** Refresh source
  findings; select exact owners and files, chassis dimensions/mass/inertia,
  wheel layout, driven axle, gravity/units, force sequencing, capacities, and
  control mapping. Author a deterministic course specification and numerical
  acceptance thresholds before tuning: supported slopes and speeds, ride
  height tolerance, settling time, stopping distance, turning radius, maximum
  penetration, jump/drop heights, and CPU/memory budgets. Preserve an unchanged
  non-vehicle control run. Record replay/prediction state and input policy.
  Acceptance: every subsequent phase has a concrete input, measurable outcome,
  and owning subsystem; no unexplained global-physics change is planned.

- [ ] **TV1 — Exact bounded suspension queries.** Add the narrow Physics query
  over the terrain value view, returning distance, point, normal, and stable
  terrain feature identity. Handle triangle seams, angled/parallel rays,
  boundaries, below-surface starts, invalid input, and deterministic equal-hit
  choices. Decide and document single-ray limitations for abrupt steps rather
  than claiming wheel-volume collision. Acceptance: analytical plane and
  authored heightfield tests match expected results; misses generate no force;
  existing public approximate-ray semantics remain compatible.

- [ ] **TV2 — Chassis and four-point suspension.** Construct the bounded vehicle
  state and spring/damper response at the fixed-step boundary. Apply all four
  contact forces and torques exactly once for the elapsed interval; cover
  compressed, extended, airborne, inverted, bottomed-out, and sleeping states.
  Acceptance: the chassis settles at the declared ride height, responds to an
  off-centre load, dissipates bounce, crosses seams without force spikes, and
  lands without nonfinite values or artificial launches. A planted missing
  wheel contribution or reversed damper sign must fail a focused assertion.

- [ ] **TV3 — Traction, steering, braking, and reverse.** Add load-limited grip,
  drive/brake commands, wheel spin, steering geometry and rate limits, and
  stable zero-speed transitions. Acceptance: acceleration, stopping distance,
  forward/reverse turns, hill starts, coasting, combined braking/turning, and
  airborne throttle satisfy TV0 bounds. No lateral grip exists without normal
  load; braking cannot inject forward energy or oscillate across zero speed.
  Tyre and ordinary chassis contact friction must not double-count the same
  intended wheel support.

- [ ] **TV4 — Playable controls, camera, and wheel presentation.** Integrate the
  existing input router with explicit drive mode, keyboard controls, release
  on focus loss/UI capture, and one-shot reset. Reuse the attached-camera owner
  with stable low-speed/reverse following and terrain clearance. Draw wheels
  at suspension/steering/spin poses using existing generic render facilities.
  Add Skarness control and observation capabilities as needed. Acceptance:
  native driving completes the course, UI typing cannot accelerate the car,
  held reset triggers once, and screenshots show the observed chassis and
  wheel identities. This is the first playable milestone.

- [ ] **TV5 — Reusable authoring and complete history state.** Finish registered
  asset placement, scene save/load, reset, deletion, and schema migration tests.
  Capture all state that affects subsequent dynamics, including any retained
  suspension, wheel-speed, steering, and command state. Replay scrub/restore and
  save/load restore matching wheel visuals and simulation; a continued recorded
  command sequence reproduces the same result. Prediction uses an explicit
  frozen-input assumption and cloned vehicle state, never samples live future
  input, and communicates that assumption. Acceptance: same-state continuation,
  branch restore, loaded recording, old non-vehicle artifacts, and prediction
  isolation pass without a second vehicle/history timeline.

- [ ] **TV6 — Driving acceptance matrix and tuning.** Run the declared flat,
  uphill/downhill, side-slope, crest, seam, bump, jump, landing, rollover/reset,
  edge-of-terrain, and chassis-obstacle cases. Check deletion/reload and multiple
  vehicles even though only one is controlled. Verify identical fixed-tick
  command results at varied render rates and supported worker counts, within
  the repository's determinism envelope. Preserve identity-bound Skarness
  streams and inspect representative screenshots. Acceptance: TV0 bounds pass,
  negative controls detect broken support/grip, and the same course is usable
  by a person. Record single-ray obstacle limitations honestly.

- [ ] **TV7 — Terminal review, validation, and handoff.** Perform one independent
  implementation/ownership review and resolve findings. Run the cumulative
  mapped gates below and measure vehicle CPU, query work, and memory against
  TV0 budgets. Confirm zero steady-gameplay allocation, unchanged non-vehicle
  behaviour, warnings-free builds, and required rendering diagnostics. Complete
  the terminal plan gate and leave Profile built according to `AGENTS.md`.
  Provide launch/control instructions, course and evidence paths, limitations,
  and the final checked count. Update MASTER-PLAN and SessionState on closure;
  remove the completed plan under the repository convention. PR submission or
  merge still requires explicit owner instruction.

## Validation And Evidence

No repository validation is required to write or park this document. During
implementation, add focused subsystem tests with each behaviour. Keep iteration
checks short; concentrate heavy suites and independent review in TV7. At any
earlier push boundary, use the applicable cumulative lane in `AGENTS.md`.

The following commands exist at drafting time; reconcile the current mapping
at activation and avoid rerunning gates already supplied by the terminal
umbrella unless a change or failure requires it:

| Change/risk | Required command or evidence |
|---|---|
| Changed production targets, Runtime, tooling, authored scenes | `tools\validate_fast.bat` |
| Unit tests | `tools\validate_tests.bat`; TV0 must record exact focused test filters once named |
| Physics forces, query, contact interaction, fixed stepping | `tools\validate_physics.bat` and `tools\validate_physics_deep.bat` |
| Dependency direction | `tools\validate_dependency_graph.bat` |
| Authored format changes | `python tools\migrate_data_formats.py --check` plus scene reader/writer migration tests |
| Native input/scene lifecycle | `tools\validate_automation.bat` plus the new vehicle Skarness regression |
| History/restore/artifact/presentation | `tools\validate_replay_v2_artifact.bat`, `tools\validate_replay_visual_fidelity.bat`, and `tools\validate_replay_allocation_policy.bat` |
| Rendering changes | `tools\validate_dx12_renderer.bat`; DX12/shader changes additionally run `tools\run_graphics_stress.bat 1` |
| Hot-path cost and allocation | `tools\validate_perf.bat` plus matched vehicle/non-vehicle measurements |
| Entire plan closure, after review | `tools\agent_validate.bat --plan-completion` |

The vehicle-specific test driver is a planned deliverable, not an existing
command. TV4 records its exact invocation and capability names; TV6 runs it
against the complete matrix and integrates it into the appropriate ordinary
gate. Follow `Agentic/Skills/skarness/SKILL.md`: launch Automation through
Skarness, check capabilities, assert committed state by identity, preserve
events under `TestOutput/skarness/terrain-vehicle/`, inspect screenshots, and
stop the owned session with `session.stop`. Preserve and replay any supplied
interaction manifest unchanged before and after fixes.

Baseline changes are not needed merely to add an opt-in vehicle. Preserve
existing non-vehicle oracles. If implementation produces an intentional governed
Physics change, follow the then-current `AGENTS.md` golden policy and
`Agentic/Plans/Artifacts/README.md`; a failing check alone never justifies a
refresh. No baseline authority is exercised while this plan is parked.

## Risks, Dependencies, And Estimate

The principal risks are query continuity, stiff suspension stability, contact
force timing around CCD, low-speed grip, sleep interaction, and complete
restoration of retained vehicle state. Existing rigid bodies and terrain are
the foundation; no new physics middleware or renderer is proposed. Vehicle
implementation does not require reactivating the parked selectable-solver,
stack-stability, or mechanical-suspension work.

There are no approved dependency or allocation exceptions in this plan. If
activation exposes a needed exception, record its owner, concrete reason, and
deletion condition and follow the repository decision process before use.

Initial sizing only: roughly 1-2 full-time developer weeks for a basic playable
buggy, and 3-6 weeks total for a robust terrain prototype. These are estimates
from source inspection, not delivery promises; complete replay/prediction,
schema, and terminal acceptance may extend them. Re-estimate after TV0 against
the frozen scope and measured integration cost. Detailed physical suspension
and realistic tyre/drivetrain simulation need a separate future plan.

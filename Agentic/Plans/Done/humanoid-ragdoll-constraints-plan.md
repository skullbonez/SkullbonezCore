# Humanoid Ragdoll Constraints Plan

Date: 2026-06-23
Status: Draft implementation plan for Nightrunner
Impact area: physics, ragdolls, scene tests, light editor authoring, diagnostics
Validation for this document-only change: none required

## Goal

Turn the current hacky ragdolls into production-use humanoid ragdolls with a
proper constraint lineup, stable sleep behavior, and enough joint resistance to
make them funny and physical without turning the project into a full generic
articulated-body system.

The target experience is simple: humanoid ragdolls can be placed in scenes,
shot, stacked, thrown into water, crushed by boxes, and left to settle without
exploding, jittering forever, or flying upward. They do not need animation
authoring, get-up behavior, network replication, or a general editor for
arbitrary machines.

## User Decisions Captured

| Topic | Decision |
|-------|----------|
| Product target | Humanoid ragdolls, not a generic articulated constraint system. |
| Animation | No animation support and no get-up system for this plan. |
| Constraint lineup | Ball socket, hinge, cone twist, slider, six degrees of freedom, angle limits. |
| Motors | Not required for the first production version. Leave clean extension points only. |
| Breakable constraints | Not required for the first production version. Leave clean extension points only. |
| Physical feel | Add resistance/damping so ragdolls are not uselessly floppy. Accuracy matters enough to look believable, but fun impacts matter more than biomechanical precision. |
| Editor tooling | Keep it light. Agents can author presets and scene files directly. |
| Determinism/network | No special network replication work. Existing validation determinism still must remain intact. |
| Water | Ragdolls should fall into water, float, and remain visually lively rather than sleeping underwater. |

## Non-Goals

- Do not build a full author-facing generic constraint graph editor.
- Do not add animation blending, procedural get-up, pose matching, or authored
  animation import.
- Do not add motors unless a later task explicitly asks for driven joints.
- Do not add breakable constraints unless a later task explicitly asks for
  destructible ragdolls.
- Do not prioritize network replication, rollback serialization, or multiplayer
  determinism beyond preserving current validation behavior.
- Do not rewrite the rigid body solver wholesale. Extend the existing solver
  pipeline in small, testable slices.

## Current Starting Point

The current ragdoll work already provides useful scaffolding:

- `Ragdoll` can create simple humanoid body parts.
- Point-joint style constraints keep the hacky ragdoll connected.
- Constraint-aware sleep islands prevent settled ragdolls and piles from
  jittering forever.
- Ragdoll test scenes cover sleeping piles, mixed box/ragdoll piles, water
  floating, and a shooting sandbox.
- Physics prediction and command-line diagnostic capture already know how to
  run scenes and collect solver traces.

This plan should evolve that foundation instead of deleting it. The point-joint
path can become the first compatibility layer over the new constraint model,
then the humanoid preset can migrate joint-by-joint to richer constraints.

## Design Direction

Build a small constraint framework inside the physics system, but scope the
public product surface to humanoid ragdolls.

The engine still benefits from shared internals:

- one constraint descriptor format,
- one runtime constraint state array,
- one solver-row representation,
- one island/wake/sleep integration path,
- one debug/SkullScope reporting path.

That does not mean exposing a generic user-facing constraint editor. The first
real customer is `Ragdoll`, and the shipped presets should be named humanoid
joint presets such as neck, spine, shoulder, elbow, wrist, hip, knee, and ankle.

## Core Data Model

Add a constraint model that separates authoring data from per-frame solver
state.

Suggested source files:

- `SkullbonezSource/Physics/PhysicsConstraint.h`
- `SkullbonezSource/Physics/PhysicsConstraint.cpp`
- `SkullbonezSource/Physics/PhysicsConstraintSolver.h`
- `SkullbonezSource/Physics/PhysicsConstraintSolver.cpp`
- `SkullbonezSource/Physics/HumanoidRagdollPreset.h`
- `SkullbonezSource/Physics/HumanoidRagdollPreset.cpp`

Suggested core types:

```cpp
enum class PhysicsConstraintType
{
    BallSocket,
    Hinge,
    ConeTwist,
    Slider,
    SixDof
};

enum class ConstraintAxisMode
{
    Free,
    Locked,
    Limited
};

struct PhysicsConstraintLimit
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float softness = 0.0f;
    float damping = 0.0f;
    float restitution = 0.0f;
};
```

The exact names can change to match local style, but keep the concepts:

- body A and body B indices,
- local anchor on each body,
- local joint frame on each body,
- linear axis modes,
- angular axis modes,
- linear limits,
- angular limits,
- damping/resistance,
- warm-start impulse state,
- debug name or stable id for diagnostics.

### Local Frames

Store anchors and axes in each body's local space.

The solver should derive world anchors and world axes every fixed tick from
the current body transforms. This avoids scene-authored joints drifting when
the ragdoll starts rotated, is placed in the editor, or is spawned into a
non-default pose.

### Extension Points

Reserve descriptor fields for future motors and breakage only if they stay
inactive:

- `motorEnabled = false`
- `breakEnabled = false`
- `breakImpulseThreshold = 0.0f`

Do not implement motor solving or break events in this plan. The fields are
only useful if they keep later file-format changes smaller.

## Solver Model

Use velocity-level sequential impulses, matching the current solver family.

Each constraint should emit one or more scalar rows:

- Jacobian terms for linear and angular velocity.
- Effective mass.
- Bias for positional/angular error correction.
- Lower and upper impulse bounds.
- Accumulated impulse for warm starting.
- Optional damping/resistance term.

The solver should run after contacts are generated and before final sleep
classification. The current rough order should become:

1. Apply forces.
2. Integrate candidate velocities.
3. Detect contacts.
4. Build contact rows.
5. Build constraint rows.
6. Solve contacts and constraints with stable iteration order.
7. Apply positional correction only where needed.
8. Update sleep islands, including constraint graph edges.
9. Emit bounded diagnostics.

The implementation can initially solve all constraints in a serial pass. Do
not parallelize this work until behavior is stable and validated.

## Constraint Types

### Ball Socket

Purpose: keep two anchors coincident while allowing free rotation.

Use for:

- neck/head in the first simple preset,
- loose spine joints,
- wrists/ankles if richer limits are not ready.

Rows:

- three linear rows that remove anchor separation along world X/Y/Z or an
  orthonormal joint basis.

Tuning:

- moderate positional correction,
- optional angular damping between the bodies to reduce uncontrolled spinning.

Acceptance:

- connected bodies do not visibly separate under gravity or bullet hits,
- stacked ragdolls settle into sleep islands,
- solver does not inject upward energy.

### Hinge

Purpose: allow rotation around one axis while locking the other two angular
axes and keeping anchors together.

Use for:

- elbows,
- knees.

Rows:

- ball-socket anchor rows,
- two angular lock rows for non-hinge axes,
- one angular limit row when the hinge angle crosses min/max.

Tuning:

- knees and elbows should have asymmetric limits,
- add damping around the hinge axis so limbs do not buzz at the limit.

Acceptance:

- elbows and knees bend in the expected direction,
- limbs do not twist through themselves,
- limit hits look damped rather than springy/explosive.

### Cone Twist

Purpose: allow swing inside a cone plus limited twist around the primary axis.

Use for:

- shoulders,
- hips,
- optional neck upgrade.

Rows:

- ball-socket anchor rows,
- swing limit row when the child axis leaves the cone,
- twist limit row when relative twist exceeds min/max.

Tuning:

- shoulders need wider cones than hips,
- twist should be tighter than swing,
- add resistance so arms do not spin freely after impacts.

Acceptance:

- shoulders can flop broadly without detaching,
- hips allow ragdolls to fold and tumble,
- neither joint type becomes a high-energy spinner.

### Slider

Purpose: allow translation along one axis while locking the other five degrees
of freedom, with optional min/max travel.

Use for:

- not required by the humanoid preset initially,
- useful as a testbed and future prop constraint.

Rows:

- two linear lock rows,
- three angular lock rows,
- one linear limit row when travel crosses min/max.

Acceptance:

- a simple slider test scene moves along one axis only,
- limits are stable under gravity and impacts.

### Six Degrees Of Freedom

Purpose: one configurable backend that can lock, limit, or free each linear
and angular axis.

Use for:

- internal testing,
- future preset experimentation,
- possible implementation backend for hinge/slider variants once stable.

Rows:

- emit rows from the configured axis modes.
- locked axes use equality rows.
- limited axes emit rows only when outside the configured range, or use soft
  limit bias near the boundary if needed.

Acceptance:

- can reproduce ball socket, hinge, and slider behavior through settings,
  even if those named constraints keep specialized code for clarity.

## Humanoid Preset

Keep the visible ragdoll API humanoid-first.

Suggested preset schema:

```cpp
struct HumanoidRagdollPreset
{
    float totalHeight = 70.0f;
    float torsoMass = 8.0f;
    float headMass = 3.0f;
    float upperArmMass = 2.0f;
    float lowerArmMass = 1.6f;
    float upperLegMass = 4.0f;
    float lowerLegMass = 3.0f;
    float jointDamping = 0.35f;
    float limitSoftness = 0.15f;
};
```

The first production preset should include:

- torso,
- head,
- upper/lower arms,
- upper/lower legs,
- optional pelvis if the current torso-only core makes hip limits poor.

Joint mapping:

| Joint | Type | Notes |
|-------|------|-------|
| neck | Cone twist or ball socket | Start simple if cone twist is not stable yet. |
| spine/pelvis | Cone twist or six degrees of freedom | Keep torso from folding impossibly. |
| shoulders | Cone twist | Wide swing, limited twist. |
| elbows | Hinge | One bending axis with realistic min/max. |
| wrists | Ball socket | Low priority, loose limits acceptable. |
| hips | Cone twist | Moderate swing, limited twist. |
| knees | Hinge | Strong asymmetric hinge limits. |
| ankles | Ball socket or hinge | Low priority, keep stable over anatomical accuracy. |

## Scene Authoring

Keep editor work minimal for this plan. The scene system should support enough
data to place ragdolls and choose a preset:

- position,
- rotation,
- scale or height,
- preset name,
- start sleeping or awake,
- optional ragdoll group name,
- optional material/style override.

The first version can rely on agent-authored `.scene.json` files instead of a
full editor UI. If an editor affordance is added, keep it to one ragdoll place
action with a small preset dropdown.

## Sleep, Water, And Wake Rules

Constraint graph edges must participate in sleep islands.

Rules:

- bodies connected by active constraints belong to the same sleep candidate
  graph,
- waking one constrained body wakes the whole ragdoll or constrained island,
- contacts between ragdolls and boxes can merge sleep islands,
- fixed support should allow the island to sleep only when velocities and
  constraint errors are below thresholds,
- underwater or buoyant bodies should normally resist sleeping unless the
  water system later gains a deliberate "floating sleep" policy.

Water behavior should favor visual life:

- ragdolls should float or bob,
- they should not settle into a dead underwater sleep state by accident,
- constraint damping must not fight buoyancy so strongly that limbs snap or
  pull the torso below the surface.

## Physics Prediction Integration

The prediction system must simulate constraints through the same code path as
normal physics.

Implementation requirements:

- prediction must copy or reference constraint descriptors consistently,
- warm-start state must not leak from prediction back into live simulation,
- predicted wake/sleep state must not mutate live sleep islands,
- scene reload and diagnostic capture must preserve constraint data.

Acceptance:

- shooting a ragdoll with prediction enabled shows plausible predicted body
  movement,
- cancelling prediction leaves the live ragdoll unchanged,
- committing an action wakes the same constrained island live and predicted.

## Diagnostics

Add SkullScope-friendly constraint diagnostics only after the solver behavior
exists. Keep output bounded.

Useful records:

- constraint descriptor summary,
- per-frame max constraint error,
- per-frame max limit violation,
- per-frame max accumulated impulse,
- sleeping blocked by constraint error,
- constraint island id and body membership.

Do not dump every row for every joint by default. Add focused queries later if
debugging requires them.

## Implementation Phases

### Phase 0: Audit Current Ragdoll And Constraint Touchpoints

Goal: identify exact integration points before changing solver behavior.

Steps:

1. Read `Ragdoll`, `PhysicsWorld`, `SimulationSystem`, sleep island code,
   scene loading, prediction, and existing ragdoll scenes.
2. Identify where point joints are stored, solved, woken, and included in
   sleep islands.
3. Identify how prediction clones or steps physics state.
4. Write a short implementation note in the PR or handoff naming the exact
   files that will own constraints.

Acceptance:

- The worker can explain the current point-joint pipeline.
- The worker can name where constraints must be copied for prediction.

Validation while iterating:

- no repository validation required for the audit itself.

### Phase 1: Constraint Data Model

Goal: add descriptors and runtime state without changing behavior.

Steps:

1. Add constraint descriptor and runtime state types.
2. Add a collection owner in the physics world or model collection, whichever
   best matches current ownership.
3. Add conversion from existing point joints into ball-socket-style
   descriptors if useful.
4. Add clear/reset/copy paths for scene reload, prediction, and diagnostics.
5. Add learning headers and concept comments following
   `Agentic/Reference/comment-style-guide.md`.

Acceptance:

- Existing ragdoll scenes still load.
- Existing point-joint behavior is unchanged.
- Prediction state construction still compiles and runs.

PR gate if committed alone:

- `tools\validate_fast.bat` if behavior truly does not change.
- `tools\validate_physics.bat` if any physics stepping path changes.

### Phase 2: Solver Row Foundation

Goal: create the row machinery needed by all joint types.

Steps:

1. Add a constraint-row struct compatible with the current contact solver
   style.
2. Implement effective mass and impulse application for two rigid bodies.
3. Add warm starting for constraints.
4. Add deterministic serial iteration order.
5. Add conservative bias and damping controls.
6. Add debug asserts for invalid body ids, NaN impulses, and zero-length axes.

Acceptance:

- A disabled constraint list has no behavior impact.
- A single internal test constraint can pull two bodies together without
  injecting obvious energy.

PR gate:

- `tools\validate_physics.bat`.

### Phase 3: Ball Socket

Goal: replace the hacky point-joint connection with a stable ball socket.

Steps:

1. Build three linear rows from body-local anchors.
2. Preserve compatibility with existing ragdoll builder output.
3. Tune positional bias to avoid visible separation without violent correction.
4. Add optional relative angular damping.
5. Make ball sockets participate in sleep/wake islands.

Acceptance:

- Existing ragdoll fall/sleep scenes still pass focused inspection.
- Ragdolls no longer need the old point-joint solve path except as a temporary
  fallback.
- A ragdoll hit by a bullet stays connected and wakes as one constrained group.

PR gate:

- `tools\validate_physics.bat`.
- `tools\validate_physics_deep.bat` if solver baselines or SkullScope baselines
  are intentionally refreshed.

### Phase 4: Hinge With Angle Limits

Goal: make elbows and knees bend along one axis.

Steps:

1. Add hinge frame construction in the humanoid preset.
2. Add angular lock rows for the two forbidden axes.
3. Add hinge angle measurement in the joint frame.
4. Add min/max limit rows with damping near the limit.
5. Tune elbow and knee presets separately.

Acceptance:

- Knees and elbows bend in the intended direction.
- Limbs do not spin around their long axis under ordinary impacts.
- Limit hits do not launch the ragdoll.

PR gate:

- `tools\validate_physics.bat`.

### Phase 5: Cone Twist

Goal: make shoulders and hips physically readable.

Steps:

1. Implement swing cone measurement from parent and child joint frames.
2. Implement twist extraction around the primary axis.
3. Add swing and twist limit rows.
4. Tune shoulder and hip presets for broad but bounded motion.
5. Add damping/resistance so impacts look heavy, not weightless.

Acceptance:

- Shoulders and hips allow broad ragdoll motion without detachment.
- Ragdolls fold and tumble believably when shot or dropped.
- No high-energy spin develops during pile settling.

PR gate:

- `tools\validate_physics.bat`.
- `tools\validate_full.bat` if scene/render/debug changes are included.

### Phase 6: Slider And Six Degrees Of Freedom

Goal: complete the requested constraint lineup without making humanoid ragdolls
depend on slider behavior.

Steps:

1. Add slider as a focused constraint type.
2. Add one small test scene for slider limits.
3. Add six degrees of freedom with axis modes for linear and angular axes.
4. Verify six degrees of freedom can reproduce simple named constraints in
   controlled scenes.
5. Keep public ragdoll presets on named humanoid joint types for clarity.

Acceptance:

- Slider moves along only one axis and respects limits.
- Six degrees of freedom can lock, free, and limit axes independently.
- Humanoid ragdolls remain tuned through named preset joints.

PR gate:

- `tools\validate_physics.bat`.

### Phase 7: Humanoid Presets And Scene Coverage

Goal: ship the usable ragdoll experience.

Steps:

1. Add at least one default humanoid preset.
2. Add optional heavier/lighter/funnier preset variants only if tuning time
   allows.
3. Update the ragdoll scenes to use production constraints.
4. Add focused scenes:
   - `ragdoll_constraints_ball_socket.scene.json`,
   - `ragdoll_constraints_hinge.scene.json`,
   - `ragdoll_constraints_cone_twist.scene.json`,
   - `ragdoll_constraints_slider.scene.json`,
   - `ragdoll_constraints_six_dof.scene.json`,
   - updated pile, water, and shooting scenes.
5. Keep `ragdoll_fun` as the human-facing sandbox.

Acceptance:

- Several ragdolls can fall onto each other and sleep as one island.
- Ragdolls and boxes can settle together.
- Ragdolls float in water without accidental underwater sleep.
- The shooting scene can wake sleeping standing ragdolls and knock them into
  water.
- Existing prediction tools can show ragdoll outcomes.

PR gate:

- `tools\validate_physics.bat`.
- `tools\validate_dx12_renderer.bat` if visual baselines or renderer-visible
  behavior changes.
- `tools\validate_full.bat` for mixed runtime, physics, and scene-system work.

### Phase 8: Tuning, Diagnostics, And Handoff

Goal: make the implementation maintainable after the first working version.

Steps:

1. Tune limits and damping from focused scenes.
2. Add bounded diagnostics for max constraint error and sleep blockers.
3. Document preset values in the plan or a follow-up report.
4. Move this plan to `Agentic/Plans/Done/` only after implementation, testing,
   and validation are complete.

Acceptance:

- A future agent can adjust joint values without reverse engineering the solver.
- Constraint diagnostics identify which joint blocks sleep or explodes.
- The final PR notes state exactly which validation scripts passed.

## Validation Strategy

This plan is documentation-only, so creating it requires no validation.

Implementation is physics work. Use targeted builds and scene launches while
iterating only when they answer a specific question. Before PR-bound commits:

| Change | Required PR Gate |
|--------|------------------|
| Descriptor-only refactor with no stepping behavior | `tools\validate_fast.bat` |
| Constraint solver, ragdoll physics, sleep, prediction | `tools\validate_physics.bat` |
| Physics baseline refresh or SkullScope baseline refresh | `tools\validate_physics_deep.bat` |
| Runtime scene/editor changes mixed with physics | `tools\validate_full.bat` |
| Debug overlay or renderer-visible validation change | `tools\validate_dx12_renderer.bat` |
| Hot-path allocation or performance-sensitive solver work | `tools\validate_perf.bat` |

Any physics baseline update must come from the final Debug executable and then
rerun the matching physics gate after copying the baseline.

## Definition Of Done

The production ragdoll constraint implementation is done when:

- humanoid ragdolls use named production constraints rather than hacky point
  joints,
- ball socket, hinge, cone twist, slider, six degrees of freedom, and angle
  limits exist and have focused test scenes,
- humanoid presets use appropriate joint types for head, torso, arms, and legs,
- joint damping/resistance is tunable per preset,
- sleep islands include constraint edges and wake whole constrained ragdolls,
- water scenes keep ragdolls floating and lively,
- prediction simulates constraints through the same code path without mutating
  live state,
- no motors, breakage, animation, or network replication scope has leaked into
  the first implementation,
- required validation gates pass with quoted output in the handoff.

## Nightrunner Implementation Notes

- Use `Agentic/Skills/orchestrator/SKILL.md` when turning this plan into code.
- Keep the first implementation serial and deterministic in ordering.
- Prefer obvious, named humanoid preset code over a clever generic authoring
  surface.
- Do not add broad editor tooling unless the implementation is already stable.
- Follow `Agentic/Reference/comment-style-guide.md` for new or meaningfully
  touched physics files.
- Keep comments focused on concepts, invariants, hazards, and validation rules.
- If a joint explodes, first reduce bias/softness and inspect constraint error
  through SkullScope before adding more iterations.
- If a ragdoll will not sleep, inspect constraint error and contact support
  before weakening sleep thresholds globally.
- If water ragdolls feel dead, keep underwater sleep inhibited and tune damping
  locally rather than disabling sleep for all ragdolls.

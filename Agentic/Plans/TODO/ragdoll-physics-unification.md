# Future Physics — Multi-Mode Collision & Ragdoll Unification

Date: 2026-08-22
Status: Future and unregistered. 0/4 candidate phases complete.
Impact area: collision detection modes (Discrete, Swept TOI, Speculative), dynamic velocity promotion threshold, ragdoll point joints, joint compliance, shared constraint iteration, physics baselines and tests
Owner: Physics contact and joint solver
Priority: Not selectable

## Owner Direction

This file deliberately floats under `Agentic/Plans/TODO/` without an entry in
`Agentic/Plans/MASTER-PLAN.md`. It is a detailed design shelf, not live work. A
plan runner must ignore it until the owner explicitly registers it in the
master ledger, assigns a commit token and binding order, and approves the first
behavior transition.

The active Catto Divergence Repairs plan retained the existing partial-time CCD
architecture and limited ragdoll work to stage (a), accumulated impulse caching
and warm start. This future plan organizes the complete next-generation physics
upgrade:

1. **Tri-Mode Collision Architecture:** Establish three explicit collision detection
   modes (**Discrete**, **Continuous Swept TOI**, and **Continuous Speculative**).
   Retain Swept TOI for fast bouncy projectiles with sub-frame bounce fidelity; add
   dynamic velocity promotion from Discrete to Swept TOI; and assign Speculative
   CCD by default to ragdolls and articulated bodies.
2. **Ragdoll & Joint Solver Unification:** Complete ragdoll stages (b) through (d):
   true 3-DOF point-to-point constraint blocks, principled physical compliance and
   damping, and unified contact/joint Projected Gauss-Seidel (PGS) iteration.

These tracks can be registered independently or in sequence. Registering one does
not authorize another, and none carries baseline-refresh authority while this file
is unregistered.

## Evidence To Capture Before Registration

Before activating any phase, record fresh evidence against the then-current
tree:

- The active Catto plan's R4 local-interval results, including tunnelling,
  friction, penetration, and performance measurements.
- R2(a)'s ragdoll-sag result, accumulated-impulse history, joint error by axis,
  iteration count, and contact-versus-joint impulse interaction.
- Current byte-exact physics, replay-fidelity, allocation, and performance gate
  results. Historical green output does not approve a future transition.
- Current ownership and complexity inventories for the exact solver and ragdoll
  surfaces that would change.

---

## FP1 — Tri-Mode Collision Hierarchy & Speculative Contacts

### What This Aims To Solve

Rigid bodies have differing collision requirements:
- 95%+ of bodies (crates, barrels, resting debris, terrain) are slow-moving and
  perform best with standard, cheap **Discrete** collision.
- High-speed bouncy projectiles (bullets, cannonballs, bouncing spheres) require
  sub-frame temporal accuracy and exact restitution via **Continuous Swept TOI**.
- Articulated ragdolls, animated kinematic obstacles, and fast limbs fail under
  both Discrete (limb tunneling, wall snagging, joint explosion) and Swept TOI
  (micro-substepping desynchronizes joint networks and explodes CPU cost).

FP1 introduces a unified tri-mode collision classification, implements
**Speculative Contacts** for articulated/kinematic bodies, and adds dynamic
velocity-based promotion from Discrete to Swept TOI.

### Proposed Solution

1. **Define Explicit Collision Modes:**
   Add `CollisionDetectionMode` to body/collider properties:
   - `Discrete`: Standard static overlap / SAT / GJK tests at step boundaries.
   - `ContinuousSwept` (TOI): Exact continuous swept-volume advancement with
     sub-step restitution for fast, elastic projectiles.
   - `ContinuousSpeculative`: Proactive velocity clamping for ragdolls, limbs,
     and moving platforms to ensure zero tunneling without sub-stepping.
2. **Dynamic Velocity Promotion (Discrete -> Swept TOI):**
   - For bodies in `Discrete` mode, evaluate motion each tick: $d = \|\mathbf{v}\| \Delta t$.
   - If $d > \alpha \cdot r_{\text{min}}$ (where $r_{\text{min}}$ is the minimum
     bounding radius / thickness and $\alpha \approx 0.5$ is the safety margin),
     dynamically promote the body to `ContinuousSwept` for that tick.
   - Return to `Discrete` as soon as velocity drops below the threshold.
3. **Default Ragdolls to Speculative CCD:**
   - Authoring and spawning pipelines tag all ragdoll body records and colliders
     as `ContinuousSpeculative` by default.
4. **Produce Trustworthy Predictive Geometry:**
   - For pairs involving `ContinuousSpeculative` bodies, extend swept broadphase
     bounds along relative velocity.
   - Compute closest points, separating normal, signed separation $s$, and
     conservative angular motion.
5. **Admit Speculative Constraint Rows:**
   - For positive separation $s$ and closing normal velocity $v_n < 0$, admit a
     speculative row if $s + v_n \Delta t < 0$.
   - Target velocity: $v_n \ge -s / \Delta t$.
   - Clamp impulse unilaterally ($\lambda \ge 0$) so speculative contacts only
     brake closing speed and never pull separating bodies together.
6. **Preserve Impact & Friction Boundaries:**
   - Disable friction constraints while separated ($s > 0$) to prevent ghost drag.
   - Fire restitution only when actual touching contact occurs ($s \le 0$),
     preserving authentic inelastic resting for ragdolls.
7. **Integrate at Uniform $\Delta t$:**
   - Speculative contacts solve within the shared step without sub-stepping,
     keeping all 15–20 ragdoll limbs on the exact same time boundary.

### Hazards And Required Tests

- Near-miss and grazing trajectories must not produce ghost collisions or mid-air deflection.
- Fast rotation must not tunnel a long hull corner; conservative angular bounds must be tested.
- Static friction must engage only upon physical surface contact ($s \le 0$).
- High-speed discrete bodies must trigger dynamic promotion to Swept TOI without dropping frames.
- Fast bouncy projectiles in Swept TOI mode must retain exact sub-frame bounce timing and energy.
- Replay capture and restore must serialize and reproduce all three collision modes byte-identically.

### FP1 Acceptance

- All three modes (`Discrete`, `ContinuousSwept`, `ContinuousSpeculative`) function correctly.
- Ragdoll limbs swinging at $50\text{ m/s}$ do not tunnel through thin static or dynamic walls.
- Fast discrete bodies automatically promote to Swept TOI when exceeding the motion threshold.
- Fast bouncy projectiles in Swept TOI mode continue passing existing exact baseline tests.
- Replay, deterministic 0/1/4-worker checks, and performance benchmarks pass.

---

## FP2 — Ragdoll Stage (b): Three-Degree-Of-Freedom Point Joint

### What This Aims To Solve

The current joint is one scalar row along the present anchor-error direction.
It primarily corrects distance and cannot simultaneously constrain all three
components of anchor-relative velocity. Near zero error the preferred axis also
becomes poorly defined. R2(a) adds temporal coherence to that scalar model, but
warm start alone does not turn it into a true point-to-point constraint.

Stage (b) aims to pin the two world-space anchors to coincidence in all three
linear dimensions while still allowing the bodies to rotate around the joint.

### Proposed Solution

1. Build the two anchor arms from each center of mass and compute anchor
   velocities including angular velocity cross arm.
2. Form the 3-by-3 point-constraint effective-mass matrix from inverse masses,
   world-space inverse inertias, and the two skew-symmetric anchor-arm terms.
3. Solve one vector impulse for the three coupled axes. Use deterministic,
   explicitly guarded singular handling for fixed or nearly singular bodies;
   do not hide failure behind an unconstrained matrix inverse.
4. Replace the scalar cached impulse introduced by R2(a) with an accumulated
   vector impulse owned by the stable constraint handle. Define the coordinate
   space and lifetime of that cache and warm-start both linear and angular body
   velocity from it.
5. Clamp only against a physically named joint-force or impulse limit if such a
   limit is part of the authored constraint. Do not independently clamp the
   three vector components, which would make the result basis-dependent.
6. Preserve a focused diagnostic trail containing vector error, vector impulse,
   effective-mass conditioning, and per-iteration residual without allocating
   in the solver hot path.

### FP2 Acceptance

- A joint under off-axis load controls all three anchor-error components rather
  than merely preserving anchor distance.
- The result is invariant under equivalent world-axis rotations of the fixture.
- Fixed/dynamic, dynamic/dynamic, zero-length error, and near-singular inertia
  cases have focused tests.
- Ragdoll sag, jitter, energy, and iteration cost are measured against both the
  pre-R2(a) solver and the completed R2(a) state.
- The owner reviews the exact physics and replay baseline transition before any
  replacement golden is committed.

## FP3 — Ragdoll Stage (c): Explicit Softness Model

### What This Aims To Solve

The current expression `(relativeVelocity + biasSpeed) * (1 + damping)` mixes
positional recovery and damping without stable physical units. Its apparent
stiffness changes with iteration count and fixed-step duration, and the
`damping` value is not recognizably a damping ratio, decay rate, or force.

Stage (c) aims to make joint stiffness and damping explicit, tunable, and
predictable across the supported fixed-step envelope.

### Proposed Solution

1. Use FP2 measurements to choose one named model rather than blending two:
   - a hard point constraint with bounded Baumgarte positional bias; or
   - a soft constraint expressed through natural frequency and damping ratio,
     converted to solver bias and compliance for the current `dt`.
2. For a soft constraint, add compliance to the 3-by-3 effective-mass solve in a
   unit-consistent form and include the accumulated impulse term required by the
   formulation. Document every coefficient's units and limiting behavior as
   stiffness approaches zero or a hard constraint.
3. Apply damping to relative anchor motion through the constraint equation, not
   as a post-solve velocity multiplier or hidden energy sink.
4. If authored settings change meaning, use the repository's versioned schema
   migration process. Do not silently reinterpret existing scene values.
5. Measure step-size and substep sensitivity at the supported fixed steps, and
   record whether the chosen parameters represent frequency/damping ratio,
   compliance, or direct solver coefficients.

### FP3 Acceptance

- Joint parameters have documented units and one owner.
- Equivalent physical settings produce bounded, explained behavior across the
  supported fixed-step sizes and iteration counts.
- Energy decay is attributable to the selected damping model and visible to
  diagnostics.
- Ragdoll load, free swing, impact recovery, and long-rest tests distinguish the
  new model from the removed ad-hoc multiplier.
- Any authored-data migration and exact baseline transition receive separate
  owner approval.

## FP4 — Ragdoll Stage (d): Shared Contact And Joint Iteration

### What This Aims To Solve

Contacts currently solve first and point joints solve in a separate pass. A body
that is both supported by contact and constrained by a joint can be pushed by
one family and pulled back by the other without either family seeing the newest
impulse until the next outer step. The result can be iteration-order-dependent
softness, sag, or contact/joint fighting.

Stage (d) aims for each PGS sweep to observe both contact and joint responses,
so one convergence process owns the coupled constrained system.

### Proposed Solution

1. Introduce a concrete constraint-solve transaction that owns preparation,
   warm start, iteration order, convergence observation, writeback, and phase
   legality. Do not introduce a solver-wide context bag, callback pack,
   polymorphic row hierarchy, or owner reach-back.
2. Keep contact scalar rows and joint 3-by-3 blocks in compact typed arrays. A
   deterministic schedule may visit typed arrays through tagged indices or
   explicit loops; the shared property is the sweep and body-velocity state,
   not a lowest-common-denominator data bag.
3. Build deterministic islands or another explicit ownership boundary so only
   constraints connected through bodies share convergence authority. Preserve
   stable ordering by body identity, constraint handle, contact pair, and feature
   identity.
4. Warm-start both families before iteration. During every sweep, apply contact
   normal/friction rows and joint blocks against the same current body-velocity
   scratch, respecting contact normal-to-friction dependencies.
5. Define convergence across scalar contact impulses and vector joint impulses
   without summing unrelated row counts into a scene-size-dependent criterion.
6. Publish typed diagnostics that identify family, owner, row/block identity,
   residual, and impulse while preserving zero steady-state allocation.
7. Delete the old post-contact ragdoll solve pass once equivalence and intended
   behavioral differences are proven. A forwarding wrapper would leave the
   split authority in place and does not close this phase.

### FP4 Acceptance

- Focused fixtures show that a contacted, joint-constrained body converges as one
  system rather than alternating between two independent passes.
- Joint stiffness and contact support are stable under deterministic constraint
  ordering, supported worker counts, and ordinary scene-size changes.
- The solve transaction enforces legal phase order and passes the repository's
  aggregate, extraction-scar, wide-signature, complexity, and ownership review.
- Hot-path storage remains fixed or pre-reserved with no new runtime allocation
  privilege.
- Physics, replay visual fidelity, performance, and full validation pass after
  an owner-approved exact baseline transition.

## Candidate Phase Order

- [ ] **FP1 — Tri-Mode Collision Hierarchy & Speculative Contacts.** Establish
  Discrete, Swept TOI, and Speculative modes with dynamic velocity promotion and
  ragdoll speculative defaults.
- [ ] **FP2 — Ragdoll 3-DOF point joint.** Pin linear anchor coincidence with
  3x3 effective mass and 3D vector warm starting.
- [ ] **FP3 — Explicit ragdoll softness.** Principled frequency/damping compliance
  across timestep and iteration variations.
- [ ] **FP4 — Shared contact/joint iteration.** Unified PGS sweeps eliminating
  pass fighting between ragdoll ground contacts and joint pulls.

FP1 may be registered independently of FP2 through FP4. The ragdoll phases are
strictly ordered and each carries a stop-and-review boundary.

## Non-Goals While Unregistered

- Do not select or implement any checkbox in this file.
- Do not add this file to `Agentic/Plans/MASTER-PLAN.md` or binding order without
  a new explicit owner direction.
- Do not refresh physics, replay, SkullScope, visual, or performance baselines.
- Do not discard Swept TOI continuous collision detection.
- Do not import Box2D, Bullet, or another engine's source. Primary literature
  may guide the derivation, but repository code owns its contracts.

## Validation Map After Registration

| Future change | Required pre-commit evidence |
|---|---|
| Collision modes, velocity promotion, speculative rows | Focused CCD tests; `tools\validate_physics.bat`; `tools\validate_physics_deep.bat`; `tools\validate_perf.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_full.bat --plan-completion` only at plan closure |
| Ragdoll FP2 or FP3 | Focused ragdoll and solver tests; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat` |
| Shared solver FP4 | Focused contact/joint tests; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat`; `tools\validate_full.bat --plan-completion` only at plan closure |
| Authored joint/collision schema change | Versioned migration tests; `tools\migrate_data_formats.py --check`; mapped physics and full gates |
| Documentation-only refinement | No validation required |

## Registration And Closure Conditions

To activate work, split FP1 and the FP2-FP4 ragdoll sequence into separately
owned plans if their evidence, baseline transitions, or delivery schedules have
diverged. Register only the plan the owner actually intends to execute. The
registered plan must add current evidence, exact task counts, a commit token,
binding order, per-transition baseline rulings, and any exception inventory the
then-current repository contract requires.

This file may be deleted once its candidate work has either moved into
registered plans or been explicitly declined. Git history remains the archive.

## Reference Sites

- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- `SkullbonezSource/Physics/Ragdoll.cpp`
- `SkullbonezSource/Physics/PhysicsHandles.h`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf`
- `Agentic/Reference/physics-overview.md`

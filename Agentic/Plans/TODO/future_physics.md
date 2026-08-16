# Future Physics

Date: 2026-08-16
Status: Future and unregistered. 0/4 candidate phases complete.
Impact area: continuous collision detection, contact-row time semantics,
ragdoll point joints, shared constraint iteration, physics baselines and tests
Owner: Physics contact and joint solver
Priority: Not selectable

## Owner Direction

This file deliberately floats under `Agentic/Plans/TODO/` without an entry in
`Agentic/Plans/MASTER-PLAN.md`. It is a detailed design shelf, not live work. A
plan runner must ignore it until the owner explicitly registers it in the
master ledger, assigns a commit token and binding order, and approves the first
behavior transition.

The active Catto Divergence Repairs plan retains the existing partial-time CCD
architecture and limits ragdoll work to stage (a), accumulated impulse caching
and warm start. This future plan preserves the two larger ideas excluded by that
ruling:

1. Replace time-of-impact position advancement with speculative contacts.
2. Continue ragdoll repair stages (b) through (d) after stage (a) has produced
   measured evidence.

These are independent projects. Registering one does not authorize the other,
and neither carries baseline-refresh authority while this file is unregistered.

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

## FP1 — Speculative Contacts

### What This Aims To Solve

The current CCD path advances a body to its detected time of impact, redirects
velocity through the contact solver, and integrates that velocity through the
remaining fraction of the fixed step. That design is valid and intentionally
retained by the active R4 repair. Its structural costs are nevertheless worth a
separate future evaluation:

- Bodies can enter the shared solve at different positions within the same
  fixed tick, so time-scaled row terms need explicit interval semantics.
- Collision discovery happens before the shared velocity response. A response
  that creates a second impact during the remaining interval may not place that
  second contact in the same solve transaction.
- Sequential time-of-impact processing resists broad parallelism when extended
  to complete collision chains.
- Per-body `m_timeRemaining` state crosses narrowphase, terrain, integration,
  sleep, replay capture, and restore, increasing the proof surface.

Speculative contacts aim to keep every body at one common step boundary. A
contact row is created before overlap when current separation and closing speed
show that the shapes would cross during the fixed step. The solver then limits
closing velocity so ordinary uniform integration reaches contact without
tunnelling.

### Proposed Solution

1. **Produce trustworthy predictive geometry.** Extend the swept broadphase only
   as necessary to identify candidate pairs. At the current poses, compute the
   closest points, separating normal, signed separation, feature identity, and a
   conservative angular-motion contribution. Do not manufacture a speculative
   manifold from an AABB overlap alone.
2. **Admit a speculative row by an end-step test.** For relative normal speed
   `vn`, positive separation `s`, and step `dt`, admit the row only when the
   conservative predicted separation can cross the contact skin during the
   step. The row target permits approach no faster than the rate that consumes
   the available separation: conceptually `vn >= -s / dt`, with the repository's
   sign convention stated beside the implementation.
3. **Keep the constraint unilateral.** A speculative normal impulse may prevent
   closing but may never pull separating bodies together. Preserve the
   non-negative accumulated normal impulse and prove that a near miss, grazing
   pass, or normal flip does not create a ghost collision.
4. **Separate pre-contact from impact policy.** Do not apply restitution merely
   because a speculative row exists. Define the exact boundary at which an
   actual impact becomes eligible for restitution. Likewise, prove whether
   friction begins only at touching contact or can be safely bounded during the
   speculative interval without producing drag at a distance.
5. **Solve predicted chains together.** Include speculative rows in the same
   deterministic constraint graph as ordinary contacts so PGS iterations can
   transmit an impulse through a bullet, struck body, and downstream obstacle
   before positions integrate. Preserve canonical pair and feature ordering
   across serial and worker-assisted preparation.
6. **Integrate once with one fixed interval.** After the shared solve, advance
   every awake body through the same `dt`. Remove time-of-impact advancement and
   `m_timeRemaining` only after object, terrain, sleep, wake, replay, and
   diagnostics consumers have proven they no longer need it. Do not leave a
   compatibility clock beside the new path.
7. **Adopt incrementally behind behavioral comparison.** Begin with the
   high-speed object/object and terrain cases that currently require CCD. Keep a
   test-only comparison path long enough to explain differences, but do not ship
   two runtime CCD authorities or a permanent mode switch.

### Hazards And Required Tests

- Near-miss and grazing trajectories must not receive impulses.
- Fast rotation must not tunnel a long hull corner through another shape.
- Restitution must occur once at impact, not while separated and not once per
  speculative frame.
- Static friction must still hold after contact without slowing separated
  bodies.
- A projectile striking a dynamic body that then strikes a third body must
  produce the intended same-step chain response.
- Terrain, fixed/dynamic, dynamic/dynamic, sleeping-body wake, and one-sided
  surface rules need focused coverage.
- Contact identity and warm-start lifetime must remain deterministic when a row
  transitions from speculative separation to touching contact.
- Serial, zero-worker, one-worker, and four-worker output must remain byte
  identical.
- The replacement must show a measured performance benefit or a clearly stated
  correctness benefit large enough to justify the broader pipeline change.

### FP1 Acceptance

- One uniform fixed-step interval governs row setup and final integration.
- The focused CCD suite proves no tunnelling, ghost contact, premature friction,
  duplicate restitution, or missed chain reaction.
- Replay capture and restore reproduce the replacement state byte-for-byte.
- The old TOI-advance authority and obsolete remaining-time state are deleted,
  not retained behind a compatibility spelling.
- Physics, replay visual fidelity, allocation, performance, and full validation
  pass after an owner-approved exact baseline transition.

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

- [ ] **FP1 — Speculative contacts.** Independent structural CCD evaluation.
- [ ] **FP2 — Ragdoll 3-DOF point joint.** Begins only after R2(a) evidence is
  reviewed and the owner activates the ragdoll continuation.
- [ ] **FP3 — Explicit ragdoll softness.** Begins only after FP2 establishes the
  coupled vector constraint and its measurements.
- [ ] **FP4 — Shared contact/joint iteration.** Begins only after FP2 and FP3;
  the shared solver must not conceal an unresolved joint formulation.

FP1 may be registered independently of FP2 through FP4. The ragdoll phases are
strictly ordered and each carries a stop-and-review boundary.

## Non-Goals While Unregistered

- Do not select or implement any checkbox in this file.
- Do not add this file to `Agentic/Plans/MASTER-PLAN.md` or binding order without
  a new explicit owner direction.
- Do not refresh physics, replay, SkullScope, visual, or performance baselines.
- Do not retain old and new CCD or joint-solver authorities as permanent modes.
- Do not import Box2D, Bullet, or another engine's source. Primary literature
  may guide the derivation, but repository code owns its contracts.
- Do not combine speculative contacts with a ragdoll phase in one implementation
  commit or baseline decision.

## Validation Map After Registration

| Future change | Required pre-commit evidence |
|---|---|
| Speculative contact geometry or solver rows | Focused CCD tests; `tools\validate_physics.bat`; `tools\validate_physics_deep.bat`; `tools\validate_perf.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_full.bat` |
| Ragdoll FP2 or FP3 | Focused ragdoll and solver tests; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat` |
| Shared solver FP4 | Focused contact/joint tests; `tools\validate_physics.bat`; `tools\validate_replay_visual_fidelity.bat`; `tools\validate_perf.bat`; `tools\validate_full.bat` |
| Authored joint-setting schema change | Versioned migration tests; `tools\migrate_data_formats.py --check`; mapped physics and full gates |
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

# Contact Stack Stability Techniques

Date: 2026-08-02
Status: WNF — owner-parked 2026-08-02; restore to `TODO/` only by explicit
owner decision. 0/7 phases complete.
Impact area: Physics contact islands, contact solving, position correction,
deterministic scenes, tests, diagnostics, and performance
Owner: Physics contact solver
Priority: Parked follow-up

## Owner Direction

Stacking is deliberately left alone for now so Contact Energy And Warm-Start
Integrity can finish its non-stacking work. This plan preserves a future
experimental campaign based on the techniques actually used by Bullet and
Box2D. It is not active MASTER-PLAN work and grants no production-edit,
iteration-policy, or baseline-refresh authority while it remains under `WNF/`.

Source investigation:
`../../Reports/2026-08-02/contact-energy-stack-stability-reference-investigation.md`.

## Problem And Evidence

The current solver performs at most 12 flat scalar PGS sweeps over every contact.
Canonical feature identity and stable warm starts can settle eight levels, but
the required 32/64-level cold transient remains dominated by serial load
propagation. A favorable one-dimensional chain retains about 96%/99% relative
support-impulse error after 12 sweeps at 32/64 levels. Continuing to tune SAT,
cache identity, row retention, friction, seeds, or global row order cannot change
that convergence scale.

Established engines add different mechanisms:

- Bullet groups active constraints by simulation island and separates
  sufficiently deep penetration repair into split push/turn velocities before
  transform writeback.
- Box2D 2.4 warm-starts contact impulses, couples a well-conditioned two-point
  patch through a small block solve, and iterates position correction separately.
- Current Box2D uses Soft Step substeps with a bias solve, position integration,
  and a bias-disabled relaxation pass inside each substep.

Primary sources:

- [Bullet sequential impulse solver](https://github.com/bulletphysics/bullet3/blob/master/src/BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.cpp)
- [Bullet simulation islands](https://github.com/bulletphysics/bullet3/blob/master/src/BulletCollision/CollisionDispatch/btSimulationIslandManager.cpp)
- [Box2D 2.4 contact solver](https://github.com/erincatto/box2d/blob/v2.4.1/src/dynamics/b2_contact_solver.cpp)
- [Box2D 2.4 island sequencing](https://github.com/erincatto/box2d/blob/v2.4.1/src/dynamics/b2_island.cpp)
- [Box2D current solver](https://github.com/erincatto/box2d/blob/main/src/solver.c)
- [Box2D current contact solver](https://github.com/erincatto/box2d/blob/main/src/contact_solver.c)
- [Box2D Soft Step migration notes](https://github.com/erincatto/box2d/blob/main/docs/migration.md)

## Goal

Measure the cost and stability of island-local adaptive work, Bullet-style split
penetration repair, Box2D 2.4-style coupled patch/position solving, and current
Box2D-style substep/relaxation on the same deterministic 4/8/16/32/64/128 tower
fixtures. Select at most one smallest production direction whose ownership,
energy behavior, determinism, and performance are demonstrated rather than
assuming any reference technique transfers directly from 2D or Bullet's solver.

## Non-Goals

- Do not raise the production global iteration cap above 12.
- Do not bundle multiple solver techniques into one experimental result.
- Do not treat a tower-only pass as proof for the 200-box topple, impacts,
  terrain, joints, Replay, or ordinary shallow piles.
- Do not import Bullet or Box2D source, compatibility abstractions, service bags,
  callbacks, or solver-wide context objects.
- Do not call an island-local extension cheap until row visits and frame time are
  measured on representative mixed scenes.
- Do not change gravity, timestep, damping, friction, restitution, sleep policy,
  tower geometry, or acceptance thresholds to manufacture stability.
- Do not refresh any Physics, Replay, SkullScope, or visual baseline without a
  later explicit owner decision on an exact candidate transition.

## Experiment Contract

Every technique uses the same starting scenes, fixed timestep, settings, worker-
count witnesses, measurement windows, and planted energy/launch controls. Each
experiment lands as an independently revertible slice or remains outside tracked
source. Record contact rows, normal/friction row visits, base and extra passes,
support depth, residual history, penetration, launch/reversal counts, time to
sleep, body retention, total mechanical energy, solver time, whole-frame time,
and deterministic hashes.

Reject a technique immediately if it requires unbounded work, violates the
closed-solve energy/momentum oracle, changes unrelated islands, loses byte-exact
worker-count determinism, introduces post-gameplay allocation, or exceeds its
predeclared performance budget. A rejected experiment is evidence, not a reason
to tune indefinitely or combine it with the next technique.

## Phases

- [ ] **CS0 — Refresh the common evidence and cost budget.** Re-run the exact
  tower matrix and representative shallow/mixed scenes from one authoritative
  binary. Lock deterministic measurements and owner-approved absolute row-visit,
  solver-time, and frame-time stop budgets before behavior changes.
- [ ] **CS1 — Build observational contact-island depth/residual attribution.** Add
  deterministic pre-solve connected components, anchored vertical support depth,
  and island-local maximum residual reporting without changing row execution.
  Prove fixed-capacity ownership, zero shipping cost when disabled, and exact
  mapping to the later sleep-island facts.
- [ ] **CS2 — Test the island-local adaptive sweep hypothesis.** Keep 12 sweeps
  for every island. Permit only an anchored deep island with material residual to
  consume a hard-capped extension. Stop on local convergence or row-visit/time
  budget, and reject the approach if 32/64 stability is not affordable.
- [ ] **CS3 — Test Bullet-style split penetration repair independently.** Solve
  deep penetration through detached push/turn state so position repair does not
  inject kinetic velocity. Define the full 3D angular/writeback invariant and
  compare it against the unchanged 12-sweep velocity solve.
- [ ] **CS4 — Test Box2D 2.4-style patch and position techniques independently.**
  First measure a conditioned coupled normal solve for one stable 3D manifold;
  separately measure iterative position constraints. Do not treat Box2D's 2D
  two-point mini-LCP as a drop-in design for a four-point 3D patch.
- [ ] **CS5 — Test current Box2D-style substep/relaxation independently.** Measure
  contact-anchor reuse, bias-enabled solve, position integration, relaxation, and
  restitution timing under fixed total frame time. Record changed external-force,
  CCD, joint, Replay, and determinism semantics before judging viability.
- [ ] **CS6 — Select or reject the production direction.** Compare all experiments
  against the common budget and full energy/determinism gates. Obtain independent
  ownership review. Recommend at most one bounded design or record that the
  32/64 target requires a different solver/acceptance decision. Production work
  and any baseline decision require a new active TODO phase and explicit owner
  authorization.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` only when the owner explicitly resumes
stack stability experimentation. At reactivation, refresh every source
measurement and reference-implementation assumption against the then-current
repository and upstream sources before implementing CS0.

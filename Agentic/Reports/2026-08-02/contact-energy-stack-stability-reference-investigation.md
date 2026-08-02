# Contact Energy Stack-Stability Reference Investigation

Date: 2026-08-02

Plan: `../../Plans/TODO/contact-energy-and-warm-start-integrity.md`

Outcome: INVESTIGATION COMPLETE; implementation remains owner-held at 4/7

## Owner Ruling

The production solver's global cap remains 12 iterations. The owner authorized
this bounded feasibility investigation and required direct comparison with the
existing Bullet and Box2D implementations. The owner is willing to consider a
deterministic, bounded iteration increase isolated to the connected tall-stack
contact island, but explicitly did not authorize its implementation yet.

No production source, simulation setting, scene, or baseline changed during this
investigation.

## Short Answer

Stop the identity, SAT, row-retention, and global row-order tuning path for the
32/64 acceptance target. The current 12-sweep scalar PGS algorithm is not a
credible cold-start solver for 32 or 64 serial support levels, even if every
contact key is perfect. Bullet and Box2D obtain their practical stability by
changing more than the count of the same scalar sweep: they separate penetration
repair, couple manifold points, or sub-step and relax constraints.

An island-local iteration exception is technically possible and can protect the
rest of the scene from the extra work. It is not automatically cheap enough for
a 64-level chain: the reduced model below shows that a modest 12-to-24 or
12-to-48 increase still attacks the wrong convergence scale. Any implementation
proposal must therefore begin with island-local residual/depth measurement and a
hard row-visit budget, not a guessed tower-height multiplier.

## Current SkullbonezCore Solve

`PersistentContactSolveTransaction::SolveRowsIterations` performs one flat,
deterministically ordered loop over every persistent contact. Each global
iteration solves a scalar normal row and then its two tangent rows. All contacts
share the same `stepPolicy.iterations` limit, and the only early-out is the
whole-solve sum of squared impulse deltas. There is no contact-island scheduling
boundary in the solve.

Warm start applies cached scalar normal and tangent impulses before those
sweeps. Position repair is a later single direct position pass rather than an
iterated or velocity-separated constraint channel. The existing sleep-island
graph is built after contact solving and integration, so it is not a current
pre-solve scheduling input.

The ES4 experiment already supplied the production-scale control: canonical
face/corner identity plus stable two-row anchors made eight levels sleep and
kept the formerly flipping 64-level pair warm-started, yet 32 and 64 levels
still collapsed. That result rules out temporal identity as the missing source
of serial load propagation.

## Reduced-Chain Feasibility Check

This investigation used a deliberately favorable scalar model to test the
algorithm rather than the complete 3D scene:

- identical unit masses in one vertical chain;
- one normal constraint per level, with no friction or angular coupling;
- every contact present and perfectly identified from the first solve;
- zero initial impulse, one unit downward velocity increment, and bottom-up PGS
  ordering; and
- the same accumulated, non-negative normal-impulse clamp as the production
  solver.

The exact solution gives the base contact an impulse equal to the number of
dynamic levels. After 12 sweeps:

| Dynamic levels | Relative L2 error in support impulses | Base impulse / exact | Maximum residual speed |
|---:|---:|---:|---:|
| 4 | 13.8% | 3.489 / 4 | 0.154 |
| 8 | 58.9% | 3.867 / 8 | 0.701 |
| 16 | 86.3% | 3.868 / 16 | 0.981 |
| 32 | 96.0% | 3.868 / 32 | 1.000 |
| 64 | 98.9% | 3.868 / 64 | 1.000 |

The sweep count needed merely to reduce support-impulse error below 10% was 15,
58, 235, 945, and 3,795 for 4, 8, 16, 32, and 64 levels respectively. This is a
reduced diagnostic, not a prediction that the 3D engine needs those exact
counts. Its value is the scaling result: the slow serial mode grows roughly with
the square of chain depth. Real multi-point contacts, rotations, changing
geometry, penetration bias, and unilateral contact loss do not make the cold
problem easier.

Warm starting is still essential once a good support solution exists, but it
cannot supply the missing first solution before the discrete bodies acquire
penetration and velocity. The production ES4 evidence confirms that distinction:
stable cached rows improve eight levels but do not rescue the 32/64 transient.

## What Bullet Actually Does

Bullet's default sequential-impulse solver is still scalar PGS, but its default
policy is not equivalent to SkullbonezCore's current path:

- solver information defaults to 10 iterations, warm starting at 0.85, and
  split impulse enabled;
- sufficiently deep penetration past the configured negative split threshold is
  removed from the kinetic-velocity right-hand side and solved in a separate
  push/turn-velocity loop, then written back through the transform;
- active bodies and manifolds are grouped and processed by simulation island;
- optional constraint-order randomization exists but is not in the default
  solver mode; and
- typed constraints can override their iteration count, while ordinary contact
  rows still follow the global contact iteration path.

Sources: Bullet's official
[`btSequentialImpulseConstraintSolver.cpp`](https://github.com/bulletphysics/bullet3/blob/master/src/BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.cpp),
[`btContactSolverInfo.h`](https://github.com/bulletphysics/bullet3/blob/master/src/BulletDynamics/ConstraintSolver/btContactSolverInfo.h),
and
[`btSimulationIslandManager.cpp`](https://github.com/bulletphysics/bullet3/blob/master/src/BulletCollision/CollisionDispatch/btSimulationIslandManager.cpp).

The relevant lesson is split penetration repair and an island scheduling
boundary, not Bullet's optional random order. Randomization conflicts with the
repository's byte-exact determinism and does not remove the serial convergence
limit. Bullet's typed-constraint override is evidence that bounded local work is
a legitimate solver concept, but it is not a ready-made per-contact tower rule.

## What Box2D Actually Does

Box2D 2.4, the closer scalar-PGS relative, warm-starts impulses scaled by the
timestep ratio, iterates velocity constraints, then runs a separate iterative
position solve. A two-point contact manifold uses a coupled 2-by-2 mini-LCP block
solve when its effective mass is well conditioned. This reduces rocking within
one 2D contact patch, but it does not eliminate convergence through dozens of
serial body-to-body contacts.

Sources: Box2D 2.4.1's official
[`b2_contact_solver.cpp`](https://github.com/erincatto/box2d/blob/v2.4.1/src/dynamics/b2_contact_solver.cpp)
and
[`b2_island.cpp`](https://github.com/erincatto/box2d/blob/v2.4.1/src/dynamics/b2_island.cpp).

Current Box2D replaced velocity/position iteration counts with the Soft Step
sub-stepping solver. Its migration guide recommends starting at four substeps.
For each substep the source integrates velocities, warm-starts, performs one
bias-enabled solve, integrates positions, and performs one bias-disabled
relaxation solve; restitution is applied after the substeps. Contact rows use
softness mass/impulse scaling and a stiffer static-contact policy.

Sources: Box2D's official
[`migration.md`](https://github.com/erincatto/box2d/blob/main/docs/migration.md),
[`solver.c`](https://github.com/erincatto/box2d/blob/main/src/solver.c), and
[`contact_solver.c`](https://github.com/erincatto/box2d/blob/main/src/contact_solver.c).

The Box2D 2.4 block solve and current Soft Step solver are both explicitly
outside this plan's present non-goals. They are useful evidence for why the
engines remain stable without a huge global scalar-iteration count, not
permission to import either design into ES4.

## Non-Implemented Island-Local Option

A bounded SkullbonezCore experiment could isolate extra scalar work as follows:

1. Build deterministic connected components from the current persistent contact
   rows before solving. The existing sleep-island data cannot simply be borrowed
   because it is rebuilt later in the step.
2. Within each component, derive directed support depth only from terrain/fixed
   anchors and sufficiently vertical object-contact normals. A raw body count is
   not a stack classifier.
3. Give every island the unchanged 12-sweep production budget.
4. At sweep 12, consider extension only for an anchored, deep island whose own
   maximum row residual remains material. Do not extend detached collisions,
   shallow piles, or islands already converged.
5. Stop on island-local residual convergence or a hard extra row-visit budget.
   Record depth, rows, base/extra sweeps, residual, early-out reason, and elapsed
   solver time so cost cannot hide inside the frame average.
6. Preserve canonical island and row ordering, fixed-capacity storage, replay
   capture/restore semantics, and byte-exact worker-count behavior.

This design protects unrelated islands, but it cannot yet be called cheap. A
64-level two-row tower is already roughly 128 normal/friction contact rows; each
extra sweep revisits the whole connected island. The reduced chain says that a
small multiple of 12 is unlikely to converge the cold transient. The only honest
next experiment, if the owner later authorizes it, is a bounded island-only
work-versus-residual ladder with a predeclared stop condition. It must be rejected
if acceptable 32/64 behavior requires an owner-unacceptable row-visit or frame-
time cost.

## Recommendation And Stop Condition

1. Keep the global production cap at 12.
2. Do not resume ES4 identity/cache, SAT, contact-retention, friction, seed, or
   global-order experiments for the tower target.
3. Do not implement Bullet split impulse, Box2D's block solver, or Soft Step
   under the existing ES4 authorization.
4. If the owner wants one implementation experiment, separately authorize only
   deterministic contact-island construction, support-depth/residual
   attribution, and a hard-budget island-local sweep ladder. No baseline refresh
   belongs to that experiment.
5. Stop and reopen the 32/64 acceptance/solver-design decision if the measured
   island-local work exceeds its predeclared performance budget. Do not keep
   tuning the scalar 12-sweep path after that result.

The plan remains at 4/7 because this report changes neither production behavior
nor acceptance evidence. The next action is an owner decision on the narrowly
specified island-local diagnostic/experiment, not more autonomous solver
tuning.

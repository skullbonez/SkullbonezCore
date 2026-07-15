# Mutual Gravity Large-Scene Fallback — Restore The 8,192-Body Envelope

Date: 2026-07-15
Status: Active — 0/3 tasks complete
Impact area: `PhysicsWorld::PrepareMutualGravityForces`, physics scratch
reservation, determinism tests
Owner: physics

## Problem And Evidence

The round-4 gravity parallelization (`d2846bf1`) silently shrank the engine's
supported domain:

1. Before: the serial triangular loop ran for any body count up to
   `MAX_GAME_MODELS` (8,192, `SceneCapacity.h:39`); its scratch was body-count
   sized.
2. After: `PrepareMutualGravityForces` executes
   `SB_FATAL` when `modelCount > MUTUAL_GRAVITY_MAX_BODIES` (512) with mutual
   gravity enabled (`PhysicsWorld.cpp`, capacity guard). A scene with 600
   gravity-enabled bodies that simulated correctly on `nightrunner-14th-july`
   now terminates the Release process.
3. The 512 cap itself is justified — the triangular pair scratch at 8,192
   bodies would need ~402 MB — but nothing in the owning plan authorized
   shrinking the envelope, and no commit or comment records the trade as a
   deliberate owner decision.

2026-07-15 adversarial review of the round-4 claims, finding 1 (reopens the
`deterministic-parallel-mutual-gravity` closure per the review-finding rule).

## Goal

Scenes above the pair-scratch cap run the exact serial accumulation path —
bitwise-identical physics, no fatal — while scenes at or under the cap keep
the parallel pair-build. The cap becomes a documented performance threshold
instead of a hard capability limit.

## Non-Goals

- No change to forces, ordering, or results for any body count: the serial
  fallback is the original triangular accumulation and must be bit-identical
  to pre-d2846bf1 behavior for large scenes and to the current path for small
  ones. No baseline refresh.
- No approximation (Barnes-Hut/caps remain rejected per the 2026-07-15
  ruling), and no raising of the 512 parallel cap or its scratch reservation.

## Tasks

- [ ] T1 — Fallback path. When `modelCount > MUTUAL_GRAVITY_MAX_BODIES`,
      skip the pair-scratch machinery entirely and run the direct serial
      triangular accumulation into `m_mutualGravityForces` (the
      pre-parallelization loop shape, which needs only the body-count scratch
      already reserved to `MAX_GAME_MODELS`). The lane-F fatal remains only
      for true invariant breaks (body scratch capacity, not body count). A
      `Why:` comment records the 512 threshold as the pair-scratch memory
      trade and names this plan.
- [ ] T2 — Coverage. Extend the mutual-gravity determinism test with an
      above-cap fixture (e.g. 520 bodies, bounded steps) proving: no fatal,
      and kinematics bit-identical between the fallback path and a
      pair-scratch run of the same field forced through the under-cap path
      shape (or, more directly, identical across 0/1/4 worker counts, since
      above-cap must ignore workers entirely).
- [ ] T3 — Final gates: `tools\validate_tests.bat` for the new coverage, then
      `tools\validate_physics.bat` byte-exact against the unchanged committed
      baseline. Update the allocation-policy allowlist wording if its cap
      description changes ("capped at 512 bodies" becomes "parallel path
      capped at 512 bodies; larger counts use the serial exact path").

## Dependencies And Decisions

- Reopens the closure of `deterministic-parallel-mutual-gravity` (round 4);
  this plan is the remediation row.
- Owner ruling 2026-07-15: exact-sum only; the fallback restores the old
  envelope rather than growing scratch or approximating.
- Independent of the other round-5 plans.

## Acceptance

- No reachable `SB_FATAL` from body count alone with mutual gravity enabled
  up to `MAX_GAME_MODELS`.
- Above-cap test passes with byte-identical kinematics across worker counts.
- `validate_physics` passes with zero baseline changes.

## Validation

- `tools\validate_tests.bat`, then `tools\validate_physics.bat`, output pasted
  at closure with an explicit no-baseline-change statement.

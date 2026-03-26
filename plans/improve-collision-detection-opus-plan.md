# Plan — Improve Collision Detection (Multi-Ball)

## Problem

When three or more balls collide in the same frame, the third ball passes through the others.
Two root causes conspire:

1. **`isTimeStepApplied` one-shot guard** — `GameModelCollection::RunPhysics` marks both balls
   in a pair as "done" after their first collision response. Any ball already marked is skipped
   for all further pair checks AND for terrain checks. A third ball touching either of them is
   never tested.

2. **Position advancement baked into response** — `CollisionResponseGameModel` calls
   `UpdatePosition(remainingTimeStep)` on both balls at the end, teleporting them to their
   end-of-frame positions. Even if the `isTimeStepApplied` guard were removed, subsequent
   collision detections against those balls would use stale time accounting: the balls are
   already at end-of-frame but `CalculateRay` would project them forward by the full
   `fChangeInTime` again, double-advancing them.

   This is why simply deleting the `isTimeStepApplied` flag is not sufficient — the time
   accounting must also be fixed.

## Approach

**Separate velocity response from position advancement, and track per-model remaining time.**

- `CollisionResponseGameModel` will change velocities and clear flags only. It will no longer
  advance positions.
- `RunPhysics` will replace the `isTimeStepApplied` bool vector with a `float timeRemaining`
  vector. Each pair is detected using `min(timeRemaining[x], timeRemaining[y])`. On collision,
  both models advance to the collision point, their remaining time is reduced, and the
  velocity-only response fires. Because positions are at the collision point and flags are
  cleared, the same model can immediately participate in further pair checks with correct time
  and velocity.
- Terrain collision passes use each model's remaining time instead of the full frame time.
  After terrain response (which still advances position internally), remaining time is zeroed.
- A final pass advances every model by whatever time remains.
- Positional correction (push overlapping spheres apart) is added as a safety net against
  floating-point overlap accumulation.

**Why `min(timeRemaining[x], timeRemaining[y])`?**
`GetModelCollisionTime` projects both models using a single `changeInTime` for both rays.
Using the minimum of the two remaining times is conservative — we only check the window in
which both models are still in play. The less-constrained model's extra time is consumed in the
final advancement pass. This avoids needing to modify `GetModelCollisionTime` to accept two
separate time values.

## Tasks

| ID | File(s) | Change |
|----|---------|--------|
| `split-response` | `SkullbonezGameModel.cpp` | In `CollisionResponseGameModel`: remove both `UpdatePosition(remainingTimeStep)` calls and the `remainingTimeStep` parameter. The method becomes velocity-response-only: call `RespondCollisionGameModels`, clear `isResponseRequired` on both models, done. Update the declaration in `SkullbonezGameModel.h` to match. |
| `restructure-loop` | `SkullbonezGameModelCollection.cpp` | Replace `std::vector<bool> isTimeStepApplied` with `std::vector<float> timeRemaining` (initialised to `fChangeInTime`). In the game-model collision loop: remove both `isTimeStepApplied` guards, detect each pair using `min(timeRemaining[x], timeRemaining[y])`, on collision advance both to collision point and subtract `colTime` from their `timeRemaining`, then call the modified response. In the terrain loop: detect using `timeRemaining[x]`, after terrain response set `timeRemaining[x] = 0`. In the final loop: advance each model by `timeRemaining[x]` if > 0. |
| `positional-correction` | `SkullbonezCollisionResponse.cpp` | At the end of `RespondCollisionGameModels`, after velocities and angular velocities have been applied: compute the distance between sphere centres, compare to the sum of radii, and if overlapping push each sphere apart by half the overlap distance along the collision normal. Requires `dynamic_cast` to `BoundingSphere*` to get radii (already done elsewhere in the same method for angular response). |
| `verify-build` | All | Build the solution to 0 errors, 0 warnings. |

## Notes

- The `isResponseRequired` guard in `CollisionDetectGameModel` (throws if either model already
  has a pending response) is safe to keep unchanged. Since each pair is detect→respond
  atomically within the loop body, the flag is always cleared before the next detection call.

- Terrain collision handling (`CollisionResponseTerrain`) still advances position internally.
  This is fine — `timeRemaining` is zeroed after terrain response to prevent double-advancement.

- Previously, a ball that collided with another ball was excluded from terrain checks. The new
  code checks all balls against terrain using their remaining time. This is more correct — a
  ball can now collide with another ball AND the terrain in the same frame.

## Out of Scope

- Earliest-collision-first ordering (sort all pairs by collision time, resolve earliest, re-detect). Correct for many-body simulations, but overkill for the current ball count.
- Iterative constraint solving (multiple passes per frame).
- Broad-phase spatial partitioning.
- Modifying `GetModelCollisionTime` to accept per-model time values.

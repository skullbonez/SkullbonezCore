# Plan — Improve Collision Detection (Multi-Ball)

## Problem

The collision detection loop in `GameModelCollection::RunPhysics` uses an `isTimeStepApplied`
flag that marks both balls as "done" after the first collision pair is resolved. When three or
more balls collide in the same frame, any ball already marked is skipped for all further
collision checks. The third (and beyond) ball receives no velocity response and no positional
correction, causing it to pass through other balls.

Two compounding issues:

1. **`isTimeStepApplied` early-exit** — a ball involved in one collision cannot participate in
   any further collision that frame, even if it is simultaneously touching a third ball.
2. **No positional correction** — response is velocity-only. Balls that are already overlapping
   (centre distance < sum of radii) are not pushed apart before velocities are updated, so the
   overlap persists into the next frame and accumulates.

## Approach

- **Remove `isTimeStepApplied`** from the game-model collision loop entirely. Every pair must
  be tested every frame. Impulses applied to a ball from multiple simultaneous collisions are
  additive, which is a physically reasonable approximation for small ball counts.
- **Add positional correction** inside `CollisionResponseGameModel`: after velocities are
  changed, compute the overlap along the collision normal and push each sphere out by half the
  overlap distance. This prevents inter-frame accumulation of penetration.
- Terrain and ground collision handling is separate and unchanged.

## Tasks

| ID | File(s) | Change |
|----|---------|--------|
| `remove-flag` | `SkullbonezGameModelCollection.cpp` | Delete `isTimeStepApplied` vector and all guard checks from the game-model collision loop |
| `positional-correction` | `SkullbonezGameModel.cpp` / `SkullbonezGameModel.h` | After velocity impulse in `CollisionResponseGameModel`, compute sphere overlap and translate both spheres apart by half the penetration depth along the collision normal |
| `verify-build` | All | Build to 0 errors, 0 warnings |

## Out of Scope

- Iterative constraint solving (multiple passes per frame) — not needed for the current ball count
- Broad-phase spatial partitioning — not needed at this scale
- Terrain/ground collision changes

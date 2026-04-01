# Plan — Terrain Rolling Physics

## Problem

When a ball hits the terrain and `projectedVelocity < 1.0f`, `RespondCollisionTerrain` sets
`isGrounded = true`. On subsequent frames, `SphereVsPlaneRollResponse` fires, which multiplies
both linear and angular velocity by 0.9 every frame — an unconditional damping that rapidly
kills all motion. There is nothing that converts gravitational potential energy on a slope into
lateral velocity while grounded, so the ball bleeds speed and stops even on steep hills.

Three interlocking problems:

1. **No gravity-along-slope force while grounded** — `AddWorldForces` applies gravity as a
   pure Y-axis force. While airborne, this creates downward acceleration that eventually causes
   terrain collision. But once grounded, `SphereVsPlaneRollResponse` only dampens — it never
   projects gravity onto the terrain surface to produce a downhill force. The ball has no way to
   accelerate along a slope.

2. **Unconditional 0.9× damping every frame** — `SphereVsPlaneRollResponse` multiplies linear
   and angular velocity by 0.9 regardless of slope, speed, or frame time. On flat ground this
   is reasonable (simulates rolling friction), but on a slope the ball should accelerate
   downhill, not decelerate. The damping is also frame-rate-dependent — faster frames damp more
   aggressively.

3. **`isGrounded` is sticky** — once set to `true`, `isGrounded` is never cleared by the normal
   physics path. `GetTerrainCollisionTime` short-circuits to `collisionTime = 0.0f` when
   grounded, so the ball is permanently locked to the terrain surface. If the slope steepens
   enough that the ball should become airborne (e.g., rolling off a cliff), it cannot.

## Approach

- **Add a slope force** to the grounded rolling path: project gravity onto the terrain plane
  to produce a tangential acceleration vector pointing downhill. Apply this as a velocity
  change scaled by `changeInTime` so it is frame-rate-independent.

- **Make damping frame-rate-independent** and apply it only to the component of velocity NOT
  along the slope force direction. Replace the flat `*= 0.9f` with a time-based exponential
  decay: `velocity *= powf(frictionFactor, changeInTime)`. This way, steeper slopes produce
  larger downhill force that overcomes rolling friction, while flat ground still brings the ball
  to rest.

- **Clear `isGrounded` when the ball is moving away from the surface** — after computing the
  terrain plane normal, if the ball's velocity has a positive component along the normal
  (moving away from terrain), set `isGrounded = false` so it can become airborne again (e.g.,
  launching off a ridge or ramp).

## Tasks

| ID | File(s) | Change |
|----|---------|--------|
| `slope-force` | `SkullbonezCollisionResponse.cpp` | In `SphereVsPlaneRollResponse`: compute slope direction by projecting gravity onto the terrain plane; apply downhill acceleration to linear velocity scaled by `changeInTime`. Requires passing `changeInTime` and the terrain plane normal into `SphereVsPlaneRollResponse`. |
| `framerate-damping` | `SkullbonezCollisionResponse.cpp` | In `SphereVsPlaneRollResponse`: replace `*= 0.9f` with `*= powf(ROLLING_FRICTION, changeInTime)` for both linear and angular velocity. Add `ROLLING_FRICTION` constant to `SkullbonezCommon.h`. |
| `clear-grounded` | `SkullbonezCollisionResponse.cpp` | In `RespondCollisionTerrain`: after computing the slope force, if the ball's velocity dotted with the terrain normal is positive (moving away from surface), set `isGrounded = false`. |
| `pass-context` | `SkullbonezCollisionResponse.h`, `SkullbonezCollisionResponse.cpp` | Update `SphereVsPlaneRollResponse` signature to accept `float changeInTime` and `const Vector3& planeNormal`. Update `RespondCollisionTerrain` to accept `float changeInTime` and thread it through. Update call site in `GameModel::CollisionResponseTerrain` to pass `remainingTimeStep`. |
| `slope-angular` | `SkullbonezCollisionResponse.cpp` | In `SphereVsPlaneRollResponse`: derive the angular velocity from the ball's linear velocity and the terrain plane so the spin axis is perpendicular to the rolling direction projected onto the slope surface, not just the flat XZ mapping that `GetRollVelocity` assumes. Use the cross product of the plane normal and the linear velocity direction to get the correct spin axis, scaled by `linearSpeed / radius`. |
| `verify-build` | All | Build to 0 errors, 0 warnings. |

## Notes

- The terrain plane normal is already available in `gameModel.responseInformation.collidedPlane.normal` at the point `RespondCollisionTerrain` is called.

- Gravity magnitude is in `WorldEnvironment::gravity` but `CollisionResponse` doesn't have
  access to it. Options: (a) pass gravity as a parameter, (b) use a hardcoded constant
  matching the value in `SkullbonezCommon.h`, or (c) add a gravity accessor. Prefer (a) — pass
  it through from `RunPhysics` via `CollisionResponseTerrain`.

- `UpdateRollPosition` exists on `RigidBody` but is never called. It could be repurposed for
  slope-aware position updates, but the simpler approach is to let the existing
  `UpdatePosition` handle it since we're modifying velocity directly.

## Out of Scope

- Variable friction per terrain region.
- Ball-to-ball collision while both are grounded and rolling (already handled by the static overlap fix).

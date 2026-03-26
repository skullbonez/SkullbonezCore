# Progress — Terrain Rolling Physics

## Task: `pass-context`

**Goal:** Thread `changeInTime` and terrain plane normal through to `SphereVsPlaneRollResponse`
so slope force and frame-rate-independent damping can be computed.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 1 | Update `SphereVsPlaneRollResponse` signature | Change from `(GameModel& gameModel)` to `(GameModel& gameModel, float changeInTime, const Vector3& planeNormal)` in both `.h` and `.cpp` | ✅ Done |
| 2 | Update `RespondCollisionTerrain` signature | Add `float changeInTime` parameter. Update declaration in `SkullbonezCollisionResponse.h` | ✅ Done |
| 3 | Thread `changeInTime` in `RespondCollisionTerrain` | Pass `changeInTime` and `gameModel.responseInformation.collidedPlane.normal` to `SphereVsPlaneRollResponse` call | ✅ Done |
| 4 | Update `CollisionResponseTerrain` call in `GameModel.cpp` | `CollisionResponse::RespondCollisionTerrain(*this)` → `CollisionResponse::RespondCollisionTerrain(*this, remainingTimeStep)` | ✅ Done |
| 5 | Pass gravity to `CollisionResponseTerrain` | Add gravity parameter to `RespondCollisionTerrain` and `CollisionResponseTerrain` so slope force can use the correct value. Thread from `WorldEnvironment` through `GameModel` (which already holds a `worldEnvironment` pointer). Alternatively, add a `GetGravity()` accessor to `GameModel` that delegates to `worldEnvironment` | ✅ Done |

---

## Task: `slope-force`

**Goal:** When a ball is grounded, project gravity onto the terrain surface to create a downhill
acceleration. This converts gravitational potential energy into lateral motion on slopes.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 6 | Compute gravity vector | In `SphereVsPlaneRollResponse`: `Vector3 gravityVec(0.0f, gravity, 0.0f)` where `gravity` is the signed gravity value (negative = downward) | ✅ Done |
| 7 | Project gravity onto terrain plane | `Vector3 gravityAlongNormal = planeNormal * (gravityVec * planeNormal)` — component of gravity along the terrain normal | ✅ Done |
| 8 | Compute tangential (downhill) force | `Vector3 slopeForce = gravityVec - gravityAlongNormal` — the component of gravity tangent to the surface, pointing downhill | ✅ Done |
| 9 | Apply slope acceleration to linear velocity | `linearVelocity += slopeForce * changeInTime` — frame-rate-independent acceleration | ✅ Done |
| 10 | Apply slope-induced angular velocity | Convert the slope linear acceleration into a rolling angular impulse: `angularVelocity.x -= slopeForce.z * changeInTime; angularVelocity.z += slopeForce.x * changeInTime` (matches the axis mapping in `GetRollVelocity`) | ✅ Done |

---

## Task: `framerate-damping`

**Goal:** Replace frame-rate-dependent `*= 0.9f` with time-based exponential decay so damping
behaves consistently regardless of frame rate.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 11 | Add `ROLLING_FRICTION` constant | In `SkullbonezCommon.h`: `#define ROLLING_FRICTION 0.1f` — the fraction of velocity remaining after 1 second of rolling (lower = more friction). Tune to taste | ✅ Done |
| 12 | Replace linear damping | In `SphereVsPlaneRollResponse`: change `linearVelocity * 0.9f` to `linearVelocity * powf(ROLLING_FRICTION, changeInTime)`. Add `#include <cmath>` if needed | ✅ Done |
| 13 | Replace angular damping | Same treatment for angular velocity: `angularVelocity * powf(ROLLING_FRICTION, changeInTime)` | ✅ Done |
| 14 | Apply damped velocities | Call `SetLinearVelocity` and `SetAngularVelocity` with the damped values (after slope force has been added, so slope acceleration is not immediately damped away) | ✅ Done |

---

## Task: `clear-grounded`

**Goal:** Allow a ball to become airborne again when it's moving away from the terrain surface
(e.g., rolling off a cliff edge, or after a collision impulse pushes it upward).

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 15 | Check velocity vs terrain normal | In `RespondCollisionTerrain`, after the roll response: compute `float normalVelocity = gameModel.physicsInfo.GetVelocity() * gameModel.responseInformation.collidedPlane.normal` | ✅ Done |
| 16 | Clear grounded if moving away | `if(normalVelocity > GROUNDED_ESCAPE_THRESHOLD) gameModel.SetIsGrounded(false)` — use a small positive threshold to avoid flickering. Add `#define GROUNDED_ESCAPE_THRESHOLD 0.1f` to `SkullbonezCommon.h` | ✅ Done |

---

## Task: `slope-angular`

**Goal:** Derive the angular velocity from the ball's actual rolling direction on the terrain
surface, so spin matches movement direction on slopes instead of using the flat-ground XZ
axis mapping from `GetRollVelocity`.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 17 | Get linear velocity on the surface | In `SphereVsPlaneRollResponse`, after slope force and damping: compute `Vector3 surfaceVelocity = linearVelocity - planeNormal * (linearVelocity * planeNormal)` — project linear velocity onto the terrain plane to remove any normal component | ✅ Done |
| 18 | Compute rolling speed | `float linearSpeed = Vector::VectorMag(surfaceVelocity)` | ✅ Done |
| 19 | Get sphere radius | `float radius = dynamic_cast<BoundingSphere*>(gameModel.boundingVolume.get())->GetRadius()` — already used elsewhere in collision response code | ✅ Done |
| 20 | Compute spin axis | `Vector3 spinAxis = Vector::CrossProduct(planeNormal, surfaceVelocity)` — perpendicular to both the surface normal and the rolling direction. This is the axis the ball rotates around when rolling without slipping | ✅ Done |
| 21 | Normalise spin axis | Guard: if `linearSpeed > 0.0f` then `spinAxis.Normalise()`, else leave angular velocity unchanged (ball is stationary) | ✅ Done |
| 22 | Set angular velocity | `gameModel.physicsInfo.SetAngularVelocity(spinAxis * (linearSpeed / radius))` — angular speed = linear speed / radius for rolling without slipping. This replaces the flat `angularVelocity.x/z` mapping | ✅ Done |

---

## Task: `verify-build`

| # | Step | Detail | Status |
|---|------|--------|--------|
| 23 | Build Debug configuration | `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32` — expect 0 errors, 0 warnings | ✅ Done |
| 24 | Build Release configuration | `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=Win32` — expect 0 errors, 0 warnings | ✅ Done |

---

## Summary

| Task | Steps | Status |
|------|-------|--------|
| `pass-context` | 1–5 | ✅ Done |
| `slope-force` | 6–10 | ✅ Done |
| `framerate-damping` | 11–14 | ✅ Done |
| `clear-grounded` | 15–16 | ✅ Done |
| `slope-angular` | 17–22 | ✅ Done |
| `verify-build` | 23–24 | ✅ Done |

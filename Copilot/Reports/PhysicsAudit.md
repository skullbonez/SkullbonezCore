# SkullbonezCore Physics Simulation — Technical Audit

**Date:** 2026-05-03  
**Scope:** All physics, collision, math, and game-object source files  
**Engine version:** Post-Phase 10 (shader migration in progress)

---

## Table of Contents

1. [Current Physics System Summary](#1-current-physics-system-summary)
2. [Formula Correctness Audit](#2-formula-correctness-audit)
3. [Suggestions for Improving Ball Rolling on Terrain](#3-suggestions-for-improving-ball-rolling-on-terrain)
4. [Suggestion for Adding Rigid Cubes](#4-suggestion-for-adding-rigid-cubes)
5. [Other Physics Improvements](#5-other-physics-improvements)

---

## 1. Current Physics System Summary

### 1.1 Integration Method — Symplectic Euler (with issues)

The engine uses a **semi-implicit (symplectic) Euler** integrator, but with a critical dimensional error in how forces are applied. The pipeline per frame:

1. **`ApplyForces()`** — accumulates world forces (gravity, buoyancy, drag) and any impulse forces into velocity
2. **Collision detection** — swept sphere-sphere and ray-plane tests
3. **Collision response** — modifies velocities (linear + angular)
4. **`UpdatePosition(dt)`** — `position += velocity * dt`

The integration is split across `RigidBody::ApplyWorldForce()`, `ApplyLinearForce()`, `ApplyAngularForce()`, and `UpdatePosition()`.

**Relevant code** (`SkullbonezRigidBody.cpp:40-54`):
```cpp
void RigidBody::ApplyWorldForce()
{
    Vector3 worldLinearAcceleration = m_worldForce / m_mass;
    m_linearVelocity += worldLinearAcceleration;  // ← Missing: * dt
    Vector3 worldAngularAcceleration = m_worldTorque / m_rotationalInertia;
    m_angularVelocity += worldAngularAcceleration; // ← Missing: * dt
}
```

The `dt` multiplication is performed earlier in `WorldEnvironment::AddWorldForces()` (line 215):
```cpp
target.SetWorldForce( m_worldForce * changeInTime, m_worldTorque * changeInTime );
```

So the "force" being set is actually `force * dt` — meaning the units passed to `SetWorldForce` are **impulse** (N·s), not force (N). Then in `ApplyWorldForce()`, dividing by mass gives `Δv` directly (m/s), which is then added to velocity. This is **dimensionally correct in effect** but confusingly structured — the "force" members are really impulses.

### 1.2 Collision Detection

#### Sphere-Sphere: Swept Sphere Test
`BoundingSphere::CollisionDetect()` (`SkullbonezBoundingSphere.cpp:23-76`) performs a **swept sphere intersection** using the quadratic formula on the combined movement ray. It:

1. Computes relative movement: `totalMovement = targetRay.vector3 - focusRay.vector3`
2. Computes the displacement magnitude
3. Solves the quadratic: `t = (d·n̂ - √(d·n̂² + r²_sum - d·d)) / |totalMovement|`
4. Returns `t ∈ [0, 1]` as the fraction of the frame where collision occurs

This is a **correct and standard approach** for swept sphere-sphere tests.

#### Sphere-Terrain: Ray-Plane Intersection
`GameModel::GetTerrainCollisionTime()` (`SkullbonezGameModel.cpp:278-315`):

1. Constructs a ray from position + velocity * dt
2. Offsets the ray origin downward by the sphere radius (bottom of sphere)
3. Locates the terrain polygon (triangle) under the sphere via `Terrain::LocatePolygon()`
4. Computes ray-plane intersection time via `GeometricMath::CalculateIntersectionTime()`

This uses a **single-sample** approach — it only checks the triangle directly below the sphere's current XZ position. **It does not account for horizontal motion crossing triangle boundaries** within a single frame.

#### Broadphase: Spatial Hash Grid
`SpatialGrid` (`SkullbonezSpatialGrid.h`) is a uniform-grid spatial hash with:
- 1024-bucket open-addressing hash table with generation stamping (zero-allocation clear)
- Pair deduplication via triangular bit array
- Cell size configurable (default 11.0 units, `Cfg().broadphaseCell`)

### 1.3 Collision Response

#### Sphere-Terrain Response
Split into two paths (`SkullbonezCollisionResponse.cpp:14-52`):

- **Bouncing** (not grounded): `SphereVsPlaneLinearImpulse()` + `SphereVsPlaneAngularImpulse()`
- **Rolling** (grounded, `projectedVelocity < 1.0`): `SphereVsPlaneRollResponse()` — simple velocity damping

The grounded threshold is **hardcoded at 1.0** (unitless projected velocity magnitude), which is not physically principled.

#### Sphere-Sphere Response
Two-phase response:
1. **Linear** (`SphereVsSphereLinear`, line 239-293): Uses the 1D elastic collision equations projected along the collision normal, then reconstructs 3D velocity. Correct formula.
2. **Angular** (`SphereVsSphereAngular`, line 296-387): Uses the rigid body impulse formula. Contains a notable workaround where collision points are replaced with the normalized relative velocity direction (line 335) to avoid the degenerate case where the collision point is parallel to the collision normal for spheres.
3. **Positional correction** (line 86-99): Pushes overlapping spheres apart by half the overlap along the separation axis.

#### Sphere-Sphere (Static Overlap)
`GameModel::StaticOverlapResponseGameModel()` (`SkullbonezGameModel.cpp:136-157`): Pure positional correction for grounded/slow balls missed by swept tests. No velocity response — avoids division-by-zero in angular calculations when relative velocity is near-zero.

### 1.4 Terrain Interaction

**Terrain structure:** A heightmap loaded from a `.RAW` file, tessellated into a regular grid of triangles. Each quadrilateral cell is split into two triangles along the diagonal.

**Polygon location** (`SkullbonezTerrain.cpp:198-276`):
1. Maps world XZ to grid cell via `floor(pos / (stepSize * scale))`
2. Determines which of the two triangles the point falls in by computing the gradient of the relative position within the quad
3. Returns the triangle vertices

**Height query** (`GeometricMath::GetHeightFromPlane`, `SkullbonezGeometricMath.cpp:75-96`):
Uses plane equation + law of sines to compute height at an arbitrary XZ coordinate on the triangle plane.

**Normal query** (`Terrain::GetTerrainNormalAt`, `SkullbonezTerrain.cpp:139-154`):
Cross product of two triangle edges, normalized, with sign flip if pointing downward.

### 1.5 Force Application

#### Gravity
`WorldEnvironment::CalculateGravity()` (`SkullbonezWorldEnvironment.cpp:219-223`):
```cpp
float WorldEnvironment::CalculateGravity( float objectMass )
{
    return objectMass * m_gravity; // Fg = mg, m_gravity = -30.0 m/s²
}
```

Applied as Y-component only. Default gravity is -30.0 m/s², which is approximately 3× Earth gravity (9.81 m/s²). This is a deliberate gameplay tuning choice for more energetic bouncing.

#### Viscous Drag (Linear and Angular)
`WorldEnvironment::CalculateViscousDrag()` (`SkullbonezWorldEnvironment.cpp:233-273`):
```
F_drag = -v̂ * 0.5 * ρ_avg * |v|² * C_d * A
```
Where `ρ_avg` is linearly interpolated between gas and fluid density based on submersion percentage. Applied identically to both linear velocity and angular velocity vectors.

#### Buoyancy
`WorldEnvironment::CalculateBuoyancy()` (`SkullbonezWorldEnvironment.cpp:226-230`):
```
F_b = -g * ρ_fluid * V_submerged
```
Standard Archimedes' principle. Submerged volume is calculated via the spherical cap formula.

### 1.6 Angular Velocity and Orientation

**Orientation** is stored as a `Quaternion` (`SkullbonezQuaternion.h`). Updated each frame by:
```cpp
m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
```

This decomposes the angular velocity vector into three sequential Euler-angle rotations (X, then Y, then Z) and creates corresponding quaternions via half-angle formulas. **This is an approximation** — the true quaternion derivative is `q̇ = ½ω·q`, but for small timesteps and moderate angular velocities, the sequential approach is acceptable.

**Rotational inertia** is stored as a `Vector3` — the **three diagonal elements** of the inertia tensor. Off-diagonal terms are assumed zero (principal-axis aligned), which is correct for spheres. Division by inertia is done component-wise (`torque / rotationalInertia`), which is only valid when the inertia tensor is diagonal.

---

## 2. Formula Correctness Audit

### 2.1 Gravity — ✅ Correct
```
F_gravity = m * g
```
`SkullbonezWorldEnvironment.cpp:222`: `return objectMass * m_gravity;`  
Dimensionally correct. The value g = -30 m/s² is tuned for gameplay, not realism.

### 2.2 Viscous Drag — ⚠️ Correct Formula, Misapplied to Angular Velocity

**Linear drag** (`SkullbonezWorldEnvironment.cpp:266-272`):
```
F_drag = -v̂ * 0.5 * ρ * |v|² * C_d * A
```
This is the **standard drag equation**. ✅ Correct.

**Angular drag** (`SkullbonezWorldEnvironment.cpp:209-213`):
```cpp
m_worldTorque += CalculateViscousDrag( target.GetAngularVelocity(), ... );
```
The same linear drag formula is applied to angular velocity, using the same drag coefficient and projected surface area. This is **physically incorrect**:

- **Issue 1:** Angular drag torque depends on the geometry's rotational drag characteristics, not the same linear `C_d * A` product. For a sphere, rotational drag is governed by `τ = -8πμR³ω/3` (Stokes) or similar, not `½ρω²C_dA`.
- **Issue 2:** The units are wrong — `CalculateViscousDrag()` computes a force (N), but it's being used as a torque (N·m).
- **Impact:** Angular velocity will be damped, but at a physically meaningless rate. In practice, this probably produces reasonable-looking results because the drag coefficient is small and the visual difference between correct and incorrect angular damping is hard to perceive.

**Severity:** Medium. Incorrect units and formula, but visually acceptable.

### 2.3 Buoyancy — ✅ Correct
```
F_buoyancy = -g * ρ_fluid * V_submerged
```
`SkullbonezWorldEnvironment.cpp:229`: `return m_gravity * m_fluidDensity * submergedObjectVolume * -1.0f;`  
Archimedes' principle. ✅ Correct.

**Submerged volume calculation** (`SkullbonezBoundingSphere.cpp:118-146`):  
Uses the spherical cap formula: `V = π/3 * (3r - y) * y²`.  
✅ Correct — the standard formula for the volume of a sphere below height `y` from the bottom.

**Issue with `GetSubmergedVolumePercent`** — the function uses `m_position.y` (the local-space offset), **not** the world-space position. This is passed the adjusted height from `GameModel::GetSubmergedVolumePercent()` (`SkullbonezGameModel.cpp:424-431`):
```cpp
float totalPercentage = GetShapeSubmergedVolumePercent(
    m_boundingVolume, m_fluidSurfaceHeight - m_physicsInfo.GetPosition().y );
```
The second argument `fluidSurfaceHeight - worldY` is passed as the "fluidSurfaceHeight" parameter, but inside `BoundingSphere::GetSubmergedVolumePercent()` it compares against `m_position.y` (the local offset, which is `ZERO_VECTOR` for spheres centered at origin). So the effective comparison is:
- Top of sphere: `0 + radius` vs `fluidHeight - worldY`  
- Bottom of sphere: `0 - radius` vs `fluidHeight - worldY`

This is **correct** — the adjustment transforms the problem into local space where the sphere center is at the local offset origin. ✅

### 2.4 Sphere-Sphere Linear Collision — ✅ Correct

`CollisionResponse::SphereVsSphereLinear()` (`SkullbonezCollisionResponse.cpp:239-293`):

The 1D elastic collision formula:
```
v1f = [(m1 - e*m2)*v1i + (1+e)*m2*v2i] / (m1 + m2)
v2f = [(m2 - e*m1)*v2i + (1+e)*m1*v1i] / (m1 + m2)
```

Projected along the collision normal, then reconstructed into 3D:
```cpp
gameModel1.SetLinearVelocity(
    (gameModel1FinalVelocity - gameModel1ProjectedVelocity) * collisionNormal
    + gameModel1.GetVelocity() );
```

This correctly:
- Projects velocities onto the collision normal
- Applies the 1D formula to the projected components
- Adds the velocity change back along the collision normal to the original 3D velocity

✅ **Correct.** The tangential velocity components are preserved (frictionless collision along normal).

**Note:** The coefficient of restitution is **averaged** between the two objects. This is a common simplification — the physically correct approach uses `e = min(e1, e2)` or a product, but averaging is standard in game physics.

### 2.5 Sphere-Sphere Angular Collision — ⚠️ Approximated

`CollisionResponse::SphereVsSphereAngular()` (`SkullbonezCollisionResponse.cpp:296-387`):

Uses the rigid body impulse formula:
```
j = -v_r(e+1) / [1/m1 + 1/m2 + n·((r1×n)/I1)×r1 + n·((r2×n)/I2)×r2]
```

**Issue 1: Collision point substitution (line 335)**
```cpp
objectSpaceCollisionPoint1 = objectSpaceCollisionPoint2 = relativeLinearVelocity;
```
The actual object-space collision points (radius along collision normal) are discarded and replaced with the normalized relative linear velocity. The developer's comment acknowledges this is a heuristic ("I don't know why this works so well"). 

**Why this matters:** For sphere-sphere collisions, the collision point `r` is always along the collision normal, making `r × n = 0` — which would zero out the angular contribution entirely in the correct formula. The substitution with `relativeLinearVelocity` breaks the geometric meaning but introduces non-zero angular effects. This is a **creative hack** that produces visually plausible (but physically incorrect) angular responses.

**Issue 2: Rotational inertia application**
```cpp
Vector3 scaledRotationalInertia1 = Vector::VectorMultiply(
    gameModel1.m_physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal1 );
```
This is a component-wise multiplication (`I * (r×n)` element-by-element), which only works when the inertia tensor is diagonal AND aligned with world axes. For spheres (isotropic inertia), this is **coincidentally correct**.

**Severity:** Medium-High for physical accuracy, Low for visual quality.

### 2.6 Sphere-Terrain Angular Impulse — ⚠️ Mixed Correctness

`CollisionResponse::SphereVsPlaneAngularImpulse()` (`SkullbonezCollisionResponse.cpp:152-236`):

**The impulse magnitude formula** is the standard rigid body impulse against an infinite-mass plane:
```
j = -v_r(e+1) / [1/m + n·((r×n)/I)×r]
```
✅ **Correct** for single-body vs. infinite-mass plane.

**Angular velocity change from impulse (line 203-206):**
```cpp
Vector3 m_changeInAngularVelocity = Vector::CrossProduct( collisionPoint, impulseForce );
m_changeInAngularVelocity *= gameModel.m_physicsInfo.GetFrictionCoefficient() * 10;
```

**Issue 1:** `cross(r, F)` gives torque, but this is used directly as `Δω` without dividing by the inertia tensor. The comment says "rotational inertia has already been taken into account" but this isn't quite right — inertia was used in computing the impulse magnitude `j`, but converting the impulse to angular velocity change should use `Δω = I⁻¹ * (r × j*n)`, and here `impulseForce = j*n`.

**Issue 2:** The `* 10` multiplier on the friction coefficient is an arbitrary scaling factor with no physical basis.

**Tangential friction (no-slip) code (lines 208-232):**
```cpp
Vector3 noSlipDelta = Vector::CrossProduct( collisionPoint, tangentialVelocity * (-1.0f) ) / contactRadiusSq;
float grip = gameModel.m_physicsInfo.GetFrictionCoefficient() * 10.0f;
if (grip > 1.0f) grip = 1.0f;
m_changeInAngularVelocity += noSlipDelta * grip;
```
This attempts to drive the contact point toward zero-slip velocity via `Δω = (r × (-v_t)) / |r|²`. This is a **reasonable approximation** of rolling friction, interpolated by the friction coefficient. The math for the no-slip delta is correct: `v_surface = v + ω×r`, and the `Δω` needed to zero the tangential component is `(r × (-v_t)) / |r|²`.

**Severity:** Medium. The impulse-to-angular conversion is missing inertia division, but the tangential friction approximation is sound.

### 2.7 Sphere-Terrain Linear Bounce — ⚠️ Partially Correct

`CollisionResponse::SphereVsPlaneLinearImpulse()` (`SkullbonezCollisionResponse.cpp:118-149`):

```cpp
Vector3 bounceVelocity = direction * (projectedVelocity * gameModel.GetCoefficientRestitution());
Vector3 spinContribution = spinTangential * (-gameModel.GetFrictionCoefficient());
gameModel.SetLinearVelocity( bounceVelocity + spinContribution );
```

**Issue 1:** The bounce velocity uses `projectedVelocity * e`, where `projectedVelocity` is the dot product of the **reversed** total velocity with the plane normal. This correctly reflects the normal component with restitution. ✅

**Issue 2:** The spin contribution computes the tangential surface velocity from `ω × r_contact` and applies it as a velocity offset scaled by friction. This models **spin-to-linear momentum transfer** (e.g., backspin causing backward motion after bounce). ⚠️ The transfer rate is the friction coefficient directly, not derived from impulse theory, but it's a reasonable game-physics approximation.

**Issue 3:** `DampenAngularVelocity()` is called after the linear impulse:
```cpp
void RigidBody::DampenAngularVelocity()
{
    m_angularVelocity *= 1.0f - ( m_frictionCoefficient * 5.0f );
}
```
With default `frictionCoefficient = 0.1`, this multiplies angular velocity by `0.5` — **halving angular velocity on every bounce**. The `* 5.0f` factor is arbitrary and aggressive. At friction = 0.2, the multiplier becomes `0.0`, completely killing spin on impact.

**Severity:** Medium. The damping factor is too aggressive and non-physical.

### 2.8 Rolling Response — ❌ Significantly Simplified

`CollisionResponse::SphereVsPlaneRollResponse()` (`SkullbonezCollisionResponse.cpp:106-115`):
```cpp
void CollisionResponse::SphereVsPlaneRollResponse( GameModel& gameModel )
{
    Vector3 m_linearVelocity = gameModel.m_physicsInfo.GetVelocity() * 0.9f;
    gameModel.m_physicsInfo.SetLinearVelocity( m_linearVelocity );
    Vector3 m_angularVelocity = gameModel.m_physicsInfo.GetAngularVelocity() * 0.9f;
    gameModel.m_physicsInfo.SetAngularVelocity( m_angularVelocity );
}
```

This is **not a rolling response** — it's a **flat 10% damping per collision tick** on both linear and angular velocity. There is:
- No coupling between linear and angular velocity
- No rolling friction torque
- No terrain slope effects
- No no-slip constraint enforcement
- No distinction between rolling and sliding friction

Additionally, this function is called **every frame** while grounded (not just on collision), meaning the 0.9× factor compounds rapidly and brings balls to rest very quickly regardless of slope.

**Severity:** High. This is the weakest part of the physics simulation.

### 2.9 Roll Position Update — ⚠️ Partially Correct

`RigidBody::UpdateRollPosition()` (`SkullbonezRigidBody.cpp:203-217`):
```cpp
void RigidBody::UpdateRollPosition( float changeInTime, float circumference )
{
    Vector3 rollRevolutions = GetRollVelocity() / _2PI;
    Vector3 positionUpdate = rollRevolutions * changeInTime * circumference;
    m_position += positionUpdate;
    m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
}
```

**Note: This function appears to be unused** — `UpdatePosition()` is called instead, which uses `m_linearVelocity` directly. `UpdateRollPosition` seems to be legacy code that was meant to derive linear position from angular velocity for pure rolling, but it's not in the active call chain.

`GetRollVelocity()` (`SkullbonezRigidBody.cpp:220-236`):
```cpp
rollVelocity.x = m_angularVelocity.z;
rollVelocity.y = 0.0f;
rollVelocity.z = -m_angularVelocity.x;
```
This maps angular velocity to linear velocity assuming a flat XZ plane. For rolling on a flat surface: `v = ω × r` where `r` points down. With `r = (0, -R, 0)`, `v = ω × (0, -R, 0) = (-ωy·0 - ωz·(-R), ωz·0 - ωx·0, ωx·(-R) - ωy·0) = (Rωz, 0, -Rωx)`. The direction mapping is correct, but the magnitude scaling via `circumference / 2π` effectively gives `R`, making the full formula `v = R * ω`, which is correct for no-slip rolling. ✅ However, this only works on flat terrain.

### 2.10 Terrain Normal Calculation — ✅ Correct

`Terrain::GetTerrainNormalAt()` (`SkullbonezTerrain.cpp:139-154`):
```cpp
Vector3 edge1 = tri.v2 - tri.v1;
Vector3 edge2 = tri.v3 - tri.v1;
Vector3 m_normal = Vector::CrossProduct( edge1, edge2 );
m_normal.Normalise();
if (m_normal.y < 0.0f) m_normal = m_normal * -1.0f;
```

**Note:** This uses `edge2 = v3 - v1` (different from `ComputeTriangleNormal` which uses `v3 - v2`). Both are valid but may produce normals pointing in opposite directions for the same triangle, hence the `y < 0` sign flip. The sign correction ensures the normal always points upward. ✅

### 2.11 Quaternion Integration — ⚠️ Approximation

`Quaternion::RotateAboutXYZ()` (`SkullbonezQuaternion.cpp:56-76`):

Decomposes the angular velocity vector `(ωx, ωy, ωz) * dt` into three sequential axis-angle rotations and multiplies them: `q' = q * Rx * Ry * Rz`.

**Issue:** The correct quaternion update is:
```
q(t+dt) = q(t) + (dt/2) * ω_quat * q(t)
```
where `ω_quat = (ωx, ωy, ωz, 0)`.

The sequential Euler decomposition introduces **order-dependent error** — rotating X then Y then Z gives different results than Y then X then Z. For small timesteps this is acceptable, but at high angular velocities it can cause visible wobble.

**Quaternion multiplication** (`SkullbonezQuaternion.cpp:94-119`): Uses the Hamilton product. ✅ **Correct.**

**Quaternion-to-matrix** (`SkullbonezQuaternion.cpp:79-91`): Standard right-handed rotation matrix from unit quaternion. ✅ **Correct.**

### 2.12 Integration Timestep — ⚠️ Variable Timestep

The main loop (`SkullbonezRun.cpp:200-213`) uses **variable-length timesteps** clamped to `[0, 0.05]` seconds:
```cpp
double secondsPerFrame = m_cFrameTimer.GetElapsedTime();
if (secondsPerFrame > 0.05) secondsPerFrame = 0.05;
```

This means the physics simulation runs at **frame rate** (typically 60-240 Hz depending on vsync). For a symplectic Euler integrator, variable timesteps can cause:
- Energy instability at low framerates
- Non-deterministic behavior
- Different results on different hardware

The 50ms cap prevents catastrophic instability from frame hitches.

### 2.13 Velocity Throttling — ⚠️ Artificial Clamp

`RigidBody::ThrottleAngularVelocity()` (`SkullbonezRigidBody.cpp:123-151`):

Clamps each component of angular velocity independently to `[-velocityLimit, velocityLimit]` (default 5.0 rad/s). This is a **per-component clamp**, not a magnitude clamp, which means diagonal angular velocities can reach `5√3 ≈ 8.66` rad/s while axis-aligned ones are limited to 5.0. This creates an anisotropic velocity limit that depends on the current rotation axis.

### 2.14 Coefficient of Restitution — ⚠️ Inconsistent Usage

- **Sphere-terrain:** Uses the single object's `e` directly
- **Sphere-sphere:** Averages the two objects' `e` values: `(e1 + e2) / 2`

The standard approach for combining restitution coefficients is `e = min(e1, e2)`, `e = e1 * e2`, or `e = sqrt(e1 * e2)`. Averaging overestimates bounciness when one material is very bouncy and the other is not. **Not technically incorrect** for game physics, but physically inconsistent.

---

## 3. Suggestions for Improving Ball Rolling on Terrain

### 3.1 Current State — What's Wrong

The current rolling system has several fundamental problems:

**Problem 1: Flat damping replaces rolling physics**  
`SphereVsPlaneRollResponse()` multiplies both linear and angular velocity by 0.9 every frame the ball is grounded. This is called from `RespondCollisionTerrain()` when `isGrounded == true`. Since the collision detect runs every frame for grounded balls (`collisionTime = 0.0`), this damping compounds at frame rate — a 60 FPS simulation would reduce velocity to `0.9^60 ≈ 0.002` of its original value in one second.

**Problem 2: No angular-linear coupling while rolling**  
While grounded, linear and angular velocities are damped independently. There is no constraint enforcing `v = ω × r` (no-slip condition). A ball can have high angular velocity with zero linear velocity or vice versa.

**Problem 3: No terrain slope influence**  
The rolling response ignores the terrain normal entirely. A ball on a steep slope experiences the same damping as one on flat ground. There's no gravitational component driving the ball downhill.

**Problem 4: Binary grounded state**  
The `isGrounded` flag is set when `projectedVelocity < 1.0` — a hardcoded threshold that doesn't account for mass, gravity, or terrain slope. Once grounded, the ball enters the simplified rolling path and only exits via a ball-ball collision (`SetIsGrounded(false)` in `RespondCollisionGameModels`). There's no mechanism for a ball to "launch" off a slope crest.

**Problem 5: `UpdateRollPosition()` is dead code**  
The method that would derive position from angular velocity exists but is never called.

### 3.2 Proposed Fixes

#### Fix 1: Replace flat damping with physics-based rolling

Replace `SphereVsPlaneRollResponse()` with a proper rolling friction model:

```cpp
void CollisionResponse::SphereVsPlaneRollResponse( GameModel& gameModel )
{
    // Get terrain normal at current position
    Vector3 normal = gameModel.m_terrain->GetTerrainNormalAt(
        gameModel.GetPosition().x, gameModel.GetPosition().z );
    
    float radius = GetShapeBoundingRadius( gameModel.m_boundingVolume );
    Vector3 velocity = gameModel.m_physicsInfo.GetVelocity();
    Vector3 omega = gameModel.m_physicsInfo.GetAngularVelocity();
    float mass = gameModel.m_physicsInfo.GetMass();
    float friction = gameModel.m_physicsInfo.GetFrictionCoefficient();
    
    // 1. Project velocity onto terrain surface (remove normal component)
    float vDotN = velocity * normal;
    Vector3 surfaceVelocity = velocity - normal * vDotN;
    
    // 2. Enforce no-slip: angular velocity must match linear velocity
    // v = ω × (-r * normal) => ω = (normal × v) / r
    Vector3 targetOmega = Vector::CrossProduct( normal, surfaceVelocity ) / radius;
    
    // Blend toward no-slip condition
    float blendRate = friction * 10.0f; // Higher friction = faster convergence
    if (blendRate > 1.0f) blendRate = 1.0f;
    Vector3 newOmega = omega + (targetOmega - omega) * blendRate;
    
    // 3. Gravitational acceleration along slope
    float normalForceMag = mass * fabsf(gameModel.m_worldEnvironment->GetGravity());
    Vector3 gravityTangent = Vector3(0, mass * gameModel.m_worldEnvironment->GetGravity(), 0)
                             - normal * (normal * Vector3(0, mass * gameModel.m_worldEnvironment->GetGravity(), 0));
    // For a sphere: I = 2/5 * m * r²
    // Rolling acceleration = F_tangent / (m + I/r²) = F_tangent / (m * 7/5)
    float rollingMassEffect = mass * 1.4f; // 7/5 for solid sphere
    Vector3 rollingAcceleration = gravityTangent / rollingMassEffect;
    
    // 4. Rolling friction opposes motion
    // F_roll = μ_roll * N (normal force)
    float rollingFrictionCoeff = 0.01f; // Much smaller than sliding friction
    if (VectorMag(surfaceVelocity) > TOLERANCE)
    {
        Vector3 frictionForce = surfaceVelocity;
        frictionForce.Normalise();
        frictionForce *= -rollingFrictionCoeff * normalForceMag;
        rollingAcceleration += frictionForce / mass;
    }
    
    // 5. Apply
    gameModel.m_physicsInfo.SetLinearVelocity( surfaceVelocity + rollingAcceleration * dt );
    gameModel.m_physicsInfo.SetAngularVelocity( newOmega );
}
```

#### Fix 2: Add slope-based grounded exit condition

A ball should leave grounded state when the terrain normal component of velocity exceeds a threshold:

```cpp
// In RespondCollisionTerrain, before entering grounded path:
Vector3 terrainNormal = gameModel.m_terrain->GetTerrainNormalAt(pos.x, pos.z);
float normalVelocity = gameModel.GetVelocity() * terrainNormal;

// If moving significantly away from the terrain, leave grounded
if (normalVelocity > 0.5f) // threshold tuned for gameplay
{
    gameModel.SetIsGrounded(false);
    // Proceed with bounce response instead
}
```

#### Fix 3: Add rolling friction coefficient

Add a separate `m_rollingFrictionCoefficient` to `RigidBody` (typically 0.001–0.05 for spheres on terrain, much lower than sliding friction 0.1–0.5). This controls energy loss while rolling without the aggressive per-frame damping.

#### Fix 4: Properly couple angular and linear velocity

When transitioning to grounded state, immediately set the angular velocity to match the linear velocity via the no-slip condition:
```cpp
// ω = (n × v) / r
Vector3 noSlipOmega = CrossProduct(terrainNormal, linearVelocity) / radius;
gameModel.m_physicsInfo.SetAngularVelocity(noSlipOmega);
```

### 3.3 Summary Table

| Aspect | Current | Needed |
|--------|---------|--------|
| Rolling friction | 0.9× flat damping per frame | `F = μ_roll * N` |
| Angular-linear coupling | Independent damping | No-slip constraint: `v = ω × r` |
| Slope influence | None | Gravity tangent component |
| Grounded detection | `projectedVelocity < 1.0` | Normal velocity + slope analysis |
| Launch from terrain | Only via ball-ball collision | Normal velocity exceeds threshold |

---

## 4. Suggestion for Adding Rigid Cubes

### 4.1 Collision Detection Approach

**Recommended: SAT (Separating Axis Theorem)** for the SkullbonezCore codebase.

| Approach | Pros | Cons | Recommendation |
|----------|------|------|----------------|
| **SAT** | Straightforward, gives contact normal and penetration depth directly, well-suited to convex shapes | O(n²) axes for general polyhedra (but only 15 for OBB-OBB) | ✅ **Best fit** |
| OBB Tree | Good for complex shapes | Overkill for single boxes | ❌ |
| GJK/EPA | General-purpose, handles any convex shape | Complex implementation, EPA needed for contact info | ⚠️ Future upgrade |

For **OBB vs OBB**, SAT requires testing 15 separating axes:
- 3 face normals of box A
- 3 face normals of box B  
- 9 cross products of edge pairs (3 × 3)

For **OBB vs Sphere**, only 1-3 axes need testing (closest point on OBB to sphere center).

For **OBB vs Terrain**, test the 8 cube vertices against the terrain height, plus edge-terrain intersections.

### 4.2 Inertia Tensor for a Box

For a solid rectangular box with half-extents `(a, b, c)` and mass `m`:

```
I_xx = m/12 * (b² + c²)    (Y² + Z² extents)
I_yy = m/12 * (a² + c²)    (X² + Z² extents)
I_zz = m/12 * (a² + b²)    (X² + Y² extents)
```

For a cube with side `s`: `I_xx = I_yy = I_zz = m*s²/6`

The current `Vector3 m_rotationalInertia` storage is sufficient for boxes **only if the inertia tensor remains diagonal** — which is true only in the body's principal axes frame. When the box rotates, the world-space inertia tensor is:

```
I_world = R * I_body * R^T
```

This requires upgrading to a **full 3×3 matrix inertia tensor** or computing it on the fly from orientation + diagonal body inertia.

**Required changes:**
- Add `Matrix3` or use `Matrix4` for inertia tensor
- Store body-space diagonal inertia in the `RigidBody`
- Compute world-space inertia each frame: `I_world = R * diag(Ixx, Iyy, Izz) * R^T`
- Replace all `torque / m_rotationalInertia` (component-wise) with `I_world_inv * torque` (matrix-vector multiply)

### 4.3 Cube-Terrain Collision

**Approach: Multi-point contact**

1. Transform the 8 cube vertices to world space using the orientation quaternion
2. For each vertex, query terrain height at its XZ coordinate
3. Any vertex below the terrain surface is a **contact point**
4. For each contact point:
   - Penetration depth = terrain_height - vertex_y
   - Contact normal = terrain normal at that XZ
5. Handle multiple contacts by summing impulses or using iterative constraint solving

**Edge-terrain contacts** (cube resting on an edge):
- Sample terrain height at multiple points along each of the 12 edges
- If the edge penetrates terrain between two vertices that are both above, compute the intersection point

**Face-terrain contacts** (cube resting flat):
- Detected when 4 coplanar vertices are at similar height
- Apply a single planar contact constraint

### 4.4 Cube-Sphere Collision

1. Transform sphere center into cube's local space
2. Find the closest point on the cube (OBB) to the sphere center by clamping each axis:
   ```cpp
   closest.x = clamp(sphereLocal.x, -halfExtent.x, halfExtent.x);
   closest.y = clamp(sphereLocal.y, -halfExtent.y, halfExtent.y);
   closest.z = clamp(sphereLocal.z, -halfExtent.z, halfExtent.z);
   ```
3. If `distance(closest, sphereCenter) < sphereRadius`, collision detected
4. Contact normal = `normalize(sphereCenter - closest)`
5. Penetration depth = `sphereRadius - distance`

### 4.5 Multiple Contact Points

The current system handles exactly one contact per collision event. Cubes require handling 1-4+ simultaneous contacts. Options:

**Option A: Sequential Impulses (easiest)**  
Process each contact point independently, iterating multiple times until convergence. This is the approach used by Bullet, Box2D, and most modern engines.

```
for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
    for (each contact point) {
        compute_and_apply_impulse(contact);
    }
}
```

**Option B: Simultaneous Solve (LCP)**  
Formulate as a Linear Complementarity Problem and solve with Projected Gauss-Seidel. More accurate but significantly more complex.

**Recommendation:** Option A (sequential impulses) with 4-8 iterations per frame. This is the industry standard for real-time physics.

### 4.6 Required Class Hierarchy Changes

**New files needed:**
```
SkullbonezBoundingBox.h / .cpp        — OBB collision shape
SkullbonezContactManifold.h           — Contact point collection
SkullbonezConstraintSolver.h / .cpp   — Sequential impulse solver
SkullbonezMatrix3.h / .cpp            — 3×3 matrix (or extend Matrix4)
```

**Modified files:**
| File | Change |
|------|--------|
| `SkullbonezCollisionShape.h` | Add `BoundingBox` to variant: `using CollisionShape = std::variant<BoundingSphere, BoundingBox>;` |
| `SkullbonezCollisionShape.h` | Add all free-function visitors for the new shape |
| `SkullbonezRigidBody.h/.cpp` | Upgrade inertia to full tensor; add `I_world_inverse` computation |
| `SkullbonezCollisionResponse.h/.cpp` | Add `BoxVsPlane`, `BoxVsSphere`, `BoxVsBox` response methods |
| `SkullbonezGameModel.h/.cpp` | Add `AddBoundingBox(halfExtents)` method |
| `SkullbonezCommon.h` | Add box-related constants |
| `SkullbonezHelper.h/.cpp` | Add box mesh rendering |

**The `std::variant` approach is excellent** for adding new shapes — the compiler will force handling of all shape combinations at every `std::visit` dispatch site.

### 4.7 Estimated Complexity

| Component | Effort | Notes |
|-----------|--------|-------|
| `BoundingBox` class | 2 days | OBB data, model matrix, volume, drag, surface area |
| SAT collision detection | 3 days | OBB-OBB (15 axes), OBB-Sphere (clamping), OBB-Terrain (vertex/edge tests) |
| Full inertia tensor | 2 days | Matrix3, body↔world transform, update all `torque/I` sites |
| Contact manifold | 1 day | Store N contact points + normals + depths |
| Sequential impulse solver | 3-5 days | Core constraint solver, friction cones, iterative convergence |
| Rendering | 1 day | Box mesh, instanced rendering |
| Integration + testing | 2-3 days | Debug visualization, stability tuning |
| **Total** | **~15-18 days** | |

### 4.8 Key Implementation Challenges

1. **Resting contacts and stacking:** Cubes on top of cubes require persistent contact manifolds and warm-starting. Without this, stacks will jitter.
2. **Friction cones:** Box friction requires a proper friction cone (or box friction approximation) at each contact point, not just a single coefficient.
3. **Numerical stability:** Thin cubes and high stacks are notoriously unstable with simple solvers. May need bias terms and warm-starting.
4. **Swept collision for boxes:** The current swept-sphere test doesn't extend to OBBs. Options: conservative advancement, or just use penetration correction (simpler but less accurate).
5. **Terrain edge cases:** A cube can bridge two terrain triangles, requiring consistent normals across triangle boundaries.

---

## 5. Other Physics Improvements

### 5.1 Better Integrator — Semi-Implicit Euler → Velocity Verlet

The current integration is:
```
v(t+dt) = v(t) + a(t) * dt    (velocity update)
x(t+dt) = x(t) + v(t+dt) * dt  (position update with NEW velocity)
```

This is already semi-implicit Euler (symplectic), which is decent. **Velocity Verlet** would be better:
```
x(t+dt) = x(t) + v(t)*dt + 0.5*a(t)*dt²
a(t+dt) = F(x(t+dt)) / m
v(t+dt) = v(t) + 0.5*(a(t) + a(t+dt))*dt
```

**Benefits:** Second-order accuracy (vs first-order), better energy conservation, no additional cost for force evaluation. **Estimated effort:** 1-2 days.

However, the biggest win would be **fixed timestep with interpolation**:
```cpp
const float FIXED_DT = 1.0f / 120.0f;
accumulator += frameTime;
while (accumulator >= FIXED_DT) {
    StepPhysics(FIXED_DT);
    accumulator -= FIXED_DT;
}
float alpha = accumulator / FIXED_DT;
InterpolateRenderState(alpha);
```

This ensures deterministic, framerate-independent physics. **Estimated effort:** 2-3 days.

### 5.2 Sleeping / Deactivation for Resting Objects

Currently, all objects are simulated every frame, even when at rest. With 300+ balls, many will be sitting still on the terrain.

**Proposal:**
- Track velocity magnitude over N frames
- If `|v| < sleep_threshold` and `|ω| < sleep_threshold` for 30+ frames, mark as sleeping
- Sleeping objects skip force integration, collision detection, and position update
- Wake on: external force, collision with active object, terrain change

```cpp
// In RigidBody or GameModel:
int m_sleepCounter = 0;
bool m_isSleeping = false;
static constexpr float SLEEP_THRESHOLD = 0.01f;
static constexpr int SLEEP_FRAMES = 30;
```

**Performance benefit:** Potentially 50-80% reduction in physics cost for scenes where most balls have come to rest. **Estimated effort:** 1 day.

### 5.3 Continuous Collision Detection (CCD)

The current swept-sphere test is already a form of CCD for sphere-sphere. However, **sphere-terrain** uses a single-point check that can miss fast-moving balls tunneling through thin terrain features.

**Proposed fix:** Use the swept-sphere approach for terrain as well — trace the sphere along its movement ray and check all terrain triangles it crosses, not just the one at the starting position.

A simpler alternative: **sub-stepping** — divide the frame into N sub-steps and check terrain collision at each. With the current 50ms cap and typical ball speeds, 2-4 sub-steps would catch most tunneling cases. **Estimated effort:** 2-3 days.

### 5.4 Force Accumulation Cleanup

The current force pipeline is confusing because "forces" are actually pre-multiplied by `dt`:
```cpp
// WorldEnvironment::AddWorldForces():
target.SetWorldForce( m_worldForce * changeInTime, m_worldTorque * changeInTime );
```

Then in `ApplyWorldForce()`:
```cpp
Vector3 worldLinearAcceleration = m_worldForce / m_mass;
m_linearVelocity += worldLinearAcceleration;  // No dt needed — already baked in
```

This works but is error-prone. **Recommendation:** Store actual forces, apply `dt` in one place:
```cpp
void RigidBody::Integrate(float dt) {
    Vector3 acceleration = m_totalForce / m_mass;
    m_linearVelocity += acceleration * dt;
    m_position += m_linearVelocity * dt;
    
    Vector3 angAccel = m_totalTorque / m_rotationalInertia; // or I_inv * torque
    m_angularVelocity += angAccel * dt;
    m_orientation.IntegrateAngularVelocity(m_angularVelocity, dt);
}
```

### 5.5 Constraint-Based Contact Resolution

The current collision response applies impulses immediately and moves on. This doesn't handle:
- Simultaneous contacts (ball touching two other balls at once)
- Resting contacts (ball sitting on terrain with another ball on top)
- Contact persistence (recomputing everything from scratch each frame)

**Long-term recommendation:** Migrate to a constraint solver architecture:
1. Generate contact constraints from collision detection
2. Solve all constraints simultaneously using iterative methods (PGS)
3. Apply velocity corrections
4. Apply position corrections (Baumgarte stabilization or split impulses)

This is a significant architectural change but would be necessary for cube support and would improve sphere behavior dramatically. **Estimated effort:** 2-3 weeks.

### 5.6 Per-Component Angular Velocity Throttle

As noted in §2.13, the angular velocity throttle (`ThrottleAngularVelocity`) clamps each component independently, creating anisotropic limits. Replace with a magnitude-based clamp:

```cpp
void RigidBody::ThrottleAngularVelocity()
{
    float magSq = Vector::VectorMagSquared( m_angularVelocity );
    float limitSq = Cfg().velocityLimit * Cfg().velocityLimit;
    if (magSq > limitSq) {
        float scale = Cfg().velocityLimit / sqrtf(magSq);
        m_angularVelocity *= scale;
    }
}
```

### 5.7 Sphere-Terrain Normal Usage in Bounce

The current terrain bounce uses the pre-collision terrain plane normal stored in `collidedPlane.m_normal`. If the ball has moved significantly before the response is processed (due to the collision time calculation), this normal may be for a different triangle than the one the ball actually hits. Consider re-querying the terrain normal at the actual collision point.

### 5.8 Performance Considerations

| Optimization | Impact | Effort |
|-------------|--------|--------|
| Sleep system | High — skip 50-80% of stationary balls | 1 day |
| SIMD Vector3 math | Medium — 2-4× for dot/cross/normalize | 2-3 days |
| Cache-friendly layout (SoA) | Medium — better cache utilization | 3-5 days |
| Parallel broadphase | Low — spatial grid is already fast | 2 days |
| GPU collision detection | Low — only beneficial at 1000+ objects | 5+ days |

---

## Summary of Findings

### Critical Issues (should fix)
1. **Rolling response is pure damping** — no physics, no slope, no coupling (§2.8, §3.1)
2. **Angular drag uses wrong formula and units** — linear drag applied as torque (§2.2)
3. **`DampenAngularVelocity` halves spin on every bounce** — `* 5.0f` arbitrary multiplier (§2.7)

### Moderate Issues (should consider)
4. Sphere-sphere angular response uses heuristic collision points (§2.5)
5. Sphere-terrain angular impulse skips inertia tensor division (§2.6)
6. Variable timestep makes simulation non-deterministic (§2.12)
7. Per-component angular velocity throttle is anisotropic (§2.13)
8. Force pipeline pre-multiplies dt, making code confusing (§5.4)

### Minor Issues (nice to have)
9. Quaternion update uses sequential Euler decomposition (§2.11)
10. Coefficient of restitution averaging vs min/product (§2.14)
11. Single-sample terrain collision can miss triangle boundaries (§1.2)

### Architecture Strengths
- `std::variant<BoundingSphere>` collision shape is excellent — adding new shapes will be compiler-enforced
- Swept sphere-sphere test is correct and standard
- Spatial hash broadphase is zero-allocation and efficient
- Buoyancy and linear drag formulas are physically correct
- Force accumulation pipeline (despite confusing dt placement) produces correct results

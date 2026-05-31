# Plan — Natural Contact Solver Physics Rewrite

**Date:** 2026-05-13
**Scope:** Replace the remaining rolling, orientation, and sphere-sphere response hacks with a mathematically consistent impulse/constraint solver.
**Primary files:** `SkullbonezSource/SkullbonezGameModelCollection.cpp`, `SkullbonezSource/SkullbonezGameModel.cpp`, `SkullbonezSource/SkullbonezCollisionResponse.cpp`, `SkullbonezSource/SkullbonezRigidBody.cpp`, `SkullbonezSource/SkullbonezConfig.h`, `SkullbonezData/engine.cfg`.

---

## 1. Goals

The current physics system already contains a good foundation: swept sphere-sphere tests, terrain plane tests, rigid body mass/inertia state, quaternion orientation, normal impulses, and Coulomb friction. The remaining problems come from correction layers that make rolling and spin look right by directly overwriting state.

This plan moves the system toward a single contact model where:

1. **Bouncing, sliding, rolling, and settling emerge from contact impulses** rather than from separate hard-coded modes.
2. **Angular impulse comes from the actual contact point** and the same effective-mass equation for terrain and sphere-sphere contacts.
3. **Rolling emerges when friction drives contact-point tangential velocity toward zero**; the solver should not overwrite `omega` with a hand-derived no-slip value.
4. **Orientation is only integrated from angular velocity**; no pole-vector visual correction should directly rotate the quaternion.
5. **Resting contacts use bias/constraint stabilization** instead of terrain snap, static positional-only overlap correction, and global velocity clamps.
6. **All forces have clear units**: forces are integrated with `dt`, impulses are immediate velocity changes, and config values are named according to what they physically represent.

---

## 2. Current hacks to eliminate

| Current behavior | Why it is a hack | Replacement target |
|------------------|------------------|--------------------|
| Per-component angular velocity clamp in `ThrottleAngularVelocity()` | Hides unstable impulses and clamps axes independently, which changes spin direction. | Fix impulse math; optionally add diagnostic energy guard or magnitude-based debug safety clamp. |
| Terrain contact proximity epsilon returns `t = 0` | Useful anti-jitter shortcut, but it confuses detection with constraint stabilization. | Keep a small contact skin, but resolve it through positional bias and warm-started contacts. |
| Static sphere-sphere overlap correction only moves positions | Removes penetration without applying momentum-correcting impulses. | Use Baumgarte/split impulse positional correction plus velocity-level contact impulse. |
| Terrain response overwrites angular velocity with no-slip rolling omega | Forces rolling rather than deriving it from friction impulses. | Iterative tangent friction constraints drive contact tangential velocity toward zero naturally. |
| Pole-vector orientation correction | Edits orientation directly for visual stability. | Quaternion orientation changes only through `omega`; rolling visual stability follows from correct angular velocity. |
| Sphere-sphere angular response uses normalized relative velocity as contact point | Empirical substitute for real contact geometry. | Use the physical sphere-sphere contact points `rA = n * radiusA`, `rB = -n * radiusB`. |
| Sphere-sphere angular response sign negation | Compatibility shim for historical visual conventions. | Normalize conventions: one physical angular velocity sign, one quaternion integration convention, tests lock it. |
| One terrain collision consumes the rest of the frame | Avoids re-processing but loses multi-contact/multiple-bounce behavior. | Fixed-step substepping with multiple solver iterations. |

---

## 3. Target solver model

### 3.1 State variables

Each dynamic body should expose:

```text
x       world-space center position
q       world-space orientation quaternion
v       linear velocity
ω       angular velocity
m       mass
M⁻¹     inverse mass scalar
I_body  body-space inertia tensor
I⁻¹     world-space inverse inertia tensor
```

For a sphere, inertia is isotropic:

```text
I = (2/5) m r²
I⁻¹ = 1 / I
```

For the current engine, `m_rotationalInertia` is stored as a vector. The rewrite can keep this representation for spheres, but the math should be written as if it supports the general tensor form:

```text
I_world⁻¹ = R(q) · I_body⁻¹ · R(q)ᵀ
```

For spheres, this collapses to the scalar inverse inertia and is cheap.

### 3.2 Contact representation

Unify terrain and sphere-sphere contacts into one structure:

```cpp
struct Contact
{
    int bodyA;
    int bodyB;              // -1 for static terrain
    Vector3 point;          // world-space contact point
    Vector3 normal;         // from A toward B / out of terrain
    float penetration;      // positive when overlapping
    float restitution;
    float frictionStatic;
    float frictionDynamic;
    float rollingFriction;
    float spinFriction;
    float accumulatedNormalImpulse;
    Vector3 accumulatedTangentImpulse;
    float accumulatedSpinImpulse;
};
```

A contact should be generated by collision detection only. It should not decide whether to bounce, roll, or visually align. Those outcomes belong to the solver.

### 3.3 Contact-point velocity

For each body, define the vector from center of mass to contact:

```text
rA = p - xA
rB = p - xB
```

Velocity at the contact point:

```text
vA_contact = vA + ωA × rA
vB_contact = vB + ωB × rB
```

Relative contact velocity:

```text
vrel = vB_contact - vA_contact
```

Normal velocity:

```text
vn = vrel · n
```

Tangential velocity:

```text
vt = vrel - vn n
```

If terrain is static, set `vB = 0`, `ωB = 0`, `M_B⁻¹ = 0`, and `I_B⁻¹ = 0`.

---

## 4. Normal impulse: bounce and non-penetration

### 4.1 Effective mass along an arbitrary direction

For any contact direction `d`, the effective inverse mass is:

```text
K(d) = M_A⁻¹ + M_B⁻¹
     + d · [ (I_A⁻¹ (rA × d)) × rA ]
     + d · [ (I_B⁻¹ (rB × d)) × rB ]
```

Impulse magnitude along `d`:

```text
j = - (vrel · d) / K(d)
```

Apply impulse `J = j d`:

```text
vA' = vA - J M_A⁻¹
ωA' = ωA - I_A⁻¹ (rA × J)

vB' = vB + J M_B⁻¹
ωB' = ωB + I_B⁻¹ (rB × J)
```

The signs above assume `n` points from A to B. For terrain, choose A = sphere and B = terrain, with `n` pointing out of terrain into the sphere, then verify sign conventions in tests.

### 4.2 Restitution

Restitution should only apply when the bodies are approaching fast enough:

```text
if vn < -v_restitution_threshold:
    bounceVelocity = -e vn
else:
    bounceVelocity = 0
```

Normal impulse with restitution and bias:

```text
j_n = - (vn - bounceVelocity + biasVelocity) / K(n)
```

Clamp accumulated normal impulse so contacts only push, never pull:

```text
λ_n_new = max(0, λ_n_old + j_n)
Δλ_n = λ_n_new - λ_n_old
```

Then apply `Δλ_n n`.

### 4.3 Baumgarte position bias

Instead of snapping the sphere to terrain or doing positional-only overlap correction, add a bias velocity when penetration exceeds a small slop:

```text
penetrationError = max(0, penetration - linearSlop)
biasVelocity = -(β / dt) penetrationError
```

Typical starting values:

```text
linearSlop = 0.01 to 0.05 world units
β = 0.1 to 0.2
```

This pushes penetrations out gradually through the same impulse pipeline.

### 4.4 Split impulse option

Baumgarte bias can inject energy. If bouncing looks too energetic, use split impulses:

- Keep normal/friction impulses for real velocities.
- Maintain separate correction velocities `v_bias`, `ω_bias` only for positional correction.
- Integrate `x += (v + v_bias) dt`, but do not add `v_bias` back into `v`.

This avoids the current static overlap correction while preventing penetration correction from launching balls.

---

## 5. Tangential friction: rolling should emerge naturally

### 5.1 Friction impulse from tangential contact velocity

After normal impulse, recompute `vrel` and tangential velocity:

```text
vt = vrel - (vrel · n)n
|vt| = tangentSpeed
```

If `|vt| > ε`, tangent direction:

```text
t = vt / |vt|
```

Tangential impulse that would stop slip:

```text
j_t_unc = - (vrel · t) / K(t)
```

Coulomb limit:

```text
|λ_t| ≤ μ λ_n
```

For a 2D tangent basis, prefer accumulated vector friction:

```text
T1, T2 = orthonormal basis perpendicular to n
vt2 = [vrel · T1, vrel · T2]
K_t = 2×2 effective mass matrix
Δλ_t_unc = -K_t⁻¹ vt2
λ_t_new = clampLength(λ_t_old + Δλ_t_unc, μ λ_n)
Δλ_t = λ_t_new - λ_t_old
J_t = Δλ_t.x T1 + Δλ_t.y T2
```

Apply `J_t` through the same impulse equations.

### 5.2 Why this produces rolling without forcing omega

For a sphere on static terrain, no slip means the contact point has zero tangential velocity:

```text
vt = v + ω × r - ((v + ω × r) · n)n = 0
```

For flat terrain with `n = (0,1,0)` and `r = -r n`:

```text
v + ω × (-r n) = 0
v = r (ω × n)
```

Equivalently:

```text
ω_roll = (n × v) / r
```

The current code computes a version of this directly and overwrites `omega`. The replacement solver does not set `ω = ω_roll`. Instead, each friction impulse changes both `v` and `ω` until the contact velocity constraint is satisfied. Rolling becomes the result of `vt → 0` under the Coulomb friction limit.

### 5.3 Static vs dynamic friction

Use two coefficients:

```text
μ_s = static friction coefficient
μ_d = dynamic friction coefficient
```

If the impulse required to cancel `vt` fits inside the static cone:

```text
|λ_t_unc| ≤ μ_s λ_n
```

then apply it and the contact sticks/rolls without slip. Otherwise clamp to dynamic friction:

```text
|λ_t| = μ_d λ_n
```

This naturally gives three states without explicit mode flags:

1. **Sliding:** friction saturates at `μ_d λ_n`; `vt` remains non-zero.
2. **Transition:** tangential speed decays as impulses transfer energy into spin.
3. **Rolling:** `vt ≈ 0`; static friction supplies only the impulse needed to maintain the constraint.

---

## 6. Spin friction and rolling resistance without fake orientation correction

### 6.1 Drill-spin friction

Coulomb tangential friction does not fully remove spin around the contact normal (`ω · n`), because that spin does not create tangential motion at a point contact. Model it as a torsional friction impulse around `n`.

Relative spin around normal:

```text
ω_spin = (ωB - ωA) · n
```

Angular effective mass around `n`:

```text
K_spin = n · I_A⁻¹ n + n · I_B⁻¹ n
```

Impulse-like angular correction:

```text
j_spin_unc = -ω_spin / K_spin
```

Clamp by a torsional friction limit proportional to the normal impulse and contact patch radius:

```text
|j_spin| ≤ μ_spin λ_n r_patch
```

For a perfect mathematical sphere point contact, `r_patch = 0`; for gameplay, expose a small effective patch radius:

```text
r_patch = spinPatchRatio * sphereRadius
spinPatchRatio ≈ 0.05 to 0.2
```

Apply angular impulse:

```text
ωA' = ωA - I_A⁻¹ (j_spin n)
ωB' = ωB + I_B⁻¹ (j_spin n)
```

This replaces direct `omega.y` or `omega · normal` damping with a constraint-like angular impulse.

### 6.2 Rolling resistance

Rolling resistance should oppose rolling motion without violating the no-slip constraint. Approximate it as a torque around the rolling axis, capped by normal force:

Rolling direction:

```text
v_tangent = v - (v · n)n
u = normalize(v_tangent)
```

Rolling axis for a sphere:

```text
a_roll = normalize(n × u)
```

Resisting torque magnitude:

```text
τ_roll = -C_rr N r sign(ω · a_roll)
```

Integrated angular impulse:

```text
j_roll = τ_roll dt
```

Clamp so it cannot reverse spin in one step:

```text
|j_roll| ≤ |ω · a_roll| / (a_roll · I⁻¹ a_roll)
```

Apply as angular impulse. The subsequent friction solve will maintain no-slip by adjusting linear velocity consistently. This replaces the current direct linear velocity subtraction plus forced omega recomputation.

---

## 7. Sphere-sphere angular impulse fix

### 7.1 Correct contact geometry

For two spheres:

```text
n = normalize(xB - xA)
p = xA + n rA
rA_contact = p - xA = n rA
rB_contact = p - xB = -n rB
penetration = rA + rB - |xB - xA|
```

Do **not** replace `rA_contact` or `rB_contact` with relative linear velocity. If `r × n = 0`, then a pure normal impulse creates no angular velocity. That is correct: friction, not normal impulse, creates spin transfer for spheres.

### 7.2 Normal impulse creates no spin for centerline hits

For sphere-sphere normal impulse:

```text
J_n = j_n n
rA × J_n = (rA n) × (j_n n) = 0
rB × (-J_n) = (-rB n) × (-j_n n) = 0
```

So the normal impulse changes linear velocities only. This is physically correct.

### 7.3 Tangential friction creates spin transfer

If the surfaces slide at the contact, tangential impulse produces angular velocity:

```text
J_t = j_t t
rA × J_t = rA n × j_t t
rB × (-J_t) = -rB n × -j_t t
```

Because `n × t ≠ 0`, this creates spin naturally. The same tangent friction code used for terrain handles ball-ball spin transfer.

### 7.4 Replacement for current `SphereVsSphereAngular()`

Remove the separate `SphereVsSphereAngular()` pathway. Sphere-sphere response should become:

1. Generate contact with physical `p`, `n`, `penetration`.
2. Solve normal impulse.
3. Solve tangent friction impulse.
4. Solve optional spin/rolling friction.
5. Apply split positional correction if needed.

This removes both the empirical collision point substitution and the sign-negation shim.

---

## 8. Orientation convention cleanup

### 8.1 One convention

Declare and enforce one convention:

```text
ω is a world-space angular velocity vector in radians/second.
Positive rotation follows the right-hand rule.
Orientation q maps local/body coordinates to world coordinates.
```

Quaternion integration should be:

```text
q' = normalize( Δq · q )
Δq = [axis = normalize(ω), angle = |ω| dt]
```

or the differential form:

```text
q_dot = 0.5 Ω(ω) q
q(t + dt) = normalize(q + q_dot dt)
```

Use the current axis-angle route if it matches the chosen convention, but add regression tests for the cardinal cases.

### 8.2 Delete visual pole correction after tests pass

The pole-vector correction exists because the rendered roll axis and the physical angular velocity convention drifted apart. Once convention tests prove that `ω` integrates visibly correctly, remove direct orientation correction entirely.

Regression expectations:

| Initial velocity | Expected roll axis on flat ground | Expected visible texture motion |
|------------------|-----------------------------------|---------------------------------|
| `+X` | `-Z` or `+Z` depending final convention, but fixed and tested | Texture rolls forward along +X |
| `-X` | Opposite of +X case | Texture rolls forward along -X |
| `+Z` | Perpendicular X axis | Texture rolls forward along +Z |
| `-Z` | Opposite of +Z case | Texture rolls forward along -Z |

The exact signs matter less than consistency between physics, quaternion integration, and rendering.

---

## 9. Collision detection and time stepping

### 9.1 Fixed physics step

Use a fixed physics timestep independent of render frame duration:

```text
fixedDt = 1 / 120 seconds
accumulator += frameDt * timeScale
while accumulator >= fixedDt:
    StepPhysics(fixedDt)
    accumulator -= fixedDt
```

Clamp accumulated time to avoid spiral of death:

```text
accumulator = min(accumulator, maxAccumulatedPhysicsTime)
```

This improves determinism and stabilizes iterative contact solving.

### 9.2 Substepping for fast motion

For high velocities, choose substeps based on maximum displacement relative to radius/cell size:

```text
substeps = ceil(maxBodySpeed * dt / maxAllowedDisplacement)
substeps = clamp(substeps, 1, maxSubsteps)
```

A practical starting point:

```text
maxAllowedDisplacement = 0.25 * minSphereRadius
maxSubsteps = 8
```

### 9.3 Terrain contact generation across triangle boundaries

Current terrain collision samples the triangle under the sphere's current XZ. For high lateral motion, generate contacts at the predicted position too, or sweep against crossed heightfield cells:

1. Compute `x0 = current position`, `x1 = x0 + v dt`.
2. Traverse grid cells crossed by the XZ segment.
3. Test the swept sphere against each candidate triangle plane.
4. Keep earliest time of impact and/or generate speculative contacts for nearby triangles.

Short-term compromise: generate resting contacts from both current and predicted XZ samples:

```text
sample0 = terrain triangle under x0.xz
sample1 = terrain triangle under x1.xz
contacts = unique(sample0, sample1)
```

This catches many ridge/cell-boundary cases without a full heightfield sweep.

---

## 10. Iterative solver pipeline

### 10.1 Proposed `StepPhysics(dt)`

```text
StepPhysics(dt):
    1. Apply external forces: v += (F / m) dt, ω += I⁻¹ τ dt
    2. Predict broadphase bounds using x and x + v dt
    3. Generate contacts:
       - sphere-sphere
       - sphere-terrain
       - optional speculative contacts within contact skin
    4. Warm start cached impulses from previous frame
    5. Iterate velocity constraints N times:
       - normal impulse
       - tangent friction impulse
       - spin friction impulse
       - rolling resistance impulse
    6. Integrate positions and orientations
    7. Generate/solve split positional corrections M times, or apply bias in step 5
    8. Cache accumulated impulses for warm starting
```

Starting iteration counts:

```text
velocityIterations = 8
positionIterations = 3
```

### 10.2 Sequential impulses

Use Projected Gauss-Seidel / sequential impulses because it is simple and fits the current architecture:

```text
for iteration in velocityIterations:
    for contact in contacts:
        SolveNormal(contact)
        SolveFriction(contact)
        SolveSpinFriction(contact)
```

This approximates solving all contact constraints together while requiring only local contact math.

### 10.3 Warm starting

Cache accumulated impulses by contact key:

```text
ContactKey = {bodyA, bodyB, featureId}
```

At the start of the next step, apply the cached impulses before iterations:

```text
J_cached = λ_n n + λ_t1 T1 + λ_t2 T2
ApplyImpulse(J_cached)
ApplyAngularSpinImpulse(λ_spin n)
```

Warm starting is important for stable resting contacts and will reduce the need for `contactEpsilon` hacks.

---

## 11. Energy and stability diagnostics

Before removing safety clamps, add debug logging/tests:

### 11.1 Energy calculation

Linear kinetic energy:

```text
E_linear = 0.5 m |v|²
```

Angular kinetic energy:

```text
E_angular = 0.5 ω · I_world ω
```

Potential energy relative to an arbitrary zero height:

```text
E_potential = m g y
```

Total mechanical energy when no drag/friction:

```text
E_total = E_linear + E_angular + E_potential
```

### 11.2 Debug assertions

In a no-friction/no-drag bounce test:

```text
|E_total(t) - E_total(0)| / E_total(0) < tolerance
```

In a frictional rolling test:

```text
E_total(t + dt) ≤ E_total(t) + smallNumericalTolerance
```

If energy spikes, log contact data:

```text
body ids, n, p, penetration, vn, vt, λ_n, λ_t, K_n, K_t, dt
```

Then fix the offending impulse rather than clamping angular velocity.

---

## 12. Config changes

Replace ambiguous or global knobs with solver-specific terms:

```ini
# Contact solver
physics_fixed_dt              = 0.008333333   # 120 Hz
velocity_iterations           = 8
position_iterations           = 3
contact_linear_slop           = 0.02
contact_baumgarte_beta        = 0.15
contact_restitution_threshold = 2.0
contact_skin                  = 0.05

# Friction
terrain_static_friction       = 0.9
terrain_dynamic_friction      = 0.7
sphere_static_friction        = 0.5
sphere_dynamic_friction       = 0.35
spin_friction_coeff           = 0.3
spin_patch_ratio              = 0.1
rolling_resistance_coeff      = 0.02

# Debug safety only; not normal gameplay physics
max_linear_speed_debug        = 500.0
max_angular_speed_debug       = 100.0
```

Keep existing keys temporarily for compatibility, but migrate call sites to the new names.

---

## 13. Implementation phases

### Phase 1 — Document conventions and add tests

**Goal:** Lock down signs, units, and expected behavior before rewriting the solver.

Tasks:

1. Add comments to `RigidBody` declaring `v`, `ω`, `q`, mass, and inertia units.
2. Add physics debug scenes:
   - flat roll +X, -X, +Z, -Z
   - frictionless terrain bounce
   - slope roll from rest
   - sphere-sphere glancing collision with spin
   - sphere-sphere head-on collision with no spin transfer
3. Add CSV logging for energy and contact impulses.
4. Add regression checks for:
   - no NaNs
   - no penetration above tolerance after solve
   - no energy increase in friction-only tests
   - cardinal roll visual direction

Exit criteria:

- Existing scenes still run.
- Current behavior is measured and reproducible.
- Sign convention is written down and testable.

### Phase 2 — Introduce unified contact data

**Goal:** Generate contacts separately from response.

Tasks:

1. Add `Contact` and `ContactManifold` structs.
2. Convert terrain detection to emit contacts with `bodyB = terrain`.
3. Convert sphere-sphere detection to emit contacts with physical point/normal/penetration.
4. Keep old response path behind a compile-time or config flag until the new solver is ready.

Exit criteria:

- New contact generation logs match old collision events.
- No behavior change yet, except optional debug output.

### Phase 3 — Implement normal impulse solver

**Goal:** Replace terrain bounce and sphere-sphere linear response with one normal impulse function.

Tasks:

1. Implement `ApplyImpulse(body, point, impulse)`.
2. Implement `ComputeEffectiveMass(bodyA, bodyB, rA, rB, direction)`.
3. Implement accumulated normal impulse clamp.
4. Add restitution threshold.
5. Use Baumgarte or split impulse for penetration correction.

Exit criteria:

- Frictionless sphere-terrain bounce works.
- Frictionless sphere-sphere head-on collision works.
- Static overlap positional-only correction is disabled in the new path.

### Phase 4 — Implement tangent friction solver

**Goal:** Make rolling emerge from friction instead of overwriting omega.

Tasks:

1. Build tangent basis from contact normal.
2. Solve 2D tangent impulse with accumulated cone clamp.
3. Add static/dynamic friction distinction.
4. Remove forced no-slip omega assignment in the new path.
5. Validate slope roll and flat roll tests.

Exit criteria:

- Ball on a slope accelerates downhill from gravity.
- Sliding transitions into rolling when friction is sufficient.
- On flat ground with no external force, rolling gradually stops from rolling resistance rather than arbitrary velocity scaling.

### Phase 5 — Implement spin friction and rolling resistance

**Goal:** Replace ad hoc spin damping and visual pole correction with physical angular constraints.

Tasks:

1. Add torsional friction around contact normal.
2. Add rolling resistance torque around the rolling axis.
3. Delete pole-vector orientation correction in the new path.
4. Remove water-specific skip for pole correction because pole correction no longer exists.

Exit criteria:

- Y/drill spin decays without breaking rolling motion on slopes.
- Floating/water scenes do not snap orientation.
- Orientation changes only via angular velocity integration.

### Phase 6 — Replace sphere-sphere angular hack

**Goal:** Remove empirical spin transfer.

Tasks:

1. Delete or bypass `SphereVsSphereAngular()`.
2. Use unified normal + tangent + spin solver for sphere-sphere contacts.
3. Verify glancing collisions generate spin via tangential friction.
4. Verify head-on centerline collisions do not generate fake spin.

Exit criteria:

- No relative-velocity-as-contact-point substitution remains.
- No sign-negation shim remains.
- Momentum and angular response are plausible and stable.

### Phase 7 — Fixed timestep and iteration loop

**Goal:** Improve determinism and reduce need for contact hacks.

Tasks:

1. Add fixed physics accumulator in the main run loop.
2. Add solver iteration config.
3. Warm start contacts.
4. Add optional substeps for fast motion.
5. Update perf tests because physics workload will change.

Exit criteria:

- Deterministic replay for physics validation scenes.
- No visible regression in render scenes.
- Physics perf remains acceptable for 300-ball scenes.

### Phase 8 — Remove old hacks and compatibility code

**Goal:** Clean out legacy paths once tests pass.

Remove:

1. `ThrottleAngularVelocity()` from normal per-frame flow, or convert to debug-only magnitude assertion.
2. Terrain forced no-slip omega overwrite.
3. Pole-vector orientation correction.
4. Static positional-only sphere overlap response.
5. `SphereVsSphereAngular()` empirical response.
6. Sphere-sphere sign-negation compatibility shim.
7. Any stale `m_isGrounded` behavior used as a physics mode switch.

Exit criteria:

- All physics modes use the unified solver.
- README physics section is updated to describe the final solver accurately.
- Old config keys are removed or marked deprecated.

---

## 14. Validation scenes and expected outcomes

| Scene | Purpose | Expected outcome |
|-------|---------|------------------|
| `physics_flat_roll_x.scene` | Cardinal +X rolling convention | `vt → 0`, `|v| ≈ r|ω|`, texture rolls forward. |
| `physics_flat_roll_z.scene` | Cardinal +Z rolling convention | Same as +X with perpendicular roll axis. |
| `physics_slope_from_rest.scene` | Gravity tangent component | Ball accelerates downhill without a special grounded path. |
| `physics_frictionless_bounce.scene` | Energy conservation | Restitution controls bounce height; no frictional energy loss. |
| `physics_glancing_spheres.scene` | Tangential sphere-sphere friction | Glancing hit creates spin naturally from `r × J_t`. |
| `physics_headon_spheres.scene` | No fake angular impulse | Centerline hit changes linear velocity but creates no spin. |
| `physics_rest_stack_like.scene` | Resting contact stability | No jitter/creep beyond slop with warm starting. |
| `float_snap_test.scene` | Regression for water snap | No orientation snaps without any pole-alignment special case. |

---

## 15. Suggested order of code changes

1. **Add tests/logging first.** The current solver has many convention patches; lock down what must remain true visually.
2. **Introduce new contact structs without changing behavior.** This keeps the refactor reviewable.
3. **Implement normal impulse only.** Validate bounce and sphere-sphere linear collision before friction.
4. **Add tangent friction.** This is the key step where natural rolling replaces forced omega.
5. **Add spin/rolling friction.** Tune only after normal/tangent impulses are stable.
6. **Enable fixed timestep.** Do this after the solver works, because it changes every validation trace.
7. **Delete hacks.** Remove old code only after new-path scenes and performance tests pass.

---

## 16. Non-goals for the first rewrite

- General convex-polyhedron rigid bodies.
- Full continuous collision detection for arbitrary moving triangle meshes.
- Stacked rigid-body towers.
- Material tables per terrain texture.
- GPU physics.

The first rewrite should remain sphere-focused and terrain-focused. The goal is not to become a full commercial physics engine; the goal is to make the existing ball simulation mathematically consistent enough that rolling and angular response do not require visual correction shims.

---

## 17. Success criteria

The rewrite is complete when:

1. A sphere rolling on terrain satisfies `|vt| < tolerance` during steady rolling without directly assigning `ω = (n × v) / r`.
2. Sphere-sphere glancing collisions generate spin from tangent friction impulses, not from a separate empirical angular response.
3. Head-on sphere-sphere collisions generate no fake spin.
4. Orientation is never directly corrected for visual pole alignment.
5. Penetration is handled by bias/split impulses, not positional-only overlap hacks.
6. Angular velocity is not clamped during normal gameplay.
7. Energy diagnostics show no unexplained energy injection in no-friction/no-drag tests.
8. Existing visual regression scenes remain acceptable.
9. The 300-ball performance scene remains within the agreed performance envelope.

# Plan - Iterative Dynamics Physics

## Scope

This plan starts from an engine where stacking, resting, rolling, and
sleep-island physics have not been built yet.

It describes the system to build from first principles.

The contact solver direction is credited to Erin Catto's 2005 GDC paper on
iterative dynamics: sequential impulses, accumulated constraint impulses,
warm starting, contact caching, friction constraints, and island-level reasoning
provide the foundation for the build.

The only assumed assets are:

- the executable/build harness,
- the scene format or equivalent ability to load the listed scenes,
- the new physics debug rendering capability, or enough of it to add the
  checkpoints described here,
- the three target scenes:
  - `SkullbonezData\scenes\stacking.scene`,
  - `SkullbonezData\scenes\at_rest.scene`,
  - `SkullbonezData\scenes\physics_bench_varied.scene`.


## Renderer Scope

This implementation is OpenGL only.

All debug rendering, visual checkpoints, screenshots, and manual scene reviews
in this plan should use:

```bat
--renderer gl
```

Do not spend this implementation on DX11 or DX12 debug overlay support,
renderer parity, or tri-renderer visual matching. Treat DX11/DX12 support as a
separate later porting task after the OpenGL physics behavior is correct.

## Test Timing Rule

All manual tests, visual checkpoints, screenshots, and acceptance runs for this
implementation must run with vsync on.

Use:

```bat
--vsync on
```

Fixed-step physics is still fine and should be used where determinism matters.
If a scene file, helper script, or inherited test command disables vsync, change
or override it for this implementation so the actual run is vsync on.

## Purpose

Start from a physics engine where balls and boxes mostly just move and bounce.
Build it up until it reaches the intended behavior:

1. Balls roll, slide, bounce, and settle naturally.
2. Boxes collide as oriented boxes, not as bounding spheres.
3. Boxes can rest on terrain without balancing forever on fake edge support.
4. Boxes can stack on boxes.
5. Mixed piles settle and sleep.
6. Sleep happens by connected islands, not isolated bodies freezing one at a
   time.
7. Sleeping bodies are cheap but still wake from real contact.

The development method is visual-first. Every phase has a debug rendering
checkpoint so a programmer can watch the physics, identify what is wrong, and
make the next small change.

## The Three Required Scenes

| Scene | Role |
|------|------|
| `stacking.scene` | The stack proof. A base box, middle box, and top box should settle into a stable tower and sleep as one island. |
| `at_rest.scene` | The false-sleep proof. A mixed bowl of balls and boxes should all settle without any visibly floating sleeper. |
| `physics_bench_varied.scene` | The performance proof. Mixed piles, side clusters, falling objects, rolling balls, and sleepers should remain correct and cheap. |

## Debug Rendering Vocabulary

Use command-line debug controls for the visual checkpoints. The required
arguments are:

| Argument | Values | Use |
|----------|--------|-----|
| `--physics-debug` | `none`, `axes`, `contacts`, `sleep`, `all` | Select the overlay group for the run. |
| `--physics-debug-axes` | `on`, `off` | Force body-axis lines on or off. |
| `--physics-debug-contacts` | `on`, `off` | Force contact-row lines on or off. |
| `--physics-debug-sleep` | `on`, `off` | Force sleep/support/inhibition markers on or off. |
| `--physics-debug-contact-linger` | seconds | Keep short-lived contact rows visible long enough to inspect. |

Use only those debug arguments in this plan's commands.

| Debug view | What to show | Why it matters |
|-----------|--------------|----------------|
| Body axes | Red/green/blue local axes at each body. | Reveals orientation, spin, and whether boxes are solving with the intended frame. |
| Contact rows | Contact point cross, normal arrow, tangent directions, optional pair line. | Proves the solver is receiving real contact geometry. |
| Sleep state | Sleeping body marker. | Shows when bodies become inactive. |
| Support state | Supported body marker. | Shows whether a body is allowed to sleep. |
| Inhibited state | "Do not sleep this frame" marker. | Shows edge/point or unstable contacts that should remain active. |
| Broadphase cells | Occupied cells and cells with narrowphase collision. | Shows candidate-pair coverage and sleeper wake visibility. |

Useful command shape:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug all --physics-debug-contact-linger 0.85 --vsync on
```

Equivalent commands should be used for `at_rest.scene` and
`physics_bench_varied.scene`.

## Target Tick Shape

The finished solver should have this conceptual order:

```text
Start physics tick
  Clear frame-local debug and support markers
  Apply external forces to awake bodies
  Build broadphase over every body, including sleepers
  Generate candidate pairs
  Wake sleepers only from real moving impact or overlap
  Detect and respond to fast impacts
  Generate and solve terrain contacts for awake bodies
  Generate persistent object contacts from real shape manifolds
  Solve persistent object contacts with warm-started sequential impulses
  Seed support from terrain
  Propagate support upward through stack-like object contacts
  Integrate remaining positions and orientations
  Build contact islands
  Advance sleep counters for quiet, supported, uninhibited islands
  Put eligible islands to sleep together
End physics tick
```

The important rule is that terrain is the root of support. Object contacts can
pass support upward through a stack, but object contacts alone must not create
sleep support from nothing.

## Equation Sheet

This section contains the equations taken from classical mechanics & Catto's 2005 GDC paper. 

### Body Integration

Use a right-handed world with positive Y upward. Gravity is negative Y.

```text
invM = 1 / m

v = v + (force * invM) * dt
omega = omega + (I_world_inverse * torque) * dt
x = x + v * dt
```

Orientation can be integrated with an axis-angle delta:

```text
angle = length(omega) * dt
axis = omega / length(omega)
q_next = normalize(delta_quaternion(axis, angle) * q)
```

If `length(omega)` is tiny, skip the orientation delta for that tick.

### Inertia

Solid sphere:

```text
I = (2 / 5) * m * r * r
I_body = (I, I, I)
I_body_inverse = (1 / I, 1 / I, 1 / I)
```

Box with half-extents `hx`, `hy`, `hz`:

```text
Ixx = (m / 3) * (hy * hy + hz * hz)
Iyy = (m / 3) * (hx * hx + hz * hz)
Izz = (m / 3) * (hx * hx + hy * hy)

I_body_inverse = (1 / Ixx, 1 / Iyy, 1 / Izz)
```

World-space inverse inertia for an oriented box:

```text
I_world_inverse_vector(a) =
    R * component_multiply(I_body_inverse, transpose(R) * a)
```

Spheres can use component multiplication directly because their inertia is
isotropic.

### Contact Point Velocity

For a contact point `p`:

```text
rA = p - xA
rB = p - xB

vA_contact = vA + cross(omegaA, rA)
vB_contact = vB + cross(omegaB, rB)
vRel = vB_contact - vA_contact
```

The contact normal `n` always points from body A toward body B. For terrain,
treat terrain as an infinite-mass body B with zero velocity and use a normal
that points out of the terrain into the dynamic body.

```text
vn = dot(vRel, n)
```

Negative `vn` means the contact points are approaching.

### Effective Mass

For any contact direction `d`:

```text
K =
    invMassA + invMassB
  + dot(d, cross(IworldInvA(cross(rA, d)), rA))
  + dot(d, cross(IworldInvB(cross(rB, d)), rB))

effectiveMass = 1 / K
```

If one side is static, set its inverse mass and inverse inertia terms to zero.
If `K` is near zero, the row cannot apply a useful impulse.

### Normal Impulse

Normal rows are push-only:

```text
lambdaN_delta = effectiveMassN * (bias - vn)
lambdaN_new = max(0, lambdaN_old + lambdaN_delta)
lambdaN_apply = lambdaN_new - lambdaN_old
J = n * lambdaN_apply
```

Apply `J` with equal and opposite impulses:

```text
vA = vA - J * invMassA
omegaA = omegaA - IworldInvA(cross(rA, J))

vB = vB + J * invMassB
omegaB = omegaB + IworldInvB(cross(rB, J))
```

For terrain, body B terms are zero and only body A changes.

### Bias And Restitution

Use a contact slop so the solver does not chase tiny numerical overlap:

```text
penetrationError = max(0, penetration - contactSlop)
baumgarteBias = beta * penetrationError / dt
baumgarteBias = min(baumgarteBias, maxBias)
```

Use Baumgarte bias for low-speed resting contacts:

```text
if abs(vn) < restitutionThreshold:
    bias = baumgarteBias
```

Use restitution only for real impacts:

```text
if vn < -restitutionThreshold:
    bias = -restitution * vn
```

For multi-point impact manifolds, divide restitution bias across contact rows or
collapse the impact manifold intentionally.

### Tangent Basis

Build two perpendicular tangent directions from the normal:

```text
seed = (1, 0, 0)
if abs(dot(seed, n)) > 0.9:
    seed = (0, 0, 1)

t1 = normalize(seed - n * dot(seed, n))
t2 = cross(n, t1)
```

### Friction Impulses

Tangential speeds:

```text
vt1 = dot(vRel, t1)
vt2 = dot(vRel, t2)

lambdaT1_delta = effectiveMassT1 * (-vt1)
lambdaT2_delta = effectiveMassT2 * (-vt2)
```

Accumulate both tangent impulses as a 2D vector:

```text
lambdaT1_new = lambdaT1_old + lambdaT1_delta
lambdaT2_new = lambdaT2_old + lambdaT2_delta
```

Clamp to a friction cone:

```text
limit = frictionCoefficient * normalImpulseBudget
mag = sqrt(lambdaT1_new * lambdaT1_new + lambdaT2_new * lambdaT2_new)

if mag > limit and mag > epsilon:
    scale = limit / mag
    lambdaT1_new = lambdaT1_new * scale
    lambdaT2_new = lambdaT2_new * scale
```

Apply only the delta:

```text
Jt = t1 * (lambdaT1_new - lambdaT1_old)
   + t2 * (lambdaT2_new - lambdaT2_old)
```

Then apply `Jt` with the same impulse application equations as the normal row.

### Terrain Resting Friction Budget

Terrain contacts do not necessarily have a previous-frame cache early in the
rebuild. For stable terrain support, seed a normal budget from expected gravity:

```text
normalImpulseBudget =
    mass * abs(gravity) * abs(dot(terrainNormal, worldUp)) * dt / contactCount
```

Do not grant this budget to unstable box edge or point contacts. Those contacts
may use only the normal impulse they actually generate from impact or
penetration.

### Rolling Resistance

Use rolling resistance only for contacts classified as stable resting support.

```text
normalForce = mass * abs(gravity) * abs(dot(contactNormal, worldUp))
rollingTorque = rollingCoeff * normalForce * effectiveRadius
deltaOmega = (rollingTorque / averageInertia) * dt
```

Clamp so rolling resistance cannot reverse angular velocity in one step:

```text
if deltaOmega >= length(omega):
    omega = (0, 0, 0)
else:
    omega = omega - normalize(omega) * deltaOmega
```

### Sphere/Sphere Manifold

```text
delta = centerB - centerA
dist = length(delta)
n = delta / dist

penetration = radiusA + radiusB - dist
pointA = centerA + n * radiusA
pointB = centerB - n * radiusB
p = (pointA + pointB) * 0.5
```

If centers are nearly identical, choose a deterministic fallback normal such as
world up.

### Sphere/Box Manifold

Let `R` be the box orientation matrix.

```text
localCenter = transpose(R) * (sphereCenter - boxCenter)
closestLocal = clamp(localCenter, -halfExtents, halfExtents)
closestWorld = boxCenter + R * closestLocal

delta = sphereCenter - closestWorld
dist = length(delta)
```

Outside case:

```text
nBoxToSphere = delta / dist
penetration = sphereRadius - dist
```

Inside case:

```text
distanceToFaceX = halfExtents.x - abs(localCenter.x)
distanceToFaceY = halfExtents.y - abs(localCenter.y)
distanceToFaceZ = halfExtents.z - abs(localCenter.z)

axis = axis with smallest distanceToFace, using deterministic tie break
sign = sign(localCenter[axis])
nBoxToSphere = R * axisVector(axis, sign)
penetration = sphereRadius + distanceToFaceAxis
```

Orient the final normal from body A to body B.

### Box/Box SAT

Candidate axes:

```text
A0, A1, A2
B0, B1, B2
cross(Ai, Bj) for every i,j
```

Skip cross axes with tiny length. Normalize every accepted axis.

Projection radius of a box onto axis `a`:

```text
radius =
    hx * abs(dot(A0, a))
  + hy * abs(dot(A1, a))
  + hz * abs(dot(A2, a))

center = dot(boxCenter, a)
interval = [center - radius, center + radius]
```

Overlap:

```text
overlap = min(maxA, maxB) - max(minA, minB)
```

If any overlap is negative, boxes are separated. Otherwise the axis with the
smallest positive overlap is the contact normal candidate. Orient it from body A
to body B. Prefer face axes over edge axes for near ties to reduce jitter.

### Face Contact Clipping

For a face/face box contact:

1. Select the reference face from the winning SAT face axis.
2. Select the incident face with the most opposing normal.
3. Clip the incident polygon against the four side planes of the reference face.
4. Keep clipped points whose separation from the reference face is within the
   contact skin.
5. For each kept point:

```text
contactPoint = clippedPoint - referenceNormal * (separation * 0.5)
penetration = -separation
```

Keep at most four contact points.

### Edge Contact

For an edge/edge box contact, compute closest points on the two selected edge
segments.

```text
p = (closestPointOnEdgeA + closestPointOnEdgeB) * 0.5
penetration = SAT_minimum_overlap
```

Emit one contact row.

### Position Correction

Use position correction only as a small post-solve cleanup:

```text
correctionDepth = max(0, penetration - contactSlop)
totalInvMass = invMassA + invMassB

correction = normal * (correctionDepth * percent / totalInvMass)
xA = xA - correction * invMassA
xB = xB + correction * invMassB
```

Suggested `percent` is `0.35` for object contacts and `0.4` for terrain. If the
scene depends on large correction to look stable, the velocity solver or contact
geometry is still wrong.

### Sleep And Wake Equations

Quiet test:

```text
linearQuiet = dot(v, v) < sleepLinearSpeed * sleepLinearSpeed
angularQuiet = dot(omega, omega) < sleepAngularSpeed * sleepAngularSpeed
quiet = linearQuiet and angularQuiet
```

Suggested starting thresholds:

```text
sleepLinearSpeed = 0.5
sleepAngularSpeed = 0.3
sleepFrames = 30
```

Wake energy test:

```text
hasWakeEnergy =
    dot(v, v) >= sleepLinearSpeed * sleepLinearSpeed
 or dot(omega, omega) >= sleepAngularSpeed * sleepAngularSpeed
```

Island sleep rule:

```text
bodyEligible = quiet and supportedThisFrame and not inhibitedThisFrame
islandEligible = every awake body in island is bodyEligible
islandCanSleep = every awake body counter >= sleepFrames
```

When an island sleeps, zero `v` and `omega` for every newly sleeping body.

## Phase 1 - Make The Physics Observable

Goal: add enough visual instrumentation to debug like a human.

Build:

1. A line-rendered physics overlay that can be enabled per scene or by command
   line.
2. Body axes for all dynamic bodies.
3. Contact row rendering:
   - point,
   - normal,
   - two tangents,
   - body-pair link.
4. Sleep, support, and inhibition markers.
5. Optional broadphase cell overlay.
6. Contact linger so one-frame contacts can be inspected visually.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug axes --vsync on
```

Expected:

- Every box has stable axes attached to its rendered body.
- Turning debug rendering on or off does not change physics behavior.

If this fails, fix debug rendering before continuing. Bad overlays create bad
physics conclusions.

## Phase 2 - Make Replays Deterministic

Goal: make every failure repeatable.

Build:

1. Fixed-step physics independent from render frame time.
2. Scene-driven frame counts and `exit_on_complete`.
3. Seed control for any randomized object placement.
4. Optional debug CSV logging for:
   - frame,
   - body id/name,
   - position,
   - velocity,
   - angular velocity,
   - orientation,
   - sleeping,
   - supported,
   - inhibited.
5. Screenshot capture at specific frames for visual comparison.

Visual checkpoint:

Run `stacking.scene` twice. If it falls over, it should fall over the same way
both times. If it stacks, it should stack the same way both times.

Expected:

- Same frame, same body positions, same sleep markers.
- Debug logs are comparable across runs after the schema is fixed.

Validation after implementation:

```bat
tools\validate_physics.bat
```

## Phase 3 - Establish Rigid Body Fundamentals

Goal: bodies need enough real state for impulses to produce translation and
rotation.

Build:

1. Per-body position and orientation.
2. Linear and angular velocity.
3. Mass and inverse mass.
4. Body-space rotational inertia and inverse inertia.
5. World-space inverse inertia for oriented boxes:
   - conceptually `R * I_body_inverse * transpose(R)`.
6. Semi-implicit integration:
   - apply forces into velocity,
   - solve contacts,
   - integrate position and orientation.
7. Quaternion orientation integration from angular velocity.
8. Shape data:
   - sphere radius,
   - box half-extents,
   - conservative broadphase radius.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug axes --vsync on
```

Expected:

- Boxes rotate around plausible axes after off-center contact.
- Spheres and boxes do not respond identically because their inertia differs.
- Local axes remain aligned to visual boxes.

Failure signs:

- Tilted boxes behave as if their inertia axes are world-aligned.
- Angular velocity explodes and must be hidden by clamps.
- Orientation changes without corresponding angular velocity.

## Phase 4 - Replace Bounce-Only Terrain With Contact Rows

Goal: terrain contact should produce bounce, slide, roll, and rest from one
constraint model.

Build:

1. Generate terrain contact rows for awake bodies.
2. Sphere terrain contact:
   - one contact at the bottom point along the terrain normal.
3. Box terrain contact:
   - transform all eight box vertices into world space,
   - measure distance to the terrain contact plane,
   - keep the deepest vertex cluster,
   - allow one-point, two-point, and face-like contacts.
4. For high-speed box terrain impacts, collapse a multi-point terrain manifold
   to a centroid row. This limits angular blow-up on impact while preserving
   full contact patches for resting.
5. For every terrain contact row, compute:
   - contact arm,
   - normal,
   - penetration,
   - two tangents,
   - effective mass along normal and tangents,
   - restitution bias for real impact,
   - Baumgarte bias for low-speed penetration.
6. Solve rows with sequential impulses:
   - accumulated normal impulse is clamped to push-only,
   - tangent impulses are friction-limited,
   - solve for a fixed iteration count with deterministic early-out.
7. Add small rolling resistance after the main normal/tangent solve.
8. Add a small partial position correction only as cleanup.

Suggested initial values:

| Knob | Starting value |
|------|----------------|
| Terrain contact slop | `0.005` |
| Terrain Baumgarte beta | `0.3` |
| Max terrain Baumgarte bias | `2.0` |
| Terrain solver iterations | `20` |
| Rolling resistance coefficient | `0.02` |

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug axes --physics-debug-sleep on --vsync on
```

Expected:

- Balls roll down the bowl and eventually settle.
- Boxes react to terrain using their actual orientation.
- Resting objects are not visibly pumping energy.

Failure signs:

- Balls only slide and never roll.
- Boxes balance forever on one corner.
- Terrain contact launches boxes upward from position correction.

Validation after implementation:

```bat
tools\validate_physics.bat
```

## Phase 5 - Separate Collision From Sleep Support

Goal: a contact can resolve collision without being trusted as rest support.

Build:

1. A terrain contact classification step.
2. A per-body "supported this frame" marker.
3. A per-body "sleep inhibited this frame" marker.
4. Grant rest-only privileges only to credible support contacts:
   - gravity-style normal warm start,
   - static friction floor,
   - rolling resistance rest cleanup,
   - sleep support seed.
5. For boxes on terrain, require a credible footprint:
   - a face normal close enough to the terrain normal,
   - enough real box vertices close to sampled terrain height,
   - stricter acceptance for two-vertex near-plane cases.
6. Still solve collision for unstable contacts. Do not let unstable contacts
   sleep the body.

Suggested starting thresholds:

| Policy | Starting value |
|--------|----------------|
| Vertex cluster threshold | `0.15` world units |
| Low-row face alignment dot | `0.95` |
| Stable plane patch dot | `0.99` |
| Vertex terrain slack | contact skin plus `0.15` |

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug sleep --vsync on
```

Expected:

- Supported boxes show support markers only when they have believable terrain
  support.
- Edge and point contacts show inhibition instead of sleep permission.
- No unsupported body gets a sleep marker.

Failure signs:

- A visibly floating box becomes supported.
- A box flat on terrain never becomes supported.
- Sleep support appears from contact alone.

## Phase 6 - Preserve Fast Impact Response

Goal: fast collisions still bounce or deflect, but impact handling does not
pretend to be rest support.

Build:

1. Broad or swept detection for fast active pairs.
2. On a real impact:
   - advance bodies to collision time,
   - generate contact geometry,
   - apply immediate impact impulses with restitution and friction.
3. Do not mark bodies sleep-supported from an object-object impact.
4. Leave long-lived support to persistent contact rows.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\physics_bench_varied.scene --physics-debug contacts --vsync on
```

Expected:

- Falling and rolling bodies still collide responsively.
- Mid-air collisions do not create support markers.
- Bodies do not tunnel through settled piles.

Failure signs:

- A body turns sleep-supported immediately after a mid-air hit.
- Fast bodies pass through sleeping bodies.
- Immediate response fights later resting contacts and causes jitter.

## Phase 7 - Build Real Object Contact Manifolds

Goal: object contacts must be generated from the real shapes.

Build a fixed-size contact manifold with:

- normal from body A toward body B,
- up to four contact points,
- contact arms from each body center,
- penetration,
- stable feature id.

Shape pairs:

1. Sphere/sphere:
   - normal between centers,
   - contact point midway between surface points,
   - one row.
2. Sphere/box:
   - transform sphere center into box local space,
   - clamp to box extents,
   - use closest point on box,
   - handle inside-box case by choosing nearest face deterministically,
   - one row.
3. Box/box:
   - build both oriented box bases in world space,
   - run separating-axis test across 15 axes,
   - choose minimum-overlap axis,
   - orient normal consistently,
   - for face contacts, clip incident face against reference face side planes,
   - for edge contacts, use closest points on the two edges.

Feature ids:

- must be deterministic,
- must identify individual face-contact rows,
- must distinguish sphere/box, box face, and box edge contacts,
- must remain stable across frames so warm starting works.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug contacts --physics-debug-contact-linger 1.0 --vsync on
```

Expected:

- Box-on-box contact points lie on real box faces.
- Face contacts produce multiple points.
- Contact normals do not flip every frame.
- Tangent lines lie in the contact plane.

Failure signs:

- Contact points are at bounding-radius distance from the boxes.
- A broad face contact has only one anonymous row.
- Contact rows reshuffle frame to frame.

Validation after implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Phase 8 - Add Warm-Started Persistent Object Contacts

Goal: stacks should remember the support force solved last frame.

Build:

1. Convert object manifolds into persistent contact rows.
2. Store a previous-frame impulse cache keyed by body pair plus feature id.
3. Before solving:
   - look up cached normal and tangent impulses,
   - clamp cached tangent impulse to this frame's friction budget,
   - apply cached impulses to temporary solver velocities.
4. Use temporary solver body state for the row solve:
   - linear velocity,
   - angular velocity,
   - inverse mass,
   - inverse inertia,
   - orientation-derived inertia frame for boxes.
5. Solve persistent rows with sequential impulses:
   - normal push-only constraint,
   - two tangent friction constraints,
   - tangent clamp as a 2D friction cone,
   - accumulated impulse clamping,
   - apply only impulse deltas.
6. Use low-speed Baumgarte bias for overlap correction.
7. Apply small positional correction after the velocity solve.
8. Write temporary velocities back to awake bodies.
9. Store non-zero accumulated impulses for the next frame.
10. Emit rows to debug rendering.

Suggested starting values:

| Knob | Starting value |
|------|----------------|
| Object contact slop | `0.005` |
| Object Baumgarte beta | `0.2` |
| Object position correction percent | `0.35` |
| Object solver iterations | `12` |
| Early-out impulse squared | `1.0e-6` |

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug all --physics-debug-contact-linger 1.0 --vsync on
```

Expected:

- Contact rows become steady as the boxes settle.
- The tower does not need to rediscover its support impulse from zero every
  frame.
- No visible pumping, sinking, or exploding.

Failure signs:

- Warm starting injects energy.
- Stack stability depends on excessive position correction.
- Diagonal sliding friction is stronger than axis-aligned friction.

Validation after implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Phase 9 - Add Broadphase For Scale And Wake-Up

Goal: avoid all-pairs contact generation and keep sleepers discoverable.

Build:

1. A uniform spatial grid or equivalent broadphase.
2. Insert every body, awake or sleeping.
3. Use conservative bounds so no possible contact is missed.
4. Generate unique candidate pairs.
5. Feed the candidate list to:
   - fast impact detection,
   - object manifold generation,
   - sleeper wake tests.
6. Make pair ordering deterministic.
7. Keep debug rendering observational.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\physics_bench_varied.scene --physics-debug all --vsync on
```

Toggle the broadphase overlay while watching the scene.

Expected:

- Candidate cells cover every active and sleeping cluster.
- Sleeping bodies remain visible to approaching active bodies.
- Toggling overlay does not change behavior.

Failure signs:

- Sleeping stacks cannot be woken.
- Candidate order changes between deterministic runs.
- Debug rendering alters broadphase state.

Validation after implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Phase 10 - Wake Sleepers From Real Contact

Goal: sleepers are cheap, but never frozen inside active objects.

Build:

1. Per-body sleep state and sleep counter.
2. If one broadphase candidate sleeps and the other is awake:
   - ignore it if the awake body has too little linear and angular energy,
   - otherwise test for swept impact or persistent overlap.
3. Wake only from real impact or overlap, not proximity alone.
4. On wake:
   - clear sleep state,
   - clear sleep counter,
   - clear visual island id,
   - allow the body to receive forces and integrate in the same tick.
5. If both bodies sleep, skip the pair.

Suggested wake thresholds:

| Threshold | Starting value |
|-----------|----------------|
| Linear wake speed | `0.5` units/s |
| Angular wake speed | `0.3` rad/s |

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug sleep --vsync on
```

Expected:

- Purple sleeping objects stay still when untouched.
- A real hit wakes a sleeper immediately.
- Woken bodies resume gravity without a one-frame hitch.

Failure signs:

- Sleepers wake from nearby non-contacting objects.
- Sleepers remain frozen while overlapped.
- Woken bodies do not move until a later tick.

## Phase 11 - Define Sleep Support

Goal: slow is not enough; sleep requires credible support.

Build:

1. Per-body support marker for this frame.
2. Per-body sleep-inhibition marker for this frame.
3. Directed support edges from object contacts.
4. Terrain is the only root support source.
5. Terrain seeds support only when its contact passed the support classifier.
6. Terrain inhibits sleep when contact resolved collision but did not prove
   support.
7. Object contacts create possible support edges only when the normal has a
   meaningful vertical component.
8. Object contacts do not directly mark either body as supported.

Suggested vertical support threshold:

```text
abs(contactNormal.y) > 0.25
```

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug sleep --vsync on
```

Expected:

- Base support appears from terrain.
- Middle support appears only after base support can pass through object contact.
- Top support appears only after middle support can pass through object contact.
- Mid-air collisions do not create support markers.

Failure signs:

- A side hit creates support.
- Support propagates downward from an unsupported top body.
- A stack never becomes supported even when stable.

## Phase 12 - Propagate Stack Support

Goal: support climbs through a stack only from a proven base.

Build:

1. After terrain contact classification and object manifold generation, process
   directed support edges.
2. If the supporter is supported this frame, mark the supported body supported.
3. If the supporter is already sleeping, treat it as proven support.
4. Repeat until no support changes, bounded by body count.
5. Keep the support graph frame-local.

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug sleep --vsync on
```

Expected:

- Support climbs base to mid to top.
- Support disappears from bodies that lose contact.
- The stack cannot sleep until all awake members are quiet and supported.

Failure signs:

- Support leaks sideways.
- Support persists after contact disappears.
- Sleeping becomes order-dependent.

## Phase 13 - Sleep Whole Islands Together

Goal: prevent partial stack freeze.

Build:

1. Build contact islands from persistent object contacts.
2. For each awake body, evaluate:
   - linear speed below threshold,
   - angular speed below threshold,
   - supported this frame,
   - not inhibited this frame.
3. An island is eligible only if every awake body in it passes.
4. Each awake body accumulates consecutive eligible frames.
5. An island sleeps only after every awake body in it reaches the required
   counter.
6. When an island sleeps:
   - assign a stable visual island id,
   - mark all eligible awake bodies sleeping,
   - zero residual linear velocity,
   - zero residual angular velocity.

Suggested sleep thresholds:

| Threshold | Starting value |
|-----------|----------------|
| Linear sleep speed | `0.5` units/s |
| Angular sleep speed | `0.3` rad/s |
| Consecutive quiet frames | `30` |

Visual checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug all --vsync on
```

Expected:

- The stack sleeps as one island.
- No single box in the tower freezes while neighbors keep resolving.
- Purple sleeping bodies show no visible drift.

Visual false-sleep checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug sleep --vsync on
```

Expected:

- No floating body sleeps.
- Orange inhibition blocks sleep when contact is not credible support.

Failure signs:

- A base sleeps while the top is still moving.
- A body sleeps while falling slowly.
- Inhibition markers are ignored.

Validation after implementation:

```bat
tools\validate_physics.bat
```

## Phase 14 - Make Rest Cheap

Goal: correct sleeping should reduce cost.

Build:

1. Skip force application for sleepers.
2. Skip terrain response for sleepers.
3. Skip integration for sleepers.
4. Still insert sleepers into broadphase.
5. Treat sleepers as static anchors in persistent contact solving.
6. Skip pairs where both bodies sleep.
7. Use retained buffers for candidate pairs, contact rows, solver bodies,
   support edges, islands, and debug rows.
8. Avoid hot-path allocation.

Visual/perf checkpoint:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\physics_bench_varied.scene --physics-debug sleep --vsync on
```

Expected:

- Early frames are busy.
- Later frames contain stable sleeping islands.
- Physics cost drops as more bodies sleep.
- Active falling objects can still wake sleepers.

Failure signs:

- Most bodies are sleeping but physics cost stays flat.
- Sleepers are cheap only because unsupported bodies froze incorrectly.
- Sleepers cannot wake because they were removed from broadphase.

Validation after implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Phase 15 - Prove The Three Required Scenes

### `stacking.scene`

Run:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\stacking.scene --physics-debug all --physics-debug-contact-linger 1.0 --vsync on
```

Acceptance story:

1. Base lands on terrain and gains support.
2. Middle lands on base and forms real box-face contact rows.
3. Top lands on middle and forms stable contact rows.
4. Support propagates upward through the stack.
5. All three bodies become one quiet island.
6. The whole island sleeps.

### `at_rest.scene`

Run:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\at_rest.scene --physics-debug all --physics-debug-contact-linger 0.85 --vsync on
```

Acceptance story:

1. Balls roll and settle in the terrain bowl.
2. Boxes collide with terrain using oriented-box contacts.
3. Edge/point terrain contacts inhibit sleep.
4. Object contacts pass support only from a proven base.
5. Final sleepers are all visibly supported.
6. No body sleeps while floating.

### `physics_bench_varied.scene`

Run:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\physics_bench_varied.scene --physics-debug all --physics-debug-contact-linger 0.60 --vsync on
```

Acceptance story:

1. Initial piles begin with credible support.
2. Falling and rolling bodies create contact churn.
3. Side clusters exercise broadphase and wake-up.
4. Sleep appears only after quietness and support agree.
5. Resting islands reduce physics cost.

## Validation Ladder

| Change | Validation |
|--------|------------|
| Documentation only | `tools\validate_fast.bat` |
| Debug scene or docs | `tools\validate_fast.bat` plus manual visual scene run if expectations changed |
| Solver, terrain contacts, manifolds, sleep | `tools\validate_physics.bat` |
| Broadphase or hot path | `tools\validate_physics.bat` and `tools\validate_perf.bat` |
| OpenGL debug overlay work | Manual `--renderer gl` scene checks plus physics validation if physics data changed |
| Full rebuild of this plan | `tools\validate_full.bat` |

Visual inspection finds the bug. Validation output accepts the change.

## Completion Criteria

The rebuild is complete when:

1. `stacking.scene` forms a stable three-box tower and sleeps it as one island.
2. `at_rest.scene` settles every body without floating sleepers.
3. `physics_bench_varied.scene` stays correct while showing a real rest-cost win.
4. Contact debug rows lie on real object surfaces.
5. Support is terrain-rooted or propagated through a proven stack.
6. Object-object mid-air contacts do not seed sleep.
7. Sleepers remain in broadphase and wake from real impact or overlap.
8. Fixed-step validation is deterministic.
9. Performance validation confirms rest is cheap.

## Things Not To Do

- Do not fix stability by adding global damping.
- Do not lower gravity, restitution, or scene energy to hide bugs.
- Do not let "has any contact" mean "can sleep."
- Do not remove multi-point box contacts to make stacks cheaper.
- Do not use bounding radius as final object contact geometry.
- Do not remove sleeping bodies from broadphase.
- Do not update physics baselines without reviewing the behavior change.

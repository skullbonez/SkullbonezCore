# Plan - Box Stacking And Object Contact Manifolds

**Date:** 2026-06-01
**Status:** Draft for review - updated after Catto audit implementation pass
**Current edit type:** Documentation only
**Primary future impact areas:** Physics, collision, solver, performance, validation baselines
**Last reviewed against branch:** `implement-catto-physics-audit`

## Update Summary - Current Branch State

This plan originally assumed the object contact solver was still missing several Catto-style support pieces. The current branch has since moved the solver forward. The remaining work is now more clearly a narrowphase/contact-manifold problem, not a wholesale solver rewrite.

Already implemented in this branch:

- `GameModelCollection::SolvePersistentObjectContacts` now uses retained compact solver body state (`m_solverBodies`) and writes velocities back once after PGS iteration.
- Persistent contacts now carry `featureId`, and the warm-start cache key is pair + feature, not just pair.
- The persistent contact cache is sorted and queried with `lower_bound` instead of scanning the whole previous-frame cache.
- Warm-started impulses are applied to the mutable solver state before iteration, matching Catto Algorithm 4's `a = B * lambda` initialization shape.
- Object persistent friction now clamps the two tangent accumulators as a 2D vector cone, preventing diagonal friction from exceeding the intended budget.
- Box inverse inertia is applied in world space in terrain, immediate object response, and persistent object contacts.
- The object response migration previously unified sphere-sphere, sphere-box, and box-box fallback contacts before the persistent solver became the single dynamic response owner.
- Terrain response returns whether its contact is safe for sleep. Under-constrained terrain box contacts inhibit sleep until a local box face normal is sufficiently aligned to the actual terrain contact plane normal.
- The previous bespoke box toppling assists were removed. There is no forced angular "fall to face" launcher and no gravitational toppling torque block. The solver now relies on contact resolution plus sleep eligibility.
- The physics logger includes orientation (`qX,qY,qZ,qW`), `sleeping`, and `sleepInhibited`; the deterministic physics regression baseline has been updated for that schema and the accepted solver behavior.
- `SkullbonezGameModelCollection.cpp` now includes detailed Catto references to `Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf`, with engine-specific/novel behavior explicitly marked.

Still not implemented:

- Real object-object shape-pair manifold generation.
- Exact sphere-box closest-point contacts for object-object response.
- OBB-vs-OBB SAT overlap and face/edge manifold clipping.
- Shared manifold consumption by both immediate impact response and persistent resting support.
- Focused stack/manifold regression scenes.

## Context

The current object-object path cannot produce correct box stacking because the contact geometry is still based on bounding radii.

The engine already has useful pieces:

- `CollisionShape` is a `std::variant<BoundingSphere, BoundingBox>`, so compile-time shape-pair dispatch already exists.
- Box-vs-terrain uses oriented box vertices and creates multiple terrain contact rows.
- The persistent object contact solver already has warm starting, tangent friction rows, Baumgarte bias, Projected Gauss-Seidel iteration, compact solver body state, world-space box inertia, sorted cache lookup, feature IDs, and 2D tangent cone clamping.
- Broadphase and pair collection already exist.

The missing part is real object-object narrowphase manifold generation. The current solver can process contacts, but it is being fed the wrong contacts for boxes.

Important distinction:

- Terrain contacts already use terrain-plane/OBB vertex contact rows. They are not the primary blocker for box stacking.
- Object-object contacts still use bounding-radius contact geometry. That is the blocker.

## Current Failure

### Object-object detection

`GameModel::GetModelCollisionTime` dispatches to `TestShapeCollision`, but `BoundingBox::TestCollision` approximates boxes as bounding spheres:

- Box-sphere uses `box.GetBoundingRadius() + sphere.GetRadius()`.
- Box-box uses `boxA.GetBoundingRadius() + boxB.GetBoundingRadius()`.

That is acceptable as a broadphase or conservative early candidate test, but it is not a valid narrowphase for boxes.

### Immediate response

The old immediate object response path ran through a shared contact-point impulse path, but the contact arms were still based on bounding radii:

```cpp
float br1 = GetShapeBoundingRadius( gameModel1.m_boundingVolume );
float br2 = GetShapeBoundingRadius( gameModel2.m_boundingVolume );
Vector3 rContact1 = normal * br1;
Vector3 rContact2 = normal * ( -br2 );
```

This means a box uses box inertia and orientation, but its contact point lies on the circumscribed sphere rather than on the actual box surface.

### Persistent resting contacts

`GameModelCollection::SolvePersistentObjectContacts` now has the right Catto-style solver shape for stacking support, including per-feature warm starting. However, its contact generator is still a single center-to-center bounding-sphere fallback contact with `featureId = 0`.

That cannot support box-on-box stacking:

- A face contact needs up to 4 contact points, not 1.
- Edge contacts need stable feature IDs.
- Contact normals must come from actual box features, not center-to-center direction.
- Contact distance must be based on the real OBB surfaces, not corner radius.

### Terrain sleep-gate note

The recent at-rest cube bug was not solved by adding a toppling special case. The accepted current fix is narrower:

- classify whether a box-terrain contact manifold is sleep-safe against the local terrain plane normal;
- inhibit sleep when the contact is an under-constrained edge/vertex support that is not face-stable;
- allow the existing contact solve to keep running until the box reaches a plausible rest face.

Future object-object manifold work should follow that same philosophy: improve contact geometry first, and avoid manual impulses or damping that hide bad manifolds.

## Goal

Replace bounding-radius object contact geometry with real shape-pair contact manifolds while preserving the existing sequential impulse solver direction.

Target behavior:

1. Sphere-sphere remains exact and cheap.
2. Sphere-box uses closest-point-on-OBB contacts.
3. Box-box uses SAT for overlap and clipping for a face/edge manifold.
4. Persistent contacts keep stable feature IDs so warm starting works.
5. Immediate impacts and resting contacts use the same manifold representation.
6. Bounding radii remain only as broadphase or conservative swept candidate data.
7. The existing compact PGS solver state, sorted cache, feature keying, and 2D friction cone are preserved.

## Non-Goals

- Do not add cylinders yet.
- Do not add generic convex hulls yet.
- Do not rewrite the entire solver before fixing contacts.
- Do not tune stacking by adding damping, sleeping hacks, or larger correction constants before the manifold is correct.
- Do not update physics baselines until the new behavior is reviewed and intentionally accepted.
- Do not remove terrain sleep-plane handling while working on object-object contacts. It fixes a separate, accepted terrain rest-state issue.
- Do not reintroduce explicit toppling manual impulses or gravity torque assists for boxes.

## Target Design

### Contact structures

Add a reusable object contact representation, likely near `SkullbonezGameModelCollection` first or in new narrowphase files if the implementation grows:

```cpp
struct ObjectContactPoint
{
    Vector3 point;        // World-space contact point.
    Vector3 rA;           // point - bodyA center.
    Vector3 rB;           // point - bodyB center.
    float penetration;    // Positive when overlapping.
    uint32_t featureId;   // Stable pair-local feature id for warm starting.
};

struct ObjectContactManifold
{
    uint16_t bodyA;
    uint16_t bodyB;
    Vector3 normal;       // Points from bodyA to bodyB.
    ObjectContactPoint points[4];
    uint8_t pointCount;
};
```

Keep the max contact count small and fixed to avoid hot-path allocations. Four points is enough for box face contacts and matches common real-time rigid body practice.

Current cache-key detail:

`PersistentContact::featureId` exists today and is packed into the current cache key as a 16-bit feature field:

```cpp
uint64_t packed = ( uint64_t(lo) << 40 ) |
                  ( uint64_t(hi) << 16 ) |
                  uint64_t(featureId & 0xffffu);
```

That is enough for the initial sphere/box/box feature IDs proposed here. If future generic convex hulls need more feature identity space, update the key packing deliberately instead of silently truncating.

### Solver row mapping

The future manifold should map to existing `PersistentContact` rows:

| Manifold field | Existing row field |
|----------------|--------------------|
| `bodyA/bodyB` | `PersistentContact::bodyA/bodyB` |
| `normal` | `PersistentContact::normal` |
| `point - centerA` | `PersistentContact::rA` |
| `point - centerB` | `PersistentContact::rB` |
| `penetration` | `PersistentContact::penetration` |
| `featureId` | `PersistentContact::featureId` and cache key |

The solver already computes tangent axes, effective masses, bias, friction limits, warm-start impulses, PGS iteration, and cache storage. The manifold generator should not duplicate that solver work.

### Narrowphase API

Add shape-pair contact builders:

```cpp
bool BuildObjectContactManifold(
    const GameModel& a,
    const GameModel& b,
    uint16_t bodyA,
    uint16_t bodyB,
    ObjectContactManifold& out );
```

Internally dispatch by shape pair:

- `SphereSphereManifold`
- `SphereBoxManifold`
- `BoxSphereManifold`
- `BoxBoxManifold`

The function should return false when shapes are separated beyond the contact skin.

Recommended placement:

- Start with a small new helper module if the implementation is more than a few local helpers:
  - `SkullbonezObjectContactManifold.h`
  - `SkullbonezObjectContactManifold.cpp`
- Keep the API free of allocations and expose fixed-size manifold output.
- Keep shape dispatch explicit using the existing `CollisionShape` variant.
- Avoid putting manifold construction inside the PGS iteration loop.

### Contact skin

Use a small contact skin based on `Cfg().contactEpsilon` so resting contacts are generated before visible penetration becomes large.

Important: keep contact skin separate from visible geometry. The normal, point, and penetration must still be derived from the real shapes.

### Catto reference alignment

This plan should stay aligned with the solver comments now present in source:

- Contact model: Catto 2005 local PDF p. 9, Section 4.
- Normal constraint: p. 9, Equations 16-19.
- Baumgarte/contact bias: p. 8 Equation 15 and p. 10 Equation 20.
- Friction constraints: pp. 11-12, Equations 21-25.
- World-space inertia: p. 12, Section 5.
- Time stepping / `JB * lambda = eta`: p. 14, Equations 34-35.
- PGS: pp. 16-17, Algorithm 4.
- Contact caching and feature IDs: pp. 18-19, Algorithm 5.

When implementing manifold phases, keep comments similarly explicit about whether a detail is Catto-derived or Skullbonez-specific.

## Current Solver Invariants To Preserve

- PGS operates on `m_solverBodies` and writes velocities back once.
- Cached impulses must be applied to solver state before iteration.
- `m_persistentContactCache` stays sorted by key before binary lookup.
- Friction tangent accumulators in persistent contacts stay clamped as a 2D vector cone.
- Box inverse inertia uses `R * I_body^-1 * R^T`.
- Immediate object response and persistent object support should converge toward the same `ObjectContactManifold` input.
- Physics determinism remains byte-exact for fixed-step validation after intentional baseline updates.

## Implementation Phases

### Phase 0 - Add focused failing coverage first

Add deterministic scenes or regression cases that expose the current bug:

1. Single box resting on terrain, as a control. This should already behave reasonably.
2. Box dropped onto another box.
3. Three-box vertical stack.
4. Sphere dropped onto a box face.
5. Sphere grazing a box corner.
6. Thin box stack, to catch corner-radius false contacts.
7. Box stack on a mild slope, to keep the sleep-plane normal issue covered.
8. Box resting on box with initial yaw/pitch, to catch unstable feature ID churn.

Expected current behavior should be documented before changing code. If the current output is visibly wrong, do not bless it as a new baseline. Use the cases as development probes first, then update baselines only after the fixed behavior is accepted.

Useful existing probes:

- `SkullbonezData/scenes/at_rest.scene` now verifies terrain sleep-plane behavior with logger support.
- `box_slope_test.scene` and `box_flush_test.scene` are useful terrain controls but do not prove object-object manifolds.
- New stack scenes should log orientation and `sleepInhibited` so bad rest states are obvious.

### Phase 1 - Extract object contact rows without changing behavior

Refactor the persistent object contact build step so contact rows are created from an `ObjectContactManifold`, but initially fill that manifold with the existing bounding-sphere fallback.

Purpose:

- Keep the first code change behavior-neutral.
- Give immediate and persistent response one shared contact format.
- Make later shape-pair work localized to manifold generation.
- Preserve current `featureId = 0` fallback and sorted warm-start cache behavior.

Validation after this phase:

- `tools\validate_physics.bat`
- `tools\validate_perf.bat` if contact row construction or retained buffers change.
- `tools\validate_full.bat` if `SkullbonezGameModelCollection*` changes are broad enough to affect render test orchestration.

### Phase 2 - Sphere-sphere exact manifold

Replace the fallback for sphere-sphere with an explicit exact manifold:

- `normal = normalize(centerB - centerA)`.
- `pointA = centerA + normal * radiusA`.
- `pointB = centerB - normal * radiusB`.
- `contactPoint = 0.5f * (pointA + pointB)`.
- `penetration = radiusA + radiusB - distance`.
- `featureId = 0`.

This should preserve current sphere behavior while making it explicit and testable.

Implementation note:

- The existing immediate response already behaves like center-to-center sphere contact, but this phase makes that geometry explicit and shared.
- Keep `featureId = 0`.

### Phase 3 - Sphere-box manifold

Implement exact sphere-OBB contact:

1. Transform the sphere center into box local space.
2. Clamp local coordinates to `[-halfExtents, +halfExtents]`.
3. Transform the closest point back to world space.
4. Compute the normal from closest point to sphere center.
5. Compute penetration from sphere radius and distance.

Inside case:

- If the sphere center is inside the box, choose the nearest box face as the normal.
- Use that face as the contact feature.
- Penetration should push the sphere out along the shallowest axis.

Feature IDs:

- Encode the box face for face contacts.
- Encode a special inside-face id when the center starts inside.
- For sphere-box, one contact point is enough.

This phase fixes sphere-on-box support and removes the most obvious false positives from the current radius path.

Implementation notes:

- For `BoxSphereManifold`, either call `SphereBoxManifold` and flip `bodyA/bodyB` plus normal, or implement both explicitly. Be very careful that `normal` always points from bodyA to bodyB.
- For inside contacts, choose face normal deterministically. Tie-break local axes in fixed order, for example X before Y before Z only if depths are equal within tolerance.
- Feature ID can reserve high bits for shape pair/contact class and low bits for face id.

### Phase 4 - Box-box SAT overlap

Implement OBB-vs-OBB SAT as the boolean and normal/depth finder.

Test 15 axes:

- 3 face normals from box A.
- 3 face normals from box B.
- 9 cross products of box edge axes.

For each axis:

- Skip near-zero cross axes.
- Project both OBBs.
- Compute overlap.
- Track the axis with minimum positive overlap.

The final axis is the collision normal candidate. Orient it from bodyA to bodyB.

Failure means separated, no manifold. Success means build a contact manifold from the selected features.

Implementation notes:

- Use deterministic tie-breaking for near-equal overlaps. Prefer face axes over edge axes when overlaps are within tolerance; this reduces edge-edge jitter in near-face contacts.
- Normalize candidate axes before projection except when skipping near-zero cross products.
- Keep a record of the winning axis type:
  - A face axis,
  - B face axis,
  - edge-edge axis.
- Store enough feature metadata from SAT to build stable feature IDs in Phase 5.

### Phase 5 - Box-box contact manifold by clipping

For face contacts:

1. Pick reference box and reference face from the minimum-overlap SAT axis.
2. Pick incident face on the other box with the most opposing normal.
3. Clip the incident face polygon against the four side planes of the reference face.
4. Keep clipped points behind or within the reference face plane.
5. Produce up to 4 contact points with penetration depths.

For edge-edge contacts:

1. When the minimum SAT axis is an edge cross-product, compute closest points between the two selected edges.
2. Use their midpoint as the contact point.
3. Generate one contact point.
4. Encode both edge ids into `featureId`.

Feature IDs:

- Face-face contacts should encode reference face, incident face, and clipped vertex identity where practical.
- Edge-edge contacts should encode edgeA and edgeB.
- IDs must be deterministic and stable across frames for warm starting.
- Keep IDs within the current 16-bit `featureId` packing unless you also update cache-key packing.

Suggested feature ID layout while the key has 16 feature bits:

```text
bits 15..14: contact kind
             00 sphere/fallback
             01 sphere-box face
             10 box-box face
             11 box-box edge
bits 13..10: reference feature
bits  9..6 : incident feature
bits  5..0 : clipped point / vertex / reserved
```

This is only a starting layout; document the final layout next to the encoder.

### Phase 6 - Replace object collision response contact geometry

Update both object response paths to consume the generated manifold:

- Immediate impact response should use actual contact points and normals.
- Persistent resting support should use the same manifold points.
- Remove or isolate bounding-sphere contact arms from object-object response.

The existing solver equations can remain mostly intact because they already operate on `rA`, `rB`, normal, tangent axes, effective mass, accumulated impulses, and bias.

Expected code movement:

- Shared manifold generation should live outside the solver iteration loop.
- Solver rows should be fixed-size or retained vectors, not newly allocated per pair.
- Pair-specific dispatch should be explicit and compiler-checked.
- Immediate response should either:
  - process all manifold points, or
  - collapse high-speed multi-point manifolds intentionally, as terrain currently does.
- Persistent support should preserve all stable manifold points so stacks have face support.
- Do not use bounding radius contact arms in either path after this phase, except as a named fallback for unsupported shapes.

### Phase 7 - Broadphase cleanup

Keep bounding radius or AABB logic only in broadphase and candidate generation.

Options:

- Current bounding-radius swept test can remain as conservative pair discovery while the narrowphase rejects false contacts.
- Prefer swept/expanded AABBs for object pairs if that fits the existing `SkullbonezSpatialGrid` better.

Do not let broadphase candidate tests set authoritative collision normals or contact points.

Current branch note:

The persistent solver still receives candidate pairs from the existing broadphase and then rejects non-overlaps using the fallback contact distance. After real manifolds exist, the broadphase can remain conservative; the narrowphase must be authoritative.

### Phase 8 - Determinism and baseline update

The fixed box contact behavior will change physics CSV output. Treat that as intentional only after reviewing:

- Box stacks settle without visible gaps.
- Sphere-box contacts occur at actual faces/corners.
- Boxes on boxes do not hover at corner-radius distance.
- Repeated fixed-step runs are byte-exact.
- Performance remains acceptable in box-pile scenes.

Only then update baselines with explicit commit notes explaining the behavior change.

Baseline/logger note:

The physics CSV schema currently includes:

```text
frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,grounded,sleeping,sleepInhibited
```

Keep this schema while developing manifolds unless a new diagnostic column is genuinely needed. If schema changes, update `TestOutput/baselines/physics_regression_solver.csv` intentionally and call it out.

### Phase 9 - Retire fallback contact paths

After exact shape-pair manifolds are validated:

- Remove or clearly isolate object-object bounding-radius contact-arm code.
- Keep bounding radius for broadphase and conservative swept candidate discovery only.
- Ensure comments in old object response notes no longer describe fallback geometry as the primary path.
- Update this plan and `Agentic/Reference/physics-overview.md` if solver ownership changes.

## Detailed Shape Algorithms

### Sphere-box closest point

Given sphere center `Cs`, box center `Cb`, box rotation `R`, half extents `h`:

```text
local = R^T * (Cs - Cb)
closestLocal = clamp(local, -h, h)
closestWorld = Cb + R * closestLocal
delta = Cs - closestWorld
```

If `length(delta) > tolerance`, use `normal = delta / length(delta)`.

If the sphere center is inside the box, choose the local axis with minimum distance to a face and push along that face normal.

### Box projection onto axis

For OBB center `C`, axes `u0,u1,u2`, half extents `h0,h1,h2`:

```text
center = dot(C, axis)
radius = h0 * abs(dot(u0, axis)) +
         h1 * abs(dot(u1, axis)) +
         h2 * abs(dot(u2, axis))
interval = [center - radius, center + radius]
```

This should be a small inline helper. It is hot path code.

### Face clipping

Use a deterministic Sutherland-Hodgman clip against the reference face side planes.

Keep polygon vertices in fixed-size arrays:

```cpp
Vector3 in[8];
Vector3 out[8];
int inCount;
int outCount;
```

Do not allocate. Maintain deterministic ordering by using a fixed face vertex order.

## Risks

| Risk | Mitigation |
|------|------------|
| Contact normal flips between frames | Orient all normals from bodyA to bodyB and use deterministic tie-breaking for equal SAT overlaps. |
| Warm starting becomes unstable | Add stable feature IDs before enabling multi-point warm starting. Start with no warm-start for new box-box points if needed, then enable carefully. |
| Edge-edge contacts jitter | Use tolerances for near-parallel edges and prefer face contacts when overlaps are nearly tied. |
| Thin boxes produce huge angular response | Clamp bias, use contact slop, and verify inertia math for boxes. Do not hide this with global damping. |
| Broadphase misses fast contacts | Keep conservative swept candidate generation until a better temporal AABB path exists. |
| Validation baselines change widely | Add focused scenes first, review behavior visually/logically, then update baselines intentionally. |
| Feature IDs alias after manifold expansion | Keep feature encoding documented and within current 16-bit key field, or update key packing with tests. |
| Persistent cache lookup loses determinism | Keep cache sorting and deterministic key construction; avoid unordered containers in the hot path. |
| Sleep hides manifold bugs | Use logger orientation and `sleepInhibited`; do not treat sleep as proof that the contact is physically plausible. |

## Validation Plan

For this documentation-only plan, run:

```bat
tools\validate_fast.bat
```

For implementation phases, run at minimum:

```bat
tools\validate_physics.bat
```

Also run:

```bat
tools\validate_perf.bat
```

when touching solver hot paths, retained contact buffers, broadphase, or pair generation.

Before merging the full fix, run:

```bat
tools\validate_full.bat
```

because the implementation will likely touch physics, `SkullbonezGameModelCollection*`, test baselines, and performance-sensitive code.

## Completion Criteria

- Bounding radii are no longer used as object-object contact geometry.
- Sphere-box contacts use closest-point OBB geometry.
- Box-box contacts use SAT plus a real contact manifold.
- Persistent object contacts can store and warm-start multiple contact points per pair.
- A box stack can settle without visible corner-radius gaps.
- Physics validation is deterministic and baselines are updated only after review.
- Focused stack/manifold scenes exist and are documented.
- Source comments clearly identify Catto-derived steps versus Skullbonez-specific improvements.

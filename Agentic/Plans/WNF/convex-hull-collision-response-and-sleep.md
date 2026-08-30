# Convex Hull Collision Response And Sleep Quality

Date: 2026-08-30
Status: WNF — owner-requested parked plan; restore to `TODO/` only by explicit
owner decision. 0/8 phases complete.
Owner: Physics convex-contact pipeline
Impact area: convex-hull assets and mass properties, object CCD, object and
terrain manifolds, persistent contact response, sleep classification, tests,
diagnostics, deterministic scenes, replay state, and performance
Priority: Parked correctness and physical-quality follow-up

## Owner Direction

Convex hulls must stop behaving like loose spherical proxies. A bounding sphere
may reject an impossible object-pair sweep or provide a coarse time bracket, but
it must never authorize a hull collision, advance either body, consume
`timeRemaining`, wake a target, or publish a hit without an exact convex-shape
confirmation.

The exact confirmation is not a vertex-containment test alone. Convex polyhedra
can intersect through edge/edge configurations while every vertex remains
outside the other shape. Use the existing complete discrete SAT test — both
shapes' face normals plus useful edge-cross-edge axes, with all vertices
projected — or an equivalent support-mapped GJK distance/shape cast. The
existing SAT and clipped manifold remain the response geometry at the accepted
time of impact.

Collision response, persistent-contact lifetime, support geometry, and sleep
eligibility are separate decisions. A hull must not lose friction merely because
it is not yet a credible sleep candidate. A point or line support must not become
sleep-stable merely because it happens to emit one or two solver rows.

This file records a future implementation direction only. While it remains in
`WNF/`, it grants no source-edit, generated-asset, scene, golden-baseline, commit,
or push authority.

## Read-Only Evaluation Snapshot

This evaluation inspected `codex/replay-capture-bugfixes` and its working tree on
2026-08-30. No executable, simulation scene, or validation suite was run. The
observations are source-level findings and must be reproduced by CH0 before a
behavior change is accepted.

The working tree contains a large, user-owned implementation experiment for
`Agentic/Plans/TODO/persistent-simulation-island-sleep.md`. The committed hull
CCD, manifold, mass-property, friction, and support behavior described below is
not part of that experiment. The experiment improves body-local deactivation and
island-wide transitions, but its new narrow-support state is still explicitly
box-only. This plan must rebase on the accepted result of that work rather than
editing through it.

## Findings, In Priority Order

### 1. Promoted Hull CCD Can Report A Bounding-Sphere False Hit

The discrete hull path is not spherical. `ObjectContactManifold.cpp` builds a
full polytope, runs face and edge-cross-edge SAT, clips the incident face, and
reduces the result to at most four points.

The promoted object-pair sweep is different:

- `ConvexHullShape::TestCollision` uses `SweptBoundingRadiusCollision` for
  sphere/hull, box/hull, and hull/hull pairs.
- `BoundingBox::TestCollision` also uses a bounding-radius candidate for box/box
  and box/hull pairs.
- `RefineObjectSweepContactTime` correctly calls the exact discrete manifold at
  the coarse time and at 48 later samples, then binary-searches a found contact
  window.
- If none of those exact samples touches, it returns the original coarse sphere
  time anyway. The caller records a swept hit, advances both bodies, subtracts
  that time from both integration remainders, and publishes collision side
  effects.

For an elongated, flat, or irregular hull, the loose bounding sphere can overlap
well before the hull. The current fallback therefore turns an unconfirmed broad
candidate into authoritative motion. It can make a hull stop short, wake on a
near miss, or move in visible chunks. A fixed 48-sample search can also step over
a narrow real contact interval, so changing the fallback to “miss” is the
minimum correctness floor, not the final continuous-collision algorithm.

### 2. Hull/Terrain Friction Is Disabled By Sleep Geometry

`TerrainContactManifold.cpp` sets:

```text
allowsTangentFriction = !isConvexHull || supportsRestingPolicy
```

The shared solver then zeroes both tangent effective masses when this flag is
false. Rolling and spin resistance are also excluded. A hull on terrain thus has
no tangential collision response until it passes a strict face-and-vertex
footprint proof.

That proof shares box-named thresholds and currently requires, for a small
manifold, a face normal within about 18 degrees of the terrain normal followed
by either three supported vertices or a more restrictive two-vertex/three-row
near-coplanar patch. This is useful evidence for sleep, but it is not a valid
reason to turn Coulomb friction off during impact, tipping, or slope contact.
Low-vertex hulls and irregular rocks are especially likely to free-slide while
trying to settle.

Object contacts have a related split. A hull contact with fewer than two points
is marked non-resting. It still receives friction coupled to this tick's solved
normal impulse, but it cannot warm start. A hull contact with two or more points
can warm start, yet its friction limit changes to the global
`object_friction_coeff * contactMassShare * gravity * interval` estimate rather
than the solved normal load. The contact-mass share depends on row counts. This
makes tangential response change with manifold classification and tessellation
rather than only material and load.

The per-collider `ColliderRecord::friction` value is populated but never read by
the contact solver. All object pairs use one global coefficient.

### 3. Irregular Hull Inertia Is An AABB Box Approximation

`tools/bake_hulls.py` integrates signed volume and center of mass, but computes
`unit_inertia` from the centered hull's axis-aligned half extents with the box
formula. `ConvexHullShape::ComputeBoxApproxInertia` exposes that approximation
directly, and non-uniform editor scaling recomputes the same box value.

A read-only tetrahedral volume-integral check covered all 37 current `.hull`
assets. Its integrated volumes matched the serialized volumes to a maximum
relative difference of `2.5e-9`, which checks the face winding and integration
setup. Comparing exact principal moments with the baked box approximation found:

- median maximum principal-moment error: about 40%;
- 14 of 37 hulls above 50%;
- 6 of 37 hulls above 100%;
- `pyramid.hull`: exact unit principal moments `8.75, 8.75, 10.0`, baked as
  `16.6667, 16.6667, 16.6667`; and
- `test_motion_tetrahedron.hull`: exact `0.25, 0.25, 0.4`, baked as
  `0.6667, 0.6667, 0.6667`.

Several asymmetric hulls also have non-zero products of inertia, which the
current three diagonal body-space values cannot represent. Contact impulses use
that stored inertia for angular effective mass and velocity change, so even a
perfect manifold produces visibly wrong rotation for these assets.

### 4. Spatial Manifold Geometry Is Credible, Temporal Identity Is Fragile

The hull manifold foundation is worth retaining: complete SAT axes, clipping,
deepest-first/max-spread reduction, deterministic ordering, and feature-keyed
warm starting. It is much better than a spherical discrete proxy.

The temporal contract needs work:

- SAT and clipping ties use absolute tolerances that are not scaled to hull size.
- Near an edge/face tie, the code may replace the minimum-overlap edge normal
  with a face normal if it can construct at least two points.
- Both legal reference faces are built, and the result with more points wins.
  Tiny motion can change the point count and flip the reference shape.
- An incident polygon starts with its face-local vertex ordinal, not its source
  vertex id. A clipped id retains only four bits from each predecessor and does
  not encode the clipping plane. Repeated clipping can collide or change ids.
- The solver intentionally treats exact feature identity as the only contact
  lifetime authority. A feature change loses normal and tangent warm starts and
  can make a loaded contact eligible for restitution again.

Current tests are strong on single-pose analytic geometry and include one tiny
translation identity case for the box-shaped brick hull. They do not prove
long-running rotation, reference switching, multiple irregular hulls, multiple
scales, loaded sliding, or restitution continuity through a harmless manifold
rebuild.

### 5. Hull Sleep Can Be Both Too Strict And Too Permissive

Terrain hull contacts are too strict because a failed support proof both disables
friction and sets `inhibitsSleep`. The resulting free slide helps prevent the
body from ever becoming quiet.

Object hull contacts have the opposite hole: `manifold.pointCount >= 2` is enough
to declare a resting footprint. Two points can be collinear and represent a
single edge, so an edge-balanced hull can become sleep-eligible. The current
working-tree sleep experiment computes face/narrow/second-contact evidence for
boxes only; hulls bypass that gate.

`ApplyPointSupportInstability` handles only a one-point hull support. When the
point lies almost directly below the center of mass, it injects a small angular
velocity along a feature/body-hash-selected tangent. It does not classify a
two-point line support and can make an otherwise exact symmetric pose choose a
visibly arbitrary direction.

The future policy should borrow body-local deactivation clocks and whole-island
transitions from established engines without reducing sleep to velocity alone.
The current persistent-island work's contact residual and pose-drift checks are
valuable guards. The missing piece is shape-neutral support geometry: response
always solves, while sleep separately asks whether the support patch and solver
state are actually stable.

### 6. Linear-Only Position Cleanup Amplifies Off-Centre Hull Errors

`CorrectPositions` selects one deepest row per manifold and translates bodies
along the contact normal by inverse mass. It intentionally applies no angular
position correction. An off-centre irregular hull can therefore keep the wrong
orientation while being translated out of penetration, leaving velocity rows to
fight the same torque-producing geometry on later ticks.

This is solver-wide, not a hull-only defect. Bullet-style split push/turn and
Box2D-style substep/relaxation experiments already belong to
`Agentic/Plans/WNF/contact-stack-stability-techniques.md`. This plan may measure
hull evidence and hand it to that owner; it must not add a hull-specific
penetration correction or silently duplicate the solver-wide plan.

## What To Borrow

### Bullet

- [`btPersistentManifold.cpp`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletCollision/NarrowPhaseCollision/btPersistentManifold.cpp)
  retains local anchors, refreshes them under new transforms, removes points by
  normal and tangent breaking distance, and keeps the deepest/widest bounded
  patch. Skullbonez should adapt the persistent geometric lifetime, not Bullet's
  storage layout.
- [`btContinuousConvexCollision.cpp`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletCollision/NarrowPhaseCollision/btContinuousConvexCollision.cpp)
  repeatedly queries convex distance and advances by the closing speed. A failed
  distance query, non-closing pair, invalid fraction, or iteration limit is a
  miss — a bounding-volume candidate is never promoted into a hit by default.
- [`btSimulationIslandManager.cpp`](https://github.com/bulletphysics/bullet3/blob/63c4d67e337017f9d8b298c900e9aabdb69296e7/src/BulletCollision/CollisionDispatch/btSimulationIslandManager.cpp)
  applies sleeping to a complete dynamic island only when every member is ready.
  This reinforces the current persistent-island direction.

Do not treat every Bullet shortcut as a quality target. Skullbonez already has
asset topology sufficient to bake exact polyhedral mass properties, and its
measured AABB-inertia error is too large to preserve.

### Box2D

- [`geometry.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/geometry.c)
  integrates polygon mass, centroid, and rotational inertia from the actual
  polygon instead of its bounding box. The 3D implementation should use the
  equivalent polyhedral volume integrals.
- [`contact.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/contact.c)
  matches old and new contact ids and carries normal/tangent impulses into the
  next solve independently of whether a body is allowed to sleep.
- [`contact_solver.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/contact_solver.c)
  clamps tangent impulse by mixed friction times the accumulated normal impulse.
- [`distance.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/distance.c)
  provides a bounded GJK shape cast over convex point proxies.
- [`solver.c`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/src/solver.c)
  advances body-local sleep time and lets any not-ready member keep its island
  awake. It also includes correction motion in the sleep-speed calculation.
- [`docs/simulation.md`](https://github.com/erincatto/box2d/blob/617d32ab02570930625bbcb8479f54be9bf8d045/docs/simulation.md)
  documents normal-load-proportional friction, per-shape friction mixing,
  persistent contact ids, shape casts, and island-based sleep.

Box2D is two-dimensional. Its exact constants, two-point manifold limit, block
solver, and scalar inertia do not transfer directly to a 3D four-point patch.
The transferable rules are actual-shape mass properties, persistent geometric
contact identity, normal-load-bounded friction, exact convex casting, and
island-wide sleep.

For 3D mass properties, use the original uniform-polyhedron method in
[Mirtich, “Fast and Accurate Computation of Polyhedral Mass Properties”](https://people.eecs.berkeley.edu/~jfc/mirtich/massProps.html)
or a separately proven tetrahedral integration with the same complete tensor
output.

## Goal

Make convex hulls collide, rotate, grip, topple, settle, sleep, and wake according
to their actual polyhedral geometry and material properties. Preserve the current
automatic Discrete-to-Swept promotion boundary while replacing the promoted
hull sweep's spherical authority with exact convex confirmation.

## Non-Goals

- Do not change gravity, fixed timestep, damping, restitution, friction, contact
  slop, global iteration count, sleep speeds, or sleep duration to make a scene
  look better.
- Do not replace the existing full discrete SAT/clipping path with vertex-only
  containment, a bounding sphere, an AABB, or a sampled surface proxy.
- Do not make sleep topology depend on support classification. Every active
  dynamic contact and joint remains an island edge.
- Do not disable collision response for a contact that fails sleep admission.
- Do not add hull-only friction, damping, position-correction, or angular-nudge
  constants.
- Do not broaden warm starts to arbitrary rows from the same pair. Match a
  persistent geometric contact or start it cold.
- Do not add unbounded maps, post-gameplay growth, per-contact heap allocation,
  or worker-order-dependent reductions.
- Do not implement split impulse, block solving, or Soft Step inside this plan.
  Send demonstrated solver-wide need to the contact-stack plan.
- Do not refresh a Physics, Replay, or visual golden merely to make changed
  behavior pass.

## Design Invariants

### Exact Contact Authority

- A broad bounding radius may only reject or bracket.
- A swept hit mutates simulation state only after a bounded exact convex query
  reports contact.
- The accepted TOI is rechecked by the full discrete manifold builder before
  either body advances.
- A failed exact query is a miss with unchanged `timeRemaining` and no hit, wake,
  or visual side effects.
- Linear promotion retains the existing direction-valid threshold and equality
  behavior. Angular eligibility remains broadphase expansion unless a separate
  owner decision funds exact rotational casting.

### Response Is Not Sleep Policy

- Every exact hull contact receives normal and tangent response according to its
  material, regardless of support dimension or sleep eligibility.
- Tangent capacity is derived from solved normal load. Manifold row count must
  not create or destroy total friction capacity.
- Rest-only gravity seeds, support classification, deactivation, and sleep
  inhibition remain metadata decisions after collision response exists.

### Persistent Geometric Contacts

- A retained contact owns local anchors on both shapes, normal, lifetime, and
  accumulated impulses in fixed-capacity storage.
- Rebuilt features match only within declared normal and tangent breaking
  distances scaled from contact slop and shape size.
- Patch reduction retains deepest penetration and maximizes supported area.
- Feature encoding remains deterministic and collision-free for the supported
  hull limits, but exact bit identity is not the sole proof of geometric
  continuity.

### Honest Mass Properties

- The baker emits volume, center of mass, and the complete symmetric unit inertia
  tensor about the center of mass.
- Runtime and editor scaling preserve the full tensor under uniform and
  non-uniform scale.
- Inertia remains positive definite for valid dynamic hulls and uses the same
  body/collider frame as collision vertices.

### Shape-Neutral Sleep Stability

- Island membership uses active contacts and joints, never a “stable support”
  subset.
- Body-local deactivation and whole-island transition remain authoritative.
- A support patch is classified by geometric dimension and center-of-mass
  projection: point, line, or area. Two collinear rows are not an area.
- Contact residual, correction motion, pose drift, and configured velocity
  thresholds can veto sleep without disabling response.
- An exact point or edge balance may remain awake. Production code must not pick
  a visible topple direction from body or feature hash.

## Physics Body Storage Review Decision

Exact asymmetric hull inertia cannot be consumed honestly through the current
three diagonal values. The consuming stages are force/torque integration, point
joints, persistent contacts, gameplay impulses, Replay restore, and Prediction
clone/restore. A contact-stage parallel store is insufficient because inertia is
durable body state used before and after contact solving and must survive replay
and prediction.

If CH3 is reactivated, the owner approves replacing the current approximate
three-component body-space inertia representation with one complete durable
representation: either six independent symmetric-tensor components or exact
principal moments plus a principal-axis rotation consistently composed with the
collider frame. This is a replacement of false mass data, not permission to add
an unrelated per-body field. The implementation must measure hot-store cost,
update every snapshot/clone path, and prefer the smaller representation that
passes the analytic tensor and frame tests.

## Phases

- [ ] **CH0 — Reproduce and quantify hull quality without behavior changes.**
  Add deterministic observation fixtures using a box-equivalent hull, pyramid,
  tetrahedron, wedge, elongated slab, and irregular rock at small, ordinary, and
  large scale. Cover level and sloped terrain, face rest, point and edge balance,
  shallow slide, glancing impact, hull/box and hull/hull contact, loaded reference
  switching, a promoted thin-target crossing, and a promoted near miss. Record
  coarse and exact TOI results, integration remainder, manifold normals/ids/local
  anchors, cache hits, accumulated normal/tangent impulses, slip distance,
  angular response, correction motion, energy, sleep reset reason, and transition
  tick. Add planted negative controls for a sphere-authorized near miss, disabled
  terrain friction, AABB inertia, collinear “area,” and stale-contact warm start.

- [ ] **CH1 — Make exact convex geometry authoritative for promoted hull pairs.**
  Keep the bounding-radius calculation only as an early rejection/coarse bracket.
  Change refinement to return an explicit hit/miss result; an unconfirmed bracket
  is a miss and cannot consume time. Replace the fixed-sample final authority with
  a bounded support-mapped convex cast or conservative advancement that queries
  exact convex distance. Re-run the existing full SAT manifold at the candidate
  TOI before mutation. Cover sphere/hull, box/hull, and hull/hull, including
  swapped order, opposing motion, initial overlap, grazing, loose-radius near
  miss, narrow contact window, sleeping target wake, and zero/near-zero remainder.
  Preserve current linear promotion bits, thresholds, equality hysteresis,
  broadphase overlay, and angular-expansion behavior byte-for-byte outside
  changed hit classification.

- [ ] **CH2 — Decouple friction and warm starting from sleep admission.** Give
  every exact hull contact finite tangent rows unless its mixed material friction
  is zero. Remove the hull/terrain `allowsTangentFriction` sleep gate. Combine
  collider and terrain/object material coefficients through one documented,
  symmetric rule with zero preservation. Clamp each 3D tangent vector by the
  contact's accumulated normal impulse, and prove the patch's summed friction
  budget is independent of retained row count. Base warm-start admission on
  persistent geometric lifetime, not `supportsRestingPolicy`; retain the existing
  restitution threshold and prove a loaded contact cannot rebound solely because
  its reference face changed. Validate analytic incline boundaries, material
  ordering, zero-friction controls, load scaling, row-reduction equivalence,
  impact-to-rest transition, and long-running slip/energy bounds.

- [ ] **CH3 — Bake and consume exact polyhedral inertia.** Implement complete
  center-of-mass inertia integration with analytic box, tetrahedron, pyramid, and
  asymmetric fixtures. Version the hull schema; prove current writer output,
  previous-version migration/re-bake behavior, and recoverable future-version
  rejection. Re-bake all tracked hulls only after the reader, writer, and runtime
  tensor representation pass. Update editor placement/scaling, scene creation,
  body storage, force/joint/contact impulse response, Replay, and Prediction.
  Prove symmetry, positive definiteness, principal moments, products of inertia,
  frame composition, mass scaling, uniform `s^5` inertia scaling at fixed density,
  non-uniform scale integration, and exact box equivalence. Record the body-store
  memory and step-time change.

- [ ] **CH4 — Retain geometric contact patches across harmless rebuilds.** Add a
  bounded local-anchor manifold cache and refresh it under current transforms.
  Match within declared breaking distances, reject stale normal/tangent drift,
  and retain deepest plus maximum-area points. Repair clipped feature provenance
  so source vertices/edges and clip boundaries cannot alias within supported hull
  limits. Make reference selection stable under scale-aware ties without hiding a
  real edge-axis change. Prove contact lifetime, cache hit rate, normal continuity,
  no duplicate keys, no repeated restitution, and byte-exact results through
  slow translation/rotation, face-reference crossover, scaling, row permutation,
  and swapped shape order.

- [ ] **CH5 — Replace hull row-count sleep guesses with support geometry.** Start
  only after the persistent simulation-island sleep owner has accepted its base.
  Publish fixed-capacity point/line/area patch facts and center-of-mass projection
  for object and terrain contacts. Keep every active constraint in island
  topology, and let patch facts affect only body eligibility. A broad stable face
  must sleep; a lone point or collinear edge must not sleep as an area; two
  independent contacts may form a stable aggregate; moving or corrected members
  keep the complete island awake. Remove or replace the feature-hash angular
  nudge so a symmetric pose does not choose an arbitrary visible direction.
  Prove stable rest, ideal balanced-awake behavior, natural topple under a real
  torque, wake propagation, contact removal, pose-drift reset, and object/terrain
  consistency.

- [ ] **CH6 — Decide whether solver-wide penetration work remains necessary.**
  Re-run CH0 after CH1-CH5. If hull breathing, creep, or off-centre penetration is
  still attributable to linear-only correction or velocity bias, attach the
  measured rows, energy, correction, and visual evidence to
  `contact-stack-stability-techniques.md`. Do not implement a shape-specific
  correction. The owner must explicitly reactivate and select a solver-wide
  experiment before split impulse, angular push/turn, block solving, substeps, or
  relaxation can enter production.

- [ ] **CH7 — Terminal closure and owner visual review.** Run focused unit and
  scene checks while iterating, then concentrate full Physics, deep regression,
  Replay fidelity, dependency, allocation, source-design, determinism, and
  performance gates here. Prove repeated clean-process and 0/1/4-worker exactness,
  no post-gameplay allocation, bounded convex-cast iterations, no unrelated-shape
  regression, and measured cost on representative hull-heavy and ordinary scenes.
  Capture side-by-side video or screenshots of the accepted hull matrix. Any
  golden transition requires content-level explanation, planted-control proof,
  and explicit owner approval.

## Acceptance Matrix

| Area | Required proof |
|---|---|
| Discrete geometry | Full SAT/GJK overlap handles separated, face, edge/edge, containment, swapped-order, and scale cases; no sphere/AABB authority |
| CCD | Promoted fast hull hits a thin target; a loose bounding-sphere near miss is a miss with unchanged integration remainder; exact TOI is deterministic and bounded |
| Friction | Terrain and object hulls always receive material-correct tangent response; incline and load tests follow Coulomb bounds; row count does not change patch capacity |
| Inertia | Exact tensor fixtures and all tracked assets pass; irregular-hull angular response follows the tensor; box-equivalent hull remains equivalent |
| Persistence | Harmless reference/clip changes retain valid anchors and impulses; stale geometry is rejected; restitution fires once per real impact |
| Sleep | Stable area support sleeps as a whole island; point/line balance never freezes as area support; residual/correction/pose motion vetoes sleep without disabling response |
| Determinism | Repeated clean processes and 0/1/4 workers produce exact state, event, and artifact hashes |
| Cost | No unbounded work or gameplay allocation; convex-cast, manifold-cache, tensor, and support-patch costs fit owner-approved budgets |

## Validation Map For A Reactivated Plan

| Phase | Focused iteration checks | Terminal checks |
|---|---|---|
| CH0 | New analytic tests and observation scenes only | None; no behavior transition |
| CH1 | Motion eligibility, object CCD, sleeping-target wake, pair-order, and exact-overlap tests | `tools\validate_physics.bat`; `tools\validate_physics_deep.bat` |
| CH2 | Friction cone, material mixing, terrain support, cache, restitution, incline, and energy tests | Physics and deep gates; contact-energy scene checker |
| CH3 | Hull baker/loader/version/tensor/scaling tests; Replay and Prediction snapshot round trips | Physics, Replay visual fidelity, allocation, dependency, and performance gates |
| CH4 | Manifold geometry/identity/cache tests and long-running deterministic contact sequences | Physics/deep gates and contact-energy scenes |
| CH5 | Sleep controller, persistent island, terrain/object support, wake, and authored hull scenes | Physics/deep gates and persistent-island regression checker |
| CH6 | Same CH0 matrix and contact-stack evidence export | None unless the separate solver plan is reactivated |
| CH7 | No new implementation | Full repository-mapped closure plus owner visual review |

Heavy validation remains terminal. During source iteration use `validate_fast` or
the smallest focused checks expected to finish within one to two minutes.

## Planned File Map

Likely owners include:

- `SkullbonezSource/Physics/ConvexHullShape.{h,cpp}`
- `SkullbonezSource/Physics/BoundingBox.cpp`
- `SkullbonezSource/Physics/ObjectContactManifold.{h,cpp}`
- `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.{h,cpp}`
- `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- `SkullbonezSource/Physics/PersistentContactSolver.{h,cpp}`
- `SkullbonezSource/Physics/PhysicsBodyStore.{h,cpp}`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController*`
- Runtime scene/editor hull creation and Replay/Prediction snapshot owners
- `tools/bake_hulls.py` and tracked `.hull` assets
- focused convex-hull, manifold, CCD, solver, sleep, replay, determinism, and
  scene tests

This list is an impact map, not blanket edit authority. Dependency direction must
remain unchanged, and no new upward Physics include is permitted.

## Reactivation Conditions

Move this file to `TODO/` only after the owner explicitly reactivates convex-hull
quality work, the persistent simulation-island sleep working tree has an accepted
or rejected disposition, and CH0's fixtures and cost budget are agreed. Register
the plan in `MASTER-PLAN.md` at that time. The first behavior slice is CH1: exact
convex authority after the bounding-sphere early out. CH2 follows before sleep
policy changes so later rest evidence is measured with real friction.

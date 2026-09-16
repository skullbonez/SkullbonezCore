# Convex Hull Collision Response And Sleep Quality

Date: 2026-09-16
Status: CH0-CH6 complete (7/8); CH7 owner visual/cost review remains.
Owner: Physics convex-contact pipeline
Impact area: convex-hull assets and mass properties, object CCD, object and
terrain manifolds, persistent contact response, sleep classification, tests,
diagnostics, deterministic scenes, replay state, and performance
Priority: Active ahead of PHYSICS_AB; preservation and reproduction first

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

The owner confirmed activation and implementation after PR #172, from pushed
main 060cd3acc on `nightrunner-16th-SEP-26`. Feature-branch commits and pushes
are authorized. The owner subsequently authorized a review PR; do not merge it. The accepted scoped persistent-island
implementation is present; its broader parked plan stays parked.

## Current activation contract

Before Physics changes, capture the unchanged `prediction_ragdoll_wall_200.scene.json`
with producer binaries and configuration. Require exact before/after body motion,
collision events, joint state and sleep/wake transitions throughout the recorded
envelope. Also protect prediction/replay behavior and measure performance. Preserve
scene settings and accepted baselines; a divergence is a defect to investigate.

Author every permanent hull test scene with varying terrain: flat, shallow and
steep slopes, uneven ground, isolated stacks, balanced and toppling bodies, near
misses and thin-target crossings. Use box-equivalent hull, wedge/pyramid, elongated
hull and irregular rock, with primitive controls and scale variants. Legitimate
sliding/toppling must remain possible; unstable sleep is not success.

Execution order: CH0, independent terrain-response portion of CH2, CH1, remaining
CH2, CH3, CH4, CH5, CH6, CH7. Terrain friction does not depend on a convex cast;
this prioritizes measured sliding without bypassing persistence/sleep dependencies.
CH2 stays unchecked until its complete acceptance passes. Historical findings below
were tested against the preserved producer. The terrain friction/sleep gate and
coarse-time fallback were reproduced and their first fixes now have focused proof.

Accounting limitation: default WORK_LEDGER belongs to another unfinished task.
An isolated LedgerPath using this exact thread id was attempted but rejected for
missing verified gpt-6-astra pricing. No counters/prices were invented. Continue
the owner-directed work while preserving the prior ledger.

## Current implementation

All production changes and all 36 permanent scenes are implemented. CH0-CH6
are complete. CH7 remains open; this is an implementation review PR, not a
claim that every closure criterion passed. The owner
explicitly authorized a review PR and forbids merging it. The later PR direction
supersedes the original saved goal's no-PR sentence. Phase checkboxes remain the
acceptance authority; historical findings below are followed by current evidence.

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

- [x] **CH0 — Reproduce and quantify hull quality without behavior changes.**
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

- [x] **CH1 — Make exact convex geometry authoritative for promoted hull pairs.**
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

- [x] **CH2 — Decouple friction and warm starting from sleep admission.** Give
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

- [x] **CH3 — Bake and consume exact polyhedral inertia.** Implement complete
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

- [x] **CH4 — Retain geometric contact patches across harmless rebuilds.** Add a
  bounded local-anchor manifold cache and refresh it under current transforms.
  Match within declared breaking distances, reject stale normal/tangent drift,
  and retain deepest plus maximum-area points. Repair clipped feature provenance
  so source vertices/edges and clip boundaries cannot alias within supported hull
  limits. Make reference selection stable under scale-aware ties without hiding a
  real edge-axis change. Prove contact lifetime, cache hit rate, normal continuity,
  no duplicate keys, no repeated restitution, and byte-exact results through
  slow translation/rotation, face-reference crossover, scaling, row permutation,
  and swapped shape order.

- [x] **CH5 — Replace hull row-count sleep guesses with support geometry.** Start
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

- [x] **CH6 — Decide whether solver-wide penetration work remains necessary.**
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

## Activation disposition

Explicit owner confirmation activates all eight phases. The prior persistent-island
working-tree concern is resolved by accepted main. CH0 must establish the full
matrix and preservation evidence before Physics changes. Final validation and
independent review remain mandatory; no phase is complete yet.


## Implementation and evidence — 2026-09-17

### Permanent scene matrix and preservation

`tools/generate_convex_quality_scenes.py --check` proves 36 scenes, 18 generated
hulls and three native heightmaps. Shapes are box-equivalent hull, wedge, pyramid,
tetrahedron, elongated slab and irregular rock, with primitive controls and
0.5/1/2 scales. Terrain covers flat, shallow X/Z, compound, steep, basin, ridges
and steps. Three-body stacks cover flat/shallow/ridges; five-high hull towers,
mixed box/slab/rock towers and primitive towers cover flat/shallow/steps.

All scenes ran with identity receipts, gameplay allocation enforcement and clean
shutdown: 2,400 ticks at 120 Hz, or 120 ticks for CCD. Before evidence is
`TestOutput/skarness/convex-quality-observed-before/`; current matrix is
`TestOutput/skarness/convex-quality-final/`. Native screenshots were inspected.
`tools/check_convex_quality.py <capture> --matrix` checks all identities and finite
states, broad rest/slide, retained three-body stacks and flat/steps five-high
box hulls. Its planted sleeping-collapse control fails as required.

The generator now rounds editable source coordinates to the writer's nine-digit
precision before baking. This avoids source-hash/tensor drift on an ordinary
rebake. All 55 baked assets remain unchanged by that final generator correction.
Twenty-eight scene position spellings changed below float precision; an exact
float-byte comparison proved every runtime value unchanged. Original capture
input text is preserved in `TestOutput/convex-generator-original-scenes/`.

The untouched `prediction_ragdoll_wall_200.scene.json` SHA-256 remains
`9f89151ddf1681bd3d6b54db842f97012e9c1b4293e42b86125098bfe212b0c2`.
Both final clean processes match all 2,400 solver hashes/event counts and the
39,073,610-byte recording SHA-256
`7a6cf34a095a944e350ba3c6f8d25ee6c13d65d1f8c2d7e8df1ee9ee0ce92025`.
Control is `convex-ragdoll-preservation-bounded`; final evidence is
`convex-ragdoll-preservation-final`, beneath `TestOutput/skarness/`.
The comparator rejects a planted tick-1200 solver mutation. Earlier intermediate
captures and original producer binaries/configuration are retained separately.
No accepted Physics, Replay, visual or scene baseline was changed.

### Exact CCD and physical friction

Hull promotion now uses at most 64 conservative-advancement steps, each with the
existing bounded GJK distance query. A separating support plane supplies the
lower-bound advance; exact SAT/manifold confirmation alone authorizes a hit.
An unconfirmed pair cannot consume either integration remainder or wake a body.
Primitive refinement and promotion thresholds are unchanged. The cast remains
fixed-orientation linear CCD; existing angular expansion is not a rotational cast.

Native near misses publish three misses with no response and unchanged travel.
Thin-target, swapped-order and 50,000-unit/second narrow-window crossings each
produce three identity-bound hits. Unit checks additionally cover sphere/hull,
opposing motion, overlap, grazing, exhausted intervals and a sleeping target:
true impact wakes once; a sleeping-pair miss publishes no event or mutation.
`convex-matrix-acceptance.log` contains the native planted controls.

Every actual hull contact permits tangent friction independently of sleep.
Speculative gaps remain frictionless. Hull friction is coupled to the accumulated
solved normal load, including warm starts. Materials mix symmetrically by geometric
mean with zero preservation; the object/terrain configuration ratio preserves
existing authored defaults. Primitive response remains on its accepted path.
The isolated shallow-X box-hull slip path falls from 1.0937 to 0.5310, wedge from
0.6356 to 0.5341 and pyramid from 0.6334 to 0.5018. The primitive control is exactly
0.5292 in both runs. The final rest/slide matrix settles on flat, shallow X/Z,
compound, basin and ridges; steep terrain still permits physical sliding.

Analytic actual-solver tests straddle tan(theta)=mu, include zero friction, masses
1/4 and one/two/four rows, and bound total tangent impulse by mu times solved
normal impulse without energy gain. These isolate translation at the center of
mass; separate geometric tests cover physical torque and support area.

### Exact mass properties and durability

Hull v3 integrates the full center-of-mass symmetric tensor, including products
of inertia. Analytic box, tetrahedron and pyramid tests cover exact mass/scale
response; nonuniform scaling transforms second moments, uniform fixed-density
inertia scales as s^5. All 55 tracked/new hulls are rebaked and `bake_hulls --check`
passes. Previous v2 data fails recoverably with an explicit rebake instruction;
current output and future-version rejection are tested. There is no approximate
inertia fallback for asymmetric hulls.

Review decision: body inertia is authoritative physical state consumed by force,
contact, joint and fixed-release stages, as well as editor history and prediction
clones. A stage-owned parallel copy would duplicate that shared state and lose
mutations/restore consistency. PhysicsBodyRecord therefore gains the three cold
products and the hot body store gains three inverse-product arrays. All creation,
update, compaction, copy, memory accounting and inverse-response paths include
them. Symmetric/primitive diagonal multiplication retains its original arithmetic.

Nested solver snapshot v9 stores geometric cache data and sparse full-inertia
rows; primitive-only snapshots retain v8 serialization. Outer replay v5 remains
unchanged. Codec, hash, delta, capacity, clone, editor undo and live restore all
carry the data. Restore preflights identities and positive-definite inverse
consistency before mutation, and uses binary lookup on sorted sparse rows.
Python replay queries decode nested versions 1-9, reject truncated tensor tails
and future v10, and successfully decode the native asymmetric hull recording.

Reserve inventory: PhysicsSolverSnapshot retains its registered Replay-only
RuntimeReserveAllocator owner, 8 MiB cap, grant/growth counters and growth limit
in PhysicsWorld.cpp. Byte accounting includes 88-byte geometric cache entries
and sparse 32-byte inertia rows. Only asymmetric rows request tensor backing.
A two-body durability test exposed the old 4,096-row chunk expanding a snapshot
to 2,131,712 bytes; concurrent verifier snapshots exhausted that existing cap.
The corrected 64-row chunks plus doubling retain demand-proportional backing
without a cap or phase expansion. The failed log is preserved as
`TestOutput/convex-tensor-artifact-primitive.log`; subsequent durability checks pass.

### Contact lifetime and support

Clipped features encode actual source vertex/edge/face provenance within declared
limits. Scale-aware SAT ties and normalized edge directions retain meaningful
edge minima. Local anchors/normals refresh under current transforms, reject stale
separation/normal/tangent drift, and carry lifetime and transported tangent impulse
across harmless feature changes. Loaded contact continuity suppresses repeated
restitution. Exact narrowphase remains mandatory before refreshing a bounded
four-point patch; fresh point/line contact cannot regain an old face area.
Hull patches retain all four rows and unloaded geometry. Primitive row reduction
and duplicate-key choice are preserved before hull cache insertion.

Hull sleep now uses the sleep owner's fixed-capacity geometric aggregate around
the actual supported body's center of mass. Solver resting flags cannot bypass
point/line/outside-area rejection, including reversed gravity and swapped pairs.
Bodies without colliders remain valid for body-only sleep policy. Stable area
support can sleep; removal, drift, correction, motion and island constraints
still veto deactivation. The feature-hash angular nudge is removed. Exact balance
receives zero invented torque; small opposite diamond tilts produce opposite
physical lever-arm responses. Actual controller tests cover aggregate support,
removal, object/terrain, both gravity signs and pair orders.

### Stack outcome and CH6 disposition

Three-body box-hull height retention is 1.0000 on flat, 0.9953 on shallow X and
0.9955 on ridges. Corresponding maximum horizontal drift is 0.0004, 1.1516 and
1.0922; all retain layer order and sleep. Five-high box hulls retain 0.999965 on
flat and steps with about 0.0015 drift. This does not apply to every tall tower:

| Tall stack | Flat height retention | Shallow X | Steps |
|---|---:|---:|---:|
| Five box hulls | 1.0000 | -0.1178 | 1.0000 |
| Five elongated hulls | 0.0022 | -0.1566 | -0.0329 |
| Mixed hull/slab/rock | -0.0483 | -0.1919 | -0.0437 |
| Primitive control | -0.0015 | -0.1064 | 0.9999 |

Collapsed sleeping piles fail the stack oracle. Exact inertia supersedes the
older approximate-inertia mixed-stack result; it must not be reverted to obtain
a cosmetically stable pile. Measured onset, rows, bias, energy, correction and
screenshots are handed to the still-parked contact-stack solver plan. Current
correlation warrants a controlled solver-wide experiment but does not prove which
correction/bias mechanism causes each collapse. No solver-wide stabilization,
iteration increase, shape-specific correction or baseline transition is included.

### Independent review and terminal status

The read-only rubber-duck review found reversed-gravity support admission and
quadratic tensor lookup. Both are repaired and the bounded follow-up is clear.
Full CPU testing then found missing-collider bounds and the exact reserve-owner
inventory needing eight new entries; fixes preserve existing body-only semantics
and verify each new owner's capacity instead of weakening the allocation proof.

Focused combined tests passed 59 cases / 9,968 assertions before the final fixes;
review support/restore tests passed 14 / 509; analytic incline plus allocation
inventory passed 2 / 9,956. Compiler-backed design passed 39 sources / 317 contexts
with zero findings, followed by a clean targeted sleep check. Project metadata,
dependency direction, formatting, deterministic math and plain language pass.
Deep Physics and immutable prediction/replay visual fidelity both pass without
baseline changes. The full CPU gate passes: 1,090 Profile tests and 3,747,034
assertions, plus Debug product coverage and all six CPU lanes. The staged Physics
gate passes with fingerprint 371ea623d9c4. The independent terminal harness review
is clear after confining CRT handling to deliberate fatal-probe children, rejecting
signed Windows coverage exits, and supplying explicit viewport hover/Lab inputs.

The 19-body mixed hull stack matches all 2,400 state/event ticks and exact replay
bytes with 0, 1 and 4 workers plus a repeated four-worker clean process:
`1e807a85e05b31cfdf21cd9918080b20a2839c98df7463099161ceda5eb5bfed`.
Evidence: `TestOutput/skarness/convex-workers-final/exactness.json`.


### Terminal runtime results and remaining acceptance

`tools/agent_validate.bat --plan-completion` was invoked once. Its failed CPU
phase was repaired and rerun successfully, then its remaining lanes were resumed
explicitly. The original full script did not return success; repaired and resumed
required lanes pass individually. The preserved wall-only comparison correctly
rejects the changed hull asset set. Native UI gates now capture a new immutable
240-tick comparison from the current executable and assets under TestOutput/ui.
Both tracks use the same build to test UI behavior, not cross-build divergence.
Original recordings, hashes and golden baselines remain untouched.

The viewport fixtures now move off the gizmo before pressing, explicitly load
and assert a valid comparison, and frame actual recorded target positions inside
both split images before testing native picking. All identity, axis, pan, zoom,
rendered-gizmo, playback and workspace-retention assertions remain active.
Interrupted captures remain evidence; retry attempts use new short directories.
A unit test verifies interruption, preservation, reuse and changed-input recapture.
Independent review found no remaining issues in these repairs.

Final native results:
- Editor views: 40 checks pass (`TestOutput/convex-editor-views-current.log`).
- Four views: 23 checks and allocation guard pass
  (`TestOutput/convex-four-views-current3.log`). Both recorded ball caps remain
  visible and pickable; the screenshot was inspected.
- Causal viewports: 45 checks pass, including sustained plane drags and detail
  exclusion at multiple window sizes (`TestOutput/convex-causal-viewports-current2.log`).
These supersede the historical viewport failures listed below.

Remaining Skarness continuation results (`TestOutput/convex-skarness-resume/summary.json`):

- `validate_four_views.py` native: exit 1 (136.75s).
- `validate_scene_reset.py` native: exit 0 (12.81s).
- `validate_skarness_state_stream.py` native: exit 0 (1.62s).
- `validate_skarness_queries.py` self-test: exit 0 (0.19s).
- `validate_skarness_queries.py` native: exit 0 (1.19s).
- `validate_skarness_future_render.py` self-test: exit 0 (0.27s).
- `validate_skarness_future_render.py` native: exit 0 (20.00s).
- `validate_skarness_causal_playback.py` native: exit 0 (57.19s).
- `validate_skarness_prediction_shortcut.py` native: exit 0 (5.95s).
- `validate_prediction_horizon.py` native: exit 0 (66.77s).
- `validate_prediction_memory.py` native: exit 0 (23.58s).
- `validate_space_prediction_horizon.py` native: exit 0 (68.14s).
- `validate_native_window.py` native: exit 0 (1.76s).
- `validate_skarness_prediction_matrix.py` self-test: exit 0 (0.16s).
- `validate_skarness_prediction_matrix.py` native: exit 0 (367.50s).

The Debug replay artifact checker also passes save/load, legacy/current/future
versions, checkpoint/target/branch restore, rollback, generated topology, and
query exports. DX12 renderer validation passes. Existing prediction horizon,
memory, Space 200 curve continuity and native window-control checks pass.

### Measured cost

Eight interleaved clean-process runs used four workers, fixed 120 Hz ticks,
prediction/recording off, the gameplay allocation guard, and no competing
native validation. Each advanced 720 ticks, discarding the first 60 recorded active-frame samples.
Profiler frames and Physics ticks differ: both ordinary producers contribute
552 retained samples per run and both hull producers 641. The ordinary
scene is the unchanged 212-body ragdoll control. The hull-heavy scene contains
192 hulls in 64 three-high box/rock/box stacks; settings and geometry match between
producers, with v2 AABB inertia assets for the preserved reader and v3 integrated
inertia for the candidate. Behavior changes, so these are workload costs rather
than an isolated instruction benchmark. Timing values below average the two
per-run statistics, in milliseconds.

| Scene | Producer | Physics median | Physics p95 | Physics excluding diagnostic dump median | Excluding dump p95 |
|---|---|---:|---:|---:|---:|
| ordinary | before | 4.1656 | 13.4746 | 0.1303 | 2.7430 |
| ordinary | after | 4.1723 | 13.3301 | 0.1306 | 2.5596 |
| hulls | before | 2.9785 | 3.0681 | 0.0454 | 0.0581 |
| hulls | after | 2.9518 | 3.1361 | 0.0477 | 0.0618 |

Diagnostic serialization is measured separately because it dominates the Skarness
trace workload; subtracting its timed child region is an estimate of the ordinary
step, not a claim about total game frame time. Raw CSVs, executable/scene hashes,
process exits and timing summaries remain under
`TestOutput/skarness/convex-performance-final/`.

Body storage adds 12 cold bytes plus 12 hot inverse-product bytes per body.
The support owner adds 100 bytes plus one flag per provisioned body. The geometric
contact cache is 88 bytes per provisioned entry instead of 24; the ragdoll scene's
91,160 entries occupy 8,022,080 bytes, an increase of 5,834,240 bytes. This is fixed
scene-provisioned Physics memory. Sparse replay tensor backing is zero for the
primitive control and demand-proportional for asymmetric hulls; the existing
8 MiB registered snapshot cap is unchanged. No gameplay allocation violations
occurred in the measured hull matrix or exactness captures.

Cost-lane results (`TestOutput/convex-performance-lane.json`):

- `paired`: exit 0 (68.14s); log `TestOutput/convex-final-paired.log`.
- `timings`: exit 1 (0.12s); log `TestOutput/convex-final-timings.log`.
- `perf`: exit 9 (26.12s); log `TestOutput/convex-final-perf.log`.
- `spikes`: exit 1 (13.53s); log `TestOutput/convex-final-spikes.log`.
- `timings-corrected`: exit 0 (0.33s); log `TestOutput/convex-final-timings-corrected.log`.
- `perf-retry`: exit 9 (35.17s); log `TestOutput/convex-final-perf-retry.log`.
- `perf-remaining`: exit 0 (63.00s); log `TestOutput/convex-perf-remaining.log`.

Native before/after screenshots of rest, shallow sliding, ridged stacks, and
five-high flat/shallow/stepped stacks were inspected side by side. The composite
is `TestOutput/convex-before-after-review.jpg`; full originals remain in
`convex-quality-observed-before` and `convex-quality-final` under TestOutput/skarness.
It shows the measured tall mixed/elongated collapse, not a universal stacking pass.
After the repairs below, the resumed renderer/physics performance budgets and
comparisons pass without baseline changes (`TestOutput/convex-perf-remaining.log`).
Its duration is the approximate log creation-to-final-write span.

The terminal allocation checker initially rejected 27 stale code contexts and
nine new sites. Exact context entries were refreshed without changing their
operations or ownership. Independent review verified the nine additions: three
fixed inverse-inertia arrays, three fixed contact scratch lists, two fixed support
lists, and the pre-reserved sparse Replay tensor append. All retain their
provisioned bounds and the existing Replay phase/cap; checker self-tests and the
689-source inventory pass with zero errors.

The causal performance tool also required a removed standalone PanelLayout marker.
Layout runs inside the already-required PanelInput and RenderCauseInspectorDrawer
scopes; the report now explicitly attributes layout to those enclosing operations.
The native causal inspection, wheel/detail identity, fixed storage and allocation
proof passes (`TestOutput/convex-causal-perf-final.log`).

The informational four-generation frame-spike diagnostic failed its scripted
full-horizon assertion (frame 61); no baseline or deadline was changed. The
separate native 120-second horizon/memory/selection and full 15-scene prediction
matrix pass. This informational diagnostic is not reported as a successful run.

CH7 remains unchecked pending owner visual/cost review of the measured outcomes.
MASTER explicitly retains this plan for that unmet aggregate acceptance.

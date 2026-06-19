# Convex Hull Collision Implementation Plan

Date: 2026-06-16
Status: Draft implementation plan
Last updated: 2026-06-17 - refreshed against current object manifold code and
Catto/Box2D reference material
Impact area: physics, collision, scene system, diagnostics, tests, performance
Validation for this document-only change: none required

## Goal

Add deterministic convex-hull collision support without replacing the existing
Catto-style persistent solver.

The intended end state:

1. `CollisionShape` can represent spheres, OBB boxes, and authored convex hulls.
2. Broadphase continues to use conservative bounding radii or future AABBs.
3. Narrowphase builds precise object/object contact manifolds for hull pairs.
4. The existing persistent contact solver consumes those manifold points as
   ordinary `PersistentContact` rows.
5. Feature IDs remain stable enough for warm starting and SkullScope analysis.
6. No per-frame heap allocation is added to the collision hot path.

## Current State

Useful pieces already exist:

| Area | Current state |
|------|---------------|
| Shape ownership | `CollisionShape` is `std::variant<BoundingSphere, BoundingBox>`. |
| Manifold path | `SkullbonezObjectContactManifold.*` already builds exact sphere/sphere, sphere/OBB, and OBB/OBB object contact manifolds. The OBB path uses SAT, face clipping, edge-edge fallback, and deterministic feature IDs. |
| Solver | `PersistentContactSolver` consumes manifold points as `PersistentContact` rows. `PhysicsWorld` owns the retained rows, cache entries, feature IDs, solver stats, and sleep policy state. |
| Broadphase | `SpatialGrid` uses conservative shape radius data to generate candidate pairs. |
| Diagnostics | SkullScope records body shape, contacts, feature IDs, solver rows, and pipeline stages. |
| Scene authoring | Scene files currently create balls, floating balls, boxes, and floating boxes. |

The main missing pieces are a convex hull shape type, deterministic hull asset
or scene authoring, a generic polytope view built from the existing OBB
SAT/clipping implementation, sphere/hull closest-feature contacts, and
hull-aware diagnostics.

## Non-Goals

- Do not rewrite the persistent solver.
- Do not replace the existing sphere and box paths in the first pass.
- Do not make concave mesh collision part of this feature.
- Do not add deformable hulls.
- Do not make physics terrain use arbitrary convex hulls in the first pass.
- Do not update physics baselines until the behavior is intentionally reviewed.
- Do not use random feature IDs, unordered containers, or per-frame allocation in
  narrowphase.

## Recommended Strategy

Use the current OBB manifold architecture as the stepping stone.

Recommended first algorithm:

- Use SAT for convex polytope overlap.
- Test face normals from both hulls.
- Test cross products of unique edge directions from both hulls.
- Use the minimum-overlap axis as the contact normal.
- Generate face contacts by clipping the incident face polygon against the
  reference face side planes.
- Generate edge-edge contacts with closest-points-on-segments for edge axes.

Reasoning:

- The existing box-box path already follows a SAT plus clipping shape.
- SAT exposes reference faces, incident faces, edges, and stable feature IDs.
- Stable contact features matter more to this solver than a pure yes/no hit.
- GJK/EPA can be added later for distance queries or conservative advancement,
  but it is less convenient for a first stable manifold implementation.

Current-code implication:

- Generalize the helpers already in `SkullbonezObjectContactManifold.cpp`
  (`BoxWorld`, `SatResult`, clipping vertices, SAT axis selection, face
  clipping, edge segment fallback) instead of adding a parallel hull collision
  pipeline.
- Keep the existing sphere/sphere, sphere/OBB, and OBB/OBB pair builders as the
  behavior reference while the generic polytope path is introduced.
- Before enabling hull/hull warm starting, audit the current contact cache key:
  `PersistentContactSolver` stores only `featureId & 0xffff` in the packed key.
  The suggested hull limits may require either tighter first-version feature
  ranges or a deliberate key-widening change.

## External Reference Notes

Relevant Catto and Box2D material:

- Erin Catto, [Contact Manifolds GDC 2007](https://box2d.org/files/ErinCatto_ContactManifolds_GDC2007.pdf):
  constraint solvers need contact points; SAT can compute a manifold in one
  shot; GJK can build a manifold point-by-point; contact rows need position,
  normal, penetration, and contact ID; coherence improves when feature choices
  and old impulses can be matched frame to frame.
- The same Contact Manifolds deck recommends feature weighting to avoid
  flip-flopping between equivalent separating axes, stores clipped feature
  numbers as contact IDs, and treats deep GJK contacts as a case where SAT, EPA,
  or brute force may be needed.
- Erin Catto, [Computing Distance GDC 2010](https://box2d.org/files/ErinCatto_GJK_GDC2010.pdf):
  useful later for closest-point distance and conservative advancement. It is a
  2D-focused GJK reference; the concepts extend to 3D, but it explicitly does
  not cover convex hull construction.
- Erin Catto, [Continuous Collision GDC 2013](https://box2d.org/files/ErinCatto_ContinuousCollision_GDC2013.pdf):
  useful later for moving hulls and TOI. It identifies closest features with
  GJK, V-Clip, or SAT, then reduces polygon/polygon advancement to feature-plane
  tests.
- Box2D [Collision](https://box2d.org/documentation/md_collision.html) and
  [Geometry](https://box2d.org/documentation/group__geometry.html) docs:
  `b2ComputeHull()` and `b2ValidateHull()` are good 2D examples for load-time
  hull validation: reject degenerate point sets, weld close points, remove
  collinear points, and check hull creation before constructing a collision
  shape.
- Erin Catto, [Diablo 3 Ragdolls GDC 2012](https://box2d.org/files/ErinCatto_Ragdolls_GDC2012.pdf):
  useful authoring guidance. Simplified convex hulls with broad faces improve
  simulation quality and reduce teetering on shallow edges.

## Shape Representation

Add a new collision shape, likely named `ConvexHullShape` or
`BoundingConvexHull`.

Required data, built once at load time:

```cpp
struct ConvexHullFace
{
    Math::Vector::Vector3 normalLocal;
    float planeOffsetLocal = 0.0f;
    uint16_t firstIndex = 0;
    uint8_t indexCount = 0;
};

struct ConvexHullEdge
{
    uint16_t vertexA = 0;
    uint16_t vertexB = 0;
    uint16_t faceA = 0;
    uint16_t faceB = 0;
};

class ConvexHullShape
{
    // Local-space convex polytope data. Storage can be vector-backed at load
    // time, but narrowphase must not allocate per frame.
};
```

Store or derive:

- local vertices;
- face index ranges;
- unique undirected edges;
- face normals and plane offsets;
- adjacency from faces to edges;
- centroid or local origin offset;
- bounding radius;
- approximate volume;
- approximate inertia tensor or a conservative fallback inertia.
- deterministic face, edge, and vertex ordering that is frozen after load-time
  validation.

Set practical first-version caps:

| Limit | Suggested start |
|-------|-----------------|
| Vertices | 64 |
| Faces | 96 |
| Edges | 160 |
| Vertices per face | 16 |

If any limit is exceeded, reject the hull at load time with a clear error.
Keeping limits explicit protects feature IDs and hot-path memory.

Treat the built hull as immutable. If an editor or asset bake step changes
vertices or faces, rebuild and revalidate all derived normals, planes, edges,
adjacency, mass properties, and feature IDs.

## CollisionShape Integration

Update the shape variant deliberately:

```cpp
using CollisionShape = std::variant<BoundingSphere, BoundingBox, ConvexHullShape>;
```

Then handle all visitor sites:

- `GetShapePosition`
- `GetShapeVolume`
- `GetShapeDragCoefficient`
- `GetShapeProjectedSurfaceArea`
- `GetShapeSubmergedVolumePercent`
- `GetShapeBoundingRadius`
- `GetShapeTerrainBottomOffset`
- `GetShapeModelMatrix`
- `TestShapeCollision`
- diagnostics and scene snapshot paths that print shape information
- collision and physics debug visualizers

For the first pass, `GetShapeTerrainBottomOffset` can use the bounding radius
for hulls, matching the conservative behavior used by other shapes. Terrain
contacts for hulls should remain a later phase.

## Narrowphase Architecture

Keep `SkullbonezObjectContactManifold.*` as the object/object contact builder.

Recommended helper layer:

```cpp
struct ConvexPolytopeView
{
    const Math::Vector::Vector3* verticesLocal;
    const ConvexHullFace* faces;
    const ConvexHullEdge* edges;
    uint16_t vertexCount;
    uint16_t faceCount;
    uint16_t edgeCount;
};
```

Boxes can be adapted into a temporary fixed-size polytope view without changing
their public shape type. Hulls provide the view from stored data. This lets
box-hull and hull-hull reuse the same SAT and clipping code while preserving
the current optimized sphere/box paths.

The existing OBB path already proves the solver-facing contract:
`BuildObjectContactManifold()` returns an `ObjectContactManifold` with a normal
from body A to body B and up to four fixed contact points. Hull work should keep
that public API and broaden only the shape-pair internals.

Required pair builders:

| Pair | First implementation |
|------|----------------------|
| Sphere / hull | Closest feature on hull, one contact point |
| Hull / sphere | Same as sphere/hull with normal flipped |
| Box / hull | Generic convex polytope SAT plus clipping |
| Hull / box | Generic convex polytope SAT plus clipping |
| Hull / hull | Generic convex polytope SAT plus clipping |

Keep sphere/sphere, sphere/box, and box/box on their current paths until the
generic convex path is proven.

## SAT Details

For each candidate pair:

1. Build world-space polytope views for both bodies.
2. Test all face normals from A.
3. Test all face normals from B.
4. Test all non-degenerate cross products of unique edge directions.
5. If any axis separates by more than `contactSkin`, return no manifold.
6. Track the axis with the smallest overlap.
7. Orient the final normal from body A toward body B.

Important determinism rules:

- Iterate faces and edges in stable stored order.
- Use deterministic tie-breaking when overlaps are nearly equal.
- Ignore edge cross axes below a fixed epsilon.
- Prefer face axes over edge axes when penetration is tied within epsilon.
- Apply a deterministic face-axis bias when two face normals are nearly tied so
  the chosen reference face does not flip frame to frame.
- Avoid unordered maps or address-order dependent sorting.

## Manifold Generation

### Face Contact

When the best axis is a face normal:

1. Choose the reference hull and face.
2. Choose the incident face on the other hull whose normal is most opposite the
   reference normal.
3. Transform the incident polygon to world space.
4. Clip the incident polygon against the side planes of the reference face.
5. Keep points behind or within the reference contact plane plus contact skin.
6. Limit to four contact points using deterministic deepest-point selection.
7. Encode feature IDs from reference face, incident face, and clipped vertex id.

### Edge Contact

When the best axis is an edge cross axis:

1. Find the participating edge from A and edge from B.
2. Compute closest points between the two world-space line segments.
3. Use the midpoint as the contact point.
4. Use SAT overlap as penetration.
5. Encode feature ID from both edge indices.

### Sphere / Hull Contact

Start with a robust but simple path:

1. Transform the sphere center into hull local space.
2. Test signed distance to each hull face.
3. If outside one or more faces, find the closest point on the clipped hull
   boundary or use a GJK distance helper once available.
4. If inside all faces, use the nearest face as the contact normal.
5. Add one row with a face/vertex/edge feature id.

A first version may restrict sphere/hull contact to face contacts if authored
hull scenes are chosen accordingly, but the plan should not bless false corner
contacts as final behavior.

GJK is a good later helper for outside-hull closest-feature cases, especially
near edges and vertices. It should not be the only first-pass answer for deep
overlap because Catto's own contact-manifold material recommends another method
such as SAT, EPA, or brute force when GJK produces only an awkward deep-contact
point.

## Feature IDs And Cache Keys

Current persistent contact cache keys include a feature field. Convex hulls need
stable local feature identity.

Recommended first encoding:

| Contact kind | Feature identity |
|--------------|------------------|
| Sphere/hull face | hull body side + face index |
| Sphere/hull edge | hull body side + edge index |
| Sphere/hull vertex | hull body side + vertex index |
| Hull face contact | reference side + reference face + incident face + point id |
| Hull edge contact | edge index A + edge index B |

The current solver key keeps only the low 16 bits of `featureId`. That is
probably too small for full `96 face / 160 edge` hull pair identity with kind,
side, and point bits. Do not silently truncate. Either lower the first-version
hull limits for warm-started contacts, or widen the cache key packing in a
focused solver change before hull/hull warm starting is accepted.

## Scene And Asset Authoring

Add hull authoring after the shape type and narrowphase are ready enough to
load a simple scene.

Possible scene directive:

```text
convex_hull name px py pz qw qx qy qz vx vy vz mass hull=SkullbonezData/hulls/wedge.hull
floating_convex_hull name px py pz qw qx qy qz hull=SkullbonezData/hulls/ramp.hull
```

Possible hull asset format:

```text
hull_version 1
vertex -1 0 -1
vertex  1 0 -1
vertex  1 0  1
face 0 1 2
```

The first asset format can be plain text and repository-local. It should be
validated at load time:

- at least 4 non-coplanar vertices;
- every face has at least 3 vertices;
- face winding is consistent;
- all vertices lie on or behind every face plane within tolerance;
- no duplicate or zero-length edges;
- close duplicate vertices are welded or rejected deterministically;
- collinear face vertices are removed or rejected deterministically;
- every undirected edge has exactly two adjacent faces;
- bounding radius is finite and positive;
- authored hulls prefer broad, stable faces over skinny facets when either shape
  is intended to rest or stack.

## Diagnostics And Debug Rendering

Add enough visibility before tuning:

- Collision visualizer draws hull wireframes.
- Physics debug overlay draws hull contact points and normals.
- SkullScope records `shape:"convex_hull"`, hull name, hull counts, and contact
  feature IDs.
- Launcher repro snapshots include hull name, vertex/face counts, and bounding
  radius.
- Optional pipeline stage counters split sphere/box/hull manifold counts.

Do not ingest raw hull or SkullScope dumps into model context during debugging.
Use bounded `tools\physics_query.bat` queries when diagnostics are needed.

## Implementation Phases

### Phase 0 - Design Freeze And Probes

- Confirm shape name, limits, hull asset location, and scene directive syntax.
- Decide whether hull feature IDs fit the current 16-bit cache field or require
  a key-widening prerequisite.
- Add or identify tiny deterministic probe hulls: tetrahedron, wedge, bevelled
  box, triangular prism.
- Prefer probe hulls with at least one broad rest face before testing skinny
  edge cases.
- Draft expected behavior for one hull/sphere and one hull/box scene before
  touching baselines.

### Phase 1 - Shape Type And Loading

- Add `ConvexHullShape`.
- Add hull asset parser and validation.
- Add conservative volume, drag, projected area, submerged fraction fallback,
  model matrix, and bounding radius support.
- Add scene directives for dynamic and floating hulls.
- Add diagnostics strings and launcher snapshot output.
- Freeze validated hull topology and derived feature ordering at load time.

Development checks:

- Use scene-load-only runs for parser failures and success cases.
- Do not run full validation scripts during iteration unless preparing a commit
  or PR gate.

### Phase 2 - Debug Visualization

- Draw convex hull wireframes in the collision visualizer.
- Draw local axes or face normals for selected hulls only if useful.
- Confirm hull world transform matches physics pose in simple scenes.

### Phase 3 - Sphere / Hull Manifolds

- Implement sphere/hull and hull/sphere contacts.
- Add focused scenes: sphere resting on hull face, sphere grazing hull edge,
  sphere landing on a wedge.
- Preserve sphere/sphere and sphere/box behavior.

### Phase 4 - Box / Hull And Hull / Hull Manifolds

- Add generic convex polytope SAT helper.
- Adapt boxes into convex polytope views.
- Port the existing OBB face clipping and edge-edge helper behavior into the
  generic path while preserving current OBB/OBB results.
- Add face clipping and edge-edge contact generation.
- Add scenes for box on wedge, box against triangular prism, hull stack, and
  hull-hull edge contact.

### Phase 5 - Solver Integration Review

- Verify manifold rows map cleanly into `PersistentContact`.
- Confirm cache hits are stable frame to frame.
- Confirm sleep support does not falsely sleep under-constrained hull contacts.
- Add SkullScope questions or query presets if the output becomes hard to
  inspect manually.

### Phase 6 - Validation And Baseline Decision

- Review visual and SkullScope behavior before accepting new physics baselines.
- If behavior is accepted, refresh only the specific physics baselines required
  by the new hull scenes from the final Debug executable and committed scene
  state.
- Run the required PR gate after baseline updates.

## Future Validation Gates

When this plan becomes code, expected PR-bound validation is:

```bat
tools\validate_physics.bat
```

Also run:

```bat
tools\validate_perf.bat
```

if hull broadphase, candidate-pair generation, or narrowphase hot paths affect
performance-sensitive scenes.

Run:

```bat
tools\validate_dx12_renderer.bat
```

only if hull debug rendering, screenshots, visual baselines, shaders, or DX12
renderer code change.

If the final scope crosses runtime, parser, renderer, diagnostics, and physics
at once, use:

```bat
tools\validate_full.bat
```

## Risks

| Risk | Mitigation |
|------|------------|
| Feature ID churn hurts warm starting | Stable face/edge ordering, deterministic tie-breaking, SkullScope cache-hit checks |
| Feature IDs exceed current 16-bit cache field | Decide limits or widen the key before accepting hull/hull warm starting |
| Edge-edge axes create jitter | Epsilon-filter degenerate axes and prefer face axes on near ties |
| Hull scenes allocate in hot paths | Build all hull data at load time; use fixed scratch buffers during narrowphase |
| Bad hull assets crash physics | Validate assets strictly and reject non-convex or malformed hulls |
| Baselines hide wrong behavior | Use probe scenes first; update baselines only after review |
| Terrain support for hulls is ambiguous | Keep hull/terrain out of the first pass or handle as conservative bounding support only |

## Open Questions

- Should boxes eventually become a specialized convex hull internally, or remain
  a separate fast path forever?
- What hull asset format should be accepted first: a minimal `.hull` text file,
  scene-inline vertices, or imported mesh data baked by a tool?
- Should the first hull inertia use exact convex mass properties or a safe
  bounding-box approximation?
- How large can hull feature indices be before the contact cache key must be
  widened?
- Should hull/terrain contact be face clipping against local terrain patches, or
  postponed until object/object hulls are stable?

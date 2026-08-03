# Narrowphase Manifold And Sleep Coverage - NM1 Geometry

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Plan progress: 2/6
Portfolio progress: 2/26 (8%)

## Outcome

Every ordered object-manifold shape family now has a geometric oracle for
normal direction and magnitude, penetration, row count, and world-space point
placement. The box/box and hull/hull matrices include face/face, face/edge,
vertex/face, edge/edge, and deep-overlap configurations. Sphere/box and
sphere/hull include inside, exact-surface, skin-only near, outside-overlap, and
reversed-owner cases. Mixed box/hull is pinned in both orders.

The edge/edge hull oracle found and repaired one production defect. Parallel
authored edges could tie on the same SAT line while the lower source edge id
named the reverse, back-side support pair. The manifold normal and penetration
were plausible, but its one contact point lay between the wrong two edges.
Edge-axis eligibility now orients the axis from polytope A toward polytope B and
admits only the two support edges that face one another along that final normal.

No baseline, golden, configuration, hull asset, capacity, or solver policy
changed.

## Derivation Method

The fixtures derive their expectations from authored dimensions and poses:

- unit spheres and boxes use their explicit radii and half-extents;
- hull cases load `SkullbonezData/hulls/building_brick_unit.hull` and name its
  baked half-extents `(1.45, 0.72, 0.34)` explicitly;
- rotated fixtures compute world axes from the authored quaternion;
- vertex placements use signed support vertices;
- face/edge placements align an authored support edge to a known face plane;
- edge/edge placements derive the cross-axis normal, projection radii, exact
  overlap, supporting edge segments, and their closest-point midpoint.

No expected manifold row was copied from current output. The initial hull
edge/edge failure was kept as a defect witness: the derived support midpoint
was approximately `(0.552038, 0.542085, 0.548595)`, while the pre-fix result was
approximately `(-0.552038, 0.515577, 0.509067)`. Inspection of the authored edge
order and current SAT tie policy showed that the latter point used the reverse
support pair. The production repair made the original derived expectation pass.

## Geometry Matrix

| Family | Configuration-derived oracle |
|---|---|
| sphere / sphere | Two unit spheres separated by 1.5 along +X produce unit +X normal, 0.5 penetration, and midpoint `(0.75, 0, 0)`. |
| sphere / box | A radius-0.5 sphere covers outside overlap, exact +X surface, 0.001 skin-only separation, and center-inside escape. Box/sphere reverses the normal while preserving depth and point. |
| sphere / hull | A radius-0.25 sphere uses the brick's authored +X face for outside overlap, exact surface, skin-only near contact, and center-inside escape. Separate diagonal placements select the authored +X/+Y edge and +X/+Y/+Z vertex with exact 0.05 overlap. Hull/sphere reverses the face normal. |
| box / box face and deep | Unit boxes at X separation 1.8 produce a four-corner patch at X 0.9 with depth 0.2. Coincident boxes use deterministic +X tie order, depth 2.0, and a centered four-corner patch. |
| box / box face-edge | A 45-degree Y rotation places one vertical support edge 0.10 through the +X face, producing two points at X 0.95. |
| box / box vertex-face | An axis `(1,1,1)` rotation places one support vertex 0.05 through the +X face, producing one point at X 0.975. |
| box / box edge-edge | Two non-parallel long support edges are placed with exact 0.05 cross-axis overlap; the derived cross normal and segment midpoint produce one row. |
| box / hull and hull / box | Equal brick extents at X separation 2.80 produce four points at X 1.40, depth 0.10, and ordered normals +X then -X. Rotated mixed fixtures additionally pin face/edge, vertex/face, and edge/edge placement. |
| hull / hull face and deep | Brick hulls at X separation 2.80 produce the same four-corner face patch. Coincident hulls select the authored first minimum-width Z face, normal -Z, and depth 0.68. |
| hull / hull face-edge | A 45-degree Y rotation presents a vertical support edge. The hull reference-selection policy retains its derived four-row clipped alternative at X 1.425. |
| hull / hull vertex-face | The rotated support vertex produces one point at X 1.425 and depth 0.05. |
| hull / hull edge-edge | The same analytic cross-axis fixture exposed the reverse-support defect and now returns the true opposing-edge midpoint. |

## Defect And Repair Boundary

`PolytopeSat` enumerates authored edge pairs. The old
`IsUsefulPolyEdgeAxis` admitted either normal-cone orientation for a pair, then
`AcceptPolyAxis` resolved equal overlaps by lower `axisA`. For parallel edge
families that meant the winning stored edge ids could describe A's back-side
edge and B's far-side edge even though `SatResult::normal` was later oriented
from A to B. `BuildPolyEdgeContact` trusted those stored ids and built its one
row between the wrong segments.

The repair is local to useful polytope edge-axis selection. It normalizes the
cross axis, flips it toward `b.center - a.center`, and requires A's edge normal
cone to support that axis while B's supports its opposite. The SAT line remains
the same; only the physical edge pair retained for contact construction is
made consistent with the final normal. CodeGraph impact identified only
`IsUsefulPolyEdgeAxis`, `PolytopeSat`, and `BuildPolyPoly` in the affected call
chain.

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/ObjectContactManifold.cpp` | Pass | Existing learning header states narrowphase ownership and solver flow; the repaired edge filter has a nearby `Invariant:` comment explaining the reverse-support hazard and permanent NM1 evidence is linked. |
| `SkullbonezTests/TestObjectContactManifold.cpp` | Pass | Learning header now owns all object-family geometry; analytic helpers document authored extent and non-parallel segment invariants; topology-sensitive fixtures explain reference selection and point derivation. |

Checked: 2/2. Deferred: 0.

## Validation

| Command | Result |
|---|---|
| Focused Profile build and `Profile\\SKULLBONEZ_TESTS.exe --test-case="Object contact manifold geometry:*"` | Pass: 8/8 cases, 251/251 assertions. |
| Complete Profile doctest executable during iteration | Pass, exit 0. |
| `tools\\validate_tests.bat` | Final-source pass in 49.8 seconds; 129/129 project/filter items and the complete Profile test harness passed. |
| `tools\\validate_coverage.bat` | Final-source pass in 68.0 seconds; Physics stages/solver is 4,965/5,760 lines (86.20%) against the 70% floor and every subsystem floor passes. |
| `tools\\validate_physics.bat` | Final-source pass in 24.5 seconds; Debug build, two deterministic engine processes, and the owner-approved Physics baseline comparison passed without refresh. |
| `tools\\validate_format.bat` | Pass in 45.1 seconds; 587 source files, 327 headers, and every repository-relative `Related:` path are clean. |
| `git diff --check` | Pass. |

NM1 is an ordinary incremental slice, so no rubber-duck review was appropriate.
The mandatory independent plan-level review remains owned by NM5.

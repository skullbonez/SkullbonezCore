# Maths Surface Reachability MR0 Census

Date: 2026-07-30

Tree: `nightrunner-30th-JUL-26` after Build Configuration Parity

## Method

MR0 used three independent evidence forms:

1. CodeGraph definition/caller paths identified the source-level dependency
   graph and the only production consumers.
2. Fresh Debug and Profile builds produced MSVC COFF objects. Decorated symbols
   preserve overload parameter types after each configuration's preprocessor
   has run. Searching every object separates production, same-TU, and test
   references without confusing same-arity overloads.
3. `git log -S "<symbol>"` and the relevant historical diffs determined whether
   a removed edge represented lost wiring or an intentionally retired
   algorithm.

The complete Debug and Profile solutions built successfully after deletion.
The focused Profile suite passes 4/4 GeometricMath cases and 17/17 assertions.

## Exact GeometricMath Census

| Definition | Debug/Profile evidence | Historical intent | MR1 decision |
|---|---|---|---|
| `ComputePlane(Triangle)` | Production references from `World/Terrain.cpp`; test references also present | Terrain collision-cache plane construction | Retain public |
| `CalculateIntersectionTime(Plane, Ray)` | Production references from `Physics/TerrainContactManifold.cpp`; test references also present | Live swept terrain collision query | Retain public |
| `ComputeTriangleNormal(Triangle)` | Same defining object only, downstream of live `ComputePlane` | Implementation helper of plane construction | Retain with anonymous-namespace linkage |
| `GetHeightFromPlane(Triangle, float, float)` | One test object reference; no production object reference | Terrain callers were deliberately replaced by cached/direct terrain interpolation in `7893de3f` and `17c438da` | Delete |
| `DeterminePointDistFromPlane(Plane, Vector3)` | Same defining object only; reachable only through dead height/classification code | No surviving owner after height retirement | Delete |
| `ClassifyPointAgainstPlane(Plane, Vector3)` | Definition object only; no caller | No live or historical intended entry found | Delete |
| `IsPointInsideTriangle(Triangle, Vector3)` | Definition object only; no caller | Legacy private surface present since the original import | Delete |
| `ComputeBarycentricCoordinates(Triangle, Vector3)` | Same defining object only, downstream only of dead `IsPointInsideTriangle` | No surviving owner | Delete |
| `CalculateIntersectionTime(Triangle, Ray)` | One test object reference; no production object reference | Convenience overload with no runtime caller | Delete |
| `ComputeIntersectionPoint(Plane, Ray)` | One test object reference; no production object reference | Convenience wrapper over the dead point-evaluation overload | Delete |
| `ComputeIntersectionPoint(Ray, float)` | Same defining object only; its legacy collision-response caller was removed in `13dd10a0` | Retired legacy response path, not lost wiring | Delete |

No heightfield, picking, terrain, editor, or runtime path was found that should
still call a deleted definition. Terrain height queries now own their direct
heightfield/cached-slope algorithms; reconnecting the legacy law-of-sines path
would regress that design.

## Post-Deletion Result

`GeometricMath.h` exposes exactly two functions and both have production
callers. The singular `acosf`/`sinf` height path and the private barycentric
component are absent. The triangle normal is an anonymous implementation helper.
The post-deletion compiler-backed repository scan reports zero GeometricMath
rows.

The first repository-wide scan found 408 non-GeometricMath rows. Follow-up
review found that declaration defaults were not joined into permitted arity;
the corrected scan also incorporates standalone `Agentic/Tests` lexical edges.
It reports 407 rows (299 no-reference, 60 test-only, 41 own-TU-only, and 7
own-TU-and-test-only) and supersedes this initial population for MR3 routing.
MR3 routed each one to the completed follow-up recorded in
`Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md`; none
received a blanket retain ruling.

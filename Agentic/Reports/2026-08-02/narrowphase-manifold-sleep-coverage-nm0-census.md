# Narrowphase Manifold And Sleep Coverage - NM0 Census

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Scope: current tracked source and tests; no production or test behavior changed

## Method

The repository CodeGraph index was current before inspection. The census used
CodeGraph for the first-pass call map, then confirmed every contract below
against the current source and test files. Counts are current-source
measurements, not budgets.

## Object-Manifold Contract Matrix

`ObjectContactManifold::normal` is always oriented from body A toward body B.
`AddContactPoint` clamps negative penetration to zero, retains world-space
points and body-center arms, and caps the published manifold at four rows.

| Ordered shape pair | Entry condition | Normal and penetration | Maximum rows | Feature identity | Reduction |
|---|---|---|---:|---|---|
| sphere / sphere | Center distance is at most the radius sum plus contact skin. | Normal is normalized A-center to B-center, with world +Y for coincident centers. Penetration is radius sum minus center distance. | 1 | Literal `0`. | None. |
| sphere / box | The closest OBB point is within sphere radius plus skin, or the sphere center is inside the OBB. | Outside: sphere-to-box normal is opposite box outward; inside: the nearest deterministic face supplies the normal. Penetration is radius minus distance outside, or radius plus distance to the selected face inside. | 1 | `EncodeSphereBoxFeature`: kind, box-owner bit, and six-face id. | None. |
| box / sphere | Same geometry as sphere / box with argument ownership reversed. | Final normal is flipped so it still points from body A (box) to body B (sphere). Penetration is unchanged. | 1 | Same sphere/box encoding with the box-owner bit reversed. | None. |
| box / box | All 15 OBB SAT axes overlap within contact skin. Face axes win overlap ties ahead of edge axes. | Minimum-overlap SAT normal points A to B. Face penetration is negative clipped separation; edge penetration is SAT overlap. | 4 for a face patch; 1 for edge/edge | Face rows encode reference owner, reference face, incident face, and clipped point id. Edge rows encode both 12-way OBB edge ids. | A clipped polygon has at most 8 candidates. Deepest is first; later rows maximize minimum tangent-plane spread. |
| sphere / hull | No hull face excludes the center beyond radius plus skin; an outside center must also be within radius plus skin of its closest face, edge, or vertex. | The closest hull boundary feature supplies hull outward. Final normal is flipped by ordered ownership. Penetration is radius minus face signed distance for an inside center, or radius minus boundary distance outside. | 1 | `EncodeSphereHullFeature`: kind, hull-owner bit, feature kind (face/edge/vertex), and source id. | Closest feature wins; equal distance uses feature-kind then source-id order. |
| hull / sphere | Same geometry as sphere / hull with argument ownership reversed. | Final normal is flipped so it points from hull A toward sphere B. | 1 | Same sphere/hull encoding with the hull-owner bit reversed. | Same closest-feature rule. |
| box / hull | Box is converted to a six-face/twelve-edge polytope; hull uses authored face/edge topology. All face axes and useful edge cross axes must overlap within skin. | Polytope SAT normal points A to B. Face penetration is negative clipped separation; edge penetration is SAT overlap. A nearby multi-row face patch supersedes a one-row edge result when it is within the explicit tolerance. | 4 for face; 1 for edge | Poly-face rows encode reference owner, both source faces, and clipped point id. Poly-edge rows encode both source edge ids. | Face clipping permits 32 candidates and uses the shared deepest/spread reducer. |
| hull / box | Same polytope path with argument ownership reversed. | Final normal and reference selection remain ordered A to B. | 4 for face; 1 for edge | Same poly-face/poly-edge encoding with reference ownership reversed. | Same shared reducer. |
| hull / hull | Both authored hulls enter polytope SAT. All face axes and useful edge cross axes must overlap within skin. | Minimum-overlap normal points A to B; face/edge penetration follows the polytope rules above. | 4 for face; 1 for edge | Authored face/edge source ids feed the poly encoders. | Same shared reducer. |

### Shared Candidate-Reduction Contract

`SelectContactCandidateIndices` owns the object face-patch policy:

1. Select greatest penetration first; a penetration tie within `1e-5` selects
   the smaller feature id.
2. Build a deterministic tangent basis from the normal.
3. Repeatedly select the candidate whose minimum squared tangent distance from
   the selected set is greatest.
4. Reject points within squared tangent distance `1e-6` of a selected point.
5. Resolve spread ties within `1e-5` by penetration, then feature id.
6. Stop at four rows.

This policy is insertion-order independent only when geometric and feature
identities are themselves stable; NM2 must prove that with permuted candidates.

## Terrain-Manifold Contract Matrix

Terrain manifolds require a valid terrain view, a non-fixed body, and a valid
sweep hit. Their normal is the collided terrain-plane normal. This is a
terrain-specific convention rather than the object manifold's body-A-to-body-B
convention because terrain is represented by sentinel body B `-1`.

| Shape | Entry and geometry | Penetration | Maximum rows | Feature identity | Reduction/policy |
|---|---|---|---:|---|---|
| sphere | Bottom pole along the terrain normal. | Negative signed plane distance. | 1 | Literal `0`. | None. |
| box | All 8 oriented corners are measured; vertices within `minSignedDistance + max(0, terrainContactThreshold)` survive. | `max(0, -signedDistance)`. | 8 at rest; 1 on fast impact | Stable corner index plus one. Fast-impact centroid uses `0x7fff`. | Stable corner order. A closing speed beyond restitution threshold collapses a multi-row patch to its centroid. |
| hull | All authored hull vertices are measured; the same deepest-band threshold applies and publication stops at 8. | `max(0, -signedDistance)`. | 8 at rest; 1 on fast impact | `0x6000` plus the low 12 bits of authored vertex index. Fast-impact centroid uses `0x7fff`. | Stable authored vertex order, then the same fast-impact centroid collapse. |

Every terrain manifold also derives one tangent basis and classifies box/hull
support. Unsupported edge/point contacts still solve, but set the resting,
friction, and sleep-inhibition metadata rather than silently disappearing.

## Existing Manifold Tests

`SkullbonezTests/TestObjectContactManifold.cpp` contains five cases.

| Existing case | What it genuinely asserts | Classification and residual gap |
|---|---|---|
| Unchanged box stack keeps four stable face rows | Four rows, distinct/stable feature ids, stable point placement and penetration across identical evaluations. | Behavioral for box/box repeat identity; no hand-derived normal or geometry. |
| Reduced tilted face starts with deepest retained point | Returned row zero equals the deepest returned row for at least one tilted four-point patch. | Behavioral for deepest-first, but expected depth is derived from current output and there is no spread or insertion-order control. |
| Coplanar face and degenerate slab stay finite and nonempty | Bounded row count, finite point/normal/penetration, nonnegative penetration. | Structural safety/non-emptiness only. |
| Boundary-band feature selection is stable across ten evaluations | Identical row count, normal, ids, and penetration for ten identical calls. | Behavioral repeatability at one box/box boundary; no perturbation sweep. |
| Every object manifold shape pair publishes contacts | All nine ordered pair permutations return finite, nonempty rows; two separated pairs miss; one sphere sweep returns a bounded time. | Non-emptiness only for sphere, mixed, and hull geometry. It does not pin normal, penetration, point count, or placement. |

No current test directly exercises the shared reducer with permuted candidate
order, the feature-id tie break, or a warm-start cache hit/miss caused solely by
narrowphase feature identity.

## Sleep State And Wake Matrix

| Transition or entry | Trigger and state mutation | Current focused reach |
|---|---|---|
| Cold mirror / awake-list rebuild | Topology, replay, config, or row-size mismatch copies body sleep flags, normalizes fixed rows, and rebuilds ascending dynamic-awake indices plus reverse positions. | Directly asserted in `TestPhysicsStageState.cpp`, including same-count topology invalidation. |
| Seed awake to asleep | Sleep enabled and a valid dynamic body: set sleep state, remove awake index, seed counter, clear underwater lock, assign visual-island id. | Directly used by the awake-list and underwater cases. |
| Island awake to asleep | A whole connected island is quiet, supported, eligible, and at the frame threshold: set sleep state, remove awake index, assign visual id, zero velocities/hot awake, then consider underwater lock. | Directly asserted by the one-frame transition case; end-to-end threshold behavior is also asserted in `TestDeterminism.cpp`. |
| Direct dynamic wake | Valid non-fixed row: clear sleep/counter/underwater/visual state, set hot awake, forget cache, and insert into the sorted awake list when it was sleeping. | Reached through public explicit-wake behavior, but fan-out ownership is not isolated. |
| Same-step dynamic wake with forces | Direct wake plus reset step time and reapply forces before cache/list publication. | Used by automatic point-joint wake; no focused assertion covers its ordering or one-application rule. |
| Narrowphase atomic wake | One compare/exchange owns sleeping-to-awake, pending-index publication, counter/visual reset, step-time reset, hot awake, and force application. Sequencer later sorts the pending index. Fixed and underwater-locked rows are rejected. | Directly asserted for one body and sorted flush in `TestPhysicsStageState.cpp`; worker-count exactness is end-to-end in `TestDeterminism.cpp`. |
| Visual-island wake | Public `WakeModel` wakes every row sharing the selected positive visual id, or only the selected row when no id exists. | No focused fan-out assertion. Existing engine wake tests check the selected body only. |
| Explicit point-joint island wake | Public `WakeModel` rebuilds point-joint connectivity and wakes every non-fixed body in the selected component. | No focused wake assertion. The existing point-joint case tests sleep eligibility, not wake propagation. |
| Resting-contact island wake | Public `WakeModel` performs a bounded breadth-first traversal through retained contact edges or the proximity/vertical resting-neighbor rule and wakes every eligible sleeping row reached. | No focused traversal, boundary, or false-neighbor assertion. |
| Automatic mixed point-joint wake | If a point-joint component has both an awake member and a sleeping member, wake sleeping members with same-step force application. | No focused assertion. |
| Underwater lock | A sleeping fully submerged ball sets the lock, zeroes remaining time and all velocities, and remains not awake. Explicit wake rechecks submersion and refuses while locked. | Lock and disable-sleep clearing are direct; ordinary wake refusal and a normal release/wake sequence are not asserted. |
| Disable sleep | Clear sleep counters, locks, and visual ids; mark awake-list topology for cold rebuild. | Directly asserted for state/lock clearing; complete rebuilt membership is not asserted in the same case. |
| Support propagation | Directed support edges iterate to a fixed point; fixed, sleeping, or already-supported supporters propagate support upward. Point joints add both directions. | A two-edge fixed-anchor chain is direct. Symmetric point-joint support and invalid-edge behavior are not isolated. |
| Awake-list add/remove | Add inserts in ascending dense order; remove compacts and repairs reverse positions. Invalid/stale inputs force a future rebuild. | Remove is exercised through seed and island sleep; add through pending wake flush; no dedicated mixed-sequence invariant matrix exists. |
| Support-edge capacity Lane F | `ValidateSleepSupportEdgeCount` rejects either the semantic ceiling or the scene-committed capacity before `emplace_back`. | The omnibus runtime-contract child probe reaches the semantic ceiling through `AppendSleepSupportEdge`; reserved-capacity exhaustion below the semantic ceiling is not isolated. |
| Replay restore | Copies all retained sleep rows, clears pending wakes, and invalidates the derived awake list for cold rebuild. | Covered only as part of broad replay/determinism state restoration, not as a sleep-owner transition case. |

The current named sleep corpus comprises thirteen behavior-oriented cases across
`TestDeterminism.cpp`, `TestPersistentContactSolver.cpp`,
`TestPhysicsStageState.cpp`, and `TestSolverBroadphaseStage.cpp`. An additional
omnibus runtime-contract case contains the support-edge fatal probe. None of
these tests merely asserts that a sleep container is nonempty; the gap is
focused reachability and transition specificity, especially the three explicit
wake fan-out paths.

No production one-hop wake policy was found. Visual and point-joint wake operate
over a complete visual/component membership; resting-contact wake and support
propagation iterate transitively. NM4 must therefore assert the actual bounded
fan-out contracts and must not invent a one-hop expectation that the source does
not own.

## Exact Untested Work Carried Forward

NM1 must add hand-derived normal, penetration, point-count, and placement
expectations for sphere/sphere, sphere/box inside/surface/near, sphere/hull
face/edge/vertex, mixed box/hull face/edge/vertex contact, box/box face-edge and
edge-edge configurations, and hull/hull face/edge/deep overlap.

NM2 must isolate feature-id lifetime under sub-slop perturbation, the 45-degree
reference/incident boundary, insertion-order-independent candidate selection,
deepest-first and feature-id ties, spread selection, and an actual warm-start
hit/miss consequence.

NM4 must directly cover visual-island fan-out, point-joint component fan-out,
resting-contact traversal and its exclusion boundaries, explicit and automatic
same-step wake, underwater refusal and release, awake-list rebuild/add/remove,
bidirectional point-joint support, and both semantic and reserved-capacity
support-edge failure.

## Validation

Documentation-only NM0 slice. No repository validation was required or run.

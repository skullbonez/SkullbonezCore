# Terrain Legacy And Contact Seed Remediation

Date: 2026-07-26
Status: IN PROGRESS — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 13 of the Architecture Follow-Up Campaign
Round 5. Runs last in the campaign. 3/5 phases complete; T3 is binding.
Impact area: `World/Terrain.{h,cpp}`, `Physics/PhysicsTerrainView.{h,cpp}`,
`Physics/PersistentContactSolver.cpp`, `Runtime/Debug/PhysicsDebugVisualizer.cpp`
Owner: physics + world
Priority: Medium — `Terrain.cpp` is the oldest code in the tree and the one
subsystem where the governance sweep stopped at the comment layer. The
contact-seed item is a documented approximation that should be an owner decision
rather than an accident of history.

## Problem And Evidence (measured 2026-07-26)

### 1. The axis swap is documented rather than resolved

`World/Terrain.cpp:867-869`, inside `LocatePolygon`:

> NOTE: X and Z params are switched in this method to account for world
> co-ordinate space ... (treat m_terrain as orthagonal XZ projection to locate
> the quadric)

`Physics/PhysicsTerrainView.cpp:88-89` repeats the same fact from the other side:

> Invariant: the historic terrain cache maps Z to xPosting and X to zPosting.
> The names look crossed, but changing them rotates the surface.

So two owners each carry a comment explaining that the variable names lie, and
the comment is the only thing preventing a future edit from rotating the world.
`Terrain.cpp` uses `quadric` throughout for what is a quad, and spells
`orthagonal` and `co-ordinate`. `Terrain.cpp:891` tests a float with
`if ( !zRelativePosition )` — implicit float-to-bool for an exact-zero check, an
outlier in a file set that otherwise compares explicitly.

### 2. A load-bearing index derivation is undocumented at the index site

`LocatePolygon` computes
`targetQuadric = zPosting * m_postsPerSide + xPosting + m_postsPerSide`
(`Terrain.cpp:873`) and then reads `m_postData[targetQuadric + 1]` (`:926`) and
`m_postData[targetQuadric - m_postsPerSide + 1]` (`:919,925`) with unchecked
`std::vector::operator[]`.

The review verified this is **safe**, but only through a derivation five hundred
lines away: `m_terrainSizeWorldCoords = ((m_mapSize - m_stepSize) / m_stepSize) *
m_stepSize` (`Terrain.cpp:117`) equals `mapSize - stepSize`, and
`PhysicsTerrainView::IsInBounds` (`PhysicsTerrainView.cpp:49`) tests
`x < extent` strictly, so `xPosting` and `zPosting` cap at `postsPerSide - 2` and
the maximum index reached is `postsPerSide² - 1`. Nothing at the index site says
so, and `m_postData` is `resize`d to `postsPerSide²` at `Terrain.cpp:393-395`
with no bound assertion. `PhysicsTerrainView::HeightAndPlaneAt` does guard
(`PhysicsTerrainView.cpp:92`); `LocatePolygon` does not. `LocatePolygon`'s only
remaining production caller is
`Runtime/Debug/PhysicsDebugVisualizer.cpp:502`.

### 3. Terrain contact support is seeded from hand-computed weight

`Physics/PersistentContactSolver.cpp:1005-1011`:

```cpp
const float warmStartTotal = m_bodyRecords[manifold.bodyA].mass *
                             stepPolicy.gravityMagnitude *
                             fabsf( manifold.normal.y ) * dt * supportSeedScale;
const float warmStartPerContact = warmStartTotal / manifold.pointCount;
```

The comment above it (`:994-1003`) is honest about what this is: a "full
gravity-sized seed so a body already on the ground does not sink before the
solver converges", plus a `0.35f` shoreline seed so "a half-wet log or jetty beam"
does not bob. Three properties worth an explicit owner ruling rather than
inheritance:

- It substitutes a computed weight for solver convergence. The cached
  accumulated impulse still wins where larger (`:1289-1292`), so stacks behave,
  but a first-frame resting contact is propped rather than solved.
- `fabsf( normal.y )` approximates the supported fraction of gravity and assumes
  gravity along -Y. `stepPolicy.gravityMagnitude` is
  `fabsf( settings.worldForces.gravity )` (`:104`), a scalar, so a non-vertical
  gravity configuration silently misattributes the seed.
- `supportSeedScale` is a bare `0.35f` literal with no named constant and no
  test pinning the shoreline behavior it exists for.

## Goal

The terrain axis convention is expressed in code rather than in warnings about
the code. Terrain index safety is provable at the index site. The contact support
seed is either replaced by something principled or explicitly ratified as an
approximation with a named constant, a stated assumption, and a test.

## Non-Goals

- **No behavior change at all.** The 44,401-row physics regression CSV stays
  byte-exact in every phase. Following the 2026-07-27 T3 ruling no phase in this
  plan may request divergence authority or refresh a baseline.
- No terrain rewrite. No change to heightfield storage, generation, normals,
  render mesh construction, or the terrain file format.
- No change to the terrain collision algorithm, manifold construction, or the
  solver's PGS structure.
- No removal of `LocatePolygon` merely because it has one caller. If T2 shows the
  debug visualizer should use the guarded cache path instead, that is a ruled
  outcome, not an assumption.
- No new gravity-direction feature. T3 may *document* that the seed assumes
  vertical gravity; implementing non-vertical gravity support is out of scope.

## Phases

- [x] **T0 — Establish the byte-exactness harness first.**
  Before touching any source, capture the current 44,401-row physics regression
  CSV and the SkullScope terrain query baselines from the current Debug binary, and
  confirm they match the committed baselines exactly. Identify the smallest
  focused test set that covers terrain contact, shoreline seeding, and resting
  support so T1-T3 have a fast oracle before the full gate. Acceptance: current
  baselines reproduce byte-exactly; the focused terrain test set is named and
  passing; any pre-existing mismatch is reported and blocks the plan rather than
  being absorbed.

- [x] **T1 — Resolve the axis convention in code.**
  Replace the crossed names with names that describe what the values are, so no
  comment is required to prevent a rotation. Both sides move together:
  `Terrain::LocatePolygon`, `Terrain::QueryCollisionDataUnchecked`, the cached
  quad build at `Terrain.cpp:422-470`, and
  `PhysicsTerrainView::HeightAndPlaneAt`. Rename `quadric` to `quad` throughout,
  fix `orthagonal` and `co-ordinate`, and replace
  `if ( !zRelativePosition )` with an explicit comparison. Acceptance: no comment
  in either file warns that a name is misleading; physics CSV byte-exact; terrain
  focused tests pass; `validate_physics.bat` and `validate_physics_deep.bat` pass.

- [x] **T2 — Make terrain index safety local and provable.**
  State the bound at the index site. Add the derivation as an `Invariant:` comment
  where `targetQuad` is computed, and add a debug assertion or lane-F guard so an
  out-of-range index is caught at the read rather than depending on a constant
  defined 750 lines earlier. Rule whether `LocatePolygon` should adopt the same
  explicit `postsPerSide` guard `PhysicsTerrainView::HeightAndPlaneAt` already has
  (`PhysicsTerrainView.cpp:92`), or whether its sole debug-visualizer caller should
  move to the guarded cache path. Acceptance: every `m_postData` index in
  `LocatePolygon` is bounded by a check or a stated invariant at the site; a
  deliberately out-of-range probe trips the guard in Debug; physics CSV byte-exact;
  the debug visualizer still draws the same polygon.

- [ ] **T3 — Document and test the ratified terrain contact support seed.**
  **Owner ruling 2026-07-27: ratify the approximation. Do not replace the seed.**
  The reason, recorded so it is not relitigated: the seed works, its existing
  comment is already honest about what it is, and replacing it would cost a full
  physics and replay baseline transition needing personal owner re-approval for no
  visible gameplay gain. This plan therefore stays byte-exact and requires no
  divergence authority at any phase.

  Produce the measured behavior as evidence anyway, because it is what the tests
  are written against: what happens to a first-frame resting contact, a shoreline
  edge contact, and a three-box stack with the seed at full strength, at `0.35`,
  and at zero, measured through the focused tests and SkullScope.

  Then implement the ratified outcome: name the `0.35f` constant, state the
  vertical-gravity and `fabsf(normal.y)` assumptions as `Invariant:`/`Hazard:`
  comments at the seed site, and add focused tests pinning both behaviors the seed
  exists for — a first-frame resting contact that must not sink, and a shoreline
  edge contact that must not bob. `stepPolicy.gravityMagnitude` is a scalar
  (`PersistentContactSolver.cpp:104`), so a non-vertical gravity configuration
  would silently misattribute the seed; say so in the `Hazard:` comment rather
  than fixing it, because non-vertical gravity support is an explicit non-goal.
  Acceptance: the seed has a named constant, stated assumptions, and tests pinning
  both behaviors; the physics CSV is byte-exact against the committed baseline; no
  baseline, golden, or config moves.

- [ ] **T4 — Reconcile, review, and hand off.**
  Re-run the T0 harness at final source. Complete the comment audit for all four
  touched files — `Terrain.cpp` has not had a comment pass at this depth and the
  audit must cover the whole file, not only the diff. Obtain one independent review
  asking: can a future edit still rotate the terrain surface, is every terrain
  index bound provable at its site, and is the contact seed's assumption set
  complete and tested. Acceptance: review clear; `validate_physics.bat`,
  `validate_physics_deep.bat`, `validate_perf.bat`, and `validate_full.bat` pass
  with the CSV byte-exact from the final Debug binary and no SkullScope, replay, or
  DX12 baseline refresh.

## Dependencies And Decisions

- Runs last in the campaign. `scene-sized-store-capacity` SC5 and
  `extraction-scar-remediation` ES0 both touch terrain-adjacent physics files;
  doing this plan after them keeps each byte-exactness proof attributable to one
  change.
- Depends on `governance-shape-to-judgment-conversion` G1 for T4's review test.
- **Ratified 2026-07-27: T3 ratifies the approximation.** No open decision
  remains and this plan requires no divergence authority at any phase, so the
  campaign is byte-exact throughout. A principled replacement is explicitly *not*
  a deferred follow-up row: if the owner later wants it, it is registered as its
  own plan with its own divergence gate.
- `LocatePolygon`'s survival is a T2 implementation call, not an owner decision.
  Default, so the agent is not blocked: add the bound guard and leave the method in
  place, then report — without implementing — whether its one debug-visualizer
  caller should move to the guarded cache path. Moving it would change what the
  visualizer draws, which is behavior this plan does not own.

## Acceptance

- No comment in `Terrain.cpp` or `PhysicsTerrainView.cpp` warns that a name is
  misleading.
- Every `m_postData` index is bounded at its site by a guard or a stated
  invariant.
- The contact support seed has a named constant, stated assumptions, and focused
  tests.
- Physics output byte-exact throughout. Under the T3 ruling no phase of this plan
  may move a physics, SkullScope, replay, or DX12 baseline.

## Validation

Per the File To Validation Mapping, `WorldEnvironment*`-class and terrain physics
changes require the physics gate; SkullScope-visible terrain queries require the
deep gate:

- `tools\validate_physics.bat` — byte-exact 44,401-row CSV diff
- `tools\validate_physics_deep.bat` — terrain SkullScope query baselines
- `tools\validate_perf.bat` — `LocatePolygon` and the solver seed are on measured
  paths
- `tools\validate_full.bat` — required at the closure gate

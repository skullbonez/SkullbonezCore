# Plan - Catto Physics Branch Optimisation Roadmap

**Date:** 2026-06-02
**Status:** Draft for review
**Current edit type:** Documentation only
**Primary future impact areas:** Physics, performance, tests, diagnostics

## Context

This document covers how to optimise the work on the `implement-catto-physics-audit`
branch. It is deliberately narrower than
`Agentic/Plans/physics-solver-optimisation-suggestions.md`: that older plan is the broad
solver backlog, while this document focuses on the new Catto-branch behaviour.

The branch now contains several correctness and diagnosis changes that should shape the next
optimisation pass:

- Catto-aligned object contact manifolds and more stable box stacking.
- Tighter sleep/support eligibility so contacts are not automatically treated as stable rest.
- Collision visualisation, Q scene reset, and nudge repro snapshot tooling.
- A terrain box rest-support policy that can deny gravity warm-start, static-friction floor,
  rolling damping, and sleep support for unstable edge or point contacts.
- An unattended `<detector-command>` path that searches for stationary boxes balanced on too few
  terrain-supported vertices.

The main optimisation risk is optimising a transitional behaviour before it is proven correct.
The branch is specifically trying to prevent false rest states, so any performance work must keep
that correctness target visible.

## Optimisation Goals

Optimise in this order:

1. Measure the new branch-specific cost centres.
2. Make the terrain support classification cheap and single-source.
3. Keep diagnostic tooling from adding accidental hot-path cost.
4. Move repeated contact policy decisions toward generated contact data.
5. Only then continue the larger shared solver/body-cache work from the Catto audit.

Do not fix performance by adding damping, weakening the edge-rest predicate, allowing false
sleep, or collapsing valid multi-point support contacts. Those would make the branch faster by
walking away from the bug it is trying to catch.

## Current Hot Spots And Risks

| Area | Why it matters | Optimisation concern |
|------|----------------|----------------------|
| Terrain support policy | Runs inside `ImpulseSolver::RespondCollisionTerrain`, which is already one of the expensive physics markers. | Box support classification now adds face-axis tests and terrain-height checks for up to 8 OBB vertices. |
| Low-repro detector | Scans boxes during unattended repro runs and can build object contact manifolds to reject supported/contacting candidates. | It is diagnostic-only, but a long run can still burn CPU if every frame scans many objects. |
| Sleep support semantics | Terrain and object contacts now distinguish support from ordinary collision. | Duplicating this logic in multiple places risks both cost and future divergence. |
| Contact manifolds | The branch adds more correct multi-row box contacts. | Multi-row contacts increase solver work, and they require better cache identity before warm-start optimisation is safe. |
| Updated baselines | Physics CSV output moved intentionally. | Future optimisation must separate intentional physics changes from accidental drift. |

## Priority 0 - Profile The New Branch Behaviour First

Before changing code, split profiler markers enough to identify whether the new support policy is
actually material.

Suggested markers:

- `Frame/Physics/Terrain/BoxSupportPolicy`
- `Frame/Physics/Terrain/BoxSupportPolicyFaceAxes`
- `Frame/Physics/Terrain/BoxSupportPolicyVerts`
- `Frame/Physics/Terrain/UnstableSupportRowsSkipped`
- `Frame/Physics/Terrain/SolveRows`
- `Frame/Physics/LowReproDetector/Scan`
- `Frame/Physics/LowReproDetector/TerrainVerts`
- `Frame/Physics/LowReproDetector/ObjectContactReject`
- `Frame/Physics/ObjectContacts/BuildManifolds`
- `Frame/Physics/ObjectContacts/Solve`

Capture data for:

- `SkullbonezData/scenes/stacking.scene`
- `SkullbonezData/scenes/at_rest.scene`
- `SkullbonezData/scenes/box_crater_edge_repro.scene`
- A generated all-box solver run with `--fixed-step --no-water --all-boxes`
- A generated all-box low-repro detector run with `<detector-command>`

The decision point is simple: if terrain support classification is noise, do not micro-optimise it
yet. If it is visible in box-heavy scenes, optimise the classification before touching larger solver
architecture.

## Priority 1 - Make Box Terrain Support Classification Cheaper

The current support policy is correct in spirit: only plausible rest footprints get rest-only
privileges. The first optimisation pass should preserve that behaviour while reducing repeated
work.

Recommended changes:

1. Extract a small internal support classifier used by both terrain solving and diagnostic checks.
   It should return the support decision plus the measured facts behind it:
   - whether the shape is a box,
   - best face-normal dot against the terrain plane,
   - terrain-supported vertex count,
   - min/max terrain gap when needed,
   - whether rest-support policy is allowed.
2. Keep the fast rejection order:
   - non-box shapes skip all box policy work,
   - obvious stable full manifolds avoid extra terrain-vertex checks where safe,
   - unstable low-row manifolds do only the minimum work needed to reject rest support.
3. Avoid repeated `std::visit` and shape extraction in the same terrain response call.
4. Reuse canonical local box corners instead of rebuilding the eight corner sign combinations in
   every support test.
5. Use a height-only terrain query for vertex support checks unless the plane is actually needed.
6. Consider a one-tick support result cache only after profiling proves repeated terrain vertex
   checks are expensive. Cache invalidation must include movement, rotation, wake, teleport,
   scene reset, terrain change, and object respawn.

Expected result:

- Same sleep/support decisions as the branch currently intends.
- Less terrain query and box-corner setup cost in box-heavy scenes.
- One support-policy implementation for future audit work.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

If `SkullbonezGameModelCollection*` is touched during the extraction, prefer
`tools\validate_full.bat`.

## Priority 2 - Keep Low-Repro Detector Cost Controlled

`<detector-command>` is useful because it turns a visual bug hunt into an unattended repro search.
It should stay cheap enough for long runs, but it should not influence normal runtime paths.

Recommended changes:

1. Keep the feature disabled by default and ensure normal runs pay only the existing boolean branch.
2. In low-repro detector mode, keep the scan ordered from cheapest to most expensive:
   - skip non-boxes,
   - skip sleeping boxes if the test is specifically looking for awake impossible rest,
   - check speed and angular speed,
   - check terrain-supported vertex count,
   - track candidate persistence,
   - only then run expensive object-contact rejection before logging a hit.
3. Replace the all-model object-contact rejection scan with broadphase-backed nearby candidates if
   profiling shows it is significant.
4. Share the same terrain support classifier used by `RespondCollisionTerrain`.
5. Keep log writes one-shot and avoid per-frame file I/O after the header is written.

Expected result:

- The test remains strict enough to catch the edge-rest failure.
- Long repro runs spend most time in actual simulation, not diagnostic scanning.
- The diagnostic path does not become another source of support-policy divergence.

Validation for implementation:

```bat
tools\validate_physics.bat
```

Run at least one manual low-repro detector command afterwards and preserve the log if it finds a hit.

## Priority 3 - Push Rest-Support Policy Toward Contact Data

The terrain solver currently decides rest-support policy inside response. That is pragmatic, but
the longer-term Catto shape is cleaner if contact generation emits enough facts for the solver to
stay generic.

Recommended direction:

1. Generate contact rows with support flags:
   - can solve normal impulse,
   - can receive gravity warm-start,
   - can receive static-friction floor,
   - can apply rest rolling damping,
   - can seed sleep.
2. Keep unstable edge/point rows valid for real penetration or impact response.
3. Keep the current branch invariant: a contact that cannot seed sleep also cannot receive
   artificial rest support.
4. Report row counts by category so future optimisation can distinguish real contact work from
   rest-policy bookkeeping.

Expected result:

- Less special policy branching inside the inner solver loop.
- Cleaner path to a shared terrain/object contact solver.
- Easier validation because support policy is visible in generated contact data.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Priority 4 - Continue The Shared Solver Work After Policy Stabilises

Once the branch-specific support behaviour is measured and centralised, continue the larger Catto
optimisation direction:

1. Add a compact solver body cache for awake bodies:
   - linear velocity,
   - angular velocity,
   - inverse mass,
   - world inverse inertia,
   - awake/sleep/support state.
2. Build terrain and object contact rows against body indices rather than mutating `GameModel`
   state inside every row solve.
3. Write velocities back once after the solver pass.
4. Use feature/contact IDs for cache identity before doing more warm-start work.
5. Keep multi-point manifolds intact. The prior centroid-collapse experiment regressed because it
   removed the rotational support boxes needed to settle.

Expected result:

- Object-contact solving gets the same cache-friendly shape as the terrain solver.
- Future SIMD and SoA work becomes safer because the row/body data layout is explicit.
- Warm starting can be evaluated with correct contact identity instead of pair-only guesses.

Validation for implementation:

```bat
tools\validate_full.bat
```

Also run `tools\validate_perf.bat` separately if the full script does not capture the target perf
scene closely enough.

## Priority 5 - Revisit Broadphase And Sleeping After Correctness

Sleeping produced the largest historical physics performance win, but this branch exists because
false sleep/support states are dangerous. Optimise sleeping only after the support semantics are
stable.

Recommended changes:

1. Keep sleep eligibility derived from proven support, not merely contact.
2. Measure awake/sleeping pair generation separately.
3. Consider separate awake and sleeping grids only after contact support is correct.
4. Wake sleeping bodies from actual overlap or conservative predicted overlap, not broad contact
   suspicion alone.
5. Validate stack, slope, and at-rest scenes with sleeping enabled and disabled when possible.

Expected result:

- Settled scenes remain fast.
- Mid-air or edge-balanced boxes do not become performance wins by incorrectly going to sleep.
- Broadphase work scales better once the support model is trustworthy.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` for changes that touch scene runtime, renderer-facing validation, or
multiple systems.

## Guardrails

- Do not use extra damping, lower restitution, altered gravity, or scene tuning to hide edge-rest
  cases or false sleep.
- Do not resurrect terrain contact caching unless fresh profiling proves it helps this branch.
  The earlier experiment made the adaptive solver converge worse.
- Do not collapse multi-point box contacts into centroids as a general optimisation. The earlier
  attempt increased awake object count and worsened performance.
- Do not optimise pair-only contact cache lookup before contact identity includes feature or
  contact IDs.
- Treat physics CSV differences as real until they are explained and intentionally baselined.
- Keep renderer and UI diagnostics separate from physics hot-path changes unless profiling proves
  they interact.

## Suggested Implementation Order

1. Add profiler markers for support policy, low-repro detector scan, and object manifold work.
2. Capture fresh physics/perf numbers for stacking, at-rest, edge repro, and generated all-box runs.
3. Extract a shared box-terrain support classifier.
4. Reorder low-repro detector work so expensive object-contact rejection happens only for persistent
   candidates.
5. Convert rest-support decisions into generated contact-row flags.
6. Add solver body-state arrays and write back once per physics step.
7. Upgrade contact cache identity for multi-row manifolds.
8. Revisit awake/sleeping broadphase partitioning after the above behaviour is stable.

## Validation Plan For This Markdown Change

Run:

```bat
tools\validate_fast.bat
```

Future code implementation should use the stricter validation listed in each priority section.


# Plan - Catto Physics Branch Optimisation Roadmap

**Date:** 2026-06-02
**Status:** Draft for review
**Current edit type:** Documentation only
**Primary future impact areas:** Physics, performance, tests, diagnostics

## Context

This document covers how to optimise the current `implement-catto-physics-audit` branch after
the recent code updates. It is narrower than
`Agentic/Plans/physics-solver-optimisation-suggestions.md`: that older plan is the broad solver
backlog, while this roadmap focuses on what is left after the Catto branch work already landed.

Current code already includes several of the big architectural steps:

- Catto-style object contact manifolds for sphere/sphere, sphere/box, and OBB/OBB contacts.
- Per-contact feature IDs for persistent warm starting.
- A compact persistent-contact solver body cache with one write-back after PGS.
- Sorted persistent-contact cache lookup instead of pair-wide linear matching.
- Vector cone clamping for object-contact friction.
- Terrain support policy that withholds gravity warm-start, static-friction floor, rolling damping,
  and sleep support from unstable box edge or point contacts.
- A deterministic edge-rest repro scene at `SkullbonezData/scenes/standing_box_repro.scene`.
- Debug nudge snapshots with terrain support probes and replay hints.

The main optimisation risk has changed: do not plan to add systems that are already present.
The next useful work is to measure the remaining terrain/support cost, remove duplicated support
classification logic, and decide when terrain contacts should share more of the object-contact
row pipeline.

## Current Code Baseline

| Area | Current state | Optimisation concern |
|------|---------------|----------------------|
| Terrain solver | Terrain contacts now feed the shared row solver through explicit terrain manifolds. | Remaining optimisation work should measure terrain row build/solve cost rather than duplicate response math. |
| Terrain box support policy | Face-axis checks and OBB vertex terrain-height probes gate rest-only privileges. | The same terrain-supported-vertex idea also exists in debug nudge snapshot logging. |
| Object persistent solver | Uses manifolds, feature IDs, sorted warm-start cache lookup, solver body cache, and vector friction clamp. | The next gains are profiling/submarker cleanup and avoiding redundant work, not adding these systems from scratch. |
| Sleep support propagation | Terrain seeds support; object contacts pass it through directed stack edges. | The relaxation loop is simple and correct, but should be measured in stack-heavy scenes. |
| Repro tooling | Current reproducible path is a seeded scene plus nudge snapshots. | There is not currently a wired `--standing-test` or equivalent detector command in runtime parsing. |

## Priority 0 - Profile The Updated Branch First

Before changing code, split markers around the costs that remain ambiguous. Some useful markers
already exist, including `Frame/Physics/Terrain/BoxVertexManifold`,
`Frame/Physics/Terrain/BoxSupportPolicyVerts`, and
`Frame/Physics/Narrowphase/PersistentContacts`; keep those and add finer splits only where they
answer a decision.

Suggested new or refined markers:

- `Frame/Physics/Terrain/BoxSupportPolicy`
- `Frame/Physics/Terrain/BoxSupportPolicyFaceAxes`
- `Frame/Physics/Terrain/SolveRows`
- `Frame/Physics/Terrain/RollingDamping`
- `Frame/Physics/Narrowphase/BuildManifolds`
- `Frame/Physics/Narrowphase/WarmStartLookup`
- `Frame/Physics/Narrowphase/SolveRows`
- `Frame/Physics/Narrowphase/PositionCorrection`
- `Frame/Physics/SleepSupport/Propagate`
- `Frame/Debug/NudgeRepro/TerrainSupportProbe`

Capture data for:

- `SkullbonezData/scenes/standing_box_repro.scene`
- `SkullbonezData/scenes/box_crater_edge_repro.scene`
- `SkullbonezData/scenes/stacking.scene`
- `SkullbonezData/scenes/at_rest.scene`
- A generated all-box run with `--fixed-step --no-water --all-boxes`
- A solver scene with `solver_boxes` and `perf_log` if a stable per-frame CSV is needed

Decision rule: if the new support policy is not visible in these profiles, leave it alone and
focus on shared row architecture. If it is visible, centralise and cheapen support classification
before touching broader solver behaviour.

## Priority 1 - Centralise Box Terrain Support Classification

The terrain support policy is doing important correctness work. Optimise it by making it one
small, reusable classifier rather than by weakening the predicate.

Recommended changes:

1. Extract a local helper such as `ClassifyBoxTerrainSupport`.
2. Return a compact result:
   - `isBox`,
   - best face-normal dot,
   - supported terrain vertex count,
   - min/max terrain gap when requested by diagnostics,
   - `supportsRestingPolicy`.
3. Use the helper from terrain response and Debug nudge snapshot logging.
4. Keep the current fast rejection order:
   - non-box shapes skip all box work,
   - obvious unstable low-row manifolds avoid unnecessary terrain sampling,
   - terrain vertex sampling happens only when it can change the support decision or diagnostic log.
5. Avoid repeated `std::visit` and shape extraction in the same response call.
6. Reuse canonical local box corners instead of rebuilding the eight sign combinations in every
   support probe.
7. Use height-only terrain queries for vertex support tests unless the plane is actually needed.
8. Keep the support constants in one place. Do not make them broad config knobs unless profiling or
   tests prove tuning is needed.

Expected result:

- Same support/sleep decisions as the current branch.
- Less duplicated terrain support math.
- Debug snapshot fields stay consistent with the solver's actual support policy.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

If `SkullbonezRun*` or `SkullbonezGameModelCollection*` is touched, use `tools\validate_full.bat`
or the stricter mapping in `AGENTS.md`.

## Priority 2 - Optimise The Existing Object Contact Solver, Not A Hypothetical One

The object solver already has the main Catto-shaped pieces. The next pass should be measured
cleanup around the current implementation.

Recommended changes:

1. Split the broad `PersistentContacts` marker into build, precompute, warm-start lookup, solve,
   write-back, position-correction, and cache-store markers.
2. Confirm whether sorting `m_persistentContactCache` at the start of the pass is still needed,
   since the cache is also sorted after it is rebuilt. If the end-of-pass invariant is reliable,
   replace the start sort with a debug assertion or remove it.
3. Measure feature-ID cache hit rate and row count by contact type:
   - sphere/sphere,
   - sphere/box,
   - OBB face,
   - OBB edge.
4. Measure how often direct position correction fires and how much correction is applied. Do not
   optimise projection-heavy scenes until it is clear projection is not masking solver weakness.
5. Consider island-local early-out only after submarkers show the global 12-iteration solve is a
   real cost in mixed active/settled scenes.

Expected result:

- The current solver gets cheaper without reverting to pair-wide or centroid shortcuts.
- Future work has better data about row counts, cache quality, and correction reliance.
- Correctness remains anchored by feature IDs and multi-point manifolds.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` for broad edits to `SkullbonezGameModelCollection*`.

## Priority 3 - Decide How Much Terrain Should Share With The Object Solver

Terrain still has a separate solver path. That path is fast in Profile because it has an SSE loop,
but it also duplicates normal/tangent effective mass, warm-start policy, early-out, projection, and
friction behaviour.

Recommended direction:

1. First extract shared row setup helpers only where behaviour is identical.
2. Preserve terrain-specific policies explicitly:
   - gravity warm-start floor,
   - rest-support gating,
   - unstable contact row skip,
   - rolling damping,
   - impact-time centroid collapse.
3. Compare terrain's independent tangent clamps with object-contact vector cone clamping. Unify
   only if physics scenes prove the change is intentional and stable.
4. Keep the terrain SSE path until a shared row kernel can match or beat it.
5. Treat a full terrain/object solver merge as a later optimisation, not the next small cleanup.

Expected result:

- Less duplicated solver logic over time.
- No accidental regression of the terrain path that currently carries the edge-rest fix.
- A clear bridge toward one row pipeline without throwing away the working SSE terrain loop.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Run renderer validation too if visual baselines move because terrain/object sleep or orientation
behaviour changes.

## Priority 4 - Keep Repro Tooling Honest And Cheap

Current runtime code does not expose a dedicated unattended edge-rest detector command. The current
repro path is:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\standing_box_repro.scene --seed 4096348761 --no-water
```

Debug builds can also write nudge snapshots from the object under the crosshair. The low-repro
skill documents a useful future pattern, but the roadmap should not refer to placeholder commands
as if they are implemented.

Recommended changes:

1. Keep `standing_box_repro.scene` as the canonical deterministic edge-rest repro.
2. Add a real detector command only if seeded scenes and nudge snapshots are not enough.
3. If a detector is added, wire it through command-line parsing, runtime reference docs, and a
   single log contract in one change.
4. Reuse the nudge snapshot payload for any detector hit so manual and unattended captures are
   comparable.
5. Keep detector scans ordered cheap-to-expensive and disabled by default.

Expected result:

- Repro docs match runtime reality.
- Future detector work has a clear contract.
- Normal runtime pays no hidden diagnostic cost.

Validation for implementation:

```bat
tools\validate_fast.bat
```

For detector code touching `SkullbonezRun*`, use:

```bat
tools\validate_full.bat
```

## Priority 5 - Revisit Sleep And Broadphase After Support Semantics Stay Stable

Sleeping remains the biggest historical performance win, but this branch exists because false
sleep/support states are dangerous. Optimise sleep and broadphase only after support semantics are
stable in the edge-rest, stacking, and at-rest scenes.

Recommended changes:

1. Keep sleep eligibility derived from proven support, not merely contact.
2. Profile `PropagateSleepSupport` in stack-heavy scenes.
3. Replace bounded relaxation with a queue traversal only if support propagation is measurable.
4. Measure awake/sleeping pair generation separately.
5. Consider separate awake and sleeping grids only after contact support is correct.
6. Wake sleeping bodies from actual overlap or conservative predicted overlap, not broad contact
   suspicion alone.

Expected result:

- Settled scenes remain fast.
- Mid-air or edge-balanced boxes do not become performance wins by incorrectly going to sleep.
- Broadphase work scales better once the support model is trustworthy.

Validation for implementation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` for changes touching scene runtime, renderer-facing validation, or
multiple systems.

## Guardrails

- Do not use extra damping, lower restitution, altered gravity, or scene tuning to hide edge-rest
  cases or false sleep.
- Do not resurrect terrain contact caching unless fresh profiling proves it helps this branch.
  The earlier experiment made the adaptive solver converge worse.
- Do not collapse multi-point box contacts into centroids as a general optimisation. The earlier
  attempt increased awake object count and worsened performance.
- Do not regress from feature-ID contact caching back to pair-only matching.
- Treat physics CSV differences as real until they are explained and intentionally baselined.
- Keep diagnostic docs and runtime flags in sync. No placeholder commands in handoff material.

## Suggested Implementation Order

1. Add the profiler submarkers listed above.
2. Capture fresh physics/perf numbers for edge repro, stacking, at-rest, and generated all-box runs.
3. Extract a shared box-terrain support classifier for terrain response and Debug nudge snapshots.
4. Remove redundant object-contact cache sorting only if the sorted invariant is proven.
5. Instrument position correction and contact cache hit rates.
6. Decide whether terrain and object contact row helpers can be shared without losing the terrain
   SSE path.
7. Revisit support propagation and broadphase sleeping partition only after the correctness scenes
   remain stable.

## Validation Plan For This Markdown Change

Run:

```bat
tools\validate_fast.bat
```

Future code implementation should use the stricter validation listed in each priority section.

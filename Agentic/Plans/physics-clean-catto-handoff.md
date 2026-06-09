# Clean Catto Physics Handoff

**Date:** 2026-06-09
**Status:** Implemented and validated in the current workspace
**Current edit type:** Physics/runtime/diagnostics/documentation
**Primary impact areas:** Physics, collision, solver, tests, diagnostics

## Goal

Move Skullbonez object collision away from the current hybrid path and into one
clean Catto-style contact pipeline.

The target is not merely "some Catto helpers are present". The target is that
object/object collision response is owned by the persistent contact solver:

```text
forces
-> broadphase candidate pairs
-> optional object/object TOI or conservative advancement
-> shape manifolds
-> contact rows
-> warm start
-> Projected Gauss-Seidel / sequential impulse solve
-> write back velocities
-> position or split correction
-> cache impulses
-> integrate remaining time
-> sleep islands
```

No legacy object/object impulse path should compete with this pipeline.

## Required Visualizer Caveat

The clean Catto migration should include a physics visualizer mode that can step
through the solver process inside a single frame. This is a user requirement,
not merely optional polish.

The goal is to make every important stage inspectable in order:

1. Broadphase grid occupancy.
2. Broadphase candidate pair generation.
3. Candidate pairs colored by state, for example unchecked, rejected, touching,
   sleeping-pruned, swept-hit, discrete-overlap, or solver-contact.
4. Candidate pair collision tests and their rejection reason.
5. Shape manifold construction for sphere/sphere, sphere/box, box/box, and
   terrain contacts.
6. Contact points, normals, tangents, penetration, feature IDs, and contact row
   keys.
7. Warm-start cache hits/misses and the cached impulse applied per row.
8. PGS/sequential impulse iterations, including normal impulse, tangent impulse,
   friction clamp, bias, and row convergence.
9. Velocity writeback.
10. Position correction or split-correction step.
11. Contact cache storage for the next frame.
12. Sleep support edges, support anchors, and final sleep-island decisions.

The implemented UX is a frame debugger for physics: pause the simulation and use
F7/F8, the physics tab, or SkullScope `pipeline` queries to inspect the Catto
pipeline stage by stage. The visual mode shows the process spatially in the
running scene through the existing physics debug overlay.

This requirement should influence the refactor shape. Prefer explicit stage
records and debug event structs over hidden side effects, so the same data that
drives the solver can also drive the visualizer and SkullScope queries.

## Implementation State

The current workspace has a Catto-style persistent object/object solver as the
single dynamic object response owner. `RunSolverPhysics` still uses the old
object/object swept narrowphase as a CCD front-end, but that front-end now only
finds candidate timing, advances bodies to a time of impact, wakes sleeping
pairs, records debug stages, and clears legacy response flags. It no longer
calls the legacy object/object impulse or overlap-projection wrappers.

Current order in `GameModelCollection::RunSolverPhysics`:

1. Apply forces to awake dynamic bodies.
2. Build broadphase candidate pairs with `SkullbonezSpatialGrid`.
3. Prune pairs where both bodies are sleeping.
4. Run the old object/object swept narrowphase through
   `CollisionDetectGameModel` as CCD/contact discovery only.
5. For swept object/object hits, advance bodies to the candidate collision time,
   wake sleepers, clear legacy response-required flags, and record pipeline
   stages without applying an impulse.
6. Run swept terrain detection/response.
7. Run `SolvePersistentObjectContacts(dt)` on the same candidate pair list.
8. Propagate object contact support.
9. Integrate remaining time.
10. Build/evaluate sleep islands.

The persistent object solver is now the Catto-shaped response path:

1. Build compact `m_solverBodies`.
2. Convert candidate pairs into shape manifolds.
3. Build sphere/sphere, sphere/box, and box/box rows through
   `BuildObjectContactManifold`.
4. Precompute tangents, effective mass, bias, friction limits, and cached
   impulses, including dynamic/dynamic restitution bias.
5. Warm start by applying cached impulses to solver velocities.
6. Run PGS/sequential impulse iterations.
7. Write solved velocities back to `GameModel`.
8. Emit debug contacts and bounded pipeline stage records.
9. Apply direct position correction.
10. Store accumulated impulses for next-frame warm starting.

## Remaining Compatibility Facts

- `CollisionResponseGameModel`, `StaticOverlapResponseGameModel`, and
  `RespondCollisionGameModels` remain as deprecated compatibility wrappers, but
  the active solver frame does not call them for object/object response.
- Object/object boxes still use broadphase radius filtering plus discrete OBB
  manifold contacts in the persistent solver.
- Object/terrain is separate and swept. Terrain response is not currently just
  another contact row in the persistent object solver.
- Direct position correction is a local cleanup step after the velocity solve.
  Treat it as intentional current behavior until a split-correction path proves
  it can replace the direct cleanup.

## Primary Files To Inspect First

| File | Why |
|------|-----|
| `SkullbonezSource/SkullbonezGameModelCollection.cpp` | Frame order, candidate pairs, old object pass, terrain pass, persistent solver, sleep islands. |
| `SkullbonezSource/SkullbonezObjectContactManifold.cpp` | Sphere/sphere, sphere/box, and box/box manifold generation. |
| `SkullbonezSource/SkullbonezImpulseSolver.cpp` | Legacy object response and separate terrain response. |
| `SkullbonezSource/SkullbonezGameModel.cpp` | Collision detect/response wrappers and terrain collision time. |
| `SkullbonezSource/Physics/SkullbonezContactSolver.h` | Shared Catto row math helpers. |
| `Agentic/Reference/physics-overview.md` | Current physics reference, but verify it against code before trusting it. |

Do not rely on old "natural contact solver" notes as current design guidance.
The intended direction is Catto-style persistent contact solving.

## Implementation Plan

### Phase 0 - Add The Step-Through Physics Visualization Contract

Goal: define the debug data contract before rewriting response ownership, so the
new clean pipeline is observable while it is being built.

Recommended steps:

1. Define a compact per-frame physics-stage trace model with stable stage names.
2. Record broadphase pairs, narrowphase decisions, manifold points, contact rows,
   warm-start state, solver iteration deltas, writeback, correction, cache store,
   and sleep-island decisions.
3. Add enough IDs to connect the same body pair from broadphase through manifold
   construction, solver rows, and sleep support.
4. Add a visualizer stepping model:
   - paused frame,
   - current physics stage,
   - current candidate pair or row,
   - next/previous stage controls,
   - optional next/previous pair or row controls.
5. Keep the trace bounded. Large scenes should summarize most pairs and expand
   only the selected pair/row.
6. Make the output compatible with SkullScope queries where possible, so visual
   debugging and agent-readable diagnostics describe the same facts.

Expected result:

- The migration can be inspected stage by stage instead of treated as a black
  box.
- Future numerical differences can be explained by pointing at the exact stage
  where behavior diverged.

Validation for code changes:

```bat
tools\validate_full.bat
```

Use renderer/UI screenshot checks if the in-game physics tab or visual overlay
is changed.

### Phase 1 - Make Object/Object Response Ownership Explicit

Goal: object/object velocities should only be changed by the persistent contact
solver.

Recommended steps:

1. Add comments or temporary assertions around the old object/object response
   boundary so it is obvious when it is still being used.
2. Rename the legacy path if it remains temporarily, for example from generic
   `RespondCollisionGameModels` toward `RespondSphereSphereSweptImpact`.
3. Remove or quarantine unreachable box logic from the old response path.
4. Keep the current behavior covered by physics validation before deleting
   response code.

Expected result:

- A future agent can tell which path owns object/object response.
- Dead box branches are not mistaken for implemented box collision response.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

If `SkullbonezRun*` or broad runtime behavior changes, use:

```bat
tools\validate_full.bat
```

### Phase 2 - Move Sphere/Sphere Fully Into Persistent Rows

Goal: sphere/sphere should no longer receive an immediate legacy impulse before
the persistent solver.

Recommended steps:

1. Disable the old immediate sphere/sphere velocity response behind a temporary
   internal switch or small scoped change.
2. Move dynamic/dynamic restitution into persistent contact row setup for all
   shape pairs, not just fixed-body impacts.
3. Tune restitution threshold carefully so stacks do not jitter.
4. Compare sphere bounce scenes and physics CSVs before accepting the new
   behavior.

Expected result:

- Sphere/sphere pairs are solved the same way as sphere/box and box/box:
  manifold rows, warm start, PGS, writeback.
- Numerical differences from the previous baseline are expected and must be
  explained before rebaselining.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use SkullScope queries for behavioral analysis before changing baselines.

### Phase 3 - Make Object/Object Manifold Generation The Only Narrowphase Input

Goal: all object/object shape pairs should produce solver-compatible contact
data from one narrowphase contract.

Recommended steps:

1. Define a narrowphase result type that can hold:
   - hit/overlap state,
   - optional time of impact,
   - `ObjectContactManifold`,
   - stable feature ids,
   - debug reason flags.
2. Route sphere/sphere through `BuildObjectContactManifold` or an equivalent
   unified entry point rather than a special path in the solver build loop.
3. Keep sphere/box and box/box feature IDs stable. Do not collapse box contacts
   to a single centroid unless a measured and validated replacement exists.
4. Make detection functions return facts only. They should not apply response.

Expected result:

- The solver receives rows; it does not care which shape pair created them.
- Detection and response responsibilities are separated.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

### Phase 4 - Decide Object/Object CCD Explicitly

Goal: avoid recreating the hybrid under a cleaner name.

Recommended sequence:

1. First land a clean discrete Catto object/object solver.
2. Then add object/object CCD/TOI as a front-end if tunneling or gameplay needs
   require it.

Possible CCD direction:

```text
broadphase pair
-> conservative sweep or TOI candidate
-> advance to safe time
-> build manifold at TOI/current position
-> feed rows into the same persistent solver
```

Do not make CCD apply a separate impulse. CCD should decide when and where a
contact exists; the solver should decide the response.

Expected result:

- Discrete and swept object/object contacts share response code.
- CCD can be tested independently from solver impulse math.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Add focused repro scenes or SkullScope queries for any tunneling case.

### Phase 5 - Bring Terrain Toward The Same Row Pipeline

Goal: terrain response should eventually be solver-compatible, while preserving
the terrain support policy that keeps box stacks and sleep stable.

Recommended steps:

1. Keep terrain swept detection at first.
2. Convert terrain hit/contact data into contact rows against a static world
   body or terrain body abstraction.
3. Preserve terrain-specific metadata separately:
   - stable support classification,
   - edge/point support rejection,
   - sleep support/inhibit flags,
   - terrain debug fields.
4. Share Catto row helpers only where behavior is identical.
5. Do not delete the current terrain SSE/response path until the row path is
   measured and validated.

Expected result:

- Terrain can keep swept detection and sleep classification without owning a
  totally separate impulse solver forever.
- The long-term pipeline becomes one response model with terrain-specific
  contact generation and support metadata.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` if runtime flags, scenes, or renderer-visible
baselines move.

### Phase 6 - Delete The Legacy Hybrid Only After Acceptance Scenes Pass

Goal: remove confusing old response code after replacement behavior is proven.

Delete or reduce:

- `RespondCollisionGameModels` as a generic object/object response path.
- Dead box/generalized branches inside that function.
- Any `CollisionResponseGameModel` wrapper that implies object/object response
  can happen outside the persistent solver.
- Any comments or docs that describe the old hybrid as the intended model.

Expected result:

- Object/object response has one owner.
- Debugging future physics bugs starts from the persistent contact pipeline, not
  from competing response paths.

Validation for code changes:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Run `tools\validate_full.bat` for broad cleanup touching runtime, diagnostics,
or multiple systems.

## Acceptance Scenes And Checks

Use the exact scene names available in the repo at the time of implementation,
but start with these known cases:

- `SkullbonezData/scenes/solver_smoke.scene`
- `SkullbonezData/scenes/stacking.scene`
- `SkullbonezData/scenes/at_rest.scene`
- `SkullbonezData/scenes/standing_box_repro.scene`
- `SkullbonezData/scenes/box_crater_edge_repro.scene`
- Generated solver/perf scenes used by `tools\validate_physics.bat` and
  `tools\validate_perf.bat`

Required behavior checks:

- Step-through physics visualization can show broadphase pairs, contact
  decisions, manifold construction, solver rows, solver iterations, correction,
  cache storage, and sleep-island decisions for at least one focused scene.
- Sphere/sphere bounce still works after removing the old response path.
- Dynamic sphere/sphere pairs are not solved twice.
- Sphere/box and box/box contacts produce stable rows and feature IDs.
- Box stacks settle without excessive jitter.
- Sleeping does not freeze unsupported mid-air bodies.
- Terrain support classification still distinguishes stable support from
  edge/point contacts.
- Physics CSV changes are explained and intentionally baselined.
- Perf does not regress enough to hide behind correctness changes.

## SkullScope And Baseline Guidance

Physics determinism baselines are expected to change during the real migration.
Do not claim "no physics behavior changed" if this work removes the old hybrid.

Use SkullScope rather than loading raw logs into the model:

```bat
Profile\SKULLBONEZ_CORE.exe --fixed-step --physics-diag <trace.ndjson> --scene <scene>
tools\physics_query.bat <trace.ndjson> summary
tools\physics_query.bat <trace.ndjson> events
```

When using SkullScope, report the exact trace command, query commands, artifact
sizes, and GPT-read output sizes as required by `AGENTS.md`.

Only update physics baselines after:

1. The behavioral delta is understood.
2. The relevant repro scenes look correct.
3. SkullScope summaries support the new behavior.
4. The user agrees the new behavior is intended.

## Guardrails

- Do not fix solver bugs by changing gravity, damping, scene timing, or object
  masses.
- Do not make the clean Catto migration opaque. The implementation should leave
  a step-through physics visualization path for inspecting each frame stage.
- Do not let object/object CCD apply its own impulse. It may create contacts;
  the persistent solver should respond.
- Do not collapse box manifolds to one point as a shortcut.
- Do not remove sleep support classification while moving terrain toward shared
  rows.
- Do not treat byte-exact physics CSV matches as expected for this migration.
  Numerical differences should happen if response ownership changes.
- Do not trust old comments that imply boxes are handled by the legacy response
  path; verify with code.

## Suggested First PR Scope

Keep the first implementation PR small:

1. Rename or fence the legacy object/object response path so it is explicitly
   sphere/sphere legacy impact only.
2. Remove unreachable box branches/comments from that legacy path.
3. Add diagnostics or counters proving how often the legacy path still runs.
4. Do not change solver behavior yet unless the PR is explicitly about moving
   sphere/sphere into persistent rows.

Suggested validation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` if touched files require it under `AGENTS.md`.

## Suggested Second PR Scope

Move sphere/sphere response ownership to the persistent solver:

1. Add dynamic/dynamic restitution bias in persistent row setup.
2. Disable old immediate sphere/sphere velocity response.
3. Verify bounce scenes, stacks, sleep, and perf.
4. Use SkullScope to document the expected baseline deltas.
5. Rebaseline only after user approval.

## Documentation Cleanup After Code Lands

After the migration lands, update:

- `Agentic/Reference/physics-overview.md`
- any runtime reference text that describes physics diagnostics,
- any stale plans that still describe the old hybrid as intended,
- comments around `CollisionDetectGameModel`, `CollisionResponseGameModel`,
  `RespondCollisionGameModels`, and `SolvePersistentObjectContacts`.

## Validation Gate For This Implementation

This is no longer a documentation-only change. Required validation is
`tools\validate_full.bat`.

Validation completed after the intentional physics CSV and SkullScope query
baselines were approved and updated:

```bat
tools\validate_full.bat
```

Result: all phases passed, including renderer parity, byte-exact physics CSV,
SkullScope query baseline, and perf validation.

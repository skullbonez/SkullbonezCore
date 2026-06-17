# Convex Hull Collision Implementation Report

Date: 2026-06-17
Branch: `codex/convex-hull-collision-implementation-plan`
Plan: `Agentic/Plans/Done/convex-hull-collision-implementation-plan.md`
Impact area: physics, collision, scene system, diagnostics
Required PR gate: `tools\validate_physics.bat`

## Summary

Implemented first-pass authored convex hull collision support without replacing
the existing persistent solver:

- `CollisionShape` now supports `ConvexHullShape`.
- Hull assets load from deterministic `.hull` files with bounded fixed storage,
  convex/topology validation, finite/range-checked coordinates, coplanar faces,
  unique edges, bounding radius, surface estimate, and box-approx inertia.
- Object narrowphase now covers sphere/hull, box/hull, and hull/hull using the
  existing SAT/contact-manifold style.
- Persistent contact cache keys preserve the wider feature IDs needed by hull
  contacts.
- Scene files can author `convex_hull` and `floating_convex_hull` bodies.
- SkullScope, nudge repro snapshots, and collision/physics visualizers report or
  display hull-specific shape information.
- Added a wedge hull asset plus a focused `convex_hull_collision.scene` fixture
  that exercises sphere/hull, box/hull, and hull/hull contacts.

## Orchestration Notes

Startup contract was completed before editing: `AGENTS.md`, `README.md`,
`Agentic/README.md`, `Agentic/SessionState.md`, and
`git status --short --branch`.

`tools\orchestrator.bat check` passed but reported an unrelated stale active
queue item, `physics-shadow-worker-parallelization`, in `verifying`.
`tools\orchestrator.bat doctor` passed. To avoid mutating someone else's active
queue state, this task was run as an explicit ad hoc orchestrated plan on branch
`codex/convex-hull-collision-implementation-plan`.

The plan is now archived under `Agentic/Plans/Done/`.

## Orchestration Ledger

Local ledger path:

```text
Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\orchestration-ledger.md
```

`Agentic/Runs/` is ignored local run state, so the ledger contents are mirrored
here for committed review.

This is not a formal `tools\orchestrator.bat run-loop` ledger generated from
`orchestration-steps.jsonl`. No formal step log existed for this ad hoc run.
The timeline was reconstructed from:

- `git reflog --date=iso` entries for pull, checkout, and commits;
- `Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\*`
  file `LastWriteTime` values for build, scene, SkullScope, and validation
  outputs;
- elapsed-time lines captured in those command logs;
- verifier/sub-agent responses in the Codex thread.

Minute ranges are rounded and activity labels are inferred from the surrounding
logs and conversation. They should be treated as a retrospective approximation,
not as authoritative state-machine telemetry.

Agent accounting:

```text
External implementation worker agents: 0
Main-agent implementation/orchestration: about 44 minutes from branch checkout to report drafting
Explorer agents: 2 read-only mapping agents before final implementation
Rubber-duck verifier agents: 1 verifier agent, 3 feedback rounds
Verifier duration: about 3 minutes total
Validation duration counted from timed commands: about 252 seconds
Finalization duration: about 3 minutes for plan archive, report commit, and push
```

Retrospective approximate minute ledger:

```text
20:03-20:08  Orchestrator startup, queue/policy check, doctor smoke, branch setup.
20:08-20:25  Main-agent implementation work: hull shape, scene parser/runtime, narrowphase, solver, diagnostics.
20:25-20:29  Initial focused builds, focused scene runs, SkullScope trace/query, and first physics gate.
20:29-20:39  Verifier feedback round and fixes: sphere/hull feature IDs, parser validation, visualizer behavior.
20:39-20:43  Collision visualizer crash fix and Profile/Debug rebuilds.
20:43-20:48  Final trace/query refresh and physics validation round after visualizer fix.
20:48-20:51  Verifier range-check finding, parser range fix, final builds, final focused trace/query, final physics gate.
20:51-20:55  Plan archive, report drafting, implementation commit, report commit, push.
```

Ledger correction note:

```text
This ledger should have been created before the first final response. It was
missing because the task used ad hoc orchestration instead of the formal
tools\orchestrator.bat run-loop path, and the manual report did not include the
mandatory ledger section. That was an agent error, not an orchestrator policy
exception.
```

## Implementation Details

### Hull Shape

`SkullbonezConvexHullShape.*` adds immutable hull data with first-version caps:
64 vertices, 96 faces, 160 unique edges, and 1536 face indices. Load-time
validation rejects malformed coordinates, nonfinite values, float-overflowing
values, duplicate or invalid face indices, degenerate/nonplanar faces, and
nonconvex topology.

Broadphase-facing sweep tests remain conservative by using bounding radii. Hull
inertia uses a deterministic box approximation from the validated extents.

### Narrowphase

`SkullbonezObjectContactManifold.cpp` now builds a fixed-buffer world polytope
view for boxes and hulls. The generic SAT path tests face normals and edge cross
axes, generates clipped face contacts, and falls back to edge/edge contact when
the best axis is an edge axis.

Sphere/hull contacts use deterministic closest-feature selection:

- inside sphere center: closest face;
- outside sphere center: face interior, then edge segment, then vertex
  candidates;
- feature IDs encode face, edge, or vertex kind plus source id.

The hot path uses fixed arrays and no per-frame heap allocation.

### Solver And Diagnostics

`SkullbonezPersistentContactSolver.cpp` widens contact cache packing so hull
feature IDs are not truncated to 16 bits. Hull bodies use world-space inertia
where needed and rolling-friction radius scaling is hull-aware.

`SkullbonezSkullScope.cpp` records `shape:"convex_hull"` and hull names/counts.
`SkullbonezRun.cpp` writes hull details into nudge repro snapshots.
`SkullbonezPhysicsDebugVisualizer.cpp` can draw authored hull wireframes through
the existing debug-line path. `SkullbonezCollisionVisualizer.cpp` renders hulls
as solid validated AABB/inertia box proxies rather than sphere proxies; the
authored-edge view remains in the physics debug overlay.

### Scene Authoring

`SkullbonezTestScene*` and `SkullbonezRunScene.cpp` support:

```text
convex_hull <name> <x> <y> <z> <mass> <restitution> <hull> [eulerX eulerY eulerZ] [velX velY velZ]
floating_convex_hull <name> <x> <y> <z> <mass> <restitution> <hull> [eulerX eulerY eulerZ] [velX velY velZ]
```

The focused fixture is:

```text
SkullbonezData\scenes\convex_hull_collision.scene
SkullbonezData\hulls\wedge.hull
```

## Verification Rounds

Independent verifier round 1 found three issues:

- sphere/hull closest point was too order-dependent;
- hull parser did not reject every malformed/nonfinite/nonplanar input;
- collision visualizer treated hulls as spheres.

All three were fixed. The visualizer fix was revised once more after a Profile
scene run exposed a crash in the first debug-line attempt; the final collision
visualizer path uses solid box-proxy instances, while authored-edge wireframes
stay in `PhysicsDebugVisualizer`.

Independent verifier round 2 found one remaining parser issue: a coordinate such
as `1e39` was finite as `double` but overflowed when narrowed to `float`.
`ParseFiniteFloatOrThrow` now rejects values outside `[-FLT_MAX, FLT_MAX]`
before casting.

Independent verifier round 3 reported:

```text
No blockers remain.
```

## Final Validation

Commands were run through the available Codex shell and mirrored to log files.
No separate visible console window was opened by the automation surface.

Focused final checks after the last code change:

```text
tools\validate_build.bat Profile
PASS: Build Profile|x64 succeeded.
Build succeeded.
    0 Warning(s)
    0 Error(s)
Elapsed: 2.67s
Log: Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\build-profile-final2.log
```

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData\scenes\convex_hull_collision.scene
EXIT_CODE=0
Elapsed: 3.72s
Log: Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\focused-hull-scene-profile-final2.log
```

```text
tools\validate_build.bat Debug
PASS: Build Debug|x64 succeeded.
Build succeeded.
    0 Warning(s)
    0 Error(s)
Elapsed: 2.01s
Log: Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\build-debug-final2.log
```

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData\scenes\convex_hull_collision.scene --physics-diag Debug\convex_hull_collision.physicsdiag.ndjson
EXIT_CODE=0
Elapsed: 3.16s
Log: Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\focused-hull-scene-debug-diag-final2.log
```

Final required gate:

```text
tools\validate_physics.bat
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
PASS: bullet_sweep_wall.csv (2 lines, byte-exact match)
PASS: bullet_sweep_object.csv (2 lines, byte-exact match)
PASS: bullet_sweep_terrain.csv (2 lines, byte-exact match)
PASS: shooting_reaction_volley.csv (641 lines, byte-exact match)
PASS: physics_query_varied.json exact match
VALIDATE_PHYSICS: ALL PASSED
Elapsed: 46.68s
Log: Agentic\Runs\2026-06-17\convex-hull-collision-implementation-plan\validate-physics-final2.log
```

The focused SkullScope contact-type query on the final trace reported:

```text
box/convex_hull: first_frame=0 last_frame=0 rows=4
convex_hull/convex_hull: first_frame=0 last_frame=1 rows=6
sphere/convex_hull: first_frame=0 last_frame=164 rows=6
```

## SkullScope Query Cost

Final trace command:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData\scenes\convex_hull_collision.scene --physics-diag Debug\convex_hull_collision.physicsdiag.ndjson
```

Raw artifacts:

```text
Debug\convex_hull_collision.physicsdiag.ndjson: 1,287,746 bytes
Debug\convex_hull_collision.physicsdiag.sqlite: 757,760 bytes
```

Final query commands read by GPT:

```text
tools\physics_query.bat Debug\convex_hull_collision.physicsdiag.ndjson summary
tools\physics_query.bat Debug\convex_hull_collision.physicsdiag.ndjson contacts --frame 0 --limit 30
tools\physics_query.bat Debug\convex_hull_collision.physicsdiag.ndjson sql "select contact_type,min(frame) as first_frame,max(frame) as last_frame,count(*) as rows from contacts group by contact_type order by contact_type"
tools\physics_query.bat Debug\convex_hull_collision.physicsdiag.ndjson body 5 --frames 0:2 --limit 10
```

Final query output read by GPT:

```text
summary: 2,841 chars
contacts --frame 0 --limit 30: 3,709 chars
contact-type SQL: 587 chars
body 5 --frames 0:2 --limit 10: 2,444 chars
Total final GPT-read SkullScope output: 9,581 chars
```

Successful exploratory query outputs saved in the run folder:

```text
skullscope-body2-frames0-2.txt: 2,231 chars
skullscope-body5-frames0-2.txt: 2,444 chars
skullscope-body5-frames0-2-after-verifier.txt: 2,444 chars
skullscope-body5-frames0-2-final.txt: 2,444 chars
skullscope-body5-frames0-2-final2.txt: 2,444 chars
skullscope-broadphase-frame0.txt: 533 chars
skullscope-contacts-frame0.txt: 3,709 chars
skullscope-contacts-frame0-after-verifier.txt: 3,709 chars
skullscope-contacts-frame0-final.txt: 3,709 chars
skullscope-contacts-frame0-final2.txt: 3,709 chars
skullscope-contacts-frame1.txt: 648 chars
skullscope-contacts-frame2.txt: 652 chars
skullscope-contact-types-sql.txt: 587 chars
skullscope-contact-types-sql-after-verifier.txt: 587 chars
skullscope-contact-types-sql-final.txt: 587 chars
skullscope-contact-types-sql-final2.txt: 587 chars
skullscope-events.txt: 312 chars
skullscope-frame0.txt: 3,973 chars
skullscope-summary.txt: 2,839 chars
skullscope-summary-after-verifier.txt: 2,839 chars
skullscope-summary-final.txt: 2,841 chars
skullscope-summary-final2.txt: 2,841 chars
Total successful SkullScope query output read during the run: 46,669 chars
```

One exploratory unsupported query, `tools\physics_query.bat Debug\convex_hull_collision.physicsdiag.ndjson bodies --frame 1 --limit 20`, returned a CLI error and was not used for conclusions. Narrower supported `body` queries were run before drawing conclusions.

No query output was truncated.

`tools\validate_physics.bat` also ran the SkullScope regression packet through
`tools\check_physics_query_regression.py`; it passed exact baseline comparison
as shown above.

## Timings

Recorded branch work began at `2026-06-17T20:08:04+10:00`. Report drafting began
at `2026-06-17T20:52:07+10:00`. Total measured branch/report time at report
creation: about 44 minutes. The broader startup/pull/orchestration setup began
around `2026-06-17T20:03+10:00`, so total task time at report creation was about
49 minutes.

Sub-run timings:

```text
Initial Profile build: 83.50s
Initial Debug build: 79.89s
Final Profile build: 2.67s
Final focused Profile scene: 3.72s
Final Debug build: 2.01s
Final Debug trace generation: 3.16s
Final SkullScope query set: about 0.54s
Final tools\validate_physics.bat: 46.68s
Verifier round 1: about 1 minute
Verifier round 2/3: about 1 minute each
```

## Residual Risk

The first hull implementation intentionally uses conservative broadphase radius
checks and approximate inertia. Terrain-vs-hull contact is still conservative
rather than a full hull/terrain manifold. Visual collision view uses a solid
box proxy for hulls; exact authored hull edges are available in the physics
debug overlay.

The final required physics gate passed after the last code change.

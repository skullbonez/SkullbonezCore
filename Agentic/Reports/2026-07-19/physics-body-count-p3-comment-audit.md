# Physics Body-Count P3 Comment Audit

Date: 2026-07-19

Plan task: `physics-body-count-scale-campaign` P3

Guide: `Agentic/Reference/comment-style-guide.md`

Skill: `Agentic/Skills/comment-style-audit/skill.md`

Result: 20/20 touched source-bearing files inspected and compliant; 0 deferred.

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/PhysicsScene.cpp` | Pass | The learning header defines the scene/world coordination boundary; its invariants now name every cold authored mutation that invalidates derived awake/grid state, including same-count replacement. |
| `SkullbonezSource/Physics/PhysicsWorld.cpp` | Pass | The header defines the awake-list borrow and parallel flush invariant; nearby cold-boundary and sequencer comments explain topology invalidation, sleeper-resident broadphase membership, and deterministic worker publication barriers. |
| `SkullbonezSource/Physics/PhysicsWorld.h` | Pass | The learning header and declaration comment define awake-list lifetime and the cold mutation boundary without transferring authority out of the sleep/broadphase owners. |
| `SkullbonezSource/Physics/SolverBroadphaseStage.h` | Pass | The header distinguishes geometry-only admission from final sleep-aware admission and explains why Debug retains the former predicate for exact diagnostic semantics. |
| `SkullbonezSource/Physics/SpatialGrid.cpp` | Pass | The learning header and local `Concept`, `Why`, and `Invariant` comments cover persistent membership, pair-source generations, production cell restriction, Debug sleep evidence, fixed dedup storage, and Lane F exhaustion. |
| `SkullbonezSource/Physics/SpatialGrid.h` | Pass | The public contract defines pair-source stamps as frame-local work selection, explicitly separates them from sleeper membership, and documents diagnostic/restricted candidate outputs. |
| `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` | Pass | The header and local comments explain cold radius caching, full seed versus awake maintenance, source-cell traversal, Debug-only canonical reconstruction of the pre-P3 diagnostic stream, production-only work omission, and fixed capacities. |
| `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` | Pass | The learning header defines awake-source cells, dormant membership retention, Debug/production behavior, borrowed span lifetime, and cold topology invalidation. |
| `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp` | Pass | The header defines awake work slots and deterministic mapping; the existing mutual-gravity comments continue to document exact pair/reduction order and capacity policy. |
| `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` | Pass | The public contract states that awake spans are synchronous ascending borrows and that force/integration arithmetic remains model ordered. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp` | Pass | The header and restore code identify replay as a cold dense-index rebuild boundary; the returned awake span remains a synchronous owner-retained view. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp` | Pass | The learning header and local comments define one-winner atomic sleep transition, fixed pending publication, underwater/fixed rejection, sequencer folding, and same-step force application. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` | Pass | The header and nearby comments teach the ascending list/reverse map, cold rebuilds, transition-only updates, Debug drift assertion, deterministic flush, and Lane F queue checks. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` | Pass | The owner contract defines awake indices and the pending queue, explains atomic-ref/plain-storage copy semantics, and keeps worker versus sequencer mutation authority explicit. |
| `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp` | Pass | The header defines awake-slot mapping; implementation names per-body candidate identity and preserves the original worker threshold and serial commit boundary. |
| `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` | Pass | The public contract documents the borrowed ascending awake list, worker-slot mapping, and retained per-model candidate lifetime. |
| `SkullbonezTests/TestPhysicsStageState.cpp` | Pass | The test header and local cold-boundary comment explain sorted-list transition, queued wake flush, and same-count fixed/dynamic rebuild coverage. |
| `SkullbonezTests/TestSolverBroadphaseStage.cpp` | Pass | The test header distinguishes sleep-only rejection from conservative geometry admission and covers the awake-to-sleep keep case. |
| `SkullbonezTests/TestSpatialGrid.cpp` | Pass | The test header defines pair-source restriction without eviction; the focused case explains retained membership across generation changes. |
| `SkullbonezTests/TestTerrain.cpp` | Pass | The test header now states that only supplied awake indices may be tested, with fixed and sleeping candidate slots remaining untouched. |

No file is deferred. Every touched source-bearing file has the required learning
header and nearby ownership, lifetime, determinism, capacity, or concurrency
comments where the P3 behavior is non-obvious.

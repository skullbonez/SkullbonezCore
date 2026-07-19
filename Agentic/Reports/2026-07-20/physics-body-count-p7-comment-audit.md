# Physics Body-Count P7 Comment Audit

Date: 2026-07-20

Scope: final P7 retained source plus the campaign-required `SpatialGrid.h`
reinspection.

Guide: `Agentic/Reference/comment-style-guide.md` via
`Agentic/Skills/comment-style-audit/skill.md`.

## Result

Checked: 9 / 9 source-bearing files. Deferred: 0. Unchecked: 0.

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/PersistentContactSolver.cpp` | checked | The learning header teaches solver rows, support edges, canonical work, fixed scratch, and post-pass side effects. The retained producer call now names the shared fail-before-grow support-edge boundary rather than hiding allocation policy in a raw vector append. |
| `SkullbonezSource/Physics/SleepIslandSystem.cpp` | checked | The header describes support propagation, deterministic traversal, hot body fields, and the fixed support-edge budget; local invariant comments remain adjacent to non-obvious propagation state. |
| `SkullbonezSource/Physics/SleepIslandSystem.h` | checked | The public contract defines the four-edges-per-body budget and explains the Lane F fail-before-grow invariant at the declaration used by every producer. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` | checked | The header and local comments teach sleep ownership, construction-time reserve, phase diagnostics, and the distinction between a semantic cap and actual reserved capacity. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp` | checked | The header teaches replay/state restoration and the restore path documents why support-edge cardinality is validated before vector assignment. |
| `SkullbonezSource/Physics/SpatialGrid.h` | checked | The required vocabulary is current: persistent membership, current swept overlay, pair-source stamps, retained bucket/entry pools, and canonical pair emission replace rebuild/generation-stamping language. Invariants explain that stamps restrict work without mutating sleeper membership. |
| `SkullbonezTests/TestDeterminism.cpp` | checked | The test header now defines the 520-body parallel contact field, distinguishes byte-exact scheduling checks from tolerance properties, and states the worker-count sleep-state invariant. The fixture shape and dispatch-threshold assertions are readable beside the test. |
| `SkullbonezTests/TestPersistentContactSolver.cpp` | checked | The test header teaches direct solver fixtures and support-edge output; the fixed reserve mirrors the production owner precondition without an unexplained allocation. |
| `SkullbonezTests/TestRuntimeContracts.cpp` | checked | The header names Lane F probes and the support-edge owner; the child-process case makes requested capacity, reserve, high-water, and gameplay phase part of the expected diagnostic contract. |

## Audit Judgment

The retained comments describe ownership, ordering, capacity, failure phase,
and test intent rather than restating syntax. No stale generation-rebuild
language remains in `SpatialGrid.h`, and no touched dense or risky code lacks
the nearby invariant needed to understand why its order or capacity check is
required.

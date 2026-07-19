# Physics Body-Count P4 Comment Audit

Date: 2026-07-19

Plan task: `physics-body-count-scale-campaign` P4

Guide: `Agentic/Reference/comment-style-guide.md`

Skill: `Agentic/Skills/comment-style-audit/skill.md`

Result: 6/6 touched source-bearing files inspected and compliant; 0 deferred.

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/PhysicsWorld.cpp` | Pass | The learning header defines the awake-list borrow and cross-stage clock ownership. Nearby `Concept`, cold-boundary, and invariant comments explain logical byte accounting, same-timestep awake-only CCD-clock writes, and one-shot underwater sleeper probes without obscuring the intervening sequenced stages. |
| `SkullbonezSource/Physics/PhysicsWorld.h` | Pass | The owner header defines deterministic cross-stage state, cold topology invalidation, and synchronous awake-list lifetime. Local comments explain why the CCD clock stays on the sequencer and when the underwater census flag may be set. |
| `SkullbonezSource/Physics/SpatialGrid.h` | Pass | The prior P3 learning header already teaches persistent membership, pair-source stamps, canonical output, and fixed-capacity policy. P4 changes are formatting-only inline-comment alignment and preserve those declaration-level contracts. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` | Pass | The learning header defines controller-owned sleep rows and transition-maintained awake indices. Local cold-boundary comments explain one-way body-store mirroring, awake-only eligibility/counter walks, mutation-safe in-place list compaction, and immediate underwater locking on ordinary transitions. |
| `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` | Pass | The public contract states controller authority, fixed-capacity worker publication, and sequencer-only sorted-list mutation. The `MirrorFlagsFrom` comment names its cold-rebuild result and the stage context exposes the borrowed awake span without transferring ownership. |
| `SkullbonezTests/TestPhysicsStageState.cpp` | Pass | The test header freezes sorted awake-list behavior across transitions and cold rebuilds. Focused cases and nearby comments cover steady mirrors versus explicit invalidation, authored underwater sleeper retention, and visiting every row while the live awake list compacts. |

No file is deferred. The inventory was derived from the final touched-source
diff and reconciled against tracked source-bearing paths. Each file has the
required learning header plus nearby ownership, lifetime, determinism, or
mutation-hazard comments where P4 behavior is non-obvious.

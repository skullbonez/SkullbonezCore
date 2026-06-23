# Replay V2 Authoritative State Inventory

Date: 2026-06-23
Branch: `nightrunner-22-june`
Plan: `Agentic/Plans/authoritative-replay-rollback-plan.md`

## Scope

This inventory records the authoritative state currently captured by the replay
v2 checkpoint/hash path. It is not a proposal for deleting legacy replay code.
The old JSON replay/debug exporters remain in place while v2 continues to own
the binary smooth-scrub and saved-restore path.

Primary evidence:

| Area | Source |
|------|--------|
| Solver sample shape | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` |
| Snapshot schema | `SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h` |
| Capture and hash inputs | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` |
| Restore application | `SkullbonezSource/Runtime/Run.cpp` |
| Physics snapshot capture/restore | `SkullbonezSource/Physics/PhysicsWorld.cpp` |
| Saved target event replay | `SkullbonezSource/Runtime/RunFrame.cpp` |
| v2 chunk serialization | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |

## Checkpoint Decision Table

| State owner | Checkpoint decision | Hash decision | Current restore behavior |
|-------------|---------------------|---------------|--------------------------|
| Replay timeline metadata | Serialize `frameIndex`, branch provenance, event cursor, scene frame, simulation seconds, fixed dt, and checkpoint boundary on `ReplaySolverFrameSample`. | Solver hash rows store the hash beside frame metadata; frame identity itself is comparison metadata rather than solver state. | Saved restore chooses the nearest checkpoint before the target, uses the checkpoint event cursor, and steps fixed ticks to the target hash. |
| World/runtime presentation scalars | Serialize gravity, fluid height/density, fixed-step flag, scene physics/text flags, water-hidden, and terrain-hidden in `ReplayWorldPresentationSample`. | `HashWorld` feeds these values into presentation and solver hashes. | Restore writes the world/environment and scene flags before hash verification. |
| Camera | Serialize eye/view/up in the solver sample. | Not authoritative solver state; used for scrub presentation. | Restore cancels camera tween and reapplies camera pose for visual continuity. |
| Launcher control state | Serialize launcher fire mode, ray visualization toggle, impulse strength, projectile speed, active ray lines, laser shots, and cursors. | Launcher control fields are included in the solver hash; transient ray/laser visual age is intentionally excluded. | Restore reapplies launcher mode/config and laser shot state; deterministic firing is replayed through `LauncherFire` events. |
| Body identity and topology | Serialize replay body id, model index, name, and shape kind in each solver body. | Included in body hash; model count also feeds the hash. | Restore requires saved body ids to match live topology, trims live models when valid, and can rebuild generated topology from a saved generated-scene-config event before applying a mismatched checkpoint. |
| Body transforms and velocities | Serialize position, orientation, linear velocity, and angular velocity. | Included in presentation and solver body hashes. | Restore applies transform/velocity and clears impulse force before world snapshot restore. |
| Body physical constants | Serialize mass, inverse mass, rotational inertia, inverse rotational inertia, and fixed flag. | Mass/fixed feed presentation hash; inverse mass and inertia feed solver hash. | Restore currently reapplies fixed flag plus pose/velocity; inertia/mass are validated by the hash and depend on matching topology/shape construction. |
| Body sleep/contact summary | Serialize sleeping, sleep-supported, sleep-inhibited, collision-contact flag, visual island id, contact count, max penetration, and normal impulse sum. | Included in presentation and solver body hashes. | The detailed sleep/contact owner state below is restored from `ReplaySolverWorldSnapshot`. |
| Physics timing buffers | Serialize per-body `timeRemaining`. | Included in `HashSolverWorldSnapshot`. | Restored directly into `PhysicsWorld`. |
| Sleep system | Serialize global sleep-enabled flag, next visual island id, sleep-supported/inhibited arrays, sleep state, sleep counters, underwater sleep locks, sleep support edges, island parent/rank arrays, island awake/support/eligible/can-sleep flags, assigned visual ids, and current visual ids. | Included in `HashSolverWorldSnapshot`. | Restored directly; derived narrowphase/island scratch arrays are cleared for deterministic rebuild. |
| Tornado force field | Serialize tornado config plus per-body capture/eject cooldown timers. | Included in `HashSolverWorldSnapshot`. | Restored directly and mirrored back to runtime settings. |
| Persistent contacts | Serialize body pair, feature id, key, normal/tangents, contact offsets, penetration, masses, bias, friction limit, accumulated impulses, warm-start flag, terrain/resting/friction policy flags, manifold point count, terrain normal, and terrain warm-start. | Every field is included in `HashPersistentContact`. | Restored into `m_persistentContacts` for warm-start continuity. |
| Persistent contact cache | Serialize cache key and accumulated normal/tangent impulses. | Included in `HashContactCache`. | Restored into `m_persistentContactCache`. |
| Solver stats | Serialize persistent-contact solver row/cache/warm-start/position-correction counters. | Included in `HashSolverStats`. | Restored into `m_persistentContactSolverStats`. |
| Debug contacts and pipeline trace | Serialize current debug contacts and physics pipeline records. | Included in solver snapshot hash today. | Restored for diagnostics and query correlation. If this becomes too expensive, it needs a deliberate hash/schema decision before removal. |
| Collision broadphase | Serialize collision cell keys for hash/query evidence. | Included in `HashSolverWorldSnapshot`. | Live spatial grid, candidate pairs, terrain manifolds, narrowphase scratch, and point-joint scratch are cleared and rebuilt on the next physics step. |
| Point joints | Not serialized in the current replay checkpoint schema. | Not included. | Current validated v2 scenes do not depend on point-joint constraints; adding joint-authoring replay requires a new checkpoint/event field. |
| Runtime event stream | Serialize bounded `EVNT` rows plus `ECUR` checkpoint cursors. | Event cursor is checkpoint metadata; event effects are proved by stepped target solver hashes. | Saved restore replays generated-scene config validation, world overrides, editor placement, editor transform, launcher config, and launcher fire between checkpoint and target. Runtime scene-load/create/advance/reset commands are checkpoint barriers, not in-window replay actions. |
| Generated-scene random/topology state | Serialize generated scene counts, override flags, and seed in `GeneratedSceneConfig` events; checkpoints serialize the resulting body topology. | Resulting topology and body state are included in solver hashes. | If live topology mismatches a saved generated checkpoint, restore rebuilds deterministic topology from the latest saved generated-scene-config event before applying the checkpoint. |
| Scene load/create/advance commands | Treat as whole-timeline transitions. | Queryable as `RuntimeCommand` events when captured; not solver-hash inputs by themselves. | A checkpoint after the transition is restorable. A transition inside the checkpoint-to-target window is rejected with an explicit restore failure instead of silently rebuilding the runtime mid-window. |

## Hash Contract

The current solver hash is built from:

| Hash component | Evidence |
|----------------|----------|
| World scalars and runtime flags | `HashWorld` in `ReplayRecorder.cpp` |
| Model count, contact count, pipeline record count | `ReplaySolverRecorder::CaptureFrame` |
| Launcher control state | `HashLauncherControlState` before body hashing |
| Solver world snapshot | `HashSolverWorldSnapshot` |
| Body presentation fields | `HashSolverBodyPresentationFields` |
| Body solver constants | `HashSolverBodySample` |
| Persistent contacts/cache/stats/debug/pipeline/collision keys | `HashSolverWorldSnapshot` |

The hash intentionally does not include purely visual ray/laser ages or camera
pose as authoritative physics state. The saved hash rows compare solver hashes
after checkpoint application and every stepped fixed tick through the restore
target.

## Known Boundaries

| Boundary | Current decision |
|----------|------------------|
| Legacy replay files | Keep v1 JSON presentation/solver debug exports until v2 fully replaces the supported save/load/debug workflow. |
| Runtime scene transitions | Record/query them, but treat scene load/create/advance/reset as checkpoint barriers. Replaying them inside a restore window remains rejected by design. |
| Point-joint constraints | Not covered by current replay v2 validation scenes. Add schema/event coverage before claiming replay support for authored joint scenarios. |
| Compression/delta encoding | Out of scope until uncompressed binary v2 restore/query evidence stays stable. |
| Broadphase internals | Rebuilt after restore; collision cell keys are retained as hash/query evidence. Serialize more only if rebuild order proves nondeterministic. |

## Acceptance Status

| Phase 1 acceptance item | Evidence |
|-------------------------|----------|
| Every solver-affecting owner has an explicit checkpoint decision. | The table above names the current serialize/rebuild/query-boundary decision for runtime, body, physics world, contact, sleep, broadphase, event, launcher, and scene-transition state. |
| Hash changes when authoritative body/contact/sleep state changes. | Body physical fields, persistent contacts/cache, sleep arrays/islands, tornado state, solver stats, debug contacts, pipeline trace, and collision cell keys feed the solver hash in `ReplayRecorder.cpp`. |
| Validation selection is named. | Code/tool changes touching replay v2 validation require `tools\validate_fast.bat` and `tools\validate_replay_v2_artifact.bat`; runtime source changes still require `tools\validate_full.bat`. |

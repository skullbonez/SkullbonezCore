# Physics Settings Snapshot

Status: Registered — 0/4 tasks (C0-C3)
Owner: repository owner; registered 2026-07-20 as campaign plan 3 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding B)
Ledger: C0-C3
Depends on: `physics-facade-unification` (the stamp point is the surviving
PhysicsEngine); `dependency-direction-restoration` recommended first.

## Objective

Cut the one `EngineConfig` edge that matters: physics solver stages must not
see the Core process-config type. A physics-owned settings value record is
stamped once at `ApplyRuntimeConfig` time and threaded through
`RunPhysics`/stage contexts, replacing `const EngineConfig&` in every
fixed-step signature. Byte-exact baselines are the faithfulness oracle.

## Problem / Evidence

`Core/Config.h` is 595 lines / ~266 fields / 75 includers. It is threaded by
reference into the hot path: `PhysicsWorld::RunPhysics` takes
`const EngineConfig&`; solver code reads
`config.persistentContactSolver.iterations` and
`config.terrainContact.maxBaumgarteBias` at solve time
(`PersistentContactSolver.cpp:165-193`); eleven stage headers name
`EngineConfig`. The contact solver can currently see skybox texture paths.

## Non-Goals

- No decomposition of `EngineConfig` for renderer, UI, camera, or window
  consumers — this plan cuts the physics edge only.
- No tuning-value change of any kind: every snapshot field carries the exact
  value the same code read from `EngineConfig` before. Byte-exact CSV is the
  proof; a mismatch is a defect, never a baseline refresh.
- No engine.cfg schema change, no config format version bump.
- The snapshot is a domain value record, not a `*Runtime`/`*Compatibility`
  bag: fields group by owner (solver, sleep, broadphase, forces, terrain
  contact, fluid) per the Migration Cleanup Review Rule's domain-noun
  requirement.

## Binding Decisions

1. The record is named `PhysicsRuntimeSettings` (composed of existing or new
   domain sub-values such as `ContactSolverSettings`, `SleepSettings`,
   `BroadphaseSettings`, `FluidForceSettings`, `TerrainContactSettings`), is
   owned by `Physics/`, and includes nothing from `Core/Config.h`.
2. It is stamped exactly once per config application in
   `PhysicsEngine::ApplyRuntimeConfig` (and re-stamped on any existing
   runtime-config reapply path); fixed-step code never consults
   `EngineConfig`.
3. Field-by-field provenance is recorded: each snapshot field names the
   `EngineConfig` field it copies. The C2 unit test enforces the mapping.
4. Value semantics only; no pointers back into config storage.

## Tasks

- [ ] C0 — Field inventory: enumerate every `EngineConfig` read inside
  `Physics/` (world, stages, solver, sleep, buoyancy, tornado-until-extracted,
  spatial grid), grouped by proposed owner sub-value, with the exact
  config-field provenance table. Output: table committed into this plan.
  No validation (documentation).
- [ ] C1 — Introduce `PhysicsRuntimeSettings` + sub-values, stamp in
  `ApplyRuntimeConfig`, thread through `RunPhysics`, stage contexts
  (`PhysicsStageContexts.h`), and solver entry points; delete every
  `EngineConfig` include/parameter from `Physics/` fixed-step code. Proof:
  `grep -rn "EngineConfig" SkullbonezSource/Physics` returns only the
  cold `ApplyRuntimeConfig`/authoring stamp edges (target: PhysicsEngine
  only). Validation: `tools\validate_physics.bat` (byte-exact) and
  `tools\validate_perf.bat` (hot-path signature change).
- [ ] C2 — Faithfulness tests: unit coverage asserting the stamped snapshot
  equals the source config field-for-field (including defaults and clamp
  behavior such as the existing `max(1, iterations)` / `max(0, slop)`
  guards, which must move or be provably duplicated, not silently doubled).
  Validation: `tools\validate_tests.bat`; add `validate_coverage` if these
  tests are intended to raise the physics-stores/stages floor.
- [ ] C3 — Closure: independent rubber-duck review (bag-shape check: the
  record must not have become a catch-all runtime bag; clamp/verbatim
  semantics preserved), final gates, provenance table reconciled against
  final source. Validation: `tools\validate_physics.bat`,
  `tools\validate_perf.bat`, `tools\validate_full.bat` at closure tip.

## Acceptance

- No fixed-step physics code names `EngineConfig`; the C1 grep proof holds
  at closure tip with only the recorded cold stamp edges.
- Physics regression CSV byte-exact; perf gate passes with no regression
  outside normal noise (record the numbers).
- C2 tests pass and pin the field mapping.
- Independent review clear on domain-noun grouping and clamp preservation.

## Validation Summary

C1: `validate_physics` + `validate_perf`. C2: `validate_tests`
(+ `validate_coverage` if floor-raising). C3: `validate_physics` +
`validate_perf` + `validate_full` at final source.

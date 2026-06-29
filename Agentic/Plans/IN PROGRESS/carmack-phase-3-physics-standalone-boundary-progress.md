# Carmack Phase 3 - Physics Standalone Boundary Progress

## Source Plan

- Source of truth: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`
- Assigned scope: `Phase 3 - Physics Standalone Boundary`
- Historical context only: `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`
- Current review context: `Agentic/Reports/2026-06-29/carmack-handoff/physics-boundary-rubber-duck-review.md`

## Current Status

Phase 3 selected the explicit compatibility-bridge contract instead of forcing
strict standalone step ownership in this slice.

- Runtime stepping may continue through `PhysicsScene::RunPhysics(PhysicsModelAccess&, float)`, but the bridge is now documented as the named pose/state sync boundary, not hidden standalone solver ownership.
- `PhysicsScene::RunPhysics()` documents the load-solve-writeback invariant beside the bridge calls in `SkullbonezSource/Physics/PhysicsScene.cpp`.
- Runtime handle mirror smoke now mutates same-count collider authoring data after the first `GetColliderStore()` call and verifies that a subsequent accessor refresh exposes changed shape, restitution, radius, surface/material data, and stable handles.
- `RunPhysicsStandaloneSmoke()` did not need lifecycle changes; the existing standalone API smoke plus the runtime mirror ratchet covers the open Phase 3 gap.
- Final Phase 3 validation passed through `tools\validate_physics.bat` and the broader `Runtime/Init.cpp` mapping gate in `tools\validate_full.bat`.

## Action Checklist

- [x] Record the final boundary decision in this progress file and, when implemented, in the authoritative plan evidence field: strict `PhysicsBodyStore`/`PhysicsScene` step authority vs accepted `PhysicsModelAccess` compatibility bridge.
- [x] If strict standalone authority is selected, remove hot-step `m_bodyStore.LoadFromModelAccess(...)` and `m_bodyStore.WriteBackToModelAccess(...)` from `SkullbonezSource/Physics/PhysicsScene.cpp`; replace with explicit pre-step import and post-step export calls at named runtime boundary points. Not applicable: compatibility bridge selected for this slice.
- [x] If strict standalone authority is selected, change `PhysicsWorld::RunPhysics`, `PhysicsWorld::RunSolverPhysics`, `PersistentContactSolver::Solve`, `Ragdoll::SolvePointJoints`, `SleepIslandSystem::PropagateSupport`, and diagnostics helpers to consume store-owned pose/body/collider data instead of normal-path `PhysicsModelAccess` reads. Not applicable: compatibility bridge selected for this slice.
- [x] If the compatibility bridge is accepted, add a clear invariant comment near `PhysicsScene::RunPhysics` in `SkullbonezSource/Physics/PhysicsScene.cpp` explaining that `PhysicsModelAccess` is the explicit pose sync bridge, not standalone solver ownership.
- [x] If the compatibility bridge is accepted, ratchet `tools/check_runtime_boundaries.py` so new normal-path physics dependencies cannot bypass `PhysicsModelAccess`, stores, stable handles, or a named adapter. Not applicable in this slice: the existing boundary checker already passed with zero errors after Phase 2; no guardrail source changed for Phase 3.
- [x] Extend `RunPhysicsRuntimeHandleSmokeSample()` in `SkullbonezSource/Runtime/Init.cpp` to mutate a same-count collider authoring property after the first `GetColliderStore()` call, then verify `GetColliderStore()` refreshes shape, restitution, broadphase radius, and material data.
- [x] Extend `RunPhysicsStandaloneSmoke()` in `SkullbonezSource/Physics/PhysicsApi.cpp` only if standalone API lifecycle gaps remain after the runtime mirror extension; preserve the deterministic hash expectation when adding checks. Not needed: runtime mirror extension covered the open collider freshness gap without changing standalone hash logic.
- [x] Regenerate and record standalone smoke output using `Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke --physics-standalone-smoke-log TestOutput\validation\agent_logs\carmack_phase3_physics_standalone_smoke.log`.
- [x] Run and record `tools\check_runtime_boundaries.py` after any guardrail change. Not applicable: no guardrail file changed in Phase 3.
- [x] Record final physics validation evidence from `tools\validate_physics.bat` before marking Phase 3 done.

## Likely Files And Tools To Inspect

- `SkullbonezSource/Physics/PhysicsScene.cpp`
- `SkullbonezSource/Physics/PhysicsScene.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- `SkullbonezSource/Physics/PhysicsBodyStore.h`
- `SkullbonezSource/Physics/ColliderStore.cpp`
- `SkullbonezSource/Physics/ColliderStore.h`
- `SkullbonezSource/Physics/PhysicsModelAccess.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/Ragdoll.cpp`
- `SkullbonezSource/Physics/SleepIslandSystem.cpp`
- `SkullbonezSource/Physics/PhysicsApi.cpp`
- `SkullbonezSource/Physics/PhysicsApi.h`
- `SkullbonezSource/Runtime/Init.cpp`
- `tools/check_runtime_boundaries.py`
- `tools/validate_physics.bat`
- `tools/validate_full.bat`

## Dependencies

- Phase 0 evidence reconciliation should ideally refresh boundary classifications before final closure, but Phase 3 can still produce focused smoke and guardrail work independently.
- Phase 1 perf closure is not a prerequisite unless the chosen Phase 3 implementation changes hot-path allocations or solver timing.
- Any source-bearing edit must follow `Agentic/Reference/comment-style-guide.md` and run `Agentic/Skills/comment-style-audit/skill.md` over touched source before handoff.
- Keep dirty worktree files user-owned; this progress file was created without touching existing files.

## Evidence To Collect

- Boundary decision: accepted `PhysicsModelAccess` as the explicit compatibility
  pose/state sync bridge for the runtime path in this slice. Strict store-owned
  stepping is intentionally not forced here because it would widen into solver,
  sleep, joint, diagnostics, replay, editor, and render mirror behavior.
- Source evidence: `SkullbonezSource/Physics/PhysicsScene.cpp` documents the
  `PhysicsScene::RunPhysics()` load-solve-writeback invariant beside the bridge
  calls.
- Source evidence: `SkullbonezSource/Runtime/Init.cpp` extends
  `RunPhysicsRuntimeHandleSmokeSample()` with a same-count collider authoring
  mutation and checks refreshed shape, restitution, bounding radius, surface
  data, drag/material data, stable collider handle, stable body handle, and
  unchanged collider count.
- Focused build evidence:
  `TestOutput/validation/agent_logs/carmack_phase3_debug_build_after_smoke_edit.log`
  reports `PASS: Build Debug|x64 succeeded.`
- Focused smoke evidence:
  `TestOutput/validation/agent_logs/carmack_phase3_physics_standalone_smoke_console.log`
  reports `lifecycle_checks=pass`, `runtime_mirror_checks=pass`,
  `store_handles=pass`, `render_mirror=pass`, `joint_handles=pass`,
  `collider_refresh=pass`, and
  `PASS: standalone physics and runtime handle mirror smoke matched expected state.`
- Guardrail evidence: no Phase 3 guardrail file changed; the full Phase 3 gate
  reran runtime-boundary validation and reported 0 errors.
- Comment-audit evidence: inspected `SkullbonezSource/Physics/PhysicsScene.cpp`
  and `SkullbonezSource/Runtime/Init.cpp` against
  `Agentic/Skills/comment-style-audit/skill.md`; 2 checked, 0 deferred. The
  touched code has learning headers and local `Invariant:` comments for the
  bridge and collider-refresh contract.
- Final physics gate:
  `TestOutput/validation/agent_logs/carmack_phase3_validate_physics.log`
  reports `collider_refresh=pass`,
  `PASS: physics_regression_solver.csv (20001 lines, byte-exact match)`,
  `VALIDATE_PHYSICS: ALL PASSED`, and `PHASE3_VALIDATE_PHYSICS_EXIT=0`;
  elapsed 24.17s.
- Broad runtime gate:
  `TestOutput/validation/agent_logs/carmack_phase3_validate_full.log` reports
  `DX12 validation errors: 0`, `PASS: DX12 screenshots match committed baselines`,
  `VALIDATE_PHYSICS: ALL PASSED`, `VALIDATE_FULL: DEFAULT GATE PASSED`, and
  `PHASE3_VALIDATE_FULL_EXIT=0`; elapsed 29.34s.

## Validation Note

This phase now includes source changes. Required commit-gate validation is
`tools\validate_physics.bat` for the physics boundary and `tools\validate_full.bat`
because `SkullbonezSource/Runtime/Init.cpp` falls under the `Init*` mapping.

## Open Risks And Questions

- Strict store-owned stepping remains deferred. If revisited later, it needs an
  explicit runtime import/export design for replay restore, editor transforms,
  scene load, diagnostics, and render mirrors.
- `PersistentContactSolver` still receives `PhysicsModelAccess`; narrowing that
  remains the largest behavior-risk slice because it touches contact resolution,
  sleep support, diagnostics, and deterministic baselines.
- Guardrails currently allow the `PhysicsModelAccess` bridge. Future guardrail
  tightening should happen with a dedicated solver-ownership migration, not as a
  hidden side effect of this smoke ratchet.

# Carmack Phase 3 - Physics Standalone Boundary Progress

## Source Plan

- Source of truth: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`
- Assigned scope: `Phase 3 - Physics Standalone Boundary`
- Historical context only: `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`
- Current review context: `Agentic/Reports/2026-06-29/carmack-handoff/physics-boundary-rubber-duck-review.md`

## Current Status

Phase 3 is not closed. Current code has two parallel truths:

- Runtime stepping still uses the compatibility bridge: `PhysicsScene::RunPhysics(PhysicsModelAccess&, float)` reloads `PhysicsBodyStore` from `PhysicsModelAccess`, refreshes `ColliderStore` only when model count changes, runs `PhysicsWorld`, then writes body state back to `PhysicsModelAccess` in `SkullbonezSource/Physics/PhysicsScene.cpp`.
- A model-free public API already exists in `SkullbonezSource/Physics/PhysicsApi.cpp` through `PhysicsStandaloneWorld` and `RunPhysicsStandaloneSmoke()`.
- Runtime handle mirror smoke exists in `SkullbonezSource/Runtime/Init.cpp` through `RunPhysicsRuntimeHandleSmokeSample()` and `--physics-standalone-smoke`.
- `tools/validate_physics.bat` runs `Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke` before the deterministic scene regression.
- Rubber-duck review found no blocker, but called out `PersistentContactSolver` still reading `GameModel` pose/transform state through `PhysicsModelAccess`.

## Action Checklist

- [ ] Record the final boundary decision in this progress file and, when implemented, in the authoritative plan evidence field: strict `PhysicsBodyStore`/`PhysicsScene` step authority vs accepted `PhysicsModelAccess` compatibility bridge.
- [ ] If strict standalone authority is selected, remove hot-step `m_bodyStore.LoadFromModelAccess(...)` and `m_bodyStore.WriteBackToModelAccess(...)` from `SkullbonezSource/Physics/PhysicsScene.cpp`; replace with explicit pre-step import and post-step export calls at named runtime boundary points.
- [ ] If strict standalone authority is selected, change `PhysicsWorld::RunPhysics`, `PhysicsWorld::RunSolverPhysics`, `PersistentContactSolver::Solve`, `Ragdoll::SolvePointJoints`, `SleepIslandSystem::PropagateSupport`, and diagnostics helpers to consume store-owned pose/body/collider data instead of normal-path `PhysicsModelAccess` reads.
- [ ] If the compatibility bridge is accepted, add a clear invariant comment near `PhysicsScene::RunPhysics` in `SkullbonezSource/Physics/PhysicsScene.cpp` explaining that `PhysicsModelAccess` is the explicit pose sync bridge, not standalone solver ownership.
- [ ] If the compatibility bridge is accepted, ratchet `tools/check_runtime_boundaries.py` so new normal-path physics dependencies cannot bypass `PhysicsModelAccess`, stores, stable handles, or a named adapter.
- [ ] Extend `RunPhysicsRuntimeHandleSmokeSample()` in `SkullbonezSource/Runtime/Init.cpp` to mutate a same-count collider authoring property after the first `GetColliderStore()` call, then verify `GetColliderStore()` refreshes shape, restitution, broadphase radius, and material data.
- [ ] Extend `RunPhysicsStandaloneSmoke()` in `SkullbonezSource/Physics/PhysicsApi.cpp` only if standalone API lifecycle gaps remain after the runtime mirror extension; preserve the deterministic hash expectation when adding checks.
- [ ] Regenerate and record standalone smoke output using `Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke --physics-standalone-smoke-log TestOutput\validation\agent_logs\carmack_phase3_physics_standalone_smoke.log`.
- [ ] Run and record `tools\check_runtime_boundaries.py` after any guardrail change.
- [ ] Record final physics validation evidence from `tools\validate_physics.bat` before marking Phase 3 done.

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

- Boundary decision note with the accepted invariant and exact source lines changed.
- `Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke --physics-standalone-smoke-log ...` output showing `lifecycle_checks=pass`, `runtime_mirror_checks=pass`, `store_handles=pass`, `render_mirror=pass`, and `joint_handles=pass`.
- Smoke evidence for same-count collider freshness: changed shape/restitution/material/broadphase data must appear in `ColliderStore` without relying on model-count changes.
- `tools\check_runtime_boundaries.py` output if guardrails are changed.
- `tools\validate_physics.bat` output for final physics boundary changes.
- `tools\validate_full.bat` output only when the broader Carmack branch is ready for PR/commit handoff.

## Validation Note

This progress document is documentation-only, so no repository validation is required for creating it. Future Phase 3 source changes require `tools\validate_physics.bat` at the PR/commit gate; run `tools\validate_full.bat` only for final broad Carmack handoff or if the implementation scope becomes broad.

## Open Risks And Questions

- Is the project willing to accept `PhysicsModelAccess` as the named compatibility bridge for pose sync, or is Phase 3 expected to force full store-owned stepping now?
- If strict standalone authority is required, where should explicit runtime import/export calls live so replay restore, editor transforms, scene load, and diagnostics all sync intentionally?
- The current collider refresh in `PhysicsScene::RunPhysics` only detects model-count changes. Same-count authoring edits can leave stale collider records unless all edit paths call `RefreshColliderStore()` or the store gains a sharper dirty/version invariant.
- `PersistentContactSolver` still receives `PhysicsModelAccess`; narrowing that may be the largest behavior-risk slice because it touches contact resolution, sleep support, diagnostics, and deterministic baselines.
- Guardrails currently allow `PhysicsModelAccess` compatibility. Tightening them too far before the boundary decision could block legitimate bridge code; leaving them broad could let new model-backed solver authority creep back in.

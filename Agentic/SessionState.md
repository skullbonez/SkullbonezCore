# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-3rd-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Implement `Agentic/Plans/To_Eval/contrived-migration-artifact-removal-plan.md` from the committed kill-list CSV. |
| Last documentation milestone | Added implementation status at `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`. |
| Last source milestone | K004 store-force slice is implemented, K003 pose integration now advances `PhysicsBodyRecord` state inside `PhysicsBodyStore`, `GameModel::UpdatePosition` is deleted, tornado field no longer borrows `modelAccess.Models()`, PersistentContactSolver no longer receives `PhysicsModelAccess` or opens raw model ranges, and K005 allocator-owned body/collider handles are implemented through `PhysicsBodyStore` and `ColliderStore`. |
| Pending work | K003 still has PhysicsWorld wake-persistent/object sweep, terrain/manifold, final solver writeback/model ranges, and PhysicsScene bridge ranges. K005 is done in the tracker; no `MakeCompatibility*Handle`, compatibility generation, `SetCompatibilityBodies`, or no-store point-joint index helper hits remain. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | `tools\validate_physics_deep.bat` currently fails on `physics_known_stacking.csv` even with the K003 diagnostics patch reversed; do not attribute that failure to the current K003 slices without rechecking. |
| Validation | Pending PersistentContactSolver view slice touched-source comment audit inspected `ObjectContactManifold.cpp`, `ObjectContactManifold.h`, `PersistentContactSolver.cpp`, `PersistentContactSolver.h`, `PhysicsWorld.cpp`, and `PhysicsWorld.h` with 0 deferred files; it passed `tools\validate_build.bat Profile` with log `TestOutput\agent_build_profile_persistent_solver_view.log`, `python tools\check_runtime_boundaries.py --repo .` with log `TestOutput\agent_runtime_boundaries_persistent_solver_view.log`, and `tools\validate_physics.bat` with log `TestOutput\agent_validate_physics_persistent_solver_view.log`; physics gate built Debug/Profile at 0 warnings and 0 errors, matched standalone/runtime handle mirror smoke, and produced a byte-exact `physics_regression_solver.csv`. Commit `78533f94` touched-source comment audit inspected `GameModelCollection.cpp`, `GameModelCollection.h`, `PhysicsModelAccess.h`, and `PhysicsWorld.cpp` with 0 deferred files; it passed `tools\validate_build.bat Profile` with log `TestOutput\agent_build_profile_tornado_sync.log`, `python tools\check_runtime_boundaries.py --repo .` with log `TestOutput\agent_runtime_boundaries_tornado_sync.log` and 0 errors, and `tools\validate_physics.bat` with log `TestOutput\agent_validate_physics_tornado_sync.log`; physics gate built Debug/Profile at 0 warnings and 0 errors, matched standalone/runtime handle mirror smoke, and produced a byte-exact `physics_regression_solver.csv`. Commit `a20329cf` touched-source comment audit inspected `GameModel.cpp`, `GameModel.h`, `PhysicsBodyStore.cpp`, `PhysicsBodyStore.h`, and `PhysicsWorld.cpp` with 0 deferred files; it passed `tools\validate_build.bat Profile`, runtime-boundary checks, and `tools\validate_physics.bat`. Commit `abe3f4bf` passed build, runtime-boundary, physics, and full gates. Earlier store-force and wake/ragdoll slices passed their targeted build, runtime-boundary, and physics gates. Deep physics known-issue signature mismatch is pre-existing on this branch. |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md` unless the user asks to bypass it.
- The retired `Agentic/Orchestrator` JSON/Python path should not be revived
  unless explicitly requested. Use the orchestrator skill for plan work.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you
  launched.
- Time user-requested work and report elapsed wall-clock time in the final
  answer or handoff.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Contrived migration artifact cleanup | Active implementation | Plan: `Agentic/Plans/To_Eval/contrived-migration-artifact-removal-plan.md`; kill list: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`; implementation status: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`. |
| Missed plan items index | Active reference | `Agentic/Plans/missed_plan_items.md` summarizes actionable leftovers from mostly completed Done plans. |
| Runtime interaction state-machine hardening | Active plan | `Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md`; avoid while another agent is editing UI/replay/camera/input code. |
| Render graph / backend interface continuation | Active plan | `Agentic/Plans/render-graph-irender-interface-plan.md`; barrier migration is done, but graph callbacks/transients/capability cleanup continue in focused slices. |
| Global service context cleanup | Active plan | `Agentic/Plans/global-service-context-plan.md`; Carmack plans are complete and only provide evidence for current compatibility debt. |
| Game model data boundary | Active follow-up | `Agentic/Plans/game-model-data-boundary-plan.md`; the physics step boundary is narrowed, but strict store authority is future work. |
| Runtime static allocation policy | Draft active plan | `Agentic/Plans/runtime-static-allocation-policy-plan.md`; implementation touches hot runtime paths and needs perf-aware validation. |
| Architecture pass follow-up | Active reference | `Agentic/Plans/architecture_pass_2026-06-02.md` remains the broad checkpoint for runtime, physics data, assets, parser, water, and render graph boundaries. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Mitigated but not fully solved; see `Agentic/Plans/missed_plan_items.md` item 5 before starting new water work. |

Additional bug notes live in `Agentic/Bugs.md`.

## Validation Map

Use `AGENTS.md` as the source of truth. These are targeted pre-commit/PR gates,
not routine iteration steps.

| Change | Validation |
|--------|------------|
| Documentation-only | No validation required |
| Small non-render code refactor | `tools\validate_fast.bat` |
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_dx12_renderer.bat` |
| DX12 renderer gate or validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` |
| Performance-sensitive hot path | `tools\validate_perf.bat` |
| General DX12 graphics stress or memory-growth investigation | `tools\run_graphics_stress.bat 1` for a bounded probe; `overnight` only when intentionally soaking |
| Broad or uncertain scope | `tools\validate_full.bat` |

## Key Paths

| Purpose | Path |
|---------|------|
| Source | `SkullbonezSource/` |
| Scenes | `SkullbonezData/scenes/` |
| Shaders | `SkullbonezData/shaders/` |
| Baselines | `TestOutput/baselines/` |
| Validation scripts | `tools/` |
| Runtime reference | `Agentic/Reference/runtime-reference.md` |
| Physics overview | `Agentic/Reference/physics-overview.md` |

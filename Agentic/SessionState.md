# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/physics-store-authority` in worktree `C:\SkullbonezCore`. |
| Active objective | Finish the physics/GameModel authority migration with store-owned physics state, handle-keyed commands, deterministic standalone views, and guardrails against contrived migration artifacts. |
| Last documentation milestone | Completed and moved the contact-audio perceptual model plan to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`. |
| Last source/data milestone | Runtime scene setup, editor placement/gizmo wake-sleep, mouse-pickup impulse, launcher laser/projectile wake, replay velocity edit, replay restore/prediction, replay/editor transform restore wake, ragdoll start-asleep commands, fixed-tree release wake, tornado fixed-tree release, store-owned sleep seeding, store-owned wake propagation, dead `PhysicsWorld` model-stream wake/seed deletion, regression CSV diagnostics row building, and SkullScope frame emission now avoid `GameModelCollection` model-index physics wrappers or model-stream rebuilds at their migrated boundaries. The obsolete `GameModelCollection::WakeModel`, `SeedModelAsleep`, `ApplyBodyImpulse`, `SetPendingBodyImpulse`, full diagnostics-record bridge, and diagnostics-view bridge were deleted. Contact-highlight ticking, fixed-contact highlight notification, persistent-contact fixed-tree release, tornado release writeback/reload, store wake/seed invalidation, post-step model stream invalidation, Debug diagnostics emission, and bulk compatibility writeback moved out of `PhysicsWorld::RunPhysics`/side-effect hot paths toward `PhysicsBodyStore` plus the `PhysicsScene` compatibility edge. `PhysicsBodyStore` now owns fixed-tree release-group metadata and attached fixed-tree release application for solver/tornado step paths; `PhysicsWorld::RunPhysics`, `RunSolverPhysics`, and `ApplyTornadoField` no longer take `PhysicsModelAccess`. `PhysicsEngine::Step` and `PhysicsScene::RunPhysics` no longer take `PhysicsModelAccess`; `GameModelCollection::RunPhysics` owns the remaining model-side topology repair, presentation feedback, diagnostics names, bulk writeback, and model-stream invalidation around the store-owned step. `SimulationSystem` now returns committed tick counts only, and runtime/replay step callers execute those ticks through the collection-owned step instead of `SimulationPhysicsStep`. `PhysicsModelAccess` now exposes only model-owned refresh/import methods; the old body-stream, writeback, invalidation, presentation-feedback, diagnostics-name, `Count`, and `size` facade surface was deleted. Object rendering, shadow object bounds/caster batching, and DXR matrix upload now consume physics-backed `RenderInstanceStore` records instead of GameModel render/body streams or per-pass GameModel matrix recomputation. Read-only body-store access now repairs topology only on count drift, and object-follow camera focus plus UI scene-energy sampling read `PhysicsBodyStore` records instead of post-step `GameModel` body mirrors. Runtime-boundary guardrails block those regressions from returning. |
| Pending work | Continue reducing runtime stepping dependence on `PhysicsModelAccess` and `GameModelCollection`. Phase 8 diagnostics/SkullScope rows, Phase 9 public/standalone guardrail rows, and Phase 10 pending-impulse mirror/overload, prediction-capture, velocity-command, sleep-seed mirror, wake-command mirror, wake/apply signature, velocity signature, step-diagnostics names, collision-time names, fixed-tree store release, step-boundary authority, render snapshot authority, and body-store read authority rows are closed; remaining strict authority work includes reducing the model-owned topology refresh/writeback edge, moving callers toward direct store/handle ownership, and lowering allowlists or adding exact guardrails as future deletion commits remove borrowers. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No active blocker for the default `aaa_ragdoll_sunset_showcase` cinematic/water render path or the contact-audio perceptual model plan. |
| Validation | 2026-07-05 Phase 10 body-store read authority: `git diff --check` passed; `python -m py_compile tools\check_runtime_boundaries.py` passed; `python tools\check_runtime_boundaries.py --repo .` passed with 0 errors; focused Debug build passed with 0 warnings/0 errors; `tools\validate_fast.bat` passed with Profile/Debug 0 warnings/0 errors; intermittent `tools\validate_physics.bat` passed with byte-exact `physics_regression_solver.csv`. Logs: `TestOutput\agent_build_debug_body_store_read_authority.log`, `TestOutput\agent_validate_fast_body_store_read_authority.log`, `TestOutput\agent_validate_physics_body_store_read_authority.log`. Prior Phase 10 render snapshot logs remain available; `tools\validate_perf.bat` is still known-red only for the pre-existing `PHYSICS_BENCH` `Frame/Render.avg` baseline drift described in the strict-goal checklist. |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md` unless the user asks to bypass it.
- Rubber-duck review is for major completed plans/checkpoints, explicit user
  requests, or repeated failure loops. Do not run one per small source slice.
- The retired `Agentic/Orchestrator` JSON/Python path should not be revived
  unless explicitly requested. Use the orchestrator skill for plan work.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you
  launched.
- Time user-requested work and report elapsed wall-clock time in the final
  answer or handoff.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Contact-audio perceptual model | Complete | Plan moved to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`; final acceptance evidence covers material lab, rolling quietness, 200-brick/showcase reducer counts, tail quietness, and `validate_full`. |
| Contrived migration artifact cleanup | Complete; final review passed | Plan: `Agentic/Plans/To_Eval/contrived-migration-artifact-removal-plan.md`; kill list: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`; implementation status: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`. |
| Missed plan items index | Active reference | `Agentic/Plans/missed_plan_items.md` summarizes actionable leftovers from mostly completed Done plans. |
| Runtime interaction state-machine hardening | Active plan | `Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md`; avoid while another agent is editing UI/replay/camera/input code. |
| Render graph / backend interface continuation | Active plan | `Agentic/Plans/render-graph-irender-interface-plan.md`; barrier migration is done, but graph callbacks/transients/capability cleanup continue in focused slices. |
| Global service context cleanup | Active plan | `Agentic/Plans/global-service-context-plan.md`; Carmack plans are complete and only provide evidence for current compatibility debt. |
| Physics/GameModel authority | Active follow-up | `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md`; the persistent solver event/writeback boundary is narrowed, but strict store authority and the final compatibility step boundary remain future work. |
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

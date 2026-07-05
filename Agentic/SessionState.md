# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-5th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Complete handoff for the GameModel compatibility endgame/checker consolidation slice and the first runtime static-allocation enforcement slice. |
| Last documentation milestone | Moved the GameModel compatibility endgame plan to `Agentic/Plans/Done/`, updated `runtime-static-allocation-policy-plan.md`, and refreshed the parent physics/GameModel authority plan with 2026-07-05/06 validation evidence. |
| Last source/data milestone | `GameModel` is now presentation metadata only; physics creation/repair/edit paths use `PhysicsBodyCreateDesc`, `PhysicsColliderCreateDesc`, stores, and descriptor sidecars; deleted `PhysicsModelAccess.h`, `RigidBody.*`, `LoadFromModels`, `RefreshBodyFromModel`, and `MakeBodyRecordFromAuthoredModel`; moved authored contact-material tokens into collider descriptors/records; added the runtime allocation tracker/static checker/validate_perf integration. The temporary physics CSV drift was fixed in code by restoring sphere descriptor metric parity; no intentional baseline update remains. |
| Pending work | Plan 1 is complete and moved to `Agentic/Plans/Done/game-model-compat-endgame-and-fence-consolidation-plan.md`. Plan 2 first enforcement is active, warning-bearing, and intentionally incomplete: it still needs `RuntimeReserveAllocator`, owner pool conversions, replay growth approval, strict no-allocation failure, and owner-level summaries. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No validation blocker. Open scope remains tracked in the plan rows above. |
| Validation | 2026-07-05/06 `nightrunner-5th-july`: `python -m py_compile tools\check_runtime_boundaries.py tools\check_allocation_policy.py`, both checker self-tests, `python tools\check_runtime_boundaries.py --repo . --max-errors 30`, `python tools\check_allocation_policy.py --repo .`, and `git diff --check` passed; `git diff --check` only reported line-ending normalization warnings. Post-review gates passed: `tools\validate_fast.bat` (30.12s), `tools\validate_perf.bat` (28.69s, allocation evidence warning-bearing by design), and `tools\validate_full.bat` (38.72s, DX12 InfoQueue 0, screenshots matched baselines, physics CSV byte-exact). Earlier standalone `tools\validate_physics.bat` also passed (14.48s). |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
- For the active physics/GameModel authority migration, the user requested
  intermittent `tools\validate_physics.bat` checkpoints on completed source
  slices so determinism regressions are caught before the final checkpoint.
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
| Physics/GameModel authority | Active follow-up | `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md`; 2026-07-05 endgame slice deleted the remaining physics-side GameModel compatibility import/writeback/API surface, removed render-host concrete collection reliance from production render surfaces, tightened tombstone guardrails, and moved the focused endgame plan to Done. |
| Runtime static allocation policy | First enforcement slice landed | `Agentic/Plans/In_Progress/runtime-static-allocation-policy-plan.md`; allocation tracker, static checker, allowlist, CLI, phase scopes, and `validate_perf` warning-bearing evidence are active. Strict pool/allocator conversion remains open. |
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

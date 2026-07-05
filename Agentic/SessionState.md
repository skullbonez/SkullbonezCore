# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-5th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Requested two-plan orchestration is complete on `nightrunner-5th-july`; store-owned dynamic STL correction is validated and ready for commit/push handoff. |
| Last documentation milestone | Moved the GameModel compatibility endgame plan and the runtime static allocation policy plan to `Agentic/Plans/Done/`; updated plan evidence and `AGENTS.md` with the runtime static-allocation gate. |
| Last source/data milestone | `GameModel` is now presentation metadata only; physics creation/repair/edit paths use `PhysicsBodyCreateDesc`, `PhysicsColliderCreateDesc`, stores, and descriptor sidecars; deleted the remaining physics-side model compatibility import/writeback/API surface. Runtime allocation policy now has strict gameplay guard failure, reserve-policy denial failure, `RuntimeReserveAllocator` replay-owner accounting, fixed command queue storage, DX12 grid-line PSO warmup/cache caps, interaction automation allocation scoping, replay prediction/cause-tree preallocation, registered replay growth proof, fixed-capacity body/collider store lists, and a static guard against store-owned dynamic STL members. |
| Pending work | No pending work for the requested plans. Follow-up architecture items remain in their own active plans and references. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No validation blocker. Open scope remains tracked in the plan rows above. |
| Validation | 2026-07-06 `nightrunner-5th-july`: store dynamic-container correction passed `python -m py_compile tools\check_allocation_policy.py tools\validate_project_filters.py`, allocation-policy self-test/repo scan (`dynamic_stl_member_findings=0`), project filters, and `git diff --check`. Final post-duck gates passed: `tools\validate_fast.bat` (`Agentic\Temp\validate_fast_fixed_store_vector_ban_final.log`, Profile/Debug 0 warnings/errors), `tools\validate_perf.bat` (`Agentic\Temp\validate_perf_fixed_store_vector_ban_final_retry.log`, clean `perf_1000` guard with `gameplay_violations=0`, `policy_violations=0`, no DX12/physics-bench regressions), and `tools\validate_full.bat` (`Agentic\Temp\validate_full_fixed_store_vector_ban_final.log`, DX12 InfoQueue 0, screenshots matched baselines, physics CSV byte-exact). |

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
| Runtime static allocation policy | Complete | Plan moved to `Agentic/Plans/Done/runtime-static-allocation-policy-plan.md`; ordinary `perf_1000` and replay interaction/report harness are clean under `--allocation-guard gameplay`, registered replay growth is bounded/counted, reserve-policy denials participate in strict guard failure, and body/collider stores no longer own dynamic STL containers. |
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

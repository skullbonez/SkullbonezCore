# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-7th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | 2026-07-07 overnight run complete: all five authoritative CSVs drained in order 04 -> 03 -> 05 -> 02 -> 01, including user-approved `overnight=defer` rows. |
| Last documentation milestone | End-state housekeeping complete: plan files, CSVs, protocol, architecture note, and `Inventories/` moved to `Agentic/Plans/Done/`; blocker ledger left as the single `Agentic/Plans/In_Progress/` file. |
| Last source/data milestone | Fable-03 phase 2 complete: replay prediction now seeds and steps a private replay-owned `PhysicsEngine`, the live mutation/restore window is deleted, and the interaction proof locks live solver-hash stability. Current authoritative row totals remain 130 done and 30 blocked until fable-03 phase 4 closes the PHYS-035 guardrail/ledger work. |
| Pending work | Address the remaining 30 blocked rows in `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; no pending rows remain in the five authoritative CSVs. |
| Concurrent work warning | No unrelated dirty files were present before the fable-01 phase 0 slice. Still run `git status --short --branch` before editing. |
| Blockers | `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; Plan03 5 blocked, Plan05 9 blocked, Plan02 12 blocked, Plan01 4 blocked, Plan04 0 blocked. |
| Validation | Latest gates passed on 2026-07-07 for fable-03 phase 2 after rubber-duck follow-up: `tools\validate_physics.bat` (13.519s, byte-exact), `prediction_ragdoll_wall_200_predict` proof (4.537s, report `ok=1`), `tools\validate_perf.bat` (30.648s after explicit current-machine perf baseline refresh), and `tools\validate_full.bat` (39.288s, DX12 validation errors 0, screenshots matched, physics byte-exact). |

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
| Runtime static allocation policy | Complete; post-duck correction validated | Plan moved to `Agentic/Plans/Done/runtime-static-allocation-policy-plan.md`; post-duck source/data update converts replay prediction growth accounting to bytes, adds tracked F6/branch interaction proofs, and passed targeted proof plus `validate_perf`/`validate_full`. |
| Architecture pass follow-up | Active reference | `Agentic/Plans/architecture_pass_2026-06-02.md` remains the broad checkpoint for runtime, physics data, assets, parser, water, and render graph boundaries. |
| 2026-07-07 overnight remediation | Remediation pass active | Handoff: `Agentic/Reports/2026-07-07/overnight-run-handoff.md`; blockers: `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; SVC-034 and fable-01 phase 0 are fixed; fable-03 phase 2 is complete toward PHYS-035 prediction isolation, with phase 4 guardrails/ledger closure still open. |

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
| Unit tests only | `tools\validate_tests.bat` |
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

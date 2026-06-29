# Carmack Phase 6 Comment Audit And Final Validation Progress

Source plan: `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-remaining-work-authoritative-plan.md`

Assigned scope: Phase 6 - Comment Audit And Final Validation, plus the Definition Of Done items that are final-validation gates or final handoff evidence.

## Current Status

- Status: not started. This file turns Phase 6 into a runnable checklist; it does not claim any Phase 6 evidence is complete.
- Creation task is documentation-only. No repository validation is required for this file.
- Startup status at creation was dirty on branch `nightrunner-29th-june`; treat all existing dirty files and other `carmack-phase-*progress.md` files as user-owned or other-worker-owned.
- Phase 6 should not begin final closure until Phases 0-5 have either landed their source changes or explicitly recorded deferrals in the source plan.

## Checklist

- [ ] Run `git status --short --branch` and record the final branch plus dirty scope before starting Phase 6 closure.
- [ ] Build the final touched source-bearing file list from all implementation work:
  `git diff --name-only -- '*.cpp' '*.h' '*.hpp' '*.inl' '*.hlsl' '*.py' '*.bat' '*.ps1'`,
  `git diff --cached --name-only -- '*.cpp' '*.h' '*.hpp' '*.inl' '*.hlsl' '*.py' '*.bat' '*.ps1'`, and
  `git ls-files --others --exclude-standard -- '*.cpp' '*.h' '*.hpp' '*.inl' '*.hlsl' '*.py' '*.bat' '*.ps1'`.
- [ ] Read `C:\SkullbonezCore\Agentic\Reference\comment-style-guide.md` and `C:\SkullbonezCore\Agentic\Skills\comment-style-audit\skill.md`.
- [ ] Run the comment-style audit over every touched source-bearing file in the final implementation slice.
- [ ] If the audit scope becomes subsystem-wide, create or update a checklist under `C:\SkullbonezCore\Agentic\Plans\` using `git ls-files` inventory, then reconcile checked/deferred/unchecked counts before final reporting.
- [ ] Record comment-audit evidence in the Phase 6 audit item in `carmack-remaining-work-authoritative-plan.md`: exact files audited, checklist/report path if any, checked count, deferred count, and unchecked files.
- [ ] Run `git diff --check`; record clean output or exact failure/fix notes in the Phase 6 `git diff --check` evidence item.
- [ ] Choose the smallest required area gates from the final diff:
  `tools\validate_physics.bat` for physics boundary changes,
  `tools\validate_dx12_renderer.bat` for render graph or DX12 changes,
  `tools\validate_perf.bat` for hot-path or allocation-sensitive changes,
  `tools\validate_runtime_boundaries.bat` or `tools\validate_select.bat runtime-boundaries` for runtime-boundary guardrail changes, and
  `tools\validate_project_filters.bat` for project filter/path ownership changes.
- [ ] Run selected validation gates in a visible `cmd.exe` or PowerShell window when available, and mirror logs under `C:\SkullbonezCore\TestOutput\validation\agent_logs\carmack_phase6_<gate>.log`.
- [ ] Ensure `tools\validate_perf.bat` passes, or record the reviewed waiver/baseline-update note that explicitly closes the remaining perf deltas.
- [ ] Run the final broad gate `tools\validate_full.bat` after all source, evidence, and documentation updates are in their final branch state; mirror to `C:\SkullbonezCore\TestOutput\validation\agent_logs\carmack_phase6_validate_full.log`.
- [ ] Rubber-duck `carmack-remaining-work-authoritative-plan.md` against the final branch state and record reviewer/report path plus blocker status in the Phase 6 rubber-duck evidence item.
- [ ] Update the Definition Of Done evidence in `carmack-remaining-work-authoritative-plan.md`: no unchecked plan items, regenerated/reconciled global-service evidence, perf closure, full gate, comment audit evidence, and final handoff wording.
- [ ] Final handoff names `carmack-remaining-work-authoritative-plan.md` as the source of truth and treats older Carmack plans under `Agentic\Plans\Done\` as archived history only.

## Likely Files And Tools To Inspect

- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-remaining-work-authoritative-plan.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-0-evidence-reconciliation-progress.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-1-perf-gate-closure-progress.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-2-global-service-lifetime-progress.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-3-physics-standalone-boundary-progress.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-4-render-backend-capability-progress.md`
- `C:\SkullbonezCore\Agentic\Plans\IN PROGRESS\carmack-phase-5-render-graph-resource-ownership-progress.md`
- `C:\SkullbonezCore\Agentic\Reference\comment-style-guide.md`
- `C:\SkullbonezCore\Agentic\Skills\comment-style-audit\skill.md`
- `C:\SkullbonezCore\Agentic\Reports\2026-06-29\carmack-handoff\perf-validation-note.md`
- `C:\SkullbonezCore\Agentic\Reports\2026-06-29\carmack-handoff\global-service-hit-classification.csv`
- `C:\SkullbonezCore\Agentic\Reports\2026-06-29\carmack-handoff\global-service-hit-classification-summary.md`
- `C:\SkullbonezCore\tools\check_runtime_boundaries.py`
- `C:\SkullbonezCore\tools\validate_runtime_boundaries.bat`
- `C:\SkullbonezCore\tools\validate_project_filters.bat`
- `C:\SkullbonezCore\tools\validate_physics.bat`
- `C:\SkullbonezCore\tools\validate_dx12_renderer.bat`
- `C:\SkullbonezCore\tools\validate_perf.bat`
- `C:\SkullbonezCore\tools\validate_full.bat`

## Dependencies

- Phase 0 must provide regenerated global-service classification evidence from the final source tree before the Definition Of Done can close.
- Phase 1 must provide either a clean `tools\validate_perf.bat` log or a reviewed waiver/baseline-update note.
- Phases 2-5 must stabilize the final source-bearing diff before the comment audit and validation file list can be trusted.
- Any source-bearing files touched late for review fixes must be added back into the comment audit scope before final handoff.
- Existing dirty files and other workers' phase progress files must remain untouched unless the user assigns them explicitly.

## Evidence To Collect

- Final `git status --short --branch` output before validation and before handoff.
- Exact touched source-bearing file list used for the comment audit.
- Comment audit result: audited files, checklist/report path, checked count, deferred count, unchecked files, and any human-wording questions.
- `git diff --check` result.
- Selected area-gate logs under `C:\SkullbonezCore\TestOutput\validation\agent_logs\`.
- `tools\validate_perf.bat` pass log or reviewed perf waiver/baseline-update note.
- `tools\validate_full.bat` log showing project filters, runtime boundaries, Profile/Debug builds, DX12 validation errors 0, screenshots matching baselines, and byte-exact `physics_regression_solver.csv`.
- Rubber-duck final review report and blocker status.
- Final source-plan evidence updates for Phase 6 and Definition Of Done.

## Validation Note

No validation is required for creating this progress document because this is a documentation-only change. Phase 6 itself must run the selected gates and the final `tools\validate_full.bat` only after the final implementation branch state is ready for PR/commit handoff.

## Open Risks And Questions

- The final touched-file list can change while other phase workers finish; do not start the audit from an early snapshot.
- The perf gate is still a Definition Of Done dependency until Phase 1 records a pass or an explicit reviewed waiver/baseline-update note.
- A clean broad gate before source-plan evidence updates is not final evidence; rerun or clearly invalidate it if later code, shader, scene, tool, or baseline files change.
- Final rubber-duck review may find missing evidence rather than code defects; leave time to update evidence and rerun any invalidated gate.
- If another worker creates or modifies this exact progress file, stop and resolve the ownership conflict before editing it.

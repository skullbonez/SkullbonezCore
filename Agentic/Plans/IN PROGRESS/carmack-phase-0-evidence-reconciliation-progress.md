# Carmack Phase 0 Evidence Reconciliation Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

## Current Status

- Status: Complete for Phase 0 evidence reconciliation.
- Scope: Phase 0 only, evidence reconciliation for the remaining Carmack work.
- Impact area: documentation and evidence reports. No source, tool, scene, shader, or baseline edits are part of this progress file.
- Startup notes: `codegraph status .` reported the index is up to date. `git status --short --branch` was clean at Phase 0 implementation start on `nightrunner-29th-june`.
- Reconciliation result: `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv` now reports zero direct `Gfx()` hits in `SkullbonezSource/Assets/TextureCollection.cpp`; current source routes texture backend calls through `m_renderResources` and `m_renderCommands`.

## Checklist

- [x] Regenerate `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv` from the current `SkullbonezSource/` tree using the matching logic in `tools/check_runtime_boundaries.py`.
- [x] Remove stale `SkullbonezSource/Assets/TextureCollection.cpp` `Gfx()` rows from the regenerated CSV if the current source still has no direct `Gfx()` calls there.
- [x] Change the `SkullbonezSource/Assets/TextureCollection.cpp` classification row from `Gfx()=3, TextureCollection::Instance()=1` to the current actual labels/counts in the regenerated evidence.
- [x] Regenerate `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md` from the regenerated CSV, including totals by classification and label.
- [x] Record which remaining hits are allowed `bootstrap`, `OS callback bridge`, `diagnostics`, or `test/tool` access, and which remain `normal runtime path`, `render pass`, or `asset lookup` debt.
- [x] Record the generation command and output path in `TestOutput/validation/agent_logs/carmack_phase0_global_service_reconcile.log` or an equivalent Phase 0 evidence log.
- [x] Record the final CSV and summary paths beside the Phase 0 evidence slots in `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md` after the evidence files are regenerated.
- [x] Leave the archived Carmack plans under `Agentic/Plans/Done/` unchanged; use them only to resolve classification wording drift.

## Likely Files And Tools To Inspect

- `tools/check_runtime_boundaries.py`: source of global-service patterns, stripping logic, counted allowlist, renderer-service file classifications, and runtime-boundary JSON output.
- `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`: current stale per-hit evidence to replace.
- `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`: current stale summary to replace.
- `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md`: historical bucket wording and 2026-06-28 allowlist-row classification snapshot.
- `Agentic/Reports/2026-06-28/carmack-plan-rubber-duck-review.md`: reviewer note requiring per-hit semantic classification.
- `Agentic/Reports/2026-06-28/carmack-test-post-plan-review.md`: high-level Carmack-test evidence for remaining global-service debt.
- `SkullbonezSource/Assets/TextureCollection.cpp`: first stale-hit sanity check for regenerated output.
- Useful commands:
  - `codegraph status .`
  - `python tools/check_runtime_boundaries.py --repo . --json-out TestOutput/validation/runtime_boundaries/carmack_phase0_runtime_boundaries.json`
  - `rg -n "Gfx\\(|GfxRayTracing\\(|Cfg\\(|ActiveAssetSystem\\(|CreateShaderFromActiveAssets\\(|::Instance\\(|pInstance|g_[A-Za-z_]" SkullbonezSource -g "*.cpp" -g "*.h" -g "*.hpp" -g "*.inl"`

## Dependencies

- Use the authoritative plan as the active queue; the old Carmack plans are historical unless the authoritative plan points back to them.
- If a reusable CSV generator is added to `tools/check_runtime_boundaries.py`, that becomes a tool change and needs the matching validation gate.
- If no checked-in CSV generator is added, preserve the exact one-off generation command in the Phase 0 evidence log so the report is reproducible.
- Coordinate before editing the authoritative plan because other workers may be adding evidence for other phases.

## Evidence To Collect

- Regenerated CSV: `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`.
- Regenerated summary: `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`.
- Generation/evidence log: `TestOutput/validation/agent_logs/carmack_phase0_global_service_reconcile.log`.
- Runtime-boundary JSON: `TestOutput/validation/runtime_boundaries/carmack_phase0_runtime_boundaries.json`.
- Count comparison: stale total 611 hits; regenerated total 593 hits.
- Bucket totals: `OS callback bridge` 56, `asset lookup` 12, `bootstrap` 30, `diagnostics` 79, `normal runtime path` 223, `render pass` 163, `test/tool` 30.
- `SkullbonezSource/Assets/TextureCollection.cpp` direct `Gfx()` evidence was removed; regenerated evidence has one remaining `TextureCollection::Instance()` hit.

## Validation Note

This progress file is documentation-only; no repository validation script is required. A future Phase 0 evidence-only update to Markdown/CSV reports also requires no repository validation. If Phase 0 changes `tools/check_runtime_boundaries.py` or any other tool script, run the changed script directly and the required tool gate: `tools\validate_fast.bat`.

## Open Risks And Questions

- There is still no checked-in command that emits the CSV/summary; Phase 0 used an inline Python script and recorded the exact method in `TestOutput/validation/agent_logs/carmack_phase0_global_service_reconcile.log`.
- The regenerated CSV/summary should now be treated as the Phase 2 starting point until source changes make it stale again.
- `shutdown` has no separate rows in the regenerated classification; future migration work can add that bucket only if new evidence needs it.
- Confirm whether `g_*` hits are service-locator debt, callback accumulators, diagnostics counters, or benign local naming before assigning migration buckets.

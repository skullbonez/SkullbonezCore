# Carmack Phase 0 Evidence Reconciliation Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

## Current Status

- Status: Not started; this file makes Phase 0 actionable without changing the authoritative plan.
- Scope: Phase 0 only, evidence reconciliation for the remaining Carmack work.
- Impact area: documentation and evidence reports. No source, tool, scene, shader, or baseline edits are part of this progress file.
- Startup notes: `codegraph status .` reported the index is up to date. `git status --short --branch` showed a pre-existing user-owned edit in `SkullbonezData/scenes/aaa_ragdoll_clean_sky.scene.json`; do not touch it.
- Current stale-evidence clue: `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv` still lists direct `Gfx()` hits in `SkullbonezSource/Assets/TextureCollection.cpp`, but current source routes texture backend calls through `m_renderResources` and `m_renderCommands`.

## Checklist

- [ ] Regenerate `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv` from the current `SkullbonezSource/` tree using the matching logic in `tools/check_runtime_boundaries.py`.
- [ ] Remove stale `SkullbonezSource/Assets/TextureCollection.cpp` `Gfx()` rows from the regenerated CSV if the current source still has no direct `Gfx()` calls there.
- [ ] Change the `SkullbonezSource/Assets/TextureCollection.cpp` classification row from `Gfx()=3, TextureCollection::Instance()=1` to the current actual labels/counts in the regenerated evidence.
- [ ] Regenerate `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md` from the regenerated CSV, including totals by classification and label.
- [ ] Record which remaining hits are allowed `bootstrap`, `shutdown`, `OS callback bridge`, `diagnostics`, or `test/tool` access, and which remain `normal runtime path`, `render pass`, or `asset lookup` debt.
- [ ] Record the generation command and output path in `TestOutput/validation/agent_logs/carmack_phase0_global_service_reconcile.log` or an equivalent Phase 0 evidence log.
- [ ] Record the final CSV and summary paths beside the Phase 0 evidence slots in `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md` after the evidence files are regenerated.
- [ ] Leave the archived Carmack plans under `Agentic/Plans/Done/` unchanged; use them only to resolve classification wording drift.

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

- Regenerated CSV path and timestamp.
- Regenerated summary path and timestamp.
- Generation command or script path.
- Count comparison against the stale summary, including total classified hits and bucket totals.
- Explicit note that `TextureCollection.cpp` direct `Gfx()` evidence was removed or, if still present, the exact current lines that justify keeping it.
- Final list of remaining `normal runtime path`, `render pass`, and `asset lookup` debt groups.
- Optional runtime-boundary JSON path proving the guardrail still passes while the evidence is regenerated.

## Validation Note

This progress file is documentation-only; no repository validation script is required. A future Phase 0 evidence-only update to Markdown/CSV reports also requires no repository validation. If Phase 0 changes `tools/check_runtime_boundaries.py` or any other tool script, run the changed script directly and the required tool gate: `tools\validate_fast.bat`.

## Open Risks And Questions

- There may be no checked-in command that emits the current CSV/summary; the prior evidence may have come from a one-off script.
- The current CSV and summary appear stale against at least `TextureCollection.cpp`; do not trust counts until regenerated from the current source.
- The authoritative plan asks for evidence paths inside itself, but this progress-file task is not allowed to edit that plan.
- Decide whether `shutdown` should be a separate bucket if the regenerated evidence has no existing shutdown classification rows.
- Confirm whether `g_*` hits are service-locator debt, callback accumulators, diagnostics counters, or benign local naming before assigning migration buckets.

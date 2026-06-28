# Rubber-Duck Catch Ledger

Date: 2026-06-28
Branch: `Night-Runner-27th-June`
Scope: Carmack plan implementation and review slices.

Purpose: track how much the independent rubber-duck reviewers catch before each
commit. Count blocking findings separately from non-blocking findings, and name
the concrete change that happened because of the review.

## Running Totals

Rows reconstructed from existing plan notes are included so the count is useful
for the whole Carmack run, not only from this file forward.

| Source | Blocking findings caught | Non-blocking findings caught |
|--------|--------------------------|------------------------------|
| Reconstructed from existing plan notes | 7 | 8 |
| Live tracked in this ledger | 13 | 12 |
| Total so far | 20 | 20 |

## Ledger

| Slice | Reviewer | Blocking findings | Non-blocking findings | What changed before commit | Source |
|-------|----------|-------------------|-----------------------|----------------------------|--------|
| Global-service guardrail draft | Ampere | 2 | 1 | Expanded guardrail coverage from a narrow named-service set to include `Cfg()`, generic `::Instance()`, `pInstance`, and `g_*`; recorded that counts are a ratchet, not semantic classification. | Reconstructed from `Agentic/Plans/carmack-global-service-lifetime-plan.md`. |
| Standalone point-joint handles | Feynman | 0 | 3 | Added same-body rejection, endpoint-update smoke coverage, and validation/hash evidence before commit. | Reconstructed from `Agentic/Plans/carmack-physics-standalone-boundary-plan.md`. |
| Standalone query handles | Chandrasekhar | 2 | 2 | Fixed local-offset/oriented collider query false-negative risk, clamped conservative radii, and documented conservative envelope tradeoff. | Reconstructed from `Agentic/Plans/carmack-physics-standalone-boundary-plan.md`. |
| Standalone activation commands | Avicenna | 3 | 2 | Made sleep-disable durable across create/update/step/query, documented `SetSleepEnabled` ignoring the body handle, added activation/sleep/wake glossary coverage, and tightened `SleepEnabled()` wording. | Reconstructed from `Agentic/Plans/carmack-physics-standalone-boundary-plan.md`. |
| Allocation policy baseline evidence | Herschel | 1 | 2 | Replaced a replay-themed scene candidate with an explicitly replay-enabled launch, and called out that the perf log is local/ignored while durable excerpts live in the plan. | Live subagent review before commit `87e17f19`. |
| Global-service lifetime owners | Banach | 0 | 2 | Clarified `RuntimeRenderHost` owns render scratch only and borrows services; left broader catalog completeness as a non-blocking note. | Live subagent review before commit `5f5b6de9`. |
| Standalone physics ordering comments | Sartre | 1 | 0 | Added glossary definitions for `AABB` and `STL` after the comment-style blocker; kept replay wording future-facing. | Live subagent review before commit `ef18a787`. |
| Counted global-service allowlist classification | Bernoulli | 1 | 0 | Split counted allowlist-row classification from still-open per-site hit classification so the checklist no longer overclaims completion. | Live subagent review before this commit. |
| GameModel physics command adapter | Plato | 0 | 1 | Made duplicate replay-derived scene object ids fail closed in `BodyHandleForSceneObjectId` instead of silently selecting the first matching model. | Live subagent review before this commit; missing evidence reminders: final validation still required, and no focused adapter-unit evidence exists yet. |
| Scene parser asset context | Harvey | 1 | 0 | Removed stale `TestSceneParser.cpp` `ActiveAssetSystem()` debt from the global-service plan snapshot and lowered the plan's total `ActiveAssetSystem()` count from 4 to 3. | Live subagent review before this commit; missing evidence reminder cleared by final validation. |
| Editor placement asset context | Pauli | 0 | 2 | Accepted the one-shot process-static building catalog cache as a residual design constraint and reminded the agent to keep unrelated dirty startup/tooling docs unstaged. | Live subagent review before this commit; missing-evidence reminders cleared by comment audit and final validation. |
| Runtime lifetime-order docs | Sagan | 3 | 1 | Added the missing `Run::Run()`/`EngineContext` bind step, corrected `RebuildRegisteredRenderResources()` wording to texture/source-record work, fixed cleanup wording around `UnregisterClass(...)`/`Window::Destroy()`, and moved worker self-test after worker-pool init. | Live subagent review before this commit; docs-only, no repository validation required. |
| Runtime borrowed-binding assertions | Faraday | 1 | 1 | Added the missing `RuntimeViewModelBuilder::Build()` debug assertion so the view-model fallback cannot hide an unbound `EngineContext`, and confirmed normal startup/shutdown does not false-positive. | Live subagent review before this commit; final `validate_full` passed before commit. |
| Input callback bridge comments | Kepler | 2 | 2 | Split true callback-fed wheel/raw mouse accumulators from cursor policy and automation override state, corrected the mouse-wheel consumer/reset path, expanded raw mouse reset wording, and widened the file invariant to frame/UI snapshots. | Live subagent review before this commit; comment-only, no repository validation required. |
| Input callback bridge lifecycle | Mencius | 3 | 1 | Tightened bind/unbind assertions, gated raw-input registration on the bound HWND, confirmed startup order, and updated the stale plan checklist before validation. | Live subagent review before this commit; final validation passed before commit. |

## Review Accounting

| Run id | Plan path | Reviewer | Prompt chars | Response chars | Token accounting | Elapsed | Verdict | Follow-up |
|--------|-----------|----------|--------------|----------------|------------------|---------|---------|-----------|
| carmack-physics-runtime-adapter-duck-01 | `Agentic/Plans/carmack-physics-standalone-boundary-plan.md` | Plato / `019f0cf9-b479-7720-995c-d5161f7a5426` | 2331 | 1548 | n/a | ~15m | 0 blockers, 1 non-blocking catch, 2 missing-evidence reminders | Duplicate scene ids now fail closed; final validation still required before commit. |
| carmack-global-scene-parser-assets-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Harvey / `019f0d0c-e183-77f2-a350-55be236546f6` | 2194 | 1490 | n/a | 2m37s | 1 blocker, 0 non-blocking catches, 1 missing-evidence reminder | Stale plan count/table fixed; final validation passed before commit. |
| carmack-global-editor-placement-assets-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Pauli / `019f0d1f-56e3-7ab0-9504-8a41a88c8d78` | 1702 | 2450 | n/a | ~3m19s | 0 blockers, 2 non-blocking catches, 3 missing-evidence reminders | One-shot cache left as intentional residual, user-owned dirty docs left unstaged, and final comment-audit/validation evidence completed before commit. |
| carmack-global-lifetime-order-docs-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Sagan / `019f0d29-5316-70f1-ab15-1c1a1f4c11ac` | 1303 | 2312 | n/a | ~10m | 3 blockers, 1 non-blocking catch, 0 missing-evidence reminders | Missing EngineContext bind, render-resource overclaim, cleanup wording, and worker self-test ordering fixed before commit. |
| carmack-global-engine-context-assertions-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Faraday / `019f0d33-6682-7340-853f-cc686a4f671c` | 1080 | 1978 | n/a | ~1m23s | 1 blocker, 1 non-blocking catch, 2 missing-evidence reminders | RuntimeViewModel fallback bypassed `EngineContext::Bindings()` assertions; fixed before follow-up. |
| carmack-global-engine-context-assertions-duck-02 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Faraday / `019f0d33-6682-7340-853f-cc686a4f671c` | 743 | 798 | n/a | <1m | 0 blockers, 0 non-blocking catches, 2 missing-evidence reminders | Original blocker fixed; final validation passed before commit. |
| carmack-global-input-callback-comments-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Kepler / `019f0d3a-d65e-72b1-af69-76f23a1fc69e` | 1102 | 2354 | n/a | ~8m | 2 blockers, 1 non-blocking catch, 1 missing-evidence reminder | Cursor policy and automation were misclassified as callback accumulators, and the wheel consumer/reset path was misnamed; fixed before follow-up. |
| carmack-global-input-callback-comments-duck-02 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Kepler / `019f0d3a-d65e-72b1-af69-76f23a1fc69e` | 1212 | 987 | n/a | <1m | 0 blockers, 1 non-blocking catch, 0 missing-evidence reminders | Original blockers fixed; final `InputState` wording was widened to frame/UI snapshots before commit. |
| carmack-global-input-callback-lifecycle-duck-01 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Mencius / `019f0d44-33db-79d2-967e-65ffdd33dc8b` | 1185 | 2436 | n/a | ~12m | 2 blockers, 1 non-blocking catch, 4 missing-evidence reminders | Bind/unbind assertions were too permissive and raw-input registration was not gated on the bound HWND; fixed before follow-up. |
| carmack-global-input-callback-lifecycle-duck-02 | `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Mencius / `019f0d44-33db-79d2-967e-65ffdd33dc8b` | 1152 | 1732 | n/a | ~8m | 1 blocker, 0 non-blocking catches, 3 missing-evidence reminders | Source blockers fixed; stale plan checklist updated before validation; final validation passed before commit. |

## Update Rule

Before each future reviewed commit:

1. Add one row for the reviewer result.
2. Increment blocking and non-blocking totals.
3. Record the exact pre-commit change caused by each blocking finding.
4. Track missing-evidence reminders in review accounting, but keep them out of
   blocking/non-blocking catch totals unless the reviewer classifies them as
   findings.
5. Do not count issues caught after commit as pre-commit catches.

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
| Live tracked in this ledger | 3 | 4 |
| Total so far | 10 | 12 |

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

## Update Rule

Before each future reviewed commit:

1. Add one row for the reviewer result.
2. Increment blocking and non-blocking totals.
3. Record the exact pre-commit change caused by each blocking finding.
4. Do not count issues caught after commit as pre-commit catches.

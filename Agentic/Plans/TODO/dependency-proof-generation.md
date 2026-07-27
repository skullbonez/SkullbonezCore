# Dependency Proof Generation

Date: 2026-07-28
Status: TODO — 0/3 phases complete
Impact area: Dependency governance, validation tooling, agent instructions
Owner: Repository governance
Priority: Medium

## Problem And Evidence

`tools/dependency_graph_rules.json` is authoritative, but `AGENTS.md` manually
mirrors the 21-row Runtime package matrix with approximately 21 handwritten
regular expressions. Both copies require synchronized edits and can drift while
claiming to prove the same rule set.

## Goal

Retain concise human-readable dependency direction while deriving executable
proofs from the authoritative rule data through one tested mechanism.

## Phases

- [ ] **DP0 — Compare JSON, matrix, regexes, and fixtures.** Produce a
  deterministic difference report and classify information that is genuinely
  explanatory versus mechanically duplicated.
- [ ] **DP1 — Generate or replace the mirror proofs.** Extend the existing
  dependency checker to emit a review-friendly current proof/report from rule
  data, or generate a marked documentation block checked for freshness. Do not
  add a second checker, frozen edge count, or hardcoded package branch.
- [ ] **DP2 — Prove drift detection and update instructions.** Add positive and
  negative fixtures showing a rule-data edit updates the proof while a stale
  generated block fails; update `AGENTS.md`, tool docs, comment audit, and all
  mapped gates.

## Acceptance

One rule-data source determines enforcement and executable review proofs.
`AGENTS.md` remains understandable without carrying an independently maintained
regex implementation, and a planted drift is rejected.

## Validation

Changed-tool self-test, `tools\validate_dependency_graph.bat`,
`tools\validate_fast.bat`, and `tools\validate_full.bat`.

# Dependency Proof Generation

Date: 2026-07-28
Status: ACTIVE — 2/3 phases complete
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

- [x] **DP0 — Compare JSON, matrix, regexes, and fixtures.** Produce a
  deterministic difference report and classify information that is genuinely
  explanatory versus mechanically duplicated. Evidence:
  `../../Reports/2026-07-28/dependency-proof-generation-dp0-comparison.md`.
- [x] **DP1 — Generate or replace the mirror proofs.** Extend the existing
  dependency checker to emit a review-friendly current proof/report from rule
  data, or generate a marked documentation block checked for freshness. Do not
  add a second checker, frozen edge count, or hardcoded package branch.
  Evidence:
  `../../Reports/2026-07-28/dependency-proof-generation-dp1-checkpoint.md`.
- [ ] **DP2 — Prove drift detection and update instructions.** Add positive and
  negative fixtures showing a rule-data edit updates the proof while a stale
  generated block fails; update `AGENTS.md`, tool docs, comment audit, and all
  mapped gates.

## DP1/DP2 Required Evidence

- One deterministic marked block owns the 21-row Runtime matrix and all 27
  mechanical regex commands: 20 Runtime plus seven broad/boundary proofs. Only
  qualitative ownership/review prose and one explicitly labelled
  non-executable Rendering vocabulary search remain hand-written outside it.
- The projection distinguishes source/target prefixes from exact files. Migrate
  the six `RuntimeFrameViews.h` allow entries from raw prefix lists to exact
  files: valid exact-file edges remain allowed, while a negative
  `Runtime/RuntimeFrameViews.h/Child.h` fixture pins the intentionally closed
  pseudo-descendant prefix allowance.
- Two unchanged renders are byte-identical. The committed block passes a
  freshness check, while a planted rule edit changes output and a stale block
  fails.
- DP1 fails closed on missing, duplicate, and reversed generated-block markers.
  DP2 fixtures prove all three failures and prove write mode preserves every
  byte outside the unique ordered marker pair.
- DP1 uses deterministic Markdown escaping. DP2 fixtures cover pipes, backticks,
  angle brackets, ampersands, and line breaks from rule-controlled text.
- A planted future Runtime package proves every allow row remains closed-world
  without a package-specific Python branch. Input's three exact-file exceptions
  and the frame-view self-include deny remain visible.
- Record macro-expanded and backslash-continued include operands plus
  angle/quoted search-order differences as residual parser limits; do not
  describe the textual scanner as a compiler dependency graph.
- DP2 separates the compound project negative into missing-required,
  required-plus-Core, required-plus-Tests, and end-to-end XML/path-discovery
  cases with exact finding assertions.

## Acceptance

One rule-data source determines enforcement and executable review proofs.
`AGENTS.md` remains understandable without carrying an independently maintained
regex implementation, and a planted drift is rejected.

## Validation

Changed-tool self-test, `tools\validate_dependency_graph.bat`,
`tools\validate_fast.bat`, and `tools\validate_full.bat`.

DP0 is documentation-only; no repository validation is required. DP1 owns the
first changed-tool proof.

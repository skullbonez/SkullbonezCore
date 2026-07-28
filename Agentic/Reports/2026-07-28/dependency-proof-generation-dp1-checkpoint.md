# Dependency Proof Generation — DP1 Checkpoint

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/TODO/dependency-proof-generation.md`
Phase: DP1 — 2/3 complete

## Outcome

The existing `tools/check_dependency_graph.py` now owns both dependency
enforcement and one deterministic, review-friendly `AGENTS.md` projection.
There is no second checker, package-specific policy branch, edge count, or
budget.

- `--render-proof` writes the canonical marked block to stdout.
- `--check-proof AGENTS.md` fails when the unique ordered block differs from
  current rule data.
- `--write-proof AGENTS.md` replaces only the unique ordered marker span and
  preserves every byte outside it.
- Missing, duplicate, and reversed markers fail closed.
- Rule-controlled pipes, backticks, angle brackets, ampersands, and line breaks
  have deterministic Markdown escaping.
- Default repository mode self-tests first, freshness-checks `AGENTS.md`, then
  runs the existing include/content/project evaluators.

The generated block replaces the handwritten 21-row Runtime matrix and all 27
mechanical `rg` proofs. It shows prefixes and exact files in separate columns,
shows App's current closed-world allowance, renders Camera's App permission as
the raw prefix it really is, and includes both Runtime and Rendering in the UI
deny row. The one broad Rendering feature-vocabulary search remains outside the
block and is explicitly labelled qualitative.

All six `Runtime/RuntimeFrameViews.h` allowances moved from
`allowed_target_prefixes` to `allowed_target_files`. Generic positive-fixture
coverage now exercises every allowed exact file, while the added
`Runtime/RuntimeFrameViews.h/Child.h` fixture pins intentional descendant
rejection.

## Residual Limits

The generated proof states the text scanner's real boundary: macro-expanded
include operands and backslash-continued directives are not parsed. Quoted and
angle operands are recognized, but resolution uses one local-first search order
instead of reproducing the compiler's distinct search semantics.

DP2 still owns:

- a planted rule-data edit that changes rendered bytes and makes a stale
  committed block fail;
- independent missing-required, required-plus-Core, and required-plus-Tests
  project cases;
- end-to-end project XML and tracked-path discovery;
- bounded macro/continuation and quoted/angle residual-parser evidence;
- final instruction/comment reconciliation, independent review, and the mapped
  `validate_fast` and `validate_full` gates.

## Validation

| Proof | Result |
|---|---|
| `python -m py_compile tools/check_dependency_graph.py` | PASS |
| scoped `git diff --check` | PASS |
| `python tools/check_dependency_graph.py --self-test` | PASS — 27 include rules, 47 negative edge fixtures, one content rule with two negative fixtures, one project fixture, and generated-proof fixtures |
| `python tools/check_dependency_graph.py --check-proof AGENTS.md` | PASS — generated block current |
| `tools\validate_dependency_graph.bat` | PASS — zero repository findings |
| `python tools/inventory_authority_free_aggregates.py --repo .` plus strict JSON gate | PASS — 85/85 gated rows ruled, zero unruled |
| `python tools/inventory_extraction_scars.py --repo .` | PASS — 1/1 finding ruled |
| `python tools/inventory_wide_signatures.py --repo . --threshold 12 --format json --strict` | PASS — every triggered signature has a current owner ruling |

No broad/full gate ran in DP1; DP2 owns the mapped final gates. No baseline,
golden, config, schema, performance artifact, or layout changed.

## Comment Audit

Touched source/tool scope: `tools/check_dependency_graph.py`.

- Checked: 1
- Deferred: 0

The file retains a complete learning header and now names generated-proof
semantics, byte-preserving marker ownership, exact-file versus prefix behavior,
fixture purpose, and residual textual-parser limits. Nearby comments explain
the non-existent rooted fallback used by the pseudo-descendant fixture.

The three warm-start experiment files remained pre-existing, unstaged,
unformatted, and outside every DP1 edit and validation target.

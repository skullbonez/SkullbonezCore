# Engine Glossary Consolidation — GC1 Standard

Date: 2026-07-31
Branch: `nightrunner-30th-JUL-26`
Plan: `Agentic/Plans/TODO/engine-glossary-consolidation.md`
Phase: GC1

## Outcome

GC1 establishes one authoritative shared-vocabulary boundary without changing
engine source or behavior:

- `Agentic/Reference/engine-glossary.md` contains exactly the 321 canonical
  definitions adjudicated by GC0.
- File learning headers retain `Summary:`. The standard now rejects filename
  restatements and requires ownership, decision, or flow information instead.
- A term defined in exactly one tracked source file stays in that file's
  `Glossary:` block. A term defined in more than one tracked source file belongs
  in the shared glossary and is cited from `Related:`.
- The comment-style audit and rubber-duck review instructions state that split
  directly, including the no-threshold rule.
- `tools/inventory_glossary_terms.py` reports multi-file definitions and wording
  drift, then joins each finding to exact current-source migration evidence.

No `SkullbonezSource` file, baseline, golden, schema, configuration value, or
runtime artifact changed.

## Canonical Glossary

The GC0 shared-term table was parsed mechanically and reproduced in
`Agentic/Reference/engine-glossary.md`, ordered case-insensitively for lookup
while preserving each exact term spelling. A direct reconciliation compared
term and canonical wording maps and passed all 321 rows exactly.

The glossary distinguishes exact spellings. GC0 therefore retains its separate
`UI (User Interface)` and `UI (user interface)` adjudications until an owner
explicitly unifies their vocabulary.

## Repeatable Inventory Contract

The new inventory scans tracked `.cpp`, `.h`, `.hpp`, `.inl`, and `.hlsl` files
under `SkullbonezSource` by `git ls-files`. It reads only the leading
learning-header `Glossary:` block and normalizes wrapped definition whitespace.
A term becomes a finding when two or more distinct tracked files define the
same exact term.

Each finding records:

- exact term spelling;
- every definition site's repository path and line;
- every normalized wording variant;
- whether wording drift exists; and
- a SHA-256 fingerprint of the complete term/file/line/wording set.

Strict mode fails when a finding is unruled, a fingerprint changes, a ruling is
stale or malformed, or a repair ruling does not name an existing canonical TODO
Markdown plan.
Repair plans must use the canonical repository-relative
`Agentic/Plans/TODO/*.md` shape; absolute paths, traversal, README/report files,
and unsupported ruling schema versions are rejected. The sole disposition is
`repair-plan`: every current copy is owned by GC2 for removal. The 321 rows in
`tools/glossary_term_rulings.json` are migration evidence, not allowances. When
GC2 removes the duplicate definitions, their stale rows must be removed as well.

The self-test plants identical and drifted multi-file terms, a local term,
wrapped wording, a bullet-form entry, namespace continuations, and a false
`Glossary:` after source code. It also proves unruled-fails, exact-ruled-passes,
fingerprint-drift failure, stale-ruling failure, and malformed-ruling failure.
It also plants README, report, absolute, traversal, and unsupported-schema
evidence to prove that only the live canonical TODO plan can satisfy a repair
ruling.

## Integration

The inventory and ruling contract are registered in:

- `AGENTS.md` Comment Quality Gate, repeatable-inventory table, and
  file-to-validation mapping;
- `Agentic/Reference/comment-style-guide.md`;
- `Agentic/Skills/comment-style-audit/skill.md`;
- `Agentic/Skills/rubber-duck/SKILL.md`;
- `tools/README.md`; and
- `tools/validate_fast.bat`.

`validate_fast` now runs the glossary self-test before the repository scan and
runs the scan in strict mode with JSON output suppressed on success.

## Validation

| Check | Result |
|---|---|
| `python -m py_compile tools/inventory_glossary_terms.py` | Pass |
| `python tools/inventory_glossary_terms.py --self-test` | Pass |
| `python tools/inventory_glossary_terms.py --repo . --strict` | Pass: 575 files, 2,172 definitions, 1,285 unique terms, 321 multi-file terms, 264 drifted, 321 exact current rulings, zero unruled, zero ruling issues |
| GC0 canonical term/wording reconciliation | Pass: 321/321 exact |
| `tools\validate_fast.bat` | Pass: formatting, project filters, dependency proof, all inventory gates, staged-size check, Profile/Debug builds, compiled-symbol reachability, and 457/457 doctest cases with 2,424,712/2,424,712 assertions |
| `git diff --check` | Pass |
| `git diff --name-only -- SkullbonezSource` | Empty |

## Comment-Style Audit

The two touched source-bearing tool files were inspected against
`Agentic/Skills/comment-style-audit/skill.md`:

- `tools/inventory_glossary_terms.py` has the required learning-header fields,
  defines its parser and ruling vocabulary, states tracked-file/currentness
  invariants, and places the fingerprint rationale beside the digest code.
- `tools/validate_fast.bat` retains a non-tautological `Summary:`, validation
  invariants, and the nearby `Why:` comment that explains why inventory
  self-tests precede current-tree scans.

Checked: 2. Deferred: 0. No engine source comment checklist row changes in GC1;
the 575-file consolidation checklist remains the GC2 source of truth.

## Review

The first independent rubber-duck review found two blockers: the repair-plan
validator accepted arbitrary Markdown paths, and the tool's permanent
`Related:` block cited the deletion-bound TODO plan. It also recommended
validating the ruling schema version.

GC1 now restricts repair plans to canonical existing
`Agentic/Plans/TODO/*.md` paths, rejects hostile/non-plan paths and unsupported
schema versions in planted self-tests, and cites this durable report from the
tool header. The full validation and direct inventory gates were rerun after
those fixes.

Final independent verdict: **ACCEPT/CLEAR**, with no blocking, non-blocking, or
missing-evidence findings.

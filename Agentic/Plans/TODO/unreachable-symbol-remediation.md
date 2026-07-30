# Unreachable Symbol Remediation

Date: 2026-07-30
Status: NOT STARTED — 0/4 phases complete
Impact area: First-party C++ public/private out-of-line definitions reported by
`tools/inventory_unreachable_symbols.py`, tests, project builds, governance
Owner: Owning source modules + reachability governance
Priority: Medium — follow-up discovered by Maths Surface Reachability MR2

## Problem And Evidence

The corrected post-MR1 Debug-and-Profile compiler-symbol scan reports 407
externally declared definitions with no production reference outside their own
translation unit:

- 299 have no reference;
- 60 are test-only;
- 41 are reachable only within an unrooted same-TU component;
- 7 are both same-TU and test-only;
- 320 map to one exact decorated MSVC symbol;
- 72 have an ambiguous source-to-symbol join;
- 15 have no emitted symbol mapping.

The current rows are exact-ruling evidence, not 407 defects and not an
allowance. Dynamic dispatch, callback registration, compiler-generated uses,
default arguments, and source-to-symbol uncertainty must be distinguished from
genuinely dead surface before deletion.

Evidence:
`Agentic/Reports/2026-07-30/maths-surface-reachability-closure.md` and
`tools/reachability_rulings.json`.

## Goal

Adjudicate every current reachability row with compiler/source evidence, delete
genuinely unreachable surface without compatibility aliases, retain only seams
with a named live owner and proof, and leave the inventory with zero unruled or
repair-plan rows.

## Non-Goals

- No bulk deletion from lexical evidence alone.
- No count threshold, target ratio, stale allowlist, or blanket retain ruling.
- No behavior, baseline, schema, shader, scene, or physics-golden change.
- No compatibility wrappers, deprecated aliases, forwarding declarations, or
  commented-out bodies.

## Phases

- [ ] **UR0 — Reproduce and partition all 407 rows.** Rebuild Debug and Profile,
  rerun the exact compiler-symbol scan, and create a checked inventory grouped
  by owner. Resolve all 87 missing/ambiguous mappings with decorated symbols,
  call-site compilation, callback/vtable evidence, or a scanner correction
  before judging the affected row.
- [ ] **UR1 — Remove no-reference production surface.** For each exact
  no-reference row, prove that no dynamic, exported, callback, or
  compiler-generated entry reaches it, then delete the declaration, definition,
  and dead tests/helpers. Record every retained exception with a concrete owner
  and caller mechanism.
- [ ] **UR2 — Resolve test-only and unrooted components.** Delete tests that
  manufacture reachability for retired APIs. Retain an explicit test seam only
  when its owning runtime invariant requires it and production deliberately
  does not call it. Collapse or internalize unrooted same-TU components after
  proving their root.
- [ ] **UR3 — Close the plan.** Replace every repair ruling with an exact
  retain-owner ruling or delete the row, complete the touched-source comment
  checklist, obtain independent ownership review, and run all mapped gates.
  Evidence:
  `Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md`.

## Dependencies And Decisions

- Starts after all four originally registered Claim Integrity plans so their
  planned source changes settle before the full inventory is adjudicated.
- This plan owns all 407 corrected MR2 rows. New rows introduced by later source
  changes require their own exact judgement in the touching plan.
- Every deletion is byte-exact for physics. Any changed physics byte proves a
  supposed dead edge was live and blocks the deletion.

## Acceptance

The compiler-backed inventory reports zero unruled, stale, or repair-plan rows.
Every retained row names a concrete owner and invocation mechanism. Every
deleted row leaves no alias or forwarding surface. Debug/Profile builds,
coverage floors, all CPU suites, and full validation pass with byte-exact
physics.

## Validation

`tools\validate_fast.bat`, direct reachability self-test and repository scan,
`tools\validate_coverage.bat`, `tools\validate_all_cpu_tests.bat`, and
`tools\validate_full.bat`. Add targeted owner tests where a retained indirect
seam lacks direct proof.

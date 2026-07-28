# Replay Restore And Wide-Signature RG1 Governance

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Phase: RG1 — qualitative wide-signature ruling gate

## Decision

An operation with 12 or more parameters now triggers mandatory qualitative
owner review. The trigger is not a maximum arity, accepted allowance, automatic
defect, frozen count, or ratio. A wider operation may pass when its current
ruling proves one cohesive owner operation; an exact-12 operation remains a
defect when the responsibilities are unowned.

## Current-Source Instrument

`tools/wide_signature_ownership_rulings.json` records the RG0 rulings for all 33
current exact-trigger operations:

- 28 `retain-owner` rulings; and
- 5 `repair-plan` rulings owned by RG2 of
  `Agentic/Plans/DONE/replay-restore-wide-signature-governance.md`.

Each entry matches both the source file and normalized signature. A changed or
new triggered signature becomes `UNRULED`; a deleted, moved, or narrowed
signature leaves `STALE-RULING`. Both conditions fail strict validation. Prior
report dispositions remain historical context and cannot satisfy the gate.

The five RG2 repair rulings pass the currentness gate because they name a
concrete active plan, not because the design has been accepted. RG2 must remove
or replace those exact signatures and delete their then-stale entries in the
same change.

## Review Contract

`AGENTS.md`, the repository rubber-duck skill, the Carmack-test skill, and the
C++ style guide now agree that reviewers must identify the concrete operation
owner, synchronous participant lifetime, banned courier/slice/callback/context
substitutes, extraction/destructuring evidence, and either a retain ruling or
active repair plan. A current ruling is evidence another reviewer can reject;
it is not immunity.

## Fixtures And Validation

The inventory self-test proves:

- a below-trigger signature is not gated;
- an exact-trigger signature without a ruling fails;
- ruled exact-trigger and above-trigger signatures pass;
- a signature change produces both an unruled current row and stale old ruling.

The fast validation gate runs the inventory self-test and strict repository scan
alongside the aggregate and extraction-scar gates.

Validation commands:

```powershell
python tools/inventory_wide_signatures.py --self-test
python tools/inventory_wide_signatures.py --repo . --threshold 12 --format json --strict
tools\validate_fast.bat
```

Results:

- inventory self-test: pass;
- strict repository scan: pass, 33 ruled trigger rows (`retain-owner` 28,
  `repair-plan` 5, unruled 0);
- `tools\validate_fast.bat`: pass in a detached clean snapshot, including
  formatting, project metadata, dependency graph, all three ownership
  inventories, Profile/Debug builds, and unit tests.

The main worktree format preflight separately reports only the owner-held
warm-start changes in `PersistentContactSolver.cpp` and
`PhysicsNarrowphaseStage.cpp`; RG1 did not edit, format, stage, or validate from
those files.

## Touched-File Comment Audit

The comment-audit skill inspected both source-bearing tool changes:

- `tools/inventory_wide_signatures.py`;
- `tools/validate_fast.bat`.

Checked: 2. Deferred: 0. Unchecked: none. A separate checklist plan is not
required for a touched-file audit. The inventory tool now defines owner ruling
and review-trigger vocabulary, states the exact-currentness and no-budget
invariants, explains stale-ruling detection locally, and points only to
resolving permanent `Related:` evidence. The fast gate explains why
wide-signature review is qualitative rather than a ceiling.

No runtime validation is required for this governance/tooling phase.

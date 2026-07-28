# Function Complexity Review Trigger

Date: 2026-07-29
Owner: skullbonez
State: In progress
Ledger tasks: 3 (CX0-CX2)
Branch: `nightrunner-29th-JUL-26`
PR: TBD

## Goal

Add a fourth repeatable inventory that reports function **size and nesting**
alongside the three existing shape inventories, on the identical
unruled-fails / ruled-passes contract, and install its owning rule in
`AGENTS.md`.

The Governance Review Model currently has three instruments —
`inventory_wide_signatures`, `inventory_authority_free_aggregates`, and
`inventory_extraction_scars` — and all three measure **shape**. None measures
extent. That gap is why a 1,721-line function in the most determinism-sensitive
code in the engine has never been reported by any gate.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f`.

Largest single function bodies in tracked first-party source:

| Function | Location | Lines | Closures | Max brace depth |
|---|---|---:|---:|---:|
| `PhysicsContactSolverStage::Solve` | `SkullbonezSource/Physics/PersistentContactSolver.cpp:121-1841` | 1,721 | 19 | 7 |
| `ParseAction` | `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp:1362` | ~1,096 | — | — |

`ParseAction` additionally carries 164 `if`/`else` branch keywords in one body.

Neither is reported by any current gate. Meanwhile `validate_fast` mechanically
blocks a two-member struct whose name ends in a candidate suffix and whose own
doc comment omits an `Invariant:` block. The instruments are calibrated to the
shapes the 2026-07 campaigns were repairing, not to the defects the current
tree actually has.

## Design Constraints

These are binding and follow directly from the Governance Review Model.

- **Not a budget.** No threshold in this tool is an allowance, a maximum, or a
  ratchet. A row at or above the trigger requires a current owner judgement; it
  is not automatically a defect, and a row below the trigger is not blessed.
- **Unruled fails, ruled passes.** Identical to
  `wide_signature_ownership_rulings.json`: a triggered function must match a
  current ruling by file and normalized identity. Editing the function
  invalidates its ruling. Deleting or shrinking it below the trigger makes the
  ruling stale, and stale rulings fail.
- **A ruling is a judgement, never a number-management device.** Dispositions
  are `retain-owner` (this body is one cohesive operation and splitting it would
  create a courier or forwarder) or `repair-plan` (naming the live plan that
  owns the decomposition). "It is long because it does a lot" is not a reason.
- **Reuse, do not fork.** Build on `tools/cpp_source_scan.py` and the existing
  ruling-file conventions. Do not add a second parser, a second rulings format,
  or a second `validate_*` entry point.
- **Two independent signals.** Body line count and maximum brace depth are
  reported separately. A 300-line flat dispatch table and a 300-line body nested
  seven deep are not the same object and must not collapse into one score.

## Ratified Owner Decision

CX0 recorded the trigger values before CX1 implementation. The owner ratified:

- body length trigger: **400 lines**
- nesting-depth trigger: **6**
- either signal alone triggers review

Rationale: 400 lines selects the 19-function body-length tail, depth 6
independently selects 28 nesting rows, and their 40-row union is 0.64% of the
6,285 recognized definitions. That is a bounded current-review surface while
preserving visibility into short but deeply nested bodies.

## CX0 Measurement Evidence

Report: `Agentic/Reports/2026-07-29/function-complexity-cx0-distribution.md`

- 6,285 tracked first-party function definitions were recognized; zero
  recognized definitions were left unpaired.
- The proposed 400-line signal selects 19 functions. The proposed depth-6
  signal selects 28. Their union is 40 functions, with 7 selected by both.
- Nearby combinations are published in the report: holding 400 lines and
  moving depth to 7 selects 24 rows, while depth 8 selects 21. Keeping depth 6
  preserves review visibility for the 16 additional depth-6 bodies.
- The current largest body is `ImGuiEditorOwner::BuildEditorShell` at 2,098
  lines and depth 8. `PhysicsContactSolverStage::Solve` is 1,721 lines, depth 7,
  and contains 28 lexically recognized closures.
- The earlier 19-closure observation for `Solve` is superseded by this complete
  current-tree measurement.

Owner ratification: **Ratified 2026-07-29.** The owner accepted 400 inclusive
body lines and maximum brace depth 6, with either independent signal triggering
qualitative review. These are review triggers, never maxima or allowances.

## CX1 Current-Ruling Evidence

- `tools/function_complexity_rulings.json` contains 40 exact file, normalized
  signature, and body-digest judgements: 38 `retain-owner` and 2 `repair-plan`.
- `PhysicsContactSolverStage::Solve` and `ParseAction` are the two campaign
  decomposition targets. Both route to
  `Agentic/Plans/TODO/contact-solve-phase-ownership.md`; strict mode verifies
  that repair plan exists.
- Every other triggered function carries a function-specific owner and cohesion
  reason. No row is justified by its measurement or by a generic count waiver.
- The self-test plants a new triggered function, an edited ruled body, a
  fabricated stale ruling, and a deleted ruled function; all four fail closed
  through their expected diagnostic class.
- Direct current-tree strict scan: 6,285 recognized definitions, 40 triggered,
  40 current rulings, zero scan or currentness diagnostics.

## Ledger

- [x] CX0 — Build `tools/inventory_function_complexity.py` in report-only mode
  over `cpp_source_scan` tracked files. Emit file, function identity, start
  line, body line count, maximum brace depth, and closure count. Publish the
  complete distribution and the candidate trigger set, and record the owner's
  ratified trigger values. No gate wiring in this task.
- [x] CX1 — Add `tools/function_complexity_rulings.json` on the wide-signature
  schema, seed a ruling for every currently triggered function, and add
  `--self-test` plus `--strict` modes with planted-drift, stale-ruling,
  edited-body, and deleted-function fixtures. Every seeded row for a function
  this campaign will decompose uses `repair-plan` naming its owning plan; every
  other row states a concrete cohesion reason.
- [ ] CX2 — Wire `--self-test` and `--strict` into `tools\validate_fast.bat`
  beside the three existing inventories, add the `AGENTS.md` rule text and the
  Governance Review Model table row, add the file-to-validation mapping row, and
  pass one independent review confirming the tool cannot be satisfied by
  renaming, by splitting a body across a helper that is immediately called once,
  or by adding a ruling that manages a number rather than records a judgement.

## Acceptance

- `python tools\inventory_function_complexity.py --self-test` and
  `--repo . --strict` both run inside `validate_fast`.
- An unruled triggered function fails `validate_fast`. A ruled one passes.
- Editing a ruled function's body invalidates its ruling and fails until the
  owner re-rules. Proved by a planted fixture, not asserted.
- `AGENTS.md` states the rule in the Governance Review Model with the same
  "current measurement requiring review, never an allowance" language as the
  other three inventories, and the reviewing skills that `AGENTS.md` delegates
  to are updated so the rule is stated where a reviewer actually reads it.
- No threshold anywhere in the tool or its rule text is phrased as a maximum,
  budget, or "no more than N".
- Zero source behavior change; no baseline, golden, or artifact moves.

## Validation

- Iteration: `python tools\inventory_function_complexity.py --self-test`.
- CX0: report only; no gate.
- CX1: `--self-test` plus fixture proofs.
- CX2: `tools\validate_fast.bat` (mapped: `tools/*` requires it, then run the
  changed script). Documentation-only `AGENTS.md` edits need no further gate,
  but the `validate_fast` run must occur after the wiring lands.

## Comment-Audit Checklist

- [x] `tools/inventory_function_complexity.py`
- [x] `tools/inventory_wide_signatures.py`
- [ ] `tools/validate_fast.bat`

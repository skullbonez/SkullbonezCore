# New Aggregate Ruling Gate Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `new-aggregate-ruling-gate` NA0-NA2

## Outcome

The bounded permanent gate is armed and the three-task plan is complete.
Suffix-free discovery now finds 1,176 data-bearing or bounded-suffix type
definitions. Strict mode gates the 86 legacy-suffix/no-invariant rows; all 86
carry an owner ruling, with zero unruled or ambiguous names.

The gate is intentionally name-scoped. A deliberately renamed
`FooFrameData` remains a human review question and is not claimed as a
mechanically closed case.

## Transition Retired

CA4 had already replaced the historical unreviewed inventory before NA0 began,
so the repository transition count was always zero. NA2 removed
`pre-existing-unreviewed` from the accepted verdicts and ruling schema. The
self-test proves an injected row using that verdict fails.

## Failure Proof And Hardening

A temporary seven-member `FooFrameContext` changed the inventory to 87 gated /
86 ruled and strict mode failed by naming the type and required ruling. The
probe was removed and the final source returned to 86/86.

Independent review found and drove closure of every demonstrated bounded
bypass:

- unqualified-name collisions;
- `final`, `alignas`, standard-attribute, export-macro, and `__declspec` heads;
- qualified out-of-class definitions and partial specializations;
- raw function-pointer and attributed fields;
- bitfields and anonymous unions;
- base-only aggregate inheritance;
- nested storage, macro-declared storage, and any suffix type whose direct
  member grammar resolves to zero;
- accidental `enum class` matches.

The final self-test plants each form. Duplicate bounded names fail rather than
sharing a ruling, while enum classes remain outside aggregate discovery.

## Ruling Review

The review rejected 54 generated “named boundary” restatements and five weak
carry-forward reasons. They now state concrete snapshot, transaction,
generation, serialization, arbitration, or lifetime rules that a reviewer can
dispute. The newly discovered `FramePresentationFacts` and the behavior-owning
platform `Input` class also carry current-source rulings.

The independent final verdict is **CLEAR — zero blockers**:

- every demonstrated bounded bypass fails strict mode;
- all 86 reasons are concrete and disputable;
- `pre-existing-unreviewed` is zero and unusable;
- no count, ratio, member budget, or allowance threshold was introduced.

## Validation

- Planted `FooFrameContext`: strict scan exits 1 with
  `require an owner ruling ... FooFrameContext`.
- `python tools\inventory_authority_free_aggregates.py --self-test`: passes.
- Final strict scan: 1,176 discovered, 86 gated, 86 ruled, zero unruled,
  ambiguous, or transitional.
- `tools\validate_fast.bat`: all eight stages pass; Profile and Debug build
  with zero warnings/errors.
- `tools\validate_all_cpu_tests.bat`: all six lanes pass; 418/418 doctests and
  2,410,186 assertions pass.

No engine behavior, physics/DX12/replay baseline, count threshold, or budget was
changed.

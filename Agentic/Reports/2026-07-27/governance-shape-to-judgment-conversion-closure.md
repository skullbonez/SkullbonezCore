# Governance Shape-To-Judgment Conversion Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `governance-shape-to-judgment-conversion` G0-G4

## Outcome

The governance regime now asks repeatable ownership questions instead of
matching preferred or forbidden nouns. The two shape inventories are
self-tested, run by `validate_fast`, and fail only when a mechanically
signalled row lacks an owner judgement. The wide-signature inventory remains
review evidence governed by the standing 12-parameter ceiling; it does not
pretend to share the ruling-file gate.

The G4 independent review initially blocked closure four ways: the three-tool
contract was false, two single-reference graph wrappers carried unjustified
retain rulings, aggregate discovery was suffix/`struct` evadable, and the
extraction scanner both missed block-scope declaration forms and admitted a
namespace global. All four were corrected and adversarial fixtures were added.
A follow-up review found one stale CA3 non-goal; that documentation was corrected
and the final spot check returned `ZERO BLOCKERS`.

## Final Inventory Evidence

| Inventory | Final result | Gate meaning |
|---|---|---|
| Authority-free aggregates | 1,205 data-bearing types discovered; 10 borrowed couriers signalled; 10 ruled; 0 unruled | Suffix-independent `struct`/`class` discovery; comment-only invariants cannot suppress courier findings |
| Extraction scars | 89 findings; 89 ruled; 0 unruled | Function-block only; covers statement, `if`/`for`, direct-reference, and structured-binding forms |
| Wide signatures | Current operation table emitted successfully | Review evidence; prior dispositions are inline and the 12-parameter ceiling is binding |

Both inventory self-tests pass. Aggregate fixtures pin suffix renaming,
`struct`-to-`class` evasion, comment-only invariant evasion, one-field behavior
owners, and strong scalar values. Extraction fixtures pin `if`/`for`
initializers, direct reference initialization, structured bindings, and
namespace-global exclusion in addition to the original positive/negative
declaration cases.

## Final Ruling Table

| Verdict | Rows | Owner |
|---|---:|---|
| `remove` | 2 | `ceremonial-aggregate-elimination` CA1 |
| `remove` | 3 | `ceremonial-aggregate-elimination` CA2 |
| `remove` | 5 | `ceremonial-aggregate-elimination` CA3 |
| `repair` | 88 | `extraction-scar-remediation` ES0 |
| `retain` | 1 | Core threading: forwarding-reference materialisation required for reference capture |

Every row has a non-empty owner and reason. The duplicate
`GeometricMath.cpp:m_normal` site key represents two source findings owned by
the same ES0 ruling; no repair/remove finding is unowned.

## Plan-To-Row And Scope Coverage

| Scope | Owning plan/task |
|---|---|
| UI command borrowed couriers (2) | `ceremonial-aggregate-elimination` CA1 |
| Scene setup/runtime borrowed couriers (3) | `ceremonial-aggregate-elimination` CA2 |
| Assets/render/replay borrowed couriers (5), including the two G4-reopened graph wrappers | `ceremonial-aggregate-elimination` CA3 |
| Extraction scars (88) | `extraction-scar-remediation` ES0 |
| Four Runtime frame views | `runtime-frame-view-retirement` |
| `RuntimeRenderBackendView` | `render-backend-service-bag-removal` |
| Operator-command operation family | `operator-command-invariant-ownership` |
| Coverage-gate-named test ownership | `coverage-gate-test-reorganization` |

The one-shot `tools/generate_aggregate_rulings.py` seed generator was deleted at
G4 as its own header required. Surviving tool `Related:` entries point to this
permanent report rather than the deleted live plan.

## Independent Review

| Run | Result |
|---|---|
| `governance-shape-to-judgment-conversion-duck-01` | Four blockers: false wide-signature gate claim, two waived graph couriers, rename-evadable aggregate scan, incomplete extraction grammar |
| `governance-shape-to-judgment-conversion-duck-02` | Technical blockers resolved; one stale CA3 non-goal remained |
| `governance-shape-to-judgment-conversion-duck-03` | `ZERO BLOCKERS` |

Final ownership answers:

1. Aggregate ownership: all ten mechanically signalled couriers are remove rows
   owned by CA1-CA3; behavior owners and strong scalar types are not
   misclassified.
2. Capability slices: the four known frame views remain explicitly owned by the
   concrete-operands `runtime-frame-view-retirement` plan; no new slice appeared.
3. Extraction scars: 88 repair findings are owned by ES0; the single retain is a
   language-required forwarding-reference binding.
4. Rename evasion: aggregate discovery ignores suffix and `struct`/`class`
   spelling; the review criteria judge ownership shape.
5. False claims: the wide-signature contract and the CA3 non-goal were corrected;
   the final review found none remaining.

## Comment Audit

Touched-file audit inventory: the 29 files changed across G0-G4, including
`AGENTS.md`, all affected plans/reports/reference/skill files, five surviving
tool artifacts, `validate_fast.bat`, the ruling JSON, and the deleted one-shot
generator. Checked: 29. Deferred: 0. Unchecked: none.

The surviving substantial Python tools and batch gate have learning headers.
Ownership, sequencing, scanner-scope, and gate claims were checked against the
post-change implementation. Durable `Related:` paths now target permanent
reports. No separate checklist plan was required because this was a touched-file
audit rather than a subsystem/repository-wide comment campaign.

## Validation

Final command evidence is recorded here after the required gates run from the
closure source state:

- `tools\validate_fast.bat` — PASS in 112.9 s. Both inventory self-tests
  passed; the aggregate scan reported 1,205 candidates / 10 signalled / 10
  ruled / 0 unruled; the scar scan reported 89 findings / 89 ruled / 0
  unruled; formatting, metadata, dependency, project/filter, Profile, and Debug
  gates all passed with zero build warnings or errors.
- `tools\validate_all_cpu_tests.bat` — PASS in 60.4 s. All six umbrella lanes
  passed; 402 doctests / 2,403,914 assertions passed, every ratified coverage
  floor passed, and the runtime interaction, scene parser, UI boundary, and
  DX12 architecture test suites passed.

No engine behavior, baseline, golden, scene, schema, or config changed.

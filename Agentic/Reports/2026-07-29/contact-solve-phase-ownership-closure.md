# Contact Solve Phase Ownership Closure

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Plan: `contact-solve-phase-ownership`
Final ledger: 5/5 complete; removed from the live denominator under MASTER rule 4

## Outcome

`PersistentContactSolveTransaction` now enforces the complete thirteen-phase
contact-solve order with a fatal illegal-transition lane. It owns the
solver-body working set and shared impulse arithmetic without retaining a
caller store, span, diagnostics object, stage, callback pack, context bag, or
capability-slice surface.

`PhysicsContactSolverStage::Solve` is a 108-body-line, depth-3 sequencer. The
two larger cohesive construction phases retain exact current-body owner
rulings; every other transaction phase is below the ratified 400-body-line and
brace-depth-6 review triggers. All eighteen profiler strings remain unchanged.

The preserved CS0 two-run oracle remains 88,802 rows and 12,660,434 bytes at
SHA-256
`8e9092cb7f28eafc0d9f167e90cf9d5292d022485d6ae93d591fb758caea6387`.
Every Physics gate remained byte-exact. No baseline, golden, config, schema,
allowlist, or committed runtime artifact was refreshed.

## CS4 Parser Closure

`ParseAction` is now a 21-body-line, depth-3 ordered dispatch over 34
per-action parser functions. The table preserves the original first-recognized
key precedence for malformed multi-key objects.

Assertion parsing is divided into basic, Replay/Prediction, and Runtime/UI
domains. The schema/type guard remains ahead of every `get<T>()` under
`JSON_NOEXCEPTION`; unknown assertions retain the original rejection path.
Read-only token comparison against the parent source proved:

- all 34 top-level action keys remain in identical order;
- all 33 non-assert action bodies are token-identical after removing their
  former `entry.contains(...)` wrappers;
- all 79 assertion branches retain their order, assignments, types, and error
  behavior after replacing `member.value()` with the explicit `expected`
  parameter and returning a typed parse status; and
- all 51 parser error literals remain unchanged.

`ParseReplayAssertion`, the largest new parser, is 353 body lines at depth 2.
The obsolete `ParseAction` repair ruling is deleted. No complexity
`repair-plan` ruling remains.

## Validation

| Command | Result |
|---|---|
| Profile x64 solution build | PASS; zero warnings/errors |
| `SKULLBONEZ_TESTS.exe --test-case='Runtime interaction:*'` | PASS; 2/2 cases, 33/33 assertions |
| `python tools/inventory_function_complexity.py --repo . --body-trigger 400 --depth-trigger 6 --strict` | PASS; 6,355 functions, 40/40 triggered bodies ruled |
| `python tools/inventory_authority_free_aggregates.py --repo . --strict` | PASS; 85/85 bounded rows ruled |
| `python tools/inventory_extraction_scars.py --repo .` | PASS; only the unrelated pre-existing ruled `WorkerPool::indexFn` finding |
| `python tools/inventory_wide_signatures.py --repo . --threshold 12 --strict` | PASS; every triggered signature has a current ruling |
| `tools\validate_all_cpu_tests.bat` | PASS; all six CPU lanes |
| `tools\validate_physics_deep.bat` | PASS; deep queries and Physics outputs byte-exact |
| `tools\validate_perf.bat` | PASS; allocation, structural, absolute-budget, and baseline comparisons clear |
| `tools\validate_full.bat` | PASS; 440/440 tests, 2,420,840 assertions, runtime/renderer lanes, and byte-exact Physics |
| `git diff --check` | PASS |

The first performance-gate attempt found one Automation-only warnings-as-errors
failure because `ParseReplayAssertion` named an unused error parameter. Removing
the unused name repaired the local compile defect; the unchanged gate then
passed. An earlier large-patch context mismatch and an ambiguous worker build
handoff were non-terminal orchestration events; neither changed source
semantics nor suppressed a validation result.

## Independent Ownership Review

The fresh read-only reviewer returned zero blocking and zero non-blocking
findings. Its five required answers were:

1. zero authority-free aggregates added or exposed;
2. zero whole-surface capability-slice operations;
3. zero extraction scars in the changed surface;
4. zero renamed bag/context shapes; and
5. zero false header or local invariant claims.

The review separately confirmed the action/assertion token equivalence,
fixed-precedence dispatch, `JSON_NOEXCEPTION` safety, current complexity
rulings, and stale-ruling removal. The review tool did not expose token or
elapsed-time accounting.

## Comment Audit

The plan checklist is complete at 9/9 inspected, zero deferred:

- `SkullbonezSource/Physics/PersistentContactSolver.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PhysicsFixedList.h`
- `SkullbonezSource/Physics/ContactSolverCommon.h`
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `SkullbonezTests/TestRuntimeContracts.cpp`

The transaction, solver phase, parser-dispatch, and assertion-domain comments
match their post-change owners and sequencing. All repository-relative
`Related:` paths resolve.

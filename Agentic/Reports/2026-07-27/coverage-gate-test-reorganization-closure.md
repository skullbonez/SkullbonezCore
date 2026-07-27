# Coverage Gate Test Reorganization — Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: 3/3 phases complete

## Outcome

The gate-named test collection is gone. Every behavioral case now lives in its
subsystem test file, with a value-only shared fixture header for the two
collision-shape constructors used across owners. No engine source, coverage
floor, exclusion, schema, config, baseline, or golden changed.

CG2 added the permanent rule to
`Agentic/Reference/code-style-guide.md`: test files are named for the subsystem
they pin, never for a gate, metric, campaign, or plan. The exact companion rule
was already present in the amended `AGENTS.md` Reviews section, so CG2 verified
and retained it instead of duplicating it.

## Before / After Coverage

| Subsystem | Before | After | Delta |
|---|---:|---:|---:|
| maths | 86.60% | 86.60% | 0.00 pp |
| core_primitives | 88.39% | 88.39% | 0.00 pp |
| physics_stores | 76.90% | 76.90% | 0.00 pp |
| physics_stages_and_solver | 80.37% | 80.37% | 0.00 pp |
| replay_artifact_codecs | 76.34% | 76.34% | 0.00 pp |
| startup | 91.64% | 91.64% | 0.00 pp |
| config_and_schema | 94.84% | 94.84% | 0.00 pp |
| runtime_input_and_interaction | 76.25% | 76.25% | 0.00 pp |
| scene_logic | 97.44% | 97.44% | 0.00 pp |
| replay_value_seams | 84.49% | 84.49% | 0.00 pp |

Whole instrumented output stayed 21,044 / 28,282 lines (74.41%). The moved
cases retain all 231 focused assertions, and the complete runner retains
418/418 cases and 2,410,159/2,410,159 assertions.

## Validation And Review

- Direct `tools\validate_coverage.bat`: PASS at all unchanged floors.
- `tools\validate_all_cpu_tests.bat`: PASS in all six lanes:
  doctest/coverage, runtime interaction, scene parser, UI boundary, and DX12
  architecture.
- `tools\validate_format.bat`: PASS.
- Project/filter validation: 114/114 items, zero errors.
- Exact-prefix test filename census for Coverage/Gate/Metric/Plan: zero.
- Independent review: CLEAR, zero blocking or non-blocking findings and no
  missing evidence. Detailed evidence:
  `coverage-gate-test-reorganization-cg2-independent-review.md`.

## Phase Evidence

- CG0: `coverage-gate-test-reorganization-cg0-map.md`
- CG1: `coverage-gate-test-reorganization-cg1-move.md`
- CG2 review: `coverage-gate-test-reorganization-cg2-independent-review.md`

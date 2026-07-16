# Unit Coverage Baseline And Floor Ratification

Date: 2026-07-16  
Campaign task: U0  
Measured source state: `37b56828` (`nightrunner-16th-july`), after the bounded
Debug-prerequisite fix that sizes sleep visual-id rows before their first mirror.

## Measurement

Command:

```bat
tools\validate_coverage.bat
```

Tool: OpenCppCoverage 0.9.9.0. The lane builds and runs
`Debug\SKULLBONEZ_TESTS.exe`, filters source to `SkullbonezSource`, exports
Cobertura XML to `TestOutput/coverage/coverage.xml`, and applies the versioned
policy in `tools/coverage_floors.json`.

One pre-existing case is explicitly excluded during U0:
`Persistent contact solver: a box gains sleep support only after toppling from
its edge`. Debug instrumentation moves its fixture across a manifold feature
selection boundary, causing its `BuildObjectContactManifold` precondition to
fail. U4 owns moving that fixture into the documented tolerance-safe band and
removing the exclusion. The ordinary Profile `validate_tests` lane continues
to run that case.

| Subsystem | Tier | Covered / instrumented | Line coverage | U0 active floor | Ratified U9 floor |
|---|---:|---:|---:|---:|---:|
| `maths` | 1 | 498 / 667 | 74.66% | 0% | 85% |
| `core_primitives` | 1 | 590 / 1,038 | 56.84% | 0% | 85% |
| `physics_stores` | 2 | 856 / 1,638 | 52.26% | 0% | 70% |
| `physics_stages_and_solver` | 2 | 2,452 / 4,544 | 53.96% | 0% | 70% |
| `replay_artifact_codecs` | 2 | 0 / 0 | not yet instrumented | 0% | 70% |
| `startup` | 2 | 0 / 0 | not yet instrumented | 0% | 70% |
| `config_and_schema` | 2 | 398 / 422 | 94.31% | 0% | 70% |
| `runtime_input_and_interaction` | 3 | 366 / 410 | 89.27% | 0% | 50% |
| `scene_logic` | 3 | 35 / 36 | 97.22% | 0% | 50% |
| `replay_value_seams` | 3 | 678 / 2,028 | 33.43% | 0% | 50% |

Whole instrumented product output, after the versioned Tier-4 exclusions, is
11,008 / 20,653 lines (53.30%). This is an output, never a goal or gate. It is
also deliberately described as *instrumented product output*: startup and
artifact-codec product paths are not linked into the U0 test runner yet and
therefore do not appear in the Cobertura denominator. Their honest U0 result is
"not yet instrumented", not 100% and not an invented physical-line estimate.

This mechanical measurement supersedes the plan's historical ~4% test-lines to
product-lines estimate; the two quantities use different denominators and must
not be compared as if they were the same metric.

## Floor Ratification

The owner-default tier map is ratified without amendment:

- Tier 1: Maths and core primitives, 85% line coverage.
- Tier 2: physics stores/stages/solver helpers, replay artifact codecs,
  startup, and config/schema paths, 70% line coverage.
- Tier 3: runtime input/interaction, scene logic, and replay value seams, 50%
  line coverage.
- Tier 4: DX12, UI drawing, Win32/window, presentation submission, replay
  validation harnesses, and startup probe harnesses have no unit floor. Their
  behavior remains owned by screenshot, InfoQueue, stress, replay mega, and
  automation gates.

The versioned Tier-4 exclusion globs live in `tools/coverage_floors.json`.
Changing them requires owner review; the checker contains no hidden exclusion
list.

Governance ruling: coverage floors are a quality gate, not a frozen-count debt
ratchet; the AGENTS.md ban on ratchets for migration vocabulary does not apply.

U0 found no Tier-2 scope that requires a product-source test seam. The startup
and artifact-codec owners need test-project linkage and behavioral cases in U5
and U6, not production reach-back. Reserved row U10 therefore remains uncounted.

## U0 Gate Evidence

- `python tools\check_coverage.py --self-test`: path normalization, duplicate
  line merge, exclusion, passing floor, and failing floor cases passed.
- `tools\validate_coverage.bat`: Debug build succeeded; 206 covered test cases
  ran with the one documented U4-owned exclusion; Cobertura export and
  report-only floor check passed in 2.33 seconds.
- `tools\validate_fast.bat`: formatting, metadata, staged-size policy,
  zero-warning Profile/Debug builds, and all 207 doctest cases (17,353
  assertions) passed in 49.24 seconds.

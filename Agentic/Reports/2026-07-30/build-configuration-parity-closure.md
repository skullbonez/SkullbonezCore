# Build Configuration Parity Closure

Date: 2026-07-30

Branch: `nightrunner-30th-JUL-26`

Plan: completed BP0-BP5 and removed from `Agentic/Plans/TODO/` under inventory
rule 4.

State: Complete

## Acceptance

- BP0 inventories 313 source paths and 1,640 effective compile rows across the
  five first-party projects. Sixty-one sources are shared between projects.
  The four JSON-bearing shared translation units contain no category-(c)
  accessor: self-produced JSON is structurally safe, and external authored
  values pass through the existing schema or local validation owners.
- BP1 defines `JSON_NOEXCEPTION` in every tests-project configuration. Shared
  engine translation units now use production nlohmann failure semantics while
  doctest retains `/EHsc`.
- BP2 required no source or test repair because BP0 found zero unvalidated
  external-JSON accessors.
- BP3 makes all seven ImGui/Tracy per-file forced-include overrides append
  `%(ForcedIncludeFiles)`. Compiler command evidence shows both
  `DevelopmentToolsCapability.h` and `FloatingPointContract.h` reaching those
  translation units.
- BP4 adds a fail-closed five-project consistency checker and exact current-tree
  owner rulings. The live scan reports 1,640 compile rows, zero dropped list
  inheritance, 474 raw per-configuration observations, 122 exact ruled
  fingerprints, and zero diagnostics.
- BP5 reconciles the five-inventory governance text, wires the self-test and
  repository scan into `validate_fast`, documents the utility, audits both
  touched source-bearing tools, and closes independent review.

The 122 fingerprints are the two intentional setting differences for each of
the 61 shared sources: doctest's retained exception machinery and the
project-role preprocessor definitions. They are exact current-source judgements,
not an allowance or count budget.

## JSON Accessor Census

The reproducible per-file table, canonical compile-row digest, 61-path shared
inventory, and exact accessor classification are in
`build-configuration-parity-bp0-census.md`.

`SceneSnapshotWriter` and the replay manifest read only JSON values constructed
within their own writers. `DemoDirector` validates external shot-list shape and
types before access. Startup launch resolution parses with exceptions disabled
and validates presence and type before extraction. No malformed authored file
can select an unvalidated nlohmann accessor in the four scoped translation
units, so no new parser entry point or regression was warranted.

## Independent Review

One read-only rubber-duck reviewer performed three passes. The first found that
project-default list metadata could drop inherited values without detection and
also identified one malformed governance sentence plus incomplete accessor-site
evidence. The second found the remaining explicit-empty project-default
false-pass. The checker now distinguishes absent metadata from a declared empty
element and its self-test plants both non-empty and explicit-empty default
overrides. The third pass returned `NO BLOCKER`.

No C++ source changed. The C++ aggregate, capability-slice, extraction-scar,
wide-signature, function-complexity, hot-path allocation, and dependency-owner
questions therefore have no new production surface to adjudicate. The new
checker itself owns one cohesive synchronous operation: resolve project
metadata, compare effective settings, and require exact current-tree rulings.

## Comment Audit

The touched source-bearing inventory is complete at 2/2:

- `tools/check_build_config_consistency.py`
- `tools/validate_fast.bat`

Both files were inspected against
`Agentic/Reference/comment-style-guide.md`. No file was deferred. The remaining
touched files are project metadata, JSON data, or documentation.

## Validation

| Command | Result |
|---|---|
| `tools\validate_fast.bat` | PASS on the final commit candidate in 211.6 s; all eight stages, Profile tests, and ready builds passed |
| `python tools\check_build_config_consistency.py --self-test` | PASS |
| `python tools\check_build_config_consistency.py --repo . --format json` | PASS in the same 1.0 s direct run; 1,640 rows, zero drops, zero diagnostics |
| `tools\validate_all_cpu_tests.bat` | PASS in 118.6 s; all six CPU suites passed |
| `tools\validate_full.bat` | PASS in 359.9 s; Profile, Automation, and Debug ready, all required lanes passed, and the 44,401-line physics CSV remained byte-exact |
| `git diff --check` | PASS |

The first fast-gate attempt ended with a diagnostic-free parallel `CL.exe` code
1 while compiling the tests project. A serialized diagnostic rebuild of the
complete Profile test target passed in 14.3 s, an implementation-tree fast gate
passed in 227.1 s, and the final commit-candidate fast gate passed in 211.6 s.
No compiler error, source defect, or configuration rollback was hidden.

Validation logs and the direct JSON report are under the ignored
`TestOutput/validation/build_configuration_parity/` directory.

## Rubber-Duck Accounting

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---|---|---|
| Build Configuration Parity | `build-configuration-parity-duck-01` | `/root/build_config_parity_duck_01` | Initial closure review | unavailable | unavailable | n/a | unavailable | Blockers | Fixed project-default inheritance, governance prose, and accessor evidence |
| Build Configuration Parity | `build-configuration-parity-duck-02` | `/root/build_config_parity_duck_01` | Review after first fixes | unavailable | unavailable | n/a | unavailable | Blocker | Fixed explicit-empty project-default false-pass |
| Build Configuration Parity | `build-configuration-parity-duck-03` | `/root/build_config_parity_duck_01` | Final narrow verification | unavailable | unavailable | n/a | unavailable | No blocker | None |

The collaboration interface did not retain raw prompt/response strings with
guaranteed whitespace boundaries after context compaction and exposed neither
per-pass token usage nor aggregate elapsed time. These fields are reported as
unavailable rather than estimated.

## Baselines And Residual Risk

No engine behavior, schema, scene, shader, golden, or baseline changed. The
physics regression remained byte-exact. The only retained differences are the
exact ruled tests-versus-engine settings above; any spelling or effective-value
change invalidates its fingerprint and fails `validate_fast`.

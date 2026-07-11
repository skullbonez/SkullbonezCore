# Validation V4 Native Diagnostics Evidence

Date: 2026-07-10
Owner: repository validation
Plan phase: `validation-gate-integrity.md` V4

## Result

V4 now has one opt-in entry point:

```bat
tools\validate_native_diagnostics.bat
```

The default command runs two bounded, CPU-only lanes without launching the
engine:

1. An isolated MSVC AddressSanitizer build of `SKULLBONEZ_TESTS` and its
   `SKULLBONEZ_MATHS`/`SKULLBONEZ_PHYSICS` project references. This covers the
   main doctest files plus the engine translation units compiled into that test
   executable.
2. MSVC `/analyze` over the five `SKULLBONEZ_MATHS` translation units. The lane
   requires native-analysis sidecars, so an ignored or misspelled compiler
   property cannot produce a false pass.

Temporary MSBuild settings redirect every output and intermediate directory to
`TestOutput/validation/native_diagnostics`. The developer's normal Debug and
Profile binaries are not overwritten.

`.github/workflows/native-diagnostics.yml` runs the same lane with
`--prove-asan-fixture` every Monday at 05:17 UTC and on manual dispatch. It is
an informational scheduled safety signal, not a pull-request check. The first
real hosted run remains required before its image/toolchain availability can be
cited as evidence.

The workflow passed `actionlint` v1.7.12 locally. The downloaded official
Windows archive matched SHA-256
`6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9`;
the workflow lint exit was zero. A separate YAML structural assertion proved
that its only triggers are `schedule` and `workflow_dispatch`, repository
permission is read-only, and the job uses a hosted Windows runner.

## AddressSanitizer Detector Proof

Command:

```bat
tools\validate_native_diagnostics.bat --prove-asan-fixture
```

Final focused run, 2026-07-10:

```text
ASan injected-fixture build: exit 0, 1.902s
ASan injected-fixture run (failure expected): exit 3, 0.706s
PASS: injected fixture exited 3 and reported heap-use-after-free.
ASan CPU-test build: exit 0, 9.537s
ASan CPU-test run: exit 0, 1.223s
Healthy ASan CPU lane: 10.759s
MSVC static analysis (SKULLBONEZ_MATHS): exit 0, 2.157s
Static-analysis baseline: 0 warning(s), 0 governed suppression(s).
Static-analysis evidence: 5 native analysis sidecar(s).
PASS: native diagnostics completed in 15.672s
```

After adding the fresh-sidecar guard, the affected lane was rerun directly:

```text
MSVC static analysis (SKULLBONEZ_MATHS): exit 0, 2.121s
Static-analysis baseline: 0 warning(s), 0 governed suppression(s).
Static-analysis evidence: 5 native analysis sidecar(s).
PASS: native diagnostics completed in 2.249s
```

Coordinator review found one potential false-pass edge: the persisted log was
bounded before diagnostic parsing, so an exceptionally large build could place
a warning in the omitted middle window. Detection now scans complete captured
output and bounds only the on-disk transcript. The final full proof after that
fix passed:

```text
ASan injected-fixture build: exit 0, 1.899s
ASan injected-fixture run (failure expected): exit 3, 1.511s
PASS: injected fixture exited 3 and reported heap-use-after-free.
ASan CPU-test build: exit 0, 9.621s
ASan CPU-test run: exit 0, 1.902s
Healthy ASan CPU lane: 11.524s
MSVC static analysis (SKULLBONEZ_MATHS): exit 0, 2.111s
Static-analysis baseline: 0 warning(s), 0 governed suppression(s).
Static-analysis evidence: 5 native analysis sidecar(s).
PASS: native diagnostics completed in 17.186s
```

The proof log contains both:

```text
ERROR: AddressSanitizer: heap-use-after-free
SUMMARY: AddressSanitizer: heap-use-after-free
```

The fixture is not a tracked project or source file. It is generated only when
`--prove-asan-fixture` is present, inside a process-scoped temporary directory
under ignored `TestOutput`, then its source, executable, objects, and PDB are
deleted before the command returns. Normal builds have no path to the defect.

Coordinator hardening then removed MSBuild's blanket warnings-as-errors switch
from the static-analysis invocation only. Exact governed warning rows can now
actually pass after matching; ASan builds retain warnings-as-errors. Any
compiler, linker, or MSBuild warning that does not match the exact native
`path(line): warning Cnnnn` form fails closed rather than bypassing the
baseline. The log marker was also moved inside the 240,000-character budget so
the persisted cap is exact. The tool self-test passed, and the affected lane
was rerun from final code:

```text
SELF_TEST_PASS: native diagnostics parser and log guards passed
MSVC static analysis (SKULLBONEZ_MATHS): exit 0, 2.425s
Static-analysis baseline: 0 warning(s), 0 governed suppression(s).
Static-analysis evidence: 5 native analysis sidecar(s).
PASS: native diagnostics completed in 2.548s
```

Final pre-commit rerun after formatting the generated fixture project and
integrating the new CPU-only owners:

```text
ASan injected-fixture build: exit 0, 1.882s
ASan injected-fixture run (failure expected): exit 3, 0.675s
PASS: injected fixture exited 3 and reported heap-use-after-free.
ASan CPU-test build: exit 0, 9.760s
ASan CPU-test run: exit 0, 1.558s
MSVC static analysis (SKULLBONEZ_MATHS): exit 0, 2.170s
Static-analysis baseline: 0 warning(s), 0 governed suppression(s).
Static-analysis evidence: 5 native analysis sidecar(s).
PASS: native diagnostics completed in 16.185s
```

## Healthy-Lane Evidence

The instrumented doctest executable completed:

```text
[doctest] test cases:   78 |   78 passed | 0 failed | 0 skipped
[doctest] assertions: 1883 | 1883 passed | 0 failed |
[doctest] Status: SUCCESS!
```

The static-analysis build compiled `GeometricMath.cpp`, `Matrix4.cpp`,
`Quaternion.cpp`, `RotationMatrix.cpp`, and `Vector3.cpp`. It produced five
`.nativecodeanalysis.xml` sidecars and no warnings.

Bounded evidence logs:

- `TestOutput/validation/native_diagnostics/asan_fixture/build.log`
- `TestOutput/validation/native_diagnostics/asan_fixture/run.log`
- `TestOutput/validation/native_diagnostics/asan/build.log`
- `TestOutput/validation/native_diagnostics/asan/run.log`
- `TestOutput/validation/native_diagnostics/static_analysis/build.log`

Each log is capped at 240,000 characters using a retained head/tail window.

## Suppression Governance

`tools/native_diagnostics_suppressions.json` is the owned static-analysis
baseline. It currently has zero rows. The runner rejects:

- wildcard paths or warning codes;
- entries missing owner, reason, deletion condition, or review evidence;
- warnings without an exact path/code match; and
- stale rows that no longer match emitted diagnostics.

No sanitizer or static-analysis finding is suppressed by this change.

## Honest Limits

- The ASan lane covers CPU-testable code reachable through
  `SKULLBONEZ_TESTS`; it does not exercise the DX12 device/runtime or a full game
  launch.
- Static analysis is deliberately bounded to the maths library's five
  translation units. Expanding it should happen owner-by-owner so findings are
  fixed or represented by exact governed baseline rows.
- This is an opt-in safety lane with scheduled/manual CI configuration, not
  part of the mandatory broad PR gate. The workflow is not proven until a real
  hosted run completes.
- `tools\validate_fast.bat` passed from the final combined worktree: formatting,
  production project/filter integrity, the 14-blob staged-size check, Profile
  build, and Debug ready build all completed with zero warnings and errors.

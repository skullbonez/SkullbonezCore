# Box Vibration And Warm-Start Integrity — BV5

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Task: BV5 — position-correction divisor

## Outcome

The direct position-correction pass now shares one correction budget across all
rows in a contact manifold:

`penetrationError * correctionPercent / (totalInverseMass * manifoldPointCount)`

Before BV5, every row reused its build-time penetration and applied the full
correction. A four-point face could therefore remove four times the authored
percentage against stale geometry. The shared pass covers both object and
terrain rows, and both builders already populate `manifoldPointCount`.

The new deterministic oracle compares one-point and four-point terrain
manifolds with the same `0.2` penetration:

| Measurement | One point | Four points |
|---|---:|---:|
| Correction rows | 1 | 4 |
| Total correction magnitude | 0.16 | 0.16 |
| Maximum per-row correction | 0.16 | 0.04 |
| Body displacement | 0.08 | 0.08 |

The old implementation makes the four-point total and displacement four times
the one-point result; the new test therefore fails on the retired behavior.

## Controlled Vibration Result

The final BV5 Debug binary preserves the controlled acceptance metric:

- zero downward-to-upward flips;
- zero affected bricks;
- zero frames at the 12-iteration cap;
- minimum one solver iteration;
- zero cache misses across 1,800 lookups;
- zero maximum absolute vertical velocity in frames 300-1199.

The CSV changes from BV4 because BV5 intentionally changes early position
repair, but two independent BV5 runs are byte-identical:

```text
Debug/bv5_box.csv
Debug/bv5_box_repeat.csv
Bytes each: 841,021
SHA-256: D2A4D076629851B12FF01585BC60B2E41B7198C0CB9AEF63468DDB17ED9585C3
```

## Bounded-Divergence Inspection

Baseline regeneration remains deferred until BV6's final Debug source state.

- `physics_regression_varied.csv`: 35,091 of 44,401 lines differ per run,
  beginning at line 1,276. The two 44,401-line runs are identical to each
  other.
- `shooting_reaction_volley.csv`: the same 28 of 641 lines differ; the semantic
  checker still passes all ten targets.
- All three bullet sweeps and `space_three_body_chaos.csv` remain byte-exact.
- Of the four known-issue artifacts, only stacking moves beyond BV3:
  `physics_known_stacking.csv` retains 22,501 lines and becomes 3,068,630 bytes,
  SHA-256
  `BB53C9FD7BE85931A9FC17197036C220052017BBB639365179C2E4892D3DD975`.
  Seeded solver distribution, at-rest settling, and terrain contact retain
  their BV3 hashes.
- Every changed physics CSV contains no `NaN`/`Inf`.
- No baseline or golden file was regenerated.

## Comment Audit

Touched-file inventory: 2/2 checked, 0 deferred:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`

The production comment names the build-time penetration invariant and explains
why one manifold owns one positional repair budget. The fixture comment explains
the symmetric four-point geometry and equal stale penetration. Both existing
learning headers remain complete and their `Related:` paths resolve.

## SkullScope Accounting

Trace command:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/box_vibration_t0.scene.json `
  --physics-diag Debug/bv5_box.physicsdiag.ndjson `
  --physics-regression-log Debug/bv5_box.csv
```

Artifacts:

- `Debug/bv5_box.physicsdiag.ndjson`: 7,393,737 bytes.
- `Debug/bv5_box.physicsdiag.sqlite`: 3,493,888 bytes.

Query read by GPT:

- `tools\physics_query.bat Debug\bv5_box.physicsdiag.ndjson sql "<BV0 one-row SQL>" --limit 5`
  — 559 characters / 559 bytes.

Total GPT-read SkullScope output: 559 characters / 559 bytes. Raw NDJSON,
SQLite, and CSV artifacts were not read by GPT. Query output was not truncated.

## Validation

- `tools\validate_format.bat`: pass across 573 source files and 319 headers.
- `tools\validate_build.bat Profile`: pass, zero warnings/errors.
- `Profile\SKULLBONEZ_TESTS.exe "--test-case=Persistent contact solver:*"`:
  12/12 cases and 174/174 assertions.
- `tools\validate_tests.bat`: pass; 463/463 cases and
  2,423,855/2,423,855 assertions.
- `tools\validate_physics.bat`: Debug build, lifecycle smoke, and scene launch
  pass; stops at the deliberately deferred 35,091-line golden mismatch.
- `tools\validate_physics_deep.bat`: all scene launches pass; stops at the same
  deferred core mismatch plus the inspected 28-line shooting delta.
- `tools\check_shooting_reaction.py Debug\shooting_reaction_volley.csv`: all ten
  targets pass.

BV5 is complete. BV6 owns final review, the whole-file comment audit, the
one-time baseline regeneration, matching gates, and campaign closure.

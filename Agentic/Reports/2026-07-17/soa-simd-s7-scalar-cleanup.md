# SoA/SIMD S7 — Scalar Production Cleanup

Date: 2026-07-17

Branch: `nightrunner-16th-july`

Plan task: `physics-soa-simd-1000-bodies` S7

## Outcome

The production path now keeps the useful S1-S3 work and removes the rejected
experiment. The fixed-capacity SoA body store, scalar span consumers, and the
byte-exact arithmetic/order contract remain. All five AVX2/FMA kernel pairs,
their scalar/SIMD branches, config/CLI/project plumbing, dedicated A/B tool,
and SIMD-only tests are deleted. The resulting change removes 3,190 lines and
adds 487 lines across 57 files before this report.

`SpatialGrid` is no longer a primary-table/lookup/overflow design. It is one
fixed 8,192-row open-addressed table with one admission, lookup, traversal,
copy, and clear path. The 8,193rd distinct key is an owner-attributed fatal;
there is no silent drop. Focused tests fill the table, exercise a displaced
key whose home slot is occupied, reinsert it, clear/reuse the grid, and lock
the exhaustion diagnostic.

Attribution-only frame counters and SkullScope/query fields are removed. The
inclusive `Broadphase` owner marker and its mutually exclusive child markers
remain, so timing is still interpretable without carrying production counters
whose only purpose was the completed B0-B4 investigation.

## Retained and removed scope

| Retained | Removed |
|---|---|
| Twenty aligned, fixed-capacity SoA hot-field arrays | Ten SIMD kernel source/header files |
| Scalar hot-field spans and direct scalar stage consumers | Every scalar/SIMD runtime branch and kernel context field |
| Fixed iteration and arithmetic order | `physicsExecution.simdKernels`, CLI override, and v3 writer output |
| Byte-exact physics behavior and existing baselines | Per-file AVX2/FMA project settings and SIMD-only tests |
| Broadphase owner/child timing markers | `check_physics_simd_ab.py` and attribution-only counters/query schema |
| Complete coverage through 8,192 unique grid cells | Two-route primary/lookup/overflow grid complexity |

No sleep-state representation was changed. Compacting booleans into C++
bitfields was deliberately not mixed into this cleanup: bitfields do not
guarantee portable layout or atomicity, can introduce read-modify-write work,
and would require its own measured store-layout decision.

## Config migration and replay provenance

Engine config schema v4 deterministically removes the obsolete
`physics_simd_kernels` row while preserving unknown keys. Current-file,
v3-migration, future-version rejection, and both native writer paths are
covered. `SaveRenderDefaults` and `SaveSkyDefaults` now remove the obsolete
row before stamping v4, closing the review-found path that could otherwise
write a v4 file containing the dead setting.

The engine config SHA-256 changed mechanically from
`fede1ca110a51b3368fabdf1e5b9712352e29a4b99139ee025b8da2ab3d7d3f1` to
`f5fe4daa3a3667b168da9a5aad3dbfc846a5857378bb358b50ec8de40c09e3fe`.
The replay manifest's causal binding consequently changed from
`f4a247de2d7778b17d93f8dd421dad678aed145f96a100f9f4e84f35c64b82f4` to
`77f2044158694097e55a92d348ad2529d982e8730878ea89c767786cf7fe56df`.
Mechanical verification found no packet, pose, topology, tolerance, or other
behavioral golden change. This is the standing owner-approved
config-version/provenance-only reconciliation rule applied and recorded for
this instance.

## Coverage and CI

`validate_coverage.bat` is now a mandatory lane of
`validate_all_cpu_tests.bat`. Hosted CI installs and verifies exactly
OpenCppCoverage 0.9.9.0 and checks native process exit codes explicitly.

| Subsystem | Coverage | Floor |
|---|---:|---:|
| Maths | 86.67% | 85% |
| Core primitives | 88.44% | 85% |
| Physics stores | 70.95% | 70% |
| Physics stages and solver | 71.10% | 70% |
| Replay artifact codecs | 75.79% | 70% |
| Startup | 91.82% | 70% |
| Config and schema | 94.77% | 70% |
| Runtime input and interaction | 74.56% | 50% |
| Scene logic | 97.22% | 50% |
| Replay value seams | 82.23% | 50% |

Whole instrumented product output is 16,977 / 24,609 lines (68.99%); it is
reported, not globally gated.

## Performance

The scalar-only `tools\validate_perf.bat` gate passed all absolute,
comparison, allocation-policy, and steady-gameplay allocation checks.

| Bodies | Physics Step | Broadphase | GridInsert | Candidate pairs |
|---:|---:|---:|---:|---:|
| 200 | 0.1151 ms | 0.0297 ms | 0.0237 ms | 0.0024 ms |
| 520 | 0.8219 ms | 0.1582 ms | 0.1329 ms | 0.0106 ms |
| 1,000 | 1.0871 ms | 0.3005 ms | 0.2219 ms | 0.0291 ms |
| 2,000 | 1.8670 ms | 0.7549 ms | 0.4541 ms | 0.1103 ms |

The honest comparison is mixed. Against the retained two-route scalar-OFF
matrix, 1,000 bodies moved from 1.0546/0.2741 ms Step/Broadphase to
1.0871/0.3005 ms, a modest low-scale regression. At 2,000 bodies it improved
from 2.0517/0.8904 ms to 1.8670/0.7549 ms. Against S0's AoS 0.9978 ms and
S3's SoA 0.9795 ms, the current full-product 1,000-body gate does not preserve
the original isolated layout win. This cleanup is therefore a substantial
complexity and safety win, not a claim that the final branch meets the old
0.80 ms target or improves every timing row.

## Validation

- `python tools\migrate_data_formats.py --check`: passed 39 files in 0.217 s.
- `python tools\check_allocation_policy.py --self-test` and `--repo .`:
  passed in 8.654 s; 370 files scanned, zero allowlist errors.
- `python tools\check_physics_query_regression.py`: exact regression passed in
  about 14.5 s.
- `tools\validate_coverage.bat`: all ten floors passed in 9.8 s.
- `tools\validate_perf.bat`: passed all budgets and guards in about 127 s;
  Profile/Automation builds had zero warnings and zero errors.
- `tools\validate_replay_visual_fidelity.bat`: passed in about 443.7 s; 16
  packet controls, 72 assertions, one engine process, one prediction, and all
  false-pass controls detected their injected divergence.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 1`: exit 0
  in 2.144 s with marker emission enabled. The required bare command was also
  launched; because the GUI shell returned while it remained active, its own
  PID 40700 was stopped by PID only.
- `tools\validate_format.bat`: 252 headers aligned and every source file
  correctly formatted.
- `tools\validate_full.bat`: the final successful rerun passed in about 190 s.
  It includes 282 doctest cases and 21,389 assertions, all standalone CPU
  lanes, zero-warning Profile/Debug builds, DX12 screenshots with zero
  InfoQueue errors, and the 44,401-line physics oracle byte-exact. Two earlier
  invocations failed fast on formatting drift in `PhysicsBodyStore.cpp` and
  `.h`; both files were scoped-formatted before the green rerun.

No physics behavioral baseline was regenerated.

## SkullScope query accounting

The regression trace command was:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag Debug\physics_query_varied.physicsdiag.ndjson
```

Every bounded query executed by the regression check was:

```bat
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson summary --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --limit 20
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-summary --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-events --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-rejections --reason propagated_impulse --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-body --body roll_a --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-timeline --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson frame 600 --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson body roll_a --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson energy --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --type penetration_sustained,penetration_growing --limit 20
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contacts --top penetration --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson island 1 --frame 1199 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson stacks --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson rolling --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson broadphase --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson solver --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson pipeline --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions penetration_spikes
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions stack_health
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson compare Debug\physics_query_varied.physicsdiag.ndjson --limit 8
```

The trace is 102,433,336 bytes and its SQLite cache is 50,118,656 bytes. Query
output sizes, in command order, were 8,894; 21,811; 9,360; 4,597; 393; 5,583;
1,989; 8,568; 8,431; 6,337; 310; 5,934; 716; 3,802; 7,447; 3,038;
3,520; 5,265; 1,454; 1,354; and 941 characters/UTF-8 bytes. Total generated
bounded output was 109,744 bytes. Raw artifacts and query JSON were suppressed
from the model; GPT-read query payload was zero bytes, with only these size
rows exposed. Nothing was truncated.

## Comment audit and independent review

Touched-file comment audit: all 30 source-bearing files were inspected against
`Agentic/Skills/comment-style-audit/skill.md`; 30 checked, 0 deferred, 0
unchecked. Checklist path: N/A in touched-file mode. The final exact-word scan
found only the intentional v3-to-v4 migration/test references and the
pre-existing general SIMD note in `MathsCommon.h`.

Independent review run `physics-soa-simd-cleanup-duck-01` used reviewer
`/root/u9_independent_review` (Aquinas), with an approximately 1.3k-character
initial prompt and iterative responses. It found six credible issues during
the pass: CI tool provisioning, a false-passing displaced-key test, a silent
grid insertion return, missing native-writer migration, stale ledger/session
contradictions, and insufficiently explicit CI native exit/version checks.
All six were corrected and re-reviewed. Final verdict: zero blocking findings,
zero non-blocking findings.

## S8 handoff

S7 is ready to commit at 8/9. S8 is documentation-only closure: publish the
campaign-level verdict, delete the completed live plan under MASTER inventory
rule 4, remove it from the active/future ledger, and confirm the branch is
clean and synchronized.

# Terminal performance validation, still incomplete

Exclusive full attempt 04 exits 7 after 118.375 seconds. All workload launches,
allocation checks, selected-path structural checks, dense causal measurement and
absolute budgets pass. Both frame-average and median comparisons fail against
the old references. Physics averages are lower than those reference values.
No timing baseline is approved or replaced by this evidence.

The diagnostic comparisons below identify earlier UI work and shared-preference
contamination. They are not controlled ragdoll speculative-off/on measurements.
The original FP9 A/B evidence remains authoritative for that question.

| Workload | Reference frame avg | Old Profile | Perf04 Profile | Old Automation | Current Automation |
|---|---:|---:|---:|---:|---:|
| DX12 | 0.9090 | 1.1742 | 1.4301 | 1.2133 | 1.1555 |
| physics_bench | 0.4860 | 0.7546 | 0.9866 | 0.7562 | 0.7752 |

All values are milliseconds. Old Profile SHA starts 3873131e; old Automation
starts 5820d276; current Automation starts 6dbb35b9. Exact hashes, commands,
exit codes and raw CSVs are preserved in the run manifests. Both successful old
Profile runs and all four Automation runs exit 0. The first old Profile attempt
lacked the pinned third-party DLL search path and did not enter gameplay. Its
exact PID/executable was verified before termination; it is excluded. Successful
runs add the existing Profile/Automation runtime directory to PATH without
copying third-party binaries into tracked artifacts.

Perf04 shared UI preferences between its causal-inspection recording and later
benchmarks. That recording opens Editor/Evidence and leaves Editor selected;
subsequent benchmarks then draw an extra panel. The tool now supplies a fresh
private preferences path for each causal measurement and records that path.
The native isolation check proves its complete workload passes with an inherited
Editor file, while that inherited file stays byte-identical. The change does not
alter timing thresholds, analyzer assertions, source scenes or golden files.

Raw evidence:
- TestOutput/fp9-terminal-perf-04.log and its result JSON.
- TestOutput/fp9-pre-fp8-perf-comparison-02/.
- TestOutput/fp9-pre-fp8-automation-perf-01/.
- TestOutput/fp9-causal-perf-preferences-isolation-01/.
- Current producers and original perf04 reports remain preserved under
  TestOutput/fp9-pre-fp8-perf-comparison-01/current/.

Full attempt 04 now validates current source. A new exclusive performance gate
and resolution of the older UI-related references remain required for FP9.


## Isolated performance attempt 05

`tools\validate_perf.bat` exits 7 in 95.974 seconds after the preferences
repair. The parent file remains Canvas. All native workloads, gameplay allocation
guard, selected-path structural checks, causal inspection and absolute budgets
pass. Causal maximum phase is 0.0389 ms, panel/overlay ratio 0.822, fixed storage
231552 bytes, steady allocations zero.

| Workload | Reference frame avg | Perf04 frame avg | Perf05 frame avg | Perf05 Physics avg | Perf05 UI avg |
|---|---:|---:|---:|---:|---:|
| DX12 | 0.9090 | 1.4301 | 1.1852 | 0.2844 | 0.3517 |
| physics_bench | 0.4860 | 0.9866 | 0.8025 | 0.0988 | 0.3561 |

All timings are milliseconds. Frame average/median still fail both references.
The UI cost drops after isolation, while a remaining roughly 0.35 ms of UI work
is absent from the old reports. This is evidence for the explanation, not a
passed comparison or permission to refresh blindly. Exact prior timing producer
3eacb815 is preserved in the FP4 transitions and can support the next matched
comparison before a reviewed timing transition. No timing reference changed.

Raw reports, CSVs and the current Profile producer are retained in
`TestOutput/fp9-perf05-results/`; log/result are
`TestOutput/fp9-terminal-perf-05.log` and `fp9-terminal-perf-05-result.json`.


The next reference-producer comparison is prepared under
`TestOutput/fp9-reference-producer-perf-01/`. It retains exact old producer
3eacb815 and current producer 8f40ec6e plus both original benchmark scenes
extracted from source commit 3a4b52e94. JSON comparison proves each original
scene differs from its current counterpart only in the version field (1/4
versus 5). Native runs are deferred until mapped replay validation releases
the machine. The script records alternating old/current runs, fresh preferences,
commands, executable/scene hashes, exit codes, raw CSVs and marker statistics.


## Exact reference producer comparison completed

Attempt 01 failed at grid-line shader warmup before gameplay. PID 32372 and its
exact archived executable path were verified before termination; its timing is
excluded. The old renderer needs its original tracked shader inputs. Attempt 02
exports all original SkullbonezData into a private workspace and completes all
eight alternating old/current processes with the expected 1940/2340 measured
frames. It does not change the current checkout's data.

| Workload | Old frame averages | Current frame averages | Current UI averages |
|---|---:|---:|---:|
| DX12 | 0.8791 / 0.8258 | 1.2217 / 1.1479 | 0.3625 / 0.3390 |
| physics_bench | 0.4161 / 0.4133 | 0.7427 / 0.8406 | 0.3353 / 0.3673 |

All values are milliseconds. Old DX12 Physics averages 0.3339/0.3127 versus
current 0.2914/0.2754; old physics_bench averages 0.1031/0.1028 versus current
0.0925/0.1059. UI work explains most of the frame-cost increase. Original versus
current shader inputs differ, so this is a product comparison, not a claim that
every residual timing difference is caused by UI or an isolation of FP8 cost.
The required same-executable ragdoll A/B remains the separate cost authority.

A timing transition is prepared for independent review at
`golden-transitions/performance-references-8f40ec6e/`. It retains original producer
3eacb815, current producer 8f40ec6e, original references and the exact full perf05
candidates. Both manifest rows pass read-only guard validation. No timing baseline
has been written yet. Screenshot approval remains a separate pending exception.


Independent review found no material blocker. Both references were written with
the generic baseline guard; each writer exits 0 and records the exact manifest
and candidate identity. The reviewer-requested comparison-command correction
is applied, while launch-context.json separately retains the full gate's exact
physics_bench argv using default DX12. No threshold changed. Full exclusive
performance attempt 06 is running under `TestOutput/fp9-terminal-perf-06.log`.
The screenshot preservation exception remains pending and is not covered by
this timing review or write.


## Post-write full performance gate passes

Attempt 06 exits 0 in 98.211 seconds. Relative comparisons and absolute budgets
pass for both workloads, along with native allocation and structural checks.
Frame averages are 1.1945 / 0.7796 ms, Physics 0.2873 / 0.0973 ms, and UI
0.3553 / 0.3520 ms. Dense causal inspection passes at maximum phase 0.1230 ms,
ratio 0.929, fixed storage 231552 bytes and zero steady allocations. Profile
and Debug readiness builds pass. Profile remains byte-identical to the retained
8f40ec6e producer. The transition bundle's mapped-validation.json records exact
raw artifact hashes. This completes the mapped timing gate; full-plan closure
still needs the screenshot exception and final complete aggregate gate.

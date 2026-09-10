# Timing reference transition prepared for review

Status: independently reviewed and written through the generic baseline guard;
post-write full performance validation passes in 98.211 seconds. This transition
reconciles the full current product cost measured during FP9 with references
from FP4, before the committed shared UI work. The user's active goal authorizes
reviewed baseline updates while retaining old executables. Exact old and new
first-party Profile executables, both prior goldens and both candidates are here.
No third-party DLL, compiler or shader binary is added to this bundle.

The first exact-reference launch with current data failed at grid-line warmup
before gameplay and is excluded. Repeating against the original tracked data
from commit 3a4b52e94 in an isolated workspace succeeds. Both original benchmark
scenes differ from current scenes only in the scene-format version. Historical
shader content is also required by the old renderer. This is an old-product /
current-product comparison, not an isolation test of FP8's physics cost.

Eight alternating processes all exit 0, with 1940 measured frames for DX12 and
2340 for physics_bench. Raw CSVs and logs are retained under
TestOutput/fp9-reference-producer-perf-02; reference-producer-runs.json binds
commands, executable hashes, scene hashes, frame counts and marker statistics.

| Workload | Original reference frame avg | Old producer repeat avgs | Current repeat avgs | Current UI avgs |
|---|---:|---:|---:|---:|
| DX12 | 0.9090 | 0.8791 / 0.8258 | 1.2217 / 1.1479 | 0.3625 / 0.3390 |
| physics_bench | 0.4860 | 0.4161 / 0.4133 | 0.7427 / 0.8406 | 0.3353 / 0.3673 |

All values are milliseconds. Old reports have no Frame/UI markers. Current
Physics is 0.2914/0.2754 versus old 0.3339/0.3127 ms for DX12, and current
0.0925/0.1059 versus old 0.1031/0.1028 ms for physics_bench. UI work explains most
of the total increase; these runs do not establish that every residual difference
is UI-only. Separately preserved pre-FP8/current comparisons and the mandatory
same-executable speculative-off/on A/B report FP8's own costs without mixing
that cost into the older product comparison.

Candidates come from the uncontaminated full performance attempt 05, not from
selecting the slowest diagnostic repetition: frame averages 1.1852/0.8025 ms,
Physics 0.2844/0.0988 ms, UI 0.3517/0.3561 ms. That gate passes native workloads,
allocation guards, structural checks and absolute budgets; only old frame
average/median comparisons fail. Its causal inspection has zero steady
allocations. Both exact candidate hashes are recorded in manifest.json.

Proposed decision: accept the measured current UI cost in these complete frame
references, retaining every analyzer threshold and the required separate ragdoll
A/B cost/stability evidence. After independent review, write through the generic
Physics baseline guard and rerun the entire performance gate exclusively.
This does not approve the pending screenshot exception, certify full-plan
closure, or claim that ragdoll piles now settle reliably.


Post-write attempt 06 exits 0. Both relative comparisons and absolute budgets
pass, as do all native, allocation and structural checks. Frame averages are
1.1945 ms DX12 and 0.7796 ms physics_bench; Physics 0.2873/0.0973 ms and UI
0.3553/0.3520 ms. Dense causal inspection reports maximum phase 0.1230 ms,
ratio 0.929, fixed storage 231552 bytes and zero steady allocations. Final
Profile and Debug builds pass; Profile still matches retained producer 8f40ec6e.
`mapped-validation.json` binds the complete gate result and raw artifacts.

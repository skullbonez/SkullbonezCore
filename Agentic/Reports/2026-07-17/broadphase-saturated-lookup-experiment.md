# Broadphase Saturated-Lookup Experiment

Date: 2026-07-17

Plan: `physics-broadphase-scale-attribution`, B3

Result: **retain the bounded saturated lookup and overflow tier**

## Outcome

B2 found that the 2,000-body cliff was not pair growth or duplicate rejection.
The primary grid had admitted all 4,096 cells, after which every visit to a new
cell scanned all 4,096 occupied hash slots before returning `-1`. This cost
about 15 million failed slot inspections per measured frame.

The first compact candidate built an 8,192-slot index of the admitted primary
keys. It removed the failed probes and produced excellent timing, but it kept
the legacy behavior of silently dropping every new distinct cell after 4,096.
The B4 independent review correctly rejected that behavior: a dropped cell can
hide a collision, so it violates broadphase completeness and the engine's fatal
capacity policy.

The retained design keeps the unchanged 4,096-row primary table as the common
path. When that table fills, it builds a fixed 16,384-slot key index and admits
up to 4,096 additional cells into a cold overflow array. Primary rows retain
their existing probe and admission order; overflow rows are traversed in their
deterministic admission order. The 8,193rd distinct cell terminates through
Lane F with owner, capacity, active-count, and phase diagnostics. No heap growth
or silent coverage loss remains.

The cold storage cost is 160 KiB: 32 KiB for 16,384 `uint16_t` lookup slots and
128 KiB for 4,096 32-byte bucket rows. Each grid allocates that fixed block once
during physics-owner startup; it never grows or reallocates during gameplay.
Keeping it out of line preserves the grid and containing broadphase owner's hot
layout. The stage's owned-memory accounting includes the full 160 KiB.

## Candidate sequence

| Candidate | Added fixed storage | 1,000 Step median (range) | 2,000 Step / Broadphase / insert | Decision |
|---|---:|---:|---:|---|
| 16,384 primary buckets | 384 KiB | +7.36% (+2.91% to +11.17%) | -76.61% / -88.80% / -92.88% | Reject: clear common-scale regression |
| 8,192 primary buckets | 128 KiB | +1.60% (-0.93% to +4.97%) | -77.08% / -89.53% / -93.62% | Reject: common-scale insert regression |
| 8,192-slot membership-only lookup | 16 KiB | +0.43% (-2.75% to +3.01%) | -76.21% / -88.53% / -92.54% | Reject at review: preserved silent cell drops |
| 16,384-slot lookup plus 4,096 cold buckets | 160 KiB startup-fixed | +0.65% (-0.45% to +2.45%) | -75.33% / -87.05% / -91.04% | Retain: neutral Step at 1,000; bounded, complete, deterministic |

The membership-only candidate's paired executable was 4,705,792 bytes with
SHA-256
`718AF9A777B495E9CE7C7A3266690357EB8EF7C631B3AB63343478A2B164AD92`.
The saved control was 4,705,280 bytes with SHA-256
`084C676A0125443320B5627F4C28F85573756666EA0514E3420D245A3E21A618`.
These hashes remain useful provenance for the mechanism experiment, but not as
acceptance evidence for the retained overflow design.

## Seven alternating mechanism pairs

These same-tip pairs measured the membership-only lookup before independent
review rejected its capacity behavior. They establish that replacing saturated
full-table misses is the winning algorithm; they do not waive the later safety
correction. Every launch used explicit `--physics-simd-kernels off`. Values are
marker averages in milliseconds and deltas are candidate relative to control.
Raw and analyzed artifacts are under the ignored
`TestOutput/broadphase_attribution/b3_pairs_compact/` directory.

### 1,000 bodies

| Pair | Step C->N (delta) | Broadphase C->N (delta) | Grid insert C->N (delta) | Candidate pairs C->N (delta) |
|---:|---:|---:|---:|---:|
| 1 | 1.0062->1.0335 (+2.71%) | 0.2815->0.2794 (-0.75%) | 0.2055->0.2030 (-1.22%) | 0.0276->0.0279 (+1.09%) |
| 2 | 1.0082->1.0089 (+0.07%) | 0.2832->0.2730 (-3.60%) | 0.2073->0.1973 (-4.82%) | 0.0278->0.0275 (-1.08%) |
| 3 | 1.0416->1.0130 (-2.75%) | 0.2856->0.2637 (-7.67%) | 0.2101->0.1879 (-10.57%) | 0.0274->0.0276 (+0.73%) |
| 4 | 1.0216->1.0260 (+0.43%) | 0.2677->0.2784 (+4.00%) | 0.1927->0.2021 (+4.88%) | 0.0270->0.0280 (+3.70%) |
| 5 | 1.0053->1.0356 (+3.01%) | 0.2805->0.2832 (+0.96%) | 0.2048->0.2064 (+0.78%) | 0.0275->0.0284 (+3.27%) |
| 6 | 1.0107->1.0214 (+1.06%) | 0.2803->0.2670 (-4.74%) | 0.2045->0.1911 (-6.55%) | 0.0276->0.0275 (-0.36%) |
| 7 | 1.0343->1.0268 (-0.73%) | 0.2713->0.2682 (-1.14%) | 0.1958->0.1921 (-1.89%) | 0.0271->0.0275 (+1.48%) |
| **Median** | **+0.43%** | **-1.14%** | **-1.89%** | **+1.09%** |
| **Range** | **-2.75% to +3.01%** | **-7.67% to +4.00%** | **-10.57% to +4.88%** | **-1.08% to +3.70%** |

### 2,000 bodies

| Pair | Step C->N (delta) | Broadphase C->N (delta) | Grid insert C->N (delta) | Candidate pairs C->N (delta) |
|---:|---:|---:|---:|---:|
| 1 | 7.5744->1.8198 (-75.97%) | 6.4899->0.7454 (-88.51%) | 6.1949->0.4623 (-92.54%) | 0.1058->0.0961 (-9.17%) |
| 2 | 7.6133->1.8111 (-76.21%) | 6.5188->0.7477 (-88.53%) | 6.2201->0.4641 (-92.54%) | 0.1086->0.0964 (-11.23%) |
| 3 | 7.5561->1.7876 (-76.34%) | 6.4775->0.7425 (-88.54%) | 6.1860->0.4587 (-92.58%) | 0.1025->0.0965 (-5.85%) |
| 4 | 7.5898->1.7824 (-76.52%) | 6.5058->0.7310 (-88.76%) | 6.2079->0.4484 (-92.78%) | 0.1079->0.0956 (-11.40%) |
| 5 | 7.5533->1.7877 (-76.33%) | 6.4703->0.7327 (-88.68%) | 6.1796->0.4493 (-92.73%) | 0.1015->0.0963 (-5.12%) |
| 6 | 7.5941->1.8175 (-76.07%) | 6.5036->0.7494 (-88.48%) | 6.2054->0.4647 (-92.51%) | 0.1082->0.0972 (-10.17%) |
| 7 | 7.5748->1.8167 (-76.02%) | 6.4977->0.7546 (-88.39%) | 6.2012->0.4707 (-92.41%) | 0.1067->0.0968 (-9.28%) |
| **Median** | **-76.21%** | **-88.53%** | **-92.54%** | **-9.28%** |
| **Range** | **-76.52% to -75.97%** | **-88.76% to -88.39%** | **-92.78% to -92.41%** | **-11.40% to -5.12%** |

## Seven alternating retained-design pairs

These are the acceptance pairs for the final overflow-safe source, not the
rejected membership-only mechanism. The control executable is the pre-B3
`0f203592` build. The candidate was built from the uncommitted remediation
working tree after `d8cce716`; consequently the analyzer's embedded
`d8cce7168` field identifies the last commit only and is not presented as the
candidate source identity. The eventual B3 commit records the exact source.
Raw/analyzed artifacts are under the ignored
`TestOutput/broadphase_attribution/b3_pairs_final5/` directory.

### Final 1,000-body pairs

| Pair | Step C->N (delta) | Broadphase C->N (delta) | Grid insert C->N (delta) | Candidate pairs C->N (delta) |
|---:|---:|---:|---:|---:|
| 1 | 1.0823->1.0883 (+0.55%) | 0.2958->0.3102 (+4.87%) | 0.2178->0.2328 (+6.89%) | 0.0287->0.0283 (-1.39%) |
| 2 | 1.0335->1.0323 (-0.12%) | 0.2869->0.3022 (+5.33%) | 0.2110->0.2264 (+7.30%) | 0.0277->0.0276 (-0.36%) |
| 3 | 1.0456->1.0524 (+0.65%) | 0.2785->0.3014 (+8.22%) | 0.2024->0.2254 (+11.36%) | 0.0278->0.0277 (-0.36%) |
| 4 | 1.0348->1.0302 (-0.45%) | 0.2774->0.2999 (+8.11%) | 0.2014->0.2238 (+11.12%) | 0.0276->0.0278 (+0.72%) |
| 5 | 1.0427->1.0584 (+1.51%) | 0.2902->0.3038 (+4.69%) | 0.2146->0.2275 (+6.01%) | 0.0275->0.0279 (+1.45%) |
| 6 | 1.0296->1.0548 (+2.45%) | 0.2777->0.3059 (+10.16%) | 0.2019->0.2304 (+14.12%) | 0.0279->0.0273 (-2.15%) |
| 7 | 1.0251->1.0476 (+2.20%) | 0.2746->0.2997 (+9.14%) | 0.1996->0.2234 (+11.92%) | 0.0269->0.0279 (+3.72%) |
| **Median** | **+0.65%** | **+8.11%** | **+11.12%** | **-0.36%** |
| **Range** | **-0.45% to +2.45%** | **+4.69% to +10.16%** | **+6.01% to +14.12%** | **-2.15% to +3.72%** |

The independent reviewer accepts the end-to-end Step movement as neutral: the
range crosses zero and the median is about 0.0068 ms. Broadphase and insertion
are not neutral; their consistent roughly 0.02 ms cost is disclosed rather
than hidden by the Step result.

### Final 2,000-body pairs

| Pair | Step C->N (delta) | Broadphase C->N (delta) | Grid insert C->N (delta) | Candidate pairs C->N (delta) |
|---:|---:|---:|---:|---:|
| 1 | 7.9051->1.9351 (-75.52%) | 6.8043->0.8704 (-87.21%) | 6.5018->0.5719 (-91.20%) | 0.1114->0.1089 (-2.24%) |
| 2 | 7.8562->1.9300 (-75.43%) | 6.7877->0.8783 (-87.06%) | 6.4911->0.5803 (-91.06%) | 0.1069->0.1095 (+2.43%) |
| 3 | 7.8320->1.9453 (-75.16%) | 6.7757->0.8849 (-86.94%) | 6.4798->0.5862 (-90.95%) | 0.1066->0.1104 (+3.56%) |
| 4 | 7.8338->1.9379 (-75.26%) | 6.7731->0.8858 (-86.92%) | 6.4769->0.5890 (-90.91%) | 0.1070->0.1088 (+1.68%) |
| 5 | 7.8839->1.9450 (-75.33%) | 6.7896->0.8792 (-87.05%) | 6.4885->0.5815 (-91.04%) | 0.1109->0.1101 (-0.72%) |
| 6 | 7.8510->1.9558 (-75.09%) | 6.7711->0.8839 (-86.95%) | 6.4781->0.5838 (-90.99%) | 0.1043->0.1107 (+6.14%) |
| 7 | 7.8946->1.9474 (-75.33%) | 6.8049->0.8746 (-87.15%) | 6.5157->0.5764 (-91.15%) | 0.1017->0.1098 (+7.96%) |
| **Median** | **-75.33%** | **-87.05%** | **-91.04%** | **+2.43%** |
| **Range** | **-75.52% to -75.09%** | **-87.21% to -86.92%** | **-91.20% to -90.91%** | **-2.24% to +7.96%** |

## Corrected final matrix

The formal performance gate from the retained overflow source produced:

| Bodies | Physics Step | Broadphase inclusive | GridInsertScalar | CandidatePairsScalar | FastSmallSweepAugment |
|---:|---:|---:|---:|---:|---:|
| 200 | 0.1117 ms | 0.0274 ms | 0.0215 ms | 0.0022 ms | 0.0026 ms |
| 520 | 0.8314 ms | 0.1631 ms | 0.1382 ms | 0.0099 ms | 0.0112 ms |
| 1,000 | 1.0754 ms | 0.2983 ms | 0.2218 ms | 0.0276 ms | 0.0430 ms |
| 2,000 | 2.0020 ms | 0.8945 ms | 0.5913 ms | 0.1122 ms | 0.1788 ms |

Against B0's packed-source 2,000-body Step reference, Step improves 73.51%.
Against B2's instrumented attribution, Broadphase improves 86.61% and grid
insertion improves 90.72%. The corrected design is about
0.20 ms slower than the rejected membership-only candidate at 2,000 bodies
because it performs the missing work, but it retains nearly all of the
algorithmic win and restores conservative coverage.

## Behavioral and deterministic proof

The focused regression fills all 4,096 primary rows, then inserts two new keys
chosen to collide in the low 14 bits of the secondary index. It proves both
overflow cells are admitted, both pairs are emitted exactly, and pair order
follows deterministic cell admission. After `Clear`, reversing overflow cell
admission reverses the two cell groups exactly. A child-process fatal test fills
all 8,192 rows and locks the owner/capacity/phase diagnostics for the 8,193rd
cell. A separate boundary predicate test proves the post-grid candidate append
rejects equality before `emplace_back` could grow its vector.

Fresh traces were generated from the retained Debug source with:

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_1000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_2000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson
```

| Scale | Old capped trace | Retained overflow trace | Result | Trace time |
|---:|---|---|---|---:|
| 1,000 | 57,838,185 bytes / `1C947D...C3DE7` | 57,838,185 bytes / `1C947D...C3DE7` | byte-identical | 11.181 s |
| 2,000 | 115,957,299 bytes / `79883E...BE18` | 116,018,437 bytes / `923957...EB054` | intentionally corrected coverage | 18.198 s |

The bounded 2,000-body query reports about 7,574 active cells and 8,336 entry
writes per sampled frame. Before correction it was capped at 4,096 active cells
and wrote only about 4,682 of roughly 8,342 visits. The extra 61,138 trace bytes
are evidence of restored cell and candidate coverage, not nondeterminism. The
standard 44,401-line regression oracle remains byte-exact because its covered
scenes do not exhaust the primary grid.

## SkullScope accounting for corrected traces

The exact bounded queries were:

```bat
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson broadphase --attribution --limit 4
```

| Artifact/query | On-disk or GPT-read size | Truncation |
|---|---:|---|
| 1,000 NDJSON | 57,838,185 bytes on disk | N/A |
| 1,000 SQLite | 28,676,096 bytes on disk | N/A |
| 1,000 summary | 5,222 characters / UTF-8 bytes GPT-read | none |
| 1,000 attribution | 2,362 characters / UTF-8 bytes GPT-read | none |
| 2,000 NDJSON | 116,018,437 bytes on disk | N/A |
| 2,000 SQLite | 57,536,512 bytes on disk | N/A |
| 2,000 summary | 5,231 characters / UTF-8 bytes GPT-read | none |
| 2,000 attribution | 2,366 characters / UTF-8 bytes GPT-read | none |
| **Total model-ingested query output** | **15,181 characters / UTF-8 bytes** | **none** |

No raw NDJSON or SQLite content was ingested. One accounting wrapper failed
after executing the first summary query because its JavaScript environment did
not expose `TextEncoder`; it emitted no query content. The four canonical
reruns above are the initial corrected-trace GPT-read evidence. B4's closure
report records two additional bounded 1,000-body review queries and the
campaign-wide total.

## Formal gates

- `tools\validate_tests.bat`: passed 288/288 cases and 21,444 assertions after
  saturated copy/assignment coverage was added.
- `tools\validate_physics.bat`: passed; the 44,401-line varied-scene oracle
  remained byte-exact and Profile/Debug builds had zero warnings and errors.
- `tools\validate_perf.bat`: completed in 113.349 seconds; allocation policy,
  the gameplay allocation guard, owned-memory accounting, absolute budgets,
  and comparisons passed.
- Touched-source comment audit is reconciled in the B4 closure report.

No baseline, golden, scene, config, SIMD-default, or S7 change was made.

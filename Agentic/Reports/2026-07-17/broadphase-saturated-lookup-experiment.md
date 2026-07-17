# Broadphase Saturated-Lookup Experiment

Date: 2026-07-17

Plan: `physics-broadphase-scale-attribution`, B3

Result: **retain the compact saturated-only lookup**

## Outcome

B2 found that the 2,000-body cliff was not pair growth or duplicate rejection.
The primary grid had admitted all 4,096 cells, after which every visit to a new
cell scanned all 4,096 occupied hash slots before returning `-1`. This was about
15 million failed slot inspections per measured frame.

The retained fix leaves the 4,096-cell primary table, insertion order, entry
chains, and rejection contract unchanged. When—and only when—the primary table
becomes full, `SpatialGrid` builds an 8,192-slot open-addressed index of the
already admitted bucket keys. Existing keys still resolve to their original
primary bucket; a missing key stops at the secondary table's guaranteed empty
sentinel. The cost is a cold 16 KiB array plus one readiness flag, with no heap
allocation and no common-path clear of that array.

## Candidate sequence

Two direct primary-table enlargements proved that eliminating saturation was
the right mechanism but the wrong layout. Both moved a much larger bucket array
through the 1,000-body common path.

| Candidate | Added fixed storage | 1,000 Step median (range) | 1,000 Broadphase / insert | 2,000 Step / Broadphase / insert | Decision |
|---|---:|---:|---:|---:|---|
| 16,384 primary buckets | 384 KiB | +7.36% (+2.91% to +11.17%) | +22.23% / +29.40% | -76.61% / -88.80% / -92.88% | Reject: clear common-scale regression |
| 8,192 primary buckets | 128 KiB | +1.60% (-0.93% to +4.97%) | +7.56% / +10.41% | -77.08% / -89.53% / -93.62% | Reject: common-scale insert regression |
| 8,192-slot saturated lookup | 16 KiB | +0.43% (-2.75% to +3.01%) | -1.14% / -1.89% | -76.21% / -88.53% / -92.54% | Retain |

The compact candidate's paired executable was 4,705,792 bytes with SHA-256
`718AF9A777B495E9CE7C7A3266690357EB8EF7C631B3AB63343478A2B164AD92`.
The saved control was 4,705,280 bytes with SHA-256
`084C676A0125443320B5627F4C28F85573756666EA0514E3420D245A3E21A618`.
The final formal-gate rebuild of the same candidate source was 4,705,792 bytes
with SHA-256
`5798588A75DF848326FA93AC1390EF6AE3D516834847A42A5C4076DF2516B4F3`;
the different PE hash was recorded rather than substituted into the paired-run
provenance.

## Seven alternating same-tip pairs

All launches used explicit `--physics-simd-kernels off`. Values are marker
averages in milliseconds; deltas are candidate relative to control. The raw and
analyzed artifacts are under the ignored
`TestOutput/broadphase_attribution/b3_pairs_compact/` evidence directory.

### 1,000 bodies

| Pair | Step C→N (Δ) | Broadphase C→N (Δ) | Grid insert C→N (Δ) | Candidate pairs C→N (Δ) |
|---:|---:|---:|---:|---:|
| 1 | 1.0062→1.0335 (+2.71%) | 0.2815→0.2794 (-0.75%) | 0.2055→0.2030 (-1.22%) | 0.0276→0.0279 (+1.09%) |
| 2 | 1.0082→1.0089 (+0.07%) | 0.2832→0.2730 (-3.60%) | 0.2073→0.1973 (-4.82%) | 0.0278→0.0275 (-1.08%) |
| 3 | 1.0416→1.0130 (-2.75%) | 0.2856→0.2637 (-7.67%) | 0.2101→0.1879 (-10.57%) | 0.0274→0.0276 (+0.73%) |
| 4 | 1.0216→1.0260 (+0.43%) | 0.2677→0.2784 (+4.00%) | 0.1927→0.2021 (+4.88%) | 0.0270→0.0280 (+3.70%) |
| 5 | 1.0053→1.0356 (+3.01%) | 0.2805→0.2832 (+0.96%) | 0.2048→0.2064 (+0.78%) | 0.0275→0.0284 (+3.27%) |
| 6 | 1.0107→1.0214 (+1.06%) | 0.2803→0.2670 (-4.74%) | 0.2045→0.1911 (-6.55%) | 0.0276→0.0275 (-0.36%) |
| 7 | 1.0343→1.0268 (-0.73%) | 0.2713→0.2682 (-1.14%) | 0.1958→0.1921 (-1.89%) | 0.0271→0.0275 (+1.48%) |
| **Median** | **+0.43%** | **-1.14%** | **-1.89%** | **+1.09%** |
| **Range** | **-2.75% to +3.01%** | **-7.67% to +4.00%** | **-10.57% to +4.88%** | **-1.08% to +3.70%** |

The 1,000-body Step result is neutral noise around zero. The secondary array is
never built in this scene, and the exclusive insertion marker is not slower.

### 2,000 bodies

| Pair | Step C→N (Δ) | Broadphase C→N (Δ) | Grid insert C→N (Δ) | Candidate pairs C→N (Δ) |
|---:|---:|---:|---:|---:|
| 1 | 7.5744→1.8198 (-75.97%) | 6.4899→0.7454 (-88.51%) | 6.1949→0.4623 (-92.54%) | 0.1058→0.0961 (-9.17%) |
| 2 | 7.6133→1.8111 (-76.21%) | 6.5188→0.7477 (-88.53%) | 6.2201→0.4641 (-92.54%) | 0.1086→0.0964 (-11.23%) |
| 3 | 7.5561→1.7876 (-76.34%) | 6.4775→0.7425 (-88.54%) | 6.1860→0.4587 (-92.58%) | 0.1025→0.0965 (-5.85%) |
| 4 | 7.5898→1.7824 (-76.52%) | 6.5058→0.7310 (-88.76%) | 6.2079→0.4484 (-92.78%) | 0.1079→0.0956 (-11.40%) |
| 5 | 7.5533→1.7877 (-76.33%) | 6.4703→0.7327 (-88.68%) | 6.1796→0.4493 (-92.73%) | 0.1015→0.0963 (-5.12%) |
| 6 | 7.5941→1.8175 (-76.07%) | 6.5036→0.7494 (-88.48%) | 6.2054→0.4647 (-92.51%) | 0.1082→0.0972 (-10.17%) |
| 7 | 7.5748→1.8167 (-76.02%) | 6.4977→0.7546 (-88.39%) | 6.2012→0.4707 (-92.41%) | 0.1067→0.0968 (-9.28%) |
| **Median** | **-76.21%** | **-88.53%** | **-92.54%** | **-9.28%** |
| **Range** | **-76.52% to -75.97%** | **-88.76% to -88.39%** | **-92.78% to -92.41%** | **-11.40% to -5.12%** |

Every pair improves. The near-total removal of grid-insert time, while pair
generation changes by only about 9%, matches B2's full-table-probe diagnosis.

## Behavioral and byte-exact proof

The focused regression fills exactly 4,096 distinct cells, then proves that a
new body can still join an admitted cell while a 4,097th distinct cell remains
rejected. It also locks active-cell count, visits, entry writes, and the emitted
pair. Existing prepared-AABB, deterministic ordering, candidate completeness,
and sampled-sweep duplicate tests remain in the same suite.

Fresh traces were generated from the final Debug source with:

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_1000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_1000_30f_compact.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_2000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_2000_30f_compact.physicsdiag.ndjson
```

| Scale | Control bytes / SHA-256 | Candidate bytes / SHA-256 | Result | Trace time |
|---:|---|---|---|---:|
| 1,000 | 57,838,185 / `1C947D9944CC9982961DDABA8A528D4BC8A67953B1763F88D3369D8B782C3DE7` | 57,838,185 / `1C947D9944CC9982961DDABA8A528D4BC8A67953B1763F88D3369D8B782C3DE7` | byte-identical | 12.262 s |
| 2,000 | 115,957,299 / `79883E27AA2E39262A4D008D679C1DCEA6DAA50C089D0780D084A194DEE7BE18` | 115,957,299 / `79883E27AA2E39262A4D008D679C1DCEA6DAA50C089D0780D084A194DEE7BE18` | byte-identical | 18.187 s |

No SkullScope content query was required for this comparison: raw trace and
SQLite content exposed to the model was zero bytes, and GPT-read trace-query
output was zero characters/bytes. Only file sizes and SHA-256 metadata were
read. B2's bounded attribution queries and their separate data-size accounting
remain in `broadphase-scale-attribution.md`.

## Formal gates

- `tools\validate_tests.bat`: passed 286/286 cases and 21,425 assertions.
- `tools\validate_physics.bat`: passed; the 44,401-line varied-scene oracle
  remained byte-exact and Profile/Debug builds had zero warnings and errors.
- `tools\validate_perf.bat`: completed; allocation policy/guard and performance
  budgets passed. Its measurement-only matrix reported 1,000-body
  Step/Broadphase/grid-insert at 1.0418/0.2854/0.2081 ms and 2,000-body at
  1.8036/0.7509/0.4660 ms.
- Touched-source comment audit: 3/3 checked, 0 deferred, 0 unchecked. Checklist
  path is N/A for touched-file mode.

No baseline, golden, scene, config, SIMD-default, or S7 change was made.

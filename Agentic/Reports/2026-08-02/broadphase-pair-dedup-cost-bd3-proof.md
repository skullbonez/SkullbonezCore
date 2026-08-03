# Broadphase Pair Dedup Cost BD3 — Byte-Exact Proof And Measured Gain

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Plan phase: BD3
Result: COMPLETE — byte-exact behavior proved; independent review accepted the final source

## Outcome

The scene-reserved per-body membership index is now the only production
first-seen authority. The temporary per-observation dense comparison and its
aggregate counters are removed. The Debug dense store and same-state legacy
driver remain isolated for BD4 deletion; neither participates in production
filtering.

Every BD0 pair stream, `sleepPrunedPairs` stream, geometry-invocation count, and
worker-order receipt is byte-identical on the final source. No Physics,
SkullScope, replay, visual, schema, config, golden, or performance baseline was
changed.

## BD3 Owner Refinement

BD3 supersedes the BD1/BD2 construction detail that called the membership index
complete and mode-independent. Let `M(x)` be the active buckets containing body
`x` and `E` the buckets eligible for the current pair-source mode. The owner is

`min(M(a) intersection M(b) intersection E)`

which equals

`min((M(a) intersection E) intersection (M(b) intersection E))`.

The final traversal therefore projects unstamped buckets out of restricted
prefixes. It also omits buckets with fewer than two unique bodies because such a
bucket cannot witness a distinct-body pair. Raw reservation remains the
conservative sum of all persistent and overlay rows, while Debug still walks
projected raw chains for integrity accounting. This removes irrelevant writes
without changing earliest ownership, geometry invocation order, sleep diagnostic
order, hash-alias behavior, or exhaustion semantics.

## Exact Oracle

Final Debug executable SHA-256:
`CDBAD8358A717FD5CADD8D202582826F2A1F16278ECCC31C7E9BA9E8E2E9B3CC`

All 12 engine captures exited zero, contain 360 complete passes, and the 0/1/4
worker variants compare byte-for-byte within each workload.

| Workload | Workers | Bytes each | Stream SHA-256 | Receipt SHA-256 |
|---|---|---:|---|---|
| varied | 0, 1, 4 | 108,984 | `02BA43A97F947CE233E3C26EC19950BCB7E96A0A927C723349A85760D3B25F48` | `5488168BAB4185265BFBBF1F53F5C0CCAC87DC27B9C9BCB2D63709D4F0F7B977` |
| sparse4000 | 0, 1, 4 | 41,800 | `EC8C856F8B80A96375B2781FF125A0EA147A9529E6DE99483EFEFB045D17431D` | `65B8F75AC920691EA5EDB68A78EEE70E7B69A0B88A712B99CA4CDEAEFDCAD204` |
| sleepy5000 | 0, 1, 4 | 41,800 | `5854F5D037E40D356A42DC2E8EAA002DEF700BF05DCCBAF400F0193807BCFDBA` | `ADCF61E8917ABA5F71D6A34E04B42D01D4767BB91792D8CD926F7DAD136FB13B` |
| sleep-order | 0, 1, 4 | 53,320 | `CEAA1741405E8997152C6C966B2ECFAFDA0F01E7FC40D6C85332F987E0A69497` | `4F22D9E8D3681786BE1A4B9D8B0FE719CE99F679BFC8312A663B15749DEDDE90` |

Evidence root: `TestOutput/broadphase_pair_dedup_bd3_final_locked/`.

## Performance And Scale

`tools\validate_perf.bat` completed the absolute budgets, committed DX12 and
physics-bench comparisons, structural probes, scale matrix, and ready-build
check without a baseline refresh. The final comparison artifact measured DX12
Frame/Broadphase/CandidatePairs/GridMaintain averages of
0.8599/0.0724/0.0297/0.0386 ms and physics-bench values of
0.4809/0.0109/0.0064/0.0033 ms.

| Bodies / workload | Broadphase avg | Delta vs BD0 | CandidatePairs avg | Delta vs BD0 | GridMaintain avg | Delta vs BD0 |
|---|---:|---:|---:|---:|---:|---:|
| 37 varied | 0.0109 | +17.2% | 0.0064 | +25.5% | 0.0033 | +17.9% |
| 200 scale | 0.0378 | +8.0% | 0.0111 | +91.4% | 0.0228 | -9.5% |
| 520 scale | 0.1835 | +7.2% | 0.0432 | +54.8% | 0.1264 | -2.3% |
| 1,000 scale | 0.4185 | +10.1% | 0.1095 | +45.2% | 0.2635 | +1.6% |
| 2,000 scale | 1.0471 | +2.8% | 0.3236 | +18.8% | 0.5454 | -3.9% |
| 4,000 exact sparse | 0.4289 | -28.7% | 0.2067 | -41.9% | 0.2126 | -8.3% |
| 5,000 sleeping-heavy | 1.4014 | -14.2% | 0.1098 | -66.7% | 0.1668 | -3.4% |

The gain is deliberately reported as workload-dependent rather than smoothed
into one headline. It is smaller than hoped below 2,000 bodies: per-query slice
construction raises CandidatePairs time even where total Broadphase remains
within +2.8% to +17.2% of BD0. The intended default/large cases do win: exact
sparse 4,000 improves Broadphase 28.7% and CandidatePairs 41.9%, while the
sleeping-heavy 5,000 scene improves them 14.2% and 66.7%. The correct byte-exact
change therefore closes with a measured large-scene gain and an explicit
small-scene tradeoff rather than an overstated universal speedup.

## Memory Delta

The replacement storage is exactly `24B + 10,244` bytes at scene body count
`B`; the retired triangular store is `ceil(B(B-1)/2 / 64) * 8` bytes.

| Bodies | Dense bytes | Membership bytes | Delta |
|---:|---:|---:|---:|
| 37 | 88 | 11,132 | +11,044 |
| 200 | 2,488 | 15,044 | +12,556 |
| 520 | 16,872 | 22,724 | +5,852 |
| 1,000 | 62,440 | 34,244 | -28,196 |
| 2,000 | 249,880 | 58,244 | -191,636 |
| 4,000 | 999,752 | 106,244 | -893,508 |
| 5,000 | 1,562,192 | 130,244 | -1,431,948 |
| 8,192 | 4,193,792 | 206,852 | -3,986,940 |

Small scenes pay at most about 12.6 KiB for the bounded slices. The replacement
saves 0.852 MiB at 4,000 bodies, 1.366 MiB at 5,000, and 3.802 MiB at the 8,192
ceiling, while deleting the per-pass N-squared clear from production.

## Validation

| Command | Result |
|---|---|
| Final Debug/Profile builds | PASS; zero warnings and zero errors |
| Focused `SpatialGrid*` Debug suite | PASS; 27 cases / 8,650 assertions |
| `tools\validate_physics.bat` | PASS; byte-exact Physics and ready builds; no baseline movement |
| `tools\validate_physics_deep.bat` | PASS; deep deterministic stress/baseline matrix and ready builds |
| `tools\validate_perf.bat` | PASS; committed comparisons, absolute budgets, structural probes, scale matrix, and ready builds; no baseline refresh |
| `tools\validate_tests.bat` | PASS; 509 cases / 2,431,143 assertions |
| `tools\validate_fast.bat` | PASS in 374 s; metadata, dependency proof, all ownership inventories, builds, tests, ready-build checks, and compiled-symbol reachability |
| `tools\validate_format.bat` | PASS; 587 source files, 327 headers, and all repository-relative `Related:` paths clean |
| `git diff --check` | PASS |

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result |
|---|---|
| `SkullbonezSource/Physics/SpatialGrid.cpp` | PASS |
| `SkullbonezSource/Physics/SpatialGrid.h` | PASS |
| `SkullbonezTests/TestSpatialGrid.cpp` | PASS |

Checked: 3/3. Deferred: 0. A campaign-wide checklist was not required for this
touched-file audit. Learning headers and local comments describe the final
eligibility projection, raw-row proof, prefix lifetime, all-configuration
capacity guards, temporary Debug oracle boundary, and geometry-order invariant.

## Independent Review

The final read-only reviewer returned **ACCEPT** with no source blocker. Its
exhaustive model covered 98,304 observations without divergence and separately
verified singleton and alias-only omission, eligibility projection, multi-body
hash aliases, traversal/geometry order, conservative raw reservation,
all-configuration owner/range/slice guards, Debug raw-chain accounting, focused
test design, and comment accuracy. The sole documentation condition was the BD3
owner refinement recorded above and in the live plan.

## Next

BD4 removes the remaining Debug dense bitset, legacy same-state driver, temporary
pair-stream writer, geometry counter, scratch owners, and their exact governance
rulings. Permanent BD0–BD3 artifacts and the v2 decoder remain historical proof.

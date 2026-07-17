# Physics Broadphase Scale-Attribution Closure

Date: 2026-07-17

Plan: `physics-broadphase-scale-attribution`, B4 closure

Branch: `nightrunner-16th-july`

## Outcome

The campaign removed a real algorithmic scale cliff and, after independent
review, also removed the pre-existing silent coverage loss that caused it. At
2,000 bodies, the retained full-coverage design reports a 2.0517 ms Physics
Step and 0.8904 ms inclusive Broadphase. Versus B0's packed-source 7.5576 ms
Step that is a 72.85% improvement; versus B2's instrumented 6.6789 ms
Broadphase attribution it is an 86.67% improvement. The grid admits about
7,574 active cells instead of truncating at 4,096.

At 1,000 bodies the saturation tier remains cold and the final matrix reports a
1.0546 ms Step. Seven exact-tip pairs measure -1.61% Step, -7.34% inclusive
Broadphase, and -9.78% insertion medians. The final Step is 0.2546 ms above the
owner-ratified 0.80 ms target. S7 was not started, the SIMD default remains
OFF, and no baseline or golden was regenerated.

## What the campaign established

| Task | Closure evidence |
|---|---|
| B0 | Defined `Broadphase` as the inclusive owner interval, forbade adding nested marker rows to it, and kept S7 out of scope. |
| B1 | Replaced nested/double-counted grid markers with mutually exclusive `GridSetup`, `GridInsertScalar/Simd`, and `CandidatePairsScalar/Simd`; added reset-per-rebuild bounded counters and opt-in query schema 7. |
| B2 | Proved the 2,000-body scene filled all 4,096 primary slots and averaged 3,660 lost cell visits per sampled frame, each paying a 4,096-slot failed probe. Duplicate rejection was zero. |
| B3 | Rejected larger hot primary tables, proved the saturated-index mechanism with seven alternating pairs, then replaced its unsafe membership-only form with a bounded 4,096-row cold overflow tier. |
| B4 | Closed the independent-review blockers, including silent cell drops, unguarded post-grid append, missing deterministic collision/order coverage, and a compact-route post-overflow assertion. Formal tests, physics, allocation, and performance gates passed from the corrected source. |

## Correct accounting and before/after attribution

`Broadphase` is an inclusive total. The rows below are mutually exclusive
direct phases within it; they may be reconciled against the total but must not
be added on top of it.

| Scale / marker | B2 attribution | Retained design | Change |
|---|---:|---:|---:|
| 1,000 Broadphase | 0.2829 ms | 0.2741 ms | -3.11% gate-to-gate; paired median -7.34% |
| 1,000 GridSetup | 0.0042 ms | 0.0033 ms | -21.43% (tiny marker) |
| 1,000 GridInsertScalar | 0.2060 ms | 0.1970 ms | -4.37% gate-to-gate; paired median -9.78% |
| 1,000 CandidatePairsScalar | 0.0279 ms | 0.0279 ms | unchanged; paired median +0.35% |
| 1,000 FastSmallSweepAugment | 0.0430 ms | 0.0442 ms | +2.79% |
| 2,000 Physics Step | 7.5576 ms (B0 packed source) | 2.0517 ms | -72.85% |
| 2,000 Broadphase | 6.6789 ms (B2 instrumented) | 0.8904 ms | -86.67% |
| 2,000 GridSetup | 0.0092 ms | 0.0070 ms | -23.91% (tiny marker) |
| 2,000 GridInsertScalar | 6.3734 ms | 0.5876 ms | -90.78% |
| 2,000 CandidatePairsScalar | 0.1101 ms | 0.1138 ms | +3.36% restored-work cost |
| 2,000 FastSmallSweepAugment | 0.1831 ms | 0.1795 ms | -1.97% |

The final 1,000-to-2,000 growth is 1.95x for Step and 3.25x for Broadphase,
instead of B2's 23.61x Broadphase growth. The large removal remains
concentrated exactly where B2 predicted: saturated lookup during grid
insertion. The final 2,000-body cost is higher than the rejected
membership-only candidate because the engine now inserts and processes every
covered cell rather than discarding about 3,660 visits per sampled frame.

## Final performance matrix

The corrected-source `tools\validate_perf.bat` completed in 94.443 seconds.
Allocation policy, the gameplay allocation guard, absolute budgets, and
comparison gates passed.

| Bodies | Physics Step | Broadphase inclusive | GridSetup | GridInsertScalar | CandidatePairsScalar | FastSmallSweepAugment |
|---:|---:|---:|---:|---:|---:|---:|
| 200 | 0.0999 ms | 0.0209 ms | 0.0003 ms | 0.0154 ms | 0.0020 ms | 0.0026 ms |
| 520 | 0.8121 ms | 0.1351 ms | 0.0019 ms | 0.1102 ms | 0.0100 ms | 0.0114 ms |
| 1,000 | 1.0546 ms | 0.2741 ms | 0.0033 ms | 0.1970 ms | 0.0279 ms | 0.0442 ms |
| 2,000 | 2.0517 ms | 0.8904 ms | 0.0070 ms | 0.5876 ms | 0.1138 ms | 0.1795 ms |

At 1,000 bodies, reaching 0.80 ms still requires 0.2546 ms. That is 92.89% of
the current inclusive Broadphase interval, so another arithmetic-kernel-only
campaign cannot plausibly close the gap. Sleeper-aware incremental maintenance
remains a possible future experiment, but it requires a fresh owner plan and
proof that awake movers still find sleeping targets.

The exact-tip paired candidate is commit `91da621c8`, SHA-256
`AE0F2E627F74B85C7D42D4A4F7BE21DC970F9A97AAE0042D260F5A59F3B6BCFB`
(4,710,400 bytes). The pre-B3 control SHA-256 is
`AB9DDC514C895AF5A7B886D0530FF0457E6C6F8918C2EEC0E01F3F59B4529D03`
(4,692,480 bytes). Both lanes used SIMD OFF and
`SKULLBONEZ_PLATFORM_PROFILER_MARKERS=0`; all 28 stderr logs were empty. The
seven-pair suite took 227.601 seconds and produced 28 CSVs totaling 19,474,148
bytes. Every per-pair average and range is recorded in
`broadphase-saturated-lookup-experiment.md`.

## Capacity, allocation, and deterministic order

The retained structure has an unchanged 4,096-row primary table, a 16,384-slot
fixed lookup, and a 4,096-row cold overflow array (160 KiB total cold storage).
Each grid allocates that exact block once at physics-owner startup under an
owner/phase/cap allowlist row; it never grows during gameplay. Keeping it
out-of-line preserves common owner layout, and `CollectDynamicMemoryBytes()`
adds all 160 KiB to the broadphase diagnostics/budget total.
At no more than 8,192 keys,
the lookup remains at or below 50% load and always has an empty sentinel. A
compile-time assertion prevents a valid combined bucket index from colliding
with the `uint16_t` sentinel.

Primary rows preserve their existing probe/admission order. Overflow rows use
sequential fixed admission and traversal order. The focused collision test
chooses two overflow keys with the same low-14-bit home slot, proves both exact
pairs, then clears and rebuilds in reverse cell order to lock the reversed
deterministic group order. The 8,193rd distinct cell is Lane F, with
`Physics/SpatialGrid`, capacity 8,192, active count 8,192, and
`phase=steady_gameplay` in its tested diagnostics.

The post-grid conservative candidate append now checks `size < capacity`
before `emplace_back`; equality is fatal through owner
`Physics/PhysicsBroadphaseStage`. The pre-existing `SpatialGrid` pair emitter
retains its own reserve guard. The performance allocation guard reported zero
steady-gameplay allocations and no reserve-policy violations. Saturated copy
construction and assignment are independently tested: each grid owns a
distinct cold block, source/target clears do not alias, and assignment copies
into its existing block without allocating.

## Behavior and trace evidence

The formal physics gate retained the byte-exact 44,401-line varied-scene
oracle. The scale fixtures expose a separate historical boundary: the old
2,000-body result was deterministic but incomplete because the grid silently
dropped cells after 4,096.

| Scale | Old capped trace | Retained trace | Disposition |
|---:|---|---|---|
| 1,000 | 57,838,185 bytes / `1C947D9944CC9982961DDABA8A528D4BC8A67953B1763F88D3369D8B782C3DE7` | same bytes and SHA-256 | byte-identical |
| 2,000 | 115,957,299 bytes / `79883E27AA2E39262A4D008D679C1DCEA6DAA50C089D0780D084A194DEE7BE18` | 116,018,437 bytes / `92395750940BD8B50AAE04A375932867DA873B902D464F74D9F7CD1CD06EB054` | intentional coverage correction |

The corrected 2,000-body query samples average 7,574.25 active cells and
8,335.5 exact visits/entry writes. B2 sampled 4,096 active cells, about 8,342
visits, and only 4,682 writes. Candidate generation remains bounded and modest:
the corrected first four frames average 829.75 raw combinations, 526 unique
pairs, and 50.5 emitted candidates.

## SkullScope commands and data accounting

The bounded B2 trace commands were:

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_1000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_2000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson
```

The corrected trace commands were:

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_1000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_2000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson
```

The exact bounded queries were:

```bat
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f_overflow.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f_overflow.physicsdiag.ndjson broadphase --attribution --frames 25:29 --limit 5
```

| Evidence | NDJSON bytes | SQLite bytes | Summary GPT-read | Attribution GPT-read | Truncation |
|---|---:|---:|---:|---:|---|
| B2 1,000 | 57,838,185 | 28,676,096 | 5,203 chars/bytes | 2,344 chars/bytes | none |
| B2 2,000 | 115,957,299 | 57,495,552 | 5,255 chars/bytes | 2,348 chars/bytes | none |
| Corrected 1,000 | 57,838,185 | 28,676,096 | 5,222 chars/bytes | 2,362 chars/bytes | none |
| Corrected 2,000 | 116,018,437 | 57,536,512 | 5,231 chars/bytes | 2,366 chars/bytes | none |
| Additional 1,000 review queries | reused | reused | 0 | 5,206 chars/bytes | none |
| **Total model-ingested query output** | | | **20,911 chars/bytes** | **14,626 chars/bytes** | **none** |

Total GPT-read query output was 35,537 characters and 35,537 UTF-8 bytes. No
raw NDJSON or SQLite content was ingested. The rejected membership-only B3
comparison exposed zero additional query bytes; it compared sizes and hashes.
One corrected accounting wrapper executed the first summary but failed before
emitting query content because `TextEncoder` was unavailable; the canonical
rerun is counted above.

Two initial B2 600-frame GUI launches were explicitly stopped by PID after the
shell returned early. Their partial 1,124,267,058-byte and 981,467,136-byte
traces were deleted and never queried. The bounded 30-frame commands above are
the authoritative artifacts. Corrected trace times were 11.181 seconds at
1,000 bodies and 18.198 seconds at 2,000 bodies.

## Validation and review inventory

- `tools\validate_tests.bat`: 290/290 cases and 21,452/21,452 assertions passed
  in 7.450 seconds after compact-transition coverage.
- `tools\validate_fast.bat`: formatting, metadata, Profile/Debug zero-warning
  builds, and the embedded unit suite passed in 62.640 seconds.
- `tools\validate_physics.bat`: standalone smoke passed; the 44,401-line oracle
  was byte-exact; Debug/Profile builds had zero warnings/errors; 48.517 seconds.
- `tools\validate_perf.bat`: allocation policy/guard and all performance
  budgets/comparisons passed with the 160 KiB block included; 94.443 seconds.
- `python tools\check_allocation_policy.py --self-test` and `--repo .` passed;
  the repository scan reported 380 files and zero allowlist errors.
- The campaign touched ten tracked source-bearing files: `SkullScope.cpp`,
  `SpatialGrid.cpp`, `SpatialGrid.h`, `SolverBroadphaseStage.h`,
  `PhysicsBroadphaseStage.cpp`, `TestRuntimeContracts.cpp`,
  `TestSolverBroadphaseStage.cpp`, `TestSpatialGrid.cpp`,
  `check_physics_query_regression.py`, and `physics_query.py`. Final comment
  audit reconciliation: 10/10 checked, 0 deferred, 0 unchecked; checklist path
  N/A in touched-file mode.
- CodeGraph was synced after the final source changes.

## Independent review

The first whole-campaign pass found three credible blockers:

1. The membership-only lookup preserved silent new-cell drops after 4,096.
2. `AppendCandidatePairIfMissing` could grow its vector after the fixed reserve.
3. Focused tests did not lock secondary collisions and deterministic multi-pair
   order.

All three are addressed above. The re-review found one accounting hole and two
evidence gaps: the out-of-line 160 KiB was absent from the stage
memory total, saturated copy/assignment lacked tests, and reports still showed
the earlier candidate. Exact accounting, independent-copy coverage, final-pair
tables, provenance labels, and the final matrix now close those findings; the
affected unit and performance gates were rerun.

The final hot-path review then caught a fourth blocker: a compact bounds call
could admit one overflow cell and assert on a later primary miss because the
post-admission active count exceeds 4,096. The invariant now accepts every
physically saturated count, impossible pre-saturation exhaustion is Lane-F
fatal in Release, and a three-cell compact transition test complements the
66-cell uncommon-route test. The reviewer cleared completeness, order,
capacity, fatal behavior, determinism, comments, and the final average-based
evidence after that fix.

## Final disposition

Retain the bounded saturated lookup and cold overflow tier. Reject both larger
hot primary tables and the unsafe membership-only lookup. Retain the exclusive
marker/counter/query contract as the diagnostic surface.

The SoA layout and packed sleep scratch flags remain banked. Dark SIMD kernels
remain default-OFF and S7 remains unchecked. Only fresh owner direction can
authorize a future cutover or another performance campaign.

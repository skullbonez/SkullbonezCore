# Broadphase Pair Dedup Cost - BD0 Baseline And Equivalence Oracle

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Main baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Task-start tip: `6514636cd7e02588d204d8bd7f75db4bcb581f44`
Plan progress: 1/5
Active/future portfolio progress: 1/20 (5%)

## Outcome

BD0 locks the old dense triangular dedup mechanism before replacement design.
The permanent evidence contains complete ordered pair lists at all five stage
boundaries, exact geometry-filter invocation counts, exact live clear spans,
exact committed `pairSeen` memory, and uninstrumented Profile timings from 37 to
5,000 bodies. Complete streams are byte-identical with 0, 1, and 4 workers.

No production, Profile, or Release behavior changed. The evidence writer,
predicate counter, and ownership state are Debug-only; the focused test and
decoder pin the first-seen contract and the artifact structure. No baseline or
golden was refreshed.

## Exact Clear And Memory Cost

For `N` live bodies, `SpatialGrid::ResetCandidatePairDedup` computes:

```text
pair identities = N * (N - 1) / 2
cleared words   = ceil(pair identities / 64)
explicit bytes = cleared words * 8
```

It then calls `pairSeen.ResetDefault(clearedWords)`, whose value construction
zeros the same `uint64_t` span, followed by an explicit `memset` over the span.
The table reports the exact logical words and explicit `memset` bytes. The two
source-level zeroing operations are real work, but this report does not claim
twice the bytes as measured hardware traffic because compiler and cache behavior
were not instrumented.

| Bodies | Pair identities | Words/pass | Explicit bytes/pass | `pairSeen` bytes at exact scene capacity |
|---:|---:|---:|---:|---:|
| 4 | 6 | 1 | 8 | 8 |
| 37 | 666 | 11 | 88 | 88 |
| 200 | 19,900 | 311 | 2,488 | 2,488 |
| 520 | 134,940 | 2,109 | 16,872 | 16,872 |
| 1,000 | 499,500 | 7,805 | 62,440 | 62,440 |
| 2,000 | 1,999,000 | 31,235 | 249,880 | 249,880 |
| 4,000 | 7,998,000 | 124,969 | 999,752 | 999,752 |
| 5,000 | 12,497,500 | 195,274 | 1,562,192 | 1,562,192 |
| 6,000 | 17,997,000 | 281,204 | 2,249,632 | 2,249,632 |
| 8,192 ceiling | 33,550,336 | 524,224 | 4,193,792 | 4,193,792 |

`ReserveSceneCapacity` receives the actual Physics scene body count, not the
JSON `modelCapacity` hint. The 5,000-object sleepy scene therefore commits
195,274 words / 1,562,192 bytes even though its JSON says 6,000. Capacity logs
for the exact 4,000 and 5,000 runs report those values with 100% live
utilization. `PhysicsBroadphaseStage::CollectDynamicMemoryBytes` includes the
grid once, and `CollectPhysicsWorldMemoryBytes` includes that stage total once;
the rows above are therefore the exact `pairSeen` contribution to world dynamic
memory, not an estimate or duplicated accounting value.

At 4,000 bodies, the explicit clear is 999,752 bytes per pass. Across the fixed
360-pass oracle this is 359,910,720 explicit bytes; the 5,000-body workload
clears 562,389,120 explicit bytes. A frame can execute up to eight physics
passes, so the default-capacity upper frame span is 7,998,016 explicit bytes.

## Uninstrumented Profile Timing

All values below are milliseconds from Profile executable SHA-256
`343E2F5CCD2412E132F100295F961849C4588FA58C3C9F13BBE5E44FEDC6F275`
at source commit `6514636c`. The exact JSON artifacts and hashes are preserved in
`broadphase-pair-dedup-perf/`.

| Bodies / workload | Samples | Broadphase avg / p50 / p99 | CandidatePairs avg / p50 / p99 | GridMaintain avg / p50 / p99 |
|---|---:|---:|---:|---:|
| 37 varied | 2,340 | 0.0093 / 0.0090 / 0.0211 | 0.0051 / 0.0053 / 0.0097 | 0.0028 / 0.0023 / 0.0099 |
| 200 scale | 1,140 | 0.0350 / 0.0309 / 0.0942 | 0.0058 / 0.0051 / 0.0120 | 0.0252 / 0.0212 / 0.0766 |
| 520 scale | 1,140 | 0.1712 / 0.1755 / 0.3002 | 0.0279 / 0.0231 / 0.0630 | 0.1294 / 0.1382 / 0.2234 |
| 1,000 scale | 1,140 | 0.3800 / 0.3849 / 0.7165 | 0.0754 / 0.0605 / 0.1731 | 0.2594 / 0.2810 / 0.4334 |
| 2,000 scale | 1,140 | 1.0185 / 0.9796 / 2.0329 | 0.2723 / 0.2188 / 0.6382 | 0.5674 / 0.6022 / 0.9449 |
| 4,000 exact sparse, no sleep | 1,140 | 0.6015 / 0.6137 / 0.9842 | 0.3555 / 0.3545 / 0.5147 | 0.2319 / 0.2429 / 0.4946 |
| 5,000 sleeping-heavy | 1,140 | 1.6324 / 0.5103 / 26.2522 | 0.3299 / 0.3264 / 0.5843 | 0.1726 / 0.1710 / 0.3271 |

These are workload observations, not a smooth scaling fit: the 4,000 scene is a
sparse exact-capacity input with sleep disabled, while the 5,000 scene is the
tracked sleeping-heavy stress workload. BD3 must rerun the same artifacts and
report deltas workload by workload.

## Byte-Exact Oracle Matrix

Every workload has 360 structurally complete passes. Grid and total geometry
counts are equal in this matrix, so fast-sweep augmentation called no additional
geometry predicate and added no pair; the separately encoded augmented/final
boundaries still make that fact explicit rather than inferred.

| Workload | Bodies / capacity | Clear B/pass | Grid / total geometry calls | Raw-grid candidates sum / max | Augmented sum / max | Final solver sum / max | Raw / final sleep sum / max |
|---|---:|---:|---:|---:|---:|---:|---:|
| varied | 37 / 37 | 88 | 7,056 / 7,056 | 2,794 / 10 | 2,794 / 10 | 2,794 / 10 | 8 / 8, max 1 |
| sparse4000 | 4,000 / 4,000 | 999,752 | 1,282,442 / 1,282,442 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| sleepy5000 | 5,000 / 5,000 | 1,562,192 | 1,473,942 / 1,473,942 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| sleep-order | 4 / 4 | 8 | 1,080 / 1,080 | 0 / 0 | 0 / 0 | 0 / 0 | 720 / 720, exactly 2 |

The sleep-order stream contains `(1,2)` followed by `(0,1)` on every pass.
Pair `(0,2)` shares a cell but fails geometry. The matching focused test proves
three geometry calls for the three unique first-seen identities even though the
three bodies overlap several cells, pinning both first-seen ordering and the
once-per-identity filter contract.

The complete files, exact scene inputs, hashes, commands, v2 schema, and
worker-equivalence receipts are in `broadphase-pair-dedup-oracles/`. The decoder
rejects malformed magic, wrong endianness/version/width, nonsequential or
mismatched ordinals, count-derived truncation, invalid or duplicate pairs,
incorrect record/global lengths, boundary subset violations, and trailing
bytes. `--require-identical` then compares all bytes.

## Implementation Boundary

- `SKORE_PAIR_STREAM_ORACLE` is opt-in and Debug-only. Each broadphase owner
  receives an atomic file suffix so live/prediction/replay streams cannot
  interleave.
- Geometry invocation accounting is thread-local and reset/sampled around one
  synchronous stage call, so concurrently stepped worlds on other threads do
  not contaminate one another.
- The stream borrows the existing, scene-reserved P1 normalization scratch;
  pair-stream and P1 execution are mutually exclusive. No fourth Debug list,
  registry row, memory-accounting term, or gameplay allocation was added.
- File buffering is disabled and checked at the cold open. Writes, record
  structure, trailer construction, and final `fclose` are all checked.
- Profile/Profile-WPO do not define `_DEBUG`, so the writer, file state,
  counter, and instrumentation compile out.

This Debug evidence scaffolding remains available through BD3 comparison. BD4
must remove or explicitly adjudicate it with the old mechanism; the permanent
binary baselines and decoder remain the comparison record.

## Review And Validation

Independent read-only review found three issues before artifact freeze: v1 did
not literally serialize post-augmentation/final candidates, a fourth fixed-list
owner would have invalidated registry/memory tests, and process-global counters
plus unchecked file finalization weakened evidence integrity. Version 2 records
all five boundaries; existing mutually exclusive scratch is reused; counters
are thread-local/atomic as appropriate; and open/write/close plus per-record
structure are checked. Repeat review found those remedies sound and identified
no production/Profile path.

Focused results before the mapped gates:

- first-seen fixture: 1 case / 5 assertions pass;
- scene-capacity registry fixture: 1 case / 6,665 assertions pass with the
  unchanged 108 Debug rows;
- broadphase owning-memory fixture: 1 case / 2 assertions pass;
- v2 decoder: 12/12 streams pass; all four 0/1/4 worker groups are byte-identical;
- Debug solution build: zero warnings and zero errors.

Final-source commit-preparation results:

| Check | Result | Evidence |
|---|---|---|
| `tools\\validate_tests.bat` | Pass | Debug and Profile unit suites completed on the final source snapshot. |
| `tools\\validate_physics.bat` | Pass | Determinism gate completed with zero warnings, zero errors, and no baseline movement. |
| `tools\\validate_fast.bat` | Pass | Formatting, project filters, dependency proof, ownership inventories, staged-size policy, Profile/Debug builds, tests, ready-build checks, and compiled-symbol reachability all passed. |
| `python tools\\inventory_unreachable_symbols.py --repo . --strict` | Pass | 81 current rows are ruled and blocking diagnostics are zero. The two Debug-only oracle writers carry exact `repair-plan` rulings tied to `Agentic/Plans/TODO/broadphase-pair-dedup-cost.md`; BD4 owns their removal or adjudication. |
| `tools\\validate_build.bat Automation` | Pass | The Automation object root was refreshed after repository formatting touched unchanged source timestamps; the build completed with zero warnings and zero errors. |
| `tools\\validate_perf.bat` | Pre-existing comparison failure; absolute budgets pass | Both the task-start and final runs exit 7 on historical whole-frame/GPU comparisons unrelated to this Debug-only instrumentation. Final `physics_bench` Broadphase/CandidatePairs/GridMaintain averages are 0.0095/0.0052/0.0031 ms versus the locked uninstrumented task-start 0.0093/0.0051/0.0028 ms. Both DX12 and physics-bench absolute budgets pass, no baseline was refreshed, and the task-start Profile evidence records the same external comparator condition. |

The final independent read-only review accepted first-seen ordering, all five
capture boundaries, thread isolation, scratch ownership, binary structure,
decoder rejection behavior, Profile isolation, artifacts, bookkeeping, and
comment quality. Its sole blocking finding was the two missing reachability
rulings; those exact `repair-plan` rows were added and the strict three-root
inventory then passed with zero diagnostics.

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/SolverBroadphaseStage.h` | Pass | Header and local invariant explain Debug-only, thread-local exact invocation ownership. |
| `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` | Pass | Header defines all stream boundaries, scratch ownership, reservation, and Profile isolation. |
| `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` | Pass | Header plus local lifetime/hazard/invariant comments cover multi-world suffixing, unbuffered I/O, encoding, record integrity, and scratch reuse. |
| `SkullbonezTests/TestSpatialGrid.cpp` | Pass | Header and focused hazard comment explain traversal-first order, rejected geometry, and once-per-identity observation. |
| `tools/check_broadphase_pair_stream_oracle.py` | Pass | Full tool header and named decode invariants cover structure, semantics, and explicit write authority. |

Checked: 5/5. Deferred: 0.

## Next

BD1 must evaluate all three mandated replacement families against these exact
first-seen, invocation-count, raw-sleep-order, augmented, and final-list
contracts. A mechanism that cannot preserve every byte is rejected before
implementation; this plan carries no divergence or baseline-refresh authority.

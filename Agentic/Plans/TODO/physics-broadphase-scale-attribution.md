# Physics Broadphase Scale Attribution And Cell-Insertion Campaign

Date: 2026-07-17
Status: ACTIVE — B0-B3 complete; independent-review corrections are in B4
Owner: Physics broadphase (`PhysicsBroadphaseStage` and `SpatialGrid`)
Branch: `nightrunner-16th-july`

## Owner Direction

Explain and reduce the superlinear broadphase cost in the fixed-seed scale
matrix. Show performance evidence while working, stop double-counting nested
broadphase markers, and execute this campaign to completion.

The separate `physics-soa-simd-1000-bodies` campaign remains PAUSED at 7/9.
Its S7 cutover is rejected for the current evidence and MUST NOT start during
this campaign. The SIMD toggle remains dark by default and no golden or
behavioral baseline refresh is authorized here.

## Problem And Current Evidence

The final packed-source performance gate on 2026-07-17 measured:

| Bodies | Physics Step avg | Broadphase avg | GridBuild avg | ScalarBounds avg |
|---:|---:|---:|---:|---:|
| 1,000 | 0.9652 ms | 0.2731 ms | 0.2282 ms | 0.1942 ms |
| 2,000 | 7.5576 ms | 6.5069 ms | 6.3252 ms | 6.2088 ms |

`Broadphase` is the inclusive owner total. `GridBuild` is nested inside it,
and `ScalarBounds` is nested inside `GridBuild`; adding those values reports
the same work two or three times. Worse, `ScalarBounds` currently encloses
bounds arithmetic *and* every `SpatialGrid::Insert*` call, so the marker name
cannot attribute the 2,000-body cliff to arithmetic.

Source inspection identifies a concrete, unproven hypothesis:
`SpatialGrid::InsertCell` scans the full occupied bucket chain to reject a
same-object duplicate for every exact AABB cell visit. Exact AABB enumeration
visits each cell once, so that scan may be redundant on the common path and
may turn crowded-cell insertion superlinear. Sampled oversized sweeps can
revisit cells and still require duplicate rejection. B1-B2 must measure this
before B3 changes it.

## Goal

1. Make every direct broadphase child marker a mutually exclusive phase and
   report the inclusive parent separately.
2. Attribute the 1,000→2,000 scale cliff with bounded, allocation-free grid
   counters and focused SkullScope queries.
3. Implement only the evidence-selected deterministic optimization.
4. Preserve candidate completeness/order, byte-exact physics, fixed-capacity
   runtime behavior, and the default-OFF SIMD contract.
5. Close with repeated same-tip A/B evidence, final mapped validation, an
   independent review, and an explicit record that S7 was not started.

## Non-Goals

- No S7 SIMD cutover, toggle-default change, golden regeneration, or replay
  provenance edit.
- No fast-math, reassociation, FTZ/DAZ, nondeterministic container, runtime
  allocation, broadphase false-negative policy, or object-identity change.
- No blanket exclusion of sleeping bodies. A moving awake body must still be
  able to find and wake a sleeping collision target.
- No cell-size or scene-data tuning until counters disprove the insertion
  hypothesis; authored data changes are outside the first-line remedy.
- No sum of `Broadphase` with any descendant marker. Reports show the parent
  total and an exclusive-child breakdown as two views of the same interval.

## Measurement Contract

- Performance executable: Profile x64, explicit `--physics-simd-kernels off`.
- Scenes: fixed-seed 1,000 and 2,000 body scale scenes; 1,140 measured frames
  per perf artifact after the profiler warmup.
- Before/after decision: save the pre-change executable and SHA-256, build the
  candidate from the same git tip, then run seven alternating process pairs
  per scene. Use median paired deltas; do not compare unrelated gate runs.
- Required timing rows: Physics Step, inclusive Broadphase, GridSetup,
  GridInsertScalar, CandidatePairsScalar, and the remaining exclusive direct
  children. SIMD marker names stay parallel but are not cutover evidence.
- Required grid facts per sampled frame: bodies inserted, exact-AABB cell
  visits, sampled-sweep cell visits, entry writes, duplicate rejections,
  bucket-chain entries inspected, active cells, maximum occupancy, raw pair
  combinations, unique pairs, filter rejects, and emitted candidates.
- SkullScope raw NDJSON/SQLite files remain on disk. The model reads only
  bounded `summary`/`broadphase` query output, with exact commands and byte/
  character accounting recorded in the attribution report.
- Every report labels markers `inclusive` or `exclusive`. Direct exclusive
  children may be summed; an inclusive parent is never added to descendants.
- All user-requested work and substantial builds, launches, queries, and
  validation runs record elapsed wall time.

## Decision Tree

1. If B2 shows bucket-chain inspection dominating exact insert work and exact
   inserts produce no legitimate duplicate rejection, B3 separates unique
   exact-cell insertion from duplicate-aware sampled-sweep insertion.
2. Otherwise, retain the instrumentation and choose the largest measured
   exclusive phase:
   - pair combinations/dedup → bounded bucket/pair-generation change;
   - cell visits/active cells → same-tip cell-size experiment, with candidate
     completeness tests and no authored-default change unless independently
     justified;
   - sampled sweeps → preserve CCD coverage while reducing repeated sampled
     cell work.
3. Sleeper exclusion requires a separate static/dynamic-grid design and proof
   that awake movers still pair with sleepers. It is not an implicit fallback.
4. A candidate is reverted if 1,000-body performance regresses beyond process
   noise, order changes for already admitted cells, the 44,401-line oracle
   changes, or any required validation fails. After independent review exposed
   the legacy saturated-cell drops as a completeness bug, deterministic output
   added solely by admitting those missing cells is a required correction.

## Tasks

- [x] **B0 — register the evidence and execution contract.**
  - Record the corrected inclusive interpretation of the existing markers,
    the fixed-seed current measurements, the testable insertion hypothesis,
    the same-tip A/B method, the decision tree, and the S7 prohibition.
  - Register 1/5 in MASTER and SessionState. Documentation-only; no repository
    validation required.

- [x] **B1 — make accounting exclusive and expose bounded attribution facts.**
  - Replace the nested `GridBuild/{Scalar,Simd}Bounds` timing tree with direct,
    non-overlapping broadphase children: GridSetup, GridInsertScalar/Simd, and
    CandidatePairsScalar/Simd. Retain `Broadphase` only as the inclusive total.
  - Add one reset-per-rebuild `SpatialGridFrameStats` value. Counter updates are
    serial, fixed-width, allocation-free, and do not participate in replay or
    physics decisions.
  - Extend the existing SkullScope broadphase row and query schema with the
    bounded counters; extend focused grid/query tests so missing, renamed, or
    semantically wrong fields fail.
  - Inspect every touched source-bearing file with the comment-style audit.
  - Gate: focused tests while iterating; before commit run
    `tools\validate_tests.bat`, `tools\validate_fast.bat`,
    `tools\validate_physics.bat`, `tools\validate_perf.bat`, and
    `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`.
  - Evidence (2026-07-17): the direct children are now `GridSetup`, one of
    `GridInsertScalar/Simd`, and one of `CandidatePairsScalar/Simd`; the
    reset-per-rebuild counters flow through opt-in SkullScope query schema 7.
    Tests passed 285/285 with 21,419 assertions; fast validation passed with
    zero-warning Profile/Debug builds; the focused query packet remained an
    exact golden match; physics retained the byte-exact 44,401-line oracle;
    performance completed with clean allocation/budget checks; and the bounded
    two-frame platform-profiler launch exited 0. Touched-file comment audit:
    7/7 checked, 0 deferred, 0 unchecked (checklist path N/A in touched-file
    mode). The initial unbounded marker launch was interrupted after it exposed
    the missing frame limit; the authoritative rerun used `--frames 2`.

- [x] **B2 — collect and publish scale attribution before optimizing.**
  - Generate fixed-seed Profile artifacts for 1,000 and 2,000 bodies with the
    new exclusive markers and explicit SIMD OFF.
  - Generate bounded Debug SkullScope traces, run focused summary/broadphase
    queries, and record raw artifact sizes separately from GPT-read output.
  - Publish a report that reconciles inclusive Broadphase against its direct
    exclusive phases, compares per-body/cell/pair counter growth, identifies
    the dominant mechanism, and names the single B3 candidate selected by the
    decision tree.
  - Acceptance: no optimization code lands in B2; every claim is traceable to
    a marker or counter and no marker interval is counted twice.
  - Documentation-only commit after evidence collection; no additional gate.
  - Evidence (2026-07-17):
    `../../Reports/2026-07-17/broadphase-scale-attribution.md` rejects the
    duplicate-scan hypothesis. `GridInsertScalar` grows 30.94x because the
    2,000-body scene fills all 4,096 hash slots and averages 3,660 full-table
    misses per queried frame. B3 is restricted to a lower-load fixed hash table
    that preserves deterministic order without saturated full-table misses.

- [x] **B3 — implement and prove the evidence-selected algorithmic fix.**
  - Implement only the B2-selected path. Once the unchanged 4,096-cell primary
    table saturates, build a fixed compact lookup that avoids rescanning every
    primary slot while preserving conservative cell coverage.
  - Add regression/property coverage for candidate completeness, normalized
    deterministic order, exact-cell entry counts, and sampled-sweep duplicate
    suppression. Preserve capacity diagnostics and fatal lanes.
  - Save the control executable, record both executable SHAs, and run seven
    alternating same-tip pairs at 1,000 and 2,000 bodies. Report every pair,
    medians, ranges, and the exclusive-phase movement.
  - Acceptance: 1,000-body is neutral or faster; 2,000-body improvement is
    repeatable; already admitted cells keep deterministic order; the byte-exact
    physics oracle is unchanged; and saturation never drops a conservative
    cell silently. A failed candidate is reverted and B2's next measured path
    is attempted without weakening the contract.
  - Gate before commit: `tools\validate_tests.bat`,
    `tools\validate_physics.bat`, and `tools\validate_perf.bat` (plus
    `tools\validate_fast.bat` if tooling changes after B1).
  - Evidence (2026-07-17):
    `../../Reports/2026-07-17/broadphase-saturated-lookup-experiment.md`
    records two rejected larger-primary-table candidates and the seven-pair
    mechanism proof. B4 review then rejected the first 16 KiB membership-only
    lookup because it preserved silent cell drops. The retained correction uses
    a 16,384-slot lookup plus 4,096 cold fixed buckets, admits about 7,574 cells
    in the 2,000-body scene, and fails fatally only at 8,193 distinct cells.
    Its formal matrix still improves 2,000-body Step/Broadphase/grid insertion
    by 73.51%/86.61%/90.72% (Step versus B0's packed-source reference;
    Broadphase/insert versus B2's instrumented attribution). Seven final pairs
    classify 1,000-body Step as neutral at +0.65% median (-0.45% to +2.45%)
    while disclosing Broadphase/insert medians of +8.11%/+11.12%; 2,000-body
    Step repeats at -75.33%. The fixed 160 KiB tier is startup-preallocated,
    fully memory-accounted, and deep-copy/assignment tested. The 1,000-body trace stays byte-identical;
    the 2,000-body trace changes intentionally because missing coverage is
    restored. Tests pass 288/288 with 21,444 assertions, the physics oracle
    keeps its exact 44,401-line baseline, and the allocation/performance gate
    completes cleanly.

- [ ] **B4 — final review, final matrix, and campaign closure.**
  - Rerun the final 200/520/1,000/2,000 performance matrix from the exact
    source tip and publish a closure report with before/after exclusive
    accounting, determinism evidence, validation output, and timings.
  - Run one fresh independent whole-campaign rubber-duck review covering
    candidate completeness, CCD/sample duplicate handling, capacity/fatal
    behavior, marker accounting, hot-path allocation, determinism, tests, and
    report honesty. Fix every credible finding and rerun affected gates.
  - Reconcile the comment audit and `git ls-files` inventory for every touched
    source file. Sync CodeGraph after the source changes.
  - Update the SoA/SIMD plan, MASTER, and SessionState: S7 was explicitly
    rejected on current evidence and not started; its checkbox stays open and
    the campaign remains paused at 7/9 unless the owner later issues a fresh
    direction.
  - Delete this completed plan under MASTER inventory rule 4, retain the
    reports as evidence, commit the 5/5 closure, and push the branch.

## Validation Map

| Scope | Required gate |
|---|---|
| `SpatialGrid*`, broadphase stage, physics diagnostics | `tools\validate_tests.bat`, `tools\validate_physics.bat`, `tools\validate_perf.bat` |
| `tools/physics_query.py` or query regression data | `tools\validate_fast.bat`, then the changed focused query check |
| Profiler marker names | `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` |
| Documentation-only B0/B2 | No repository validation |
| Final uncertain aggregate | Add `tools\validate_full.bat` only if the final diff crosses beyond the mapped physics/tool scope |

All Profile/Debug builds must report zero warnings and zero errors. Physics
validation must keep the 44,401-line varied-scene CSV byte-exact. No baseline,
golden, or provenance refresh is part of this campaign.

## Closure Evidence

B4 must leave:

- `Agentic/Reports/2026-07-17/broadphase-scale-attribution.md`;
- `Agentic/Reports/2026-07-17/broadphase-scale-optimization.md`;
- `Agentic/Reports/2026-07-17/broadphase-scale-closure.md`;
- final validation commands/results and run timings;
- independent-review prompt/result accounting;
- comment-audit checked/deferred/unchecked counts;
- explicit SoA/SIMD S7 no-go wording with S7 still unchecked.

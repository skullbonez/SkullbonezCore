# Physics Scale Optimization

Date: 2026-09-12
Status: Active by owner direction — 6/7 phases complete
Plan ID: `PHYSICS_SCALE`
Owner: Physics broadphase and force stages; performance tooling owns measurement and regression checks
Impact areas: Physics, collision candidate generation, joint filtering, mutual gravity, tests, and performance tooling

## Goal And Scope

Reduce fixed-step CPU cost as body counts grow, starting with the fast-body
broadphase sweep. Preserve the exact candidate stream, Physics arithmetic,
sleep/wake behavior, replay results, and zero-allocation runtime contract.

The owner reactivated this plan on 2026-09-12 and directed complete execution
on `codex/unified-ui`, with measured performance benefits and no baseline
modifications. This instruction overrides every baseline-transition allowance
below. Execution follows
`Agentic/Skills/orchestrator/SKILL.md`. All paths in this plan are
repository-relative.

The work covers sweep data reuse and duplicate lookup, spatial pruning of the
sweep fallback, joint-pair exclusion, bounded parallel mutual gravity above
512 bodies, and scale-specific performance checks. Solver iteration changes,
approximate gravity, new collision modes, sleep-policy changes, GPU Physics,
and the remaining owner-parked Physics plans are outside this scope.

## Starting Evidence

The review inspected source at `f4cba2f0b` and historical local Profile artifacts
marked `25ecc9734`. Broadphase changes between those revisions retained the
full-world sweep loop, repeated geometry calculations, and linear duplicate
search. These measurements motivate work; they are not current timings or
promised savings. PS0 must record the implementation checkout and rebuild.

| Historical workload | Physics average | Sweep augmentation average | Sweep share | Local artifact |
|---|---:|---:|---:|---|
| 1,000 bodies | 1.5313 ms | 0.3403 ms | 22.2% | `Profile/physics_scale_1000_perf.json` |
| 2,000 bodies | 3.5742 ms | 1.4363 ms | 40.2% | `Profile/physics_scale_2000_perf.json` |
| 5,000 bodies, 1,000 awake | 12.2876 ms | 10.6112 ms | 86.4% | `Profile/physics_scale_sleepy_5000_perf.json` |

The 2,000-body sweep p99 was 8.6105 ms; the sleeping-heavy sweep p99 was
242.9738 ms. Current scene durations and executable contents may differ from
those historical runs. Local artifacts may be absent on another checkout;
reproduction must use committed workloads and freshly identified producers.

Confirmed source opportunities:

| Owner and source | Current work | Intended improvement |
|---|---|---|
| `PhysicsBroadphaseStage.cpp`, `AppendFastSmallSweepPairs` | Every qualifying fast mover scans every body | Query conservative spatial candidates before the existing exact admission predicates |
| Same file, `SweptSegmentTouchesExpandedBody` and `AppendCandidatePairIfMissing` | Repeated collider-center/radius calculations and a linear search through accumulated pairs | Stage-owned geometry scratch and bounded pair membership |
| Same file, `IsPointJointCandidatePair` | Every candidate resolves and scans every joint | Resolve joint endpoints once and filter through normalized exclusion keys |
| `Stages/PhysicsForceStage.cpp`, `PrepareMutualGravityForces` | More than 512 bodies select serial all-pairs force accumulation | Bounded parallel contribution batches with canonical serial reduction |
| `tools/validate_perf.bat` and `tools/check_perf_budgets.py` | Scale runs are measurement-only; budgets cover DX12 and the small Physics benchmark | Workload-specific timing checks plus machine-independent work counters |

The broadphase source above lives under `SkullbonezSource/Physics/Stages/`.
The existing spatial-grid dense pair bitset is an accepted implementation:
MASTER-PLAN records that removing it caused a measured CPU regression. This
plan does not repeat that replacement.

## Required Behavior And Ownership

- Preserve normalized, unique, ascending solver-visible candidate pairs,
  including conservative pairs currently admitted by sweep augmentation.
  Visually similar output or merely retaining actual hits is insufficient.
- Sleeping and fixed bodies remain collision targets. Relative target motion,
  collider offsets, angular envelopes, contact skin, and the raw sweep epsilon
  retain their existing meanings. Never remove a conservative fallback until
  coverage and exact output equivalence are proved.
- Preserve floating-point operation order. In particular, keep
  `(velocityA - velocityB) * dt`; independently scaling both velocities before
  subtracting is a different arithmetic expression. Do not introduce fast
  math, approximate reciprocals, FMA, or changed convergence/impulse ordering.
- New scratch belongs to the consuming Physics stage and commits capacity
  during scene load through the registered allocator. Document maximum bytes,
  high-water counters, reset behavior, and clone/restore behavior. No per-body
  field is added to `PhysicsBodyRecord` or hot store arrays without the review
  decision required by `AGENTS.md`.
- Account for scene reload, topology compaction, collider edits, joint changes,
  body release, sleep/wake transitions, and replay/prediction restoration.
  Dense indices are temporary; stable handles must be resolved against the
  current topology before cached keys are used.
- Physics depends only downward. No Runtime dependency, new Replay growth
  privilege, serialization change, or dependency-rule edit is planned.
- Existing goldens remain the acceptance oracle. An unexplained difference is
  a defect. This performance plan targets unchanged output; it must not use a
  golden refresh to hide drift. Any explained scope change requiring a golden
  transition follows the full retained-producer policy in `AGENTS.md` and is
  recorded in this plan before proceeding.

| Exception | Owner | Reason | Deletion condition |
|---|---|---|---|
| None planned | Physics | Existing dependency and allocation rules suffice | Not applicable |

## Phase Ledger

Each checked phase requires its stated evidence. The count is phase-local,
not a count of individual edits or tests. Execute PS0 through PS6 in order;
all seven phases must be resolved before closure.

- [x] **PS0 — Reproduce costs and establish exact reference output.**
  Build the current Profile and Debug producers and record source revision,
  executable hashes, toolchain, machine, worker count, scene/config hashes,
  launch flags, fixed-step duration, warmup, and measured frame window.
  Preserve old Profile artifacts before `validate_perf.bat` deletes them.
  Measure the 200/520/1,000/2,000/sleepy-5,000 workloads with repeated isolated
  runs; report median run-average plus p50/p99, maximum, and run-to-run spread.
  Add representative joint-heavy and mutual-gravity workloads, including
  511/512/513 bodies and a larger supported count. Discover existing fixtures
  before creating new ones. Measure fast movers, target comparisons, geometry
  evaluations, duplicate probes, joint endpoint resolutions, gravity pair
  builds/reduction, and scratch bytes where current counters cannot explain
  time. Establish exact candidate-stream and force-output comparisons against
  the unmodified behavior. Define meaningful workload-specific improvement
  and non-regression thresholds from repeatability before PS1 changes results.
  Acceptance: reproducible baseline, bounded counter output, and an exact
  reference comparison that a deliberately omitted or reordered pair fails.

- [x] **PS1 — Cache sweep geometry and remove linear duplicate search.**
  Prepare exact collider centers and shape radii once for rows consumed by the
  augmentation pass, avoiding work when no mover qualifies. Keep shape radius
  distinct from any different body bounding-radius contract. Replace repeated
  candidate-list scans with a bounded membership structure while retaining
  the grid's accepted dense dedup and final canonical ordering. Seed membership
  from grid output and reject duplicates before consuming candidate capacity;
  append-then-deduplicate must not create a new transient overflow. Choose an
  honest shared owner or stage-owned scratch after inspecting the existing
  bitset rather than exposing mutable grid internals. Test duplicate discovery
  from both movers, grid/sweep overlap, exact capacity, offset colliders, and
  scratch invalidation. Acceptance: identical candidate stream, flat warmed
  allocation counts, and geometry/duplicate work no longer proportional to
  the old nested lookup counts. Record measured benefit before PS2.

- [x] **PS2 — Prune full-world sweep targets spatially.**
  Use or extend the Physics-owned spatial query path to gather a conservative
  target set, then retain the existing sweep and pair-filter predicates.
  Account for both bodies' motion, sleeping/fixed targets, offset shapes,
  angular expansion, large targets, grid boundaries, and overlong paths.
  Preserve an explicit complete-coverage fallback for unsupported geometry
  envelopes or bounded-query exhaustion; never silently truncate candidates.
  Remove the unconditional full-world target scan from ordinary supported
  queries only after exact differential evidence passes. Test moving/moving
  crossings, fast movers hitting sleepers, negative coordinates, sparse and
  crowded cells, and fallback saturation. Acceptance: identical candidate
  streams and reduced target comparisons on spatially separated scale scenes;
  any retained fallback has named triggers, counters, and measured cost.

- [x] **PS3 — Resolve and index joint exclusions once.**
  Build normalized valid endpoint keys once per step, or retain them only with
  an explicit topology/joint-generation invalidation rule. Filter canonical
  candidates by sorted merge or bounded lookup instead of resolving all joint
  handles for every pair. Preserve all current joint collision-exclusion
  semantics and remaining pair order. Test duplicate joints, reversed ends,
  stale handles, destruction, compaction, scene reload, and restored snapshots.
  Acceptance: endpoint resolution scales with joints rather than candidates
  times joints, exact pair output is unchanged, and a joint-heavy benchmark
  meets the PS0 non-regression and benefit criteria.

- [x] **PS4 — Use bounded parallel gravity batches above 512 bodies.**
  Generate canonical pair contributions in bounded batches using the existing
  worker pool, then reduce them in the original nested `(i, j)` order. Preserve
  force expressions, receiver eligibility, fixed/sleeping gravity sources, and
  the order of additions to every body. Keep scratch within an explicit
  scene-load allocation budget rather than allocating the full large-world
  triangular pair table. Use deterministic batch boundaries independent of
  worker count, and retain a measured serial path where dispatch is slower.
  Test 511/512/513 bodies, partial final batches, zero/ignored masses, fixed and
  sleeping participants, disabled gravity, worker counts 0/1/4, and private
  prediction-engine seeding/restoration. Acceptance: byte-identical force and
  multi-tick state output, bounded scratch, and a demonstrated benefit above
  the old threshold without regression below it. If evidence rejects the
  candidate, retain the current path and record the measured decision; do not
  claim an optimization or leave an unmeasured replacement enabled.

- [x] **PS5 — Enforce scale performance regressions.**
  Extend the existing performance scripts and their documentation to check
  the scale, joint-heavy, and gravity cases established in PS0. Use
  machine-matched relative comparisons and justified absolute limits where
  appropriate; use structural work counters for portable algorithm checks.
  Timing setup must fix worker policy, scene duration, warmup, and measurement
  window. Missing artifacts/counters or wrong workload identity fail closed.
  Add negative fixtures proving that an excessive target scan, repeated joint
  resolution, missing measurement, and configured timing regression are
  detected. Do not set limits from a single noisy run or silently loosen the
  existing small-scene budgets. Acceptance: the normal perf entry point checks
  every new workload and the negative fixtures fail for the intended reason.

- [ ] **PS6 — Complete review, cumulative validation, and handoff.**
  Perform one independent terminal rubber-duck review of ownership, pair
  coverage/order, arithmetic, concurrency, memory bounds, cache invalidation,
  and profiler attribution. Repair findings, run the cumulative mapped gates
  below, and compare repeated final timings against PS0 with the same workload
  identities and measurement windows. Report absolute times, percentage
  changes, p99/max, work counts, scratch bytes, allocation growth, and every
  retained fallback or rejected optimization. Reconcile all phase evidence,
  MASTER-PLAN counts, and SessionState. Close only with explicit outcomes for
  all phases; preserve inherited failures as failures and do not refresh their
  goldens. Delete the completed TODO plan under repository convention after
  its closure evidence is retained in the commit and ledger.

## Validation And Evidence

## Execution Checkpoint — 2026-09-12

PS0 evidence: freshly built Debug/Profile at the starting revision, preserved
under `TestOutput/physics-scale-optimization/before/`. Three serial four-worker
runs cover every existing scale workload. Median run-average Physics/sweep ms:
200: 0.1579/0.0178; 520: 0.4002/0.0861; 1,000: 0.9617/0.3722;
2,000: 2.8480/1.5601; sleepy-5,000: 12.4594/11.5494.
Windows runs use the committed scene frame limits and all profiler passes,
excluding frames below 60. The existing parser handles changing CSV headers.
Raw captures, executable hashes, scene hashes, launch arguments, and per-run
mean/p50/p99/max are retained beside each result. Both builds passed.

Additional generated authored fixtures under `before-extra-valid/` cover
511/512/513/1,024 gravity bodies and 320 jointed bodies, three runs each.
The first 1,024-body fixture exceeded the unchanged grid bucket capacity; its
failure remains in `before-extra/`. The revised fixture uses smaller separated
spheres and succeeds without modifying engine policy. Final comparisons must
use the same revised scene hashes. Median Physics ms: gravity 511 1.1223;
512 1.1272; 513 1.1354; 1,024 3.9263; joints 320 2.1987.

Two test-only references preserve the original all-target sweep with linear
duplicate lookup and original scalar gravity accumulation. On pre-change
production they pass 23,495 assertions, including ordered pair comparisons,
offset colliders, mixed sleepers/fixed bodies, duplicate/reversed/invalid joints,
repeated topology/restore invalidation, and raw force bits at 31/511/512/513/1,025
bodies with 0/1/4 workers. Reordered pairs and flipped force bits are rejected.
Logs: `test-reference-before.log`, `test-build-reference.log`.

Performance decision before PS1: require at least 10% median sweep improvement
on both 2,000 and sleepy-5,000 workloads, and no greater than 10% repeatable
Physics regression on another workload. These margins exceed observed ordinary
run variation; near-limit results require additional paired measurement.
Exact pair/force equality is mandatory regardless of timing. PS1 now compiles
with zero warnings and passes the same 23,495 assertions; measurements are in
progress. Its new bounded scratch belongs to Broadphase, never body records.

PS1 measured result (`ps1-valid/results.json`): median Physics ms
200 0.1482; 520 0.3452; 1,000 0.6340; 2,000 1.4730;
sleepy-5,000 2.0899. The two largest workloads improve by 48.3% and 83.2%.
The exact-output test passes, and `ps1-allocation-verified.log` records zero
steady-gameplay violations and zero foreign frees. The profiler's 16 fixed
counter slots were insufficient for work measurements; 32 fixed slots support
the existing columns plus scale diagnostics, without runtime allocation.
The initial diagnostic launch failure is preserved under `ps1/`.

PS2 implementation uses a Broadphase-owned bounded binary swept-bounds tree;
the existing grid has no general swept query covering every target's velocity.
The tree includes every current collider center/radius and target velocity,
uses conservative double bounds with an explicit float-rounding margin, and
retains the full exact scan for non-finite/extreme inputs. Below 512 bodies or
eight movers, a direct scan avoids tree construction overhead. Scratch is
SceneLoad-owned, reconstructed after every current-step geometry publication,
and included in Physics memory accounting. No body-record field is added.

Expanded 96/520/1,025-body reference cases include query work reduction,
extreme-velocity fallback, and an exactly-full 96-body pair list. Together
with raw-force references, 25,126 assertions pass. The larger fixture exposed
a reference setup error: Profile visits only awake-source cells, whereas the
test initially visited all cells. The reference now marks the same source
cells and preserves Debug's separate full-cell policy. The missed reference
pair was fixed/sleeping and had no awake cell source. Evidence is preserved
in `test-ps2-debug.log`; production filtering was unchanged.

PS2 median Physics ms (`ps2/results.json`): 200 0.1503; 520 0.3403;
1,000 0.6193; 2,000 1.3525; sleepy-5,000 0.9512. Tree queries reduce
the sleeping-heavy fixture's exact sweep-target checks from a full-world scan
to zero while retaining the same output; other large fixtures also reduce
target checks substantially. The complete-scan fallback remains for small
workloads/few movers and unsafe numeric bounds. No query truncation is used.

PS3 indexes normalized joint endpoint pairs once per pass. Scene-derived
reservation follows the PhysicsWorld joint capacity. Binary-search pruning
preserves ordinary and Debug diagnostic pair order; refresh-after-wake builds
fresh keys again. Tests prove duplicate/reversed joints, invalid and destroyed
handles, body/collider compaction, exactly-full candidate capacity, and contact
refresh. The combined references pass 25,136 assertions. Three joint-workload
runs give median Physics 1.3107 ms versus 2.1987 ms before (40.4% faster), with
unchanged gravity timings (`ps3-measure/results.json`).

PS4 now extends the existing pair builder through whole-row batches fitting
its unchanged 130,816-record cap. Batches and reductions stay in canonical
(i,j) order regardless of worker count. The direct large-field serial path
remains for disabled workers. Compilation, exact-force/multi-tick checks,
measurement, and final concurrency validation are still required.

PS4 refinement: the first bounded-batch candidate improved 1,024-body Physics
only 2.7%, so it did not satisfy acceptance. Directly reducing each initialized
chunk prefix removed the redundant contribution-copy pass while preserving
every (i,j) addition. Three runs then measured 3.4126 ms versus 3.9263 ms
(13.1% faster); the final identified matrix is still running. The 1,025-body
multi-tick fixture initially exceeded the unchanged grid bucket limit. Smaller
spheres away from cell boundaries retain the large gravity workload within
that limit; no production capacity or baseline changed.

Current exact comparisons pass 97,798 assertions in nine tests, including
511/512/513/520/1,025-body multi-tick worker cases, original scalar force bits,
mixed rotated sphere/box/hull sweep geometry, omitted/reordered negative controls,
and reset/recovery after complete fallback. `test-review-fix.log` is current.

PS5 implementation is integrated into `validate_perf.bat`: ten identified
workloads, four workers, two passes of frames 60..600 (1,082 samples), structural
work/scratch checks, absolute timing ceilings and optional matched repeated
reference comparison. Failed/truncated/duplicated reference runs, wrong body
counts, missing columns, excess scan/joint work and timing regressions have
negative controls. Existing baselines and small-scene budgets are unchanged.
`before-combined.json` derives 30 reference summaries from preserved raw CSVs;
the recorded commands prove worker counts and unchanged engine.cfg matches the
recorded extra-workload hash. Original run artifacts remain untouched.

Terminal independent reviewer `01a0951f-2249-7222-a343-a68ce3e76a16` found four
issues: incomplete reference validation, missing aggregate invariant comments,
an inaccurate borrowed-input lifetime statement, and source-design findings.
All were repaired and independently rechecked without additional material
findings. Chunk generation is now separate from dispatch/reduction, and force
application consumes its own prepared scratch without returning that pointer
through PhysicsWorld. Disabled preparation clears its live extent. The final
source-design check passes all eight changed C++ files / 58 compile contexts
(`source-design-final.log`); current focused tests pass as above.

Static allocation checking identified moved exact-site registrations plus the
new bounded joint append. Metadata now names their existing fixed-list owners,
scene-load reservation and unchanged caps; no gameplay growth is authorized.
Its rerun and terminal runtime guards remain required. Linux portable sanitizer
execution cannot run locally because this Windows host has no WSL installation;
the repository's hosted portable diagnostics lane remains the applicable path.

Starting revision: `16d472bb47bdf27a224327419783639d67ccf325`.
User-owned untracked `SkullbonezData/scenes/asdasd.scene.json` is excluded.
Pre-change artifacts and baseline hashes are preserved under
`TestOutput/physics-scale-optimization/`. Fresh Profile/Debug producers,
repeatable timings, exact differential tests, implementation, and terminal
validation remain required. PS0-PS4 are accepted on preserved measurements, exact differential checks, bounded-allocation evidence and the unchanged core worker matrix; PS5-PS6 remain open for normal perf entry-point execution and cumulative evidence.

The live work-ledger bootstrap rejects a new goal because an unrelated
2026-08-28 governance run remains unfinished (task GOV1, another session).
That historical ledger is preserved; no estimated accounting replaces it.
This accounting issue does not block Physics implementation.

Plan authoring is documentation-only and needs no repository validation.
During implementation, compile affected targets and use focused checks while
iterating. Concentrate heavy suites, independent review, and final fixes in
PS6, subject to the cumulative pre-push requirements in `AGENTS.md`.

The focused test owners are `SkullbonezTests/TestSpatialGrid.cpp`,
`TestSolverBroadphaseStage.cpp`, `TestPhysicsStageState.cpp`, `TestRagdoll.cpp`,
`TestDeterminism.cpp`, and `TestReplayDeterminism.cpp`. Extend the exact owning
cases after discovering their current test names. Keep old behavior as a
test-only oracle, never as a permanent alternate production implementation.
Use `tools/check_broadphase_pair_stream_oracle.py` for compatible recorded pair
streams; inspect its producer/format before relying on it.

Terminal commands and obligations:

```text
tools\validate_tests.bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
tools\validate_perf.bat
tools\validate_dependency_graph.bat
python tools\check_allocation_policy.py --repo .
python tools\check_source_design.py --repo . --files <actual changed C++ paths>
tools\agent_validate.bat --plan-completion
```

Run the full plan-completion command exactly once after terminal review. Reuse
its successful constituent results where they cover the same final payload;
do not run duplicate umbrellas merely to repeat evidence. The byte-exact
Physics matrix includes 0/repeat/1/4 workers. Threading changes also need the
portable sanitizer lanes required by the repository, and replay-facing changes
add `tools\validate_replay_visual_fidelity.bat` to the mapped gates. Profiler
changes require the platform-profiler marker launch in `AGENTS.md`.

Use optimized Profile batch workloads for timings. Use Skarness through
`tools/skarness.py` for supported Automation gameplay interactions, check
capabilities, retain subscribed state under `TestOutput/skarness/`, bind
assertions to identity and outcome, and stop owned sessions orderly. Any
supplied interaction manifest remains unchanged and follows the recorded-repro
contract. Use SkullScope bounded queries and report query bytes if diagnostic
traces are needed; do not ingest complete Physics logs.

Store local run evidence under `TestOutput/physics-scale-optimization/<run>/`.
Record exact commands, exit codes, hashes, durations, and bounded summaries in
phase evidence and commit notes. Do not create a committed report tree or
change tracked baselines merely to preserve ordinary measurements. A governed
golden transition, if scope changes require one, instead uses the append-only
artifact bundle mandated by `AGENTS.md`.

## Commit And Progress Contract

Plan implementation subjects use the post-commit phase count:

```text
PHYSICS_SCALE, TASK <DONE>/7 — <ACTION SUMMARY>
```

Keep the subject under 72 characters and provide substantive `Why:`,
`Ownership:`, `What:`, `Validation:`, `Baselines/Artifacts:`, and `Review:`
sections in that order. Use `git commit -F <message-file>` and the repository
message verifier and hooks. A plan-authoring commit does not claim phase
progress. Current implementation progress is **6/7**, active by owner direction.

Final timing comparison passes all 30 identified runs (`final/comparison.json`):
2000 2.8480 -> 1.4161 ms (50.3% faster); sleepy-5000 12.4594 -> 0.9423 ms
(92.4%); gravity-1024 3.9263 -> 3.2511 ms (17.2%); joints-320 2.1987 ->
1.3315 ms (39.4%). `final/detailed-comparison.json` retains p50/p99/max,
run-average spread and all work/scratch counters. Every workload satisfies the
10% non-regression criterion. The measured producer and source diff are retained
under `final/producer/`. The core Debug 0/repeat/1/4 matrix matches its unchanged
44,401-line baseline exactly: SHA-256
`50bca7c0f2c420832c4fd99b1812f4db48d88cfadb4d475622a3d3bd3465a1c1`.
Copies are preserved under `core-worker-matrix/`. The exactly-once terminal
`agent_validate --plan-completion` command ran once and stopped on stale scratch-memory test expectations;
those expectations are repaired and all 1,047 active Profile tests now pass.
Do not restart the umbrella. Static allocation, dependency and source-design checks pass.

PS4 closure: the 1,025-body prediction seed/reseed regression passes 165,035
assertions over two seeds and eight total ticks, with exact live/prediction
state and stable reserved memory. This complements raw force-bit comparisons
at 511/512/513/1,025 bodies and the multi-tick worker matrix. The additional
test file passes both compiler contexts. Both 5,000-body sweep and 1,024-body
gravity allocation guards pass with zero gameplay violations and foreign frees.
Static allocation metadata repairs preserve the same scene-load capacities.

A 6/7 checkpoint enables the required hosted Linux diagnostics while the
exactly-once Windows umbrella continues. PS5 and PS6 are not closed by that
checkpoint. Deep seeded-solver output matches the preserved pre-change Debug
producer exactly despite the inherited golden mismatch. The at-rest gate
ends at differing frame counts; all shared rows match, and an additional
600-frame bounded copy produces 18,001 identical rows before/after. The
canonical replay gate retains the existing topology 91 -> 70 failure recorded
at the starting revision. The bounded diagnostic query packet also matches
the pre-change Debug producer exactly (99,365 bytes). No baseline transition
is authorized or performed.

The normal Profile unit gate passes 1,047 active cases and 3,735,350
assertions, with one existing skipped case. Exact memory tests now include
four growing scratch owners, the already-capped membership owner, and all
five owners in registration/capacity/byte accounting. A bounded independent
review confirmed 524 additional bytes in the three-body/two-joint witness
and no weakening of unique-owner or monotonic-growth checks. DX12 passes.
The staged Physics gate independently passes the unchanged worker matrix
with staged fingerprint 3b6aadcf4bcd.

PS5 closure: the normal validate_perf entry point executed all ten scale
workloads and the negative controls successfully. Both legacy absolute budgets,
allocation guards, selected-path structural proof and dense causal-inspection
cost checks pass. The overall gate remains failed (exit 7) on eight relative
UI/frame/memory findings for each legacy workload. The preserved pre-change
Profile producer reproduces all eight corresponding failures in BOTH workloads
with the same commands and unchanged baselines. These are inherited failures,
not acceptance passes or baseline-update authority. Before/current legacy
raw CSVs and comparisons are retained in legacy-perf-before/ and perf-final.log.
Final producer repeated measurements are being refreshed after the bounded
portable integer-sort repair; no floating-point expression changed.

# Physics Scale Optimization

Date: 2026-09-06
Status: Owner-parked — 0/7 phases complete; not selectable for implementation
Plan ID: `PHYSICS_SCALE`
Owner: Physics broadphase and force stages; performance tooling owns measurement and regression checks
Impact areas: Physics, collision candidate generation, joint filtering, mutual gravity, tests, and performance tooling

## Goal And Scope

Reduce fixed-step CPU cost as body counts grow, starting with the fast-body
broadphase sweep. Preserve the exact candidate stream, Physics arithmetic,
sleep/wake behavior, replay results, and zero-allocation runtime contract.

The owner requested this plan after the 2026-09-06 optimization review, then
explicitly placed it in `WNF/` and directed that MASTER-PLAN remain unchanged.
This plan is parked, excluded from the active queue, and must not be selected
until the owner reactivates it. Future authorized execution follows
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

- [ ] **PS0 — Reproduce costs and establish exact reference output.**
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

- [ ] **PS1 — Cache sweep geometry and remove linear duplicate search.**
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

- [ ] **PS2 — Prune full-world sweep targets spatially.**
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

- [ ] **PS3 — Resolve and index joint exclusions once.**
  Build normalized valid endpoint keys once per step, or retain them only with
  an explicit topology/joint-generation invalidation rule. Filter canonical
  candidates by sorted merge or bounded lookup instead of resolving all joint
  handles for every pair. Preserve all current joint collision-exclusion
  semantics and remaining pair order. Test duplicate joints, reversed ends,
  stale handles, destruction, compaction, scene reload, and restored snapshots.
  Acceptance: endpoint resolution scales with joints rather than candidates
  times joints, exact pair output is unchanged, and a joint-heavy benchmark
  meets the PS0 non-regression and benefit criteria.

- [ ] **PS4 — Use bounded parallel gravity batches above 512 bodies.**
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

- [ ] **PS5 — Enforce scale performance regressions.**
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
progress. Current implementation progress is **0/7**, owner-parked and excluded
from the active ledger by explicit owner direction.

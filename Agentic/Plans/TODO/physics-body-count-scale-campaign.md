# Physics Body-Count Scale Campaign — Persistent Broadphase, Free Sleepers, Bandwidth Diet

Date: 2026-07-18
Status: Active — 2/8 tasks (P0-P1 complete; P2 persistent incremental grid next)
Branch: `nightrunner-18th-july`
Impact area: `SkullbonezSource/Physics/SpatialGrid.*`,
`Physics/Stages/PhysicsBroadphaseStage.*`, `Physics/Stages/PhysicsForceStage.*`,
`Physics/Stages/PhysicsSleepController.*`, `Physics/PhysicsBodyStore.*`,
`Physics/PhysicsWorld.*`, scale scenes, physics/perf baselines, replay
visual-fidelity golden (P1 and evidence-triggered P6 transitions only;
per-instance replay-golden approval still applies)
Owner: physics subsystem

## Problem And Evidence (measured 2026-07-18 at `nightrunner-17th-july`, plan authored at 06a17ff31)

The engine simulates ~1,000 awake bodies at ~1 ms/step (S3 closure: 0.9795 ms
scalar SoA; B4 closure: 1.0546 ms at 1,000 / 2.0517 ms at 2,000 post-grid-fix).
The retained scalar solver core is 0.0092 ms — **0.86% of the step** — so the
frame is broadphase/integration/bookkeeping-bound, and the S6 SIMD experiment
confirmed the ceiling is memory bandwidth, not arithmetic. High-body-count
engines (Bullet `btDbvtBroadphase`, Jolt, PhysX, Box2D v3) reach 10k+ bodies
with three structural properties this engine lacks:

1. **The broadphase is persistent.** Bodies that did not change cells cost
   nothing. Here, `PhysicsBroadphaseStage` reinserts **every** body into
   `SpatialGrid` every step (`PhysicsBroadphaseStage.cpp:357-371`); the
   generation-stamp design (`SpatialGrid.h:111`) exists precisely to make
   per-frame full rebuild cheap, which is optimizing the wrong loop.
2. **Sleeping bodies are free.** Here, sleepers are fully inserted, generate
   candidate pairs, and are only then filtered by `PruneSleepPairs`
   (`PhysicsBroadphaseStage.cpp:460`) — a sleeping body pays insertion, pair
   generation, pruning-predicate, and hot-array traversal cost every step.
   A "5,000-body scene" where 80% sleeps should cost ~1,000 bodies; today it
   costs ~5,000.
3. **Hot loops touch minimal bytes.** S1/S2 built 20 aligned SoA arrays
   (`PhysicsBodyStore.h:426-460`), but per-step passes over full arrays
   (bounds prep, force, integration, sleep scan) have never been audited for
   awake-set iteration or pass fusion; S3 already recovered 2% from exactly
   this category (span-copy removal) without exhausting it.

Determinism context: today's candidate-pair **order** is a pure function of
the per-frame rebuild (body-index scan → first-touch bucket order → linked
lists), so it is deterministic but *history-shaped*. A persistent broadphase
cannot reproduce that order without re-deriving first-touch order each frame
— which costs the very pass being eliminated. Therefore this campaign
performs one explicit, owner-approved **canonical pair-order transition**
(P1) and is byte-exact forever after against the transitioned baselines.
Run-to-run and thread-count determinism are never weakened at any point.

## Reference Model (what the high-body-count engines do)

- Bullet: incremental dynamic AABB tree (`btDbvtBroadphase`), persistent
  overlapping-pair cache; sleeping islands leave the active set.
- Jolt: quad-tree with deferred incremental maintenance, compact hot body
  state, deterministic across thread counts via fixed batch order + ordered
  reduction; bodies sorted for locality.
- Box2D v3: cross-platform deterministic **multithreaded** solver via
  constraint graph coloring (colors solve in parallel, fixed color sequence)
  and soft-step/TGS substepping for convergence per cost.
- PhysX: constraint batching/coloring for SIMD+MT; GPU rigid bodies are
  nondeterministic and are explicitly out of scope here.

This campaign adopts: persistence (P2), free sleepers (P3), bandwidth diet
(P4), and — evidence-gated after the mid-campaign checkpoint — graph-colored
solver parallelism (P6). It does not adopt GPU physics or solver-model
(TGS) changes.

## Goal

At unchanged tick rate and unchanged collision/solver behavior envelopes:

- A body that did not change its cell range costs zero broadphase work.
- A sleeping body costs zero per-step work in broadphase, force,
  integration, and bounds passes (it remains discoverable by awake movers
  and wakes exactly as today).
- Per-step bytes-touched-per-body is measured, budgeted, and reduced.
- Ratified target, measured at P7: **≥ 4,000 awake bodies** inside the
  1,000-body-era ~1 ms step budget on the reference Threadripper 3970X, and
  a sleeping-heavy scene (P0 authors it) where step cost tracks the awake
  count, not the total count. Targets are evidence goals, not mechanical
  ratchets; P7 records actuals against them.

## Non-Goals

- No solver-model change (sequential impulse stays; no TGS/substepping).
- No SIMD kernels — S7 ruling stands; a SIMD re-attempt needs its own plan
  and fresh owner approval after P4 makes loops compute-bound.
- No GPU physics in any form (nondeterministic; violates the byte-exact
  contract).
- No capacity-policy change: fixed preallocation, Lane F on exhaustion,
  zero steady-state allocation throughout (`SpatialGrid` stays
  fixed-capacity; the persistent variant keeps `TABLE_SIZE` 8,192 and the
  existing entry-pool bound unless P0 census proves a new bound, ratified
  by the owner with the diagnostics required by the allocation policy).
- No change to sleep *policy* (thresholds, timers, wake rules) — only to
  what sleeping costs.
- No cross-platform/cross-toolchain determinism expansion; the certified
  envelope stays per-binary, pinned toolchain (`/fp:precise`,
  `fp_contract off`).
- Outside P1 and an evidence-triggered P6 (each with the protocol's
  same-state evidence), zero baseline, golden, screenshot, or coverage-floor
  refresh.

## Determinism Transition Protocol (binding for P1 and P6)

A transition task may change float-visible results **only** by reordering
identical work, never by changing the work:

1. Build a Debug-only **same-input-state** oracle before moving an artifact.
   The legacy and replacement paths must consume the same pre-stage state;
   comparing two simulations after each has evolved under a different work
   order is not an equivalence test for an order-changing transition.
2. P1 runs the oracle in both driver directions over all four
   `physics_scale_*` scenes, the P0 sleeping-heavy scene, and the regression
   varied scene: legacy emission drives the simulation while canonical
   emission shadows it, then canonical emission drives while legacy emission
   shadows it. At every tick compare the normalized raw pair set immediately
   after `SpatialGrid` emission and the normalized final solver-visible pair
   set after augmentation/pruning. Both sets must match on the shared state.
3. P6 compares, from the same pre-solve state, canonical constraint identity
   and membership, exactly-once solve coverage, graph-color body disjointness,
   and fixed color order. Do **not** require numerical per-iteration impulses
   to equal the serial solver: projected Gauss-Seidel work-order changes are
   expected to change intermediate velocities and impulse values. The new
   colored solver must instead be byte-identical to itself at 0/1/4/8 workers.
4. Only after the relevant oracle passes may the task regenerate, from the
   final Debug binary per the AGENTS.md
   baseline rules: the deterministic physics CSVs (`validate_physics` set;
   `validate_physics_deep` set if its scenes' outcomes moved) and — with
   explicit per-instance owner approval under MASTER rule 11 — the 200-box
   replay visual-fidelity golden, using exactly one engine process and one
   prediction generation.
5. Record the pre-refresh deterministic diffs as transition evidence, then
   rerun the matching gates against the regenerated artifacts in the same
   task. An expected pre-refresh mismatch is not a passing gate. A transition
   without a passing same-state oracle is reverted, not fixed forward.
6. Thread-count invariance: every task in this campaign, transition or
   not, must produce byte-identical physics CSVs at worker counts 0, 1,
   and 4 (precedent: deterministic-parallel-mutual-gravity acceptance).

Owner clarification, 2026-07-18: an independently evolved legacy run and an
independently evolved canonical run are expected to separate after a solver
work-order change. Their deterministic CSVs and later broadphase sets therefore
need not match before the authorized transition. The binding proof is identical
work membership on identical input state plus deterministic results within the
new implementation's certified worker-count envelope.

## Expected Failures And Artifact Update Policy

"Expected" means a failure is legitimate **before** an authorized transition
refresh; it never means the task may close with a failing gate. Compiler/test
failures, crashes, warnings, allocation-guard failures, capacity failures
without the required Lane F diagnostics, same-state work-membership mismatch,
and worker-count nondeterminism are regressions in every task.

| Task | Expected pre-refresh deterministic failures | Must remain unchanged | Committed artifact updates allowed |
|---|---|---|---|
| P0 | None; instrumentation must not move physics state. | Physics CSVs, replay/visual goldens, screenshots, coverage floors. | Scene, profiler-marker plumbing, and measurement report only, as already completed. |
| P1 | Physics CSV comparison and replay hashes/golden may differ after canonical pair order begins driving the solver. Later independently evolved pair sets may also differ; this is not emitter-set loss when the same-state raw and final sets match. | Same-state raw/final pair membership in both driver directions; 0/1/4 repeatability; authored scenes/config/schema; coverage floors; general render screenshots. | Physics CSV baselines whose outcomes moved, from the final Debug binary. Deep CSVs only if their scenes moved. The 200-box replay golden only with explicit per-instance owner approval and one process/generation. No other baseline class. |
| P2 | None. A deterministic-output diff exposes stale membership, removal/back-link, overlay, rebuild, or history-dependence defects. | P1 physics CSVs and replay golden byte-exact; canonical pair membership/order; 0/1/4 identity. | Measurement report and profiler evidence only; no physics, replay, screenshot, perf-baseline, or coverage-floor refresh. |
| P3 | None. A deterministic-output diff exposes awake-list, wake, sleep-pair suppression, diagnostics, or restore defects. | P1 physics CSVs and replay golden byte-exact; awake-to-sleep collision/wake semantics; 0/1/4 identity. | Measurement report and an explicitly approved diagnostics-format/retirement record only; no behavioral baseline refresh. |
| P4 | None. A deterministic-output diff means operation order or persisted state changed and the candidate optimization is rejected. | P1 physics CSVs and replay golden byte-exact; hot-path allocation policy; 0/1/4 identity. | Measurement report/profiler evidence only; committed performance baselines are not authorized by this plan. |
| P5 | None; documentation/evidence-gate task. | All runtime artifacts. | Campaign report, plan, and the automatic proceed/defer decision only; the eight-task ledger denominator stays fixed. |
| P6, if evidence-triggered | Physics CSV comparison and replay hashes/golden may differ because fixed graph-color order replaces serial constraint order. Numerical impulse values may differ. | Same-state constraint membership, exactly-once coverage, conflict-free colors, fixed color order, and 0/1/4/8 identity within the new solver. Authored scenes/config/schema, screenshots, and coverage floors remain unchanged. | Physics/deep CSV baselines whose outcomes moved, from the final Debug binary; 200-box replay golden only with explicit per-instance approval and one process/generation. If that approval is unavailable, reverse only the P6 changes and preserve P5 behavior rather than blocking. No other baseline class. |
| P7 | None. Closure validates the artifacts established by P1 and, if run, P6. | Everything behavioral. A final failure reopens its owning task. | Reports, plan/MASTER/SessionState closure bookkeeping only; never refresh a baseline at P7 to make a gate pass. |

Ignored captures, profiler traces, temporary comparison dumps, and the campaign
measurement report are evidence, not behavioral baselines. They may be updated
when a task records new measurements. Authored scene/config/schema changes,
render screenshot baselines, coverage floors, and committed performance
baselines require a separate task and owner ruling; this campaign does not
authorize them. Provenance-hash-only reconciliation is also inapplicable unless
the exact AGENTS.md standing-rule conditions independently arise and are proved.

### Task-Specific Failure Probes

| Task | Failure modes the implementation must actively probe |
|---|---|
| P1 | Raw emitter omission/duplication; augmentation/prune changing final membership; pair normalization mistakes; canonical-list capacity diagnostics; hidden allocation; new-path 0/1/4 nondeterminism. |
| P2 | Cell-boundary enter/leave errors; multi-cell removal and swap-back-link corruption; free-list reuse; body removal/reinsertion; `SetCellSize`/scene-load full rebuild; transient swept/CCD overlay leakage; pool-exhaustion Lane F diagnostics; replay/restore history dependence. |
| P3 | Duplicate/out-of-order awake indices; sleep/wake transition loss; awake mover versus sleeper contact and wake behavior; sleep-sleep suppression; fixed/static-body handling; `SleepPrunedPair` diagnostic ordering/retirement; replay restore rebuilding the same awake list. |
| P4 | Cross-body or fused-pass arithmetic reordering; cold-store split omissions in replay/capture/restore; full-array diagnostics scans left on the hot path; hot-path allocation; inclusive profiler accounting overlap or gaps. |
| P6 | Two constraints sharing a body in one color; skipped/double-solved constraints; warm-start/cache order drift beyond the authorized transition; fixed-buffer overflow diagnostics; atomics/races; repeat-run or 0/1/4/8 worker drift; insufficient one-big-island speedup. |
| P7 | Baseline generated from a non-final binary; a required mapped gate omitted; transition evidence missing; comment-audit or independent-review finding left open. |

Each implementation task must cover its applicable rows with a focused unit,
standalone CPU, or deterministic scene/regression test in the same commit. If a
listed failure mode cannot be tested practically, the task notes must identify
the exact inspection or diagnostic evidence used instead and why automation was
not practical. These focused tests supplement, rather than replace, the mapped
task gates below.

## Measurement Ledger (incremental profiling obligations)

Every implementation task (P2-P4, P6) records this matrix in its commit
body and appends it to the campaign report
(`Agentic/Reports/2026-07-18/body-count-scale-measurements.md`, created at
P0). Medians over the standard perf capture protocol on the reference
machine, Profile build, markers as named:

| Marker | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` (inclusive step) | 0.1106 | 0.8486 | 1.0688 | 1.7852 | 2.1479 |
| `Frame/Physics/Broadphase` (inclusive) | 0.0245 | 0.1986 | 0.3377 | 0.6487 | 0.6963 |
| `Frame/Physics/Broadphase/GridInsert` → `GridMaintain` after P2 | 0.0180 | 0.1764 | 0.2538 | 0.4628 | 0.4456 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0017 | 0.0074 | 0.0222 | 0.0821 | 0.1976 |
| `Frame/Physics/Broadphase/PruneSleepPairs` (deleted at P3; record 0) | 0.0001 | 0.0002 | 0.0003 | 0.0004 | 0.0001 |
| Force stage inclusive | 0.0262 | 0.1812 | 0.1827 | 0.2051 | 0.2108 |
| Integration inclusive | 0.0224 | 0.1995 | 0.2079 | 0.2455 | 0.2595 |
| Narrowphase inclusive | 0.0004 | 0.0030 | 0.0070 | 0.0221 | 0.0011 |
| Solver inclusive (`PersistentContacts`) | 0.0071 | 0.0305 | 0.0569 | 0.1353 | 0.0027 |
| New: awake-body count / total count | 200/200 | 520/520 | 1,000/1,000 | 2,000/2,000 | 1,000/5,000 |
| New: bodies reinserted this step (P2+) | 0 | 0 | 0 | 0 | 0 |
| New: estimated hot bytes/body/step (P0 method) | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

Checkbox contract: a task's measurement checkbox closes only when the full
matrix row-set for that task is committed with the capture commands quoted.
Regressions against the immediately preceding task's matrix must be
explained or the task is not done.

## Tasks

- [x] P0 — Instrumentation, sleeping-heavy scene, and measurement baseline.
  - [x] Author `SkullbonezData/scenes/physics_scale_sleepy_5000.scene.json`:
    fixed-seed, ~5,000 bodies arranged so ≥80% reach engine sleep within a
    bounded settle window and ~1,000 stay perturbed/awake (use the existing
    `physics_scale_2000` authoring pattern; keep fixtures away from
    knife-edge selection boundaries per the danger-zone rule). Capacity
    check: 5,000 ≤ `DEFAULT_GAME_MODEL_CAPACITY` (4,000) fails — this scene
    must set the explicit per-scene capacity override to ≤ `MAX_GAME_MODELS`
    (8,192) through the existing authored capacity path; record the chosen
    value and its memory footprint.
  - [x] Add the new profiler markers/counters: awake-vs-total body count,
    per-step reinsertion count (0 until P2), and the hot-bytes/body/step
    estimate (static accounting over the arrays each pass touches ×
    per-pass iteration counts — a computed diagnostic, not a hardware
    counter). Marker additions run
    `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` per the
    AGENTS.md profiling rule.
  - [x] Capture the full Measurement Ledger matrix at the current tip —
    this is the campaign's fixed "before" column set.
  - [x] Owner ratifies: branch, the sleepy-scene shape, the ≥4,000-awake
    target, the P1 transition authorization (see protocol), and whether P6
    is pre-authorized at P5 or requires a fresh decision.
  - Gates: `validate_full` (new scene file per the scene mapping) +
    `validate_perf` (marker plumbing touches hot paths); byte-exact physics
    unchanged (instrumentation must not alter arithmetic).
  - Evidence: `../../Reports/2026-07-18/body-count-scale-measurements.md`.
    The owner accepted capacity 6,000 with 4,000 seeded sleepers and 1,000
    awake movers, ratified the ≥4,000-awake target, authorized P1 only through
    same-state work equivalence first, and deferred P6 authorization to P5.
    Full, perf, platform-marker smoke, 0/1/4-worker determinism, comment audit,
    and independent review passed with zero artifact or coverage-floor refresh.
    On 2026-07-18 the owner replaced that future manual decision with the
    automatic P5 evidence gate below so unattended execution cannot block.

- [x] P1 — Canonical pair-order transition (the first behavior-visible flip).
  - Implementation: `GetCandidatePairs` emission becomes history-free —
    bucket emitted pairs by min-index into per-body fixed lists during cell
    walk (O(n+k), allocation-free), then emit in ascending (minIdx, maxIdx)
    order. Delete reliance on bucket-creation order. Keep the triangular
    `pairSeen` dedup bitset.
  - Verification item recorded in-source: confirm `SleepPrunedPair`
    pipeline-trace records (`PhysicsBroadphaseStage.cpp:238-276`) are
    diagnostics-only and absent from the deterministic CSV; if any baseline
    artifact embeds them, preserve their emission semantics until P3
    handles them explicitly.
  - [x] Same-state dual-driver oracle passes: identical normalized raw and
    final per-tick pair sets on all six scenes, with legacy driving/canonical
    shadowing and canonical driving/legacy shadowing.
  - [x] Baselines transitioned per the protocol: physics CSVs whose outcomes
    moved are regenerated from the final Debug binary; any 200-box golden
    regeneration has explicit per-instance owner approval and uses one process;
    `validate_physics`, `validate_physics_deep` (if its scenes moved), and
    `validate_replay_visual_fidelity.bat` all pass against the transitioned
    artifacts.
  - [x] Thread-count invariance at 0/1/4 workers.
  - Expected cost: neutral-to-noise step time (sorting is O(n+k) with tiny
    constants); record the matrix anyway.
  - Historical blocker, resolved by the 2026-07-18 owner clarification: the
    allocation-free canonical implementation
    and Debug sorted-set probe were built and run for 360 fixed ticks over all
    six required scenes. Five scenes matched old/new exactly. In
    `physics_bench_varied`, canonical solver history removed normalized pair
    `(18,20)` from the final candidate set at ticks 152 and 332 (legacy count
    10, canonical count 9), violating the former independently-evolved-run
    equivalence condition. The transition and probe were reverted completely
    and no artifact moved. The corrected protocol above replaces that invalid
    condition; P1 may resume with the same-state dual-driver oracle.
  - Follow-up evidence: one exact Debug executable, selected by a diagnostic
    legacy/canonical toggle, first diverged in deterministic physics state at
    regression row/frame 102 (body 15 velocity by 0.0001); its first final-set
    divergence was 50 fixed ticks later at tick 152. Independent review confirms
    this causal ordering is expected from order-dependent projected
    Gauss-Seidel solving, not emitter-set loss. This causal evidence is why
    independently evolved pair-set equality is now expected to fail while
    same-state emitter and solver-visible membership remains mandatory.
  - 2026-07-18 checkpoint: the final same-state dual-driver oracle passed raw
    and final membership for all six scenes in both driver directions over 360
    ticks. The complete 0/1/4-worker matrix is byte-identical, the focused
    SpatialGrid tests pass, and final-source `validate_perf` and
    `validate_physics` pass. The canonical transition moved only the authorized
    varied and known-issue physics baselines so far.
  - 2026-07-19 closure: the binding MASTER directive authorized the complete
    bounded-divergence assessment without another owner response. The existing
    one-process/one-generation replay report retained every old topology ID,
    added only body 11, removed none, and changed no retained parent, depth, or
    contact-derived classification. The coupled visual packet stayed on the
    same target/camera/reveal/provenance inputs with 200 authored, moved, and
    settled bricks; its changed trajectory counts and hashes are direct
    consequences of the canonical solve order. No second replay process was
    launched for reconciliation.
  - The final-Debug `physics_query_varied.json` packet retained all 21 query
    shapes, contained no non-finite value, remained self-identical, moved peak
    energy only +0.14%, reduced peak penetration by 10.1%, and reported no
    sustained/growing penetration event. `supported_rows: 617 -> 621` and the
    derived event identities/floats are accepted transition evidence.
  - Exact offline replay equality and every negative/determinism/launcher
    control pass against the transitioned 2,401-tick golden package. Final
    `validate_physics` (55.917 s), `validate_physics_deep` (136.931 s), and
    `validate_perf` (107.318 s) pass with zero warnings/errors, exact physics
    and SkullScope artifacts, no performance regression, and zero steady-
    gameplay allocation violations. Full artifact and SkullScope accounting is
    in `Agentic/Reports/2026-07-18/body-count-scale-measurements.md`.

- [ ] P2 — Persistent incremental grid.
  - Implementation: retire the per-frame generation bump as the rebuild
    mechanism. Buckets and entries persist across steps. Per body, cache
    the last-inserted integer cell range (ix/iy/iz min/max derived from
    position+radius); on step, recompute the range and touch the grid
    **only when the integer range differs** (exact integer compare — a
    pure function of current state, so persistence introduces no history
    dependence into *membership*). Removal support: per-cell swap-remove
    with entry back-links or per-body entry-slot records; entry pool
    becomes a free list with the existing Lane F exhaustion diagnostics.
    Swept/CCD insertions (`InsertSwept`) stay per-frame transient in a
    separate stamped overlay region — velocity-dependent, so never
    persisted. `SetCellSize`/scene-load paths do a full clear+rebuild
    (cold). Rename the `GridInsert` marker to `GridMaintain`; keep the
    inclusive `Broadphase` owner-marker accounting rule from B0.
  - Determinism: pair *set* is a pure function of current cell ranges;
    pair *order* is canonical from P1 — so results are byte-exact against
    the P1-transitioned baselines. This claim is the task's central
    acceptance: zero CSV diff, zero golden diff.
  - [ ] Byte-exact vs P1 baselines on all six scenes; 0/1/4 workers.
  - [ ] Measurement matrix recorded. Expected shape: `GridMaintain` drops
    toward zero for settled scenes; `scale_2000` step falls materially
    below the 2.05 ms B4 tip; reinsertion counter validates (≈ moved
    bodies, not total).
  - Gates: `validate_physics` + `validate_perf` (SpatialGrid mapping row),
    `validate_full` at task end (broad hot-path scope).

- [ ] P3 — Zero-cost sleepers.
  - Implementation: the sleep controller maintains a dense **awake index
    list** in ascending body-index order (updated at sleep/wake
    transitions — cold events — never rebuilt per step). Force,
    integration, bounds-prep, and broadphase maintenance iterate the awake
    list instead of 0..count. Sleeping bodies keep their persistent grid
    entries (they cannot move, so P2 already makes them maintenance-free)
    and remain discoverable by awake movers — awake↔sleeping pairs still
    generate and still wake exactly as today. Sleep↔sleep pairs are no
    longer generated at all: pair emission walks cells reachable from
    awake bodies only, which produces the identical post-prune pair set
    the solver sees today; delete `PruneSleepPairs` and its predicate.
    Preserve `SleepPrunedPair` diagnostics semantics per the P1
    verification item (emit from the emission-skip site in diagnostics
    builds, or record the owner-approved diagnostic retirement).
  - Determinism: the awake list is a deterministic function of the
    deterministic sleep state; ascending order preserves canonical
    iteration. Solver-visible pair set and all arithmetic are unchanged →
    byte-exact vs P1 baselines.
  - [ ] Byte-exact on all six scenes; 0/1/4 workers. The sleepy_5000 scene
    is the key witness: identical CSV, step cost now tracking awake count.
  - [ ] Measurement matrix recorded. Expected shape: sleepy_5000 step
    approaches scale_1000 step; `PruneSleepPairs` row records 0/deleted.
  - Gates: `validate_physics` + `validate_physics_deep` (sleep-adjacent
    behavior per the mapping's spirit) + `validate_perf`.

- [ ] P4 — Hot-state compaction and pass fusion (bandwidth diet).
  - Implementation, driven by the P0 bytes/body/step accounting, candidate
    moves in evidence order: fuse force-accumulate → integrate passes
    where they touch the same spans back-to-back; move any cold/rarely
    read fields discovered inside per-step loops out of the hot 20-array
    set (store split, not record growth); hoist per-body flag/branch
    lookups into the awake-list walk; eliminate remaining full-array
    passes that P3 left (bounds prep, diagnostics scans) by awake-list
    iteration. Every individual fusion must preserve exact operation
    order per body — fusing across bodies (regrouping accumulation) is
    banned; that is the S5 exact-sum lesson.
  - Determinism: same operations, same per-body order → byte-exact vs P1
    baselines. Any candidate move that cannot preserve exact order is
    rejected in the task notes, not negotiated.
  - [ ] Byte-exact on all six scenes; 0/1/4 workers.
  - [ ] Measurement matrix recorded, including the bytes/body/step
    estimate before/after per accepted move; each accepted fusion names
    its measured delta in the commit body.
  - Gates: `validate_physics` + `validate_perf`; `validate_full` at task
    end.

- [ ] P5 — Mid-campaign checkpoint and automatic Tier-2 evidence gate.
  - [ ] Consolidated matrix across P0-P4 with a written attribution
    narrative (which milliseconds moved, where, and why), committed to the
    campaign report.
  - [ ] Record two repeated Profile captures at the P4 tip for
    `physics_scale_2000` and the deterministic one-big-island 200-brick-wall
    witness. P6 is automatically authorized when `PersistentContacts` is at
    least 15% of `Frame/Physics` median in either witness; otherwise it is
    deferred. This numeric result is the owner's standing P6 decision; do not
    pause for another response or substitute a subjective bottleneck ruling.
  - [ ] If the trigger is false, close the P6 slot as **deferred by evidence**,
    record the measurements and future retry condition in the campaign report,
    make no runtime/artifact change, and continue directly to P7. The plan
    remains P0-P7 at eight tasks; a deferred conditional slot counts as a
    completed decision outcome and does not change the MASTER denominator.
  - Gate: none beyond the two targeted Profile captures (documentation +
    automatic evidence decision).

- [ ] P6 — (Conditional, pre-authorized by the P5 evidence gate)
  deterministic graph-colored
  solver parallelism.
  - Implementation: greedy-color the contact/joint constraint graph in
    fixed canonical constraint order (the P1 pair order makes this stable
    and history-free); constraints within a color share no bodies and
    solve via the existing `WorkerPool::ParallelForNoAlloc` fixed-chunk
    machinery; colors execute in fixed ascending sequence with a barrier
    between colors; impulses write only their own constraint slots plus
    exclusively-owned body accumulators (no atomics, no cross-thread
    accumulation). Coloring buffers are fixed-capacity with Lane F
    exhaustion. Warm-start order follows the same canonical order.
  - Determinism: within a color, constraint solves are independent by
    construction, so scheduling cannot reorder arithmetic on any body;
    color sequence is fixed → results are a pure function of state and
    identical across worker counts. This changes solve order vs the
    serial baseline → second transition under the full Determinism
    Transition Protocol (same-state constraint-membership/color-validity
    probe, physics CSV regeneration, owner-approved golden regeneration, one
    process).
  - [ ] Same-state oracle + transition artifacts + gates per protocol.
  - [ ] Thread-count invariance 0/1/4/8 workers, byte-identical.
  - [ ] Measurement matrix recorded, including a one-big-island stress
    witness (the 200-brick wall class of scene) at 1/4/8 workers.
  - Autonomous fallback: if the same-state oracle, allocation/capacity rules,
    0/1/4/8 determinism, or mapped gates cannot pass—or if replay-golden drift
    needs per-instance approval that is unavailable—remove/revert only the P6
    implementation and artifact changes, preserve the passing P5 tip, record
    the evidence, close P6 as deferred, and continue to P7. Do not use
    `git stash`, weaken a gate, or refresh an unauthorized artifact. If a
    passing P6 does not reduce the 4-worker `PersistentContacts` median by at
    least 10% versus the P5 serial median on the triggering witness, or regresses
    `Frame/Physics` by more than 2% on either P5 witness, also reverse it and
    defer rather than retain complexity without measured benefit.
  - Gates: `validate_physics`, `validate_physics_deep`, `validate_perf`,
    `validate_replay_visual_fidelity.bat` (owner-approved refresh),
    `validate_full`.

- [ ] P7 — Final matrix, comment audit, independent review, closure.
  - [ ] Full Measurement Ledger recorded at final tip, including the
    ratified-target verdict (≥4,000 awake bodies in ~1 ms, sleepy-scene
    scaling) stated honestly as met/missed with numbers.
  - [ ] Touched-source comment audit per
    `Agentic/Skills/comment-style-audit/skill.md` (SpatialGrid's
    learning-header vocabulary — generation stamping, rebuild language —
    must be rewritten to teach the persistent design).
  - [ ] One independent review over the logical broadphase/sleep/store
    module: determinism-transition hygiene (exactly one — or two, if P6
    ran — baseline moves, each with committed same-state work-equivalence
    evidence), no hidden history dependence, no hot-path allocation, capacity
    diagnostics present. Any credible finding reopens its task.
  - [ ] Final gates from final source: `validate_full`,
    `validate_physics_deep`, `validate_perf`. Update MASTER-PLAN and
    SessionState, append the closure report, delete this plan.

## Dependencies And Decisions

- Independent of the DX12, naming, and hardening lanes (no shared files);
  may run in parallel on its own branch. Coordinate merges with
  `small-findings-hardening` H3 only if its cast rulings touch
  `Physics/PhysicsFixedList.h`.
- P0 owner decisions: branch; sleepy-scene shape and capacity override;
  targets; P1 transition authorization. P6 was initially deferred to P5; the
  owner replaced that manual checkpoint on 2026-07-18 with the binding
  automatic evidence gate and autonomous defer/continue fallback above.
- Binding precedents this plan inherits: B0 inclusive-marker accounting;
  S5/S7 exact-sum and no-dark-path rulings; the
  deterministic-parallel-mutual-gravity 0/1/4-worker acceptance pattern;
  the AGENTS.md physics-baseline-refresh and rule-11 replay-golden
  protocols.
- Risk register: P1 is the highest-governance task (baseline + golden
  transition) and deliberately happens **before** the perf work so every
  subsequent task is provable byte-exact; if the P1 same-state dual-driver
  oracle cannot be made to pass, the campaign halts for owner review rather
  than proceeding with unproved work membership. The sleepy_5000 scene must
  be re-settled and re-captured after any upstream physics change lands from
  another lane.

## Acceptance

- Broadphase maintenance cost is proportional to moved bodies; the
  reinsertion counter proves it per scene.
- Sleeping bodies generate zero broadphase/force/integration/bounds work;
  sleepy_5000 step cost tracks awake count with an identical CSV.
- Exactly one required transition (or two when P6 passes its pre-authorized
  evidence gate),
  each with committed same-state work-equivalence evidence; byte-exact CSVs at
  every other task boundary; 0/1/4-worker invariance everywhere.
- The full Measurement Ledger is continuous across P0-P7 with no
  unexplained regressions, and the final matrix records the body-count
  verdict against the ratified target.
- Independent review clear; all mapped gates pass from final source.

# Physics Body-Count Scale Campaign — Persistent Broadphase, Free Sleepers, Bandwidth Diet

Date: 2026-07-18
Status: Active — 1/8 tasks (P0 complete; P1 blocked on owner review)
Branch: `nightrunner-18th-july`
Impact area: `SkullbonezSource/Physics/SpatialGrid.*`,
`Physics/Stages/PhysicsBroadphaseStage.*`, `Physics/Stages/PhysicsForceStage.*`,
`Physics/Stages/PhysicsSleepController.*`, `Physics/PhysicsBodyStore.*`,
`Physics/PhysicsWorld.*`, scale scenes, physics/perf baselines, replay
visual-fidelity golden (P1/P6 transitions only, owner-approved)
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
(P4), and — owner-gated after the mid-campaign checkpoint — graph-colored
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
- Outside P1 and P6 (each individually owner-approved with equivalence
  evidence), zero baseline, golden, screenshot, or coverage-floor refresh.

## Determinism Transition Protocol (binding for P1 and P6)

A transition task may change float-visible results **only** by reordering
identical work, never by changing the work:

1. Build the equivalence probe first: a Debug-only dump of the per-tick
   candidate-pair **set** (P1) or per-iteration impulse **set** (P6),
   order-insensitive (sorted before hashing).
2. Run old code and new code over all four `physics_scale_*` scenes plus
   the P0 sleeping-heavy scene and the regression varied scene; the
   equivalence probe must match tick-for-tick before any baseline moves.
3. Only then regenerate, from the final Debug binary per the AGENTS.md
   baseline rules: the deterministic physics CSVs (`validate_physics` set;
   `validate_physics_deep` set if its scenes' outcomes moved) and — with
   explicit per-instance owner approval under MASTER rule 11 — the 200-box
   replay visual-fidelity golden, using exactly one engine process and one
   prediction generation.
4. Rerun the matching gates against the regenerated artifacts in the same
   task. A transition without a passing equivalence probe is reverted, not
   fixed forward.
5. Thread-count invariance: every task in this campaign, transition or
   not, must produce byte-identical physics CSVs at worker counts 0, 1,
   and 4 (precedent: deterministic-parallel-mutual-gravity acceptance).

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
    set equivalence first, and deferred P6 authorization to P5. Full, perf,
    platform-marker smoke, 0/1/4-worker determinism, comment audit, and
    independent review passed with zero artifact or coverage-floor refresh.

- [ ] P1 — Canonical pair-order transition (the only behavior-visible flip).
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
  - [ ] Equivalence probe passes: identical per-tick pair sets, all six
    scenes, old vs new emission.
  - [ ] Baselines transitioned per the protocol: physics CSVs regenerated
    from the final Debug binary; owner-approved one-process 200-box golden
    regeneration; `validate_physics`, `validate_physics_deep` (if its
    scenes moved), and `validate_replay_visual_fidelity.bat` all pass
    against the transitioned artifacts.
  - [ ] Thread-count invariance at 0/1/4 workers.
  - Expected cost: neutral-to-noise step time (sorting is O(n+k) with tiny
    constants); record the matrix anyway.
  - Blocker recorded 2026-07-18: the allocation-free canonical implementation
    and Debug sorted-set probe were built and run for 360 fixed ticks over all
    six required scenes. Five scenes matched old/new exactly. In
    `physics_bench_varied`, canonical solver history removed normalized pair
    `(18,20)` from the final candidate set at ticks 152 and 332 (legacy count
    10, canonical count 9), violating the binding tick-for-tick set-equivalence
    condition. The transition and probe were reverted completely and no
    artifact moved. P2-P7 remain dependency-blocked until the owner either
    supplies a design that meets the existing protocol or explicitly revises
    the transition protocol with new acceptance evidence.
  - Follow-up evidence: one exact Debug executable, selected by a diagnostic
    legacy/canonical toggle, first diverged in deterministic physics state at
    regression row/frame 102 (body 15 velocity by 0.0001); its first final-set
    divergence was 50 fixed ticks later at tick 152. Independent review confirms
    this causal ordering is expected from order-dependent projected
    Gauss-Seidel solving, not emitter-set loss. The written old/new evolving-run
    rule still requires an explicit owner amendment before work resumes.

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

- [ ] P5 — Mid-campaign checkpoint and Tier-2 owner decision.
  - [ ] Consolidated matrix across P0-P4 with a written attribution
    narrative (which milliseconds moved, where, and why), committed to the
    campaign report.
  - [ ] Owner decision recorded: proceed to P6 (graph-colored solver
    parallelism, second baseline transition) only if the evidence shows
    single-island solver/narrowphase time is now a binding constraint on
    the ratified body-count target; otherwise strike P6, renumber closure,
    and update the MASTER ledger denominator in the same commit (25→24
    within this plan's rows).
  - Gate: none (documentation + owner decision).

- [ ] P6 — (Conditional, owner-gated at P5) Deterministic graph-colored
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
    Transition Protocol (impulse-set equivalence probe, physics CSV
    regeneration, owner-approved golden regeneration, one process).
  - [ ] Equivalence probe + transition artifacts + gates per protocol.
  - [ ] Thread-count invariance 0/1/4/8 workers, byte-identical.
  - [ ] Measurement matrix recorded, including a one-big-island stress
    witness (the 200-brick wall class of scene) at 1/4/8 workers.
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
    ran — baseline moves, each with committed equivalence evidence), no
    hidden history dependence, no hot-path allocation, capacity
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
  targets; P1 transition authorization; P6 pre-authorization or deferral
  to P5.
- Binding precedents this plan inherits: B0 inclusive-marker accounting;
  S5/S7 exact-sum and no-dark-path rulings; the
  deterministic-parallel-mutual-gravity 0/1/4-worker acceptance pattern;
  the AGENTS.md physics-baseline-refresh and rule-11 replay-golden
  protocols.
- Risk register: P1 is the highest-governance task (baseline + golden
  transition) and deliberately happens **before** the perf work so every
  subsequent task is provable byte-exact; if the P1 equivalence probe
  cannot be made to pass, the campaign halts for owner review rather than
  proceeding order-fuzzy. The sleepy_5000 scene must be re-settled and
  re-captured after any upstream physics change lands from another lane.

## Acceptance

- Broadphase maintenance cost is proportional to moved bodies; the
  reinsertion counter proves it per scene.
- Sleeping bodies generate zero broadphase/force/integration/bounds work;
  sleepy_5000 step cost tracks awake count with an identical CSV.
- Exactly one (or two with P6) owner-approved determinism transitions,
  each with committed set-equivalence evidence; byte-exact CSVs at every
  other task boundary; 0/1/4-worker invariance everywhere.
- The full Measurement Ledger is continuous across P0-P7 with no
  unexplained regressions, and the final matrix records the body-count
  verdict against the ratified target.
- Independent review clear; all mapped gates pass from final source.

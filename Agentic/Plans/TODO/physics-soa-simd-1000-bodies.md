# Physics SoA/SIMD — 1,000+ Bodies Under An AVX2-Certified Envelope

Date: 2026-07-16
Status: ACTIVE at 8/9 (S0-S7 complete; S8 closure active). On 2026-07-17 the
owner rejected the SIMD cutover and explicitly directed S7 to retain the
byte-exact SoA layout, delete all SIMD/toggle complexity, simplify the
SpatialGrid correctness fix, remove attribution-only production counters, and
close with full validation. S7 is green; S8 publishes closure and deletes this
completed plan. No physics behavioral baseline regeneration is authorized.
Impact area: retained `PhysicsBodyStore` SoA layout, scalar physics stage
owners, rejected SIMD deletion, bounded SpatialGrid storage, config migration,
coverage enforcement, and provenance-only replay manifest reconciliation
Owner: physics

---

## Problem And Evidence

The engine's physics is scalar and AoS at tip `20fe6f2d`:

1. `PhysicsBodyStore` keeps each body's fields contiguous
   (`PhysicsBodyRecord`), so vector units cannot load eight consecutive
   `position.x` values; the compiler's only wins are incidental packed-SSE
   fragments (the 40 instructions the FP-envelope diagnosis found in the
   manifold TU — accidental, not designed).
2. All stage kernels are scalar SSE under `/fp:precise` with contraction
   pinned off (`FloatingPointContract.h`). Per-body stages (integration,
   force application, gravity, broadphase binning, narrowphase pruning) leave
   a 4-8× arithmetic factor and a larger bandwidth factor unused.
3. The gated content tops out around the 200-box wall and the 520-body
   gravity fixture; the owner's 2026-07-16 ruling sets a concrete scale
   target of **1,000+ interactive bodies**, which satisfies the standing
   "concrete perf motivation" precondition recorded in MASTER for
   baseline-entangled layout work.

Owner rulings 2026-07-16 (this plan's charter):
- Motivation: **scale target, 1,000+ bodies** within a fixed-step budget
  ratified at S0 from measured evidence.
- Envelope: **AVX2 + FMA, pinned, no runtime dispatch** — one certified
  binary envelope; machine-dependent dispatch is banned because it would
  make byte-exact validation machine-dependent.
- Baselines: **single cutover event** — all SIMD work lands behind a config
  toggle defaulting OFF and byte-exact to today; one final task flips the
  default, regenerates everything once under owner approval, and deletes the
  toggle.
- Depth: **hot-field SoA store** — `PhysicsBodyStore` itself splits hot
  fields into SoA arrays; cold fields stay in records; no per-tick
  pack/unpack tax.

## The Two-Event Structure (why this plan is shaped the way it is)

**Event 1 — layout (S1-S3): bit-neutral.** Splitting storage into SoA arrays
while every loop keeps its current iteration order and scalar arithmetic
changes no float result. These tasks run under the EXISTING byte-exact gates
with zero baseline refresh, exactly like every prior campaign.

**Event 2 — widening experiment (S4-S6), rejected at S7.** The 8-wide kernels
reordered accumulation and used explicit FMA, so they could not be byte-exact
against the scalar oracle. Their measured result did not justify a cutover;
S7 therefore deletes the kernels and toggle and retains scalar SoA.

## Non-Goals

- No runtime CPU dispatch, no per-machine code paths, no FTZ/DAZ — denormal
  handling stays IEEE because flush-to-zero silently changes results and is
  process-global state.
- No solver vectorization remains in this plan. S7 deletes S6's experimental
  row-preparation kernel; any future widening needs a new measured plan.
- No permanent dual scalar/SIMD paths after S7: the SIMD side, toggle, and
  cutover plumbing are deleted. The retained product path is scalar SoA.
- No sleep/wake policy, contact, or gameplay behavior changes. The 512-body
  parallel-gravity cap and its serial fallback keep their semantics on the
  new layout.
- No manifold hysteresis. S7 retains scalar arithmetic and the existing
  byte-exact behavioral envelope.

## Binding Design Rules

1. **Bit-neutral product path throughout.** S0-S6 historically ended with the
   unchanged byte-exact scalar gate. S7 retains that path and does not refresh
   behavioral baselines; revert-on-diff rather than fixing forward.
2. **Experimental intrinsics stay isolated and leave no residue.** S4-S6 kept
   AVX2/FMA in dedicated kernel TUs and never changed project-wide codegen.
   Because the owner rejected cutover, S7 deletes those TUs and their per-file
   build policy while retaining the global `fp_contract(off)` determinism pin.
3. **Historical width evidence is not product complexity.** S4-S6 proved
   masked remainder behavior while the experiment was live. S7 deletes every
   lane kernel and leaves the existing fixed worker-chunk ordering unchanged.
4. **Alignment through the allocation policy.** SoA arrays are 32-byte
   aligned via the existing preallocation owners; allowlist rows update in
   the same commit; checker self-test + repo scan on every allocation-moving
   commit. Capacity, high-water, and fatal diagnostics follow the store's
   existing conventions.
5. **One concern per task, one commit per task**, gate evidence pasted.
   Stage-owner boundaries from the PhysicsWorld campaign are load-bearing:
   a kernel belongs to exactly one stage owner and reads/writes only that
   stage's declared inputs/outputs.
6. **S7 does not refresh behavioral baselines or goldens.** Config v4 may
   reconcile replay provenance hashes only under the standing mechanically
   verified owner ruling; replay mass reduction is already closed.
7. Comment standard throughout. S4-S6 kernel hazards remain recorded in their
   reports; S7 removes those source files and audits every retained source edit.

## Task Checklist

- [x] **S0 — Registration, benchmark authoring, and budget ratification.**
      (a) Register in MASTER (ledger 0/9) and SessionState (skip if the
      activation commit already did it — verify, do not duplicate).
      (b) Author the scale benchmark: a deterministic 1,000-body scene (and
      a 2,000-body stretch variant) through the normal authored-scene
      pipeline with fixed seeds, added to `validate_perf`'s physics bench
      lane as measurement-only (no pass/fail yet). Capacity work this
      exposes (sleep-counter widths, scratch reserves, spatial-grid cell
      tuning at 1,000 bodies) is recorded — and fixed here only if it blocks
      the benchmark from running at all.
      (c) Measure the scalar-AoS baseline: step-time breakdown per stage at
      200/520/1,000/2,000 bodies on the reference machine, committed to
      `Agentic/Reports/2026-07-16/soa-simd-s0-baseline.md`.
      (d) Owner ratifies the budget from evidence (e.g. "1,000 bodies ≤ N ms
      average fixed-step on the reference machine") — this number becomes
      the campaign's acceptance criterion. Reference mega-relevant gates run
      once on the unmodified tip for certification.
      Evidence: the deterministic authored 200/520/1,000/2,000-body matrix and
      capacity audit are recorded in
      `Agentic/Reports/2026-07-16/soa-simd-s0-baseline.md`. The scalar-AoS
      1,000-body Physics Step averages 0.9978 ms on the Threadripper 3970X;
      the ratified final-cutover budget is no more than 0.80 ms. Performance,
      byte-exact physics, full, and the single reference mega gate passed with
      no baseline or golden refresh.
- [x] **S1 — Hot-field SoA split inside `PhysicsBodyStore` (bit-neutral).**
      Hot fields move to parallel arrays owned by the store: position,
      orientation, linear/angular velocity, inverse mass, inverse inertia,
      per-body flags (fixed/awake), bounding radius. Cold/authoring fields
      stay in the record. The store keeps its public record view initially
      via an accessor shim that reads through to SoA (this is a transitional
      seam and MUST carry a deletion condition per the migration-cleanup
      rule: S2 deletes it). Iteration orders unchanged everywhere.
      Gate: `validate_physics` byte-exact; `validate_physics_deep` once
      (layout change under every sleep/contact path); allocation checks.
      Evidence: `Agentic/Reports/2026-07-16/soa-simd-s1-layout.md` records the
      20 aligned component arrays, exact two-way compatibility seam and S2
      deletion condition, 204-test alignment/coherence coverage, byte-exact
      normal/deep physics gates, clean allocation checks, and 3/3 comment audit.
- [x] **S2 — Consumer migration and shim deletion (bit-neutral).** Every
      stage owner, replay capture, presentation sync, and diagnostics
      consumer reads the SoA arrays (or narrow spans of them) directly; the
      S1 accessor shim is deleted; `std::span` field views replace record
      references in stage contexts where the stage only touches hot fields.
      This is the wide mechanical task — budget it like the sleep-controller
      extraction. Gate: `validate_physics` byte-exact + `validate_tests` +
      one mega-gate invocation (replay capture reads moved) + allocation
      checks.
      Evidence: `Agentic/Reports/2026-07-16/soa-simd-s2-consumer-migration.md`
      records the cold-only record, sole-authority aligned hot arrays, deletion
      of every S1 compatibility helper, 61/61 touched-file comment audit,
      204-test/full/allocation proof, final 44,401-line byte-exact physics
      output, and the single passing one-process replay mega invocation. No
      baseline or golden changed.
- [x] **S3 — SoA-scalar measurement checkpoint.** Re-run the S0 benchmark
      matrix on the SoA-scalar build and commit the comparison: the layout
      alone should already move bandwidth-bound stages. If SoA-scalar
      regresses any stage beyond noise, diagnose and fix before any kernel
      work (a layout that loses scalar perf will not win it back in SIMD).
      Gates: `validate_perf` + byte-exact `validate_physics`.
      Evidence: `Agentic/Reports/2026-07-16/soa-simd-s3-scalar-checkpoint.md`
      records the diagnosed 20-span by-value copy/full-row traffic regression,
      the bit-neutral correction, the final 200/520/1,000/2,000-body matrix,
      0.9795 ms at 1,000 bodies versus S0's 0.9978 ms, 15/15 comment audit,
      clean performance gate, and 44,401-line byte-exact physics proof.
- [x] **S4 — Kernel infrastructure + integration pilot (dark).** Add the
      `physicsExecution.simdKernels` config toggle (default OFF, config
      version bump + migration + format tests per the versioning policy),
      the kernel TU layout with per-file AVX2 compilation, lane-mask
      helpers, and ONE pilot: the integration kernel (8-wide position/
      velocity advance with explicit FMA). A/B harness: toggle ON vs OFF on
      the benchmark scenes — results compared with a documented tolerance
      oracle (not byte-equal — record max divergence per field per 1,000
      ticks in the report), plus stability checks (no NaN/Inf, sleep
      outcomes equivalent). Toggle OFF remains byte-exact (gate proof).
      Perf: kernel-level speedup recorded.
      Evidence: `Agentic/Reports/2026-07-16/soa-simd-s4-integration-pilot.md`
      records the v3 default-OFF config/migration, per-file AVX2/FMA kernel,
      masked-tail coverage, honest chaotic-scale A/B oracle, 4.5% pilot-marker
      speedup, 44,401-line byte-exact OFF proof, and green full/perf gates.
- [x] **S5 — Force, gravity, and broadphase kernels (dark).** Apply-forces
      kernel; mutual-gravity pair kernel under the existing ≤512 parallel
      path (serial >512 fallback keeps its exact semantics — on the SIMD
      path divergence is expected and certified at S7 like everything
      else); broadphase AABB/cell binning kernel. Same A/B + stability +
      perf evidence per kernel; toggle OFF byte-exact gate every commit.
      Evidence: `Agentic/Reports/2026-07-16/soa-simd-s5-force-gravity-broadphase.md`
      records dedicated force/pair/bounds kernels, masked-tail unit coverage,
      a passing 2,000,000-row chaotic-scale oracle, a focused 72,000-row
      mutual-gravity oracle, byte-identical pre/post-optimization ON artifacts,
      and green fast/physics/performance gates. The paired Profile evidence is
      honestly negative, so S7's 0.80 ms cutover precondition remains binding.
- [x] **S6 — Narrowphase prune + solver row-prep kernels (dark).**
      Sphere-sphere/AABB rejection kernel in the narrowphase front-end and
      vectorized solver row preparation (mass/anchor/bias precompute). The
      solver core iteration explicitly stays scalar; the report records the
      measured share of step time it retains and the follow-up trigger
      ("convert if solver core exceeds X% of budget at 1,000 bodies").
      Same evidence set; toggle OFF byte-exact.
      Evidence: `Agentic/Reports/2026-07-17/soa-simd-s6-narrowphase-solver.md`
      records two dedicated AVX2/FMA TUs, masked-tail coverage, the 2,000,000-
      row A/B envelope, an all-284 toggle-ON doctest pass, the complete enabled
      200/520/1,000/2,000 matrix, and the retained scalar-core measurement.
- [x] **S7 — Retain SoA; remove rejected SIMD and production complexity.**
      Delete all SIMD kernel TUs, scalar/SIMD branches, toggle/config/CLI/build
      plumbing, and SIMD-specific tests. Replace the two-route SpatialGrid
      storage with one fixed 8,192-cell open-addressed table that preserves
      complete coverage through capacity and fails fatally at exhaustion.
      Remove attribution-only frame counters and query schema. Bump engine
      config to v4 with a deterministic v3-to-v4 migration that removes the
      obsolete key. Reconcile replay manifests only when the final config hash
      changes, under the standing provenance-hash-only owner ruling. Wire the
      ratified coverage floor into the mandatory CPU umbrella. Run format,
      coverage, performance, replay-fidelity, and owner-requested full gates.
      Evidence: `Agentic/Reports/2026-07-17/soa-simd-s7-scalar-cleanup.md`
      records 3,190 deleted lines, the single complete 8,192-row grid, config
      v4/provenance-only migration, mandatory coverage, honest mixed-scale
      performance, 30/30 comment audit, resolved independent review, and a
      green final full gate with byte-exact physics.
- [ ] **S8 — Final review and closure.** Independent review across the whole
      campaign: the SoA scalar layout and bit-neutral S1-S3 history remain;
      no SIMD kernel, toggle, CLI, config, project, or test residue remains;
      SpatialGrid has one bounded storage/lookup path with explicit fatal
      exhaustion; coverage floors are merge-gated; touched comments meet the
      repository standard; final gates are green. Record measured performance
      honestly against S0 and the retained SoA result. Publish the closure
      report, update MASTER/SessionState, and delete the plan per inventory
      rule 4.

## Dependencies And Decisions

- The `replay-mass-reduction` dependency is closed. S7 performs no behavioral
  golden regeneration and is the only active plan runner.
- Owner rulings 2026-07-16: scale target 1,000+; AVX2+FMA pinned, no
  dispatch; single cutover; hot-field SoA store. S0(d) ratified the numeric
  budget. The 2026-07-17 owner ruling rejected that cutover and instead
  authorized scalar-only cleanup with no behavioral regeneration.
- Prior enablers relied upon: `FloatingPointContract.h` pin, the honest
  determinism-envelope contract, PhysicsWorld stage owners, and the
  worker-pool fixed-chunk determinism rules.
- Hardware note: S4-S6 A/B evidence remains bound to the recorded AVX2-capable
  reference machine. The retained S7 scalar product path has no AVX2 runtime
  requirement.

## Acceptance

- The byte-exact SoA layout and scalar path remain; the rejected SIMD kernels,
  toggle, CLI/config/build plumbing, and dedicated SIMD tests are deleted.
- SpatialGrid retains complete bounded coverage through 8,192 unique cells and
  produces an owner-attributed fatal diagnostic on the next unique cell.
- No behavioral physics baseline regeneration occurs. Replay provenance hashes
  may change only as a mechanically verified consequence of config v4.
- The coverage floor is part of the mandatory CPU umbrella and final coverage,
  performance, replay-fidelity, and full gates pass.
- S8 independent review records zero credible findings and the closure report
  states the retained SoA performance honestly against S0.

## Validation

- S0/S3: `validate_perf` + byte-exact `validate_physics`.
- S1: `validate_physics` + one `validate_physics_deep` + allocation checks.
- S2: `validate_physics` + `validate_tests` + one mega-gate invocation +
  allocation checks.
- S4-S6: byte-exact `validate_physics` (toggle OFF) + A/B kernel reports +
  `validate_perf` per kernel task.
- S7: `tools\migrate_data_formats.py --check`, `validate_coverage`,
  `validate_perf`, `validate_replay_visual_fidelity`, and `validate_full`.
- S8: independent closure evidence plus a clean-tree confirmation. All outputs
  are recorded per task.

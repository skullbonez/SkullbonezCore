# Physics SoA/SIMD — 1,000+ Bodies Under An AVX2-Certified Envelope

Date: 2026-07-16
Status: Active — registered in `MASTER-PLAN.md`; S0-S2 are complete and S3 is
next. 3/9 tasks complete.
Impact area: `PhysicsBodyStore` layout, all seven physics stage owners, new
SIMD kernel TUs, build `/arch` policy, FP determinism envelope, and — at the
S7 cutover only — every physics baseline and replay golden
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

**Event 2 — widening (S4-S6 behind a toggle, S7 cutover): result-changing.**
8-wide kernels reorder accumulation and use explicit FMA, so they cannot be
byte-exact against today. They ship dark (toggle OFF = byte-exact scalar SoA
path) until S7 flips the default in one owner-approved ceremony.

## Non-Goals

- No runtime CPU dispatch, no per-machine code paths, no FTZ/DAZ — denormal
  handling stays IEEE because flush-to-zero silently changes results and is
  process-global state.
- No solver-core vectorization in this plan: the persistent-contact
  iteration is gather-heavy and dependency-chained; S6 converts only its
  row *preparation*. Core-iteration SIMD is a recorded follow-up with a
  measurement trigger, not a silent scope creep.
- No permanent dual scalar/SIMD paths after S7: the toggle and the scalar
  kernel bodies are deleted at cutover (the SoA-scalar loops that remain are
  the tail/reference forms the kernels themselves need).
- No sleep/wake policy, contact, or gameplay behavior changes. The 512-body
  parallel-gravity cap and its serial fallback keep their semantics on the
  new layout.
- No manifold hysteresis — still deferred; but S7's recertification is the
  recorded trigger to re-examine that deferral, since knife edges WILL move.

## Binding Design Rules

1. **Bit-neutral until S7.** Every task S0-S6 ends with the unchanged
   byte-exact `validate_physics` gate (toggle OFF), and S1-S3 must also be
   byte-exact with the toggle ON (toggle does nothing until kernels exist).
   Revert-on-diff; never fix forward a float difference before S7.
2. **Explicit intrinsics in dedicated kernel TUs.** AVX2/FMA via
   `immintrin.h` in `Physics/Stages/Kernels/*.cpp` only. The global
   `fp_contract(off)` pin stays: it governs *compiler-generated* contraction;
   explicit `_mm256_fmadd_ps` is an intentional, documented operation, and
   the envelope doc records that distinction. No `/arch:AVX2` on any
   project-wide configuration before S7 — project-wide arch changes alter
   codegen (and results) everywhere, which is exactly the accidental
   vectorization the diagnosis caught.
3. **Deterministic width handling.** Kernel results must be independent of
   body count modulo 8: remainder lanes use masked loads/stores computing
   the identical per-lane arithmetic, never a scalar tail with different
   rounding shape. Worker chunk boundaries remain a pure function of body
   count (existing rule), and kernels never accumulate across lane
   boundaries except through documented, fixed-order reductions.
4. **Alignment through the allocation policy.** SoA arrays are 32-byte
   aligned via the existing preallocation owners; allowlist rows update in
   the same commit; checker self-test + repo scan on every allocation-moving
   commit. Capacity, high-water, and fatal diagnostics follow the store's
   existing conventions.
5. **One concern per task, one commit per task**, gate evidence pasted.
   Stage-owner boundaries from the PhysicsWorld campaign are load-bearing:
   a kernel belongs to exactly one stage owner and reads/writes only that
   stage's declared inputs/outputs.
6. **S7 is the only task allowed to touch baselines or goldens**, and it may
   not start while any other campaign with a zero-refresh requirement is
   live (the replay mass-reduction campaign must be closed first).
7. Comment standard throughout; kernel TUs get `Hazard:` notes on lane
   masking, alignment assumptions, and FMA rounding.

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
- [ ] **S3 — SoA-scalar measurement checkpoint.** Re-run the S0 benchmark
      matrix on the SoA-scalar build and commit the comparison: the layout
      alone should already move bandwidth-bound stages. If SoA-scalar
      regresses any stage beyond noise, diagnose and fix before any kernel
      work (a layout that loses scalar perf will not win it back in SIMD).
      Gates: `validate_perf` + byte-exact `validate_physics`.
- [ ] **S4 — Kernel infrastructure + integration pilot (dark).** Add the
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
- [ ] **S5 — Force, gravity, and broadphase kernels (dark).** Apply-forces
      kernel; mutual-gravity pair kernel under the existing ≤512 parallel
      path (serial >512 fallback keeps its exact semantics — on the SIMD
      path divergence is expected and certified at S7 like everything
      else); broadphase AABB/cell binning kernel. Same A/B + stability +
      perf evidence per kernel; toggle OFF byte-exact gate every commit.
- [ ] **S6 — Narrowphase prune + solver row-prep kernels (dark).**
      Sphere-sphere/AABB rejection kernel in the narrowphase front-end and
      vectorized solver row preparation (mass/anchor/bias precompute). The
      solver core iteration explicitly stays scalar; the report records the
      measured share of step time it retains and the follow-up trigger
      ("convert if solver core exceeds X% of budget at 1,000 bodies").
      Same evidence set; toggle OFF byte-exact.
- [ ] **S7 — The cutover ceremony (the one radioactive day).** Preconditions:
      replay-mass-reduction closed; S0 budget met or beaten with toggle ON
      on the benchmark matrix; all dark-kernel A/B reports reviewed. Then,
      in one ordered commit series with explicit owner approval recorded:
      (1) flip `simdKernels` default ON and set project-wide `/arch:AVX2`
      (the certified envelope now includes it — SSE-only machines are no
      longer supported targets, stated in README/envelope docs);
      (2) regenerate from final binaries: the 44,401-line varied CSV, deep
      physics baselines, SkullScope baselines, physics query goldens, the
      200-box replay golden manifest (one generation, owner-approved), and
      perf baselines;
      (3) update the determinism-envelope documentation: certified envelope
      is now MSVC v143 + `/fp:precise` + `fp_contract(off)` + AVX2/FMA
      kernels + regenerated content, and the fixture-boundary rules from
      fp-envelope-hardening are re-affirmed against the new bits;
      (4) delete the toggle and dead scalar kernel bodies;
      (5) rerun the full gate suite green against the regenerated set:
      `validate_full`, `validate_physics_deep`, mega gate, `validate_perf`.
      A failure at any step reverts the entire series — there is no
      half-cut-over state on the branch.
- [ ] **S8 — Final review and closure.** Independent review across the whole
      campaign: layout bit-neutrality history (S1-S3 gates all green in git
      history), kernel ownership (each kernel inside exactly one stage
      owner, no cross-stage reach), masked-tail correctness spot checks, no
      dispatch/FTZ/dual-path residue, baseline regeneration completeness
      (no stale golden anywhere — grep the baseline inventory against the
      gate scripts). Budget acceptance stated with measured numbers vs the
      S0 ratified target. Closure report, MASTER/SessionState updates, plan
      deleted per inventory rule 4. Manifold-hysteresis deferral formally
      re-examined and re-ruled (kept deferred or activated) as recorded in
      the fp-envelope plan.

## Dependencies And Decisions

- Queued behind `replay-mass-reduction` (S7's golden regeneration is
  incompatible with that campaign's zero-refresh rule; and one active
  campaign at a time keeps the runner's ledger honest). S0-S3 could in
  principle run earlier as a parallel lane since they are bit-neutral, but
  only with an explicit owner instruction to run two lanes.
- Owner rulings 2026-07-16: scale target 1,000+; AVX2+FMA pinned, no
  dispatch; single cutover; hot-field SoA store. S0(d) ratifies the numeric
  budget; S7 requires a fresh explicit approval for the regeneration
  ceremony itself.
- Prior enablers relied upon: `FloatingPointContract.h` pin, the honest
  determinism-envelope contract, PhysicsWorld stage owners, and the
  worker-pool fixed-chunk determinism rules.
- Hardware note for the reference machine: S0 must record CPU model; the
  certified envelope binds validation evidence to AVX2-capable hardware
  from S4 onward (A/B runs) and exclusively from S7.

## Acceptance

- S0-ratified budget met at 1,000 bodies with the final cut-over binary,
  measured on the reference machine and recorded next to the S0 baseline.
- S1-S6 history shows byte-exact toggle-OFF gates at every commit; S7 is
  the only baseline-touching commit series in the campaign.
- No runtime dispatch, no FTZ/DAZ, no dual paths, no `/arch` change before
  S7 (grep + build-log proof).
- Kernel divergence reports exist for every kernel with recorded tolerances
  and stability evidence.
- S8 independent review records zero credible findings; regenerated
  baseline inventory is complete against every gate script.

## Validation

- S0/S3: `validate_perf` + byte-exact `validate_physics`.
- S1: `validate_physics` + one `validate_physics_deep` + allocation checks.
- S2: `validate_physics` + `validate_tests` + one mega-gate invocation +
  allocation checks.
- S4-S6: byte-exact `validate_physics` (toggle OFF) + A/B kernel reports +
  `validate_perf` per kernel task.
- S7: full regenerated-suite proof (`validate_full`,
  `validate_physics_deep`, mega gate, `validate_perf`).
- S8: closure evidence per the checklist. All outputs pasted per task.

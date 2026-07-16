# Replay Mass Reduction — Right-Size The 33,783-Line Replay Subsystem

Date: 2026-07-16
Status: Active — registered in `MASTER-PLAN.md` by the 2026-07-16 activation
governance commit. 0/8 tasks complete. R0's registration sub-step (a) is
already satisfied by that commit; R0 now covers the census, classification
map, map-file baseline, owner bucket ratification, and the reference gate
run only.
Impact area: `Runtime/Replay/*`, automation build boundary, replay artifact
codec, prediction presentation, replay reserve-allocator registrations,
`tools/check_replay_visual_fidelity.py` consumers (schema-frozen)
Owner: replay

---

## Problem And Evidence

`Runtime/Replay/` is **33,783 lines** at tip `c0dd7016` — about 17% of engine
source — and the 2026-07-15 hostile review flagged the proportion, not just
the ownership (ownership was already fixed by `replay-monolith-decomposition`
9/9; this campaign must not reopen that shape). The mass has three distinct
diseases, each with its own correction:

1. **Test harness compiled into the product.** Only three replay files
   reference `SKULLBONEZ_AUTOMATION_DIAGNOSTICS` (`ReplayRuntime.{h,cpp}`,
   `ReplayCoordination.h`). `ReplayValidation.cpp` (**3,729 lines** of probe
   runner, expected-failure checks, and transactional restore probes),
   `ReplayVisualPacketFingerprint.cpp` (611), and the probe-facing share of
   `ReplayPredictionArchive.cpp` (707) compile into every configuration,
   including production Release. Validation tooling this large belongs behind
   the automation boundary the engine already has.
2. **Duplicated mechanics across sibling owners.** Artifact encode/decode
   spans `ReplayRecorder.cpp` (3,488) and `ReplayV2Artifact.cpp` (2,889) with
   overlapping serialization helpers; segment/ribbon/overlay emission spans
   `ReplayPredictionDrawing.cpp` (2,057), `ReplayOverlayRenderer.cpp` (1,151),
   and `ReplayPresentation.cpp` (1,308) with per-lane repetition.
3. **Oversized owner TUs.** `ReplayPrediction.cpp` is **4,424 lines** — the
   largest file in the engine after the PhysicsWorld campaign — and
   `ReplayRecorder.cpp`/`ReplayValidation.cpp` are both larger than any
   non-replay TU.

Correcting this is *proportion and duplication* work: move the harness out of
the product build, deduplicate mechanics into single owners, delete only
owner-ratified dead paths, and partition what remains along the owner-TU
precedent. It is NOT a rewrite, an artifact-format change, or a second
ownership decomposition.

## Goal

Production (non-automation) builds compile no replay probe/validation/
fingerprint code; artifact serialization and presentation emission each have
exactly one mechanical owner; every dead path is deleted under a recorded
owner ruling; no remaining replay TU exceeds ~2,000 lines except where the
closure review records why its state belongs together; and the end-to-end
result is proven behavior-identical by the unchanged 200-box mega gate at
every single task.

## Non-Goals

- **No replay behavior, artifact-format, prediction, tick, or presentation
  change.** The 200-box golden manifest is untouchable; any provenance-only
  reconciliation repeats the explicit-owner-approval protocol used on
  2026-07-16 and touches nothing but hash fields.
- **No ownership re-decomposition.** The six replay owners from the monolith
  campaign keep their boundaries; this plan moves mass, not authority.
- **No arbitrary line quotas.** Deletion happens only through R5's
  owner-ratified dead-path rulings; everything else is relocation and
  deduplication. The final size is whatever honest mass remains.
- **No probe output schema changes.** `tools/check_replay_visual_fidelity.py`,
  `tools/replay_query.py`, and the interaction-report consumers parse these
  outputs; their strings, fields, and exit codes are frozen.
- **No new allocation paths.** Replay's `RuntimeReserveAllocator`
  registrations (the engine's only sanctioned runtime-growth lane) move with
  their owning code, unchanged in owner name, phase check, cap, and counter.

## Binding Design Rules (every task must respect all of these)

1. **MASTER rule 11 governs every task**: each task runs
   `tools\validate_replay_visual_fidelity.bat` exactly once — one engine
   process, one prediction generation; a second launch or generation is an
   immediate failure; no golden refresh. A task that cannot pass the gate
   reverts; never fix forward a replay behavior diff.
2. **Automation-boundary moves are link-level, not `#ifdef` soup.** Probe/
   validation TUs move to automation-only compilation (project configuration
   or a dedicated filter group excluded from non-automation configs), with a
   narrow header seam in the product build. Do not scatter hundreds of
   `#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )` blocks through product
   TUs — the existing three-file seam pattern is the model.
3. **Deduplication must be bit-neutral.** A shared helper replaces two copies
   only when the copies are provably identical in arithmetic and emission
   order; near-duplicates get unified only if the differences are dead
   parameters, and the R0 map records each unification with its evidence.
4. **One concern per task, one commit per task**, each ending with its gate
   evidence pasted in the commit body: the mega gate, `validate_tests` (the
   replay test lanes), and — for tasks that touch presentation/submission —
   `validate_dx12_renderer` + the bounded stress run.
5. **Owner-TU partitions only**: if a TU is split, it is a cohesive physical
   partition of the same owner (the `PhysicsSleepController.State/Wake`
   precedent), never a new owner, facade, or forwarding unit.
6. **Comment standard and allocation policy follow the code**: learning
   headers on new files, allowlist rows move with their vectors/reserves in
   the same commit, checker self-test + repo scan on every allocation-moving
   commit.
7. **Automation lane must still build and gate.** `validate_full` exercises
   the Automation configuration; extraction that breaks probe availability in
   automation builds is a failed task even if Release shrinks.

## Task Checklist

- [ ] **R0 — Census and classification map.**
      (a) MASTER/SessionState registration — ALREADY DONE by the 2026-07-16
      activation governance commit; verify the ledger row reads 0/8 and do
      not re-register.
      (b) Commit the census to
      `Agentic/Reports/2026-07-16/replay-mass-reduction-census.md`: per-file
      line counts at the starting tip; every replay file classified into
      exactly one bucket — `product-runtime` (record/timeline/scrub/present/
      authoring paths a player build needs), `prediction-engine`,
      `artifact-io`, `probe-harness` (automation-only candidates), or
      `presentation-emission`; and a duplication table listing each suspected
      duplicate mechanic pair with file:line ranges (artifact codec helpers,
      segment emission, fingerprint/hash helpers).
      (c) Measure and record the production footprint honestly: Release and
      Profile link with `/MAP`, record the replay-object contribution to
      binary size before any change (this is the R7 comparison number).
      (d) Owner ratifies the bucket map — in particular which of
      `ReplayValidation.cpp`, `ReplayVisualPacketFingerprint.cpp`, and the
      probe share of `ReplayPredictionArchive.cpp` are confirmed
      automation-only. Certification: run the mega gate once on the
      unmodified tip and file the log as the campaign's reference run.
- [ ] **R1 — Automation boundary design (no mass moves yet).** Define the
      link-level boundary: which project/configuration carries
      automation-only replay TUs, what the narrow product-side seam header
      exposes (probe entry points become no-ops or are absent in product
      configs — decide and record which, since `Run` currently constructs
      probe paths only under the define), and how `validate_full`'s
      Automation lane picks the TUs up. Prove the design with ONE moved
      pilot file (`ReplayVisualPacketFingerprint.cpp`, the smallest
      candidate): Release/Profile binaries lose its objects (map-file diff),
      Automation still passes its lanes, mega gate green.
- [ ] **R2 — Move the probe harness behind the boundary.**
      `ReplayValidation.cpp` and the R0-confirmed probe share of
      `ReplayPredictionArchive.cpp` move to automation-only compilation using
      the R1 pattern. Product-config grep-proof: no probe runner, expected-
      failure, or fingerprint symbol in the Release map file. Automation
      config: every existing probe CLI flag and report string byte-identical
      (probe output schema freeze). Gates: mega gate, `validate_tests`, plus
      one full `validate_full` because build-configuration files changed.
- [ ] **R3 — Artifact codec consolidation.** Extract the single serialization
      owner for replay artifacts: the R0 duplication table's codec rows
      (shared varint/field/section helpers currently repeated between
      `ReplayRecorder.cpp` and `ReplayV2Artifact.cpp`) collapse into one
      mechanical codec unit consumed by both owners. Byte-identical artifact
      proof: record one bounded replay artifact before and after the change
      with the same seed/scene and byte-compare the files (this is stronger
      than the mega gate for IO code and costs no extra engine process —
      use the durable artifact the mega gate already produces).
- [ ] **R4 — Presentation emission deduplication.** Unify the repeated
      segment/ribbon/overlay emission helpers across
      `ReplayPredictionDrawing.cpp`, `ReplayOverlayRenderer.cpp`, and
      `ReplayPresentation.cpp` per the R0 table — same submission order, same
      vertex arithmetic, R0-recorded evidence per unification. Gates: mega
      gate, `validate_dx12_renderer` + `run_graphics_stress.bat 1` (submission
      code), `validate_tests`.
- [ ] **R5 — Dead-path audit and owner deletion rulings.** Mechanical
      reachability pass over the replay surface (public functions with zero
      call sites outside tests, config branches no scene/CLI can reach,
      superseded pre-V2 artifact paths *not* covered by the versioned-
      migration policy's legacy window). Every candidate gets a row: evidence,
      migration-policy check, owner ruling KEEP or DELETE. Only DELETE rows
      are removed, each with its ruling cited in the commit. No ruling, no
      deletion — this task may legitimately delete little; the census stays
      honest either way.
- [ ] **R6 — Oversized-TU partition pass.** Remaining TUs over ~2,000 lines
      (`ReplayPrediction.cpp` 4,424 is the primary target; `ReplayRecorder`
      as post-R3 size dictates) split into cohesive owner partitions
      (`ReplayPrediction.Simulation.cpp` / `.Trails.cpp` style — same owner,
      rule 5), or the closure review records why the remaining mass is one
      cohesive concern. No logic edits in this task — moves only, verified by
      the mega gate.
- [ ] **R7 — Final census, binary proof, and independent review.** Re-run the
      R0 census and map-file measurement from the final tree: report
      product-config replay line/object deltas, automation-config totals, and
      per-bucket movements next to the R0 numbers. Mandatory single
      independent review over the whole campaign: no authority moved, no
      forwarding facades, no `#ifdef` scatter, dedup unifications bit-honest,
      deletion rulings all cited. Any credible finding reopens the owning
      task.
- [ ] **R8 — Closure gates.** From the final source: `validate_full` (build
      configs and broad surface changed across the campaign), the final mega
      gate invocation, `validate_perf` (presentation emission was touched),
      and the allocation-policy self-test + repo scan. Closure report under
      `Agentic/Reports/2026-07-16/`, MASTER/SessionState updated, plan
      deleted per inventory rule 4.

## Dependencies And Decisions

- Starts from `c0dd7016` (runtime mass-reduction campaign closed). No other
  live plan touches `Runtime/Replay/`.
- The 2026-07-16 provenance-hash protocol is precedent: if `engine.cfg` or
  other provenance inputs change during this campaign for unrelated reasons,
  reconciliation of hash fields requires explicit owner approval and touches
  nothing else.
- Owner decisions this plan needs at R0: the automation-only bucket
  confirmation, and the product-config behavior for probe entry points
  (absent vs no-op stubs).
- The deferred SoA/SIMD lane is unaffected; nothing here touches physics or
  the FP envelope.

## Acceptance

- Release/Profile map files contain no probe/validation/fingerprint replay
  objects; automation lanes still pass end to end.
- R0 duplication table fully dispositioned: every row unified (with
  bit-neutral evidence) or individually kept with a reason.
- Every R5 deletion cites an owner ruling; zero deletions without one.
- No replay TU over ~2,000 lines without a recorded cohesion ruling.
- Mega gate green at every task with zero golden refresh (git history is the
  proof); R7 review records zero credible findings.
- R7 census shows the product-build replay footprint reduction next to the
  R0 baseline — reported as measured numbers, not projections.

## Validation

- Every task: one `tools\validate_replay_visual_fidelity.bat` invocation
  (rule 11) + `tools\validate_tests.bat`.
- R2/R8: `tools\validate_full.bat`. R4: `tools\validate_dx12_renderer.bat` +
  `tools\run_graphics_stress.bat 1`. R8 adds `tools\validate_perf.bat` and
  the allocation-policy checks. All outputs pasted per task.

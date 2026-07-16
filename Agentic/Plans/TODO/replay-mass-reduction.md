# Replay Mass Reduction — Right-Size The 33,783-Line Replay Subsystem

Date: 2026-07-16
Status: Active — 8/9 tasks complete. R6 recorded explicit cohesion rulings for
all four oversized/near-threshold translation units and rejected cosmetic or
new-interface splits. Continue at R7's final census, binary proof, and
independent campaign review.
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

1. **Test harness objects compiled into product configurations.** Only three
   replay files reference `SKULLBONEZ_AUTOMATION_DIAGNOSTICS`
   (`ReplayRuntime.{h,cpp}`, `ReplayCoordination.h`). R0 proved the source is
   mixed more narrowly than the activation draft claimed: production restore
   logic remains in `ReplayValidation.cpp` (3,729), the production RVPD codec
   remains in `ReplayPredictionArchive.cpp` (707), while the fingerprint TU
   (611), guarded Debug probe blocks, and the archive round-trip verifier are
   diagnostics-only. Those proven diagnostics portions belong behind the
   configuration-appropriate link boundary.
2. **Duplicated mechanics need source-accurate disposition.** R0 found one
   honest codec candidate between `ReplayPredictionArchive.cpp` and
   `ReplayV2Artifact.cpp`, plus repeated quota/trajectory loops within
   `ReplayPredictionDrawing.cpp` and render-pose loops within
   `ReplayPresentation.cpp`. Recorder hashing is not artifact serialization;
   3D ribbons, screen-space UI, and packet telemetry are separate concerns.
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

Production Release/Profile builds compile no replay probe/validation/
fingerprint code; Automation and Debug retain their existing diagnostics
coverage. Artifact serialization and presentation emission each have
exactly one mechanical owner; every dead path is deleted under a recorded
owner ruling; no remaining replay TU exceeds ~2,000 lines except where the
closure review records why its state belongs together; and the end-to-end
result is proven behavior-identical by the unchanged 200-box mega gate at
every single task.

## Non-Goals

- **No replay behavior, artifact-format, prediction, tick, or presentation
  change.** The 200-box golden manifest is untouchable; any provenance-only
  reconciliation repeats the explicit-owner-approval protocol used on
  2026-07-16 and touches nothing but hash fields. Sole carve-out: R4b
  canonicalizes the R3-F1 schedule-sensitive bookkeeping *values* at
  serialization under its own versioning ruling — gate-covered content,
  layouts, and consumer contracts remain unchanged.
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
   reverts; never fix forward a replay behavior diff. Sole recorded
   exception: R4b's charter pre-authorizes exactly two invocations (owner
   approval 2026-07-16) because a determinism proof requires two artifacts;
   no other task inherits that exception.
2. **Diagnostics-boundary moves are link-level, not `#ifdef` soup.** The
   fingerprint TU belongs to Automation+Debug, the RVPD verifier to Automation,
   and existing `_DEBUG` probe blocks to Debug; all are excluded from
   Release/Profile. Product archive/restore remains in every configuration and
   exposes no probe entry points or no-op stubs. Do not scatter hundreds of
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

- [x] **R0 — Census and classification map.**
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
      (d) Owner ratifies the bucket map and mixed-file boundaries: the
      fingerprint TU is Automation+Debug-only, the RVPD verifier is
      Automation-only, guarded legacy probes remain Debug-only, and production
      archive/restore stays in all builds. Release/Profile expose no probe seam
      or no-op stubs. Certification: run the mega gate once on the unmodified
      tip and file the log as the campaign's reference run.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-census.md`
      reconciles 42/42 files and 33,783 lines; records Release/Profile
      map-attributed baselines of 498,264/456,044 bytes; dispositions D1-D8;
      records the ratified diagnostics matrix and absent product seam; and
      records the single 459.14 s mega gate plus 202/202 test pass.
- [x] **R1 — Automation boundary design (no mass moves yet).** Define the
      ratified link-level boundary: fingerprint code compiles in Automation and
      Debug, while Release/Profile expose no probe declarations or no-op stubs.
      Record how `validate_full`'s Automation lane picks the TU up. Prove the
      design with ONE moved
      pilot file (`ReplayVisualPacketFingerprint.cpp`, the smallest
      candidate): Release/Profile binaries lose its objects (map-file diff),
      Automation still passes its lanes, mega gate green.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r1-fingerprint-linkage.md`
      records the Automation+Debug project matrix and absent product header
      seam. Fresh Release/Profile maps contain zero fingerprint-object rows
      versus R0's 64/200-byte attribution; Automation linked the TU; the
      single 459.87 s mega gate and 202/202 unit-test gate passed unchanged.
- [x] **R2 — Move the probe harness behind the boundary.**
      Physically split the R0-confirmed `_DEBUG` probe blocks from the mixed
      `ReplayValidation.cpp` into a Debug-only TU, and move only
      `VerifyReplayPredictionArchiveRoundTrip` from the production codec into
      an Automation-only TU using the R1 pattern. Product archive/restore logic
      stays in all configurations. Product-config grep-proof: no probe runner,
      expected-failure, or fingerprint symbol in the Release map file. Automation
      config: every existing probe CLI flag and report string byte-identical
      (probe output schema freeze). Gates: mega gate, `validate_tests`, plus
      one full `validate_full` because build-configuration files changed.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r2-diagnostics-split.md`
      records the 1,949-line product restore TU, 1,880-line Debug probe TU,
      73-line Automation verifier TU, configuration matrix, and final Release
      map with zero probe/verifier/fingerprint rows. The single 463.42 s mega
      gate, 202/202 unit tests, and 121.33 s full gate all passed unchanged.
- [x] **R3 — Artifact codec consolidation.** Evaluate and disposition the R0
      duplication table's honest codec candidate: scalar/byte writer-reader
      mechanics in `ReplayPredictionArchive.cpp` and `ReplayV2Artifact.cpp`.
      `ReplayRecorder.cpp` hashing/field traversal is explicitly not artifact
      serialization and remains separate. Collapse only byte-identical leaf
      mechanics into one codec unit; otherwise record the KEEP reason.
      Byte-identical artifact
      proof: record one bounded replay artifact before and after the change
      with the same seed/scene and byte-compare the files (this is stronger
      than the mega gate for IO code and costs no extra engine process —
      use the durable artifact the mega gate already produces).
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r3-codec-disposition.md`
      records D1/D3 KEEP, the full 33,393,417-offset binary diff, the exact
      schedule-sensitive topology/trajectory/reserve bookkeeping fields, and
      formal finding R3-F1. The owner-approved diagnostic exception produced a
      third distinct whole artifact, while all three gate-covered projections
      matched SHA-256 `363841...A2FA`. No source, schema, golden, baseline, or
      provenance field changed.
- [x] **R4 — Presentation emission deduplication.** Evaluate D4/D5/D7 from the
      R0 table: quota/accounting and trajectory loops within
      `ReplayPredictionDrawing.cpp`, plus structurally similar render-pose loops
      within `ReplayPresentation.cpp`. R0 proved that 3D ribbons,
      screen-space UI, and packet telemetry across the three named files are
      separate concerns; do not create a catch-all emission owner. Preserve
      submission order and vertex arithmetic for every accepted unification.
      Gates: mega
      gate, `validate_dx12_renderer` + `run_graphics_stress.bat 1` (submission
      code), `validate_tests`.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r4-presentation-emission.md`
      records D4/D7 UNIFY and D5 KEEP. One quota leaf and one compile-time
      render-pose skeleton remove 20 net lines without callbacks, allocation,
      or order changes. Tests passed 202/202; DX12 passed with zero errors and
      matching baselines; 61.89 s stress was crash-free; the single 474.85 s
      mega passed with one process/generation and no golden refresh.
- [x] **R4b — Canonical artifact bookkeeping serialization (ruled fix for
      finding R3-F1).** Owner rulings 2026-07-16: canonicalize at the
      serialization boundary — live engine state keeps its raw
      schedule-sensitive counters; the artifact stops recording them. This
      task, not R3 and not R5, owns the fix.
      (a) Field inventory: from the R3 disposition report's full binary diff
      (`replay-mass-reduction-r3-codec-disposition.md`), enumerate EVERY
      schedule-sensitive field family — topology-version values in RVPD and
      in RVIS packet headers, trajectory-store bookkeeping, inactive
      worker-bank rows, and reserve-growth counters — with file:line of each
      writer. The fix covers the complete R3-F1 set; fixing only
      `topologyVersion` is a failed task.
      (b) Canonicalization design: values whose only consumer contract is
      *matching* (topology versions pairing RVPD state to RVIS rows) are
      remapped to a dense deterministic sequence in first-publication order,
      applied consistently across all chunks of one artifact; values that are
      pure runtime telemetry (inactive bank rows, reserve-growth counters)
      are serialized as documented canonical constants. A survey of every
      reader proves consumers are value-agnostic (matching/consistency only)
      before any writer changes; any reader that depends on absolute values
      is a blocking finding for this task, not a silent accommodation.
      Hazard: the durable fingerprint comparison
      (`SB_REPLAY_VISUAL_COMPARE_HEADER( topologyVersion )`,
      `ReplayVisualPacket.h:927`) compares live packets against durable rows —
      the canonical mapping must apply identically to both sides or that
      header field's comparison semantics must be explicitly remap-aware;
      a comparison that silently stops checking the field is a failed task.
      (c) Versioning ruling: decide against the authored-schema policy
      whether canonical bookkeeping is a semantic format change. If yes:
      bump the replay format version, add the deterministic migration step,
      and extend legacy/current/future/writer tests in the same commit. If
      no: record the ruling and why (layout and consumer contracts
      unchanged; only unvalidated telemetry values become constant).
      (d) Determinism proof: this task's charter pre-authorizes exactly TWO
      mega-gate invocations (the standard one plus one owner-approved
      determinism-proof run, approval recorded in MASTER's campaign section
      on 2026-07-16) — whole-file SHA-256 of the two artifacts must now be
      identical, closing R3-F1. The gate-covered projection must still match
      R3's recorded projection SHA (no behavioral drift), and MANI's
      `visualPredictionHash` becomes stable as a consequence.
      Gates: `validate_tests`, the two authorized mega-gate runs with
      whole-artifact SHA equality, no golden/baseline/provenance change.
      Sequencing note: R5 completed before this ruled task was registered;
      R4b therefore runs next, before R6.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r4b-artifact-bookkeeping-determinism.md`
      inventories every writer/reader and records the no-version-bump ruling.
      Live counters remain raw; RVPD/RVIS encode dense topology/record tokens,
      zero reserve telemetry, and a content-sensitive canonical semantic hash.
      The owner approved one additional
      same-tip invocation after the first run's zero sentinel was correctly
      rejected, for three total. Final artifacts B/C and both canonicalized
      R3 originals and the review-fix encoder artifact are 36,564,003 bytes
      with whole-file SHA
      `F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24`;
      all retain gate-covered SHA
      `363841634C50DB68D0C5CCF582A8BC07EFFC7FFED8DBF69D4444C6B3127DA2FA`.
      The R7 independent review reopened and repaired the initial fixed
      semantic sentinel. Tests now pass 203/203; the 433.34 s remediation mega
      passed one process/generation with unchanged 2,401/200/187/199 results.
- [x] **R5 — Dead-path audit and owner deletion rulings.** Mechanical
      reachability pass over the replay surface (public functions with zero
      call sites outside tests, config branches no scene/CLI can reach,
      superseded pre-V2 artifact paths *not* covered by the versioned-
      migration policy's legacy window). Every candidate gets a row: evidence,
      migration-policy check, owner ruling KEEP or DELETE. Only DELETE rows
      are removed, each with its ruling cited in the commit. No ruling, no
      deletion — this task may legitimately delete little; the census stays
      honest either way.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r5-dead-path-audit.md`
      records all 17 graph candidates individually: two zero-caller accessors
      DELETE, 13 production paths KEEP, and two test seams KEEP. Configuration,
      CLI, v2-v4 artifact, and internal snapshot migration branches remain
      reachable/supported. Profile/tests passed and the single 472.76 s mega
      passed unchanged; no golden/schema/baseline change.
- [x] **R6 — Oversized-TU partition pass.** Remaining TUs over ~2,000 lines
      (`ReplayPrediction.cpp` 4,424 is the primary target; `ReplayRecorder`
      as post-R3 size dictates) split into cohesive owner partitions
      (`ReplayPrediction.Simulation.cpp` / `.Trails.cpp` style — same owner,
      rule 5), or the closure review records why the remaining mass is one
      cohesive concern. No logic edits in this task — moves only, verified by
      the mega gate.
      Evidence: `Agentic/Reports/2026-07-16/replay-mass-reduction-r6-cohesion-rulings.md`
      records KEEP-cohesive rulings for `ReplayPrediction.cpp` (4,424 lines),
      `ReplayRecorder.cpp` (3,488), `ReplayV2Artifact.cpp` (2,913), and
      `ReplayPredictionDrawing.cpp` (2,061). Every physical split would need a
      new internal API, duplicate a byte/order contract, or become a cosmetic
      include fragment. No source/project/build change occurred. Tests passed
      202/202 and the sole 431.54 s mega passed unchanged.
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
- R0 owner decisions are resolved: configuration-appropriate diagnostics
  membership (Automation+Debug fingerprint, Automation RVPD verifier, Debug
  legacy probe blocks) and no probe entry points or no-op stubs in
  Release/Profile.
- The deferred SoA/SIMD lane is unaffected; nothing here touches physics or
  the FP envelope.
- **R3-F1 ruled follow-up is closed by R4b:** R3 correctly recorded the
  wall-clock-sensitive topology/store/inactive-bank/reserve fields without
  changing them. The owner then added R4b to this campaign and grew the ledger
  denominator 8 -> 9. R4b canonicalizes those values only at serialization;
  the independent R7 review additionally required the durable semantic hash to
  remain content-sensitive. The final original-pair and encoder byte proof is
  recorded in the R4b report.

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

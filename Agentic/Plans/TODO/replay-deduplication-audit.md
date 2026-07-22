# Replay Deduplication Audit

Date: 2026-07-22
Owner: skullbonez
State: RD1 complete; RD2 implementation active
Ledger tasks: 4 (RD0-RD3)

## Framing (Owner Statement, 2026-07-22)

The replay system is the most important part of this engine. Its size
(36,332 lines across 64 files in `SkullbonezSource/Runtime/Replay/` at main
tip 0c5263e1) is justified spend for the engine's core product — this plan is
an internal-quality pass, not a slimming exercise. The question is not "is
replay too big" but "is any of it redundant": duplicated logic inside the
most important subsystem is where its correctness risk concentrates.

## Problem And Evidence (2026-07-22)

- `replay-subsystem-consolidation` (closed RC0-RC6 on 2026-07-22) assigned
  six named domains and cut production include edges 48 → 33, and its RC3
  census explicitly found "duplicated selection logic" in Presentation that
  the campaign consolidated. That census thread has not been re-run across
  the other five domains.
- The largest TUs in the repository remain replay files (`ReplayRecorder.cpp`
  3,166; `ReplayV2Artifact.cpp` 2,737; `ReplayPrediction.cpp` 1,940;
  `ReplayValidation.cpp` + `ReplayValidation.Probes.cpp` ~3,600 combined),
  plus eleven `*Packets.h` value-packet headers whose field overlap has never
  been audited as a set.
- Prior campaigns (`replay-mass-reduction` R3-R6, `replay-subsystem-
  consolidation` RC tasks) recorded individual cohesion rulings. Those
  rulings are re-litigated only with new evidence; producing that evidence,
  or confirming the rulings hold, is precisely this audit's job.

## Goal

A completed, committed duplication census across all six replay domains
(Capture, ArtifactIO, Prediction, Presentation, Validation, Coordination
per the RC closure naming), owner rulings on every confirmed finding, and
implementation of the rulings that select deduplication — with replay
behavior frozen throughout.

## Non-Goals

- No behavior change: artifacts, schemas, golden manifests, probe output,
  tick counts, and presentation output are all frozen. Divergence is
  reverted, never normalized.
- No ownership re-decomposition: the six RC domain boundaries stand.
- No line quotas and no size-driven deletion; only duplication confirmed by
  the census and ruled by the owner is actionable.
- No artifact-format or probe-output-schema changes (Python gate tooling
  parses those schemas).
- No reserve-allocator registration changes: the three-owner inventory moves
  unchanged in owner, phase, cap, and counter.

## Phases

- [x] RD0 — Duplication census. Sweep all 64 files for: repeated selection /
  filtering / interpolation logic across Prediction and Presentation;
  parallel serialization or hashing mechanics across Recorder, V2Artifact,
  and fingerprint code; overlapping value-packet shapes across the
  `*Packets.h` set; and parallel probe/assertion scaffolding between
  `ReplayValidation.cpp` and `ReplayValidation.Probes.cpp`. Each candidate
  records file:line evidence, the prior ruling covering it (if any), and
  whether the finding is new evidence against that ruling. Documentation
  deliverable committed in this plan; no source change.
- [x] RD1 — Owner rulings. Present the census; the owner rules each
  confirmed duplicate as dedup-now, cohesion-retain (with recorded reason),
  or defer. Rulings that contradict a prior R-/RC-era ruling name the new
  evidence explicitly. Documentation-only task.
- [ ] RD2 — Implementation of dedup-now rulings. One logical consolidation
  per commit; byte-frozen behavior proven per commit by the plan's gates.
  If RD1 yields zero dedup-now rulings, RD2 closes as a recorded no-op and
  the plan's result is a certified-clean census.
- [ ] RD3 — Closure. Independent rubber-duck review over the census, the
  rulings, and every RD2 diff, answering the two standing replay review
  questions (no downward include appeared; no growth privilege appeared or
  expanded outside the inventory) plus this plan's own question: does any
  census candidate remain unruled. Final gates below.

## RD0 Evidence (2026-07-22)

The exhaustive census is recorded in
[`replay-deduplication-rd0-census`](../../Reports/2026-07-22/replay-deduplication-rd0-census.md).
It reconciles the current 64-file / 36,475-line tree, covers the intended eleven
value-packet headers despite the current eight-file `*Packets.h` suffix count,
and leaves seven candidate IDs (C1-C7) for owner disposition in RD1. Five are
direct copied mechanics; two are overlapping values with documented separate
lifetimes. Prior codec/hash/Validation cohesion rulings remain binding where no
new evidence appeared.

## RD1 Owner Rulings (2026-07-22)

The owner authorized the RD0 recommendations with the condition that all
Replay tests pass before closure:

| Candidate | Ruling | Binding reason |
|---|---|---|
| C1 | `dedup-now` | Timing, budget, reveal, and refresh-window mechanics must have one Prediction-owned implementation. |
| C2 | `dedup-now` | Capacity/reserve accounting must have one Prediction-owned implementation without changing owner, cap, phase, or counters. |
| C3 | `dedup-now` | Stable-id/model-row lookup and pose leaves must be shared value operations, not file-local copies. |
| C4 | `dedup-now` | Affected-body filtering/trail derivation is the same algorithm and may differ only at its Prediction/Presentation callers. |
| C5 | `dedup-now` | Overlay values must compose the canonical Presentation selection/state rather than flattening it again. |
| C6 | `cohesion-retain` | The immutable cursor deliberately crosses the Presentation-to-Prediction boundary without borrowing mutable path state. |
| C7 | `cohesion-retain` | Baseline comparison poses and retained visual markers have different lifetimes and archive rows. |

No candidate is deferred. C1-C5 are the complete RD2 implementation scope;
C6-C7 remain documented intentional value overlap.

RD1's sole `tools\validate_replay_visual_fidelity.bat` invocation passed in
430.23 seconds: one engine process, one prediction generation, 2,401 ticks,
17/17 typed cases (75 assertions), every negative/determinism control, and no
golden refresh.

## RD2 Implementation Evidence (2026-07-22)

### C1 — Prediction timing, budget, reveal, and refresh policy

- Centralized all seven C1 operations in the existing
  `ReplayPredictionSchedulingOperations` owner; Prediction orchestration,
  topology publication, and drawing now call that implementation. The stricter
  refresh precondition (`valid requested id`, same target, and at least two
  committed frames) is preserved.
- Deleted the file-local copies from `ReplayPrediction.cpp`,
  `ReplayPredictionPublication.cpp`,
  `ReplayPredictionTopologyPublication.cpp`, and
  `ReplayPredictionDrawing.cpp`. A definition census finds exactly one body for
  each operation, all in `ReplayPredictionScheduling.cpp`.
- Touched-source comment audit: 6 / 6 source-bearing files checked against the
  comment-style guide, 0 deferred. The scheduling owner documents the shared
  clock units, accounting order, reveal monotonicity, refresh invariant, and
  state mutation boundary; deletion-only callers retain compliant learning
  headers.
- Focused final Profile solution build passed in 3.56 seconds with zero
  warnings/errors. The direct Replay doctest filter passed in 1.87 seconds:
  52 / 52 cases and 786 / 786 assertions.
- Final `tools\validate_format.bat` passed in 13.37 seconds. Final
  `tools\validate_tests.bat` passed in 11.34 seconds: 345 / 345 cases,
  68,702 / 68,702 assertions, zero warnings/errors.
- C1's sole `tools\validate_replay_visual_fidelity.bat` invocation passed in
  442.68 seconds: one engine process, one prediction generation and
  presentation, 2,401 ticks, 17 / 17 typed controls (75 assertions), every
  negative/determinism control, and no golden refresh.
- Replay/downward-include and dependency-direction proofs return zero rows.
  No reserve registration, allocation-policy file, owner, phase, cap, or
  counter changed.

### C2 — Prediction capacity, accounting, and reserve policy

- Centralized overflow-checked capacity bytes, vector/frame-payload accounting,
  world-snapshot/frame-category/engine memory estimates, debug-contact growth
  rounding, vector reserve, and batched frame-payload reserve in the existing
  `ReplayPredictionReserveOperations` owner. Prediction orchestration and
  Publication call the same operations; Topology's dead capacity constants and
  Publication's dead copied implementation are deleted.
- The `replay_prediction_working_set` registration descriptor is byte-equivalent
  to its parent: owner, Replay phase, 256 MiB hard cap, unbounded counted-growth
  setting, and reason are unchanged. The allocation allowlist only relocates
  the two existing `.reserve()` sites to `ReplayPredictionReserve.h`; it grants
  no new API or owner privilege.
- Touched-source comment audit: 5 / 5 source-bearing files checked, 0 deferred.
  The reserve owner documents byte units, scope ordering, shared-cap behavior,
  batched frame payloads, denial behavior, and accounting ownership; the three
  deletion-only callers retain compliant learning headers.
- Focused Profile solution build passed in 10.86 seconds with zero errors; the
  direct Replay doctest filter passed 52 / 52 cases and 786 / 786 assertions.
- Allocation-policy self-test and 429-file repository scan passed in 9.20
  seconds with zero allowlist errors. `tools\validate_fast.bat` passed in 44.73
  seconds with clean format/metadata and zero-warning Profile/Debug builds.
- `tools\validate_replay_allocation_policy.bat` passed in 15.98 seconds:
  exactly two prediction generations through frame 180, zero gameplay
  allocations, and zero reserve-policy violations.
- C2's sole `tools\validate_replay_visual_fidelity.bat` invocation passed in
  430.84 seconds: one process/generation/presentation, 2,401 ticks, 17 / 17
  typed controls (75 assertions), every negative/determinism control, and no
  golden refresh.
- Replay/downward-include and dependency-direction proofs return zero rows.

## Dependencies And Decisions

- Fourth in the round-2 campaign binding order (heaviest per-task gate lands
  after the cheaper structural plans, mirroring the prior campaign's
  sequencing rationale).
- `runtime-renderer-decomposition` RR2 moves consequence-grade state into
  the Presentation domain; RD0's census runs after RR2 lands so it sweeps
  the final shape. If RR2 is deferred, RD0 proceeds and notes the pending
  seam.
- Golden-manifest refresh is not authorized by anything in this plan
  (inventory rule 11).

## Acceptance

- Census covers all six domains and all 64 files with per-candidate evidence
  and ruling links; zero unruled candidates at RD3.
- Every RD2 commit shows byte-identical artifact/schema/golden behavior via
  its gates.
- Independent review is clear on both standing replay questions and census
  completeness.
- Every Replay test lane defined below passes at RD3; no historical scrub alias
  is invoked separately from its authoritative visual-fidelity owner.

## Validation

- Every task, including the documentation-only RD0/RD1, runs
  `tools\validate_replay_visual_fidelity.bat` exactly once before it may be
  checked or committed — one engine process, one prediction generation, zero
  golden refresh (inventory rule 11 verbatim). RD0/RD1 satisfy this with the
  gate run proving the tree they described is the tree that passes.
- RD2 commits additionally run `tools\validate_tests.bat` when
  `TestReplay*` coverage moves, and the normal mapped gate for any
  non-replay file touched.
- RD3 closure: `tools\validate_full.bat` plus the strict allocation proof
  used by RC closure (three registered owners, zero policy violations).
- Owner condition for RD3: run `tools\validate_tests.bat` (including every
  `TestReplay*` case), `tools\validate_replay_v2_artifact.bat`,
  `tools\validate_replay_allocation_policy.bat`, the task's one permitted
  `tools\validate_replay_visual_fidelity.bat`, and `tools\validate_full.bat`.
  `tools\validate_replay_scrub.bat` is only a delegating alias for visual
  fidelity and is not a second test invocation.

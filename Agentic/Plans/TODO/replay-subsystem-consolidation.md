# Replay Subsystem Consolidation — Right-Size The Largest Domain

Date: 2026-07-22
Status: Active — 6/7 phases complete
Impact area: `Runtime/Replay/*` interior structure and public surface; no
boundary, retention, or feature changes
Owner: replay

## Problem And Evidence (2026-07-22 census)

Replay is behaviorally frozen and boundary-clean (containment and policy
plans closed 2026-07-21) but it is the largest single domain in the engine
and its interior domains interleave:

- `Runtime/Replay/` is 34,735 lines across 44 files — larger than the
  entire Physics module (28,025) and ~16% of engine source.
- The two largest translation units in the repository live here:
  `ReplayPrediction.cpp` (4,488 lines) and `ReplayRecorder.cpp` (3,617),
  with `ReplayV2Artifact.cpp` (2,940), `ReplayPredictionDrawing.cpp`
  (2,084), `ReplayValidation.cpp` (1,936), `ReplayValidation.Probes.cpp`
  (1,884), and `ReplayScrubberTools.cpp` (1,815) behind them.
- Prediction already sheds `Scheduling`/`Reserve`/`Drawing` satellites yet
  the core TU still mixes worker scheduling, isolated-future simulation,
  and the publication protocol. The recorder mixes live capture with
  artifact serialization concerns that `ReplayV2Artifact` also owns half
  of. Presentation is spread across four files
  (`ReplayPresentation`, `ReplayPredictionDrawing`, `ReplayOverlayRenderer`,
  `ReplayOverlayLayout`) without a recorded split rule.

Per the god-object closure rule, this is judged as one logical surface: a
domain this large must either decompose into named owners or record why its
state belongs together. Today it does neither completely.

## Goal

Replay reads as a subsystem: one small public surface (`ReplayRuntime` plus
typed value packets), and an interior of six named domains with recorded
ownership — Capture, Timeline, Prediction, ArtifactIO, Presentation,
Validation. No TU exceeds ~2,000 lines unless the closing review records a
concrete cohesion justification for it. The reserve-allocator privilege
inventory (exactly three registered owners) is unchanged or reduced.

## Frozen-Behavior Contract

This is a refactor under inventory rule 11: every task runs the
one-process, one-generation
`tools\validate_replay_visual_fidelity.bat` mega command exactly once; the
golden manifest is never refreshed; artifacts remain byte-identical. Any
behavioral drift is a defect in the task, never a baseline update.

## Non-Goals

- No feature work, no new prediction capability, no retention-policy or
  capacity changes, no allocator registration changes.
- No boundary changes: zero-inbound rule and the `PhysicsApi`-only physics
  access stand as closed.
- Validation/probe *size* is out of scope by owner direction; RC5 concerns
  placement only.
- No second prediction generation, ever, in any task's validation.

## Phases

- [x] RC0. Census and domain map. Inventory every replay file with line
  counts, the concrete responsibilities inside each oversized TU, and every
  non-replay file that includes a replay header (the public-surface list —
  expected: `ReplayRuntime.h` and value packets only; each violation is
  listed with its consumer and target packet). Record the six-domain
  assignment for all 44 files. Documentation-only.
- [x] RC1. Recorder split. Separate live capture (ring ownership, tick
  sampling, retained memory) from artifact serialization; serialization
  authority consolidates with `ReplayV2Artifact` into the ArtifactIO
  domain, which alone owns format versioning and file IO. No public-surface
  change.
- [x] RC2. Prediction core split. Divide `ReplayPrediction.cpp` along its
  three responsibilities: worker scheduling + cancellation, isolated-future
  simulation slice, and the release/acquire publication protocol. The
  publication protocol invariants (published-prefix, cancellation waits for
  in-flight slice) each land in exactly one owner with their existing
  focused tests still passing.
- [x] RC3. Presentation consolidation. One Presentation domain with a
  recorded two-sided split: presentation *data selection* (what to show for
  a scrub position/cause tree) versus *draw submission* (value packets to
  the render seam). Fold `ReplayOverlayLayout`/`ReplayOverlayRenderer`/
  `ReplayPredictionDrawing`/`ReplayPresentation` into that shape; delete
  duplicate selection logic found by RC0.
- [x] RC4. Header diet. Public headers stop carrying interior types; move
  internal structs to domain-internal headers; re-verify the public-surface
  list from RC0 shrank to `ReplayRuntime` + packets, or record each
  survivor with owner/reason/deletion condition.
- [x] RC5. Validation placement. Confirm `ReplayValidation*` probe code
  sits wholly in the Validation domain behind its automation gating with no
  include edge into Capture/Prediction hot paths; fix any edge found. Size
  is not judged.
- [ ] RC6. Closure. Rerun the RC0 census from final source (line counts,
  domain table, public-surface list, any recorded cohesion justifications).
  Comment-style audit of touched files. One independent rubber-duck review
  of the whole consolidation (single review per the migration-cleanup rule,
  not per slice). Final broad gate.

## Dependencies And Decisions

- Sequenced after `owner-fanout-reduction` so the presentation-reset seam
  migrates onto the final lifecycle-ledger shape once, not twice.
- Decision for RC0 to record: whether `TrajectoryStore` belongs to
  Prediction or Timeline; the census evidence decides, not convenience.
- Any TU kept above the size target requires the closing review to record
  why its state and invariants belong to one owner (god-object rule); an
  unjustified survivor blocks RC6.

## Acceptance

- Six named domains with every replay file assigned; public surface is
  `ReplayRuntime` + typed value packets, or explicit recorded exceptions.
- No TU above ~2,000 lines without a recorded cohesion justification
  accepted by the independent review.
- Reserve-allocator inventory: exactly the existing three owners (or
  fewer), with caps, phase gates, and counters unchanged.
- Every task's fidelity run: one engine process, one generation, all
  positive/negative controls passing, zero golden refresh.

## Validation

Every task touching `Runtime/Replay/*` runs its mapped gate plus one
`tools\validate_replay_visual_fidelity.bat` invocation (rule 11). Tasks
that change `SkullbonezTests/TestReplay*` run `tools\validate_tests.bat`
first. RC6 runs `tools\validate_full.bat` from the closure tip. Strict
two-generation replay allocation evidence is re-collected at RC6 only if
any allocation-adjacent file moved.

## RC0 Evidence — Census And Domain Map (2026-07-22)

- The final-source census is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc0-census.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc0-census.md):
  44 tracked files / 34,768 lines assigned exactly once across Capture,
  Timeline, Prediction, ArtifactIO, Presentation, and Validation.
- Four translation units exceed the ~2,000-line target:
  `ReplayPrediction.cpp` 4,488, `ReplayRecorder.cpp` 3,617,
  `ReplayV2Artifact.cpp` 2,940, and `ReplayPredictionDrawing.cpp` 2,084. The
  census records their concrete responsibility splits and owning phases.
- The production public-surface baseline is 48 include edges from 25 files to
  17 Replay headers. Five are provisional public owner/value headers; twelve
  are interior leaks with consumer and target packet/API recorded for RC4.
  Ten test files contribute 14 separately recorded white-box edges.
- `TrajectoryStore` is assigned to Prediction: `ReplayPrediction` exclusively
  mutates it, its prefix protocol is Prediction-owned, and all growth uses
  `replay_prediction_working_set`; Timeline only supplies immutable samples.
- The three-owner reserve inventory is unchanged: 32 MiB recorder, 8 MiB solver
  snapshot, and 256 MiB prediction working set, all Replay-phase with existing
  high-water counters and exhaustion rules.
- Documentation-only. No repository validation was required or run. OF2's
  provenance-only replay mismatch remains a non-stopping blocker and authorizes
  no config or golden change.

## RC1 Evidence — Capture / ArtifactIO Split (2026-07-22)

- Full evidence is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc1-artifactio.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc1-artifactio.md).
  Capture no longer owns file streams, file formatting, flush operations, or
  public chronological-copy commands.
- `ReplayArtifactSource` owns cold compact-ring reconstruction inside the
  governed `ReplayV2Artifact.cpp` unit; `ReplayArtifactHashLog` owns both paired
  hash-log streams and their stable CSV ABI. `ReplayTimeline` sequences only
  completed sample values across the boundary.
- Product public surface and the three-owner reserve inventory are unchanged.
  A proposed standalone materialization source was rejected by the allocation
  checker and moved under the existing cold ArtifactIO policy without adding an
  allowlist row or growth privilege.
- Focused build passes in 8.3 s; focused Replay tests pass 10 / 180.
  `tools\validate_tests.bat` passes 343 / 68,693 in 3.7 s. Final
  `tools\validate_full.bat` passes in 167.4 s with all CPU/coverage and five
  runtime lanes, zero DX12 errors, accepted images, and byte-exact physics.
- The single 423.6 s visual-fidelity invocation proves one process/generation
  and passes 16 / 72 typed controls, then stops at the unchanged config
  provenance mismatch. No retry or golden/config edit occurred.
- Comment audit covers 11 touched source-bearing files, zero deferred. Static
  dependency, Replay-boundary, allocation, callback/context, heap, and exception
  proofs pass.

## RC2 Evidence — Prediction Core Split (2026-07-22)

- Full evidence is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc2-prediction.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc2-prediction.md).
  Worker lifetime/cancellation, isolated simulation, and release/acquire
  publication now have named owners.
- `ReplayPrediction.cpp` fell from 4,488 to 2,083 lines; the new publication,
  topology-publication, and scheduling units are 1,390, 1,653, and 117 lines.
  RC6 retains review authority over the near-target core survivor.
- The existing three reserve registrations and caps are unchanged. Moved
  allowlist rows keep the same prediction owner/cap; the final allocation scan
  covers 414 files with zero errors. RC6 must recollect strict two-generation
  evidence because allocation-adjacent calls moved physically.
- `validate_tests` passes 344 / 68,699; final `validate_fast` passes in 62.1 s;
  final `validate_full` passes in 108.2 s with all runtime lanes and byte-exact
  physics. Comment audit covers nine touched source-bearing files, zero deferred.
- RC2's single 422.9 s mega-gate invocation proves one process/generation and
  passes 16 / 72 controls, then reaches the unchanged config-provenance blocker.
  It was not retried and no config/golden edit occurred.

## RC3 Evidence — Presentation Consolidation (2026-07-22)

- Full evidence is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc3-presentation.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc3-presentation.md).
  `ReplayRuntime::BuildPresentationSelection()` is the single data-selection
  answer consumed by overlay composition and render-pose application.
- Draw submission is value-only across `ReplayPredictionDrawing`, the new
  `ReplayCauseFocusSubmission`, overlay renderer, and overlay layout.
  `ReplayPredictionDrawing.cpp` fell from 2,084 to 1,752 lines.
- Future-tree publication readiness now has one Prediction-owned predicate;
  drawing retains only the stable-root mismatch guard.
- `validate_fast` passes in 57.9 s and `validate_full` passes in 110.9 s with
  zero DX12 errors and byte-exact physics. Comment audit checks 12 files with
  zero deferred; allocation policy scans 416 files with zero errors.
- RC3's single 418.7 s mega invocation proves one process/generation and passes
  16 / 72 controls, then reaches the unchanged config-provenance blocker. It
  was not retried and no config/golden edit occurred.

## RC4 Evidence — Header Diet (2026-07-22)

- Full evidence is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc4-header-diet.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc4-header-diet.md).
  The production Replay include surface fell from 48 to 33 edges (31.25%).
- Ten typed packet/surface headers replace direct ownership-header exposure.
  Twenty-six final edges target ReplayRuntime or bounded values/commands; seven
  concrete Presentation/Diagnostics/Validation survivors have an owner, reason,
  deletion condition, and review evidence.
- ReplayRuntime's inline concrete-owner includes remain an explicit composition
  exception: fixed allocation-free owner lifetime without PImpl heap, callback
  pack, context bag, or mutable owner accessors.
- Focused tests pass 344 / 68,699; `validate_fast` passes in 62.9 s and
  `validate_full` passes in 109.5 s with zero DX12 errors and byte-exact
  physics. Comment audit checks 39 files with zero deferred; allocation policy
  scans 426 files with zero errors.
- RC4's single 422.5 s mega invocation proves one process/generation and passes
  16 / 72 controls, then reaches the unchanged config-provenance blocker. It
  was not retried and no config/golden edit occurred.

## RC5 Evidence — Validation Placement (2026-07-22)

- Full evidence is recorded in
  [`../../Reports/2026-07-22/replay-subsystem-consolidation-rc5-validation-placement.md`](../../Reports/2026-07-22/replay-subsystem-consolidation-rc5-validation-placement.md).
- Debug CLI probes are wholly in `ReplayValidation.Probes.cpp`, which the
  production project builds only for Debug. Interaction report, offline
  archive verification, and fingerprint code are restricted to Automation or
  Debug/Automation as appropriate.
- Capture, Timeline, Prediction, and ReplayRuntime have zero include edges to
  ReplayValidation headers. `ReplayValidation.cpp` remains all-configuration
  product code because it owns transactional event replay, artifact load,
  target restore, hash verification, and rollback rather than probe bodies.
- RC5 is documentation-only; no mapped repository gate or source comment audit
  was required. Its single 407.4 s mega invocation proves one
  process/generation and passes 16 / 72 controls, then reaches the unchanged
  config-provenance blocker. It was not retried and no config/golden edit
  occurred.

# Replay Subsystem Consolidation — Right-Size The Largest Domain

Date: 2026-07-22
Status: Registered — 0/7 phases complete
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

- [ ] RC0. Census and domain map. Inventory every replay file with line
  counts, the concrete responsibilities inside each oversized TU, and every
  non-replay file that includes a replay header (the public-surface list —
  expected: `ReplayRuntime.h` and value packets only; each violation is
  listed with its consumer and target packet). Record the six-domain
  assignment for all 44 files. Documentation-only.
- [ ] RC1. Recorder split. Separate live capture (ring ownership, tick
  sampling, retained memory) from artifact serialization; serialization
  authority consolidates with `ReplayV2Artifact` into the ArtifactIO
  domain, which alone owns format versioning and file IO. No public-surface
  change.
- [ ] RC2. Prediction core split. Divide `ReplayPrediction.cpp` along its
  three responsibilities: worker scheduling + cancellation, isolated-future
  simulation slice, and the release/acquire publication protocol. The
  publication protocol invariants (published-prefix, cancellation waits for
  in-flight slice) each land in exactly one owner with their existing
  focused tests still passing.
- [ ] RC3. Presentation consolidation. One Presentation domain with a
  recorded two-sided split: presentation *data selection* (what to show for
  a scrub position/cause tree) versus *draw submission* (value packets to
  the render seam). Fold `ReplayOverlayLayout`/`ReplayOverlayRenderer`/
  `ReplayPredictionDrawing`/`ReplayPresentation` into that shape; delete
  duplicate selection logic found by RC0.
- [ ] RC4. Header diet. Public headers stop carrying interior types; move
  internal structs to domain-internal headers; re-verify the public-surface
  list from RC0 shrank to `ReplayRuntime` + packets, or record each
  survivor with owner/reason/deletion condition.
- [ ] RC5. Validation placement. Confirm `ReplayValidation*` probe code
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

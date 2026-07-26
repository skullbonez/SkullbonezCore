# Nightrunner 26 July

Date: 2026-07-26
Status: ACTIVE — owner-directed three-task corrective and style campaign
Impact area: Runtime Replay/Prediction, replay tests, C++ formatting policy,
repository documentation
Owner: replay + developer experience
Priority: High — replay scrub and velocity editing are visible frame-quality
defects; the style rules are an explicit owner direction

## Owner Specification

The work is split into three separate categories and lands in this order.

1. **Replay scrub performance.** Historical solver playback must not repeatedly
   reconstruct the same dense restore snapshot or copy all hidden solver vectors
   for every render, overlay, UI, and pause-state query. Same-position reads are
   cached, availability checks are cheap, and render-facing work shares or uses
   the narrowest honest sample. Restore/export paths retain complete solver
   state.
2. **Code style.** Assertions and precondition checks sit at the top of a
   function where practical, followed by a blank line. Conditions and loops
   have a blank line before and after their blocks. Comments always have a blank
   line above them; a blank line below is optional. One to three short
   parameters stay on one line. Multiline parameters are reserved for four or
   more parameters or lines near the 125-character soft limit. The first
   parameter stays beside the opening parenthesis and continuation parameters
   align beneath it. Pointer/reference and complex-type parameters precede
   primitive values unless ownership, ABI, or call-flow clarity gives a
   concrete reason otherwise.
3. **Space-scene velocity editing.** Held dragging updates only the selected
   object's provisional path. It does not restart complete future prediction
   for every pointer sample, and other paths remain stable. Mouse release
   commits the latest velocity and requests exactly one authoritative full
   prediction. The provisional line remains visible until that result replaces
   it.

## Goal

Replay inspection remains responsive under solver-heavy captures, code layout
matches the owner's readable-width and whitespace preferences, and velocity
editing gives stable real-time target feedback without prediction-generation
flicker.

## Non-Goals

- No replay, physics, screenshot, or performance baseline refresh.
- No change to deterministic restore contents or replay artifact schemas.
- No repository-wide semantic reordering of assertion checks or public
  parameter lists. Those judgment rules apply to new and meaningfully touched
  code; the formatter owns only mechanical layout.
- No approximate paths for non-selected bodies during a velocity drag.
- No new context/service bag, callback pack, compatibility alias, or upward
  dependency edge.

## Tasks

- [x] **N26-1 — Eliminate replay scrub reconstruction spikes.**
  Memoize solver resolution by stable retained offset plus recorder content
  revision; invalidate on every content mutation. Make scrub-state availability
  checks metadata-only. Reuse one selected solver result inside frame selection
  and reconstruct complete world state directly into its destination instead of
  copying the 25-vector scratch snapshot a second time. If render/UI consumers
  can use an existing narrow presentation boundary without restore authority,
  route them through it; otherwise record why the cached dense result is the
  lowest-risk boundary. Add focused regression evidence that repeated
  same-offset queries perform one reconstruction and mutations invalidate the
  cache.

- [x] **N26-2 — Ratify and apply the owner code style.**
  Add an authoritative code-style reference, wire it into contributor
  instructions, update `.clang-format` and the repository post-pass for the
  125-column/compact-parameter/comment/control-flow rules that are mechanical,
  and extend formatter self-tests. Apply the formatting pipeline so the
  mandatory format gate sees one stable repository state. Assert placement and
  semantic parameter ordering remain review rules and are applied to files
  meaningfully changed by N26-1/N26-3.

- [ ] **N26-3 — Publish a stable selected-body velocity-drag preview.**
  Capture the selected body's retained path at drag start, update only its
  bounded provisional points from the accepted velocity delta while held, and
  keep all committed non-selected paths unchanged. Remove per-pointer full
  prediction refresh. On release, publish one newest-state prediction request
  and retain the provisional path until the replacement generation is ready.
  Cover drag coalescing, one-release/one-generation behavior, and preview
  lifetime with focused tests.

## Dependencies And Decisions

- N26-1 lands first because the owner named it as the first task on the branch.
- N26-2 follows so N26-3 is authored under the ratified layout.
- N26-3 may reuse the existing velocity-mutation baseline polyline, but that
  baseline must remain comparison evidence; preview authority must be explicit
  and fixed-capacity.
- Replay retains the only post-gameplay growth privilege. This plan adds no
  reserve owner, raises no cap, and weakens no phase gate.
- Any source comment asserting newest-state held-drag prediction is updated in
  the same task because N26-3 intentionally replaces that policy.

## Acceptance

- A repeated solver scrub query at one retained offset performs one dense
  reconstruction until recorder content changes.
- `IsScrubPaused` and equivalent availability paths never decode a solver
  snapshot.
- The final full snapshot is not copied from a separate world scratch buffer.
- Formatter self-tests and the repository format gate enforce the mechanical
  owner rules without leaving an empty `(` line.
- Held velocity drag changes one provisional target line continuously, leaves
  other paths stable, and starts no complete prediction generation.
- Release requests exactly one authoritative replacement and the provisional
  line has no visibility gap before replacement.
- No baseline, golden, schema, allocation-policy, or dependency-direction
  change.

## Validation

| Task | Required evidence |
|---|---|
| N26-1 | Focused Replay recorder/runtime doctests; replay scrub regression; allocation-policy scan; `tools\validate_perf.bat` |
| N26-2 | Formatter self-test; `tools\validate_format.bat`; `tools\validate_fast.bat` |
| N26-3 | Focused Replay/Prediction doctests; one `tools\validate_replay_visual_fidelity.bat` invocation; replay scrub regression; `tools\validate_full.bat`; `tools\validate_perf.bat` |

At plan closure, run one independent rubber-duck review over the complete
three-task diff, remediate every credible behavior, ownership, allocation,
formatting, and validation finding, then create permanent closure evidence under
`Agentic/Reports/2026-07-26/`.

## Execution Evidence

- N26-1: solver scrub resolution now caches one dense value by retained offset
  and recorder content revision; capture/reset/artifact iteration invalidate it.
  `IsScrubPaused()` uses recorder metadata, frame selection reuses the selected
  solver pointer, and world deltas reconstruct directly into the destination.
  The focused 49-assertion replay round-trip case, replay allocation-policy
  gate, and performance gate passed on 2026-07-26. The authoritative scrub
  alias is deferred to the final N26-3 source state so the expensive immutable
  visual oracle runs once against the complete campaign.
- N26-2: `Agentic/Reference/code-style-guide.md` records the owner rules;
  `.clang-format` and the two repository post-passes enforce the 125-column,
  compact one-to-three argument, first-argument-on-opening-line, control-flow,
  and comment spacing rules. Continued macros and conditional signatures have
  dedicated regression fixtures. Repository formatting is migrated, zero
  function/call lines end with an empty `(`, and both `validate_format.bat` and
  `validate_fast.bat` pass.

# Replay Monolith Decomposition — Owner Boundaries Inside The Replay Subsystem

Date: 2026-07-13
Status: Live — 0/9 tasks complete; blocked on `replay-visual-fidelity-mega-probe.md`
Branch: `nightrunner-13th-july`
Impact area: `SkullbonezSource/Runtime/Replay/*` (26,060 lines), the two
external consumers `Runtime/Run.cpp` and `Runtime/RunUiTextPass.cpp`, project
filters
Owner: replay subsystem

## Problem And Evidence (measured 2026-07-13 at the `nightrunner-12th-july` tip)

The replay subsystem is externally contained but internally undifferentiated:

- Only two files outside `Runtime/Replay/` reference `ReplayRuntime`
  (`Runtime/Run.cpp`, `Runtime/RunUiTextPass.cpp`) — the blast radius is
  small, which is what makes this decomposition cheap relative to the old
  runtime-shell work.
- `ReplayRuntime.h` is an everything-header: 1,587 lines, **84 class/struct
  types**, and a `ReplayRuntime` class with ~24 member fields spanning
  recording, scrub/restore, prediction, authoring, overlays, and probe state.
  Every tool TU includes it, so all state is reachable from everywhere and
  every edit recompiles the whole subsystem. This reachability is the
  mechanism by which the subsystem keeps accumulating mass.
- The `RunReplay*` translation units are a mechanical TU split, not
  decomposition (the pattern `AGENTS.md` explicitly rejects for god-object
  closure): `RunReplayTools.cpp` (4,965 lines), `RunReplayProbes.cpp` (3,053),
  `RunReplayScrubberTools.cpp` (1,128), `RunReplayVelocityEdit.cpp` (792),
  `RunReplayCauseTreeTools.cpp` (390), `RunReplayQueryTools.cpp` (306) —
  mostly free functions in the bare `SkullbonezCore` namespace plus a few
  `ReplayRuntime` methods, named as if `Run` still owns them.
- The header confesses the stalled seam itself (`ReplayRuntime.h:27`):
  "Runtime state: UI and tool state that belongs to replay but is still
  consumed by Run while the subsystem is being separated."

What is NOT broken and must not be redesigned: the prediction single-writer /
published-prefix protocol, the recorder ring/eviction design, the
`ReplayRuntimeOwnerViews.h` never-stored borrow-view idiom, and the artifact
V2 format. This plan moves them intact behind owners.

## Goal

`ReplayRuntime` becomes a thin composition root (the same closure `Run`
received): it constructs five concrete owners, sequences per-frame order
(record → scrub-or-predict → present), and holds no business state. Each owner
has its own header; tool TUs include only the slice they use; Run consumes a
per-frame value snapshot instead of `ReplayRuntime&`.

Target owner map (binding once M1 confirms it; adjust only with a recorded
reason):

| Owner | Absorbs | Today's mass |
|---|---|---|
| `ReplayTimeline` | `ReplayRecorder`, retention windows, `ReplayRetainedMemory`, eviction + memory policy | ~4k lines |
| `ReplayScrubber` | Scrub cursor state machine, `ReplayRestoreService`, sample/topology restore transactions, `RunReplayScrubberTools.cpp` | ~1.5k |
| `ReplayPrediction` | Private `PhysicsEngine`, `ReplayPredictionScheduling`, `ReplayPredictionReserve`, `TrajectoryStore`, publish-prefix protocol, the prediction half of `RunReplayTools.cpp` | ~3k |
| `ReplayAuthoring` | `RunReplayVelocityEdit.cpp`, branch provenance, `RunReplayCauseTreeTools.cpp` | ~1.2k |
| `ReplayPresentation` | `ReplayOverlayLayout/Renderer`, path/ribbon drawing, target markers — the visualization bulk of `RunReplayTools.cpp` | ~6k |

Cold artifact IO (`ReplayV2Artifact`) stays a standalone owner; probes become
consumers of published views (M7).

## Non-Goals

- No behavior change anywhere: byte-exact physics baselines, replay scrub
  probes, prediction determinism, and the frame-exact 200-box replay visual
  fidelity mega probe must pass unchanged after every task.
- No semantic redesign of the prediction protocol, recorder retention, or
  artifact format.
- No `ReplayContext`/`ReplayServices` bag, no `void*`, no callback packs, no
  friend access, no `Replay::*` forwarding wrappers that relay business
  operations while authority stays in `ReplayRuntime` — the god-object closure
  failures list in `AGENTS.md` applies verbatim to this subsystem.
- No line-count targets. Big cohesive owners are fine when the closure review
  records why their state belongs together.

## Tasks

**Non-negotiable per-task gate:** every task M0-M8, including documentation
inventory work, ends with `tools\validate_replay_visual_fidelity.bat`. Any
failure reopens the current task. A decomposition task may never refresh the
known-good golden manifest.

- [ ] **M0 — Prerequisite gate (no code).**
  `replay-visual-fidelity-mega-probe` is complete and both
  `tools\validate_replay_visual_fidelity.bat` and
  `tools\validate_replay_scrub.bat` pass on the exact starting tree. Record
  command output, runtime, baseline provenance, compared tick count, packet
  schema version, and hashes here. `adversarial-review-round-3` is already
  complete. Validation: `tools\validate_replay_visual_fidelity.bat`.
- [ ] **M1 — Type inventory and binding owner map.** Enumerate all 84 types in
  `ReplayRuntime.h` and every free function in the six `RunReplay*.cpp` TUs;
  assign each to one target owner (table above) in a checklist appended to
  this plan file. Checklist rules: one row per type/function; a row may be
  reassigned later only with a written reason; any type genuinely shared by
  3+ owners goes to a small `ReplayIdentity.h`-style value header
  (`ReplayBodyId`, `ModelRowHint`, sample POD structs) — values only, no
  state, no services. Acceptance: every row has an owner; no "misc" bucket
  exists. Validation, required by owner even for this documentation task:
  `tools\validate_replay_visual_fidelity.bat`.
- [ ] **M2 — Shatter the everything-header.** Create the five owner headers
  plus the shared value header per M1's map; move type definitions without
  editing their bodies; `ReplayRuntime.h` shrinks to the composition root
  declaration and owner includes. Update includes in every replay TU so each
  tool includes only its slice; `ReplayRuntimeOwnerViews.h` keeps working
  against the new headers. Mechanical only — no member moves yet, no renames.
  Acceptance: `ReplayRuntime.h` under ~300 lines; no replay TU includes an
  owner header it does not use (spot-check with the include list per TU
  recorded in the M1 checklist); zero warnings. Validation:
  `tools\validate_replay_visual_fidelity.bat`, then
  `tools\validate_full.bat` at the PR gate (`Runtime/*` mapping), plus
  `tools\validate_replay_scrub.bat`.
- [ ] **M3 — Extract `ReplayPresentation` (biggest, safest).** Move overlay
  layout/renderer, path visualizer, ribbon/marker drawing, and the
  visualization free functions of `RunReplayTools.cpp` into the owner: state
  that today lives in `ReplayRuntime` members (overlay/trajectory display
  state) moves into `ReplayPresentation`; the owner consumes timeline/
  prediction data through published views only (never-stored borrows). Free
  functions become owner methods or file-local statics in the owner's TU.
  Acceptance: no presentation state remains in `ReplayRuntime`; the
  presentation TU does not include prediction internals (only published
  views); prediction-determinism submitted-geometry fingerprint unchanged.
  Validation: `tools\validate_replay_visual_fidelity.bat` first,
  `tools\validate_replay_scrub.bat`, and `tools\validate_full.bat` at the PR
  gate. The legacy final fingerprint is supporting evidence only; the mega
  probe is the frame-by-frame presentation proof.
- [ ] **M4 — Extract `ReplayScrubber` and `ReplayTimeline`.** Timeline first
  (recorder + retention + memory policy are already nearly self-contained),
  then scrubber (cursor state machine + restore transactions +
  `RunReplayScrubberTools.cpp`). Restore paths keep cancelling prediction
  before mutating live authority (`ReplayInteractionController.cpp:20`
  invariant) — that call becomes an explicit owner-to-owner request, not a
  reach into `ReplayRuntime` fields. Acceptance: scrub/retained-restore
  SkullScope probes pass; solver-track hash-restore behavior unchanged;
  `ReplayRuntime` no longer holds cursor or retention members. Validation:
  `tools\validate_replay_visual_fidelity.bat` first,
  `tools\validate_replay_scrub.bat`, and `tools\validate_full.bat` at the PR
  gate.
- [ ] **M5 — Extract `ReplayAuthoring`.** Velocity edit, branch provenance,
  cause-tree tools move behind the owner; the velocity-edit "dirty
  prediction" side effect becomes an explicit request to `ReplayPrediction`
  (queued value command, consistent with the repo's one-frame command-packet
  idiom) rather than direct member writes. Acceptance: velocity-edit and
  branch-restore interaction scripts pass; no authoring state in
  `ReplayRuntime`. Validation: `tools\validate_replay_visual_fidelity.bat`,
  `tools\validate_replay_scrub.bat`,
  `tools\validate_interaction_clicks.bat` if the click scripts cover velocity
  edit, `tools\validate_full.bat` at the PR gate.
- [ ] **M6 — Extract `ReplayPrediction` (most invariant-laden, deliberately
  last).** Move the private engine, scheduling, reserve, trajectory store,
  seeding (`SeedReplayPredictionEngine`), capture, and worker publication
  intact. The three documented invariants move as API shape, not comments
  where possible: prediction never writes live stores (owner takes only
  const live views), single-writer stepping (worker task owned by the owner,
  cancellation waits for in-flight slices before clearing state), serial
  physics steps with post-step fan-out capture. Acceptance: prediction
  determinism fingerprint unchanged; **200-box visual fidelity gate green**
  for every predicted/live simulation tick and exact presentation byte;
  allocation-policy allowlist rows for replay reserve
  updated to name the new owner in the same commit. Validation:
  `tools\validate_replay_visual_fidelity.bat` first,
  `tools\validate_replay_scrub.bat`,
  `tools\validate_perf.bat` (prediction budget unchanged),
  `tools\validate_full.bat` at the PR gate.
- [ ] **M7 — Close the Run seam and decouple probes.** Define a small
  `ReplayHudStatus` value struct (published once per frame by the composition
  root) carrying exactly what `Run.cpp` and `RunUiTextPass.cpp` read today;
  those two files stop taking `ReplayRuntime&`. `RunReplayProbes.cpp` and
  `RunReplayQueryTools.cpp` convert to the published views/owner APIs — a
  probe reading owner internals is how the probe TU reached 3,053 lines;
  anything a probe needs that is not published becomes a deliberate published
  view with a comment, not a friend/back-door. Delete the "Runtime state"
  glossary confession from the (now thin) `ReplayRuntime.h`. Acceptance:
  `rg -l 'ReplayRuntime' SkullbonezSource --glob '!Runtime/Replay/**'`
  returns only the composition wiring in `Run.cpp` (construction/sequencing),
  and `RunUiTextPass.cpp` consumes only the value snapshot. Validation:
  `tools\validate_replay_visual_fidelity.bat` first,
  `tools\validate_replay_scrub.bat`, and `tools\validate_full.bat` at the PR
  gate.
- [ ] **M8 — Honest renames, comment audit, and mandatory closure review.**
  Rename `RunReplay*.cpp` to owner-named files (`ReplayPresentation.cpp`,
  `ReplayScrubberTools` content merged into its owner TU, etc.); update
  `.vcxproj`/`.filters` (`tools\validate_project_filters` clean); free
  functions leave the bare `SkullbonezCore` namespace for their owner's
  namespace or anonymous namespaces. Run the comment-style audit on every
  touched file (learning headers reflect the new owners; stale
  cross-references fixed). Then the **mandatory independent ownership
  review** per the god-object closure rule: the reviewer checks each owner
  for unrelated responsibilities, reach-back into the composition root,
  forwarding wrappers, and next-god-object absorption; any credible finding
  reopens the owning task and blocks closure — it cannot be waived as
  follow-up debt. Validation: final
  `tools\validate_replay_visual_fidelity.bat` first, then
  `tools\validate_full.bat` and `tools\validate_replay_scrub.bat` from final
  source, results recorded.

## Dependencies And Decisions

- **Binding prerequisite:** `replay-visual-fidelity-mega-probe.md` complete
  (M0). Its golden-base and predicted/live packet comparisons are binding
  divergence detectors for every decomposition task, not only M6.
- **Branch binding:** both plans execute on `nightrunner-13th-july`. Moving the
  work requires an explicit owner decision and a fresh passing mega-probe
  provenance check on the destination.
- `adversarial-review-round-3` is complete and is no longer a sequencing
  dependency.
- Decision recorded: extraction order is presentation → timeline/scrubber →
  authoring → prediction, safest-first, so the riskiest move (M6) happens
  with the most decomposition experience and the fidelity gate already
  exercised by three prior tasks.
- Decision recorded: shared PODs live in a value-only identity header; any
  temptation to add behavior or state there is a closure failure.
- Hazard: replay reserve allocation rows in
  `tools/allocation_policy_allowlist.json` name owners; owner renames/moves
  must update rows in the same commit or the allocation checker fails.
- Hazard: `ReplayV2Artifact` serializes branch/provenance records that M5
  moves; the artifact format and field order must not change (no version bump
  is in scope — moving code must not reorder serialization).

## Validation

Every task M0-M8 first lands with
`tools\validate_replay_visual_fidelity.bat` green against the unchanged
known-good 200-box manifest. Every implementation task also keeps
`tools\validate_replay_scrub.bat` green. PR-gate commits use
`tools\validate_full.bat` per the `Runtime/*` file-map row. M6 adds
`tools\validate_perf.bat`. Physics CSV and replay visual baselines are untouched
by design; any diff during this plan is a defect, never a refresh.

## Definition Of Done

- `ReplayRuntime` is composition-only: constructs owners, sequences frame
  order, holds no business state (same closure standard `Run` met).
- Five owners plus artifact IO, each with its own header; no everything-header
  remains; tool TUs include only their slice.
- Run and the UI text pass consume a per-frame value snapshot; probes consume
  published views.
- No `Run*` prefixed replay files; no replay free functions in the bare
  engine namespace; no context bags, forwarding wrappers, or reach-back.
- The frame-exact 200-box mega probe passed after every M0-M8 task without a
  baseline refresh; all replay gates, perf, and the full gate pass from final
  source; the independent ownership review is recorded clean.

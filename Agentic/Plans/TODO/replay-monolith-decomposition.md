# Replay Monolith Decomposition — Owner Boundaries Inside The Replay Subsystem

Date: 2026-07-13
Status: Live — 3/9 tasks complete; M2 owner headers split
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

- [x] **M0 — Prerequisite gate (no code).**
  `replay-visual-fidelity-mega-probe` is complete and both
  `tools\validate_replay_visual_fidelity.bat` and
  `tools\validate_replay_scrub.bat` pass on the exact starting tree. Record
  command output, runtime, baseline provenance, compared tick count, packet
  schema version, and hashes here. `adversarial-review-round-3` is already
  complete. Validation: `tools\validate_replay_visual_fidelity.bat`.
- [x] **M1 — Type inventory and binding owner map.** Enumerate all 84 types in
  `ReplayRuntime.h` and every free function in the six `RunReplay*.cpp` TUs;
  assign each to one target owner (table above) in a checklist appended to
  this plan file. Checklist rules: one row per type/function; a row may be
  reassigned later only with a written reason; any type genuinely shared by
  3+ owners goes to a small `ReplayIdentity.h`-style value header
  (`ReplayBodyId`, `ModelRowHint`, sample POD structs) — values only, no
  state, no services. Acceptance: every row has an owner; no "misc" bucket
  exists. Validation, required by owner even for this documentation task:
  `tools\validate_replay_visual_fidelity.bat`.
- [x] **M2 — Shatter the everything-header.** Create the five owner headers
  plus the shared value header per M1's map; move type definitions without
  editing their bodies; `ReplayRuntime.h` shrinks to the composition root
  declaration and owner includes. Update includes in every replay TU so each
  tool includes only its slice; `ReplayRuntimeOwnerViews.h` keeps working
  against the new headers. Mechanical only — no member moves yet, no renames.
  Acceptance: the current header has no concrete top-level owner type body;
  every split TU names only the owner slices it uses before the temporary root
  include; zero warnings. The final sub-300-line composition root is measured at
  M7 after member/method authority can move honestly. Validation:
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
  for every single-generation prediction tick and exact presentation byte;
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
  (M0). Its golden-base, causal, and durable packet comparisons are binding
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

## M1 Current-Tree Inventory Method — 2026-07-14

The historical 84-type estimate described the earlier tree. The current,
CodeGraph-indexed header contains 55 concrete type definitions/aliases after
excluding forward declarations and enum members. The six named TUs contain
212 free-function definitions after excluding owner methods and local helper
types. The current CodeGraph index was up to date; the inventory was also
reconciled against the source line locations below. Every current item has one
binding destination. "Probe consumer" rows identify the published owner view
they may consume in M7; they do not grant probes state ownership. A later
reassignment requires a written reason in this plan.

M1 validation: `tools\validate_replay_visual_fidelity.bat` passed in about six
minutes with one generation/presentation, 2,401 exact ticks, 200 moved/settled
bricks, 187 grounded sleepers, 199 causal nodes, and every deliberate
first-divergence control. No baseline file changed.

M2 recorded adjustments:

- `RunReplayCauseTreeRowKind` moves from the M1 `ReplayAuthoring` row to the
  value-only `ReplayIdentity.h`. Both authoring rows and presentation camera
  focus store the discriminator, and the direct-slice compile proved that
  dependency had previously been hidden by `ReplayRuntime.h`. The enum has no
  behavior or mutable state.
- The historical sub-300 M2 target is mechanically impossible on the current
  post-mega tree: the `ReplayRuntime` class declaration alone was about 755
  lines before moving any member authority. M2 is forbidden to move members.
  The honest mechanical result is 899 lines with zero concrete top-level owner
  type definitions remaining; nested command types and method/member authority
  move with their owners in M3-M7, where the final sub-300 root remains binding.
- The six split TUs now directly include only their M1 owner slices, followed by
  a temporary `ReplayRuntime.h` include because their functions are still
  `ReplayRuntime` methods. Removing that root include before M3-M7 would require
  forwarding wrappers or premature member moves, both explicit plan failures.

M2 validation: the targeted Profile build passed with zero warnings and zero
errors; project/filter parity passed with 678/678 items; formatting passed for
all 234 headers. The unchanged mega gate then passed with exactly one engine,
one prediction generation, one presented 2,401-tick cascade, 200 moved/settled
bricks, 187 bricks directly grounded and solver-sleeping throughout the final
121 samples, 199 causal nodes, and every false-pass control. The broad gate
passed all CPU, Profile/Debug, DX12, standalone physics, and 44,401-line
byte-exact varied-physics lanes. The scrub alias's no-engine propagation probe
returned its required synthetic exit code 37. No baseline changed. The touched
source/tool comment audit checked 14/14 files with zero deferred.

## M1 Binding Type Inventory

| Done | Type | Current line | Binding owner | Reason |
|---|---|---:|---|---|
| [x] | `ReplayPredictionWorkerOperation` | 138 | ReplayPrediction | private prediction build/publication value |
| [x] | `ReplayPredictionAmortizedTask` | 148 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayTrack` | 171 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayMemoryPreset` | 177 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayMemoryPolicy` | 185 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayMemoryPolicyRequest` | 199 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `RunReplayScrubberState` | 297 | ReplayScrubber | cursor/restore transaction value |
| [x] | `RunReplayPathTarget` | 320 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `RunReplayPastTrajectoryBuildState` | 327 | ReplayPresentation | renderer-facing path/submission value |
| [x] | `RunReplayCameraFocusKind` | 343 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `RunReplayCauseTreeRowKind` | 353 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `RunReplayCameraState` | 362 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `RunReplayCauseTreeRow` | 389 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `RunReplayCauseTreeState` | 421 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `RunReplayPathVisualizerState` | 446 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `RunReplayPredictionBodyBackup` | 462 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionBodySample` | 477 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionFrame` | 486 | ReplayPrediction | private prediction build/publication value |
| [x] | `ReplayPredictionBaselineRootPoint` | 500 | ReplayPrediction | private prediction build/publication value |
| [x] | `ReplayPredictionBaselineBodyPose` | 506 | ReplayPrediction | private prediction build/publication value |
| [x] | `ReplayPredictionBaselineSnapshot` | 518 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionRevealClock` | 534 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionFutureNodeCache` | 550 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionTrajectoryBuildState` | 575 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionBuildState` | 591 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionSimulationState` | 633 | ReplayPrediction | private prediction build/publication value |
| [x] | `RunReplayPredictionState` | 666 | ReplayPrediction | private prediction build/publication value |
| [x] | `ReplayTrajectorySubmissionProbeStats` | 746 | ReplayPresentation | renderer-facing path/submission value |
| [x] | `RunReplayVelocityEditState` | 764 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `RunLoadedReplayPresentationState` | 776 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `RunReplayV2TargetRestoreResult` | 787 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayLiveRestoreKind` | 807 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayLiveRestoreRequest` | 814 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayRuntime` | 826 | Composition root | constructs/sequences the five owners only |
| [x] | `ReplayRuntime::ReplayOverlayBuildInput` | 829 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `ReplayRuntime::PathPickInput` | 839 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `ReplayRuntime::PathPickResult` | 848 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `ReplayRuntime::WorldPointerInput` | 854 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `ReplayRuntime::ReplayWorkspaceInput` | 883 | ReplayScrubber | per-frame replay workspace/control value |
| [x] | `ReplayRuntime::ReplayWorkspaceOutput` | 911 | ReplayScrubber | per-frame replay workspace/control value |
| [x] | `ReplayRuntime::ReplayLiveRestoreOutcome` | 918 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayRuntime::ReplayStartupRequest` | 925 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::ReplayStartupResult` | 937 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::RecordingConfigResult` | 942 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::SceneTimelineResetInput` | 954 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::SceneTimelineResetResult` | 994 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::SceneTimelineResetOwners` | 1000 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::PointerButtonEdges` | 1018 | ReplayPresentation | path, camera, overlay, or pointer presentation value |
| [x] | `ReplayRuntime::ScrubberInputFrame` | 1024 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayRuntime::ScrubberUnavailableResult` | 1031 | ReplayScrubber | cursor/restore transaction value |
| [x] | `ReplayRuntime::KeyboardVelocityEditInput` | 1036 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `ReplayRuntime::KeyboardVelocityEditCameraAction` | 1043 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `ReplayRuntime::KeyboardVelocityEditResult` | 1053 | ReplayAuthoring | velocity or branch/cause authoring value |
| [x] | `ReplayRuntime::ReplayProbeTickResult` | 1195 | ReplayTimeline | recording, retention, startup, or timeline policy |
| [x] | `ReplayRuntime::StartupWorkflowState` | 1533 | ReplayTimeline | recording, retention, startup, or timeline policy |

## M1 Binding Free-Function Inventory

| Done | Current TU | Function | Current line | Binding owner | Reason |
|---|---|---|---:|---|---|
| [x] | `RunReplayTools.cpp` | `TryResolveReplayBodyModelIndex` | 111 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `TryResolveReplayBodyModelIndex` | 134 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `TryAddReplayTargetMarkerFromStores` | 155 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `StepPredictionEngineTick` | 182 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BeginReplayRibbonDrawQuota` | 221 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `TryReserveReplayPathRibbonSegment` | 228 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `AddOrAccountReplayPathSegment` | 247 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `AddOrAccountReplayBaselinePathSegment` | 274 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionElapsedMilliseconds` | 322 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBudgetExpired` | 327 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBudgetExpiredForPass` | 335 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionRemainingMilliseconds` | 348 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionRevealSecondsPerSecond` | 358 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionRevealFrameIndex` | 379 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBuildPresentationFrameCountForRefresh` | 423 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionCapacityBytes` | 447 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionFramePayloadBytes` | 460 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `RoundUpReplayPredictionCapacity` | 477 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionInitialDebugContactCapacity` | 487 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionNextDebugContactCapacity` | 495 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionEngineReserveBytes` | 504 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReserveReplayPredictionVector` | 525 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReserveReplayPredictionFramePayloadVectors` | 566 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ClearReplayPredictionFutureNodeCache` | 636 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `FindReplayBodyByIdInSample` | 660 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `FindReplayBodyByModelIndexInSample` | 673 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayBodyIdForModelIndexInSample` | 703 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `FindReplayBodyById` | 714 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `FindReplayPredictionBodyById` | 719 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `FindReplayPredictionBodyByModelIndex` | 725 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `FindReplayBodyByModelIndex` | 733 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBodyIdForModelIndex` | 739 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayModelIndexIsRagdollPart` | 746 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayRagdollTorsoModelIndexForPart` | 758 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayNormalizeOr` | 769 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplaySolverBodyOrientation` | 780 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `FindReplayPredictionBodyByIdWithHint` | 787 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayTrajectoryFrameNumberForReserve` | 807 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayTrajectoryKey` | 813 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReserveReplayTrajectoryRecordSlot` | 822 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BeginReplayTrajectoryRecord` | 829 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AppendReplayTrajectoryPoint` | 853 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionTrajectoryRecordCapacity` | 866 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionChildTrajectoryBranch` | 871 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `PrepareReplayPredictionTrajectoryBuild` | 878 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `PublishReplayPredictionRootTrajectoryFrame` | 919 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `RebuildReplayPredictionCommittedRootTrajectory` | 954 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BuildReplayPredictionChildTrajectoryRecord` | 1001 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AppendReplayPredictionChildTrajectoryFrames` | 1072 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `UpdateReplayPredictionTrajectoryStore` | 1127 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionFutureTreeReadyForDraw` | 1223 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBodyHasVisibleLinearMotion` | 1239 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBodyRestingPose` | 1252 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayContactHasModelIndex` | 1287 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayContactOtherModelIndex` | 1292 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayContactPoint` | 1305 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayContactNormalForModel` | 1318 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayContactImpulseForModel` | 1332 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayFindPipelineIndexForContact` | 1343 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPathFrameT` | 1359 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayColorLerp` | 1374 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPastRootColor` | 1379 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayFutureRootColor` | 1388 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayDepthPalette` | 1396 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPathStrideForSampleCount` | 1436 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionDrawFrameWindowFor` | 1453 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ClearReplayPredictionBaseline` | 1472 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `CaptureReplayPredictionBaselineSnapshot` | 1495 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `PublishReplayPredictionBaselineRootTrajectory` | 1619 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `UpdateReplayPredictionBaselineDivergence` | 1654 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionBaselineSnapshot` | 1715 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `TryGetReplayFutureDepthInNodes` | 1768 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `FindReplayFutureNodeInNodes` | 1797 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AssignReplayFutureNode` | 1809 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AddReplayFutureNodeToNodes` | 1832 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayFutureNodeTopologyEquals` | 1887 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayFutureNodeTopologyEquals` | 1895 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AllocateReplayFutureNodeTopologyVersion` | 1913 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BuildReplayFutureNodesFromContacts` | 1934 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ShouldDrawReplayPathFrame` | 1993 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayTrajectoryPublishedPointCount` | 1998 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayTrajectoryRecordForDraw` | 2003 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayTrajectoryRecordSegments` | 2017 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayColliderRecordForModelIndex` | 2091 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `FindOrAddReplayPredictionRetainedMarker` | 2110 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RetainReplayPredictionEntryMarker` | 2142 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RetainReplayPredictionRestMarker` | 2158 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RetainReplayPredictionHorizonMarker` | 2175 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayRetainedMarkerTrailStrideForFrameCount` | 2195 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayRetainedMarkerTrailColor` | 2205 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `FindReplayPredictionMarkerTrailRecord` | 2226 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionRetainedMarkerTrailFromStore` | 2244 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionRetainedMarkers` | 2297 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RetainReplayPredictionEndStateMarkers` | 2337 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionCausalMarkers` | 2407 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `BuildReplayPredictionChildMarkerContext` | 2456 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionVisibleRootMotionFrame` | 2520 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RetainReplayPredictionRootRestMarker` | 2545 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayChildIncomingColor` | 2574 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayChildFutureColor` | 2588 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionDrawBranch` | 2602 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionRootTrajectoryFromStore` | 2607 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionSmallSceneBodyTrajectories` | 2644 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionChildTrajectoryRecord` | 2725 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionChildTrajectoriesFromStore` | 2819 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPastRootTrajectoryFromStore` | 2855 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionRagdollTorsoTrails` | 2913 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionIdInFutureNodes` | 2996 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayAffectedBodyTrailColor` | 3008 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionAffectedBodyTrails` | 3016 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `TryGetReplayPredictionFutureDepth` | 3204 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `AddReplayPredictionFutureNode` | 3214 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BuildReplayPredictionFutureNodes` | 3244 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ReplayPredictionBodyReachedActivationDisplacement` | 3285 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BuildReplayPredictionAffectedFutureNodes` | 3300 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `UpdateReplayPredictionFutureNodeCache` | 3385 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `CaptureReplayPredictionBodyState` | 3517 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `ApplyReplayPredictionBodyState` | 3582 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `SeedReplayPredictionEngine` | 3620 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `CaptureReplayPredictionFrame` | 3688 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `MarkReplayPredictionWorkerFailed` | 3801 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `RunReplayPredictionWorkerRange` | 3806 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `CompleteReplayPredictionJobOnFrameThread` | 3889 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `BeginReplayPredictionJob` | 3961 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `StepReplayPredictionJob` | 4187 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `RebuildReplayPredictionCommittedTreeAfterWorkerCompletion` | 4262 | ReplayPrediction | private prediction build, trajectory, or publication helper |
| [x] | `RunReplayTools.cpp` | `DrawReplayPredictionOverlay` | 4294 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RenderReplayPredictionVisualizer` | 4486 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayTools.cpp` | `RenderReplayPathVisualizer` | 4676 | ReplayPresentation | renderer-facing path/marker/overlay helper |
| [x] | `RunReplayProbes.cpp` | `ReplayProbeFailure` | 74 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `RenderProbeMatrixTranslation` | 80 | ReplayPresentation | published presentation probe consumer |
| [x] | `RunReplayProbes.cpp` | `TryPrepareReplayProbeRenderPosition` | 85 | ReplayPresentation | published presentation probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayProbePresentationSampleForRender` | 99 | ReplayPresentation | published presentation probe consumer |
| [x] | `RunReplayProbes.cpp` | `RestoreReplayProbeRenderInstances` | 113 | ReplayPresentation | published presentation probe consumer |
| [x] | `RunReplayProbes.cpp` | `TryGetReplayProbeBodyRecord` | 118 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `TryGetEditorTransformColliderRecord` | 135 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `ReplaySaveProbeDistanceSquared` | 172 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `InjectReplaySaveProbeEventCoverage` | 196 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `ValidateReplaySaveProbeArtifact` | 344 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `BuildSceneGeneratedModelContext` | 501 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `ReplayEventFloatFromBits` | 512 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `ReplayHexNibble` | 523 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `ReadReplayHexFloat` | 540 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `DecodeReplayRay9Payload` | 557 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `DecodeReplayPlacePayload` | 584 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `DecodeReplayTransformPayload` | 624 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `FindReplaySolverHashForFrame` | 668 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `FindReplayPresentationForFrame` | 681 | ReplayPresentation | published presentation probe consumer |
| [x] | `RunReplayProbes.cpp` | `WriteReplayProbeReason` | 694 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `LogReplayV2TargetRestoreDiagnostic` | 702 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `TryApplyReplayRestoreWorldLauncherEvent` | 772 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayRestoreEditorPlaceEvent` | 852 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayRestoreEditorTransformEvent` | 917 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayRestoreEventForTarget` | 1049 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `LoadReplayRestoreArtifactData` | 1140 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `SelectReplayRestoreTargetAndCheckpoint` | 1169 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `PrepareReplayRestoreArtifactSelection` | 1250 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ReplayCheckpointTopologyMatchesLive` | 1277 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `FindReplayGeneratedSceneConfigBeforeCheckpoint` | 1300 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `FormatReplayRestoreDivergenceMessage` | 1333 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `WriteReplayRestoreStepFailure` | 1448 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `StepReplayRestoreTarget` | 1469 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `CaptureAndValidateReplayRestoreTargetHash` | 1597 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `PopulateReplayRestoreTargetResult` | 1641 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `LogReplayRestoreTargetSuccess` | 1663 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayRestoreLiveBranch` | 1692 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `RebuildReplayGeneratedSceneTopology` | 1739 | ReplayTimeline | recording/artifact/startup probe consumer |
| [x] | `RunReplayProbes.cpp` | `EnsureReplayRestoreCheckpointTopology` | 1838 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `RunReplayRestoreTargetStep` | 1886 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayProbes.cpp` | `ApplyReplayRestoreCheckpointSample` | 1932 | ReplayScrubber | published restore/scrub probe consumer |
| [x] | `RunReplayScrubberTools.cpp` | `IsReplayScrubberToolOwner` | 66 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `KeepReplayScrubberVisible` | 384 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `ApplyReplayLiveAdvanceAction` | 391 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayPausePressed` | 457 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayVelocityEditPressed` | 474 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayPastPathPressed` | 504 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayRagdollVisualsPressed` | 516 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `SetReplayPredictionHorizonFromPointer` | 524 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayPredictionPressed` | 549 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayBranchPressed` | 581 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplaySavePressed` | 594 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `BeginReplayScrubberGesture` | 601 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `EndReplayScrubberGesture` | 618 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayLoadPressed` | 628 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayPredictionHorizonPressed` | 713 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `HandleReplayScrubPressed` | 743 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayScrubberTools.cpp` | `TickReplayScrubberGesture` | 768 | ReplayScrubber | scrub/control TU |
| [x] | `RunReplayVelocityEdit.cpp` | `IsReplayToolOwner` | 56 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `EditorAxisVector` | 64 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityLinearBaseLength` | 80 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityLinearVisualAxisT` | 86 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityLinearUnitsPerWorld` | 94 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityAngularBaseRadius` | 100 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityAngularVisualRadius` | 106 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocityAxisComponent` | 113 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `ReplayVelocitySetAxisComponent` | 127 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `EditorRotationRingBasisA` | 144 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `EditorRotationRingBasisB` | 160 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `WrapEditorAngleDelta` | 176 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `DistanceRayToSegmentSquared` | 190 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `TryResolveReplayVelocityBodyView` | 255 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `HitReplayVelocityLinearAxis` | 292 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `HitReplayVelocityAngularAxis` | 323 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `TryReplayVelocityAxisRayParameter` | 368 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayVelocityEdit.cpp` | `TryReplayVelocityAngularRayAngle` | 396 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayCauseTreeTools.cpp` | `IsReplayCauseTreeToolOwner` | 47 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayCauseTreeTools.cpp` | `ReplayCauseTreeNormalizeOr` | 55 | ReplayAuthoring | authoring TU |
| [x] | `RunReplayQueryTools.cpp` | `ReplayQueryColliderRadiusForModelIndex` | 41 | ReplayPresentation | path picking/query TU |
| [x] | `RunReplayQueryTools.cpp` | `ReplayQueryBodyIdForModelIndex` | 59 | ReplayPresentation | path picking/query TU |
| [x] | `RunReplayQueryTools.cpp` | `ReplayQueryIntersectRaySphere` | 70 | ReplayPresentation | path picking/query TU |
| [x] | `RunReplayQueryTools.cpp` | `FindReplayQueryPathTarget` | 99 | ReplayPresentation | path picking/query TU |
| [x] | `RunReplayQueryTools.cpp` | `ApplyReplayQueryPrimaryPathTarget` | 112 | ReplayPresentation | path picking/query TU |

## M1 Current Include Baseline For M2

These are the six mechanically split TUs named by M1. M2 must replace
`ReplayRuntime.h` with only the owner/value headers each row needs and record any
intentional exception.

| TU | Current direct includes |
|---|---|
| `RunReplayTools.cpp` | `ReplayRuntime.h`; `EditorTools.h`; `RuntimeTools.h`; `SceneEntityStore.h`; `EditorHullAssets.h`; `InputController.h`; `ReplayInteractionController.h`; `ReplayOverlayLayout.h`; `ReplayOverlayRenderer.h`; `ReplayPredictionReserve.h`; `RuntimePickService.h`; `RuntimeAllocationTracker.h`; `RuntimeReserveAllocator.h`; `ColliderStore.h`; `PhysicsBodyStore.h`; `PhysicsApi.h`; `PhysicsEngine.h`; `PhysicsMass.h`; `PhysicsTimestep.h`; `RuntimeFileWriter.h`; `AmortizedTask.h`; `Config.h`; `WorkerPool.h`; `UILayout.h`; standard `algorithm`, `atomic`, `chrono`, `cfloat`, `cmath`, `cstddef`, `cstdint`, `cstdio`, `cstring`, `limits`, `memory`, `commdlg.h` |
| `RunReplayProbes.cpp` | `ReplayRuntime.h`; `DiagnosticsRuntime.h`; `SceneController.h`; `AssetSystem.h`; `WorkerPool.h`; `RuntimeTuning.h`; `EditorTools.h`; `ReplayInteractionController.h`; `ReplayRestoreService.h`; `ReplayRuntimeOwnerViews.h`; `ReplayPredictionArchive.h`; `ReplayVisualPacketFingerprint.h`; `ReplayV2Artifact.h`; `FatalError.h`; `Profiler.h`; `SimulationSystem.h`; `ColliderStore.h`; `PhysicsApi.h`; `PhysicsEngine.h`; `PhysicsTimestep.h`; standard `bit`, `cmath`, `cstdint`, `cstdio`, `cstring`, `limits`, `utility`, `vector` |
| `RunReplayScrubberTools.cpp` | `ReplayRuntime.h`; `AssetKeys.h`; `CameraCollection.h`; `InputRouter.h`; `RuntimeInteractionCommands.h`; `RunCameraState.h`; `RuntimeTools.h`; `Profiler.h`; `FatalError.h`; `ColliderStore.h`; `PhysicsBodyStore.h`; `PhysicsEngine.h`; `InputController.h`; `ReplayInteractionController.h`; `ReplayOverlayLayout.h`; `ReplayRuntimeOwnerViews.h`; `Terrain.h`; standard `algorithm`, `cstddef`, `cstdio`, `cstring`, `commdlg.h` |
| `RunReplayVelocityEdit.cpp` | `ReplayRuntime.h`; `AssetKeys.h`; `EditorTools.h`; `RuntimeTools.h`; `InputRouter.h`; `SceneEntityStore.h`; `Profiler.h`; `ReplayInteractionController.h`; `ReplayOverlayLayout.h`; `ColliderStore.h`; `PhysicsBodyStore.h`; `PhysicsEngine.h`; standard `algorithm`, `cfloat`, `cmath` |
| `RunReplayCauseTreeTools.cpp` | `ReplayRuntime.h`; `AssetKeys.h`; `CameraCollection.h`; `InputController.h`; `InputRouter.h`; `Profiler.h`; `FatalError.h`; `ReplayOverlayLayout.h`; `ColliderStore.h`; `PhysicsBodyStore.h`; standard `algorithm`, `cmath` |
| `RunReplayQueryTools.cpp` | `ReplayRuntime.h`; `SceneEntityStore.h`; `RuntimePickService.h`; `ColliderStore.h`; `PhysicsBodyStore.h`; standard `algorithm`, `cfloat`, `cmath`, `cstring` |

## M0 Starting-Tree Evidence — 2026-07-14

- Starting commit: `1dff40e1d1541a6bb9511ef4f1ad4bd3986dda61` on
  `nightrunner-13th-july`, immediately after the 7/7 mega-probe closure.
- `tools\validate_replay_visual_fidelity.bat` passed in about six minutes with
  one engine process, one prediction generation, one presented cascade, 2,401
  compared ticks, packet schema 1 inside manifest schema 2, 200 moved and
  settled wall bricks, 187 grounded sleepers, and 199 causal nodes. All packet,
  causal, artifact, RVPD, and ten determinism false-pass controls rejected their
  intended mutations.
- Immutable visual manifest SHA-256:
  `2448394EB4E456DFEAB7F36645F124C9C6792456AAA689323E1C786FFB453C93`.
  Immutable causal manifest SHA-256:
  `07E6E6FDA8918CBACDFF6F151C986389D11DAEE65BCAAF64AEAFD01DC81D27D3`.
  Generated schema-4 artifact SHA-256:
  `B631D319CDF8098B038995C793045991A27FD4132A52A21647678EF2EC906136`.
- Manifest provenance remains scene
  `41ec952ba22fae36e462b750d894909dd0ee58975554d696aea16220c0f2c934`,
  interaction script
  `585a904bad3f6224062d338fd830028664067f3d2455a6d861e6969d0b57eea1`,
  config
  `541cfec50cdc052e502b042d2c80c288c06157ad4269ede382d61e2e456efcd5`,
  and shader tree
  `4c285cc8ee1371b1e97d93c8e450998a9c1a2f2b267742a1a258799b7d775d20`.
- The historical scrub alias was verified without another engine launch: its
  static launcher shape still delegates to the one authoritative command and
  `--prove-failure-propagation` previously returned synthetic exit code 37.
  Running the normal alias here is forbidden because it would present the
  prediction a second time.

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

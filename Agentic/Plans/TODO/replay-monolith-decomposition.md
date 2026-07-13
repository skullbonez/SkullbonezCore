# Replay Monolith Decomposition — Owner Boundaries Inside The Replay Subsystem

Date: 2026-07-13
Status: Live — 2/9 tasks complete; M0/M1 owner inventory closed
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
  probes, prediction determinism, and the new fidelity gate must pass
  unchanged after every task.
- No semantic redesign of the prediction protocol, recorder retention, or
  artifact format.
- No `ReplayContext`/`ReplayServices` bag, no `void*`, no callback packs, no
  friend access, no `Replay::*` forwarding wrappers that relay business
  operations while authority stays in `ReplayRuntime` — the god-object closure
  failures list in `AGENTS.md` applies verbatim to this subsystem.
- No line-count targets. Big cohesive owners are fine when the closure review
  records why their state belongs together.

## Tasks

- [x] **M0 — Prerequisite gate (no code).** The fidelity closure report confirms
  `replay-prediction-fidelity-probe` is complete and
  `tools\validate_replay_scrub.bat` (including the fidelity
  step) passes on the starting tree. Record the passing run here. Also record
  the sequencing decision against `adversarial-review-round-3` R5 (the
  `Basics` namespace rename touches every replay file): either R5 lands
  first (preferred — this plan then works in the new namespace) or the owner
  accepts the rebase cost; do not run both concurrently.
- [x] **M1 — Type inventory and binding owner map.** Enumerate all 84 types in
  `ReplayRuntime.h` and every free function in the six `RunReplay*.cpp` TUs;
  assign each to one target owner (table above) in a checklist appended to
  this plan file. Checklist rules: one row per type/function; a row may be
  reassigned later only with a written reason; any type genuinely shared by
  3+ owners goes to a small `ReplayIdentity.h`-style value header
  (`ReplayBodyId`, `ModelRowHint`, sample POD structs) — values only, no
  state, no services. Acceptance: every row has an owner; no "misc" bucket
  exists. Validation: none (documentation task).
- [ ] **M2 — Shatter the everything-header.** Create the five owner headers
  plus the shared value header per M1's map; move type definitions without
  editing their bodies; `ReplayRuntime.h` shrinks to the composition root
  declaration and owner includes. Update includes in every replay TU so each
  tool includes only its slice; `ReplayRuntimeOwnerViews.h` keeps working
  against the new headers. Mechanical only — no member moves yet, no renames.
  Acceptance: `ReplayRuntime.h` under ~300 lines; no replay TU includes an
  owner header it does not use (spot-check with the include list per TU
  recorded in the M1 checklist); zero warnings. Validation:
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
  Validation: `tools\validate_replay_scrub.bat` (fingerprint step proves
  pixel-level submission is unchanged) + `tools\validate_full.bat` at the PR
  gate.
- [ ] **M4 — Extract `ReplayScrubber` and `ReplayTimeline`.** Timeline first
  (recorder + retention + memory policy are already nearly self-contained),
  then scrubber (cursor state machine + restore transactions +
  `RunReplayScrubberTools.cpp`). Restore paths keep cancelling prediction
  before mutating live authority (`ReplayInteractionController.cpp:20`
  invariant) — that call becomes an explicit owner-to-owner request, not a
  reach into `ReplayRuntime` fields. Acceptance: scrub/retained-restore
  SkullScope probes pass; solver-track hash-restore behavior unchanged;
  `ReplayRuntime` no longer holds cursor or retention members. Validation:
  `tools\validate_replay_scrub.bat` + `tools\validate_full.bat` at the PR
  gate.
- [ ] **M5 — Extract `ReplayAuthoring`.** Velocity edit, branch provenance,
  cause-tree tools move behind the owner; the velocity-edit "dirty
  prediction" side effect becomes an explicit request to `ReplayPrediction`
  (queued value command, consistent with the repo's one-frame command-packet
  idiom) rather than direct member writes. Acceptance: velocity-edit and
  branch-restore interaction scripts pass; no authoring state in
  `ReplayRuntime`. Validation: `tools\validate_replay_scrub.bat`,
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
  determinism fingerprint unchanged; **fidelity gate green** (this is the
  task the fidelity probe exists for — seeding/restore must still produce a
  byte-exact future); allocation-policy allowlist rows for replay reserve
  updated to name the new owner in the same commit. Validation:
  `tools\validate_replay_scrub.bat` (all four steps),
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
  `tools\validate_replay_scrub.bat` + `tools\validate_full.bat` at the PR
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
  follow-up debt. Validation: final `tools\validate_full.bat` +
  `tools\validate_replay_scrub.bat` from final source, results recorded.

## M0 Evidence And Sequencing Record

- Fidelity prerequisite: complete at 5/5; closure evidence is
  `../../Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md`.
- Starting-tree divergence gate: `tools\\validate_replay_scrub.bat` passed in
  162.3 seconds on 2026-07-13. Scrub, retained restore, deterministic
  submitted geometry (`0x91DDF05DC57A993D`), and all 120 predicted/live
  solver hashes passed. Mirrored log:
  `TestOutput/validation/replay_fidelity_validate_replay_scrub.log`.
- Sequencing: adversarial-review-round-3 R5 landed before this plan and the
  round is closed at 10/10. This plan therefore works after the `Basics`
  namespace rename; no concurrent replay-wide rename or accepted rebase cost
  remains.

## M1 Binding Owner Inventory

Measured from the post-fidelity tree at commit `de768a04`. The header has
84 class/struct/enum declaration rows plus 1 alias row; the original 84-type measurement is therefore reconciled, and the alias is listed too.
CodeGraph reports 213 free-function definitions across the six legacy tool TUs.
`ReplayRuntime` member functions are not free functions and are governed by the
composition-root and owner moves in M2-M7. External forward declarations are
assigned to their real declaring subsystem rather than falsely claimed by a
replay owner. There is no miscellaneous bucket.

### Header Type Checklist

| Done | Line | Declaration | Binding owner/header |
|---|---:|---|---|
| [x] | 84 | `class EngineConfig` | External declaring subsystem |
| [x] | 88 | `class SceneController` | External declaring subsystem |
| [x] | 93 | `class CameraCollection` | External declaring subsystem |
| [x] | 94 | `class WorldEnvironment` | External declaring subsystem |
| [x] | 99 | `class Terrain` | External declaring subsystem |
| [x] | 104 | `class ColliderStore` | External declaring subsystem |
| [x] | 105 | `class PhysicsEngine` | External declaring subsystem |
| [x] | 106 | `class PhysicsBodyStore` | External declaring subsystem |
| [x] | 111 | `class WorkerPool` | External declaring subsystem |
| [x] | 116 | `class ReplayRuntime` | Composition root |
| [x] | 117 | `class InputRouter` | External declaring subsystem |
| [x] | 118 | `class RunEditorTracer` | External declaring subsystem |
| [x] | 119 | `class RuntimeTools` | External declaring subsystem |
| [x] | 120 | `class SceneController` | External declaring subsystem |
| [x] | 121 | `class DiagnosticsRuntime` | External declaring subsystem |
| [x] | 122 | `class SimulationSystem` | External declaring subsystem |
| [x] | 123 | `enum class GeneratedObjectTypeOverride` | External declaring subsystem |
| [x] | 124 | `struct RunCameraState` | External declaring subsystem |
| [x] | 125 | `struct RunDebugState` | External declaring subsystem |
| [x] | 126 | `struct RunMousePickupState` | External declaring subsystem |
| [x] | 127 | `class RuntimeRenderer` | External declaring subsystem |
| [x] | 128 | `struct RunSceneState` | External declaring subsystem |
| [x] | 129 | `struct ReplayV2SaveResult` | ReplayV2Artifact |
| [x] | 130 | `struct ReplaySolverSampleRestoreContext` | ReplayScrubber |
| [x] | 137 | `struct ReplayPredictionWorkerOperation` | ReplayPrediction |
| [x] | 147 | `using ReplayPredictionAmortizedTask` | ReplayPrediction |
| [x] | 170 | `enum class RunReplayTrack` | ReplayScrubber |
| [x] | 176 | `enum class ReplayMemoryPreset` | ReplayTimeline |
| [x] | 184 | `struct ReplayMemoryPolicy` | ReplayTimeline |
| [x] | 198 | `struct ReplayMemoryPolicyRequest` | ReplayTimeline |
| [x] | 296 | `struct RunReplayScrubberState` | ReplayScrubber |
| [x] | 319 | `struct RunReplayPathTraceNode` | ReplayPresentation |
| [x] | 332 | `struct RunReplayPathTarget` | ReplayPresentation |
| [x] | 339 | `struct RunReplayPastTrajectoryBuildState` | ReplayPresentation |
| [x] | 355 | `enum class RunReplayCameraFocusKind` | ReplayPresentation |
| [x] | 365 | `enum class RunReplayCauseTreeRowKind` | ReplayAuthoring |
| [x] | 374 | `struct RunReplayCameraState` | ReplayPresentation |
| [x] | 401 | `struct RunReplayCauseTreeRow` | ReplayAuthoring |
| [x] | 433 | `struct RunReplayCauseTreeState` | ReplayAuthoring |
| [x] | 458 | `struct RunReplayPathVisualizerState` | ReplayPresentation |
| [x] | 474 | `struct RunReplayPredictionBodyBackup` | ReplayPrediction |
| [x] | 489 | `struct RunReplayPredictionBodySample` | ReplayPrediction |
| [x] | 498 | `struct RunReplayPredictionFrame` | ReplayPrediction |
| [x] | 515 | `struct ReplayPredictionGhostDrawRequest` | ReplayPrediction |
| [x] | 527 | `struct ReplayPredictionRetainedMarker` | ReplayPrediction |
| [x] | 542 | `struct ReplayPredictionBaselineRootPoint` | ReplayPrediction |
| [x] | 548 | `struct ReplayPredictionBaselineBodyPose` | ReplayPrediction |
| [x] | 560 | `struct ReplayPredictionBaselineSnapshot` | ReplayPrediction |
| [x] | 576 | `struct RunReplayPredictionRevealClock` | ReplayPrediction |
| [x] | 589 | `struct RunReplayPredictionFutureNodeCache` | ReplayPrediction |
| [x] | 614 | `struct RunReplayPredictionTrajectoryBuildState` | ReplayPrediction |
| [x] | 630 | `struct RunReplayPredictionBuildState` | ReplayPrediction |
| [x] | 671 | `struct RunReplayPredictionSimulationState` | ReplayPrediction |
| [x] | 714 | `struct RunReplayPredictionState` | ReplayPrediction |
| [x] | 794 | `struct ReplayTrajectorySubmissionProbeStats` | Probe published value |
| [x] | 812 | `struct RunReplayVelocityEditState` | ReplayAuthoring |
| [x] | 824 | `struct RunLoadedReplayPresentationState` | ReplayV2Artifact |
| [x] | 835 | `struct RunReplayV2TargetRestoreResult` | ReplayScrubber |
| [x] | 855 | `enum class ReplayLiveRestoreKind` | ReplayScrubber |
| [x] | 862 | `struct ReplayLiveRestoreRequest` | ReplayScrubber |
| [x] | 874 | `class ReplayRuntime` | Composition root |
| [x] | 877 | `struct ReplayOverlayBuildInput` | ReplayPresentation |
| [x] | 887 | `struct PathPickInput` | ReplayPresentation |
| [x] | 896 | `struct PathPickResult` | ReplayPresentation |
| [x] | 902 | `struct WorldPointerInput` | ReplayPresentation |
| [x] | 931 | `struct ReplayWorkspaceInput` | Composition root value |
| [x] | 959 | `struct ReplayWorkspaceOutput` | Composition root value |
| [x] | 966 | `struct ReplayLiveRestoreOutcome` | ReplayScrubber |
| [x] | 973 | `struct ReplayStartupRequest` | Composition root value |
| [x] | 985 | `struct ReplayStartupResult` | Composition root value |
| [x] | 990 | `struct RecordingConfigResult` | ReplayTimeline |
| [x] | 1002 | `struct SceneTimelineResetInput` | ReplayTimeline |
| [x] | 1042 | `struct SceneTimelineResetResult` | ReplayTimeline |
| [x] | 1048 | `struct SceneTimelineResetOwners` | ReplayTimeline |
| [x] | 1060 | `struct ReplayStartupLoadInput` | ReplayV2Artifact |
| [x] | 1061 | `struct ReplayRestoreTransaction` | ReplayScrubber |
| [x] | 1062 | `struct ReplayArtifactTopologyOwners` | ReplayV2Artifact |
| [x] | 1066 | `struct PointerButtonEdges` | ReplayScrubber |
| [x] | 1072 | `struct ScrubberInputFrame` | ReplayScrubber |
| [x] | 1079 | `struct ScrubberUnavailableResult` | ReplayScrubber |
| [x] | 1084 | `struct KeyboardVelocityEditInput` | ReplayAuthoring |
| [x] | 1091 | `enum class KeyboardVelocityEditCameraAction` | ReplayAuthoring |
| [x] | 1101 | `struct KeyboardVelocityEditResult` | ReplayAuthoring |
| [x] | 1238 | `struct ReplayProbeTickResult` | Probe consumer |
| [x] | 1569 | `struct StartupWorkflowState` | Probe consumer |

### Free-Function Checklist

#### `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` (130)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 112 | `TryResolveReplayBodyModelIndex` | ReplayPresentation |
| [x] | 135 | `TryResolveReplayBodyModelIndex` | ReplayPresentation |
| [x] | 156 | `TryAddReplayTargetMarkerFromStores` | ReplayPresentation |
| [x] | 183 | `StepPredictionEngineTick` | ReplayPrediction |
| [x] | 222 | `BeginReplayRibbonDrawQuota` | ReplayPresentation |
| [x] | 229 | `TryReserveReplayPathRibbonSegment` | ReplayPresentation |
| [x] | 248 | `AddOrAccountReplayPathSegment` | ReplayPresentation |
| [x] | 275 | `AddOrAccountReplayBaselinePathSegment` | ReplayPresentation |
| [x] | 323 | `ReplayPredictionElapsedMilliseconds` | ReplayPrediction |
| [x] | 328 | `ReplayPredictionBudgetExpired` | ReplayPrediction |
| [x] | 336 | `ReplayPredictionBudgetExpiredForPass` | ReplayPrediction |
| [x] | 349 | `ReplayPredictionRemainingMilliseconds` | ReplayPrediction |
| [x] | 359 | `ReplayPredictionRevealSecondsPerSecond` | ReplayPrediction |
| [x] | 380 | `ReplayPredictionRevealFrameIndex` | ReplayPrediction |
| [x] | 414 | `ReplayPredictionBuildPresentationFrameCountForRefresh` | ReplayPrediction |
| [x] | 438 | `ReplayPredictionCapacityBytes` | ReplayPrediction |
| [x] | 451 | `ReplayPredictionFramePayloadBytes` | ReplayPrediction |
| [x] | 468 | `RoundUpReplayPredictionCapacity` | ReplayPrediction |
| [x] | 478 | `ReplayPredictionInitialDebugContactCapacity` | ReplayPrediction |
| [x] | 486 | `ReplayPredictionNextDebugContactCapacity` | ReplayPrediction |
| [x] | 495 | `ReplayPredictionEngineReserveBytes` | ReplayPrediction |
| [x] | 516 | `ReserveReplayPredictionVector` | ReplayPrediction |
| [x] | 557 | `ReserveReplayPredictionFramePayloadVectors` | ReplayPrediction |
| [x] | 627 | `ClearReplayPredictionFutureNodeCache` | ReplayPrediction |
| [x] | 651 | `FindReplayBodyByIdInSample` | ReplayPrediction |
| [x] | 664 | `FindReplayBodyByModelIndexInSample` | ReplayPrediction |
| [x] | 694 | `ReplayBodyIdForModelIndexInSample` | ReplayPrediction |
| [x] | 705 | `FindReplayBodyById` | ReplayPrediction |
| [x] | 710 | `FindReplayPredictionBodyById` | ReplayPrediction |
| [x] | 716 | `FindReplayPredictionBodyByModelIndex` | ReplayPrediction |
| [x] | 724 | `FindReplayBodyByModelIndex` | ReplayPrediction |
| [x] | 730 | `ReplayPredictionBodyIdForModelIndex` | ReplayPrediction |
| [x] | 737 | `ReplayModelIndexIsRagdollPart` | ReplayPrediction |
| [x] | 749 | `ReplayRagdollTorsoModelIndexForPart` | ReplayPrediction |
| [x] | 760 | `ReplayNormalizeOr` | ReplayPrediction |
| [x] | 771 | `ReplaySolverBodyOrientation` | ReplayPrediction |
| [x] | 778 | `FindReplayPredictionBodyByIdWithHint` | ReplayPrediction |
| [x] | 798 | `ReplayTrajectoryFrameNumberForReserve` | ReplayPrediction |
| [x] | 804 | `ReplayTrajectoryKey` | ReplayPrediction |
| [x] | 813 | `ReserveReplayTrajectoryRecordSlot` | ReplayPrediction |
| [x] | 820 | `BeginReplayTrajectoryRecord` | ReplayPrediction |
| [x] | 844 | `AppendReplayTrajectoryPoint` | ReplayPrediction |
| [x] | 857 | `ReplayPredictionTrajectoryRecordCapacity` | ReplayPrediction |
| [x] | 862 | `ReplayPredictionChildTrajectoryBranch` | ReplayPrediction |
| [x] | 869 | `PrepareReplayPredictionTrajectoryBuild` | ReplayPrediction |
| [x] | 910 | `PublishReplayPredictionRootTrajectoryFrame` | ReplayPrediction |
| [x] | 945 | `RebuildReplayPredictionCommittedRootTrajectory` | ReplayPrediction |
| [x] | 992 | `BuildReplayPredictionChildTrajectoryRecord` | ReplayPrediction |
| [x] | 1063 | `AppendReplayPredictionChildTrajectoryFrames` | ReplayPrediction |
| [x] | 1118 | `UpdateReplayPredictionTrajectoryStore` | ReplayPrediction |
| [x] | 1214 | `ReplayPredictionFutureTreeReadyForDraw` | ReplayPrediction |
| [x] | 1230 | `ReplayPredictionBodyHasVisibleLinearMotion` | ReplayPrediction |
| [x] | 1243 | `ReplayPredictionBodyRestingPose` | ReplayPrediction |
| [x] | 1278 | `ReplayContactHasModelIndex` | ReplayPrediction |
| [x] | 1283 | `ReplayContactOtherModelIndex` | ReplayPrediction |
| [x] | 1296 | `ReplayContactPoint` | ReplayPrediction |
| [x] | 1309 | `ReplayContactNormalForModel` | ReplayPrediction |
| [x] | 1323 | `ReplayContactImpulseForModel` | ReplayPrediction |
| [x] | 1334 | `ReplayFindPipelineIndexForContact` | ReplayPrediction |
| [x] | 1350 | `ReplayPathFrameT` | ReplayPresentation |
| [x] | 1365 | `ReplayColorLerp` | ReplayPresentation |
| [x] | 1370 | `ReplayPastRootColor` | ReplayPresentation |
| [x] | 1379 | `ReplayFutureRootColor` | ReplayPresentation |
| [x] | 1387 | `ReplayDepthPalette` | ReplayPresentation |
| [x] | 1427 | `ReplayPathStrideForSampleCount` | ReplayPresentation |
| [x] | 1444 | `ReplayPredictionDrawFrameWindowFor` | ReplayPrediction |
| [x] | 1463 | `ClearReplayPredictionBaseline` | ReplayPrediction |
| [x] | 1486 | `CaptureReplayPredictionBaselineSnapshot` | ReplayPrediction |
| [x] | 1610 | `PublishReplayPredictionBaselineRootTrajectory` | ReplayPrediction |
| [x] | 1645 | `UpdateReplayPredictionBaselineDivergence` | ReplayPrediction |
| [x] | 1706 | `DrawReplayPredictionBaselineSnapshot` | ReplayPresentation |
| [x] | 1759 | `TryGetReplayFutureDepthInNodes` | ReplayPrediction |
| [x] | 1788 | `FindReplayFutureNodeInNodes` | ReplayPrediction |
| [x] | 1800 | `AssignReplayFutureNode` | ReplayPrediction |
| [x] | 1823 | `AddReplayFutureNodeToNodes` | ReplayPrediction |
| [x] | 1878 | `ReplayFutureNodeTopologyEquals` | ReplayPrediction |
| [x] | 1886 | `ReplayFutureNodeTopologyEquals` | ReplayPrediction |
| [x] | 1904 | `AllocateReplayFutureNodeTopologyVersion` | ReplayPrediction |
| [x] | 1925 | `BuildReplayFutureNodesFromContacts` | ReplayPrediction |
| [x] | 1984 | `ShouldDrawReplayPathFrame` | ReplayPresentation |
| [x] | 1989 | `ReplayTrajectoryPublishedPointCount` | ReplayPresentation |
| [x] | 1994 | `ReplayTrajectoryRecordForDraw` | ReplayPresentation |
| [x] | 2008 | `DrawReplayTrajectoryRecordSegments` | ReplayPresentation |
| [x] | 2082 | `ReplayColliderRecordForModelIndex` | ReplayPresentation |
| [x] | 2101 | `FindOrAddReplayPredictionRetainedMarker` | ReplayPrediction |
| [x] | 2133 | `RetainReplayPredictionEntryMarker` | ReplayPrediction |
| [x] | 2149 | `RetainReplayPredictionRestMarker` | ReplayPrediction |
| [x] | 2166 | `RetainReplayPredictionHorizonMarker` | ReplayPrediction |
| [x] | 2186 | `ReplayRetainedMarkerTrailStrideForFrameCount` | ReplayPrediction |
| [x] | 2196 | `ReplayRetainedMarkerTrailColor` | ReplayPrediction |
| [x] | 2217 | `FindReplayPredictionMarkerTrailRecord` | ReplayPrediction |
| [x] | 2235 | `DrawReplayPredictionRetainedMarkerTrailFromStore` | ReplayPrediction |
| [x] | 2288 | `DrawReplayPredictionRetainedMarkers` | ReplayPrediction |
| [x] | 2328 | `RetainReplayPredictionEndStateMarkers` | ReplayPrediction |
| [x] | 2398 | `DrawReplayPredictionCausalMarkers` | ReplayPresentation |
| [x] | 2447 | `BuildReplayPredictionChildMarkerContext` | ReplayPresentation |
| [x] | 2511 | `ReplayPredictionVisibleRootMotionFrame` | ReplayPresentation |
| [x] | 2536 | `RetainReplayPredictionRootRestMarker` | ReplayPresentation |
| [x] | 2565 | `ReplayChildIncomingColor` | ReplayPresentation |
| [x] | 2579 | `ReplayChildFutureColor` | ReplayPresentation |
| [x] | 2593 | `ReplayPredictionDrawBranch` | ReplayPresentation |
| [x] | 2598 | `DrawReplayPredictionRootTrajectoryFromStore` | ReplayPresentation |
| [x] | 2635 | `DrawReplayPredictionSmallSceneBodyTrajectories` | ReplayPresentation |
| [x] | 2716 | `DrawReplayPredictionChildTrajectoryRecord` | ReplayPresentation |
| [x] | 2810 | `DrawReplayPredictionChildTrajectoriesFromStore` | ReplayPresentation |
| [x] | 2846 | `DrawReplayPastRootTrajectoryFromStore` | ReplayPresentation |
| [x] | 2904 | `DrawReplayPredictionRagdollTorsoTrails` | ReplayPresentation |
| [x] | 2987 | `ReplayPredictionIdInFutureNodes` | ReplayPresentation |
| [x] | 2999 | `ReplayAffectedBodyTrailColor` | ReplayPresentation |
| [x] | 3007 | `DrawReplayPredictionAffectedBodyTrails` | ReplayPresentation |
| [x] | 3195 | `TryGetReplayPredictionFutureDepth` | ReplayPrediction |
| [x] | 3205 | `AddReplayPredictionFutureNode` | ReplayPrediction |
| [x] | 3235 | `BuildReplayPredictionFutureNodes` | ReplayPrediction |
| [x] | 3276 | `ReplayPredictionBodyReachedActivationDisplacement` | ReplayPrediction |
| [x] | 3291 | `BuildReplayPredictionAffectedFutureNodes` | ReplayPrediction |
| [x] | 3376 | `UpdateReplayPredictionFutureNodeCache` | ReplayPrediction |
| [x] | 3508 | `CaptureReplayPredictionBodyState` | ReplayPrediction |
| [x] | 3573 | `ApplyReplayPredictionBodyState` | ReplayPrediction |
| [x] | 3611 | `ApplyReplayPredictionBodyState` | ReplayPrediction |
| [x] | 3655 | `SeedReplayPredictionEngine` | ReplayPrediction |
| [x] | 3728 | `CaptureReplayPredictionFrame` | ReplayPrediction |
| [x] | 3924 | `MarkReplayPredictionWorkerFailed` | ReplayPrediction |
| [x] | 3929 | `RunReplayPredictionWorkerRange` | ReplayPrediction |
| [x] | 4012 | `CompleteReplayPredictionJobOnFrameThread` | ReplayPrediction |
| [x] | 4084 | `BeginReplayPredictionJob` | ReplayPrediction |
| [x] | 4334 | `StepReplayPredictionJob` | ReplayPrediction |
| [x] | 4409 | `RebuildReplayPredictionCommittedTreeAfterWorkerCompletion` | ReplayPrediction |
| [x] | 4441 | `DrawReplayPredictionOverlay` | ReplayPresentation |
| [x] | 4633 | `RenderReplayPredictionVisualizer` | ReplayPresentation |
| [x] | 4808 | `RenderReplayPathVisualizer` | ReplayPresentation |

#### `SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp` (41)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 71 | `ReplayProbeFailure` | Probe consumer |
| [x] | 77 | `RenderProbeMatrixTranslation` | Probe consumer |
| [x] | 82 | `TryPrepareReplayProbeRenderPosition` | Probe consumer |
| [x] | 96 | `ApplyReplayProbePresentationSampleForRender` | Probe consumer |
| [x] | 110 | `RestoreReplayProbeRenderInstances` | Probe consumer |
| [x] | 115 | `TryGetReplayProbeBodyRecord` | Probe consumer |
| [x] | 132 | `TryGetEditorTransformColliderRecord` | Probe consumer |
| [x] | 169 | `ReplaySaveProbeDistanceSquared` | Probe consumer |
| [x] | 193 | `InjectReplaySaveProbeEventCoverage` | Probe consumer |
| [x] | 341 | `ValidateReplaySaveProbeArtifact` | Probe consumer |
| [x] | 498 | `BuildSceneGeneratedModelContext` | Probe consumer |
| [x] | 509 | `ReplayEventFloatFromBits` | Probe consumer |
| [x] | 520 | `ReplayHexNibble` | Probe consumer |
| [x] | 537 | `ReadReplayHexFloat` | Probe consumer |
| [x] | 554 | `DecodeReplayRay9Payload` | Probe consumer |
| [x] | 581 | `DecodeReplayPlacePayload` | Probe consumer |
| [x] | 621 | `DecodeReplayTransformPayload` | Probe consumer |
| [x] | 665 | `FindReplaySolverHashForFrame` | Probe consumer |
| [x] | 678 | `FindReplayPresentationForFrame` | Probe consumer |
| [x] | 691 | `WriteReplayProbeReason` | Probe consumer |
| [x] | 699 | `LogReplayV2TargetRestoreDiagnostic` | Probe consumer |
| [x] | 769 | `TryApplyReplayRestoreWorldLauncherEvent` | Probe consumer |
| [x] | 849 | `ApplyReplayRestoreEditorPlaceEvent` | Probe consumer |
| [x] | 914 | `ApplyReplayRestoreEditorTransformEvent` | Probe consumer |
| [x] | 1046 | `ApplyReplayRestoreEventForTarget` | Probe consumer |
| [x] | 1137 | `LoadReplayRestoreArtifactData` | Probe consumer |
| [x] | 1166 | `SelectReplayRestoreTargetAndCheckpoint` | Probe consumer |
| [x] | 1247 | `PrepareReplayRestoreArtifactSelection` | Probe consumer |
| [x] | 1274 | `ReplayCheckpointTopologyMatchesLive` | Probe consumer |
| [x] | 1297 | `FindReplayGeneratedSceneConfigBeforeCheckpoint` | Probe consumer |
| [x] | 1330 | `FormatReplayRestoreDivergenceMessage` | Probe consumer |
| [x] | 1445 | `WriteReplayRestoreStepFailure` | Probe consumer |
| [x] | 1466 | `StepReplayRestoreTarget` | Probe consumer |
| [x] | 1594 | `CaptureAndValidateReplayRestoreTargetHash` | Probe consumer |
| [x] | 1638 | `PopulateReplayRestoreTargetResult` | Probe consumer |
| [x] | 1660 | `LogReplayRestoreTargetSuccess` | Probe consumer |
| [x] | 1689 | `ApplyReplayRestoreLiveBranch` | Probe consumer |
| [x] | 1736 | `RebuildReplayGeneratedSceneTopology` | Probe consumer |
| [x] | 1835 | `EnsureReplayRestoreCheckpointTopology` | Probe consumer |
| [x] | 1883 | `RunReplayRestoreTargetStep` | Probe consumer |
| [x] | 1929 | `ApplyReplayRestoreCheckpointSample` | Probe consumer |

#### `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp` (17)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 66 | `IsReplayScrubberToolOwner` | ReplayScrubber |
| [x] | 384 | `KeepReplayScrubberVisible` | ReplayScrubber |
| [x] | 391 | `ApplyReplayLiveAdvanceAction` | ReplayScrubber |
| [x] | 457 | `HandleReplayPausePressed` | ReplayScrubber |
| [x] | 474 | `HandleReplayVelocityEditPressed` | ReplayAuthoring |
| [x] | 504 | `HandleReplayPastPathPressed` | ReplayPresentation |
| [x] | 516 | `HandleReplayRagdollVisualsPressed` | ReplayPresentation |
| [x] | 524 | `SetReplayPredictionHorizonFromPointer` | ReplayPrediction |
| [x] | 549 | `HandleReplayPredictionPressed` | ReplayPrediction |
| [x] | 581 | `HandleReplayBranchPressed` | ReplayAuthoring |
| [x] | 594 | `HandleReplaySavePressed` | ReplayV2Artifact |
| [x] | 601 | `BeginReplayScrubberGesture` | ReplayScrubber |
| [x] | 618 | `EndReplayScrubberGesture` | ReplayScrubber |
| [x] | 628 | `HandleReplayLoadPressed` | ReplayScrubber |
| [x] | 713 | `HandleReplayPredictionHorizonPressed` | ReplayPrediction |
| [x] | 743 | `HandleReplayScrubPressed` | ReplayScrubber |
| [x] | 768 | `TickReplayScrubberGesture` | ReplayScrubber |

#### `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp` (18)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 56 | `IsReplayToolOwner` | ReplayAuthoring |
| [x] | 64 | `EditorAxisVector` | ReplayAuthoring |
| [x] | 80 | `ReplayVelocityLinearBaseLength` | ReplayAuthoring |
| [x] | 86 | `ReplayVelocityLinearVisualAxisT` | ReplayAuthoring |
| [x] | 94 | `ReplayVelocityLinearUnitsPerWorld` | ReplayAuthoring |
| [x] | 100 | `ReplayVelocityAngularBaseRadius` | ReplayAuthoring |
| [x] | 106 | `ReplayVelocityAngularVisualRadius` | ReplayAuthoring |
| [x] | 113 | `ReplayVelocityAxisComponent` | ReplayAuthoring |
| [x] | 127 | `ReplayVelocitySetAxisComponent` | ReplayAuthoring |
| [x] | 144 | `EditorRotationRingBasisA` | ReplayAuthoring |
| [x] | 160 | `EditorRotationRingBasisB` | ReplayAuthoring |
| [x] | 176 | `WrapEditorAngleDelta` | ReplayAuthoring |
| [x] | 190 | `DistanceRayToSegmentSquared` | ReplayAuthoring |
| [x] | 255 | `TryResolveReplayVelocityBodyView` | ReplayAuthoring |
| [x] | 292 | `HitReplayVelocityLinearAxis` | ReplayAuthoring |
| [x] | 323 | `HitReplayVelocityAngularAxis` | ReplayAuthoring |
| [x] | 368 | `TryReplayVelocityAxisRayParameter` | ReplayAuthoring |
| [x] | 396 | `TryReplayVelocityAngularRayAngle` | ReplayAuthoring |

#### `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp` (2)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 47 | `IsReplayCauseTreeToolOwner` | ReplayAuthoring |
| [x] | 55 | `ReplayCauseTreeNormalizeOr` | ReplayAuthoring |

#### `SkullbonezSource/Runtime/Replay/RunReplayQueryTools.cpp` (5)

| Done | Line | Function | Binding owner |
|---|---:|---|---|
| [x] | 41 | `ReplayQueryColliderRadiusForModelIndex` | ReplayPresentation |
| [x] | 59 | `ReplayQueryBodyIdForModelIndex` | ReplayPresentation |
| [x] | 70 | `ReplayQueryIntersectRaySphere` | ReplayPresentation |
| [x] | 99 | `FindReplayQueryPathTarget` | ReplayPresentation |
| [x] | 112 | `ApplyReplayQueryPrimaryPathTarget` | ReplayPresentation |

### Current Include Baseline For The Six Legacy TUs

This is the M2 include spot-check baseline. M2 replaces the everything-header
with the smallest owner/value header set and records any justified shared
include.

- `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../Editor/EditorTools.h"`
  - `#include "../Tools/RuntimeTools.h"`
  - `#include "../Scene/SceneEntityStore.h"`
  - `#include "../Editor/EditorHullAssets.h"`
  - `#include "../InputController.h"`
  - `#include "ReplayInteractionController.h"`
  - `#include "ReplayOverlayLayout.h"`
  - `#include "ReplayOverlayRenderer.h"`
  - `#include "ReplayPredictionReserve.h"`
  - `#include "ReplaySolverHash.h"`
  - `#include "../RuntimePickService.h"`
  - `#include "../Allocation/RuntimeAllocationTracker.h"`
  - `#include "../Allocation/RuntimeReserveAllocator.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsBodyStore.h"`
  - `#include "../../Physics/PhysicsApi.h"`
  - `#include "../../Physics/PhysicsEngine.h"`
  - `#include "../../Physics/PhysicsMass.h"`
  - `#include "../../Physics/PhysicsTimestep.h"`
  - `#include "../RuntimeFileWriter.h"`
  - `#include "../../Core/AmortizedTask.h"`
  - `#include "../../Core/Config.h"`
  - `#include "../../Core/WorkerPool.h"`
  - `#include "../../UI/UILayout.h"`
  - `#include <algorithm>`
  - `#include <atomic>`
  - `#include <chrono>`
  - `#include <cfloat>`
  - `#include <cmath>`
  - `#include <cstddef>`
  - `#include <cstdint>`
  - `#include <cstdio>`
  - `#include <cstring>`
  - `#include <limits>`
  - `#include <memory>`
  - `#include <commdlg.h>`
- `SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../Diagnostics/DiagnosticsRuntime.h"`
  - `#include "../Scene/SceneController.h"`
  - `#include "../../Assets/AssetSystem.h"`
  - `#include "../../Core/WorkerPool.h"`
  - `#include "../RuntimeTuning.h"`
  - `#include "../Editor/EditorTools.h"`
  - `#include "ReplayInteractionController.h"`
  - `#include "ReplayRestoreService.h"`
  - `#include "ReplayRuntimeOwnerViews.h"`
  - `#include "ReplayV2Artifact.h"`
  - `#include "../../Core/FatalError.h"`
  - `#include "../../Core/Profiler.h"`
  - `#include "../../Physics/SimulationSystem.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsApi.h"`
  - `#include "../../Physics/PhysicsEngine.h"`
  - `#include "../../Physics/PhysicsTimestep.h"`
  - `#include <cmath>`
  - `#include <cstdint>`
  - `#include <cstdio>`
  - `#include <cstring>`
  - `#include <limits>`
  - `#include <utility>`
  - `#include <vector>`
- `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../../Assets/AssetKeys.h"`
  - `#include "../CameraCollection.h"`
  - `#include "../InputRouter.h"`
  - `#include "../RuntimeInteractionCommands.h"`
  - `#include "../RunCameraState.h"`
  - `#include "../Tools/RuntimeTools.h"`
  - `#include "../../Core/Profiler.h"`
  - `#include "../../Core/FatalError.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsBodyStore.h"`
  - `#include "../../Physics/PhysicsEngine.h"`
  - `#include "../InputController.h"`
  - `#include "ReplayInteractionController.h"`
  - `#include "ReplayOverlayLayout.h"`
  - `#include "ReplayRuntimeOwnerViews.h"`
  - `#include "../../World/Terrain.h"`
  - `#include <algorithm>`
  - `#include <cstddef>`
  - `#include <cstdio>`
  - `#include <cstring>`
  - `#include <commdlg.h>`
- `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../../Assets/AssetKeys.h"`
  - `#include "../Editor/EditorTools.h"`
  - `#include "../Tools/RuntimeTools.h"`
  - `#include "../InputRouter.h"`
  - `#include "../Scene/SceneEntityStore.h"`
  - `#include "../../Core/Profiler.h"`
  - `#include "ReplayInteractionController.h"`
  - `#include "ReplayOverlayLayout.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsBodyStore.h"`
  - `#include "../../Physics/PhysicsEngine.h"`
  - `#include <algorithm>`
  - `#include <cfloat>`
  - `#include <cmath>`
- `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../../Assets/AssetKeys.h"`
  - `#include "../CameraCollection.h"`
  - `#include "../InputController.h"`
  - `#include "../InputRouter.h"`
  - `#include "../../Core/Profiler.h"`
  - `#include "../../Core/FatalError.h"`
  - `#include "ReplayOverlayLayout.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsBodyStore.h"`
  - `#include <algorithm>`
  - `#include <cmath>`
- `SkullbonezSource/Runtime/Replay/RunReplayQueryTools.cpp`
  - `#include "ReplayRuntime.h"`
  - `#include "../Scene/SceneEntityStore.h"`
  - `#include "../RuntimePickService.h"`
  - `#include "../../Physics/ColliderStore.h"`
  - `#include "../../Physics/PhysicsBodyStore.h"`
  - `#include <algorithm>`
  - `#include <cfloat>`
  - `#include <cmath>`
  - `#include <cstring>`

## Dependencies And Decisions

- **Binding prerequisite:** prediction fidelity is complete; evidence lives in
  `../../Reports/2026-07-13/replay-prediction-fidelity-probe-closure.md` (M0).
  The fidelity gate is this plan's divergence detector for M6.
- **Sequencing vs round 3:** `adversarial-review-round-3` R5 (`Basics`
  rename) touches every replay file; run R5 → this plan, or record the owner's
  acceptance of the rebase cost in M0. R1–R4/R6–R10 of round 3 do not
  conflict materially.
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

Every task lands with `tools\validate_replay_scrub.bat` green (scrub,
retained-restore, prediction determinism, prediction fidelity). PR-gate
commits use `tools\validate_full.bat` per the `Runtime/*` file-map row. M6
adds `tools\validate_perf.bat`. Physics CSV baselines are untouched by design;
any physics baseline diff during this plan is a defect, never a refresh.

## Definition Of Done

- `ReplayRuntime` is composition-only: constructs owners, sequences frame
  order, holds no business state (same closure standard `Run` met).
- Five owners plus artifact IO, each with its own header; no everything-header
  remains; tool TUs include only their slice.
- Run and the UI text pass consume a per-frame value snapshot; probes consume
  published views.
- No `Run*` prefixed replay files; no replay free functions in the bare
  engine namespace; no context bags, forwarding wrappers, or reach-back.
- All replay gates (scrub, determinism, fidelity), perf, and the full gate
  pass from final source; the independent ownership review is recorded clean.

# Replay Subsystem Partition RS0 Census

Date: 2026-07-25

Plan: `Agentic/Plans/TODO/replay-subsystem-partition.md`

Status: **RS0 complete**

RS0 is documentation-only. It ratifies the physical package partition,
identifies every upward edge that the moves must remove, and freezes the
allocation and consumer inventories before behavior-preserving source work.

## Baseline Reconciliation

`git ls-files SkullbonezSource/Runtime/Replay` still returns exactly 72
source-bearing files. The plan's 36,900-line baseline used nonblank lines:

| Tip | Files | Physical lines | Nonblank lines |
|---|---:|---:|---:|
| Registration tip `c670e95f` | 72 | 39,976 | 36,900 |
| RS0 tip `fd284b46` | 72 | 41,849 | 37,022 |
| Drift | 0 | +1,873 | +122 |

The file count is unchanged. The line drift comes from the intervening
UI/renderer, formatter, and header-claim work; it does not add a fourth domain
or alter the partition ruling.

## Complete File And Include Matrix

Every file appears exactly once below. `Replay (shared value)` means the file
physically remains in the lowest package so Prediction and Planning may include
it without reversing the target arrows. Internal includes list every direct
edge to another file in the current 72-file tree.

| File | Physical / nonblank | Disposition | Internal Replay-tree includes |
|---|---:|---|---|
| `ReplayArtifactHashLog.cpp` | 176 / 154 | Replay | ReplayArtifactHashLog.h |
| `ReplayArtifactHashLog.h` | 52 / 45 | Replay | ReplayRecorder.h |
| `ReplayArtifactSource.h` | 43 / 37 | Replay (shared value) | — |
| `ReplayAuthoring.h` | 396 / 361 | Replay | ReplayIdentity.h, ReplayAuthoringPackets.h, ReplayRecorder.h |
| `ReplayAuthoringCauseTree.cpp` | 1236 / 1070 | Replay | ReplayAuthoring.h, ReplayCoordination.h, ReplayPrediction.h, ReplayPredictionPublicationOperations.h, ReplayPresentation.h, ReplayOverlayLayout.h |
| `ReplayAuthoringPackets.h` | 103 / 93 | Replay (shared value) | ReplayIdentity.h |
| `ReplayAuthoringVelocity.cpp` | 969 / 838 | Replay | ReplayAuthoring.h, ReplayCoordination.h, ReplayPresentation.h, ReplayScrubber.h, ReplayOverlayLayout.h |
| `ReplayCaptureLimits.h` | 33 / 28 | Replay (shared value) | — |
| `ReplayCapturePackets.h` | 53 / 45 | Replay (shared value) | ReplayIdentity.h |
| `ReplayCauseFocusSubmission.cpp` | 375 / 323 | Replay | ReplayPresentationSubmission.h, ReplayAuthoring.h, ReplayPrediction.h, ReplayPredictionPublicationOperations.h, ReplayPresentation.h |
| `ReplayCoordination.h` | 421 / 387 | Replay | ReplayAuthoring.h, ReplayPrediction.h, ReplayPorkchopPanel.h, ReplayPresentation.h, ReplayScrubber.h, ReplayTimeline.h, ReplayTripPlanner.h, ReplayProbeState.h |
| `ReplayEventCommand.h` | 154 / 140 | Replay (shared value) | ReplayIdentity.h |
| `ReplayGuideArcs.cpp` | 180 / 146 | Planning | ReplayGuideArcs.h |
| `ReplayGuideArcs.h` | 99 / 86 | Planning | — |
| `ReplayIdentity.h` | 70 / 60 | Replay (shared value) | — |
| `ReplayInterceptReadout.cpp` | 180 / 149 | Planning | ReplayInterceptReadout.h |
| `ReplayInterceptReadout.h` | 103 / 91 | Planning | ReplayPredictionView.h |
| `ReplayOverlayLayout.cpp` | 704 / 606 | Replay | ReplayOverlayLayout.h |
| `ReplayOverlayLayout.h` | 206 / 188 | Replay | ReplayAuthoring.h, ReplayOverlaySurface.h, ReplayPorkchopPanel.h, ReplayRecorder.h, ReplayScrubber.h, ReplayTripPlanner.h |
| `ReplayOverlayPackets.h` | 99 / 89 | Replay | ReplayCapturePackets.h, ReplayAuthoringPackets.h, ReplayInterceptReadout.h, ReplayPorkchopPanel.h, ReplayTripPlanner.h, ReplayPredictionView.h, ReplayPathPackets.h, ReplayPresentationPackets.h, ReplayTimelinePackets.h |
| `ReplayOverlayRenderer.cpp` | 1610 / 1443 | Replay | ReplayOverlayRenderer.h, ReplayOverlayLayout.h |
| `ReplayOverlayRenderer.h` | 275 / 250 | Replay | ReplayAuthoring.h, ReplayOverlayPackets.h, ReplayPrediction.h, ReplayPresentation.h, ReplayScrubber.h |
| `ReplayOverlaySurface.h` | 40 / 33 | Replay | ReplayCaptureLimits.h, ReplayTimelinePackets.h |
| `ReplayPathPackets.h` | 77 / 66 | Replay (shared value) | ReplayPredictionView.h |
| `ReplayPorkchopPanel.cpp` | 286 / 246 | Planning | ReplayPorkchopPanel.h |
| `ReplayPorkchopPanel.h` | 129 / 114 | Planning | — |
| `ReplayPrediction.cpp` | 1787 / 1571 | Prediction | ReplayPrediction.h, ReplayOverlayLayout.h, ReplayPredictionArchive.h, ReplayPredictionPublicationOperations.h, ReplayPredictionReserve.h, ReplayScrubber.h |
| `ReplayPrediction.h` | 621 / 581 | Prediction | ReplayIdentity.h, ReplayPredictionPublication.h, ReplayPredictionView.h, ReplayPredictionScheduling.h, ReplayRecorder.h, ReplayVisualPacket.h, TrajectoryStore.h |
| `ReplayPredictionArchive.Automation.cpp` | 75 / 64 | Prediction | ReplayPredictionArchive.h, ReplayRuntime.h |
| `ReplayPredictionArchive.cpp` | 799 / 715 | Prediction | ReplayPredictionArchive.h, ReplayRuntime.h |
| `ReplayPredictionArchive.h` | 60 / 51 | Prediction | — |
| `ReplayPredictionDrawing.cpp` | 2391 / 2126 | Prediction | ReplayOverlayRenderer.h, ReplayAuthoring.h, ReplayPrediction.h, ReplayPredictionPublicationOperations.h, ReplayPresentation.h, ReplayPresentationSubmission.h |
| `ReplayPredictionPackets.h` | 34 / 28 | Prediction | — |
| `ReplayPredictionPublication.cpp` | 1459 / 1266 | Prediction | ReplayPredictionPublicationOperations.h, ReplayOverlayLayout.h, ReplayPredictionReserve.h, ReplayScrubber.h |
| `ReplayPredictionPublication.h` | 95 / 80 | Prediction | — |
| `ReplayPredictionPublicationOperations.h` | 225 / 208 | Prediction | ReplayPrediction.h |
| `ReplayPredictionReserve.cpp` | 206 / 182 | Prediction | ReplayPredictionReserve.h, ReplayPrediction.h |
| `ReplayPredictionReserve.h` | 224 / 204 | Prediction | ReplayRetainedMemory.h |
| `ReplayPredictionScheduling.cpp` | 257 / 224 | Prediction | ReplayPrediction.h, ReplayPredictionPublicationOperations.h |
| `ReplayPredictionScheduling.h` | 210 / 186 | Prediction | ReplayIdentity.h, ReplayPredictionPackets.h |
| `ReplayPredictionTopologyPublication.cpp` | 1293 / 1142 | Prediction | ReplayPredictionPublicationOperations.h, ReplayOverlayLayout.h, ReplayScrubber.h |
| `ReplayPredictionView.h` | 135 / 124 | Prediction | ReplayIdentity.h, ReplayPredictionPackets.h, ReplayVisualPacket.h |
| `ReplayPresentation.cpp` | 1373 / 1162 | Replay | ReplayPresentation.h, ReplayPredictionView.h |
| `ReplayPresentation.h` | 411 / 388 | Replay | ReplayIdentity.h, ReplayPathPackets.h, ReplayPresentationPackets.h, ReplayRecorder.h, ReplayVisualPacket.h |
| `ReplayPresentationPackets.h` | 88 / 78 | Replay (shared value) | — |
| `ReplayPresentationSubmission.h` | 47 / 39 | Replay | — |
| `ReplayProbeState.h` | 281 / 254 | Replay | ReplayIdentity.h |
| `ReplayRecorder.cpp` | 3627 / 3206 | Replay | ReplayRecorder.h, ReplayRetainedMemory.h |
| `ReplayRecorder.h` | 749 / 696 | Replay | ReplayEventCommand.h, ReplayToolPackets.h, ReplayCaptureLimits.h, ReplayCapturePackets.h |
| `ReplayRestoreService.h` | 289 / 264 | Replay | ReplayRecorder.h, ReplayRestoreTransactions.h |
| `ReplayRestoreTransactions.h` | 100 / 90 | Replay | ReplayCoordination.h |
| `ReplayRetainedMemory.h` | 152 / 138 | Replay | — |
| `ReplayRuntime.cpp` | 2389 / 2064 | Replay | ReplayRuntime.h, ReplayOverlayLayout.h, ReplayRetainedMemory.h, ReplayRestoreService.h, ReplayRestoreTransactions.h, ReplayV2Artifact.h |
| `ReplayRuntime.h` | 529 / 505 | Replay | ReplayAuthoring.h, ReplayCoordination.h, ReplayGuideArcs.h, ReplayInterceptReadout.h, ReplayPorkchopPanel.h, ReplayTripPlanner.h, ReplayIdentity.h, ReplayPrediction.h, ReplayPresentation.h, ReplayOverlayRenderer.h, ReplayScrubber.h, ReplayTimeline.h, ReplayRecorder.h, ReplayVisualPacket.h, ReplayPredictionScheduling.h, TrajectoryStore.h, ReplayProbeState.h |
| `ReplayScrubber.h` | 405 / 366 | Replay | ReplayRecorder.h, ReplayTimelinePackets.h |
| `ReplayScrubberTools.cpp` | 2079 / 1847 | Replay | ReplayScrubber.h, ReplayRuntime.h, ReplayOverlayLayout.h, ReplayRestoreTransactions.h |
| `ReplayTimeline.cpp` | 397 / 345 | Replay | ReplayTimeline.h, ReplayV2Artifact.h |
| `ReplayTimeline.h` | 289 / 264 | Replay | ReplayArtifactHashLog.h, ReplayRecorder.h |
| `ReplayTimelinePackets.h` | 97 / 84 | Replay (shared value) | — |
| `ReplayToolPackets.h` | 58 / 49 | Replay (shared value) | — |
| `ReplayTripPlanner.cpp` | 438 / 381 | Planning | ReplayTripPlanner.h |
| `ReplayTripPlanner.h` | 183 / 163 | Planning | ReplayInterceptReadout.h, ReplayPredictionView.h |
| `ReplayV2Artifact.cpp` | 3127 / 2738 | Replay | ReplayV2Artifact.h, ReplayArtifactSource.h |
| `ReplayV2Artifact.h` | 156 / 143 | Replay | ReplayRecorder.h, ReplayVisualPacket.h |
| `ReplayValidation.cpp` | 2024 / 1801 | Replay | ReplayPresentation.h, ReplayOverlayLayout.h, ReplayScrubber.h, ReplayTimeline.h, ReplayRuntime.h, ReplayRestoreService.h, ReplayRestoreTransactions.h, ReplayPredictionArchive.h, ReplayValidation.Internal.h, ReplayV2Artifact.h |
| `ReplayValidation.Internal.h` | 65 / 55 | Replay | — |
| `ReplayValidation.Probes.cpp` | 2043 / 1778 | Replay | ReplayPresentation.h, ReplayOverlayLayout.h, ReplayScrubber.h, ReplayTimeline.h, ReplayRuntime.h, ReplayRestoreService.h, ReplayRestoreTransactions.h, ReplayPredictionArchive.h, ReplayValidation.Internal.h, ReplayVisualPacketFingerprint.h, ReplayV2Artifact.h |
| `ReplayVisualPacket.h` | 1202 / 1162 | Replay (shared value) | ReplayRecorder.h, TrajectoryStore.h |
| `ReplayVisualPacketFingerprint.cpp` | 730 / 620 | Replay | ReplayVisualPacketFingerprint.h |
| `ReplayVisualPacketFingerprint.h` | 114 / 100 | Replay | ReplayVisualPacket.h |
| `TrajectoryStore.cpp` | 356 / 305 | Prediction | TrajectoryStore.h, ReplayPredictionReserve.h |
| `TrajectoryStore.h` | 111 / 97 | Prediction | ReplayRecorder.h |

The ratified physical totals are:

| Destination | Files | Physical lines | Nonblank lines |
|---|---:|---:|---:|
| Replay | 46 | 29,913 | 26,492 |
| Prediction | 18 | 10,338 | 9,154 |
| Planning | 8 | 1,598 | 1,376 |
| Total | 72 | 41,849 | 37,022 |

## Include-Edge Summary And Upward-Edge Resolutions

The current direct internal matrix contains 188 edges:

| From | To Replay | To Prediction | To Planning |
|---|---:|---:|---:|
| Replay | 111 | 15 | 11 |
| Prediction | 20 | 24 | 0 |
| Planning | 0 | 2 | 5 |

The 26 preliminary upward edges are not deferred. These are the named
behavior-preserving resolutions:

| Current offenders | Resolution owned by RS1-RS3 |
|---|---|
| `ReplayAuthoringCauseTree.cpp`, `ReplayCauseFocusSubmission.cpp` -> Prediction | Move prediction-specific cause-row building/focus submission beside prediction publication; Replay authoring retains recorded-data rows. |
| `ReplayCoordination.h` -> Prediction/Planning | Keep replay lifecycle/restore transactions in Replay; move prediction update/archive operands to Prediction packets and trip/porkchop commands/views to Planning packets. No broad replacement context. |
| `ReplayOverlayLayout.h`, `ReplayOverlayPackets.h`, `ReplayOverlayRenderer.h` -> Prediction/Planning | Keep the recorded replay surface in Replay. Extract prediction overlay values/drawing into Prediction and trip/porkchop/intercept layout and packet aggregation into Planning. `Runtime/App` assembles the final late-pass view. |
| `ReplayPathPackets.h` -> Prediction | Move future-path selection/state to Prediction. Keep only recorded past-trajectory cursor values in Replay. Prediction may consume that lower value. |
| `ReplayPresentation.cpp/.h` -> Prediction | Keep recorded/solver sample presentation in Replay. Move prediction frame pose, ghost, future-path, and prediction trajectory methods/storage to the Prediction owner. |
| `ReplayRuntime.h` -> Prediction/Planning | Split composition: `ReplayRuntime` retains capture/timeline/scrub/artifact/restore; sibling Prediction and Planning owners are composed by `Runtime/App`. They consume lower value views and never back-reference ReplayRuntime. |
| `ReplayValidation.cpp`, `ReplayValidation.Probes.cpp` -> Prediction | Move prediction archive/future-presentation probes beside Prediction validation; retain artifact/timeline/restore validation in Replay. Planning assertions move with their product owners. |
| `ReplayVisualPacket.h` -> `TrajectoryStore.h` | Move the trajectory record/point value vocabulary down into the Replay shared packet seam; `TrajectoryStore` becomes a Prediction owner of Replay-defined values. The packet no longer includes an upper owner header. |
| `ReplayPredictionArchive*.cpp` -> `ReplayRuntime.h` | Delete the unused includes; both files operate on path/prediction values and do not use `ReplayRuntime`. |

After these splits the only cross-package arrows are
`Planning -> Prediction`, `Planning -> Replay`, and `Prediction -> Replay`.
No forwarding header, alias, callback pack, service bag, friend edge, or owner
backpointer is authorized.

## Shared-Seam And Composition Rulings

| Seam | RS0 ruling |
|---|---|
| `ReplayIdentity.h` | Replay shared value. Durable replay frame/branch vocabulary belongs at the lowest consumer level. |
| `ReplayEventCommand.h` | Replay shared value. Input emits replay transport/event values without prediction or planning authority. |
| `Replay*Packets.h` | Capture/authoring/presentation/timeline/tool packets stay in Replay when they describe recorded data. Prediction and Planning gain their own packet headers for upper-domain values. |
| `ReplayVisualPacket.h` | Replay shared value after trajectory value rows move down from `TrajectoryStore.h`; mutable trajectory storage remains Prediction. |
| `ReplayPathPackets.h` | Split. Recorded past-trajectory cursor stays Replay; future path state moves Prediction. |
| `ReplayCoordination.h` | Split by operation. Replay restore/lifecycle values stay; prediction operands move Prediction; planner/porkchop values move Planning. |
| `ReplayOverlay*` | Split by rendered domain. Replay keeps recorded timeline/capture surface, Prediction owns future drawing, Planning owns product panels and final upper aggregation. |
| `ReplayRuntime` composition | Clean sibling-owner split is required. `Runtime/App` constructs and sequences Replay, Prediction, and Planning. Replay exposes typed values; neither upper owner stores a ReplayRuntime backreference. |

## External Consumer Census

There are 35 direct `Replay/` include sites in 25 files across 12 Runtime
packages:

| Package | Sites | Current headers | Required post-split package access |
|---|---:|---|---|
| App | 8 | `ReplayOverlayPackets`, `ReplayRestoreTransactions`, `ReplayRuntime` | Replay, Prediction, Planning |
| Automation | 12 | coordination, overlay, prediction/archive/packets, presentation/timeline/artifact/visual packets | Replay, Prediction, Planning |
| Capture | 1 | `ReplayRuntime` | Replay |
| DevelopmentTools | 1 | `ReplayOverlayPackets` | Planning |
| Diagnostics | 1 | `ReplayRecorder` | Replay |
| Editor | 1 | `ReplayAuthoringPackets` | Replay |
| Input | 1 | `ReplayEventCommand` | Replay file exception remains |
| Render | 4 | overlay packets/renderer, presentation packet, visual packet | Replay, Prediction, Planning |
| Scene | 1 | `ReplayRuntime` | Replay, Planning (`--guide-arcs` activation) |
| Startup | 1 | `ReplayCaptureLimits` | Replay |
| Tools | 3 | event/tool/visual packets | Replay |
| UI | 1 | `ReplayOverlayPackets` | Planning |

Exact sites:

- App: `InputFrame.cpp:39`, `InputFrame.h:39`,
  `InputFrameExecution.cpp:52`, `InputRouter.Interactions.cpp:41`,
  `Run.cpp:36`, `Run.h:77`, `RunFrame.cpp:71-72`.
- Automation: `InteractionAutomationController.cpp:70`,
  `InteractionAutomationController.h:62`,
  `InteractionAutomationReportWriter.cpp:37-41`,
  `InteractionAutomationReportWriter.h:42-46`.
- Capture: `RuntimeStressController.cpp:36`.
- DevelopmentTools: `ImGuiEditorCausalityProjection.h:34`.
- Diagnostics: `RuntimeDiagnostics.cpp:41`.
- Editor: `EditorTools.h:34`.
- Input: `InputRouter.h:62`.
- Render: `RuntimeRenderHost.h:38`, `RuntimeRenderPasses.cpp:50`,
  `RuntimeRenderer.cpp:57`, `UiTextPass.cpp:60`.
- Scene: `SceneController.Load.cpp:52`.
- Startup: `StartupCommandLine.h:33`.
- Tools: `RuntimeTools.cpp:61`, `RuntimeTools.h:74-75`.
- UI: `OperatorEditorFrameComposer.cpp:41`.

The `AGENTS.md` Runtime table and `tools/dependency_graph_rules.json` therefore
add Prediction/Planning only to the App and Automation rows, Planning to
DevelopmentTools, Prediction/Planning to Render, Planning to Scene, and
Planning to Runtime/UI. Capture, Diagnostics, Editor, Input, Interaction,
Startup, and Tools retain their existing direct package access. Camera,
Direction, Simulation, Debug, and the top-level frame view gain neither edge.

New package rows are:

- Prediction: itself, Replay, Editor, Scene, and Tools. The latter three are
  proven by current prediction source includes and remain standing typed seams.
  Planning is forbidden.
- Planning: itself, Prediction, and Replay. It has no direct dependency on
  another Runtime package.
- Replay: its existing allowed lower Runtime targets remain, with Prediction
  and Planning explicitly forbidden.

## Reserve-Growth Inventory

The registered owner count remains exactly three:

| Owner | Registering file / destination | Phase gate | Hard cap | Counter source / denial |
|---|---|---|---:|---|
| `replay_recorder_samples` | `ReplayRecorder.cpp` / Replay | Replay only | 32 MiB | allocator high-water, capacity, replay growths, failures, last frame; denial fatal |
| `replay_solver_snapshot` | `PhysicsWorld.cpp` / Physics (unchanged) | Replay only | 8 MiB | same allocator counters; denial fatal |
| `replay_prediction_working_set` | `ReplayPredictionReserve.cpp` / Prediction | Replay only | 256 MiB | same allocator counters; denial cancels/truncates build |

The Prediction move changes only the package label and source paths. It does
not change the owner name, `RuntimeReserveSubsystem::Replay`, Replay phase
gate, zero initial bytes, unbounded-but-counted growth limit, cap, counter
coverage, or exhaustion behavior. RS1 updates the moved paths in
`tools/allocation_policy_allowlist.json`; RS3 splits the prediction policy row
out of `ReplayRetainedMemory.h` and keeps aggregation at the App diagnostics
boundary. The strict three-owner inventory remains authoritative.

## RS4 Enforcement Inventory

`tools/dependency_graph_rules.json` must:

1. extend `replay_downward_boundary` to deny
   `Runtime/Replay`, `Runtime/Prediction`, and `Runtime/Planning`;
2. add a Replay rule/fixture that rejects both upper packages;
3. add a Prediction allow rule for Prediction, Replay, Editor, Scene, and
   Tools, with a negative `Prediction -> Planning` fixture;
4. add a Planning allow rule for Planning, Prediction, and Replay;
5. update the six proven consumer rows listed above;
6. include positive fixtures for `Prediction -> Replay` and
   `Planning -> Prediction`, and negative fixtures for
   `Replay -> Prediction`, `Replay -> Planning`,
   `Prediction -> Planning`, and each lower-engine -> new-package edge.

`AGENTS.md` receives matching table rows and complement `rg` proofs. No frozen
file count, line budget, type-name ratchet, or compatibility rule is added.
Project/filter validation remains the exact single-project ownership proof for
all moved files.

## Final Comment-Audit Inventory

RS5 must audit:

- all 72 source-bearing files in the complete table above;
- all 25 external include consumers listed in the external census;
- every new source-bearing packet/composer/validation file created by the
  named splits;
- project/tool source files changed for path ownership, including allocation
  allowlist/checker path maps and dependency-validator rules.

The starting source inventory is therefore 97 existing files plus every new
source-bearing split file. RS5 must regenerate it with `git ls-files`, record
one checklist row per final file, and reconcile checked/deferred counts.

## Validation

RS0 changes documentation only. Per the plan and repository validation map, no
repository validation was required or run.


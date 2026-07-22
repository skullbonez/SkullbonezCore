# Replay Subsystem Consolidation RC0 — Census And Domain Map

Date: 2026-07-22

## Scope And Method

The inventory uses tracked files, not a directory glance:

```powershell
git ls-files 'SkullbonezSource/Runtime/Replay/*'
rg -n '^#include[[:space:]]+[<"].*(Runtime/Replay/|Runtime\\Replay\\|Replay[A-Za-z0-9_.-]*\.h)' SkullbonezSource SkullbonezTests -g '!SkullbonezSource/Runtime/Replay/**'
```

CodeGraph mapped the oversized owners and their call paths before targeted
source inspection. The final baseline is 44 files / 34,768 physical lines.
The plan's registration count of 34,735 was stale by 33 lines and is replaced
by this final-source measurement.

## Six-Domain File Assignment

Every tracked Replay file has one primary owning domain. Public packets remain
physically beside their current implementation until RC4, but their assignment
names the domain that owns their vocabulary.

| Lines | Domain | File | Primary responsibility |
|---:|---|---|---|
| 451 | Prediction | `ReplayAuthoring.h` | Prediction-affecting velocity edits and branch/cause authoring state; RC3 must peel presentation-only cause rows away. |
| 1,290 | Presentation | `ReplayAuthoringCauseTree.cpp` | Cause-row selection, focus, window input, and presentation behavior. |
| 893 | Prediction | `ReplayAuthoringVelocity.cpp` | Velocity mutation policy and prediction invalidation. |
| 401 | Timeline | `ReplayCoordination.h` | Public frame-scoped commands/results for timeline, workspace, reset, and startup coordination. |
| 154 | Capture | `ReplayEventCommand.h` | Fixed value command recorded into the event stream. |
| 68 | Timeline | `ReplayIdentity.h` | Replay-wide frame/capacity value vocabulary. |
| 506 | Presentation | `ReplayOverlayLayout.cpp` | Shared hit-test/draw geometry. |
| 182 | Presentation | `ReplayOverlayLayout.h` | Overlay surface and geometry declarations. |
| 1,164 | Presentation | `ReplayOverlayRenderer.cpp` | Late-pass overlay draw submission. |
| 157 | Presentation | `ReplayOverlayRenderer.h` | Overlay draw entry points and frame-local view. |
| 4,488 | Prediction | `ReplayPrediction.cpp` | Scheduling, isolated simulation, release/acquire publication, trajectory construction, and prediction owner operations. |
| 553 | Prediction | `ReplayPrediction.h` | Prediction owner state and worker/publication contracts. |
| 73 | Validation | `ReplayPredictionArchive.Automation.cpp` | Automation-only prediction archive round-trip oracle. |
| 734 | ArtifactIO | `ReplayPredictionArchive.cpp` | Bounded typed prediction-state codec. |
| 60 | ArtifactIO | `ReplayPredictionArchive.h` | Prediction artifact payload API. |
| 2,084 | Presentation | `ReplayPredictionDrawing.cpp` | Immutable future/past path and causal overlay draw submission. |
| 85 | Prediction | `ReplayPredictionReserve.cpp` | Prediction working-set reserve registration and requests. |
| 46 | Prediction | `ReplayPredictionReserve.h` | Prediction reserve wrapper contract. |
| 91 | Prediction | `ReplayPredictionScheduling.h` | Allocation-free scheduling decisions. |
| 129 | Prediction | `ReplayPredictionView.h` | Immutable prediction publication values. |
| 1,291 | Presentation | `ReplayPresentation.cpp` | Visual selection, path/focus state, and renderer packet publication. |
| 491 | Presentation | `ReplayPresentation.h` | Presentation owner plus currently mixed public visual values. |
| 281 | Validation | `ReplayProbeState.h` | Debug startup/probe state and bounded failure reporting. |
| 3,617 | Capture | `ReplayRecorder.cpp` | Presentation/solver/event capture, retention, delta materialization, hashes, and chronological export. |
| 787 | Capture | `ReplayRecorder.h` | Capture records and the three recorder owners. |
| 299 | Timeline | `ReplayRestoreService.h` | Synchronous application of retained solver samples to live owners. |
| 84 | Timeline | `ReplayRestoreTransactions.h` | Public frame-scoped restore transactions. |
| 152 | Capture | `ReplayRetainedMemory.h` | Three-owner reserve inventory and retained-data policy. |
| 1,562 | Timeline | `ReplayRuntime.cpp` | Public composition shell sequencing six internal domains. |
| 492 | Timeline | `ReplayRuntime.h` | Public Replay owner surface; currently carries excess interior declarations. |
| 473 | Timeline | `ReplayScrubber.h` | Scrub cursor and restore command values. |
| 1,815 | Timeline | `ReplayScrubberTools.cpp` | Scrub input, transport, inspection camera, and live-restore sequencing. |
| 391 | Timeline | `ReplayTimeline.cpp` | Recorder coordination, loading, events, and memory policy. |
| 284 | Timeline | `ReplayTimeline.h` | Timeline owner of recorder/loading/event state. |
| 2,940 | ArtifactIO | `ReplayV2Artifact.cpp` | Versioned chunk document codec, validation, save, and selective load. |
| 156 | ArtifactIO | `ReplayV2Artifact.h` | Cold artifact operations and result values. |
| 64 | Validation | `ReplayValidation.Internal.h` | Debug-only shared probe constants/helpers. |
| 1,884 | Validation | `ReplayValidation.Probes.cpp` | Debug scrub/save/load/failure/branch/visual probes. |
| 1,936 | Validation | `ReplayValidation.cpp` | Startup diagnostics and target-restore verification. |
| 1,011 | Presentation | `ReplayVisualPacket.h` | Frame-local typed renderer/validation packet. |
| 611 | Presentation | `ReplayVisualPacketFingerprint.cpp` | Canonical typed/render-buffer fingerprint. |
| 103 | Presentation | `ReplayVisualPacketFingerprint.h` | Fingerprint result and operation declaration. |
| 328 | Prediction | `TrajectoryStore.cpp` | Bounded mutable trajectory records with published prefixes. |
| 107 | Prediction | `TrajectoryStore.h` | Trajectory record/store values and mutation API. |

Domain totals: Capture 4 files, Timeline 10, Prediction 10, ArtifactIO 4,
Presentation 11, Validation 5; total 44.

## Oversized And Near-Target Translation Units

| File | Lines | Concrete responsibilities | Owning phase |
|---|---:|---|---|
| `ReplayPrediction.cpp` | 4,488 | frame-thread scheduling/cancellation; worker operation and private-engine initialization/stepping; release/acquire published-prefix promotion; future causal-node building; future and retained-past trajectory mutation; reveal/divergence/presentation caches; public owner commands and memory accounting | RC2 splits scheduling, isolated simulation, and publication. Trajectory helpers remain Prediction but move beside their store. |
| `ReplayRecorder.cpp` | 3,617 | reserve registration/helpers; deterministic packing/hash helpers; owner-event command builders; presentation capture/delta/keyframe ring; solver capture/delta/keyframe ring; event ring; chronological export; hash-log file output | RC1 separates live Capture from ArtifactIO export/serialization authority while preserving the three capture owners. |
| `ReplayV2Artifact.cpp` | 2,940 | wire constants/rows; scalar/chunk encoders; manifest/body/presentation/hash/checkpoint/event/visual chunk writers; file document assembly; document/chunk validation; selective loaders and v2/v3 migration | RC1 makes ArtifactIO the only format/file owner and splits internal codec/document units below the target. |
| `ReplayPredictionDrawing.cpp` | 2,084 | immutable path traversal/thinning; future ribbons/markers; retained-past ribbons; cause/contact focus geometry; submission quotas; `ReplayPresentation` draw entry points | RC3 separates data selection from draw submission and removes duplicate selection. |
| `ReplayValidation.cpp` | 1,936 | startup configuration, artifact load/restore verification, event replay, target stepping, and product operation adapters | RC5 placement review; size is explicitly not judged. |
| `ReplayValidation.Probes.cpp` | 1,884 | Debug probe state machines for scrub, restore, save/load, failures, branching, and visual reconstruction | RC5 placement review; size is explicitly not judged. |
| `ReplayScrubberTools.cpp` | 1,815 | scrub selection, pointer/transport policy, inspection camera transitions, save command, restore transaction orchestration, and scene reset | Timeline cohesion is plausible at RC0; RC6 rechecks after public-surface diet. |

## `TrajectoryStore` Decision

`TrajectoryStore` belongs to **Prediction**, not Timeline:

- `ReplayPrediction` is its only mutable owner (`RunReplayPredictionState` owns
  the store and `ReplayPrediction.cpp` performs every record/point mutation).
- both future prediction and retained-past derived paths use the same
  release/published-prefix invariant;
- all growth routes through `replay_prediction_working_set`, never a Timeline
  reserve owner; and
- Timeline provides solver samples as immutable input and retains no trajectory
  storage or mutation entry point.

Presentation may select and draw published trajectory values, but that read-only
consumption does not move storage ownership.

## Non-Replay Production Include Surface

There are 48 production include edges from 25 non-Replay files to 17 Replay
headers. RC0 classifies five headers as legitimate public owner/packet surface:
`ReplayRuntime.h`, `ReplayCoordination.h`, `ReplayEventCommand.h`,
`ReplayRestoreTransactions.h`, and `ReplayVisualPacket.h`. The other twelve are
interior leaks to remove or split by RC4.

| Replay header | Non-Replay consumers | RC0 classification / target |
|---|---|---|
| `ReplayAuthoring.h` | `Editor/EditorTools.h` | Interior leak. Publish a bounded authoring value/command packet; keep mutable authoring state private. |
| `ReplayCoordination.h` | `InteractionAutomationController.h` | Allowed typed command/result packet; RC4 verifies it carries no owner state. |
| `ReplayEventCommand.h` | `InputRouter.h`; `Tools/RuntimeTools.h` | Allowed fixed value packet. |
| `ReplayOverlayLayout.h` | `InputFrame.cpp`; `InputFrameExecution.cpp`; `InteractionAutomationController.cpp`; `InteractionAutomationReportWriter.cpp`; `Run.cpp`; `RunInput.cpp` | Interior Presentation logic. Publish one overlay-surface/action value packet and keep geometry operations inside Presentation. |
| `ReplayOverlayRenderer.h` | `DevelopmentTools/ImGuiEditorCausalityProjection.h`; `Render/RuntimeRenderer.cpp`; `RunFrame.cpp`; `UI/OperatorEditorFrameComposer.cpp`; `UiTextPass.cpp` | Interior draw API. Consumers should accept ReplayRuntime/Presentation render packets through existing render/UI seams. |
| `ReplayPrediction.h` | `InteractionAutomationReportWriter.cpp`; `Render/RuntimeRenderPasses.cpp` | Interior owner leak. Replace with `ReplayPredictionView.h` values carried by automation/render packets. |
| `ReplayPredictionArchive.h` | `InteractionAutomationReportWriter.cpp` | Interior ArtifactIO codec. Route typed archive build/load through ReplayRuntime validation commands/results. |
| `ReplayPredictionScheduling.h` | `InteractionAutomationReportWriter.h` | Interior policy. Publish the scheduling mode/facts needed by the report in the automation view. |
| `ReplayPresentation.h` | `InteractionAutomationReportWriter.cpp`; `Render/RuntimeRenderHost.h`; `UiTextPass.cpp` | Mixed owner/value header. Split public render/HUD/selection packets from the private `ReplayPresentation` owner. |
| `ReplayRecorder.h` | `RuntimeDiagnostics.cpp`; `Startup/StartupCommandLine.h`; `Tools/RuntimeTools.cpp` | Interior Capture owners/records. Replace with ReplayRuntime config/HUD calls and event/value packets. |
| `ReplayRestoreService.h` | `InputFrame.cpp`; `InputFrameExecution.cpp`; `Run.cpp`; `RunFrame.cpp`; `RunInput.cpp` | Interior Timeline implementation. Keep public restore transactions, apply through ReplayRuntime. |
| `ReplayRestoreTransactions.h` | `InputFrame.cpp`; `InputFrameExecution.cpp`; `Run.cpp`; `RunFrame.cpp`; `RunInput.cpp` | Allowed synchronous typed transaction packet; RC4 verifies no implementation owner escapes. |
| `ReplayRuntime.h` | `InputFrame.h`; `Run.h`; `RuntimeStressController.cpp`; `Scene/RunScene.cpp` | Allowed public owner surface. RC4 diets interior types from it. |
| `ReplayScrubber.h` | `InteractionAutomationReportWriter.h` | Interior Timeline owner mixed with values. Move report-facing scrub facts to a public packet. |
| `ReplayV2Artifact.h` | `InteractionAutomationReportWriter.cpp`; `Run.cpp`; `RunFrame.cpp` | Interior ArtifactIO service. Replace with ReplayRuntime typed save/load commands/results. |
| `ReplayVisualPacket.h` | `InteractionAutomationReportWriter.h`; `Render/RuntimeRenderPasses.cpp`; `Tools/RuntimeTools.h` | Allowed typed presentation packet. |
| `ReplayVisualPacketFingerprint.h` | `InteractionAutomationController.h`; `InteractionAutomationReportWriter.h` | Validation/diagnostic operation leak. Publish fingerprint values through the automation/validation packet and keep the operation internal. |

The 25 consumers are all Runtime peers; there is no downward Replay include in
Physics, Rendering, Scene, World, or Core. Rendering here means the Runtime
composition layer under `Runtime/Render`, not `SkullbonezSource/Rendering`.

## Direct Test Include Surface

Tests contribute 14 additional edges from 10 test files to 10 Replay headers:

| Header | Test consumers |
|---|---|
| `ReplayCoordination.h` | `TestReplayRecorder.cpp` |
| `ReplayOverlayLayout.h` | `TestRuntimeValueSeams.cpp` |
| `ReplayPredictionScheduling.h` | `TestReplayPredictionScheduling.cpp` |
| `ReplayRecorder.h` | `TestDeterminism.cpp`; `TestOwnerRequestQueues.cpp`; `TestReplayRecorder.cpp`; `TestReplayRecorderFullCaptureBoundary.cpp` |
| `ReplayRestoreService.h` | `TestPhysicsHandles.cpp` |
| `ReplayRetainedMemory.h` | `TestReplayRecorder.cpp` |
| `ReplayTimeline.h` | `TestCoverageFloorContracts.cpp` |
| `ReplayV2Artifact.h` | `TestCoverageFloorContracts.cpp`; `TestReplayArtifact.cpp` |
| `ReplayVisualPacket.h` | `TestReplayVisualPacket.cpp` |
| `ReplayVisualPacketFingerprint.h` | `TestReplayVisualPacket.cpp` |

These are white-box domain tests rather than product public-surface authority.
They move with their owners during RC1-RC5 and do not justify a production leak.

## Reserve-Allocator Inventory

RC0 records exactly the existing three registered owners; no name, cap, phase,
counter source, or exhaustion rule changes in this documentation slice.

| Owner | Registering domain | Phase | Hard cap | Recorded high-water | Exhaustion |
|---|---|---|---:|---:|---|
| `replay_recorder_samples` | Capture | Replay | 32 MiB | 17,737,640 B | Fatal retained-state failure |
| `replay_solver_snapshot` | Physics value snapshot consumed by Timeline | Replay | 8 MiB | 2,877,186 B | Fatal retained-state failure |
| `replay_prediction_working_set` | Prediction, including `TrajectoryStore` | Replay | 256 MiB | 110,979,828 B | Cancel/truncate prediction build |

## Sequencing Decisions For RC1-RC6

- RC1 moves chronological export, prediction payload codec ownership, artifact
  wire rows, and all file I/O under ArtifactIO; Capture retains live rings,
  committed-tick sampling, retention, hashes, and event capture.
- RC2 creates explicit Prediction scheduling, isolated-simulation, and
  publication owners. The release/acquire prefix and cancellation wait each
  have one source of truth.
- RC3 establishes Presentation selection versus draw submission and moves the
  cause-tree selection/layout duplication to one side of that seam.
- RC4 targets the twelve production leaks above and diets `ReplayRuntime.h`;
  the five provisional public headers are re-audited, not grandfathered.
- RC5 keeps Debug probes wholly behind Validation configuration and removes any
  include edge from Capture/Prediction hot paths if discovered after splits.
- RC6 repeats every count from final source and requires justification for any
  remaining >2,000-line translation unit.

## Validation And Blockers

RC0 is documentation-only; repository validation is not required or run.
The OF2 one-generation visual-fidelity provenance mismatch remains a
non-stopping campaign blocker: expected config SHA
`83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`, actual
`bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`.
It authorizes neither config nor golden changes. RC1 must still invoke the
single mega gate once and record the same blocker if provenance fails again.

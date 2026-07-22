# Replay Subsystem Consolidation Closure

Date: 2026-07-22
Branch: `nightrunner`
Result: Complete - RC0-RC6, 7/7

## Outcome

Replay now reads as six named interior domains behind `ReplayRuntime` and typed
value/command packets. Prediction scheduling, publication, and topology;
ArtifactIO/hash logging; Presentation selection/submission; and configuration-
specific Validation are physically named. The production include surface fell
from 48 to 33 edges (31.25%) with every remaining implementation edge carrying
an owner, reason, and deletion condition.

No behavior, config, golden, baseline, reserve owner, cap, phase gate, or
counter policy changed. The single required independent review found no
blocking ownership or policy issue and accepted the three concrete cohesion
exceptions recorded below.

## Final Six-Domain Census

The final inventory uses `git ls-files`, not a directory glance: 64 tracked
Replay files / 36,332 physical lines. Every file is assigned exactly once.

| Domain | Files | Lines | Boundary |
|---|---:|---:|---|
| ArtifactIO | 7 | 4,212 | Cold format, codec, materialization, and hash-log authority |
| Capture | 7 | 4,628 | Bounded presentation/solver/event capture and retained-memory policy |
| Prediction | 16 | 8,110 | Isolated future, scheduling, publication, topology, and trajectories |
| Presentation | 16 | 8,529 | Frame selection, path/cause projection, overlay and render submission |
| Timeline | 11 | 5,901 | Public composition, scrub/restore, retained ordering, and coordination |
| Validation | 7 | 4,952 | Debug/Automation probes, fingerprints, and product restore verification |
| **Total** | **64** | **36,332** | **Every tracked file assigned once** |

| Lines | Domain | File |
|---:|---|---|
| 168 | ArtifactIO | `ReplayArtifactHashLog.cpp` |
| 52 | ArtifactIO | `ReplayArtifactHashLog.h` |
| 43 | ArtifactIO | `ReplayArtifactSource.h` |
| 734 | ArtifactIO | `ReplayPredictionArchive.cpp` |
| 60 | ArtifactIO | `ReplayPredictionArchive.h` |
| 2,999 | ArtifactIO | `ReplayV2Artifact.cpp` |
| 156 | ArtifactIO | `ReplayV2Artifact.h` |
| 28 | Capture | `ReplayCaptureLimits.h` |
| 53 | Capture | `ReplayCapturePackets.h` |
| 154 | Capture | `ReplayEventCommand.h` |
| 3,448 | Capture | `ReplayRecorder.cpp` |
| 735 | Capture | `ReplayRecorder.h` |
| 152 | Capture | `ReplayRetainedMemory.h` |
| 58 | Capture | `ReplayToolPackets.h` |
| 377 | Prediction | `ReplayAuthoring.h` |
| 893 | Prediction | `ReplayAuthoringVelocity.cpp` |
| 2,083 | Prediction | `ReplayPrediction.cpp` |
| 544 | Prediction | `ReplayPrediction.h` |
| 34 | Prediction | `ReplayPredictionPackets.h` |
| 1,374 | Prediction | `ReplayPredictionPublication.cpp` |
| 68 | Prediction | `ReplayPredictionPublication.h` |
| 101 | Prediction | `ReplayPredictionPublicationOperations.h` |
| 85 | Prediction | `ReplayPredictionReserve.cpp` |
| 46 | Prediction | `ReplayPredictionReserve.h` |
| 117 | Prediction | `ReplayPredictionScheduling.cpp` |
| 170 | Prediction | `ReplayPredictionScheduling.h` |
| 1,653 | Prediction | `ReplayPredictionTopologyPublication.cpp` |
| 130 | Prediction | `ReplayPredictionView.h` |
| 328 | Prediction | `TrajectoryStore.cpp` |
| 107 | Prediction | `TrajectoryStore.h` |
| 1,290 | Presentation | `ReplayAuthoringCauseTree.cpp` |
| 103 | Presentation | `ReplayAuthoringPackets.h` |
| 387 | Presentation | `ReplayCauseFocusSubmission.cpp` |
| 506 | Presentation | `ReplayOverlayLayout.cpp` |
| 174 | Presentation | `ReplayOverlayLayout.h` |
| 112 | Presentation | `ReplayOverlayPackets.h` |
| 1,164 | Presentation | `ReplayOverlayRenderer.cpp` |
| 95 | Presentation | `ReplayOverlayRenderer.h` |
| 40 | Presentation | `ReplayOverlaySurface.h` |
| 77 | Presentation | `ReplayPathPackets.h` |
| 1,752 | Presentation | `ReplayPredictionDrawing.cpp` |
| 1,291 | Presentation | `ReplayPresentation.cpp` |
| 416 | Presentation | `ReplayPresentation.h` |
| 64 | Presentation | `ReplayPresentationPackets.h` |
| 47 | Presentation | `ReplayPresentationSubmission.h` |
| 1,011 | Presentation | `ReplayVisualPacket.h` |
| 401 | Timeline | `ReplayCoordination.h` |
| 68 | Timeline | `ReplayIdentity.h` |
| 288 | Timeline | `ReplayRestoreService.h` |
| 97 | Timeline | `ReplayRestoreTransactions.h` |
| 1,568 | Timeline | `ReplayRuntime.cpp` |
| 492 | Timeline | `ReplayRuntime.h` |
| 404 | Timeline | `ReplayScrubber.h` |
| 1,815 | Timeline | `ReplayScrubberTools.cpp` |
| 383 | Timeline | `ReplayTimeline.cpp` |
| 288 | Timeline | `ReplayTimeline.h` |
| 97 | Timeline | `ReplayTimelinePackets.h` |
| 73 | Validation | `ReplayPredictionArchive.Automation.cpp` |
| 281 | Validation | `ReplayProbeState.h` |
| 1,936 | Validation | `ReplayValidation.cpp` |
| 64 | Validation | `ReplayValidation.Internal.h` |
| 1,884 | Validation | `ReplayValidation.Probes.cpp` |
| 611 | Validation | `ReplayVisualPacketFingerprint.cpp` |
| 103 | Validation | `ReplayVisualPacketFingerprint.h` |

## Oversized Cohesion Exceptions

The single independent rubber-duck review accepted all three final files near
or above the approximate 2,000-line target:

| File | Lines | Accepted cohesion justification | Reopen condition |
|---|---:|---|---|
| `ReplayRecorder.cpp` | 3,448 | One Capture policy: synchronized bounded presentation, solver, and event tracks share the retention window, aggregate reserve gate, compact-delta reconstruction, deterministic hashes, and committed-state observation. Artifact materialization and file streams are gone. | This is the weakest exception. Split physically if any track acquires a divergent retention, reserve, or lifecycle policy. |
| `ReplayV2Artifact.cpp` | 2,999 | One stateless v2-v4 wire-format authority owns paired encoders/decoders, byte ABI/version migration, document assembly/validation, selective loading, and cold materialization. | Reopen only if a new format owner or independent lifecycle appears; a mechanical encoder/decoder split alone does not improve ownership. |
| `ReplayPrediction.cpp` | 2,083 | One isolated private-engine future and its frame-thread job lifecycle remain. Task/cancellation, release/acquire publication, and topology publication have separate named units. | Reopen if core regains task storage, prefix publication, or topology authority. |

The reviewer found no callback pack, `void*`, hot-path inheritance, migration
facade, mutable Replay owner accessor, downward Replay include, or Validation
include into Capture/Prediction. Verdict: no blocking findings.

## Final Public Surface

Production has 33 Replay include edges from 25 non-Replay files to 20 headers,
down from RC0's 48 edges. Twenty-six edges consume `ReplayRuntime` or typed
packets/commands. Seven explicit concrete survivors remain:

- Presentation: `UiTextPass.cpp` -> `ReplayOverlayRenderer.h`.
- Diagnostics: `RuntimeDiagnostics.cpp` -> `ReplayRecorder.h`.
- Automation Validation: `InteractionAutomationReportWriter.cpp` ->
  `ReplayPrediction.h`, `ReplayPredictionArchive.h`, `ReplayPresentation.h`,
  and `ReplayV2Artifact.h`.
- Automation Validation: `InteractionAutomationReportWriter.h` ->
  `ReplayVisualPacketFingerprint.h`.

The full owner/reason/deletion-condition table is in the RC4 report. RC5 proves
the five Automation survivors do not enter Profile/WPO/Release. The two product
survivors retain explicit deletion conditions. `ReplayRuntime.h` also records
its inline concrete-owner composition exception: fixed allocation-free owner
lifetime without PImpl heap, callback pack, service bag, or mutable owner
accessors.

White-box tests have 17 direct edges from 10 files to 12 Replay headers. They
exercise domain internals and do not expand the production surface.

## Replay Reserve Inventory And Strict Proof

Exactly the existing three Replay reserve owners remain; no registration, cap,
phase gate, high-water counter, growth counter coverage, or exhaustion policy
changed.

| Owner | Phase | Hard cap | RC6 high-water | Growths | Failed growths |
|---|---|---:|---:|---:|---:|
| `replay_recorder_samples` | Replay | 32 MiB | 16,270,263 B | 887 | 0 |
| `replay_solver_snapshot` | Replay | 8 MiB | 2,877,186 B | 2 | 0 |
| `replay_prediction_working_set` | Replay | 256 MiB | 110,947,060 B | 8,589 | 0 |

The strict gate completed frame 180 with
`predictionGenerationCount=2`, zero steady-gameplay allocation violations,
zero reserve-policy violations, zero failed Replay growths, and unchanged
trajectory reserve events across submitted steady-state presentation
(9,478 -> 9,478).

## Campaign Comment Audit Checklist

Scope source: `git diff --name-only 123cbf56..44487469` filtered to tracked
source/tool extensions. Checklist path: this report. Checked: 61. Deferred: 0.
Unchecked: none. Slice checklists were reconciled against the final inventory;
the closure pass also verified Purpose, Summary, Invariants, and Related
learning-header sections and the local ownership/lifetime/hazard comments.

- [x] `Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h`
- [x] `Runtime/Editor/EditorTools.h`
- [x] `Runtime/InputFrame.cpp`
- [x] `Runtime/InputFrameExecution.cpp`
- [x] `Runtime/InteractionAutomationController.cpp`
- [x] `Runtime/InteractionAutomationController.h`
- [x] `Runtime/InteractionAutomationReportWriter.cpp`
- [x] `Runtime/InteractionAutomationReportWriter.h`
- [x] `Runtime/Render/RuntimeRenderHost.h`
- [x] `Runtime/Render/RuntimeRenderPasses.cpp`
- [x] `Runtime/Render/RuntimeRenderer.cpp`
- [x] `Runtime/Replay/ReplayArtifactHashLog.cpp`
- [x] `Runtime/Replay/ReplayArtifactHashLog.h`
- [x] `Runtime/Replay/ReplayArtifactSource.h`
- [x] `Runtime/Replay/ReplayAuthoring.h`
- [x] `Runtime/Replay/ReplayAuthoringPackets.h`
- [x] `Runtime/Replay/ReplayCaptureLimits.h`
- [x] `Runtime/Replay/ReplayCapturePackets.h`
- [x] `Runtime/Replay/ReplayCauseFocusSubmission.cpp`
- [x] `Runtime/Replay/ReplayOverlayLayout.h`
- [x] `Runtime/Replay/ReplayOverlayPackets.h`
- [x] `Runtime/Replay/ReplayOverlayRenderer.h`
- [x] `Runtime/Replay/ReplayOverlaySurface.h`
- [x] `Runtime/Replay/ReplayPathPackets.h`
- [x] `Runtime/Replay/ReplayPrediction.cpp`
- [x] `Runtime/Replay/ReplayPrediction.h`
- [x] `Runtime/Replay/ReplayPredictionDrawing.cpp`
- [x] `Runtime/Replay/ReplayPredictionPackets.h`
- [x] `Runtime/Replay/ReplayPredictionPublication.cpp`
- [x] `Runtime/Replay/ReplayPredictionPublication.h`
- [x] `Runtime/Replay/ReplayPredictionPublicationOperations.h`
- [x] `Runtime/Replay/ReplayPredictionScheduling.cpp`
- [x] `Runtime/Replay/ReplayPredictionScheduling.h`
- [x] `Runtime/Replay/ReplayPredictionTopologyPublication.cpp`
- [x] `Runtime/Replay/ReplayPredictionView.h`
- [x] `Runtime/Replay/ReplayPresentation.h`
- [x] `Runtime/Replay/ReplayPresentationPackets.h`
- [x] `Runtime/Replay/ReplayPresentationSubmission.h`
- [x] `Runtime/Replay/ReplayRecorder.cpp`
- [x] `Runtime/Replay/ReplayRecorder.h`
- [x] `Runtime/Replay/ReplayRestoreService.h`
- [x] `Runtime/Replay/ReplayRestoreTransactions.h`
- [x] `Runtime/Replay/ReplayRuntime.cpp`
- [x] `Runtime/Replay/ReplayRuntime.h`
- [x] `Runtime/Replay/ReplayScrubber.h`
- [x] `Runtime/Replay/ReplayTimeline.cpp`
- [x] `Runtime/Replay/ReplayTimeline.h`
- [x] `Runtime/Replay/ReplayTimelinePackets.h`
- [x] `Runtime/Replay/ReplayToolPackets.h`
- [x] `Runtime/Replay/ReplayV2Artifact.cpp`
- [x] `Runtime/Run.cpp`
- [x] `Runtime/RunFrame.cpp`
- [x] `Runtime/RunInput.cpp`
- [x] `Runtime/Startup/StartupCommandLine.h`
- [x] `Runtime/Tools/RuntimeTools.cpp`
- [x] `Runtime/UI/OperatorEditorFrameComposer.cpp`
- [x] `Runtime/UiTextPass.cpp`
- [x] `SkullbonezTests/TestReplayPredictionScheduling.cpp`
- [x] `SkullbonezTests/TestReplayRecorder.cpp`
- [x] `SkullbonezTests/TestRuntimeValueSeams.cpp`
- [x] `tools/validate_project_filters.py`

## Final Validation

The desktop shell could not open a separate visible console, so commands ran
in the app shell and their output was captured there.

| Command | Time | Result |
|---|---:|---|
| allocation self-test + repository scan | 9.6 s | PASS; 426 files, zero allowlist errors |
| dependency-direction + Replay-boundary proofs | 0.3 s | PASS; zero rows |
| project/filter metadata proof | 3.0 s | PASS; 742/742, zero errors |
| `tools\validate_replay_allocation_policy.bat` | 4.3 s | PASS; two generations, frame 180, zero gameplay/policy violations |
| independent rubber-duck review | N/A | PASS; no blocking findings, three cohesion exceptions accepted |
| `tools\validate_replay_visual_fidelity.bat` | 415.5 s | BLOCKED after launcher/typed controls by standing provenance mismatch |
| `tools\validate_full.bat` | 104.4 s | PASS; CPU/coverage, five runtime lanes, accepted DX12 images, zero DX12 errors, byte-exact physics |

The one and only RC6 mega invocation proved `engine_processes=1`,
`prediction_starts=1`, `presented_cascades=1`, and `nested_scrub_runs=0`;
typed controls passed 16 cases / 72 assertions. It then reached the unchanged
config provenance mismatch:

- expected: `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
- actual: `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`

No RC phase retried its mega invocation and no config, golden, baseline, or
artifact metadata was edited during the campaign. The mismatch remained a
recorded external certification blocker at the RC6 tip, but it did not stop
implementation, static proof, strict allocation proof, independent review, or
the passing final broad gate.

## Closure

RC0-RC6 are complete. The active TODO plan is removed under MASTER inventory
rule 4; this report and the six slice reports are the durable campaign record.

## Post-Closure Provenance Reconciliation

The user subsequently confirmed that commit `7543b1c8`'s config-format v6
change was valid and authorized a provenance-only update. The visual baseline's
config hash and the causal baseline's mechanically dependent visual-baseline
hash were reconciled; no behavioral golden field changed. The mapped replay
visual-fidelity gate then passed all 2,401 ticks and all negative controls in
435.3 seconds. Evidence:
[`replay-visual-fidelity-provenance-reconciliation`](replay-visual-fidelity-provenance-reconciliation.md).

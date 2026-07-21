# Replay Subsystem Consolidation RC4 - Header Diet

Date: 2026-07-22
Branch: `nightrunner`
Result: RC4 complete; campaign 5/7 (71%)

## Outcome

The production Replay include surface fell from 48 edges to 33 edges, a
31.25% reduction. The final 33 edges come from 25 non-Replay production files
and target 20 deliberately narrow headers. Twenty-six edges now consume
`ReplayRuntime`, typed packets, commands, coordination values, or bounded
restore transactions. Ten new packet headers carry values that previously
forced consumers to include mutable domain owners:

- `ReplayAuthoringPackets.h`, `ReplayCaptureLimits.h`,
  `ReplayCapturePackets.h`, `ReplayOverlayPackets.h`, and
  `ReplayOverlaySurface.h`;
- `ReplayPathPackets.h`, `ReplayPredictionPackets.h`,
  `ReplayPresentationPackets.h`, `ReplayTimelinePackets.h`, and
  `ReplayToolPackets.h`.

Concrete restore sample state moved from `ReplayRestoreService` into the
bounded `ReplayRestoreTransactions` command packet. Five external consumers no
longer include the mutable restore owner. Presentation/render/authoring/tool
consumers similarly include packet headers rather than owner headers.

## Public Surface Census

| Class | Edges | Review result |
|---|---:|---|
| `ReplayRuntime` | 4 | Approved public owner |
| Typed packet/command/coordination headers | 22 | Approved bounded value surface |
| Concrete implementation/validation survivors | 7 | Explicit exceptions below |
| **Total** | **33** | Down from 48 |

The seven direct implementation survivors are explicit rather than hidden by
forwarders or broad context bags:

| Consumer -> header | Owner | Reason | Deletion condition / review evidence |
|---|---|---|---|
| `UiTextPass.cpp` -> `ReplayOverlayRenderer.h` | Presentation | The text pass invokes the concrete allocation-free overlay draw operation. | Delete when overlay submission is wholly sequenced behind a typed `ReplayRuntime`/Presentation render seam without callbacks. |
| `RuntimeDiagnostics.cpp` -> `ReplayRecorder.h` | Validation/Diagnostics | Cold diagnostic logging reads concrete recorder sample rows. | Packetize scrub/restore diagnostic samples behind a bounded diagnostic view. |
| `InteractionAutomationReportWriter.cpp` -> `ReplayPrediction.h` | Validation | Automation-only fidelity projection exercises concrete Prediction results. | Replace with a `ReplayRuntime` validation command/result that performs the projection internally. RC5 verifies the automation gate. |
| `InteractionAutomationReportWriter.cpp` -> `ReplayPredictionArchive.h` | Validation | Automation-only archive round-trip evidence needs the concrete archive operation. | Same Runtime validation command/result deletion condition; RC5 verifies placement. |
| `InteractionAutomationReportWriter.cpp` -> `ReplayPresentation.h` | Validation | Automation reconstructs presentation values for offline fidelity comparison. | Same Runtime validation command/result deletion condition; RC5 verifies placement. |
| `InteractionAutomationReportWriter.cpp` -> `ReplayV2Artifact.h` | Validation | Automation loads the persisted artifact for durability/fidelity comparison. | Same Runtime validation command/result deletion condition; RC5 verifies placement. |
| `InteractionAutomationReportWriter.h` -> `ReplayVisualPacketFingerprint.h` | Validation | The report contract carries the deterministic fingerprint value/operation. | Split fingerprint values from construction or publish them through the Runtime validation view. RC5 verifies placement. |

`ReplayRuntime.h` still includes its concrete interior owner headers because it
stores the owners inline by value. ReplayRuntime owns this composition seam.
Inline ownership preserves fixed, allocation-free lifetime and avoids a PImpl
heap, opaque service bag, callbacks, or forwarding facade. The deletion
condition is a fixed stable-storage composition design with the same explicit
ownership and no heap/context-bag regression. Current review evidence is that
ReplayRuntime exposes no mutable owner accessors: external calls and returns
are commands or value packets.

## Ownership And Policy Proof

- The three reserve registrations, caps, Replay phase gates, counters, and
  allocation allowlist rows are unchanged.
- Allocation self-test and repository scan pass over 426 files with zero
  allowlist errors.
- Dependency-direction and downward Replay-include proofs return zero rows.
- Project/filter metadata matches all 742 production entries with zero errors.
- No forwarding header, compatibility alias, callback pack, context/service
  bag, exception path, or hot-path polymorphic interface was introduced.

## Comment Audit Checklist

Checklist path: this report. Checked: 39. Deferred: 0. Unchecked: none.

- [x] `DevelopmentTools/ImGuiEditorCausalityProjection.h`
- [x] `Editor/EditorTools.h`
- [x] `InputFrame.cpp`
- [x] `InputFrameExecution.cpp`
- [x] `InteractionAutomationController.cpp`
- [x] `InteractionAutomationController.h`
- [x] `InteractionAutomationReportWriter.cpp`
- [x] `InteractionAutomationReportWriter.h`
- [x] `Render/RuntimeRenderHost.h`
- [x] `Render/RuntimeRenderPasses.cpp`
- [x] `Render/RuntimeRenderer.cpp`
- [x] `Replay/ReplayAuthoring.h`
- [x] `Replay/ReplayAuthoringPackets.h`
- [x] `Replay/ReplayCaptureLimits.h`
- [x] `Replay/ReplayCapturePackets.h`
- [x] `Replay/ReplayOverlayLayout.h`
- [x] `Replay/ReplayOverlayPackets.h`
- [x] `Replay/ReplayOverlayRenderer.h`
- [x] `Replay/ReplayOverlaySurface.h`
- [x] `Replay/ReplayPathPackets.h`
- [x] `Replay/ReplayPredictionPackets.h`
- [x] `Replay/ReplayPredictionScheduling.h`
- [x] `Replay/ReplayPredictionView.h`
- [x] `Replay/ReplayPresentation.h`
- [x] `Replay/ReplayPresentationPackets.h`
- [x] `Replay/ReplayRecorder.h`
- [x] `Replay/ReplayRestoreService.h`
- [x] `Replay/ReplayRestoreTransactions.h`
- [x] `Replay/ReplayScrubber.h`
- [x] `Replay/ReplayTimelinePackets.h`
- [x] `Replay/ReplayToolPackets.h`
- [x] `Run.cpp`
- [x] `RunFrame.cpp`
- [x] `RunInput.cpp`
- [x] `Startup/StartupCommandLine.h`
- [x] `Tools/RuntimeTools.cpp`
- [x] `UI/OperatorEditorFrameComposer.cpp`
- [x] `UiTextPass.cpp`
- [x] `tools/validate_project_filters.py`

The moved values retain their durable-identity, bounded-capacity,
publication, recorder-window, and lifetime comments. No vocabulary or hazard
is deferred.

## Validation Evidence

The desktop shell could not open a separate visible console, so commands ran
in the app shell and their output was captured there.

| Command | Time | Result |
|---|---:|---|
| Focused Profile solution build | 14.3 s | PASS; zero warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe` | 1.6 s | PASS; 344 cases / 68,699 assertions |
| allocation self-test + repository scan | 9.4 s | PASS; 426 files, zero allowlist errors |
| `tools\validate_fast.bat` | 62.9 s | PASS; format, metadata, Profile/Debug builds, zero warnings/errors |
| `tools\validate_replay_visual_fidelity.bat` | 422.5 s | BLOCKED after launcher/typed controls by standing provenance mismatch |
| `tools\validate_full.bat` | 109.5 s | PASS; CPU/coverage, five runtime lanes, accepted DX12 images, byte-exact physics |

The one and only RC4 mega invocation proved `engine_processes=1`,
`prediction_starts=1`, `presented_cascades=1`, and `nested_scrub_runs=0`;
typed controls passed 16 cases / 72 assertions. It then reached the unchanged
config provenance mismatch:

- expected: `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
- actual: `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`

The gate was not retried and no config, golden, baseline, or artifact metadata
was edited. The blocker is recorded and the campaign proceeds to RC5.

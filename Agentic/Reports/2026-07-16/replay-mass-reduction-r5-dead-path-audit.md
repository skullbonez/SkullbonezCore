# Replay Mass Reduction R5 — Dead-Path Audit And Owner Rulings

Date: 2026-07-16
Owner: replay
Task: `replay-mass-reduction` R5

## Outcome

The mechanical pass indexed 99 callable declarations in the 26 replay headers.
CodeGraph reported 17 with no production caller. Text verification corrected 13
reference-return/parser false negatives, retained two deliberate test seams, and
confirmed two zero-caller accessors for deletion. No artifact reader, probe,
configuration branch, CLI path, or migration path was deleted.

The binding ruling is conservative: DELETE only when both graph and text search
show zero callers and no compatibility/test contract. Otherwise KEEP. This is
the plan's owner-safe “no ruling, no deletion” rule applied row by row.

## Candidate Rulings

| Candidate | Indexed result | Verified evidence | Owner ruling |
|---|---|---|---|
| `ReplayAuthoring::CauseTree` | zero | Used by `ReplayRuntime.cpp`, authoring drawing, scrubber tools, and Debug probes. | **KEEP — production owner view.** |
| `ReplayAuthoring::VelocityEdit` | zero | Used throughout `ReplayAuthoringVelocity.cpp`, `ReplayRuntime.cpp`, and `ReplayScrubberTools.cpp`. | **KEEP — production owner view.** |
| `ReplayAuthoring::Branch` | zero | Used by replay capture, event submission, and production load result paths. | **KEEP — production owner view.** |
| `ReplayPrediction::State` | zero | Used by replay runtime/presentation/prediction coordination; reference-return spelling defeated the graph match. | **KEEP — production owner view.** |
| `ReplayPrediction::Enabled` | zero | Declaration/definition was its only textual match; callers use published view/state or the toggle API. | **DELETE — zero caller, no compatibility contract.** |
| `ReplayPrediction::HorizonSeconds` | zero | Declaration/definition was its only textual match; callers use the published prediction view/configuration path. | **DELETE — zero caller, no compatibility contract.** |
| `ReplayPresentation::PathVisualizer` | zero | Used by runtime, authoring, scrubber, automation, and probe code. | **KEEP — production publication.** |
| `ReplayProbeRunner::Startup` | zero | Used by `ReplayRuntime::RunStartupWorkflows`. | **KEEP — diagnostics workflow.** |
| `FindReplayGrowthOwnerPolicy` | test only | `TestReplayRecorder.cpp` verifies every fixed replay allocation-policy row is findable by owner name. | **KEEP — allocation-policy test seam.** |
| `ReplayTimeline::Presentation` | zero | Used by capture, scrub, artifact-save, status, and probe paths. | **KEEP — production recorder view.** |
| `ReplayTimeline::Solver` | zero | Used by capture, prediction, scrub, restore, artifact-save, status, and probes. | **KEEP — production recorder view.** |
| `ReplayTimeline::Events` | zero | Used by event capture/save/load coordination. | **KEEP — production event-recorder view.** |
| `ReplayTimeline::MemoryPolicy` | zero | Used by replay configuration and memory reporting. | **KEEP — production policy view.** |
| `ReplayTimeline::LoadedPresentation` | zero | Used by loaded-artifact scrub/render workflows. | **KEEP — production loaded-track view.** |
| `ReplayTimeline::RecordingConfigured` | zero | Used by runtime startup/configuration decisions. | **KEEP — production state query.** |
| `ReplayTimeline::RecordingEnabled` | zero | Used by runtime capture/input/presentation decisions. | **KEEP — production state query.** |
| `FindReplayVisualPacketDifference` | test only | `TestReplayVisualPacket.cpp` uses the exact field/bit comparator as a negative-control oracle. | **KEEP — visual-oracle test seam.** |

The only source edit removes eight inline lines from `ReplayPrediction.h`, which
is now 536 physical lines. There is no replacement spelling or forwarding API.

## Configuration And CLI Reachability

No unreachable replay configuration branch was found.

- `replay_prediction_instant_budget_ms` and
  `replay_prediction_probe_ticks` are registered in `Core/Config.cpp`, stored in
  `Core/Config.h`, and consumed by `ReplayPrediction.cpp` when choosing and
  measuring prediction build mode.
- `--replay`, `--replay-seconds`, load/save/hash options, scrub/restore probes,
  and their underscore aliases are registered by
  `StartupLaunchResolution.cpp`. `Run.cpp` passes resolved values into replay
  recording and startup workflows.
- Debug-only scrub/restore/save/load probe paths are reachable through their
  named CLI options and intentionally rejected outside their build/configuration
  boundary. R2's link separation is therefore configuration policy, not dead
  code.

## Artifact Migration Review

No superseded pre-V2 engine reader remains. The engine's binary artifact reader
accepts outer versions 2–4, writes version 4, rejects future versions, and
retains the version-dependent v2/v3 presentation layouts. The artifact checker
constructs a previous-version v3 fixture, loads it through the runtime, and
verifies version 5 rejection. `replay_query.py` explicitly rejects legacy JSON
instead of silently keeping a pre-V2 reader.

The solver snapshot's internal payload versions 1–2 are not alternative outer
artifact formats; they are still decoded inside supported binary artifacts.
Removing either would be a schema/compatibility change forbidden by R5. All
outer v2/v3 readers and internal snapshot branches are **KEEP — supported
migration interval**.

## Comment Quality Audit

Touched-file audit: 1/1 checked, 0 deferred, 0 unchecked.
`ReplayPrediction.h` retains its complete learning header and the deletion does
not stale its owner, publication, threading, lifetime, or allocation comments.
No comment edit was needed.

## Validation

The available shell was headless, so commands were mirrored to logs.

- Targeted `tools\validate_build.bat Profile`: passed with zero warnings/errors
  in 15.01 s. Transcript: 68,676 bytes, SHA-256
  `4FAA3E771DCFB6F950CA264E191FBAB275C187ADCBAE2E22B0AFAB99B57BC0FC`.
- `tools\validate_tests.bat`: passed 202/202 cases and 12,595/12,595 assertions
  in 3.52 s. Transcript: 660,776 bytes, SHA-256
  `8A52E536283BBB726BEACC33AE43FD051AFE30B538EEC3130FF2E767528BC35F`.
- R5's single `tools\validate_replay_visual_fidelity.bat` invocation passed in
  472.76 s: one engine process, one prediction generation, 2,401 ticks, 200
  moved, 187 toppled, 199 causal nodes, and all negative/determinism controls.
  Transcript: 77,056 bytes, SHA-256
  `6656854946213708D1A23D865B76ECDF4C7B6D74F73B15D8E0213DE939E81BC3`.

No golden, baseline, schema, provenance value, or generated artifact changed.

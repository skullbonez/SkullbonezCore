# Replay Subsystem Consolidation RC5 - Validation Placement

Date: 2026-07-22
Branch: `nightrunner`
Result: RC5 complete; campaign 6/7 (86%)

## Outcome

Replay probe implementation is already isolated by validation configuration,
and no misplaced include edge required a source change:

- `ReplayValidation.Probes.cpp` contains the legacy CLI scrub, save/load,
  restore, branch, failure, and visual probes. The production project compiles
  it only for `Debug|x64` and excludes it from Automation, Profile,
  Profile-WPO, and Release.
- `InteractionAutomationController.cpp`,
  `InteractionAutomationInputDriver.cpp`, and
  `InteractionAutomationReportWriter.cpp` compile only for
  `Automation|x64`. Their concrete Prediction/Presentation/ArtifactIO includes
  are therefore Validation-domain dependencies, not product or hot-path edges.
- `ReplayPredictionArchive.Automation.cpp` compiles only for Automation.
  `ReplayVisualPacketFingerprint.cpp` compiles only for Debug and Automation;
  Profile, Profile-WPO, and Release explicitly exclude it.
- `ReplayValidation.Internal.h` is included only by the product restore unit
  and the Debug probe unit. Recorder, Timeline, Prediction, and their hot-path
  headers do not include it or any ReplayValidation source.

`ReplayValidation.cpp` intentionally remains in every configuration. Its
product responsibility is transactional event replay, artifact load, target
restore, hash verification, and rollback. It contains only the thin
configuration dispatch to Debug probe operations; the probe implementations
themselves live in the Debug-only unit. Excluding this file would remove
product restore behavior and would be a placement regression, not a cleanup.

## Configuration Matrix

| Unit | Debug | Automation | Profile / WPO / Release | Owner |
|---|---|---|---|---|
| `ReplayValidation.cpp` | Yes | Yes | Yes | Validation product restore/load |
| `ReplayValidation.Probes.cpp` | Yes | No | No | Validation CLI probes |
| `ReplayPredictionArchive.Automation.cpp` | No | Yes | No | Validation offline round-trip |
| `ReplayVisualPacketFingerprint.cpp` | Yes | Yes | No | Validation fingerprinting |
| `InteractionAutomationReportWriter.cpp` | No | Yes | No | Validation report/offline fidelity |

The seven RC4 direct implementation survivors remain explicit. Five are
Automation-only report/fingerprint edges proven by this matrix. The two
product survivors (`UiTextPass` overlay draw and `RuntimeDiagnostics` recorder
sample logging) are not probe-placement leaks and retain their RC4 owner/reason
and deletion conditions.

## Static Proofs

- Search of `ReplayRecorder*`, `ReplayTimeline*`, `ReplayPrediction*`, and
  `ReplayRuntime*` finds zero `#include` edges to ReplayValidation headers.
- The project contains one explicit Debug-only condition for
  `ReplayValidation.Probes.cpp` and Automation-only conditions for the report
  writer and archive verifier.
- The downward Replay-boundary and dependency-direction proofs remain zero
  rows.
- No owner, cap, phase gate, reserve counter, allowlist row, public behavior,
  or source file changed in RC5.

## Comment Audit

RC5 is documentation-only and touches no source-bearing file. Checklist:
0 checked, 0 deferred, no unchecked source file.

## Validation Evidence

Documentation-only placement review requires no repository build/runtime gate.
The campaign's frozen-behavior contract still requires exactly one replay mega
invocation for every phase, so RC5 ran it once:

| Command | Time | Result |
|---|---:|---|
| `tools\validate_replay_visual_fidelity.bat` | 407.4 s | BLOCKED after launcher/typed controls by standing provenance mismatch |

The invocation proved `engine_processes=1`, `prediction_starts=1`,
`presented_cascades=1`, and `nested_scrub_runs=0`; typed controls passed 16
cases / 72 assertions. It then reached the unchanged config provenance
mismatch:

- expected: `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
- actual: `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`

The gate was not retried and no config, golden, baseline, or artifact metadata
was edited. The blocker is recorded and the campaign proceeds to RC6.

# Replay Mass Reduction R2 — Diagnostics Split

Date: 2026-07-16
Status: Complete
Plan task: R2 — Move the probe harness behind the boundary
Branch: `nightrunner-15th-july`

## Outcome

Replay diagnostics now have configuration-specific physical translation units:

| File | Lines | Configuration membership | Responsibility |
|---|---:|---|---|
| `ReplayValidation.cpp` | 1,949 | All engine configurations | Product v2 restore/event replay plus thin configuration dispatch. |
| `ReplayValidation.Probes.cpp` | 1,880 | Debug only | Legacy scrub, restore, save/load, failure, branch, and visual-reconstruction probes. |
| `ReplayPredictionArchive.cpp` | 680 | All engine configurations | Product RVPD codec. |
| `ReplayPredictionArchive.Automation.cpp` | 73 | Automation only | Exact RVPD decode/re-encode verifier used by the mega probe. |
| `ReplayValidation.Internal.h` | 64 | Included by product and Debug owner TUs | Recorded event-flag vocabulary and two shared authoritative store lookups; no probe entry points or mutable state. |

The former 3,729-line mixed `ReplayValidation.cpp` is now below the plan's
~2,000-line owner-TU threshold. Debug probe state and declarations remain
inside the existing `_DEBUG` header boundaries. The Automation verifier
declaration is present only under `SKULLBONEZ_AUTOMATION_DIAGNOSTICS`. No
Release/Profile no-op stub or compatibility seam was added.

## Configuration Matrix

| Configuration | Product archive/restore | Debug probe TU | RVPD verifier TU | Fingerprint TU |
|---|---|---|---|---|
| Debug | Included | Included | Excluded | Included |
| Automation | Included | Excluded | Included | Included |
| Profile / Profile-WPO / Release | Included | Excluded | Excluded | Excluded |

The project uses dotted owner partitions so the existing filter validator maps
the new files to `Runtime\Replay`. `validate_full` proved all three active
build configurations and the Automation/runtime lanes after the split.

## Final Release Map Proof

The final source/project state was rebuilt with a forced Release `/MAP` link.
The build passed in 22.50 s MSBuild / 22.65 s wall with zero warnings and zero
errors. The Release image remains 3,188,224 bytes.

Every required forbidden pattern has zero rows in
`TestOutput/agent_logs/replay-r2-release.map`:

- `ReplayValidation.Probes.obj`
- `ReplayPredictionArchive.Automation.obj`
- `ReplayVisualPacketFingerprint.obj`
- `ReplayProbeRunner`
- `ReplayProbeFailure`
- `VerifyReplayPredictionArchiveRoundTrip`
- `BuildReplayVisualPacketFingerprint`
- `expected.failure` / `ExpectedFailure`

Product restore remains visible through `ReplayValidation.obj` and the product
RVPD codec remains visible through `ReplayPredictionArchive.obj`.

## Validation

- Targeted configuration builds passed with zero warnings/errors: Profile,
  Debug (including `ReplayValidation.Probes.cpp`), and Automation (including
  `ReplayPredictionArchive.Automation.cpp`).
- `tools\validate_tests.bat` passed in 6.75 s: 202/202 cases and
  12,595/12,595 assertions.
- `tools\validate_replay_visual_fidelity.bat` was invoked exactly once for R2
  and passed in 463.42 s: one engine process, one prediction generation, one
  presented cascade, 2,401 ticks, 200 moved bricks, 187 toppled bricks, 199
  causal nodes, and all negative/artifact/determinism controls passing.
- `tools\validate_full.bat` passed in 121.33 s after three preflight-only
  iterations corrected narrow formatting, header normalization, and dotted
  project-filter naming. The successful run passed the CPU umbrella,
  Profile/Automation/Debug builds, Automation and replay/prediction smoke,
  DX12 screenshots with zero InfoQueue errors, both physics processes, and the
  44,401-line byte-exact physics baseline.
- No golden, provenance, artifact schema, probe-output schema, screenshot
  baseline, or physics baseline changed.

## Local Evidence Artifacts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `TestOutput/agent_logs/replay-r2-release.map` | 4,038,026 | `AC17E56ED1DFF2975E3326FB1D8C0EE10212DA747BB63AC6D9EE7D81B987E0C2` |
| `TestOutput/agent_logs/replay-r2-release-project-build-transcript.txt` | 53,068 | `B1839213B867ABCCBC6E4E0AEAA26501C0C48F8954E320238276DC101272769F` |
| `TestOutput/agent_logs/replay-r2-validate-tests-transcript.txt` | 670,724 | `CFD59BFA1B8DC435AAC5FB71A52EBAA9D81418B85BFEEB30766A13F97D134607` |
| `TestOutput/agent_logs/replay-r2-mega-gate-transcript.txt` | 76,192 | `824A4A836B552B3065E6B8DF09871C60FBBC21960C319156D73C4DF482F49283` |
| `TestOutput/agent_logs/replay-r2-validate-full-transcript.txt` | 901,846 | `4381C57F1A4D1ACAAD6C9BBE148E2748C896EEC97D52D360F3E62FA3373F3DBF` |

## Comment Quality Audit

Touched-file audit scope: 8 source-bearing files — the three physical replay
implementation files, the product archive source/header, `ReplayProbeState.h`,
`ReplayRuntime.h`, and `ReplayValidation.Internal.h`. Checked: 8. Deferred: 0.
Every file has `File`, `Purpose`, `Summary`, `Glossary`, `Invariants`, and
`Related` sections; the new boundaries also carry local configuration,
lifetime, Lane P, and frozen-schema comments. No checklist plan was required
for this touched-file audit, and no terms remain for owner-approved wording.

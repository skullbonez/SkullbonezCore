# Replay Mass Reduction R1 — Fingerprint Linkage Pilot

Date: 2026-07-16
Status: Complete
Plan task: R1 — Automation boundary design
Branch: `nightrunner-15th-july`

## Outcome

`ReplayVisualPacketFingerprint.cpp` now compiles only in Automation and Debug
for the engine project. Release, Profile, and Profile-WPO exclude the
translation unit. The separate unit-test project continues to compile the same
implementation for its focused packet controls.

`ReplayValidation.cpp` also hides the fingerprint header and namespace import
behind its existing `_DEBUG` probe boundary. Product configurations therefore
neither link the implementation nor see its declarations; no no-op stub or
compatibility seam was introduced.

## Boundary Proof

CodeGraph reported exactly three callers of
`BuildReplayVisualPacketFingerprint`: two in the Automation-only
`InteractionAutomationController.cpp` and one in
`ReplayProbeRunner::VerifyLoadedPresentation`, which is inside the existing
`_DEBUG` block in `ReplayValidation.cpp`. The other fingerprint operations
have the same configuration-limited caller surface.

| Configuration | Engine project membership | Consumer availability |
|---|---|---|
| Automation | Included | `InteractionAutomationController.cpp` is included and links the oracle. |
| Debug | Included | The `_DEBUG` replay probe includes the header and links the oracle. |
| Profile | Excluded | Automation controller is excluded; fingerprint declarations are absent from `ReplayValidation.cpp`. |
| Profile-WPO | Excluded | Same product boundary as Profile. |
| Release | Excluded | Automation controller is excluded; fingerprint declarations are absent from `ReplayValidation.cpp`. |

`validate_full` selects the Automation solution configuration for its
Automation lane. The R1 mega gate exercised the same configuration directly;
its link transcript contains both `Automation\ReplayVisualPacketFingerprint.obj`
and `Automation\InteractionAutomationController.obj`.

## Product Map Comparison

The post-change maps were produced by forcing fresh engine links with the same
`LINK=/MAP:<path>` method recorded by R0.

| Configuration | R0 fingerprint attribution | R1 fingerprint map rows | Engine image before | Engine image after | Build result |
|---|---:|---:|---:|---:|---|
| Release | 64 bytes | 0 | 3,188,224 bytes | 3,188,224 bytes | 20.98 s, 0 warnings, 0 errors |
| Profile | 200 bytes | 0 | 4,657,664 bytes | 4,657,664 bytes | 8.46 s MSBuild / 8.62 s wall, 0 warnings, 0 errors |

The unchanged PE file lengths are expected: the removed identity constants fit
inside existing section alignment. The map proof is the binding result: both
post-change product maps contain zero `ReplayVisualPacketFingerprint.obj`
rows, whereas the R0 maps attributed 64 and 200 bytes to that object.

## Validation

- `tools\validate_tests.bat` passed in 6.28 s: 202/202 test cases and
  12,595/12,595 assertions. The test executable intentionally linked its own
  fingerprint object.
- `tools\validate_replay_visual_fidelity.bat` was invoked exactly once for R1
  and passed in 459.87 s. Launcher shape was one engine process, one prediction
  generation, and one presented cascade. The authoritative result retained
  2,401 ticks, 200 moved wall bricks, 187 toppled wall bricks, 199 causal nodes,
  62 saved/loaded ticks, and reveal frames 0–2,400. All negative, artifact,
  semantic-packet, causal, and determinism controls passed.
- No golden, provenance, artifact schema, probe-output schema, or baseline file
  changed.

## Local Evidence Artifacts

These artifacts are ignored machine-local evidence. Hashes make substitution
or accidental regeneration visible.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `TestOutput/agent_logs/replay-r1-release.map` | 4,038,008 | `4CF09119DA2A246BC0D849F5396BBA63937C02642CE929A69B2B1AE361E7DB0F` |
| `TestOutput/agent_logs/replay-r1-profile.map` | 4,715,464 | `26B67A7E4CC8E2F9393608B22C760C30896BE3B7B1654E021697B82C6D0097B4` |
| `TestOutput/agent_logs/replay-r1-release-project-build-transcript.txt` | 51,258 | `0BE8954C88547650A6BF66F9D801E36C8B5816774754E0891B6D790397ABDD89` |
| `TestOutput/agent_logs/replay-r1-profile-project-build-transcript.txt` | 51,218 | `6761BA302E3C38ED46B9C59DB3E5EAC4C374B5BF8A2D4EF9FEA91D543F9DB15B` |
| `TestOutput/agent_logs/replay-r1-validate-tests-transcript.txt` | 670,596 | `B5B7B34264074B13DFF79292DCE45C0F9A7FAE84834CFEF8E06316A22B5A9521` |
| `TestOutput/agent_logs/replay-r1-mega-gate-transcript.txt` | 73,480 | `8F0EA62BE0A47874A2B614D9340C05A5C23EB28E958295DD71F2727D8A03B919` |

## Comment Quality Audit

Touched-file audit scope: `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`.
Checked: 1. Deferred: 0. The existing learning header already contains the
required purpose, summary, glossary, invariants, and related references. R1
added a local `Invariant:` comment at the configuration boundary. No terms
remain that need owner-approved wording.

# Replay Mass Reduction R4b — Artifact Bookkeeping Determinism

Date: 2026-07-16  
Owner: replay  
Task: `replay-mass-reduction` R4b

## Outcome

R4b closes finding R3-F1. Live prediction counters remain raw; the RVPD and
RVIS encoders now publish deterministic bookkeeping values. Two artifacts from
the final encoder are whole-file byte-identical, and applying the same encoder
rules to the two original R3 artifacts also produces that exact file.

Whole-file SHA-256:

`BF1B9C5B4D152C968ACF7C2C4C2020349B21B70A5507753931F28EF346DF04C8`

The artifact is 36,564,003 bytes. The unchanged gate-covered content oracle is:

`363841634C50DB68D0C5CCF582A8BC07EFFC7FFED8DBF69D4444C6B3127DA2FA`

No golden, baseline, provenance, scene, config, or simulation value changed.

## Complete R3-F1 Field Inventory And Writers

| Field family | Final writer | Canonical representation |
|---|---|---|
| RVPD `futureNodesTopologyVersion` / `nextFutureNodesTopologyVersion` | `ReplayPredictionArchive.cpp:374-377` | Completed publication is `1`; next token is `2` (`0`/`1` when no publication exists). |
| RVPD `trajectoryBuild.topologyVersion` | `ReplayPredictionArchive.cpp:448-453` | `1` only when the live build/future tokens matched; otherwise `0`, preserving the validity distinction. |
| RVPD `trajectoryStore.nextVersion` and active `records[*].version` | `ReplayPredictionArchive.cpp:390-426` | Active records receive dense versions in serialized publication order; `nextVersion` is active count plus one. |
| RVPD inactive worker-bank lane 2/3 records at branch ordinal 240+ | `ReplayPredictionArchive.cpp:403-423` | Each retained record slot becomes the same excluded child-bank key with zero version, values, and point count. |
| RVIS `topologyVersion` | `InteractionAutomationController.cpp:2477-2505`; `ReplayV2Artifact.cpp:1938-1970` | Dense first-publication ordinal, applied identically to offline expected packets and durable rows. |
| RVIS `replayReserveGrowthEvents` | `InteractionAutomationController.cpp:2519`; `ReplayV2Artifact.cpp:1959,1991` | Durable constant `0`; the live report retains the raw counter. |
| RVIS `semanticHash` | `ReplayV2Artifact.cpp:1958,1963` | Stable nonzero FNV offset sentinel; deterministic `visualStateHash` and `exactPacketHash` remain unchanged and fully checked. |
| MANI `visualPredictionBytes` / `visualPredictionHash` | `ReplayV2Artifact.cpp:2326-2337` | Consequence of the canonical RVPD payload; no special-case manifest write is required. |

This covers every normalized field family in the R3 full binary diff. The
outer file size, chunk offsets, MANI width, and RVPD byte count were secondary
consequences and become stable automatically.

## Reader Survey

- Future-node and trajectory-build topology versions are used as nonzero and
  equality tokens in `ReplayPrediction.cpp`; no reader orders or interprets
  their absolute process counter values.
- The durable packet comparison remains active. Offline reconstruction and
  RVIS encoding use the same first-publication mapping, and
  `check_replay_visual_fidelity.py` explicitly verifies every canonical row.
  The topology field was not removed from comparison.
- `ReplayTrajectoryStore::nextVersion` and record versions support live record
  replacement. A loaded prediction archive enters the one-way offline
  verification capability and cannot allocate a new version or restart a
  worker. Presented trajectory hashes exclude record versions.
- Completed presentation admits only the committed child bank below ordinal
  240. The schedule-selected lane 2/3 bank at 240+ is explicitly excluded from
  visual/exact hashes; retaining fixed inert slots preserves record-count and
  reader layout contracts.
- Reserve growth and the broader semantic hash are process diagnostics.
  Durable visual and exact packet hashes, topology content, geometry bytes,
  counts, and all lane hashes remain checked.

No reader depends on an absolute canonicalized value.

## Versioning Ruling

No replay format-version or RVPD schema bump is required. The v4 chunk set,
296-byte RVIS row, RVPD scalar order, field widths, record-count contract, and
all supported reader layouts are unchanged. R4b changes only value-agnostic
matching tokens and explicitly non-presenting telemetry at serialization. It
does not change an authored scene/asset/config schema and requires no legacy
migration. Existing v2/v3/v4 acceptance and future-version rejection remain
unchanged.

## Byte Proof

| Artifact | Source bytes | Canonical/final bytes | Final SHA-256 |
|---|---:|---:|---|
| Original R3 before | 46,104,063 | 36,564,003 | `BF1B9C5B4D152C968ACF7C2C4C2020349B21B70A5507753931F28EF346DF04C8` |
| Original R3 after | 46,104,064 | 36,564,003 | `BF1B9C5B4D152C968ACF7C2C4C2020349B21B70A5507753931F28EF346DF04C8` |
| Final encoder artifact B | 36,564,003 | 36,564,003 | `BF1B9C5B4D152C968ACF7C2C4C2020349B21B70A5507753931F28EF346DF04C8` |
| Final encoder artifact C | 36,564,003 | 36,564,003 | `BF1B9C5B4D152C968ACF7C2C4C2020349B21B70A5507753931F28EF346DF04C8` |

`TestOutput/agent_logs/replay-r4b-four-way-gate-oracle.json` proves all four
artifacts retain the R3 gate-covered oracle SHA. The original-pair transform is
recorded in
`TestOutput/agent_logs/replay-r4b-original-mismatch-canonicalized-result.json`.

## Invocation Accounting And Validation

The R4b charter authorized two mega-gate invocations. Invocation A reached the
artifact reader and failed because the initial canonical semantic sentinel was
zero, which the existing reader correctly rejects as an empty hash. The owner
then explicitly approved one additional same-tip R4b invocation, for three
total. Invocation B used the final encoder and produced the final byte-exact
artifact; its post-engine checker exposed the remaining raw-vs-canonical
comparison and passed CPU-only revalidation after the checker began verifying
canonical values explicitly. Invocation C was the final full passing gate.

- Focused Profile builds: 12.43 s, 11.40 s, and 12.05 s; zero warnings/errors.
- `tools\validate_tests.bat`: 4.00 s; 202/202 cases and 12,595/12,595 assertions.
- `tools\validate_fast.bat`: initial format-only stop, then 54.00 s pass with
  formatting, metadata, size, Profile, and Debug checks clean.
- Mega A: 404.46 s; one engine/generation; invalid zero sentinel rejected.
- Mega B: 402.14 s; one engine/generation; final artifact preserved and later
  CPU-revalidated successfully.
- Mega C: 430.45 s; one engine/generation; 2,401 ticks, 200 moved bricks,
  187 toppled bricks, 199 causal nodes, and every negative control passed.

Touched-source comment audit: 3/3 source-bearing files inspected against the
comment-style guide; the serialization token, worker-bank exclusion, and
cross-side comparison hazards have local teaching comments. The Python checker
also documents why excluded raw fields remain explicitly verified.

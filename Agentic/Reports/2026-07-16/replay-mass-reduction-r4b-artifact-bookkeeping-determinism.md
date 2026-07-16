# Replay Mass Reduction R4b — Artifact Bookkeeping Determinism

Date: 2026-07-16  
Owner: replay  
Task: `replay-mass-reduction` R4b

## Outcome

R4b closes finding R3-F1. Live prediction counters remain raw; the RVPD and
RVIS encoders now publish deterministic bookkeeping values. Applying the final
encoder rules to both original R3 artifacts produces the exact file emitted by
the repaired encoder.

Final content-sensitive whole-file SHA-256:

`F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24`

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
| RVIS `semanticHash` | `ReplayVisualPacket.h`; `ReplayV2Artifact.cpp:1954-1971` | Content-sensitive FNV digest seeded by `visualStateHash` and extended with the canonical topology token, zero reserve telemetry, and `exactPacketHash`. |
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
- Reserve growth and the live semantic hash retain process diagnostics. The
  durable semantic hash remains content-sensitive by hashing the unchanged
  visual/exact packet digests with the serialized topology/reserve values.
  Topology content, geometry bytes, counts, and all lane hashes remain checked.

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
| Original R3 before | 46,104,063 | 36,564,003 | `F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24` |
| Original R3 after | 46,104,064 | 36,564,003 | `F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24` |
| Review-fix encoder artifact | 36,564,003 | 36,564,003 | `F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24` |

The final direct byte comparison between each transformed original and the
review-fix encoder artifact returned true. The proof is recorded in
`TestOutput/agent_logs/replay-r4b-review-fix-original-canonicalized-result.json`;
the emitted artifact is preserved as
`TestOutput/agent_logs/replay-r4b-review-fix-final-encoder.skreplay`. The
gate-covered projection remains the R3 SHA recorded above.

## Independent-Review Reopen And Repair

R7's mandatory independent review found that the first R4b implementation's
fixed nonzero semantic sentinel erased a content-sensitive diagnostic contract.
That finding reopened R4b. The repair adds one shared little-endian canonical
semantic-hash primitive, uses it in RVIS serialization, mirrors it independently
in the Python reader/checker, and adds an exact-vector plus mutation regression
test. The row layout and supported reader versions remain unchanged, so the
no-version-bump ruling still holds; the field now preserves strictly more of its
existing meaning than the rejected sentinel implementation.

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
- R7 pre-remediation mega: 433.42 s and PASS, but invalidated when the concurrent
  independent review reopened R4b; it is not final-source R7 evidence.
- Review-fix `tools\validate_tests.bat`: 7.45 s; 203/203 cases and
  12,600/12,600 assertions, including canonical semantic hash sensitivity.
- Review-fix original-pair transform: 17.55 s; both originals and the emitted
  encoder artifact are byte-exact at the final SHA above.
- Review-fix mega: 433.34 s; one engine/generation; unchanged 2,401/200/187/199
  results and every negative control passed.

Touched-source comment audit: the original 3/3 files plus all four review-fix
source/tool/test files were inspected against the comment-style guide; the
serialization token, worker-bank exclusion, content-sensitive semantic digest,
and cross-side comparison hazards have local teaching comments.

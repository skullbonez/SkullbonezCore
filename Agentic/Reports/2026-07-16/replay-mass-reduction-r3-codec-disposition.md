# Replay Mass Reduction R3 — Codec Disposition And Byte-Proof Finding

Date: 2026-07-16  
Owner: replay  
Task: `replay-mass-reduction` R3

## Outcome

R3 keeps both candidate codec implementations and makes no source or artifact-
format change.

- **D1 KEEP:** `ReplayPredictionArchive.cpp` owns the RVPD codec. It writes an
  explicitly little-endian, scalar-by-scalar stream and enforces its 128 MiB
  archive cap and failure contract. `ReplayV2Artifact.cpp` appends host POD into
  chunk-bounded V2 records. The leaf operations are not byte-identical and
  combining them would change either the wire contract or an owner's failure
  semantics.
- **D3 KEEP:** recorder hashing is domain traversal, not artifact
  serialization. Recorder, visual-oracle, manifest, and artifact hashes have
  distinct seeds and semantic contracts. A shared seed-parameterized FNV leaf
  would add cross-owner coupling without removing a codec owner or duplicate
  traversal.

This completes the R0 duplication-table dispositions for R3. No codec helper,
schema, golden, baseline, provenance field, or runtime behavior changed.

## Required Existing-Artifact Binary Diff

Before the owner-approved diagnostic exception, the two existing durable
artifacts were compared in full. The raw positional comparison enumerated
33,393,417 differing offsets in 2,633,960 contiguous ranges. Because the
manifest's decimal hash changed width and shifted following chunk offsets by
one byte, a second key/frame-aligned inventory decoded the semantic fields
rather than misclassifying that outer shift as payload drift.

The complete inventories are retained as diagnostic artifacts:

| Evidence | Bytes | SHA-256 |
|---|---:|---|
| `TestOutput/agent_logs/replay-r3-existing-diff-raw-offset-ranges.csv.gz` | 14,470,484 | `B3A5E9E3AFED7C15DBE43DBED7E0C3607E6E6DB09E2ADE84CCA78041605C986B` |
| `TestOutput/agent_logs/replay-r3-existing-diff-aligned-byte-diffs.csv.gz` | 118,472,895 | `3A17DFBC5A8112C9100ABE5673E40F0B01D5766708F553B149AAC385DA3360A2` |
| `TestOutput/agent_logs/replay-r3-existing-diff-summary.json` | 32,140 | `F99BFD47A4CCF2EBC2CE9E104EB0302D486FFBC04833B1E600EB65E349DC2890` |

Whole chunks `BODY`, `PRES`, `BRAN`, `EVNT`, `ECUR`, `HASH`, `SCHK`, and
`INDX` were byte-identical. The remaining changes decoded exactly as follows:

- `MANI`: only `visualPredictionHash` changed; its decimal encoding grew by
  one byte.
- All 2,401 `RVIS` rows changed only `semanticHash`, `topologyVersion`
  (`22` → `23`), and `replayReserveGrowthEvents` (`144047` → `142389`).
  `visualStateHash`, `exactPacketHash`, geometry hashes/counts, camera values,
  and presentation values were identical.
- `RVPD`: `futureNodesTopologyVersion` changed `22` → `23`,
  `nextFutureNodesTopologyVersion` changed `23` → `24`,
  `trajectoryBuild.topologyVersion` changed `22` → `23`, and
  `trajectoryStore.nextVersion` changed `7593` → `7925` (`+332`).
- Both RVPD payloads held 798 trajectory records. There were 542 shared keys,
  256 keys only in the first artifact, and 256 only in the second. Every
  differing key was an inactive transient child lane 2/3 record with branch
  ordinal 280–420 in the worker bank at or above 240. Every committed branch
  below 240 was shared. Of the shared records, 541 versions moved uniformly by
  `+332` and one was unchanged; matching gate-presented record headers and
  point bytes were otherwise identical.

## Encoder Input Inspection And Suspected Source

The encoded differences are deterministic scalar fields, not opaque padding.
The inspected paths found no encoded timestamp, no map/unordered-map iteration,
and no uninitialized structure copy: RVPD emits explicit scalars and RVIS emits
members individually.

The suspected source is schedule-sensitive transient prediction bookkeeping:

1. `UpdateReplayPredictionFutureNodeCache` is bounded by `steady_clock` and
   consumes asynchronously published worker prefixes.
2. Each transient topology change calls
   `AllocateReplayFutureNodeTopologyVersion`.
3. `RebuildReplayPredictionCommittedTreeAfterWorkerCompletion` clears the
   visible cache, but `ClearReplayPredictionFutureNodeCache` preserves
   `nextFutureNodesTopologyVersion`.
4. `UpdateReplayPredictionTrajectoryStore` replaces records on each version
   change. `BeginReplaceRecord` increments `nextVersion`, while obsolete
   worker-bank keys remain stored.
5. `RuntimeReserveAllocator::GrowthEventCount()` captures allocation churn in
   RVIS. `BuildReplayVisualPacketFingerprint` computes `visualStateHash`
   before adding topology/reserve/internal-trajectory diagnostics to
   `semanticHash`, so presented visual content remains stable while the latter
   changes.

## Owner-Approved Third Invocation And Formal Finding

The owner approved one additional same-tip mega-gate invocation as an R3
diagnostic exception after the full diff and input inspection above. It passed
in 460.55 s with one engine process, one prediction generation, 2,401 ticks,
200 moved boxes, 187 toppled boxes, and 199 causal nodes. The third artifact
differs from both prior artifacts:

| Artifact | Bytes | File SHA-256 | Manifest RVPD hash |
|---|---:|---|---:|
| before (R2 mega) | 46,104,063 | `BFE3959AF83CDDB1F1917D0D581A2B67BBABEBFF24C96E0794FD026D8840086D` | 1,908,191,095,564,028,107 |
| after (R3 mega) | 46,104,064 | `2EE311C11D3C3848F03367E8CC4D99E28765D5A0546D204E389D032A0E2BF659` | 18,061,741,948,941,032,662 |
| third (approved exception) | 46,104,063 | `2D1BAA110B54B470D386E68B8F46BB8029261558B17A3289FD2FDC8B63799C41` | 6,291,473,488,163,861,144 |

**Formal finding R3-F1:** complete replay artifact encoding is run-to-run
nondeterministic at the same tip because schedule-sensitive transient topology,
trajectory-store versioning, retained inactive worker records, and reserve
growth counts are serialized into bookkeeping/diagnostic fields. R3 does not
fix or conceal that finding.

## Honest Byte-Proof Oracle

R3's byte proof is the gate-covered content, not a false whole-file equality
claim. The three-way oracle includes:

- complete `BODY`, `PRES`, `BRAN`, `EVNT`, `ECUR`, `HASH`, `SCHK`, and `INDX`
  chunks;
- every RVIS field except `semanticHash`, `topologyVersion`, and
  `replayReserveGrowthEvents`;
- RVPD future root branch 0, future-child incoming/outgoing branches below both
  `futureNodeCount` and the 240-record committed boundary, every non-future
  lane, and all included record metadata and point bytes.

It excludes only the named nondeterministic bookkeeping: RVPD current/next
future topology versions, trajectory-store `nextVersion`, per-record versions,
trajectory-build topology version, and inactive worker-bank trajectory records.

All three projections match:

- overall oracle: `363841634C50DB68D0C5CCF582A8BC07EFFC7FFED8DBF69D4444C6B3127DA2FA`;
- RVIS projection: 662,680 bytes,
  `56D1E598AD5D8A51A0F0B90BD6A8E63EF9ACDE174CD219083329FD90D5180DDD`;
- RVPD projection: 34,536,617 bytes, 399 records, 480,598 points,
  `792D7BA0E05127B40098556E2196EF22DB86BFE6042537C86B63BE6927E8476D`.

The compact oracle record is
`TestOutput/agent_logs/replay-r3-three-way-gate-oracle.json` (9,628 bytes,
SHA-256 `72BD9CE07521BC7E1197EF53FA3B619860AF021AB7BDBD4169DE4BF808E9A3A1`).

## Validation

- `tools\validate_tests.bat`: passed 202/202 cases and 12,595/12,595
  assertions in 5.82 s. Transcript SHA-256:
  `13EDECAC1BF432980BD23FBFCE02DF57B0C47E90DBD3301D832C2C0C9D8A0562`.
- Required R3 `tools\validate_replay_visual_fidelity.bat`: passed in 464.45 s
  with one engine and one generation. Transcript SHA-256:
  `AD39735F74F055D0F1041B6D5C3E73E3BDAA848D980E7C532A3777B6D1C66C0D`.
- Owner-approved R3 diagnostic exception: passed in 460.55 s with the same
  bounded counts. Transcript SHA-256:
  `4F863EEE6ED4D2FE936EE8128F8038289BD523EB23C86757F983F7DE566BAF0B`.

## Separately Ruled Follow-Up

R3-F1 is a separate replay-owner task, outside R3's mass-reduction change and
outside this campaign's current eight-task count. Its scope is to choose and
prove an artifact contract for transient topology/store bookkeeping, remove
schedule sensitivity only under an explicit format/compatibility ruling, and
add a whole-artifact same-tip determinism test. Entry requires a dedicated plan
with legacy/current reader coverage and artifact/golden authority. Its deletion
condition is whole-artifact equality across repeated same-tip runs without
excluding bookkeeping. No implementation is authorized or performed here.

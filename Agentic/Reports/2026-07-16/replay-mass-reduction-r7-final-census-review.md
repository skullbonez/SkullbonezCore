# Replay Mass-Reduction R7 Final Census And Independent Review

Date: 2026-07-16
Plan: `Agentic/Plans/TODO/replay-mass-reduction.md`
Final measured source: `fffda9fe3`
Status: Complete. The final census, binary evidence, byte proof, and independent
whole-campaign review are closed with no unresolved credible finding.

## Outcome

The campaign reduced the product-compiled replay implementation from 17 to 16
translation units and from 26,875 to 24,521 physical implementation lines:
**one fewer product TU and 2,354 fewer product-compiled lines**. The complete
tracked replay directory grew from 42 files / 33,783 lines to 45 files / 34,093
lines because diagnostic code is now physically separated into configuration-
specific files and the artifact determinism repair added explicit
canonicalization and verification. Product map-attributed bytes did not reduce:
Release increased by 364 bytes and Profile by 1,988 bytes. That small binary
increase is reported as measured, not hidden behind the source/configuration
reduction.

## Census

The final inventory repeats R0's complete `git ls-files
SkullbonezSource/Runtime/Replay` scope and physical `Get-Content` line count.
Every tracked file is assigned once to its R0 primary-concern bucket. Product
implementation counts include `.cpp` files compiled in Release/Profile;
Automation includes its configuration-only replay TUs.

| Bucket | R0 files | Final files | Delta | R0 lines | Final lines | Delta |
|---|---:|---:|---:|---:|---:|---:|
| `product-runtime` | 19 | 20 | +1 | 16,421 | 14,721 | -1,700 |
| `prediction-engine` | 8 | 8 | 0 | 5,748 | 5,740 | -8 |
| `artifact-io` | 4 | 4 | 0 | 3,808 | 3,867 | +59 |
| `probe-harness` | 3 | 5 | +2 | 992 | 2,948 | +1,956 |
| `presentation-emission` | 8 | 8 | 0 | 6,814 | 6,817 | +3 |
| **Tracked total** | **42** | **45** | **+3** | **33,783** | **34,093** | **+310** |

| Compiled implementation | R0 TUs | Final TUs | Delta | R0 lines | Final lines | Delta |
|---|---:|---:|---:|---:|---:|---:|
| Release/Profile product | 17 | 16 | -1 | 26,875 | 24,521 | -2,354 |
| Automation final | - | 18 | - | - | 25,205 | - |

The physical `probe-harness` increase is the intended link-boundary proof: the
Debug probes and Automation RVPD verifier moved out of mixed product TUs. It is
configuration-owned relocation, not newly compiled product harness mass.

## Product Link Maps

R7 repeated R0's direct-project `/MAP` procedure and attribution parser: union
public/static rows by section RVA, charge unique intervals only when one object
owns the starting RVA, and leave same-RVA folds unassigned. Comparisons are
only against the matching R0 configuration.

| Config | Build | R0 replay | Final replay | Delta | R0 image | Final image | Ambiguous | Final map SHA-256 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Release | 24.82 s | 498,264 | 498,628 | **+364** | 3,188,224 | 3,188,224 | 23,080 | `938301D8141E868A3DE4B8E7CDBDDD719B52F3D877589E4C0273B7BB60A34670` |
| Profile | 8.65 s | 456,044 | 458,032 | **+1,988** | 4,657,664 | 4,659,712 | 34,104 | `760F094AA619D076EB02DBEE9DCE50E536ED22112643380CD955CAFAFBC178F0` |

Both builds completed with zero warnings/errors. Final map sizes are 4,036,331
bytes (Release) and 4,718,252 bytes (Profile). Both product maps contain 15
nonzero replay objects and zero fingerprint bytes; configuration-only probe and
archive-verifier objects are absent. Production replay owners remain present.

| Replay object | R0 Rel. | Final Rel. | Delta | R0 Prof. | Final Prof. | Delta |
|---|---:|---:|---:|---:|---:|---:|
| `ReplayAuthoringCauseTree.obj` | 15,960 | 15,960 | 0 | 17,252 | 17,252 | 0 |
| `ReplayAuthoringVelocity.obj` | 9,808 | 9,808 | 0 | 10,468 | 10,468 | 0 |
| `ReplayOverlayLayout.obj` | 6,292 | 6,292 | 0 | 7,528 | 7,528 | 0 |
| `ReplayOverlayRenderer.obj` | 79,728 | 79,728 | 0 | 19,916 | 19,916 | 0 |
| `ReplayPrediction.obj` | 55,564 | 55,568 | +4 | 56,640 | 56,640 | 0 |
| `ReplayPredictionArchive.obj` | 2,308 | 2,308 | 0 | 22,796 | 24,252 | +1,456 |
| `ReplayPredictionDrawing.obj` | 20,328 | 20,324 | -4 | 19,708 | 19,708 | 0 |
| `ReplayPredictionReserve.obj` | 588 | 588 | 0 | 744 | 744 | 0 |
| `ReplayPresentation.obj` | 12,340 | 10,876 | -1,464 | 15,156 | 15,040 | -116 |
| `ReplayRecorder.obj` | 134,032 | 134,024 | -8 | 136,216 | 136,216 | 0 |
| `ReplayRuntime.obj` | 41,740 | 42,828 | +1,088 | 28,916 | 28,916 | 0 |
| `ReplayScrubberTools.obj` | 19,448 | 19,448 | 0 | 15,444 | 15,444 | 0 |
| `ReplayTimeline.obj` | 7,356 | 7,348 | -8 | 9,424 | 9,424 | 0 |
| `ReplayV2Artifact.obj` | 72,960 | 73,788 | +828 | 74,300 | 75,148 | +848 |
| `ReplayValidation.obj` | 19,748 | 19,740 | -8 | 21,336 | 21,336 | 0 |
| `ReplayVisualPacketFingerprint.obj` | 64 | 0 | -64 | 200 | 0 | -200 |
| **Total** | **498,264** | **498,628** | **+364** | **456,044** | **458,032** | **+1,988** |

## Automation Link Evidence

The Automation `/MAP` target linked in 8.76 s. Its 4,834,304-byte image has
491,324 bytes across 17 `Replay*.obj` rows plus 4,304 bytes in
`TrajectoryStore.obj`. The 4,966,657-byte map SHA is
`8FAB8CF84640F2E7B0FB2BD99C6CA9222A1A6BFC8B6BFBEE6A42BED3AEBF418D`.
Unlike product builds, it retains 16,208 fingerprint bytes and 988 bytes of
`ReplayPredictionArchive.Automation.obj`, proving diagnostics still link where
intended.

## Original-Mismatch Byte Proof

The original 46,104,063-byte and 46,104,064-byte R3 artifacts both canonicalize
to 36,564,003 bytes. Each transformed original, the repaired encoder artifact,
and R7's final gate artifact are byte-for-byte identical at SHA-256:

`F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24`

Direct `SequenceEqual` comparisons returned true. The original mismatch would
therefore be byte exact with the final change, not merely equal under a weakened
projection. Bookkeeping remains present and checked; its semantic hash remains
content-sensitive to visual state, exact-packet hash, and canonical topology.

## Independent Whole-Campaign Review

The first independent pass took 6m13s and found two credible blockers: R4b's
initial fixed semantic sentinel erased content sensitivity, and governance
still contradicted the ruled in-campaign repair. Those findings reopened R4b.
The repair derives the canonical semantic hash from unchanged content hashes
and canonical bookkeeping, independently recomputes it in Python, adds exact-
vector/mutation tests, and reconciles governance. The same reviewer rechecked
the repair in 1m07s and confirmed both blockers resolved with no new material
blocker.

Final review records zero unresolved credible findings: no authority moved; no
forwarding facade, callback pack, or broad context bag appeared; diagnostics
use link membership rather than scattered product `#ifdef`; D4/D7 preserve
arithmetic and order while D5 and distinct codec/emission concerns remain
separate; both R5 deletions cite owner rulings; R6's KEEP rulings explain why
moves-only splits would create APIs, duplicate contracts, or cosmetic includes;
and no golden, baseline, schema, scene, config, screenshot, or provenance input
was refreshed.

## Validation And Invocation Accounting

- Final `tools\validate_tests.bat`: 6.14 s; 203/203 cases and
  12,600/12,600 assertions passed.
- Final `tools\validate_replay_visual_fidelity.bat`: 428.07 s by transcript
  timestamps; exactly one engine process/generation; 2,401 ticks, 200 moved,
  187 toppled, 199 causal nodes, and every negative control passed.
- Final transcript: 73,428 bytes, SHA-256
  `B4E9F14F2B1D354DF0DD9BA33F342FFD1339769FE80D6C895847DC083335C949`.
- The earlier 433.42 s R7 gate passed but is not final evidence because the
  review subsequently reopened R4b; it remains only invocation accounting.

The user message after the final gate completed interrupted the wrapper wait,
not the gate. The transcript timestamps (17:59:10 to 18:06:19 local) and
complete terminal PASS establish the accepted 428.07 s run.

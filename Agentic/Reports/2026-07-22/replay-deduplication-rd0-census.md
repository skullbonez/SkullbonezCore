# Replay Deduplication RD0 Census

Date: 2026-07-22
Branch: `nightrunner-22nd-JUL-26`
Task: `replay-deduplication-audit` RD0
Result: Complete census; seven owner rulings required in RD1

## Outcome

The current Replay tree contains seven repeated surfaces worth an owner ruling.
Five are direct implementation duplication and two are overlapping value shapes
whose separate lifetimes may justify retention. The clearest risk is newly
introduced by the RC2 physical split: Prediction scheduling, reveal, capacity,
lookup, and affected-body traversal helpers now have multiple file-local copies.

This is not a size finding. The six RC domains remain binding, no behavior or
owner boundary is proposed here, and the earlier decisions to retain distinct
wire codecs, hash contracts, recorder tracks, and Validation configuration
lanes still hold. RD0 changes documentation only.

## Method And Current Inventory

- `codegraph status .` reported a current index; focused CodeGraph exploration
  mapped Prediction/Presentation, codec/hash, packet, and Validation call paths
  before targeted source confirmation.
- `git ls-files SkullbonezSource/Runtime/Replay/*` returns exactly 64 tracked
  files. Physical lines were recomputed with `ReadAllLines().Length`.
- The plan's registration-time `36,332` lines and eleven `*Packets.h` statement
  are stale after RC/RR work. The current tree is 36,475 lines. Eight filenames
  end in `Packets.h`; the value-shape pass also covered
  `ReplayPredictionView.h`, `ReplayVisualPacket.h`, and
  `ReplayPresentationSubmission.h`, preserving the intended eleven-header
  surface rather than silently shrinking the audit.
- Function-name intersection, exact source reads, and prior R-/RC-era reports
  were used to distinguish copied mechanics from deliberately separate
  contracts.

| Domain | Files | Current lines | Boundary |
|---|---:|---:|---|
| ArtifactIO | 7 | 4,212 | Cold format, codec, materialization, and hash-log authority |
| Capture | 7 | 4,628 | Bounded presentation/solver/event capture and retained-memory policy |
| Prediction | 16 | 8,156 | Isolated future, scheduling, publication, topology, and trajectories |
| Presentation | 16 | 8,612 | Frame selection, path/cause projection, overlay, and render submission |
| Timeline | 11 | 5,915 | Public composition, scrub/restore, retained ordering, and coordination |
| Validation | 7 | 4,952 | Debug/Automation probes, fingerprints, and product restore verification |
| **Total** | **64** | **36,475** | **Every tracked file assigned once below** |

## Confirmed Candidates Requiring RD1 Rulings

| ID | Repeated surface and evidence | Prior ruling / new evidence | RD0 recommendation |
|---|---|---|---|
| C1 | Prediction timing, budget, reveal, and refresh-window logic: `ReplayPrediction.cpp:220-339`, `ReplayPredictionPublication.cpp:85-191`, and `ReplayPredictionTopologyPublication.cpp:84-190`; elapsed/budget is copied again at `ReplayPredictionDrawing.cpp:166-175`. The reveal implementation is line-for-line equivalent across three units, while the core refresh precondition differs subtly from the two extracted copies. | RC2 assigned scheduling/publication/topology to named units and RC6 accepted the core's cohesion. The repeated file-local implementation appeared as a consequence of that split and is new evidence against treating the physical separation as complete. | **dedup-now** into one Prediction-owned internal operation surface; preserve the core refresh precondition exactly and make each caller's intended precondition explicit. |
| C2 | Prediction capacity/reserve accounting: constants and `ReplayPredictionCapacityBytes`, vector/snapshot/frame byte accounting, growth rounding, debug-contact capacity, engine byte accounting, and reserve helpers are duplicated at `ReplayPrediction.cpp:341-538` and `ReplayPredictionPublication.cpp:193-390`. | RC2 says publication exclusively owns published-prefix/worker-failure atomics; copied allocation-policy mechanics were not part of that ruling. New evidence. | **dedup-now** behind a Prediction-owned allocation-policy helper; no owner, cap, phase gate, or counter change. |
| C3 | Identity/model-row lookup and pose leaves repeat across `ReplayPredictionPublication.cpp:399-553`, `ReplayPredictionTopologyPublication.cpp:206-360`, and the Prediction subset in `ReplayPredictionDrawing.cpp:177-255`. Repeated names include both sample lookup templates, prediction lookups, ragdoll classification, hint fallback, and normalization/orientation helpers. | RC3 moved shared cause-focus row resolution to a typed seam, but these three copies remained. The cross-Prediction/Presentation repetition is new evidence against that completed-sharing claim. | **dedup-now** as value-only lookup leaves in the existing internal publication-operations seam or a narrower owner-named header; no callback or owner reach-back. |
| C4 | Affected-body selection/trail derivation is duplicated between Prediction topology and Presentation drawing: stride at `ReplayPredictionTopologyPublication.cpp:366` / `ReplayPredictionDrawing.cpp:508`; identical trail record shapes at `:1024` / `:1307`; future-node filtering at `:1041` / `:1324`; and the same build traversal at `:1054-1133` / `:1336-1415` (only `vector` versus `span` parameters differ). | R4 retained several trajectory loops because their endpoint/reveal/identity semantics differed. This later RC2 copy has the same filter and traversal semantics in both units, so it is new evidence and does not contradict the older differentiated-loop finding. | **dedup-now** around spans and a plain output buffer; keep topology publication and draw submission as separate callers. |
| C5 | Presentation selection/overlay state is flattened repeatedly. The canonical `ReplayPresentationSelection` is `ReplayPresentation.h:100-113`; all eleven fields recur in `ReplayOverlayPackets.h:63-73`, ten recur at `:91-100`, and `ReplayOverlayStateView` / `ReplayOverlayRenderContext` also repeat scrubber, prediction, path, velocity, cause-tree, stats, and scrubber flags at `:55-78` / `:80-111`. | RC3 explicitly established one `BuildPresentationSelection()` answer consumed by overlay and render-pose paths. Copying its fields into two more packets weakens that invariant and is new evidence against the claimed single value boundary. | **dedup-now** by composition/borrowing of the canonical selection and overlay state; retain `ReplayRenderFrameView` as the narrower renderer projection. |
| C6 | Past-trajectory progress appears as mutable `RunReplayPastTrajectoryBuildState` at `ReplayPathPackets.h:39-50` and immutable `ReplayPastTrajectoryView` at `ReplayPredictionView.h:117-129`, sharing target, frame cursor, eviction, rebuild, trim, and validity fields. | No prior ruling names this pair. The view's source comment says the duplication intentionally avoids borrowing Presentation's complete mutable path state across the Prediction boundary. | **cohesion-retain** unless the owner prefers a shared plain cursor value embedded by both; the current duplication makes the domain boundary explicit and carries no duplicated algorithm. |
| C7 | `ReplayPredictionBaselineBodyPose` at `ReplayPredictionView.h:66-76` and `ReplayPredictionRetainedMarker` at `ReplayVisualPacket.h:138-151` share identity, model-row hint, entry/rest flags, and entry/rest poses; the marker adds horizon state. Both are separately archived in `ReplayPredictionArchive.cpp:260-297,387,471,602,698`. | No prior ruling names this pair. Their current archive rows and lifetimes differ: baseline comparison state versus retained visual evidence. | **cohesion-retain** unless the owner wants an embedded common pose value; do not merge archive rows or change bytes. |

## Screened Surfaces And Binding Prior Rulings

| ID | Surface | Disposition and evidence |
|---|---|---|
| S1 | Recorder, visual fingerprint, visual-buffer, and V2 manifest FNV leaves | **Prior KEEP holds; not an RD1 candidate.** `ReplayRecorder.cpp:82-83,984-1053`, `ReplayVisualPacketFingerprint.cpp:41-79`, `ReplayVisualPacket.h:57-100`, and `ReplayV2Artifact.cpp:2115-2123` repeat byte accumulation, but Recorder deliberately uses offset `14695981039346656037` while visual hashes use `1469598103934665603`, and every family owns different field order/count semantics. R3 explicitly rejected a seed-parameterized cross-owner leaf as coupling without owner removal. RD0 found no new evidence. |
| S2 | RVPD and V2 serialization; V2 writer/reader symmetry | **Prior KEEP holds.** R3 found the RVPD scalar codec and V2 POD/chunk codec byte- and failure-contract distinct. RC6 retained V2's paired encoder/decoder authority and rejected a mechanical split. Symmetric append/read functions are a codec contract, not duplicate ownership. |
| S3 | Recorder hash traversal versus V2 field traversal | **Prior KEEP holds.** R0/R3 ruled live retained-state hashing distinct from artifact serialization. RD0 found no shared field-walk that could move without coupling Capture to the wire schema. |
| S4 | `ReplayPresentationSample` versus `ReplaySolverFrameSample` common header fields | **Intentional projection.** `ReplayRecorder.h:187-202` and `:382-400` document that solver restore state may project presentation state while presentation-only payloads remain out of the solver checkpoint. RC6 retained their synchronized Capture lifecycle. |
| S5 | `ReplayVisualArchiveSample` versus frame-local visual packet/fingerprint values | **Intentional durable representation.** The archive value flattens schema/fingerprint/submission facts so ArtifactIO owns no borrowed renderer spans. No duplicate builder exists. |
| S6 | `ReplayRenderFrameView` versus full Presentation selection | **Intentional narrow projection.** `ReplayPresentationPackets.h:38-48` carries only three selected samples plus visual/focus facts needed by Rendering; it does not repeat latest/current/loaded selection policy. |
| S7 | `ReplayValidation.cpp` versus `ReplayValidation.Probes.cpp` | **RC5 placement ruling holds.** Function-name intersection found no duplicate implementation. `ReplayValidation.cpp` owns product restore/load/hash rollback in every configuration; `ReplayValidation.Probes.cpp` owns Debug-only probe workflows. Their two shared authoritative lookups already live in `ReplayValidation.Internal.h`. Similar includes and failure returns are not duplicate behavior. |
| S8 | Artifact hash log formatting and Recorder/V2 hashes | **Distinct output concern.** `ReplayArtifactHashLog` formats cold CSV provenance rows; it does not implement replay state hashing or artifact serialization. |

## Exhaustive 64-File Coverage Checklist

The tag after each file names the candidate or screened ruling that covers its
relevant surface. `clear` means the file was inspected and contributed no
repeated logic or overlapping value shape.

### ArtifactIO (7/7)

- [x] `ReplayArtifactHashLog.cpp` - S8
- [x] `ReplayArtifactHashLog.h` - S8
- [x] `ReplayArtifactSource.h` - clear
- [x] `ReplayPredictionArchive.cpp` - C7, S2
- [x] `ReplayPredictionArchive.h` - S2
- [x] `ReplayV2Artifact.cpp` - S1, S2, S3, S5
- [x] `ReplayV2Artifact.h` - S2, S5

### Capture (7/7)

- [x] `ReplayCaptureLimits.h` - clear
- [x] `ReplayCapturePackets.h` - clear
- [x] `ReplayEventCommand.h` - clear
- [x] `ReplayRecorder.cpp` - S1, S3, S4
- [x] `ReplayRecorder.h` - S4
- [x] `ReplayRetainedMemory.h` - clear
- [x] `ReplayToolPackets.h` - clear

### Prediction (16/16)

- [x] `ReplayAuthoring.h` - clear
- [x] `ReplayAuthoringVelocity.cpp` - clear
- [x] `ReplayPrediction.cpp` - C1, C2
- [x] `ReplayPrediction.h` - C2, C7
- [x] `ReplayPredictionPackets.h` - clear
- [x] `ReplayPredictionPublication.cpp` - C1, C2, C3
- [x] `ReplayPredictionPublication.h` - clear
- [x] `ReplayPredictionPublicationOperations.h` - C3 candidate destination
- [x] `ReplayPredictionReserve.cpp` - C2 owner seam; no duplicate body
- [x] `ReplayPredictionReserve.h` - C2 owner seam; no duplicate body
- [x] `ReplayPredictionScheduling.cpp` - C1 owner seam; no duplicate body
- [x] `ReplayPredictionScheduling.h` - C1 owner seam; no duplicate body
- [x] `ReplayPredictionTopologyPublication.cpp` - C1, C3, C4, C7
- [x] `ReplayPredictionView.h` - C6, C7
- [x] `TrajectoryStore.cpp` - clear
- [x] `TrajectoryStore.h` - clear

### Presentation (16/16)

- [x] `ReplayAuthoringCauseTree.cpp` - clear
- [x] `ReplayAuthoringPackets.h` - clear
- [x] `ReplayCauseFocusSubmission.cpp` - C3 existing shared-row seam; no duplicate body
- [x] `ReplayOverlayLayout.cpp` - C5 consumer; no duplicate packet definition
- [x] `ReplayOverlayLayout.h` - clear
- [x] `ReplayOverlayPackets.h` - C5
- [x] `ReplayOverlayRenderer.cpp` - C5 consumer; no duplicate packet definition
- [x] `ReplayOverlayRenderer.h` - clear
- [x] `ReplayOverlaySurface.h` - clear
- [x] `ReplayPathPackets.h` - C6
- [x] `ReplayPredictionDrawing.cpp` - C1, C3, C4, C7
- [x] `ReplayPresentation.cpp` - C5, C7
- [x] `ReplayPresentation.h` - C5
- [x] `ReplayPresentationPackets.h` - S6
- [x] `ReplayPresentationSubmission.h` - clear
- [x] `ReplayVisualPacket.h` - C7, S1, S5

### Timeline (11/11)

- [x] `ReplayCoordination.h` - clear
- [x] `ReplayIdentity.h` - clear
- [x] `ReplayRestoreService.h` - clear
- [x] `ReplayRestoreTransactions.h` - clear
- [x] `ReplayRuntime.cpp` - C5 canonical-selection producer
- [x] `ReplayRuntime.h` - C5 boundary; no duplicate implementation
- [x] `ReplayScrubber.h` - clear
- [x] `ReplayScrubberTools.cpp` - clear
- [x] `ReplayTimeline.cpp` - clear
- [x] `ReplayTimeline.h` - clear
- [x] `ReplayTimelinePackets.h` - clear

### Validation (7/7)

- [x] `ReplayPredictionArchive.Automation.cpp` - S2 validation consumer
- [x] `ReplayProbeState.h` - S7
- [x] `ReplayValidation.cpp` - S7
- [x] `ReplayValidation.Internal.h` - S7 shared authoritative leaves
- [x] `ReplayValidation.Probes.cpp` - S7
- [x] `ReplayVisualPacketFingerprint.cpp` - S1, S5
- [x] `ReplayVisualPacketFingerprint.h` - S5

Coverage result: 64 checked, 0 deferred, 0 unchecked. Every confirmed candidate
is C1-C7 and therefore has a pending RD1 owner ruling; every screened surface
has an explicit retained/clear disposition above.

## RD1 Decision Boundary

RD1 must record exactly one of `dedup-now`, `cohesion-retain`, or `defer` for
C1-C7. A single owner acceptance of the RD0 recommendations is sufficient. Any
different ruling should name the candidate ID; no source work starts before
those decisions are committed.

## Validation

The desktop shell could not open a separate visible console, so the command ran
in the in-app shell. RD0 consumed exactly one invocation of
`tools\validate_replay_visual_fidelity.bat`; it passed in 432.83 seconds with
exit 0. Launcher proof reported one engine process, one prediction start, one
presented cascade, and zero nested scrub runs. Typed controls passed 17 cases /
75 assertions; the authoritative run passed 2,401 ticks, 200 moved wall bricks,
175 toppled wall bricks, 200 causal nodes, durable save/load evidence, and every
negative/determinism control. No golden, baseline, schema, or source file
changed. RD0 is documentation-only, so no other repository validation is
required.

# Replay Mass-Reduction R0 Census

Date: 2026-07-16  
Plan: `Agentic/Plans/TODO/replay-mass-reduction.md`  
Measured source tip: `51143c9069f7393e32d87106a19d43d2ec5b3ba1`  
Replay source baseline: `c0dd70165` (the intervening commits change only
planning documentation; `git diff c0dd70165..51143c906 --
SkullbonezSource/Runtime/Replay SKULLBONEZ_CORE.vcxproj` is empty)  
Status: Complete. Census, map baseline, reference gates, and owner ratification
are closed.

## Scope And Method

The inventory is the complete output of
`git ls-files SkullbonezSource/Runtime/Replay` at the measured tip. Line counts
are physical lines from PowerShell `Get-Content`; they reproduce the plan's
33,783-line starting total. Every tracked file appears exactly once in one of
the five R0 buckets. A bucket names the file's primary production concern; it
does not claim every line in a mixed file belongs to that concern.

The map baseline links `SKULLBONEZ_CORE.vcxproj` directly with `/MAP` after the
normal Release and Profile solution builds. Linking the project directly is
important: a solution-wide `LINK=/MAP` setting lets later executables overwrite
the requested map. Both accepted map headers read `SKULLBONEZ_CORE`.

## Bucket Totals

| Bucket | Files | Lines | Share |
|---|---:|---:|---:|
| `product-runtime` | 19 | 16,421 | 48.61% |
| `prediction-engine` | 8 | 5,748 | 17.01% |
| `artifact-io` | 4 | 3,808 | 11.27% |
| `probe-harness` | 3 | 992 | 2.94% |
| `presentation-emission` | 8 | 6,814 | 20.17% |
| **Total** | **42** | **33,783** | **100.00%** |

## Per-File Classification

| File | Lines | Bucket | Evidence / primary concern |
|---|---:|---|---|
| `ReplayAuthoring.h` | 436 | `product-runtime` | Velocity-edit, causal-authoring, and branch-provenance owner state. |
| `ReplayAuthoringCauseTree.cpp` | 1,267 | `product-runtime` | Cause rows, authoring input, and focus behavior. |
| `ReplayAuthoringVelocity.cpp` | 890 | `product-runtime` | Velocity-edit picking, dragging, mutation, and authoring overlay policy. |
| `ReplayCoordination.h` | 351 | `product-runtime` | Cross-owner commands/results; it contains a guarded automation view but is not wholly harness-only. |
| `ReplayEventCommand.h` | 153 | `product-runtime` | Value-only durable event commands emitted by runtime owners. |
| `ReplayIdentity.h` | 68 | `product-runtime` | Shared fixed capacities and schema-neutral replay values. |
| `ReplayOverlayLayout.cpp` | 506 | `presentation-emission` | Screen-space geometry shared by overlay input and drawing. |
| `ReplayOverlayLayout.h` | 182 | `presentation-emission` | Overlay rectangles and timing constants. |
| `ReplayOverlayRenderer.cpp` | 1,151 | `presentation-emission` | Late UI/text rendering of scrubber and cause-tree state. |
| `ReplayOverlayRenderer.h` | 135 | `presentation-emission` | Late overlay drawing entry points. |
| `ReplayPrediction.cpp` | 4,424 | `prediction-engine` | Prediction scheduling, private simulation, and publication. |
| `ReplayPrediction.h` | 544 | `prediction-engine` | Prediction owner state and API. |
| `ReplayPredictionArchive.cpp` | 707 | `artifact-io` | Production RVPD encode/decode; only the round-trip verifier at lines 680-706 is harness-only. |
| `ReplayPredictionArchive.h` | 56 | `artifact-io` | RVPD payload codec API used by production save/load as well as probes. |
| `ReplayPredictionDrawing.cpp` | 2,057 | `presentation-emission` | 3D path/ribbon/marker emission from immutable prediction values. |
| `ReplayPredictionReserve.cpp` | 83 | `prediction-engine` | Prediction working-set reserve registration and growth requests. |
| `ReplayPredictionReserve.h` | 45 | `prediction-engine` | Shared prediction reserve-owner declaration. |
| `ReplayPredictionScheduling.h` | 91 | `prediction-engine` | Allocation-free prediction scheduling decisions. |
| `ReplayPredictionView.h` | 128 | `prediction-engine` | Immutable prediction publication seam. |
| `ReplayPresentation.cpp` | 1,308 | `presentation-emission` | Presentation storage, render-pose application, packet publication, and submission telemetry. |
| `ReplayPresentation.h` | 487 | `presentation-emission` | Path, camera, overlay, render-pose, and visual-packet owner values. |
| `ReplayProbeState.h` | 278 | `probe-harness` | Debug/startup probe state and probe workflow values. |
| `ReplayRecorder.cpp` | 3,488 | `product-runtime` | Bounded presentation, solver, event, and delta recording. |
| `ReplayRecorder.h` | 742 | `product-runtime` | Capture records and recorder owners. |
| `ReplayRestoreService.h` | 295 | `product-runtime` | Production retained-solver restore operations. |
| `ReplayRestoreTransactions.h` | 82 | `product-runtime` | Frame-scoped production startup/restore transactions. |
| `ReplayRetainedMemory.h` | 152 | `product-runtime` | Retained ownership and registered replay-growth contracts. |
| `ReplayRuntime.cpp` | 1,484 | `product-runtime` | Top-level replay owner sequencing; one guarded automation-view method is not the file's primary concern. |
| `ReplayRuntime.h` | 447 | `product-runtime` | Composition of the six cohesive replay owners. |
| `ReplayScrubber.h` | 473 | `product-runtime` | Scrub cursor state and live-restore commands. |
| `ReplayScrubberTools.cpp` | 1,582 | `product-runtime` | Scrubber input, inspection camera, and live restore policy. |
| `ReplaySolverSnapshot.h` | 135 | `product-runtime` | Retained solver-state values for rollback. |
| `ReplayTimeline.cpp` | 373 | `product-runtime` | Recording, loading, event, and memory-policy state. |
| `ReplayTimeline.h` | 274 | `product-runtime` | Recorder and retained-track ownership. |
| `ReplayV2Artifact.cpp` | 2,889 | `artifact-io` | Versioned chunked replay artifact writer/reader. |
| `ReplayV2Artifact.h` | 156 | `artifact-io` | Versioned artifact API. |
| `ReplayValidation.cpp` | 3,729 | `product-runtime` | Mixed file: production V2 restore/event replay plus debug probe workflows; the whole TU is not automation-only. |
| `ReplayVisualPacket.h` | 988 | `presentation-emission` | Renderer-bound packet values and visual-buffer hashing. |
| `ReplayVisualPacketFingerprint.cpp` | 611 | `probe-harness` | Canonical visual oracle implementation; all callable users are automation or `_DEBUG` probes. |
| `ReplayVisualPacketFingerprint.h` | 103 | `probe-harness` | Visual fingerprint and comparison API. |
| `TrajectoryStore.cpp` | 326 | `prediction-engine` | Versioned trajectory replacement and prefix publication. |
| `TrajectoryStore.h` | 107 | `prediction-engine` | Bounded trajectory store values/API. |

## Automation Boundary Evidence And Owner Ratification

The activation plan's whole-file statement about `ReplayValidation.cpp` and
`ReplayPredictionArchive.cpp` is not supported by current source. The owner
ratified the narrower, source-accurate boundary in the Codex thread on
2026-07-16 before R1/R2 may edit build membership.

| Candidate | Current evidence | Ratified owner ruling |
|---|---|---|
| `ReplayVisualPacketFingerprint.cpp` whole TU | Its call sites are the already Automation-only `InteractionAutomationController.cpp:2624-2626,3320-3363` and the `_DEBUG` load-probe block in `ReplayValidation.cpp:2976-2987`. Release/Profile maps contain no fingerprint function; only translation-unit-triggered math identity constants remain (64/200 attributed bytes). | **DIAGNOSTICS-ONLY whole TU.** Include it in Automation and Debug, exclude it from Release/Profile, and use it as R1's link-boundary pilot. This preserves both existing consumers. |
| `ReplayPredictionArchive.cpp` | `ReplayPrediction::LoadArchive` and `BuildArchive` call production codec entry points at `ReplayPrediction.cpp:4009-4015`. Only `VerifyReplayPredictionArchiveRoundTrip` at lines 680-706 is called solely by the Automation configuration (`InteractionAutomationController.cpp:3539,3568`). | **KEEP codec in all builds; AUTOMATION-ONLY lines 680-706.** Move the verifier into an Automation-only TU in R2, not the whole codec TU. |
| `ReplayValidation.cpp` | Production maps retain `CaptureCurrentSolverHash`, `ConfigureStartupWorkflows`, `RestoreV2ArtifactTargetState[Impl]`, and `RunStartupWorkflows`. `_DEBUG` blocks occupy lines 79-277, 370-763, 984-1023, 2213-2249, 2263-2270, 2275-2281, 2303-2470, 2474-3196, 3281-3295, and 3498-3729. The associated CLI fields and `ReplayProbeRunner` methods are also `_DEBUG`-guarded today. | **KEEP production restore/event replay in all builds; DEBUG-ONLY the guarded legacy probe blocks after a reachability-checked physical split.** Do not silently migrate or delete their configuration availability. Never move the whole TU behind Automation. |
| Product behavior for probe entry points | `InteractionAutomationController.cpp` is already excluded from every configuration except Automation in `SKULLBONEZ_CORE.vcxproj:338-340`; `RunFrame.cpp` constructs the automation view only under `SKULLBONEZ_AUTOMATION_DIAGNOSTICS`; legacy replay probes are `_DEBUG`. Product maps contain no probe-runner or fingerprint callable symbols. | **ABSENT, not no-op stubs, in Release/Profile.** Configuration-appropriate diagnostics TUs remain linkable in Automation or Debug; product headers/builds do not declare or link their entry points. |

The ratified ruling preserves production save/load/restore and existing Debug
probe coverage while narrowing the campaign from "remove named files" to
"remove proven harness functions from Release/Profile." It also resolves the
plan's ambiguous use of "automation-only": the link boundary is diagnostics
configuration membership, not a forced migration of `_DEBUG` CLI probes into
Automation.

## Duplication Census

These are suspected pairs, not pre-approved rewrites. R3/R4 may unify a row
only after proving identical arithmetic, byte order, failure behavior, and
emission order. A contradicted premise is recorded as KEEP rather than forced
into a shared abstraction.

| ID | Mechanic / evidence | Suspected pair(s) | R0 disposition |
|---|---|---|---|
| D1 | Scalar/byte archive writers and readers | `ReplayPredictionArchive.cpp:58-208` (`ArchiveWriter`/`ArchiveReader`) vs `ReplayV2Artifact.cpp:148-225` (`ByteCursor`, `AppendPod`, `ReadPod`, byte/vector helpers) | **R3 candidate.** Both are little-endian scalar codecs, but RVPD explicitly shifts integral bytes and enforces a 128 MiB cap while V2 copies trivially-copyable host POD and uses chunk-bounded cursors. Unify only with byte-identical fixtures and unchanged caps/errors. |
| D2 | Presentation/solver field traversal | `ReplayRecorder.cpp:948-1510` hashes retained fields; `ReplayV2Artifact.cpp:324-705,1112-1754` writes/reads many of the same values | **KEEP as distinct mechanics.** Hashing live retained state is not artifact serialization; sharing field-walk policy would couple recorder authority to the wire schema. No varint or section codec exists in `ReplayRecorder.cpp`. |
| D3 | FNV byte accumulation | `ReplayRecorder.cpp:75-76,948-1079`; `ReplayVisualPacket.h:55-84`; `ReplayVisualPacketFingerprint.cpp:40-63`; `ReplayV2Artifact.cpp:2066-2075` | **R3 review candidate, not assumed duplicate.** Recorder uses a different offset basis (`...56037`) from visual hashing (`...65603`); visual call sites also hash different counts/field order. A seed-parameterized primitive is possible only if generated bytes/hashes remain exact. |
| D4 | Ordinary vs baseline path-segment quota/accounting | `ReplayPredictionDrawing.cpp:141-223` contains two wrappers over distinct tracer APIs | **R4 candidate within one owner TU.** The quota mechanics repeat, but baseline segments intentionally omit lane/emphasis behavior. Preserve record counts and submission order. |
| D5 | Repeated trajectory-record loops | Shared emitter `ReplayPredictionDrawing.cpp:724-799` is called from root/past/child lanes at `933-1287`; small-scene/ragdoll/affected-body loops at `980-1573` repeat stride/color/quota decisions | **R4 candidate within prediction drawing.** Prefer value/parameter composition around the existing emitter; no callback chain in the hot presentation path. |
| D6 | "Overlay emission" across the three files named by the activation plan | `ReplayPredictionDrawing.cpp:1576-1763` emits 3D ribbons/markers; `ReplayOverlayRenderer.cpp:87-1151` emits screen-space text/panels; `ReplayPresentation.cpp:1188-1287` publishes packets and records telemetry | **KEEP as separate concerns.** The cross-file segment/ribbon duplication premise is contradicted: only prediction drawing emits replay path segments. Do not create a new catch-all emission owner. |
| D7 | Three render-pose application loops | `ReplayPresentation.cpp:815-1018` applies presentation, solver, and prediction sample shapes to render instances | **R4 candidate if bit-neutral.** The loops are structurally similar but consume different body records/fallback rules; a value/template helper must preserve exact row order and ghost-request behavior. |
| D8 | Visual fingerprint vs archived visual-packet comparison | `ReplayVisualPacketFingerprint.cpp:241-433` fingerprints packet state; `437-607` compares packet/archive fields; `ReplayV2Artifact.cpp:1930-2064` writes/reads RVIS rows | **KEEP owner boundaries, consider only leaf hash primitives.** The oracle, comparator, and wire codec deliberately answer different questions. |

## Release/Profile Link-Map Baseline

### Commands

The effective measurement command for each configuration was:

```powershell
$env:LINK='/MAP:"C:\SkullbonezCore\TestOutput\agent_logs\replay-r0-<config>.map"'
Remove-Item <config>\SKULLBONEZ_CORE.exe -Force
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  .\SKULLBONEZ_CORE.vcxproj /p:Configuration=<config> /p:Platform=x64 `
  /nologo /v:normal '/clp:Summary;PerformanceSummary' /warnaserror
```

Release succeeded in 20.48 s with 0 warnings/0 errors; Profile succeeded in
8.28 s with 0 warnings/0 errors.

### Attribution Method

For each map, the parser unions the public and static symbol rows by section
RVA, sorts unique RVAs within each section, and charges the interval to the next
symbol (or section end) only when the starting RVA names exactly one object.
The replay total sums the 16 `.cpp` object basenames under
`SkullbonezSource/Runtime/Replay`; headers and similarly prefixed objects from
other directories are excluded. Same-RVA COMDAT folds naming multiple objects
are left unassigned. This is a reproducible map-attributed contribution, not a
claim that linker padding or every folded byte has a unique source owner.

| Replay object | Release bytes | Profile bytes |
|---|---:|---:|
| `ReplayAuthoringCauseTree.obj` | 15,960 | 17,252 |
| `ReplayAuthoringVelocity.obj` | 9,808 | 10,468 |
| `ReplayOverlayLayout.obj` | 6,292 | 7,528 |
| `ReplayOverlayRenderer.obj` | 79,728 | 19,916 |
| `ReplayPrediction.obj` | 55,564 | 56,640 |
| `ReplayPredictionArchive.obj` | 2,308 | 22,796 |
| `ReplayPredictionDrawing.obj` | 20,328 | 19,708 |
| `ReplayPredictionReserve.obj` | 588 | 744 |
| `ReplayPresentation.obj` | 12,340 | 15,156 |
| `ReplayRecorder.obj` | 134,032 | 136,216 |
| `ReplayRuntime.obj` | 41,740 | 28,916 |
| `ReplayScrubberTools.obj` | 19,448 | 15,444 |
| `ReplayTimeline.obj` | 7,356 | 9,424 |
| `ReplayV2Artifact.obj` | 72,960 | 74,300 |
| `ReplayValidation.obj` | 19,748 | 21,336 |
| `ReplayVisualPacketFingerprint.obj` | 64 | 200 |
| **Replay map-attributed total** | **498,264** | **456,044** |

| Configuration | Engine image bytes | Replay attributed | Share of image | Same-RVA ambiguous bytes (all objects) |
|---|---:|---:|---:|---:|
| Release | 3,188,224 | 498,264 | 15.63% | 23,080 |
| Profile | 4,657,664 | 456,044 | 9.79% | 34,104 |

Release uses whole-program optimization, so its per-object ownership and
folding differ materially from Profile; compare each configuration only to its
own R7 map. The aggregate numbers, not raw `.obj` file sizes containing debug
and linker metadata, are the campaign's product-footprint baseline.

### Local Evidence Artifacts

The maps/logs are machine-local ignored artifacts; this report preserves their
measurements and hashes so later evidence can detect accidental substitution.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `TestOutput/agent_logs/replay-r0-release.map` | 4,038,359 | `BF86847501D6B5FCFC6DEB31AD3BAFC6C4407EB4D83AB67E33FCB7E175B7E8EF` |
| `TestOutput/agent_logs/replay-r0-profile.map` | 4,716,652 | `F4EBF467801C6C6A26F3CB45B0D90AD19E2FB935FDEB736F83ACDF771B2E3D91` |
| `TestOutput/agent_logs/replay-r0-release-project-build-transcript.txt` | 3,950 | `E0D24C0392DFADDFABF2CAA62C46FC551BB8984B8017FD1DC376FA9C411443BE` |
| `TestOutput/agent_logs/replay-r0-profile-project-build-transcript.txt` | 3,950 | `1FAAE5C59ADAAE26BE655E1E5316284F57FB0E59B7E1A7D5A40A886292F1894C` |

## R0 Completion

The owner answered yes to both binding questions on 2026-07-16: approve the
configuration-appropriate diagnostics matrix and omit probe entry points and
no-op stubs from Release/Profile. The census reconciles 42/42 tracked files,
33,783/33,783 physical lines, zero missing/extra/duplicate rows, and zero count
mismatches. R0 therefore advances the active ledger to 1/8 (13%).

## Reference Gate Evidence

`tools\validate_replay_visual_fidelity.bat` ran exactly once for R0 from the
behavior-unmodified source tip. The invocation took 459.14 s and exited 0. It
started the command's one authoritative engine process and generated prediction
once; no golden or provenance field changed. Key result:

```text
PASS replay visual fidelity: ticks=2401 moved_wall_bricks=200 toppled_wall_bricks=187 causal_nodes=199 presented_cascades=1 saved_loaded_ticks=62 first_reveal=0 last_reveal=2400
PASS: one-presentation 200-box visual fidelity, causal reveal proof, durable artifact, and false-pass controls.
```

Every negative, incomplete-horizon, causal, semantic-packet, artifact-byte,
prediction-artifact, and determinism control also reported its expected first
divergence. Transcript:
`TestOutput/agent_logs/replay-r0-mega-gate-transcript.txt`, 4,479 bytes,
SHA-256
`915524620CD7065D40BF181A5594EC2539E72D7BCE51947B32FEC8B31D864EE2`.

`tools\validate_tests.bat` then ran separately, took 2.14 s, and exited 0:

```text
[doctest] test cases:   202 |   202 passed | 0 failed | 0 skipped
[doctest] assertions: 12595 | 12595 passed | 0 failed |
VALIDATE_TESTS: ALL PASSED
```

Transcript: `TestOutput/agent_logs/replay-r0-validate-tests-transcript.txt`,
3,428 bytes, SHA-256
`DF6C5847E8A80D50085818F012D22B1C8D0622679F9B296AE932B9D709BC5988`.

# Replay R5 Closure Evidence — 2026-07-11

## Outcome

R5 closes the replay plan with a named test matrix, a reconciled 26-file / 24,904-line source inventory, stable-id restore coverage, preflighted restore mutation, and a production owner boundary that no longer accepts `ReplayLiveWorld`.

`ReplayRuntime.h` briefly exceeded the 1,500-line threshold while the restore operands were narrowed. The frame-scoped definitions now live in the 94-line `ReplayRuntimeOwnerViews.h`; the primary header is 1,485 lines. Five implementation files remain above 1,500 lines and have the cohesion evidence below.

## Behavioral Test Map

| Behavior | Umbrella or named gate |
|---|---|
| Stable replay identity with deliberately swapped row hints | `TestPhysicsHandles.cpp`, CPU umbrella |
| Scrub selection and non-mutating presentation override | `tools\validate_replay_scrub.bat` |
| Solver checkpoint restore and hash verification | `tools\validate_replay_scrub.bat` |
| Target restore, branch provenance, missing-target rejection, and injected post-mutation rollback | `tools\validate_replay_v2_artifact.bat` |
| Prediction horizon and replay interaction routing | `tools\validate_replay_scrub.bat`, `tools\validate_interaction_clicks.bat` |
| Recorder windows, delta/keyframe resolution, and reserve denial | `TestReplayRecorder*.cpp`, `TestReserveAllocator.cpp`, CPU umbrella and `tools\validate_perf.bat` |
| Physics determinism after replay identity/restore changes | `tools\validate_physics.bat` |
| Replay overlay submission | `tools\validate_dx12_renderer.bat` |

## Restore Boundary And Failure Propagation

- Production startup receives `ReplayStartupLoadInput`, which exposes only camera and interaction owners.
- Production live restore composes `ReplaySolverSampleRestoreContext`, `ReplayRestoreTransaction`, and `ReplayArtifactTopologyOwners`; none stores a Run pointer, callback pack, or multi-domain host reference.
- `ReplayProbeWorld` is declared only under `_DEBUG` and exists solely as the named automation composition fixture.
- `ReplayRestoreService::ResolveBodiesForRestore` resolves `ReplayBodyId` first and treats `ModelRowHint` only as a repairable cache. The CPU regression swaps both hints, proves the stable handles are still selected, and rejects duplicate stable ids.
- Scene topology trim preflights body, authored-descriptor, presentation, render-presentation, and entity counts before the first mutation. A commit failure after successful preflight is Lane F because earlier owners may already have retired handles.
- A recoverable post-mutation restore failure returns only after a pre-mutation capture of actual live state is reapplied and hash-verified. Missing backup, failed application, or rollback hash mismatch is Lane F; the runtime never continues from a half-restored solver or scene.

## Reconciled Source Inventory

Inventory command:

```powershell
git ls-files SkullbonezSource/Runtime/Replay
```

The new uncommitted owner-view header was added to that tracked inventory before final reconciliation. Counts are physical lines from `Get-Content`.

| Lines | File |
|---:|---|
| 4,764 | `RunReplayTools.cpp` |
| 3,708 | `ReplayRuntime.cpp` |
| 3,086 | `ReplayRecorder.cpp` |
| 3,004 | `RunReplayProbes.cpp` |
| 2,546 | `ReplayV2Artifact.cpp` |
| 1,485 | `ReplayRuntime.h` |
| 1,071 | `ReplayOverlayRenderer.cpp` |
| 956 | `RunReplayScrubberTools.cpp` |
| 813 | `RunReplayVelocityEdit.cpp` |
| 696 | `ReplayRecorder.h` |
| 370 | `RunReplayCauseTreeTools.cpp` |
| 293 | `TrajectoryStore.cpp` |
| 286 | `ReplayOverlayLayout.cpp` |
| 282 | `RunReplayQueryTools.cpp` |
| 261 | `ReplayInteractionController.cpp` |
| 225 | `ReplayRestoreService.h` |
| 152 | `ReplayRetainedMemory.h` |
| 137 | `ReplayV2Artifact.h` |
| 135 | `ReplaySolverSnapshot.h` |
| 110 | `ReplayInteractionController.h` |
| 103 | `TrajectoryStore.h` |
| 98 | `ReplayOverlayLayout.h` |
| 96 | `ReplayOverlayRenderer.h` |
| 94 | `ReplayRuntimeOwnerViews.h` |
| 80 | `ReplayPredictionReserve.cpp` |
| 42 | `ReplayPredictionReserve.h` |

Total: 26 files, 24,904 lines. Files over 1,500 lines: five.

## Over-1,500 Cohesion Justifications

### `RunReplayTools.cpp` — 4,764 lines

This is the prediction/trajectory pipeline, not a miscellaneous replay bucket. Its sections resolve stable bodies, seed and step the private prediction engine, publish an atomic frame prefix, derive the contact/future-node graph, retain baseline and trajectory records, and emit the corresponding fixed-capacity ribbons and markers. They share `RunReplayPredictionState`, `ReplayTrajectoryStore`, publication counters, reserve accounting, and the rule that drawing may consume only a completed prefix. A mechanical split would require a mutable internal context or duplicate the publication protocol. The deletion condition for this justification is a typed immutable prediction snapshot that lets simulation publication and overlay projection become independent owners.

### `ReplayRuntime.cpp` — 3,708 lines

This file implements the state transitions of the one `ReplayRuntime` owner: recorder configuration, memory policy, timeline/branch reset, interaction state, prediction worker lifecycle, capture, and artifact presentation. The shared invariants are worker quiescence before reset, synchronized presentation/solver/event cursors, and one memory policy across retained owners. Splitting translation units today would shorten files without reducing authority and would create a shared mutable internal header. The justification expires when one of those state groups becomes a concrete owner with a typed command/value boundary rather than a `ReplayRuntime` forwarding surface.

### `ReplayRecorder.cpp` — 3,086 lines

Presentation, solver, and event recorders share one retained-sample codec: reserve-policy registration, metadata dictionaries, keyframe/delta promotion, ring-slot acquisition, chronological resolution, hash logging, and memory-category accounting. Presentation and solver code intentionally mirror byte-stability and compaction rules, while the event recorder is the small third lane governed by the same retention window. A split becomes justified when the common keyframe/delta and reserve machinery is extracted as a value codec that does not expose either recorder's mutable ring internals.

### `RunReplayProbes.cpp` — 3,004 lines

This is the executable transaction specification for replay restore. Production target restore and the Debug probes share artifact selection, supported-event decoding, generated-topology reconstruction, deterministic stepping, target-hash validation, diagnostic formatting, and rollback evidence. Keeping those helpers private prevents probe and production interpretations of a v2 restore from drifting. The justification expires if restore gains another production caller or a typed restore-program/value layer makes event decoding and stepping independently testable without the probe fixture.

### `ReplayV2Artifact.cpp` — 2,546 lines

This file is one symmetric binary wire codec. The writer and reader share chunk identifiers, exact ABI byte counts, dictionary order, frame headers, solver snapshot fields, event cursors, bounds checks, and manifest/index offsets. Co-location makes schema changes reviewable as paired encode/decode edits and prevents a private wire-helper header from becoming another compatibility surface. The justification expires when the format gains a new version with a separately versioned codec module; v2 remains frozen as one owner until then.

## Comment Audit

Audit record: this report. Checked: 14 touched source-bearing files. Deferred/unchecked: 0. The touched files were inspected against `Agentic/Reference/comment-style-guide.md`; every file retains a learning header. The new owner-view header documents borrow lifetime, production/debug authority, and the no-retention invariant. Restore preflight, stable-id resolution, partial-mutation hazards, and Lane F rollback behavior have local concept/invariant/hazard comments. The focused doctest and validation-tool changes also retain their existing purpose, mental-model, glossary, and invariant headers.

## Adversarial Review

The first read-only pass found three blocking defects: production use of the broad `ReplayLiveWorld` fixture, recoverable partial mutation during restore, and no restore-level stale-hint proof. The required repeat pass found duplicate stable ids were not rejected and the failure gate stopped before mutation. All findings were fixed. The repeat pass was clean after the v2 gate injected a target-hash mismatch, reapplied the actual pre-mutation live capture, and verified the rollback solver hash.

## SkullScope Cost

The v2 gate generated three runtime traces and one bounded exported slice:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_generated_topology.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe TestOutput\validation\replay_v2\replay_generated_topology_probe.skreplay --physics-diag TestOutput\validation\replay_v2\replay_generated_topology_runtime.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --replay-restore-failure-file-probe TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson
tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay export-skullscope --frames 0:5 --out TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact
```

Every `physics_query` command used:

```text
tools\physics_query.bat TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson restore --limit 4
tools\physics_query.bat TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson summary
```

On-disk artifact sizes were 83,316 bytes for the solver-one runtime trace, 189,771 bytes for the generated-topology runtime trace, 72,354 bytes for the restore-failure trace, and 2,465 bytes for the exported slice. SQLite caches were 225,280 and 204,800 bytes for the queried failure and exported traces. Model-ingested output was bounded to 1,812 bytes for `restore --limit 4` and 1,844 bytes for `summary`, 3,656 bytes total; no raw trace or SQLite content was ingested.

## Validation

- `tools\validate_fast.bat` — final staged-file run passed in 20.7s with 20 candidates and zero size violations; an earlier 29.0s run proved the project-filter extension for `ReplayRuntimeOwnerViews.h`.
- `tools\validate_all_cpu_tests.bat` — passed in 11.5s: 125/125 doctest cases, 2,708 assertions, runtime interaction policy Debug/Release, scene parser, and DX12 architecture.
- `tools\validate_replay_scrub.bat` — passed in 75.5s.
- `tools\validate_replay_v2_artifact.bat` — passed in 36.8s, including preflight and post-mutation rollback failure rows.
- `tools\validate_interaction_clicks.bat` — passed in 9.2s with both reports `ok=1`.
- `tools\validate_physics.bat` — passed in 13.6s with a 20,001-line byte-exact solver baseline.
- `tools\validate_perf.bat` — completed successfully in 32.8s.
- `tools\validate_dx12_renderer.bat` — passed in 22.8s with zero InfoQueue errors and all baselines matched.
- `tools\validate_full.bat` — passed in 50.1s with CPU, DX12, standalone physics, and byte-exact physics lanes.

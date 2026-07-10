# Replay R1 Obsolete-Path Cleanup

Date: 2026-07-10
Branch: `engine-cleanup-10th-july`

## Closure result

Replay now has 24 tracked source-bearing files / 23,347 physical lines. R1
deleted four files totaling 502 lines; after owner code moved into retained
files, the scoped source total fell by 467 lines without a replacement
compatibility wrapper.

| Removed surface | Caller/workflow evidence | Replacement or deletion proof |
|---|---|---|
| `ReplayExporter.cpp/.h` | Both legacy JSON overloads had zero callers outside their own declarations; the supposed solver UI path was never invoked | Binary v2 is the sole saved replay artifact. `ReplayV2Artifact` retains presentation, solver hashes/checkpoints, and events. |
| `RunReplayImportExport.cpp/.h` | One scrubber caller; the helper only selected a numbered path, forwarded to `ReplayRuntime`, and wrote runtime-owned UI status | `ReplayRuntime::SavePresentationFromScrubber` owns sequence state, cold save, and status publication. The free forwarding module and global static counters are gone. |
| `--replay-save-test` / underscore spelling | No tool or test used this synonym; the canonical save probe is `--replay-save-probe` | Canonical hyphen spelling plus the parser-wide underscore syntax alias remain. |
| `--replay-play` / underscore spelling | No tool or test used this synonym; the supported load workflow is `--replay-load` | Canonical load spelling plus the parser-wide underscore syntax alias remain. |
| `ReplaySolverSampleVisitor` plus `void*` | One internal past-trajectory rebuild caller | `ReplaySolverRecorder::ForEachSampleChronological` is a typed, allocation-free visitor template; no stored callback/user-data bridge remains. |
| Deleted wire kind 2 query compatibility | R0 proved the mixed command owner and format were already deleted | Query and validation accept explicit owner-action kind 10 only. |

The remaining `ReplayLiveRestoreApi` callback pack and broad mutable
`ReplayRuntime` accessors are live authority paths, not abandoned fallbacks.
They are the named R2 deletion boundary: typed replay frame output must carry
restore/camera/overlay commands before those paths can be removed. Negative
model-index fallback values still used by physics contacts are data sentinels,
not compatibility APIs.

## Regression discovered during the gate

The first replay scrub gate passed scrub and restore but failed the prediction
run because a terrain contact uses `bodyB=-1`. Replay ragdoll classification
sent that sentinel into `SceneEntityStore::At`, producing a fatal index error.
Replay contact helpers now classify only live scene rows and preserve terrain
sentinels unchanged. The production 200-ragdoll prediction determinism lane is
the practical regression test because the lightweight replay-recorder unit
target does not link the runtime prediction/contact topology implementation.
The complete replay gate passed after the fix.

## Validation evidence

| Command | Result | Time |
|---|---|---:|
| `tools\validate_build.bat Profile` | zero-warning build | 14.4s |
| `tools\validate_fast.bat` (final source) | format, metadata, size, tests, Profile/Debug builds passed | 19.1s |
| `python tools\validate_project_filters.py` | 589 project/filter items, 0 errors | 1.2s |
| `python tools\check_allocation_policy.py --self-test` | synthetic cases passed | 0.1s |
| `python tools\check_allocation_policy.py --repo .` | 302 files, 0 allowlist errors | 7.1s |
| `tools\validate_all_cpu_tests.bat` (final source) | all four CPU lanes passed; 2,633 doctest assertions | 10.7s |
| `tools\validate_replay_scrub.bat` | scrub, restore, prediction determinism, and submitted geometry passed | 82.9s |
| `tools\validate_replay_v2_artifact.bat` | all save/load/restore/query probes passed | 25.3s |
| `tools\validate_interaction_clicks.bat` | inspect and replay prediction reports `ok=1` | 8.7s |
| `tools\validate_full.bat` | CPU umbrella, DX12 0 InfoQueue/matching captures, standalone physics, byte-exact 20,001-line physics baseline | 48.4s |

## SkullScope query accounting

Trace commands:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson
```

Queries:

```text
tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8
tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8
```

Raw on-disk artifacts were 54,932-byte scrub NDJSON + 225,280-byte scrub
SQLite, and 54,912-byte restore NDJSON + 225,280-byte restore SQLite. Bounded
query output exposed to the model was 1,512 bytes for scrub and 967 bytes for
restore, 2,479 bytes total. No raw trace or SQLite content was ingested.

## Comment audit

The touched-file comment-style audit checked 9/9 source files with no deferrals:
`Init.cpp`, `ReplayRecorder.cpp/.h`, `ReplayRuntime.cpp/.h`,
`ReplayV2Artifact.cpp/.h`, `RunReplayScrubberTools.cpp`, and
`RunReplayTools.cpp`. The new CLI ownership, typed iteration, artifact-format,
save-sequencing, and terrain-sentinel contracts have local invariant comments.
No subsystem checklist was required for this bounded touched-file pass.

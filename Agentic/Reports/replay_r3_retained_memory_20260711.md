# Replay R3 Retained Memory Ownership Evidence

Date: 2026-07-11
Branch: `engine-cleanup-10th-july`
Owning plan: `Agentic/Plans/TODO/replay-architecture-and-right-sizing.md` R3

## Durable ownership

`ReplayRetainedMemory.h` is the single policy table for the four retained data
families:

| Data | Value | Lifetime owner | Runtime retained | Durable artifact |
|---|---|---|---:|---:|
| Presentation | `ReplayPresentationSample` | `ReplayRecorder` | yes | yes |
| Solver | `ReplaySolverFrameSample` | `ReplaySolverRecorder` | yes | yes |
| Prediction | `RunReplayPredictionFrame` coherent published prefix | `ReplayRuntime` prediction/trajectory owners | yes | no |
| V2 artifact | `ReplayV2Document` | `ReplayV2Artifact` cold I/O | no | yes |

`ReplayPresentationSample` is the sole durable per-frame visual extension
seam. A future visual feature extends that value (or a value it owns), its
delta/hash behavior, and v2 serialization together; it may not create a
parallel retained timeline. Solver samples remain the authoritative
restore/checkpoint value. V2 artifact allocation is cold I/O and is not a live
replay growth owner.

## Growth policy and sizing

All live replay growth owners are registered for the Replay phase, use fixed
allocator counters, have one hard byte ceiling, and name exhaustion behavior:

| Owner | Measured high-water | Final hard cap | Headroom | Exhaustion |
|---|---:|---:|---:|---|
| `replay_recorder_samples` | 6,206,626 bytes | 32 MiB | >5x | Lane F; partial scrub state is forbidden |
| `replay_solver_snapshot` | 1,437,696 bytes | 8 MiB | >5x | Lane F; partial restore state is forbidden |
| `replay_prediction_working_set` | 211,376,304 bytes | 256 MiB | 57,059,152 bytes | cancel/truncate before publishing an incoherent prefix |

The recorder measurement came from the final guarded 60-frame probe:
`Debug\\SKULLBONEZ_CORE.exe --renderer dx12 --scene
SkullbonezData\\scenes\\physics_roll.scene.json --fixed-step --frames 60
--replay on --memory-dump
TestOutput\\validation\\replay_r3_memory_guarded.json --vsync off
--allocation-guard measure`.

That JSON reported recorder high-water 6,206,626 bytes, 448 growths, zero
failures; solver-snapshot high-water 357,198 bytes, one growth, zero failures;
and prediction unregistered because the probe did not enable prediction.
Historical prediction/solver measurements remain in the policy table because
they are the larger sizing evidence.

`RuntimeReserveAllocator` now enforces replay byte caps across all active
allocations attributed to one owner, rather than granting the full cap
independently to every vector target. A fixed-registry stats view exposes
active/high-water bytes, growth/denial counters, high-water capacity, and last
growth frame without allocating. `ReplayRuntime::CollectMemoryStats` copies
the three stable rows into main-memory diagnostics, and memory dumps emit them
as `replay.growth_owners[]`.

Recorder reserves always request policy approval; turning allocation-hook
measurement off no longer bypasses hard-cap approval. Recorder and solver
denials use explicit Lane F diagnostics. Prediction denial continues to return
failure so the current build can be cancelled or left partial without
publishing an incoherent prefix.

## Behavioral coverage

Focused CPU cases prove:

- the four durable ownership rows and three growth policies are complete;
- measured high-water values remain below the evidence-backed caps;
- recorder/solver exhaustion is fatal while prediction exhaustion cancels;
- fixed-registry owner stats are queryable by handle and owner name;
- replay byte targets share one aggregate active-allocation ceiling and a
  denied over-budget request increments the owner failure counter.

The final doctest suite passed 124/124 cases and 2,698/2,698 assertions.

## Validation

- Focused Profile build: 4.5s, zero warnings/errors.
- Final `tools\\validate_fast.bat`: 38.8s.
- Allocation checker self-test plus repository audit: 8.8s including project
  filters; 304 files scanned, zero allowlist errors.
- `tools\\validate_all_cpu_tests.bat`: 10.9s, all four CPU lanes.
- Final `tools\\validate_replay_scrub.bat`: 75.3s; scrub, restore,
  prediction fingerprint, and submitted-geometry stability passed.
- Final `tools\\validate_replay_v2_artifact.bat`: 25.7s.
- Final `tools\\validate_interaction_clicks.bat`: 9.1s, both reports
  `ok=1`.
- `tools\\validate_physics.bat`: 13.1s, 20,001-line byte-exact baseline.
- Final `tools\\validate_perf.bat`: 31.9s, allocation guard and DX12/physics
  thresholds passed.
- Final `tools\\validate_full.bat`: 49.2s; CPU umbrella, zero-warning
  builds, zero DX12 InfoQueue errors, matching screenshots, standalone physics
  smoke, and byte-exact physics passed.
- Guarded memory-dump probe: 3.5s, valid JSON with all three growth-owner rows.

## SkullScope accounting

The scrub gate ran exactly:

`C:\\SkullbonezCore\\Debug\\SKULLBONEZ_CORE.exe --renderer dx12 --vsync
off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json
--frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag
C:\\SkullbonezCore\\Debug\\replay_scrub.physicsdiag.ndjson`

Query: `tools\\physics_query.bat
Debug\\replay_scrub.physicsdiag.ndjson replay --limit 8`.

The scrub trace was 54,932 bytes and its SQLite cache 225,280 bytes. Model-read
query output was 1,512 bytes.

The restore gate ran exactly:

`C:\\SkullbonezCore\\Debug\\SKULLBONEZ_CORE.exe --renderer dx12 --vsync
off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json
--frames 120 --replay on --replay-seconds 1 --replay-restore-test
--physics-diag
C:\\SkullbonezCore\\Debug\\replay_restore.physicsdiag.ndjson`

Query: `tools\\physics_query.bat
Debug\\replay_restore.physicsdiag.ndjson restore --limit 8`.

The restore trace was 54,912 bytes and its SQLite cache 225,280 bytes.
Model-read query output was 967 bytes. Total GPT-read query output was 2,479
bytes; raw NDJSON/SQLite artifact bytes are reported separately above and were
not ingested.

## Comment audit

The touched-file audit covered 14 source-bearing files: 14 checked, 0 deferred,
0 unchecked. The owning checklist/evidence path is this report plus R3 in
`Agentic/Plans/TODO/replay-architecture-and-right-sizing.md`.


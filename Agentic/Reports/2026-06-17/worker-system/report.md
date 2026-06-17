# Roadmap Item Report: worker-system

<!--
Embed visual evidence inline throughout the report wherever it helps explain the
work: screenshots, focused crops, heat maps, image diffs, artifact previews, and
before/after architectural diagrams. Do not collect visuals in a standalone
image section. Err on the side of more useful images and diagrams rather than
fewer. Every committed image must live under images/ beside this Markdown file
and be referenced with a relative Markdown link.
-->

## What Changed, In Plain English

- Added SkullbonezCore::Threading worker infrastructure with a fixed startup worker pool, deterministic ParallelFor chunking, ordered chunk-local collection and merge, and a CPU fence.
- Added worker-backed AmortizedTask infrastructure for latency-tolerant multi-frame work.
- Added a Debug lock-order validator and TrackedMutex wrapper for ABBA-style cycle detection.
- Exposed worker_threads, physics_parallel, and shadow_parallel_prep through engine.cfg/config, with --workers and --worker-self-test CLI coverage.
- Initialized the worker pool once after startup parsing and shut it down before process exit. worker_threads=0 remains the explicit disabled baseline.
- Fixed GUI console output redirection so worker self-tests and orchestration launches can be mirrored into logs.

## At A Glance

- Source plan: `Agentic/Plans/Done/worker-system-plan.md`
- Archived plan: `Agentic/Plans/Done/worker-system-plan.md`
- Branch: `codex/worker-system`
- Implementation commit: `43a82d77d81189532ffb84e42bff7f878294ffc2`
- Report commit: `pending`
- Report web URL: pending until report-only commit is pushed
- PR: ``
- Merge SHA: ``
- Final state: `done`
- Queue state: `done`
- Queue-state commit: `pending`
- Started: `2026-06-17T02:47:11.320089+00:00`
- Finished: `2026-06-17T03:19:22.127233+00:00`
- Elapsed: `pending`

## Progress Timeline

- 2026-06-17T02:47:11.320089+00:00: `start` ready -> running
- 2026-06-17T03:11:13.445659+00:00: `worker_done` running -> reviewing
- 2026-06-17T03:11:21.336804+00:00: `review_ready` reviewing -> verifying
- 2026-06-17T03:18:19.271784+00:00: `accepted` verifying -> validating
- 2026-06-17T03:19:14.366439+00:00: `passed` validating -> reporting
- 2026-06-17T03:19:22.127233+00:00: `report_committed_no_pr` reporting -> done

## Timings

- tools\validate_build.bat Profile: 82.8s
- Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 0: 0.9s
- Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 2: 0.9s
- tools\validate_full.bat: 3.2s
- tools\validate_full.bat: 190.3s

## Implementation

- Added SkullbonezCore::Threading worker infrastructure with a fixed startup worker pool, deterministic ParallelFor chunking, ordered chunk-local collection and merge, and a CPU fence.
- Added worker-backed AmortizedTask infrastructure for latency-tolerant multi-frame work.
- Added a Debug lock-order validator and TrackedMutex wrapper for ABBA-style cycle detection.
- Exposed worker_threads, physics_parallel, and shadow_parallel_prep through engine.cfg/config, with --workers and --worker-self-test CLI coverage.
- Initialized the worker pool once after startup parsing and shut it down before process exit. worker_threads=0 remains the explicit disabled baseline.
- Fixed GUI console output redirection so worker self-tests and orchestration launches can be mirrored into logs.

## Changed Files

- `SKULLBONEZ_CORE.vcxproj`
- `SKULLBONEZ_CORE.vcxproj.filters`
- `SkullbonezData/engine.cfg`
- `SkullbonezSource/SkullbonezAmortizedTask.cpp`
- `SkullbonezSource/SkullbonezAmortizedTask.h`
- `SkullbonezSource/SkullbonezConfig.cpp`
- `SkullbonezSource/SkullbonezConfig.h`
- `SkullbonezSource/SkullbonezFence.h`
- `SkullbonezSource/SkullbonezInit.cpp`
- `SkullbonezSource/SkullbonezLockOrderValidator.cpp`
- `SkullbonezSource/SkullbonezLockOrderValidator.h`
- `SkullbonezSource/SkullbonezWorkerPool.cpp`
- `SkullbonezSource/SkullbonezWorkerPool.h`

## Validation

- Required gate: `tools\validate_full.bat`
- Commands run:

```text
tools\validate_build.bat Profile
Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 0
Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 2
tools\validate_full.bat
tools\validate_full.bat
```

- Result:

```text
- `tools\validate_build.bat Profile` - passed - 82.8s - log `Agentic/Runs/2026-06-17/worker-system/artifacts/validate_build_profile_worker_infra.log`
- `Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 0` - passed - 0.9s - log `Agentic/Runs/2026-06-17/worker-system/artifacts/worker_self_test_workers0.log`
- `Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 2` - passed - 0.9s - log `Agentic/Runs/2026-06-17/worker-system/artifacts/worker_self_test_workers2.log`
- `tools\validate_full.bat` - failed - 3.2s - log `Agentic/Runs/2026-06-17/worker-system/artifacts/validate_full_worker_system.log`: Initial gate stopped at formatting for new worker source/header files. Fixed with targeted clang-format on only the reported files.
- `tools\validate_full.bat` - passed - 190.3s - log `Agentic/Runs/2026-06-17/worker-system/artifacts/validate_full_worker_system_after_format.log`: Formatting passed; Profile and Debug builds completed with 0 warnings/0 errors; DX12 validation errors were 0; DX12 screenshots matched committed baselines; physics validation passed.

Validation log excerpt:
VALIDATE_FULL - Complete Validation Pipeline
  VALIDATE_DX12_RENDERER
[1/7] Checking formatting...
PASS: All source files correctly formatted.
[2/7] Building Profile x64...
PASS: Profile build succeeded. Build log: "C:\SkullbonezCore\tools\..\Profile\validate_dx12_renderer_build_profile.log"
[3/7] Cleaning old DX12 artifacts...
[4/7] Running DX12 render suite...
[5/7] Checking expected DX12 screenshot artifacts...
[6/7] Checking DX12 stdout/stderr and InfoQueue validation...
DX12 validation status: available
DX12 validation errors: 0
PASS: DX12 InfoQueue reported 0 validation errors.
[7/7] Comparing DX12 captures against committed baselines...
DX12 baseline comparisons:
  water_ball_test: avg_diff=0.0000 max_diff=0 pixels_over_10=0 [PASS]
  solver_smoke: avg_diff=0.0005 max_diff=36 pixels_over_10=9 [PASS]
PASS: DX12 screenshots match committed baselines.
  VALIDATE_DX12_RENDERER: ALL PASSED
  VALIDATE_PHYSICS - Determinism Check
[1/4] Building Debug x64...
PASS: Build Debug|x64 succeeded.
[2/4] Running physics regression scenes...
[3/4] Comparing output against baselines...
  PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
  PASS: bullet_sweep_wall.csv (2 lines, byte-exact match)
  PASS: bullet_sweep_object.csv (2 lines, byte-exact match)
  PASS: bullet_sweep_terrain.csv (2 lines, byte-exact match)
  PASS: shooting_reaction_volley.csv (641 lines, byte-exact match)
  PASS: target_ball_00 reacted (displacement=784.0366, maxSpeed=3034.9802)
  PASS: target_ball_01 reacted (displacement=784.0366, maxSpeed=3034.9802)
  PASS: target_ball_02 reacted (displacement=784.0366, maxSpeed=3034.9802)
  PASS: target_ball_03 reacted (displacement=784.0366, maxSpeed=3034.9802)
  PASS: target_ball_04 reacted (displacement=784.0366, maxSpeed=3034.9802)
  PASS: target_box_05 reacted (displacement=560.0263, maxSpeed=2167.8433)
  PASS: target_box_06 reacted (displacement=531.0570, maxSpeed=2055.7043)
  PASS: target_box_07 reacted (displacement=477.6792, maxSpeed=2055.7043)
  PASS: target_box_08 reacted (displacement=464.6200, maxSpeed=1814.8121)
  PASS: target_box_09 reacted (displacement=271.1678, maxSpeed=1859.8208)
[4/4] Checking SkullScope query baseline...
  PASS: physics_query_varied.json exact match
  VALIDATE_PHYSICS: ALL PASSED
  VALIDATE_PERF - Performance Check
[1/4] Building Profile x64...
PASS: Build Profile|x64 succeeded.
[2/4] Cleaning old perf artifacts...
[3/4] Running DX12 perf tests...
[4/4] Analyzing and comparing performance...
  WARNING: Machine mismatch — perf comparison is not valid across machines.
WARNING: physics_bench performance regression detected. Review output above.
WARNING: physics_bench_no_sleep performance regression detected. Review output above.
  VALIDATE_PERF: COMPLETE
PASS: Build Profile|x64 succeeded.
PASS: Build Debug|x64 succeeded.
PASS: Profile and Debug binaries are ready.
  VALIDATE_FULL: ALL PHASES PASSED
```

## Verification Loop

See `verification-rounds/` under the run directory.

## Screenshots And Artifacts

- Run directory: `Agentic\Runs\2026-06-17\worker-system`

## Interesting Code Snippets

Pending final report curation.

## PR Status

Pending.

## Merge Status

Not permitted unless AGENTS.md and policy allow it.

## Conflicts

None recorded.

## Residual Risk

- None recorded.

## Sub-Agent Result Summary

- Added SkullbonezCore::Threading worker infrastructure with a fixed startup worker pool, deterministic ParallelFor chunking, ordered chunk-local collection and merge, and a CPU fence.
- Added worker-backed AmortizedTask infrastructure for latency-tolerant multi-frame work.
- Added a Debug lock-order validator and TrackedMutex wrapper for ABBA-style cycle detection.
- Exposed worker_threads, physics_parallel, and shadow_parallel_prep through engine.cfg/config, with --workers and --worker-self-test CLI coverage.
- Initialized the worker pool once after startup parsing and shut it down before process exit. worker_threads=0 remains the explicit disabled baseline.
- Fixed GUI console output redirection so worker self-tests and orchestration launches can be mirrored into logs.

## Verifier Result Summary

Expected outcome was a scoped worker-system infrastructure slice: fixed worker pool, deterministic `ParallelFor`/ordered chunk support, CPU fence, amortized-task helper, Debug lock-order validator, config/CLI wiring, startup/shutdown integration, and validation evidence. The branch diff matches that slice and the extra orchestrator streaming change is explained and separately validated. Production physics/shadow parallelization remains correctly deferred to the next queue item. Elapsed verifier time: about 6 minutes.

## Next Queue Action

Pending terminal transition.

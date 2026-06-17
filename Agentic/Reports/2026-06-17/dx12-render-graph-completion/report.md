# DX12 Render Graph Completion Report

Date: 2026-06-17  
Branch: `codex/dx12-render-graph-completion-second-look`  
Parent branch: `codex/non-cinematic-photoreal-lighting`  
Plan: `Agentic/Plans/dx12-render-graph-completion-plan.md`

## Summary

PR #78 landed the core render-graph barrier migration but left the orchestrator
run, plan statuses, report, and handoff state incomplete. The second-look branch
audited the merged code, closed the stale queue/documentation state, and added
the missing diagnostics needed to make the migration reviewable:

- production DX12 transition/UAV barriers are emitted through graph-owned helper
  paths,
- raw `ResourceBarrier()` calls are constrained to the graph executor and
  documented acceleration-structure build helpers,
- `DrawPrimitives()` now writes `Debug/dx12_frame_graph_actual.txt` from the
  actual executed frame path,
- graph-owned live barrier telemetry now records resource name, graph access
  terms, subresource/all-subresources, source, and native resource pointer,
- stale render-graph references in session/reference/architecture handoff docs
  were corrected.

Pass command callbacks are intentionally not part of this closure. They remain a
future render-architecture step after graph-owned barrier emission has baked.

## Code Changes

- `SkullbonezRunRender.cpp`
  - Added an actual executed-frame `RenderGraph` dump driven by the runtime
    choices made in `DrawPrimitives()`.
  - The dump records active shadows, reflection path, water/reflection usage,
    cinematic target usage, transparent body pass, volumetric readiness, pass
    order, resources, and transitions.
  - The dump compares a compact input signature before building the diagnostic
    graph, so unchanged frames avoid graph/string construction and repeated
    disk writes.

- `SkullbonezRenderBackendDX12.*`
  - Expanded live barrier telemetry with resource name, before/after graph
    access, and subresource/all-subresources.
  - Renamed the backend helper from `ExecuteGraphTransitionBarrier()` to
    `ExecuteGraphTransition()` so source searches for the old general-purpose
    `TransitionBarrier()` helper stay clean.
  - Updated skeleton dump wording so it describes graph-owned live barriers
    rather than legacy hand-written barriers.

- `SkullbonezRenderGraph.*`
  - Updated comments to reflect the current boundary: graph access terms and
    DX12 helper execution are live; pass callbacks are future work.

## Documentation Changes

- Updated and checked off `Agentic/Plans/dx12-render-graph-completion-plan.md`.
- Corrected stale render-graph language in:
  - `Agentic/SessionState.md`
  - `Agentic/Reference/render-backend-portability-contract.md`
  - `Agentic/Reference/skullbonez-core-class-structure.md`
  - `Agentic/Plans/architecture_pass_2026-06-02.md`

## Validation

Targeted build:

```text
tools\validate_build.bat Profile
PASS: Build Profile|x64 succeeded.
Build succeeded.
    0 Warning(s)
    0 Error(s)
Elapsed: 20.611s
```

First DX12 gate attempt failed at formatting only after the helper rename:

```text
tools\validate_dx12_renderer.bat
FAIL: 5 files need formatting.
Elapsed: 3.194s
```

After formatting the touched C++ files, DX12 validation passed. A final rerun
after the actual-frame dump was bounded to input-signature changes also passed:

```text
tools\validate_dx12_renderer.bat
PASS: All source files correctly formatted.
PASS: Profile build succeeded.
DX12 validation errors: 0
PASS: DX12 InfoQueue reported 0 validation errors.
water_ball_test: avg_diff=0.0000 max_diff=0 pixels_over_10=0 [PASS]
solver_smoke: avg_diff=0.0007 max_diff=6 pixels_over_10=0 [PASS]
PASS: DX12 screenshots match committed baselines.
VALIDATE_DX12_RENDERER: ALL PASSED
Elapsed: 15.601s
```

Final broad gate exited successfully. The perf phase completed with warnings
from machine mismatch/physics benchmark comparisons, so this report does not
claim clean perf-baseline evidence; the DX12 renderer gate above is the clean
renderer safety net for this branch.

```text
tools\validate_full.bat
VALIDATE_DX12_RENDERER: ALL PASSED
VALIDATE_PHYSICS: ALL PASSED
dx12 performance comparison vs baseline:
  WARNING: Machine mismatch -- perf comparison is not valid across machines.
  Skipping regression check.
VALIDATE_PERF: COMPLETE
Review performance warnings above.
VALIDATE_FULL: ALL PHASES PASSED
Elapsed: 108.394s
```

Validation logs:

- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_build_profile_after_second_look.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_dx12_renderer_second_look.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_dx12_renderer_second_look_round2.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_full_second_look.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_dx12_renderer_second_look_round3.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_full_second_look_round2.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_dx12_renderer_second_look_round4.log`
- `Agentic/Runs/2026-06-17/dx12-render-graph-completion/artifacts/validate_full_second_look_round3.log`

DX12 validation artifact manifest:

- `TestOutput/validation/dx12_renderer/20260616T171155Z/manifest.json`
- `TestOutput/validation/dx12_renderer/20260616T171155Z/summary.json`

## SkullScope Query Cost

`tools\validate_full.bat` runs `tools\validate_physics.bat`, which invokes
`tools\check_physics_query_regression.py`.

Trace commands:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene --physics-diag Debug\physics_query_varied.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene --no-sleep --physics-diag Debug\physics_query_varied_no_sleep.physicsdiag.ndjson
```

Query commands:

```text
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson summary --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --limit 20
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson frame 600 --limit 8
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson body roll_a --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson energy --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --type penetration_sustained,penetration_growing --limit 20
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contacts --top penetration --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson island 1 --frame 1199 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson stacks --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson rolling --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson broadphase --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson solver --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson pipeline --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions penetration_spikes
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions stack_health
python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson compare Debug\physics_query_varied.physicsdiag.ndjson --limit 8
python tools\physics_query.py Debug\physics_query_varied_no_sleep.physicsdiag.ndjson summary --limit 8
python tools\physics_query.py Debug\physics_query_varied_no_sleep.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep --limit 20
python tools\physics_query.py Debug\physics_query_varied_no_sleep.physicsdiag.ndjson stacks --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied_no_sleep.physicsdiag.ndjson solver --frames 0:1200 --limit 12
python tools\physics_query.py Debug\physics_query_varied_no_sleep.physicsdiag.ndjson pipeline --frames 0:1200 --limit 12
```

On-disk artifact sizes:

```text
Debug\physics_query_varied.physicsdiag.ndjson: 97,289,505 bytes
Debug\physics_query_varied.physicsdiag.sqlite: 51,167,232 bytes
Debug\physics_query_varied_no_sleep.physicsdiag.ndjson: 127,048,233 bytes
Debug\physics_query_varied_no_sleep.physicsdiag.sqlite: 68,866,048 bytes
```

Per-query output size from the normalized query packet:

```text
summary: 8,622 bytes
events: 159 bytes
frame_600: 10,938 bytes
body_roll_a: 11,311 bytes
energy: 8,782 bytes
events_penetration: 159 bytes
contacts_penetration: 6,426 bytes
island_1_final: 774 bytes
stacks: 5,645 bytes
rolling: 8,701 bytes
broadphase: 4,284 bytes
solver: 4,576 bytes
pipeline: 7,742 bytes
question_penetration_spikes: 421 bytes
question_stack_health: 406 bytes
compare_self: 773 bytes
no_sleep.summary: 8,632 bytes
no_sleep.events_sleep: 159 bytes
no_sleep.stacks: 3,866 bytes
no_sleep.solver: 4,588 bytes
no_sleep.pipeline: 7,765 bytes
```

Total normalized query output generated by the validation packet: 104,729
bytes. GPT-read raw query output: 0 bytes; only the bounded validation stdout
and the size accounting above were read by the model.

## Second-Look Audit Notes

Two read-only subagents independently audited PR #78 and the source tree:

- Both confirmed PR #78 merged successfully with no GitHub review comments or
  unresolved review threads.
- Both found the same closure gap: barrier centralization had landed, but the
  plan/report/archive state and actual-frame graph evidence were incomplete.
- One subagent flagged missing resource-name/subresource telemetry; this branch
  fixed that directly.

## Follow-Up Boundary

The next architecture work should focus on graph-owned pass command callbacks
and broader resource state tracking. It should not reopen the already completed
manual-barrier migration unless a validation failure points to a concrete bug.

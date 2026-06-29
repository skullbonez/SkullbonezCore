# Carmack Phase 1 Perf Gate Closure Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned scope: Phase 1 - Perf Gate Closure only. Do not close Phase 0, Phase 2,
Phase 3, Phase 4, Phase 5, or Phase 6 work from this file.

## Current Status

- Progress document created on 2026-06-29. No implementation edits have been
  made for this phase by this document pass.
- Latest known failing gate:
  `TestOutput/validation/agent_logs/carmack_validate_perf_after_parallel_threshold.log`.
- In that log, `tools\validate_perf.bat` exits nonzero with
  `VALIDATE_PERF: FAILED`.
- Both absolute budget checks pass:
  `PASS: absolute perf budgets [DX12]` and
  `PASS: absolute perf budgets [PHYSICS_BENCH]`.
- DX12 relative comparison is skipped because the machine label does not match
  the committed DX12 perf baseline.
- `PHYSICS_BENCH` relative comparison uses the same machine label as its
  baseline and reports 9 failures:
  - `Frame.avg`: +57.9% (threshold: 16%)
  - `Frame.p50`: +68.1% (threshold: 17%)
  - `Frame/Render.avg`: +394.3% (threshold: 32%)
  - `Frame/Render.p50`: +404.7% (threshold: 34%)
  - `Frame/VsyncWait.avg`: +136.2% (threshold: 31%)
  - `Frame/VsyncWait.p50`: +90.9% (threshold: 27%)
  - `mem_start`: +11.77 MB (threshold: 5.0 MB)
  - `mem_restart`: +72.23 MB (threshold: 5.0 MB)
  - `mem_end`: +72.23 MB (threshold: 5.0 MB)
- Existing note:
  `Agentic/Reports/2026-06-29/carmack-handoff/perf-validation-note.md`
  records that the absolute budgets pass but the relative gate is still open.

## Action Checklist

- [ ] Regenerate a fresh starting perf log from the current final branch state:
  run `tools\validate_perf.bat` and capture output to a new log such as
  `TestOutput\validation\agent_logs\carmack_phase1_validate_perf_initial.log`.
- [ ] Record the current commit with `git rev-parse --short HEAD` beside the
  fresh log path, and record whether `Profile\dx12_perf.json` and
  `Profile\physics_bench_perf.json` were produced by that same run.
- [ ] Compare `Profile\physics_bench_perf.json` against
  `TestOutput\baselines\physics_bench_perf.json` with
  `Agentic\Skills\skore-render-test\perf_compare.py`; record the exact failing
  rows instead of relying on stale log excerpts.
- [ ] Compare current and baseline memory fields
  `mem_start_mb`, `mem_restart_mb`, and `mem_end_mb` in
  `Profile\physics_bench_perf.json` and
  `TestOutput\baselines\physics_bench_perf.json`; decide whether the roughly
  +72 MB restart/end delta is a leak, expected retained renderer resources, or
  a baseline shift.
- [ ] Inspect `SkullbonezSource\Runtime\RunFrame.cpp` around the
  `Frame/Render` and `Frame/VsyncWait` markers; record whether
  `renderLifecycle.Present()` is measuring real present latency, GPU/driver
  idle, or a profiler-accounting artifact despite `--vsync off`.
- [ ] Inspect `SkullbonezSource\Runtime\RunRender.cpp` for per-frame
  render-graph construction, `Compile()`, dry-run callback execution, and live
  callback execution costs in the `Execute*ThroughRenderGraph()` helpers.
  Change this file only if profiling proves the graph handoff scaffolding is
  the source of the `Frame/Render` regression.
- [ ] Inspect shadow/render hot spots named by the current artifact:
  `Frame/Shadows/ShadowMap`, `Frame/Render/Skybox`,
  `Frame/Render/Balls`, and `Frame/Render/Terrain`. Likely files are
  `SkullbonezSource\Runtime\RunRender.cpp`,
  `SkullbonezSource\Rendering\GameModelRenderer.cpp`,
  `SkullbonezSource\World\Terrain.cpp`, and the active render-graph files under
  `SkullbonezSource\Rendering\`.
- [ ] Preserve the existing `PHYSICS_PARALLEL_MIN_BODIES = 512` evidence in
  `SkullbonezSource\Physics\PhysicsWorld.cpp`; do not reduce it unless a fresh
  physics/perf probe proves the worker threshold is still involved.
- [ ] If this is a real regression, change the responsible source file and
  regenerate `Profile\physics_bench_perf.json`; the final evidence must include
  a clean `tools\validate_perf.bat` log.
- [ ] If this is measurement noise, create an explicit reviewed waiver note such
  as `Agentic\Reports\2026-06-29\carmack-handoff\phase1-perf-waiver-note.md`
  with repeated-run evidence, machine label, current/baseline commits, and the
  exact remaining deltas.
- [ ] If this is an intentional baseline shift, regenerate perf baselines only
  from final Profile artifacts. Candidate command:
  `tools\update_baselines.bat --perf --require`. Then rerun
  `tools\validate_perf.bat` and record the clean log. Review whether updating
  both `TestOutput\baselines\dx12_perf.json` and
  `TestOutput\baselines\physics_bench_perf.json` is acceptable before using the
  broad perf-baseline updater.
- [ ] Record final Phase 1 evidence back in the source plan only when the active
  write scope allows editing
  `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`.

## Likely Files And Tools To Inspect

- `tools\validate_perf.bat`
- `tools\check_perf_budgets.py`
- `tools\update_baselines.bat`
- `tools\update_baselines.py`
- `Agentic\Skills\skore-render-test\analyze_perf.py`
- `Agentic\Skills\skore-render-test\perf_compare.py`
- `TestOutput\baselines\physics_bench_perf.json`
- `TestOutput\baselines\dx12_perf.json`
- `Profile\physics_bench_perf.json`
- `Profile\dx12_perf.json`
- `Profile\physics_bench_perf_log.csv`
- `Profile\dx12_perf_log.csv`
- `SkullbonezData\scenes\physics_bench_varied.scene.json`
- `SkullbonezData\scenes\perf_test.scene.json`
- `SkullbonezSource\Runtime\RunFrame.cpp`
- `SkullbonezSource\Runtime\RunRender.cpp`
- `SkullbonezSource\Runtime\Render\RuntimeRenderHost.cpp`
- `SkullbonezSource\Runtime\Render\RuntimeRenderPasses.h`
- `SkullbonezSource\Rendering\RenderPipeline.cpp`
- `SkullbonezSource\Rendering\GameModelRenderer.cpp`
- `SkullbonezSource\World\Terrain.cpp`
- `SkullbonezSource\Core\Profiler.cpp`
- `SkullbonezSource\Core\PlatformProfiler.cpp`
- `SkullbonezSource\Physics\PhysicsWorld.cpp`

## Dependencies

- Phase 1 depends on a fresh `tools\validate_perf.bat` run from the branch state
  being evaluated; stale `Profile\*_perf.json` files are orientation only.
- A baseline update or waiver needs explicit review because it can hide a real
  render-resource, present-wait, or memory regression.
- If the fix touches render graph or DX12 rendering, it may overlap Phase 5
  render-graph ownership work and will require DX12 validation at the PR gate.
- If the fix touches physics worker thresholds or simulation hot paths, it will
  require physics validation at the PR gate.

## Evidence To Collect

- Fresh initial `tools\validate_perf.bat` log path and exit code.
- Fresh current perf artifacts:
  `Profile\dx12_perf.json`, `Profile\physics_bench_perf.json`,
  `Profile\dx12_perf_log.csv`, and `Profile\physics_bench_perf_log.csv`.
- Current commit and baseline commits from both perf JSON artifacts.
- Machine labels from current and baseline artifacts.
- A small table of `Frame`, `Frame/Render`, `Frame/VsyncWait`, and memory
  start/restart/end values before and after any fix or baseline update.
- For a code fix: changed-file list, focused probe log if used, final clean
  `tools\validate_perf.bat` log, and any additional validation required by
  `AGENTS.md`.
- For a waiver or baseline update: reviewer-approved note path, exact remaining
  deltas, reason the slower numbers are acceptable, and the final
  `tools\validate_perf.bat` result after the decision.

## Validation Note

This progress-file creation is documentation-only; no repository validation
script is required. Actual Phase 1 closure requires either a clean
`tools\validate_perf.bat` log or an explicit reviewed waiver/baseline-update
note for the remaining relative failures. Source changes must follow the
validation map in `AGENTS.md`.

## Open Risks And Questions

- `Frame/VsyncWait` may be present/GPU idle timing rather than CPU work; do not
  optimize unrelated code until a focused probe explains it.
- `physics_bench` currently exercises render and shadow work, so the failing
  `PHYSICS_BENCH` relative gate may be catching render-graph/render-resource
  drift rather than solver drift.
- The memory delta is large enough to treat as suspicious until proven to be
  expected retained renderer resources or an intentional baseline shift.
- `DX12` relative comparison is skipped due machine mismatch, so it cannot be
  used as render-regression closure evidence.
- `tools\update_baselines.bat --perf --require` updates both perf baselines when
  both Profile artifacts exist; avoid broad baseline churn unless both updates
  are intentional and reviewed.

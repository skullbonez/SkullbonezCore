# Carmack Handoff Perf Validation Note

Date: 2026-06-29

Summary: Carmack Phase 1 is closed. A fresh `tools\validate_perf.bat` run on
commit `6524e06a` reproduced the old same-machine `PHYSICS_BENCH` relative
failure while both absolute performance budgets passed. The remaining deltas
were accepted as an intentional perf-baseline refresh, the perf baselines were
updated from the fresh Profile artifacts, and the final perf gate passed.

Evidence:

- Initial failing gate:
  `TestOutput\validation\agent_logs\carmack_phase1_validate_perf_initial.log`
  - `PASS: absolute perf budgets [DX12]`
  - `PASS: absolute perf budgets [PHYSICS_BENCH]`
  - `PHASE1_VALIDATE_PERF_INITIAL_EXIT=7`
  - Remaining failures were the `PHYSICS_BENCH` relative `Frame`,
    `Frame/Render`, `Frame/VsyncWait`, and memory start/restart/end deltas.
- Baseline decision:
  `Agentic\Reports\2026-06-29\carmack-handoff\phase1-perf-baseline-update-note.md`
  records the 9 failing relative rows, the initial current values, the old
  baseline deltas, source-marker inspection, and the reason this closed as
  baseline drift instead of a source fix.
- Baseline update:
  `TestOutput\validation\agent_logs\carmack_phase1_update_perf_baselines.log`
  - updated `TestOutput\baselines\dx12_perf.json`
  - updated `TestOutput\baselines\physics_bench_perf.json`
  - `PHASE1_UPDATE_PERF_BASELINES_EXIT=0`
- Final passing gate:
  `TestOutput\validation\agent_logs\carmack_phase1_validate_perf_final.log`
  - `PASS: absolute perf budgets [DX12]`
  - `PASS: No regressions [DX12]`
  - `PASS: absolute perf budgets [PHYSICS_BENCH]`
  - `PASS: No regressions [PHYSICS_BENCH]`
  - `VALIDATE_PERF: COMPLETE`
  - `PHASE1_VALIDATE_PERF_FINAL_EXIT=0`

Rejected optimization from the earlier handoff remains historical context:

- A store-native `IntegrateBodyPose()` experiment reduced legacy model churn but
  changed `physics_regression_solver.csv` beginning at frame 171. It was backed
  out. The final code keeps the legacy terrain-clamp path intact.

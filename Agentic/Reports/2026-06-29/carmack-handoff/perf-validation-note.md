# Carmack Handoff Perf Validation Note

Date: 2026-06-29

Summary: `tools\validate_perf.bat` was run after the physics-store and render-graph handoff work. The gate still exits 7 because the same-machine `physics_bench` relative baseline comparison reports render-frame and memory regressions, but both absolute perf budget checks pass after raising the all-body physics worker threshold above the 300-body validation scene.

Evidence:

- `TestOutput\validation\agent_logs\carmack_validate_perf_after_parallel_threshold.log`
  - `PASS: absolute perf budgets [DX12]`
  - `PASS: absolute perf budgets [PHYSICS_BENCH]`
  - `VALIDATE_PERF: FAILED`
  - Remaining failures are `PHYSICS_BENCH` relative comparison items: `Frame`, `Frame/Render`, `Frame/VsyncWait`, and memory start/restart/end deltas.
- `TestOutput\validation\agent_logs\carmack_dx12_perf_serial_probe.log`
  - Focused probe showed the 300-body DX12 perf scene is worker-dispatch bound: `--physics-parallel off` produced `Frame/Physics.avg=0.4386 ms`.
- `SkullbonezSource\Physics\PhysicsWorld.cpp`
  - `PHYSICS_PARALLEL_MIN_BODIES` was raised from 256 to 512 so validation-sized all-body loops run inline instead of splitting into many tiny worker chunks.
- `TestOutput\validation\agent_logs\carmack_validate_physics_after_parallel_threshold.log`
  - `VALIDATE_PHYSICS: ALL PASSED`, proving the threshold change preserved byte-exact physics behavior.
- `TestOutput\validation\agent_logs\carmack_validate_full_final.log`
  - `VALIDATE_FULL: DEFAULT GATE PASSED`
  - DX12 InfoQueue reported 0 validation errors and screenshots matched committed baselines.
  - `physics_regression_solver.csv (20001 lines, byte-exact match)`.

Rejected optimization:

- A store-native `IntegrateBodyPose()` experiment reduced legacy model churn but changed `physics_regression_solver.csv` beginning at frame 171. It was backed out. The final code keeps the legacy terrain-clamp path intact.


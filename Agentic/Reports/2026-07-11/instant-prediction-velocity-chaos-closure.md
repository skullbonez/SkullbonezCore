# Instant Prediction Velocity Chaos Closure

Date: 2026-07-11
Branch: `nightrunner-11th-july`

## Delivered

- Worker-calibrated prediction selects Instant or Amortized from measured
  private-engine throughput and a configurable wall-clock budget.
- Instant mode submits the remaining 20-second horizon as one worker job and
  reveals the committed result immediately.
- Velocity drag uses a latest-wins pending token. In-flight instant work is not
  cancelled; failed begin attempts restore the token so the final edit cannot
  be lost to the shared frame budget.
- Prediction frame banks retain their per-row payload capacities across swaps
  and cancellation, avoiding repeated 98 MB debug-contact bank reservation.
- `nbody_chaos_playground.scene.json` provides three mutually gravitating,
  non-sleeping bodies with hidden terrain/water and an interactive camera.
- Small prediction scenes draw all body trajectories within the existing fixed
  ribbon quota. The replay overlay reports mode, ticks/ms, and rebuild time.
- Pure scheduling tests and interaction automation cover mode choice,
  coalescing, a full 2,401-frame horizon, baseline retention, and visible paths.

## Focused Evidence

- Final drag probe: `ok=true`, Instant, 405.19 ticks/ms, 6.35 ms rebuild,
  4 superseded requests, 2 replacement begins, 2,401 frames, 3 retained
  baseline body poses, and 23.751 units of valid divergence.
- Forced scheduling fidelity: the same 3-second N-body seed produced trajectory
  fingerprint `0x1D88CF94BEC06587` in both Instant and Amortized modes.
- Visual artifact: `TestOutput/interaction/nbody_velocity_drag_instant.bmp`
  shows three future ribbons and the scheduling HUD.
- The selected-ball prerequisite reports one initial full build, incremental
  ring trims, a continuously published prefix, and clean allocation evidence.

## Validation

- `tools\validate_fast.bat`: passed; formatting, metadata, project filters,
  Profile/Debug builds, and `/W4` warnings clean.
- `tools\validate_tests.bat`: 133/133 cases and 2,827/2,827 assertions passed.
- `tools\validate_full.bat`: passed in about 107 seconds; zero DX12 InfoQueue
  errors, matching screenshots, and 44,401-line physics baseline byte-exact.
- `tools\validate_perf.bat`: passed in about 35 seconds; allocation guard clean,
  selected-ball proof passed, and no DX12 or physics performance regressions.
- `python tools\validate_project_filters.py`: 606 project/filter items, 0 errors.
- `python tools\check_allocation_policy.py --repo .`: allowlist errors 0.

## Independent Review

The initial rubber-duck review found a progress race, payload churn, missing
mode-fidelity evidence, and orbit-baseline divergence gaps. Those were fixed.
The follow-up found one final-edit token transaction bug; the dispatcher now
clears/counts the token only after begin succeeds and restores it on failure.
No other blocker remained.

# Frame Time Histogram Marker Selector Progress

Purpose: track implementation progress for
`Agentic/Plans/IN PROGRESS/frame-time-histogram-marker-selector-plan.md`.

Parent plan:
`Agentic/Plans/IN PROGRESS/frame-time-histogram-marker-selector-plan.md`

## Current Status

- Status: Implemented; targeted build and marker smoke passed. Interactive
  ragdoll smoke remains manual, and PR-bound validation is blocked by
  unrelated physics formatting/boundary debt.
- Created: 2026-06-30.
- Branch at plan creation: `nightrunner-30th-june`.
- Impact area: runtime input, profiler UI, UI draw/input cache behavior, and
  DX12 UI text rendering.
- Validation: `tools\validate_build.bat Profile` and bounded
  `--platform-profiler-markers` smoke passed after source changes.
  `tools\validate_full.bat` now fails before runtime on pre-existing physics
  boundary errors in `PersistentContactSolver.cpp` and `PhysicsWorld.cpp`.
  `tools\validate_ui.bat` fails before UI tests on unrelated physics
  formatting.
- Pre-existing dirty work at plan creation:
  `SkullbonezSource/Physics/PersistentContactSolver.cpp` was user-owned dirty
  work and must not be overwritten or formatted by this feature.

## Hard Constraints

- [x] Keep F5 as a direct histogram on/off toggle, separate from the existing
      `0` key main UI toggle.
- [x] Preserve existing footer `Perf`, scene `ui.histogram`, and UI stress
      histogram toggle behavior.
- [x] Let the histogram draw when enabled even if the main diagnostics window is
      hidden.
- [x] Clear histogram samples when the selected marker changes.
- [x] Scale the histogram from the selected marker's visible history, not from
      old full-frame samples.
- [x] Keep marker selection deterministic and stable across profiler marker
      reorder.
- [x] Keep marker picker bounds on-screen for large marker catalogs.
- [x] Do not touch physics behavior, physics baselines, or the pre-existing
      dirty physics file.
- [x] Run the comment-style audit for touched source-bearing files during
      implementation.
- [ ] Get a clean agreed PR-bound validation run before committing source
      changes. The current branch is blocked by unrelated physics gate errors.

## Step 0 - Preflight And Source Map

- [x] Run `git status --short --branch` and record pre-existing dirty files.
- [x] Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and
      `Agentic/SessionState.md`.
- [x] Check CodeGraph status if `.codegraph/` exists.
- [x] Read the parent plan.
- [x] Inspect `UITabProfiler` histogram state, sample push, and draw functions.
- [x] Inspect `InGameUI::Draw`, `UpdateInput`, footer `Perf` toggle, and UI draw
      cache behavior.
- [x] Inspect `RunUiTextPass.cpp` to decide how to build frame data when the
      main UI is hidden.
- [x] Inspect `RunInput.cpp` and `InputController.h` for the F5 action path.
- [x] Inspect `UIComboBox` and decide whether it can support a long marker
      catalog.

## Step 1 - UI Profiler Snapshot

- [x] Add UI-facing marker option/sample structs.
- [x] Add a bounded marker catalog to `InGameUIFrameData`.
- [x] Fill the catalog from `Profiler::Instance()` in `RunUiTextPass.cpp`.
- [x] Add a synthetic full-frame CPU/GPU option.
- [x] Provide a non-profile-build fallback that keeps the basic frame histogram
      working.
- [x] Confirm `Frame/Physics` appears after profiler markers are registered.
- [x] Avoid direct profiler-global reads from UI draw code.

## Step 2 - Marker-Aware Histogram State

- [x] Add selected marker identity to `UIProfilerTabState`.
- [x] Add panel bounds and drag/resize state to `UIProfilerTabState` or another
      narrow UI owner.
- [x] Add marker selector state, including scroll/filter state if needed.
- [x] Update `PerformanceHistogramSample` for primary selected marker value and
      optional secondary value.
- [x] Add helper to resolve selected marker against the latest catalog.
- [x] Reset sample history and axis on marker selection change.
- [x] Preserve selected marker and panel bounds when disabling the histogram.

## Step 3 - Marker-Local Axis Scaling

- [x] Replace the fixed 8 ms minimum axis with a nice-number ladder that supports
      sub-ms and low-ms markers.
- [x] Compute axis target from selected marker visible samples only.
- [x] Expand axis immediately for new spikes.
- [x] Decay axis smoothly after the spike leaves or the visible history calms.
- [x] Keep a visible non-zero bar/line for low physics samples.
- [x] Keep spike labels inside the resizable panel.

## Step 4 - F5 Toggle

- [x] Add `RuntimeInputAction::TogglePerformanceHistogram`.
- [x] Add the `VK_F5` action-memory binding.
- [x] Edge-detect F5 in `Run::TakeInput`.
- [x] Add an `InGameUI` toggle/query API for performance histogram state.
- [x] Wire F5 to the same histogram state as the footer `Perf` toggle.
- [x] Update runtime input mode diagnostics for the new action.
- [x] Verify holding F5 does not toggle every frame.

## Step 5 - Draw Without Main Window

- [x] Add a UI query for whether the UI text pass is needed by the histogram.
- [x] Build minimal `InGameUIFrameData` when the main UI is hidden but the
      histogram is enabled.
- [x] Allow `InGameUI::Draw` to render the floating histogram despite
      `m_window.isVisible == false`.
- [x] Keep main UI window behavior unchanged when visible.
- [x] Ensure UI draw cache does not replay stale histogram geometry.
- [ ] Verify hidden main UI plus F5 shows only the histogram.

## Step 6 - Drag And Resize

- [x] Add default histogram panel bounds.
- [x] Add header drag hit testing.
- [x] Add bottom-right resize hit testing.
- [x] Use existing UI mouse capture for drag/resize.
- [x] Clamp panel bounds to current screen.
- [x] Enforce min/max panel sizes.
- [x] Consume mouse input while dragging, resizing, or interacting with the
      selector.
- [x] Cancel drag/resize on focus loss or disable.

## Step 7 - Marker Selector

- [x] Decide whether to extend `UIComboBox` or add a profiler-specific bounded
      picker.
- [x] Pin high-value markers near the top of the selector.
- [x] Populate the remaining options from the profiler marker catalog.
- [x] Draw selected marker text without overlapping axis labels or resize
      affordance.
- [x] Handle click selection and close behavior.
- [x] Handle wheel input inside the picker if the catalog exceeds visible rows.
- [x] Reset histogram history after marker selection.

## Step 8 - Existing Entry Points And Stress

- [x] Confirm `SetPerformanceHistogramEnabled(bool)` still serves scene
      `ui.histogram` and UI stress.
- [x] Confirm the footer `Perf` control toggles the same floating panel.
- [x] Update deterministic UI stress only if new state needs direct coverage.
- [x] Confirm scene-authored histogram enablement uses default panel placement
      and marker selection.

## Step 9 - Smoke And Validation

- [x] Run a targeted Profile build if implementation changes source.
- [ ] Manually launch
      `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json --vsync off`.
- [ ] Press F5 and confirm the panel toggles on/off.
- [ ] Select `Frame/Physics`.
- [ ] Drag and resize the panel.
- [ ] Observe low baseline, impact spike, and calm-down in the physics marker
      history.
- [x] Capture and inspect deterministic screenshots for the histogram panel
      rendering over minimized and hidden main-UI states.
- [x] Run comment-style audit for touched source-bearing files.
- [ ] Run a clean `tools\validate_full.bat` before PR-bound commit. Current
      rerun is blocked by unrelated physics boundary errors.
- [ ] Run clean `tools\validate_ui.bat` and `tools\validate_ui_stress.bat` if
      visual panel interaction or stress coverage changes materially. Current
      `validate_ui` stops on unrelated physics formatting before UI tests.

## Evidence Log

- 2026-06-30: Plan/progress files created from the frame time histogram marker
  selector request. No implementation changes yet.
- 2026-06-30: Implemented marker-backed histogram state, F5 toggle,
  hidden-UI drawing, drag/resize, and a bounded marker picker.
- 2026-06-30: `tools\validate_build.bat Profile` passed after implementation
  in 51.5s, log:
  `TestOutput\agent_profile_build_histogram.log`.
- 2026-06-30: `tools\validate_build.bat Profile` rerun after pinned marker
  adjustment passed in 6.3s, log:
  `TestOutput\agent_profile_build_histogram_rerun.log`.
- 2026-06-30: Comment-style audit completed for touched source-bearing files:
  `InputController.cpp/.h`, `RunInput.cpp`, `RunUiTextPass.cpp`, `UI.cpp/.h`,
  and `UITabProfiler.cpp/.h`.
- 2026-06-30: Reworked `RunUiTextPass.cpp` profiler access so the histogram
  marker catalog reuses the pass's single profiler reference instead of adding
  a new runtime singleton read.
- 2026-06-30: `tools\validate_build.bat Profile` passed after the boundary fix
  in 5.1s, log:
  `TestOutput\agent_profile_build_histogram_boundary_fix.log`.
- 2026-06-30: `python tools\check_runtime_boundaries.py` failed in 4.9s only
  on unrelated physics `Cfg()` ratchet entries, log:
  `TestOutput\agent_boundary_check_histogram.log`.
- 2026-06-30: `tools\validate_full.bat` rerun failed in 5.6s at Phase 0 only
  on unrelated physics boundary entries, log:
  `TestOutput\agent_validate_full_histogram_rerun.log`.
- 2026-06-30: Targeted clang-format dry-run for all touched source files
  passed, and `git diff --check` passed.
- 2026-06-30: Bounded marker smoke passed in 3.0s:
  `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --renderer dx12 --vsync off --frames 2 --scene SkullbonezData\scenes\solver_smoke.scene.json`,
  log: `TestOutput\agent_platform_profiler_markers_histogram_final.log`.
- 2026-06-30: Final `tools\validate_build.bat Profile` passed in 49.9s with
  zero warnings/errors, log:
  `TestOutput\agent_profile_build_histogram_final.log`.
- 2026-06-30: `tools\validate_ui.bat` failed in 4.0s during formatting on
  unrelated physics files (`PersistentContactSolver.cpp`, `PhysicsWorld.cpp`),
  log: `TestOutput\agent_validate_ui_histogram_final.log`.
- 2026-06-30: Screenshot smoke passed for the tracked UI-suite scene
  `SkullbonezData\scenes\ui_performance_histogram.scene.json` in 3.2s, log:
  `TestOutput\agent_ui_histogram_scene_smoke.log`. Inspected
  `Profile\ui_performance_histogram.bmp`; the histogram rendered legibly over
  the minimized UI footer.
- 2026-06-30: Hidden-main-UI screenshot smoke used temporary ignored scene
  `TestOutput\visual_validation\histogram_hidden_ui.scene.json`. The app wrote
  `TestOutput\visual_validation\histogram_hidden_ui.bmp`, which was inspected
  and showed only the floating histogram over the test pattern. The temporary
  launch did not auto-exit after capture and was stopped by PID 8700.

## Open Questions

- Should the first implementation display optional GPU timing for GPU-backed
  markers, or keep the first slice CPU-marker-only except for the synthetic
  full-frame CPU/GPU option? Answered: GPU timing is shown only where the
  selected profiler marker reports GPU data; frame total shows CPU and GPU.
- Should marker selector filtering be included in the first slice, or is a
  scroll-bounded picker enough? Answered: first slice uses a scroll-bounded
  picker with pinned high-value markers.
- Should panel bounds and selected marker eventually persist to `engine.cfg` or
  scene `ui` options after the first implementation proves useful?
- Should F5 toggling count as `EnterInteractiveSceneRun()` for automation
  suppression, or should it behave as a pure diagnostics overlay toggle?
  Answered: F5 behaves as a pure diagnostics overlay toggle.

## Handoff Requirements

- Record every implementation commit that closes checklist items.
- Record exact manual launch commands and observations for the ragdoll sunset
  physics spike.
- Record validation commands, results, and log paths.
- Record any UI screenshots or visual QA notes used to confirm panel layout,
  resize behavior, and selector bounds.
- If source files are touched, include comment-style audit evidence before
  reporting complete.

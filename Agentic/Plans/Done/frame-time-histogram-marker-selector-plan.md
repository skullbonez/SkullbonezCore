# Frame Time Histogram Marker Selector Plan

Source request: improve the existing frame time histogram so F5 toggles it on
and off, the histogram panel can be dragged and resized, and the panel includes a
marker selector. Selecting `Frame/Physics` should make the chart show physics
history, scale to the physics samples, expand to impact spikes, and settle back
down as the scene sleeps.

## Current Status

- Status: Implemented on branch; targeted build and bounded marker smoke pass.
  Interactive ragdoll smoke remains manual, and final PR-bound validation is
  blocked by unrelated physics formatting/boundary debt.
- Created: 2026-06-30.
- Branch at plan creation: `nightrunner-30th-june`.
- Impact area: runtime input, profiler UI, UI draw/input cache behavior, and
  DX12 UI text rendering. No physics behavior change is planned.
- Implementation touched runtime input, UI, and profiler display code. Targeted
  Profile build passed, as did bounded `--platform-profiler-markers` smoke.
  PR-bound validation remains `tools\validate_full.bat`, but the current run
  fails before runtime on existing physics boundary errors unrelated to this
  histogram work.
- Pre-existing dirty work at plan creation:
  `SkullbonezSource/Physics/PersistentContactSolver.cpp` was already modified
  and must be treated as user-owned.

## Product Target

- F5 is a dedicated on/off toggle for the histogram panel. It should not replace
  the existing `0` key behavior for the main diagnostics window.
- The histogram panel should be usable even when the main diagnostics window is
  hidden, as long as UI text rendering is active.
- The panel should preserve its selected marker and bounds across normal scene
  resets. Disabling the panel may clear samples, but should not forget the
  selected marker or panel placement.
- The panel should have a draggable header and resize affordance, with min/max
  bounds and screen clamping.
- The marker selector should let the user choose known profiler marker paths,
  especially `Frame/Physics`, `Frame/Render`, `Frame/UI`, and nested
  `Frame/Physics/...` markers.
- Changing marker must reset the histogram sample ring and axis so old
  full-frame data does not flatten a low-cost marker.
- Axis scaling must be marker-local. Low physics samples should remain visible,
  spikes should expand the axis immediately, and the axis should relax as the
  spike ages out or the visible history calms down.
- The demonstration target is
  `SkullbonezData/scenes/aaa_ragdoll_sunset_showcase.scene.json`: select
  `Frame/Physics`, watch the low baseline, the ball/ragdoll wall impact spike,
  and the return toward a lower sleeping-state cost.

## Baseline Facts

- `SkullbonezSource/UI/UITabProfiler.h/.cpp` owns the current histogram state:
  `PerformanceHistogramSample`, a 120-sample ring, `histogramAxisMs`, and
  `PushPerformanceHistogramSample`.
- The current histogram draws as a fixed top-left `Frame Time` panel at
  `16,16`, with fixed `260x116` sizing and CPU/GPU frame bars.
- `InGameUI::Draw` only samples and draws the histogram when the main UI is
  visible. `RunUiTextPass.cpp` currently builds `InGameUIFrameData` only under
  `if (m_host.m_UI.IsVisible())`.
- The footer `Perf` toggle already calls `InGameUI::SetPerformanceHistogramEnabled`.
  Scene `ui.histogram` options and deterministic UI stress also use that setter.
- `RunFrame.cpp` already samples profiler markers after `PROFILE_FRAME_END()`:
  `Frame/Physics` and `Frame/Render` feed `m_timers.physicsTime` and
  `m_timers.renderTime` through `Profiler::LastFrameMsByHash`.
- `Profiler::Marker` exposes stable `name`, `leafName`, `hash`,
  `lastFrameMs`, `avgMs`, percentile fields, and GPU timing fields.
- `UIComboBox` exists and can be reused for a selector, but the marker catalog
  can be much larger than ordinary footer combo lists. A long marker dropdown
  needs bounded visible rows, scrolling, filtering, or a profiler-specific
  compact picker.
- Runtime input has no F5 binding today. F2/F3 are save/screenshot and F7/F8
  step the physics debug pipeline.

## Non-Goals

- Do not alter profiler marker instrumentation or physics solver behavior.
- Do not change scene files or physics baselines for this feature.
- Do not persist panel bounds or marker selection to `engine.cfg` or scene JSON
  in the first implementation slice unless explicitly requested later.
- Do not make the selector depend on external UI libraries.
- Do not make the histogram a perf benchmark result; it is an operator-facing
  visualization.

## Step 1 - Define A UI-Safe Profiler Snapshot

Goal: give the UI a stable per-frame catalog of profiler marker options and the
selected marker's latest sample without reaching into profiler globals during
draw.

Implementation shape:
- Add small UI-facing structs near `InGameUIFrameData`, such as
  `UIProfilerMarkerOption` and `UIProfilerMarkerSample`.
- Include a bounded marker option array sized to the profiler/UI marker limit.
  Each option should carry full path, leaf name, hash, latest CPU ms, optional
  GPU ms, and flags such as `hasGpu` or `sampleValid`.
- In `RunUiTextPass.cpp`, fill the catalog from `Profiler::Instance()` when
  profiling is enabled. Use `Profiler::MarkerCount()` and `GetMarker(i)`.
- Add a synthetic default option for current full-frame CPU/GPU totals so the
  current histogram behavior remains available.
- In non-profile builds, keep the synthetic full-frame option so the panel still
  works as a basic frame chart.
- Prefer profiler marker paths as selection identity, with hash as the fast key.
  If the selected marker disappears after a profiler reset, keep the selection
  label and show a waiting/empty state until it returns.

Validation focus:
- The marker list includes `Frame/Physics` after the first profiled frame.
- UI draw reads only the frame-data snapshot, not mutable profiler internals.
- Missing or delayed markers do not push zero samples that distort history.

## Step 2 - Refactor Histogram State For Marker Selection

Goal: make histogram history belong to the selected marker, not to the old
CPU/GPU frame-total pair.

Implementation shape:
- Extend `UIProfilerTabState` with:
  - selected marker hash/path label,
  - selected marker combo/picker state,
  - histogram panel bounds,
  - drag/resize state,
  - marker picker scroll/filter state if needed.
- Replace or extend `PerformanceHistogramSample` so it can represent one primary
  selected marker value plus an optional secondary value. The synthetic frame
  option can use CPU as primary and GPU as secondary; ordinary CPU profiler
  markers can use only primary unless GPU timing is available.
- Add helper functions to select a marker, clear history, and resolve the
  current selection against the latest marker catalog.
- Reset `histogramCount`, `histogramHead`, `histogramAxisMs`, and spike state
  when the selected marker changes.
- Keep selected marker and panel bounds when disabling the histogram.

Validation focus:
- Switching from full frame to `Frame/Physics` starts a new history ring.
- Switching back to full frame does not reuse stale physics samples.
- Selection survives scene reset and profiler marker reorder.

## Step 3 - Implement Marker-Local Axis Scaling

Goal: make small physics costs readable while still showing spikes.

Implementation shape:
- Replace the current 8 ms minimum axis floor with a "nice number" ladder that
  supports sub-millisecond and small-millisecond ranges, for example
  `0.25, 0.5, 1, 2, 4, 8, 12, 16, 24, 32, ...`.
- Compute the target axis from visible samples for the active marker only.
- Grow the axis immediately when a new sample exceeds the current axis or is a
  clear spike.
- Let the axis decay smoothly downward, but only toward the current visible
  history; do not let one old spike flatten the graph after it leaves the ring.
- Keep a spike marker/label for the largest visible sample, with placement that
  stays inside the resizable panel.
- Use a minimum visible bar height or line for non-zero values so low physics
  samples can be seen.

Validation focus:
- `Frame/Physics` remains readable before impact.
- Impact samples expand the scale in the ragdoll wall scene.
- After the scene sleeps, the visible range relaxes instead of staying pinned to
  the old spike forever.

## Step 4 - Add F5 Toggle Integration

Goal: make F5 the direct operator shortcut for the histogram panel.

Implementation shape:
- Add `RuntimeInputAction::TogglePerformanceHistogram` to
  `InputController.h`.
- Add `{ RuntimeInputAction::TogglePerformanceHistogram, VK_F5 }` to
  `AdvanceTakeInputKeyboardActionMemories` in `RunInput.cpp`.
- In `Run::TakeInput`, edge-detect F5 with
  `InputController::CaptureKeyboardActionPress`.
- Add a narrow `InGameUI` accessor or command such as
  `TogglePerformanceHistogram()` or `IsPerformanceHistogramEnabled()` so
  `RunInput.cpp` does not reach into `ProfilerTab` internals.
- Call `UpdateRuntimeInputModeAfterAction` with the new action and keyboard
  source so input diagnostics remain coherent.
- Do not call `EnterInteractiveSceneRun()` unless existing UI toggle semantics
  require operator input to suppress automation. Decide this explicitly during
  implementation; the safer initial behavior is to mirror the footer `Perf`
  toggle without altering scene physics.

Validation focus:
- Pressing and holding F5 toggles once per key press.
- F5 works when the full UI is hidden and when it is visible/minimized.
- Existing `0`, F2, F3, F7, and F8 behavior is unchanged.

## Step 5 - Let The Histogram Draw Without The Main Window

Goal: F5 should be useful as a lightweight demonstration overlay, not require
opening the full diagnostics window first.

Implementation shape:
- Add an `InGameUI` query such as `NeedsUiTextPass()` or
  `PerformanceHistogramEnabled()` that returns true when the floating histogram
  should draw.
- In `RunUiTextPass.cpp`, build the minimal `InGameUIFrameData` and call
  `m_host.m_UI.Draw(UIData)` when either the main UI is visible or the histogram
  is enabled.
- Update `InGameUI::Draw` so the early `!m_window.isVisible` return still allows
  drawing the floating histogram panel when enabled.
- Keep the main window draw/cache path unchanged when the full UI is visible.
- If drawing the histogram outside the main UI cache is simpler and safer, draw
  it after the cached main-window draw-list is flushed so histogram animation is
  never replayed stale.

Validation focus:
- Hide/minimize the main UI, press F5, and see only the histogram panel.
- The panel still draws over the full UI when both are visible.
- UI draw-call accounting remains reasonable and no stale cached histogram
  geometry appears.

## Step 6 - Add Drag And Resize Interaction

Goal: make the panel position and size operator-controlled without disrupting
camera/editor input outside the panel.

Implementation shape:
- Store panel bounds in UI state with defaults such as top-left `16,16` and a
  larger minimum than the old fixed panel, for example `320x150`.
- Use the panel header for dragging and a bottom-right affordance for resizing.
- Clamp bounds to the current screen after every drag, resize, screen resize, or
  default placement.
- Enforce min/max dimensions so text, selector, axis labels, and plot area stay
  readable.
- Capture/release mouse through existing `InputControl::BeginMouseCapture` and
  `EndMouseCapture`.
- When dragging, resizing, or interacting with the selector, set UI interaction
  state so camera mouse look and world tools do not consume the same pointer
  event.
- Add cancel behavior for focus loss or panel disable.

Validation focus:
- Dragging does not move the main UI window.
- Resizing does not resize the main UI window.
- Panel controls do not fire scene/editor/manipulator input underneath.
- Panel remains visible after window resize and scene reset.

## Step 7 - Add A Bounded Marker Selector

Goal: make marker selection discoverable without a giant off-screen dropdown.

Implementation shape:
- Reuse `UIComboBox` only if the option count can be bounded. Otherwise add a
  profiler-specific picker that supports a visible row count and wheel scrolling.
- Pin high-value markers near the top: full frame, `Frame/Physics`,
  `Frame/Render`, `Frame/UI`, and `Frame/VsyncWait` if present.
- For all other markers, list current profiler marker paths in stable order.
- Display the selected marker in the panel header or selector row. Use full path
  where space allows, falling back to a clipped leaf-name display.
- Selecting a marker closes the picker, resets history, and immediately samples
  from that marker on the next draw.
- If the picker is open, clicks and wheel input inside it should be consumed by
  UI, not by the scene camera or main window scroll.

Validation focus:
- `Frame/Physics` is reachable quickly.
- Large marker catalogs stay within the screen.
- Selector text does not overlap the axis label, resize handle, or plot.

## Step 8 - Preserve Existing Entry Points

Goal: avoid regressions for current scene-authored and footer-controlled
histogram behavior.

Implementation shape:
- Keep `InGameUI::SetPerformanceHistogramEnabled(bool)` as the public setter
  used by scene `ui.histogram`, UI stress, and the footer `Perf` toggle.
- Decide whether the footer `Perf` toggle controls the same floating panel or
  remains a main-window-only shortcut. Prefer controlling the same floating
  panel so there is only one histogram state.
- Update deterministic UI stress only if new picker/panel state needs coverage.
  The existing stress action that toggles the histogram should continue to work.
- If scene-authored `ui.histogram` enables the panel, use default bounds and the
  default marker unless future scene options explicitly configure more.

Validation focus:
- Existing scenes with `ui.histogram` still enable the histogram.
- The footer `Perf` control still toggles the histogram.
- UI stress does not crash when the histogram is toggled repeatedly.

## Step 9 - Documentation, Comments, And Validation

Goal: leave future agents with enough evidence to trust the UI/input change.

Implementation shape:
- Apply the repository comment standard to touched source-bearing files. Add
  nearby comments for non-obvious UI/cache/input ownership, not generic
  narration.
- Run the comment-style audit skill for touched source-bearing files before
  reporting implementation complete.
- During implementation, use targeted builds or manual launches only when they
  answer a specific question.
- For PR-bound validation, use `tools\validate_full.bat` because the change will
  touch runtime input and UI draw paths. Add `tools\validate_ui.bat` and
  `tools\validate_ui_stress.bat` when panel interaction or visual layout changes
  need dedicated UI evidence.
- Manual smoke target:
  `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json --vsync off`.
- Manual smoke steps: press F5, select `Frame/Physics`, drag the panel, resize
  the panel, watch the impact spike, and confirm the chart calms down as bodies
  settle.

## Likely Files To Inspect Or Edit

- `SkullbonezSource/Core/Profiler.h`
- `SkullbonezSource/Runtime/InputController.h`
- `SkullbonezSource/Runtime/RunInput.cpp`
- `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- `SkullbonezSource/UI/UI.h`
- `SkullbonezSource/UI/UI.cpp`
- `SkullbonezSource/UI/UITabProfiler.h`
- `SkullbonezSource/UI/UITabProfiler.cpp`
- `SkullbonezSource/UI/UIComboBox.h`
- `SkullbonezSource/UI/UIComboBox.cpp`
- `SkullbonezSource/Runtime/RunStress.cpp`
- `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp`
- `SkullbonezData/scenes/aaa_ragdoll_sunset_showcase.scene.json` for manual
  demonstration only.

## Risks

- Drawing only under `m_UI.IsVisible()` would make F5 appear broken when the main
  UI is hidden.
- Reusing the old full-frame sample ring after marker changes would flatten
  physics samples and hide the solver spike.
- A full marker dropdown can exceed the screen and become unusable.
- UI draw caching can replay stale histogram bars if the dynamic overlay is
  cached with static window geometry.
- Pointer capture bugs can make histogram dragging also move the camera, editor
  gizmo, or main UI window.
- Adding a new runtime action without updating action-memory setup can cause
  repeated toggles or missed edge detection.
- Sampling missing markers as zero can make scale/history misleading.

## Stop Conditions

- Stop if the implementation requires changing physics simulation behavior.
- Stop if F5 conflicts with an existing runtime command or validation shortcut
  not found during the plan review.
- Stop if the marker picker cannot be made bounded without changing generic UI
  combo behavior; split the picker into a dedicated follow-up slice.
- Stop before committing source changes without comment-style audit and the
  agreed validation evidence.

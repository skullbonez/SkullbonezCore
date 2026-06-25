# Runtime Interaction Click Regression Harness Mini Plan

Date: 2026-06-25
Status: Draft mini plan
Impact area: runtime input automation, inspect gizmo selection, replay prediction path selection, screenshots
Validation for this document-only change: none required

## Goal

Build a small deterministic harness that can perform real runtime clicks, assert
the interaction state changed correctly, capture screenshots, and then use that
harness to fix two new regressions:

1. In Inspect mode, left-clicking an object no longer selects it and brings up
   the transform gizmo.
2. In replay prediction mode, left-clicking an object no longer selects a path
   target and previews prediction paths.

This should be handled before the larger interaction state-machine cleanup. The
big refactor needs a safety rail; this harness is that rail.

## Why A New Harness Is Needed

Existing scene screenshot automation can place the UI cursor with `ui.mouse`,
but that only feeds UI hit testing. World interaction paths still read hardware
input through `Hardware::Input::GetClientMouseCoordinates()` and
`Hardware::Input::IsLeftMouseDown()`.

The failing behaviors are world-click behaviors, not static UI render states.
So the harness must drive the same runtime input path that real clicks use:

- left mouse down/up edge detection,
- client mouse position,
- current camera mode/workspace,
- UI mouse blocking,
- replay scrubber and prediction controls,
- editor/inspect selection state,
- screenshot capture after state changes.

Do not rely on a screenshot-only scene directive that merely places a fake UI
mouse. It will not prove the bug is fixed.

## Target Harness Shape

Add a focused runtime interaction automation path that can be invoked from the
command line or a scene file.

Preferred command-line shape:

```bat
Profile\SKULLBONEZ_CORE.exe ^
  --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json ^
  --interaction-script SkullbonezData\interaction\inspect_gizmo_click.json ^
  --interaction-report TestOutput\interaction\inspect_gizmo_click.json ^
  --frames 90 ^
  --vsync off
```

Equivalent scene-driven shape is also acceptable if it fits the parser better:

```json
{
  "interaction": {
    "script": "SkullbonezData/interaction/inspect_gizmo_click.json",
    "report": "TestOutput/interaction/inspect_gizmo_click.json"
  }
}
```

The harness should be deterministic and bounded. It should exit nonzero when an
assertion fails.

## Input Injection Contract

Add a test-only runtime input override that feeds the existing input paths.

Minimum required injected state:

- client mouse `x, y`,
- left button down,
- right button down,
- optional keyboard key down/press edges for shortcuts,
- frame number or simulation time for each action.

The cleanest implementation is to extend `Hardware::Input` with an automation
override used only when the harness is active:

```cpp
struct RuntimeInputAutomationState
{
    bool enabled = false;
    bool hasMouseClientPosition = false;
    POINT mouseClientPosition = {};
    bool leftMouseDown = false;
    bool rightMouseDown = false;
};
```

Then route these methods through the override:

- `Input::GetClientMouseCoordinates()`
- `Input::IsLeftMouseDown()`
- `Input::IsRightMouseDown()`

Important: the override must preserve the normal edge path. The harness should
set left down for at least one frame and then release it so existing
`CaptureMouseButtons(...)`, replay scrubber `leftWasDown`, and velocity edit
edge logic all see realistic press/release transitions.

## Script Format

Keep the first script format tiny.

Example:

```json
{
  "version": 1,
  "actions": [
    { "frame": 5, "setCameraMode": "Inspect" },
    { "frame": 10, "clickObject": "inspect_target", "button": "left" },
    { "frame": 20, "assert": { "selectedObject": "inspect_target" } },
    { "frame": 20, "assert": { "owner": "InspectGizmo" } },
    { "frame": 25, "screenshot": "TestOutput/interaction/inspect_gizmo_after_click.bmp" }
  ]
}
```

Replay prediction example:

```json
{
  "version": 1,
  "actions": [
    { "frame": 20, "showReplayScrubber": true },
    { "frame": 25, "clickReplayControl": "predict" },
    { "frame": 35, "clickObject": "path_striker", "button": "left" },
    { "frame": 55, "assert": { "replayPredictionEnabled": true } },
    { "frame": 55, "assert": { "replayPathTarget": "path_striker" } },
    { "frame": 70, "assert": { "predictionPathVisible": true } },
    { "frame": 75, "screenshot": "TestOutput/interaction/replay_prediction_after_click.bmp" }
  ]
}
```

Keep helper actions semantic where they reduce flakiness:

- `clickObject` should project the named object to screen and click its center.
- `clickReplayControl` should use the existing replay control rectangles rather
  than hardcoded pixels.
- Raw `mouseDown`, `mouseUp`, and `mouseMove` actions can still exist for
  debugging.

## Object Projection

The harness should not require hand-maintained screen coordinates for object
clicks.

Add a helper that:

1. Finds a model by scene object name.
2. Reads its current world position.
3. Projects the world position through the active camera to client coordinates.
4. Injects a left down/up click at that client coordinate.
5. Records the computed click coordinate in the report.

Fallback for phase 1 is acceptable:

- start with fixed, known camera scenes,
- hand-code stable coordinates in the script,
- then add `clickObject` projection before fixing the bugs.

The final harness should prefer semantic `clickObject` because it survives
camera and resolution changes better.

## State Report Contract

The harness must write a JSON report with enough state to prove the click did
the right thing.

Minimum report fields:

```json
{
  "ok": true,
  "scene": "...",
  "script": "...",
  "framesRun": 90,
  "actions": [
    {
      "frame": 10,
      "type": "clickObject",
      "target": "inspect_target",
      "mouse": [640, 360],
      "consumed": true
    }
  ],
  "finalState": {
    "cameraMode": "Inspect",
    "workspace": "Inspect",
    "owner": "InspectGizmo",
    "selectedObject": "inspect_target",
    "selectedModelIndex": 0,
    "gizmoVisible": true,
    "replayPredictionEnabled": false,
    "replayPathTarget": "",
    "predictionPathVisible": false
  },
  "assertions": [
    { "frame": 20, "name": "selectedObject", "expected": "inspect_target", "actual": "inspect_target", "passed": true }
  ],
  "screenshots": [
    "TestOutput/interaction/inspect_gizmo_after_click.bmp"
  ]
}
```

Useful inspected states:

- `RunCameraMode`
- `RuntimeWorkspace`
- `WorldInteractionOwner`
- selected editor/inspect model index
- selected editor/inspect model name
- gizmo visible/active/hot-axis state
- replay prediction enabled
- replay path selected target names/count
- prediction cache/path visibility or draw request count
- whether a click was suppressed by UI mouse blocking
- whether a world click was consumed by editor, manipulator, attach, replay, or
  launcher handling.

## Screenshot Verification

State assertions are the primary pass/fail signal. Screenshots are secondary
visual evidence.

The harness should capture BMPs after the expected state change:

- Inspect click: screenshot should show the selected object with transform gizmo
  handles visible.
- Replay prediction click: screenshot should show prediction path lines or path
  target visual feedback.

Add a lightweight screenshot checker only if it is cheap and robust:

- inspect screenshot exists and is non-empty,
- optional pixel-difference check against a pre-click screenshot,
- optional heuristic for bright gizmo/path overlay pixels in a bounded region.

Do not block the first fix on perfect image recognition. A JSON state report
plus screenshot artifact is enough for the first pass.

## Harness Scenes

Add two small deterministic scenes.

### Inspect Gizmo Harness Scene

Suggested path:

```text
SkullbonezData/scenes/interaction_inspect_gizmo_harness.scene.json
```

Scene requirements:

- one obvious dynamic or fixed target named `inspect_target`,
- simple background,
- terrain/water hidden if possible,
- fixed camera looking directly at the object,
- physics paused or deterministic,
- UI minimized or non-blocking,
- starts in Inspect mode if scene support exists, otherwise script switches to
  Inspect at frame 5.

Expected assertions:

- click target is computed inside the viewport,
- left click is not blocked by UI,
- selected object becomes `inspect_target`,
- `WorldInteractionOwner` becomes `InspectGizmo`,
- gizmo visible state is true,
- screenshot exists.

### Replay Prediction Harness Scene

Suggested path:

```text
SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json
```

Can start from `SkullbonezData/scenes/replay_path_pool.scene.json` because it
already has named path bodies.

Scene requirements:

- replay capture enabled,
- enough frames to have solver replay samples,
- prediction controls available,
- stable target named `path_striker`,
- deterministic fixed-step playback,
- camera sees the path bodies and path overlay region.

Expected assertions:

- replay solver/presentation samples are available,
- prediction can be toggled on,
- click on `path_striker` is not consumed by the wrong owner,
- replay path target becomes `path_striker`,
- prediction path data or draw requests become non-empty,
- screenshot exists.

## Implementation Phases

### Phase 1: Harness Plumbing Only

Tasks:

1. Add runtime input automation override.
2. Add script loading for a minimal JSON action list.
3. Add frame-based action execution.
4. Add report JSON writing.
5. Add screenshot action support using existing capture code.
6. Add a tiny smoke script that clicks a harmless screen point and reports the
   injected mouse coordinates.

Acceptance:

- The app can run a script, inject left down/up edges, write a report, and exit.
- No gameplay/input bug fix yet.

Validation while iterating:

- focused Profile build,
- focused harness run.

PR gate if committed alone:

- `tools\validate_full.bat`.

### Phase 2: Inspect Gizmo Regression Harness

Tasks:

1. Add the inspect harness scene.
2. Add `clickObject` support or stable click coordinates.
3. Add assertions for selected object, owner, and gizmo visibility.
4. Capture before/after screenshots.
5. Run it against current code and confirm it fails before fixing the bug.

Acceptance:

- The harness reproduces the broken left-click inspect selection.
- The report clearly states which expected state did not change.

Validation while iterating:

- focused harness run only.

### Phase 3: Replay Prediction Regression Harness

Tasks:

1. Add the replay prediction harness scene or reuse a focused copy of
   `replay_path_pool.scene.json`.
2. Add semantic clicks for replay prediction UI controls.
3. Add semantic click for the replay path target object.
4. Add assertions for prediction enabled, selected path target, and prediction
   path visibility.
5. Capture before/after screenshots.
6. Run it against current code and confirm it fails before fixing the bug.

Acceptance:

- The harness reproduces the broken prediction path selection/preview.
- The report identifies whether the click was blocked, consumed by another
  owner, or failed after picking.

Validation while iterating:

- focused harness run only.

### Phase 4: Fix Inspect Click State

Tasks:

1. Use the inspect harness failure report to locate whether the bug is:
   - UI blocking world clicks,
   - `InspectGizmoInteractionActive()` false,
   - `RuntimeInteractionController` owner mismatch,
   - camera mode/workspace mismatch,
   - picking failure,
   - selected model state not updated,
   - gizmo render visibility not tied to inspect selection.
2. Make the smallest code fix.
3. Re-run the inspect harness.
4. Inspect the after-click screenshot.

Acceptance:

- Harness passes.
- JSON state shows selected object and `InspectGizmo`.
- Screenshot shows gizmo handles.

### Phase 5: Fix Replay Prediction Click State

Tasks:

1. Use the replay harness failure report to locate whether the bug is:
   - prediction UI not visible/enabled,
   - replay scrubber mouse capture blocking world click,
   - world click consumed before replay path pick,
   - wrong `WorldInteractionOwner`,
   - `TryPickReplayPathTargetFromMouse(...)` not reached,
   - pick succeeds but selected target/prediction dirty state not updated,
   - prediction paths generated but not rendered.
2. Make the smallest code fix.
3. Re-run the replay prediction harness.
4. Inspect the after-click screenshot.

Acceptance:

- Harness passes.
- JSON state shows prediction enabled and target selected.
- Prediction path data/draw requests are non-empty.
- Screenshot shows path preview or clear target/path visual feedback.

### Phase 6: Tool Wrapper

Add a small wrapper script after both scenarios pass:

```text
tools/validate_interaction_clicks.bat
```

Responsibilities:

- build or reuse Profile according to repo validation conventions,
- run inspect harness,
- run replay prediction harness,
- fail if either report has `"ok": false`,
- print report paths and screenshot paths.

This script is a focused harness, not a replacement for the formal PR gate.

## Expected Root-Cause Areas

Inspect click likely lives near:

- `Run::TakeInput()` world-click routing,
- `TickEditorWorldClick(...)`,
- `InspectGizmoInteractionActive()`,
- camera mode/workspace synchronization,
- `TryPickEditorModel(...)`,
- `RuntimeInteractionController::BuildFramePolicy(...)`.

Replay prediction click likely lives near:

- `TickReplayScrubberInput(...)`,
- `TryPickReplayPathTargetFromMouse(...)`,
- replay prediction enable/dirty state,
- world-click routing after editor/manipulator/attach handling,
- `WorldInteractionOwner::ReplayPrediction` and `ReplayScrub` transitions,
- UI mouse blocking and minimized scrubber visibility.

## Guardrails

- Build the harness before fixing the bug.
- Confirm each harness fails on the current broken state before applying the
  corresponding fix.
- Keep harness changes separate from behavior fixes if possible.
- Do not make the massive interaction state-machine pass in this mini plan.
- Do not run broad validation while iterating; use targeted builds/harness runs.
- Before PR-bound commit/push, run `tools\validate_full.bat` because this
  touches `Run*`, runtime input, screenshots, and interaction behavior.
- If the fix changes physics stepping, impulses, or deterministic physics
  output, also run `tools\validate_physics.bat`.

## Final Done Criteria

This mini plan is done when:

- The harness can inject real left-click press/release through runtime input.
- The inspect scenario clicks an object, asserts selected object/state, and
  captures a screenshot with gizmo visible.
- The replay prediction scenario enables prediction, clicks a path target,
  asserts selected target/path state, and captures a screenshot with prediction
  preview visible.
- Both scenarios fail on the pre-fix regression and pass after the fix.
- The runner reports JSON artifacts and screenshot paths in the handoff.
- Formal PR-bound validation is selected and run according to the final touched
  files.

# Runtime Input State Centralization Plan

Status: Done on `nightrunner-20th-june`
Validation: `tools\validate_full.bat` passed on 2026-06-21 after implementation.

## Summary

Refactor runtime input around a single `RuntimeInputContext` owned by the input
layer. It will make the active interaction mode explicit, route keyboard/UI
requests through named actions, and keep durable render/physics/UI settings in
their existing owning systems.

Current scattered state found:
- `RunCameraState`: `isFlyMode`, `isLauncherMode`, packed key memory, mouse-look
  reset state.
- `RunEditorPlacementState`: editor enabled, placement/gizmo, viewport look,
  placement scaling, gizmo drag booleans, shortcut key memory.
- `RunDebugState`, `RunRuntimeSettings`, `RunRayCastTestState`: debug, render,
  and physics toggles changed by input or UI.
- `SkullbonezRunInput.cpp`: direct transition logic for F/N/M/backtick/Alt/Tab,
  mouse clicks, debug toggles, and UI command application.
- Raw `Input::IsKeyDown` calls also exist in frame/render paths for space
  stepping, water height page keys, shift speed, and old commented camera
  selection.

## Key Changes

- Add input-owned types in the existing input manager area:
  - `enum class RuntimeInputMode { Scene, FlyCamera, Launcher, EditorPlace,
    EditorGizmo, EditorViewportLook, EditorPlaceScale, EditorGizmoTranslate,
    EditorGizmoRotate, EditorGizmoScale }`.
  - `enum class RuntimeInputAction` for named one-frame requests: toggle fly,
    toggle launcher, toggle editor, toggle editor tool, cycle placement type,
    toggle static placement, toggle terrain align, cycle launcher fire mode,
    fire launcher, save snapshot, save screenshot, reset scene, debug toggles,
    scene navigation, UI visibility.
  - `RuntimeInputContext` containing current mode, previous mode for editor
    restore, key-edge memory, mouse state, and a small transition/debug ring
    buffer.
- Keep `Hardware::InputState` as raw device/key memory only. Do not make it the
  semantic mode state.
- Move mode-transition helpers out of `SkullbonezRun::TakeInput()` into
  `InputController` methods:
  - `BeginFrame(...)` captures device state and UI blocking.
  - `QueueKeyboardActions(...)` turns edges into `RuntimeInputAction`s.
  - `ApplyModeAction(...)` updates `RuntimeInputContext` and returns side-effect
    requests.
  - `DescribeMode()` and `DescribeLastTransitions()` expose debuggable strings
    for HUD/logging.
- Update `SkullbonezRunInput.cpp` so it consumes actions from the input context,
  then applies side effects to existing owners:
  - Camera side effects still call camera collection, cursor visibility,
    mouse-look reset, and simulation freeze behavior.
  - Editor object placement/gizmo data stays in `RunEditorPlacementState`, but
    active editor interaction mode comes from `RuntimeInputContext`.
  - Debug/render/physics booleans stay in `RunDebugState`, `RunRuntimeSettings`,
    and `RunRayCastTestState`; input/UI changes to them pass through named
    actions.
- Update UI frame data to receive/display `RuntimeInputMode` instead of
  reconstructing editor/fly/mouse status from several booleans. Keep UI commands
  as one-frame requests.
- Remove static local key memories in `TakeInput()` such as `s_key2WasDown` and
  F7/F8 pipeline memory by moving edge memory into the input context.

## Behavior Rules

- Mode precedence is explicit:
  - UI text/input capture blocks keyboard actions and clears movement.
  - Editor modes override fly/launcher while active and restore previous
    fly/launcher state on exit.
  - Launcher implies fly-camera controls but does not freeze simulation.
  - Plain fly freezes simulation and camera auto-cycle as today.
  - Editor viewport look is a temporary mode while right mouse is held.
  - Placement scale and gizmo drag are transient modes entered by left mouse and
    exited on release or UI suppression.
- Input manager logs every mode transition as `{from, to, action, source}` where
  source is `Keyboard`, `UI`, `Mouse`, or `FocusLost`.
- No renderer, physics, or scene setting becomes input-owned; the input layer
  only names and routes the request.

## Test Plan

- Add focused CPU tests if a small input-mode test target is practical;
  otherwise add deterministic helper tests inside an existing CPU-side test
  pattern only if it does not pull Win32 runtime state.
- Manual behavior checks after implementation:
  - F toggles fly on/off, hides/restores cursor, and freezes/unfreezes physics
    as before.
  - N enters/exits launcher, keeps simulation live, M cycles laser/projectile,
    and left click fires.
  - Backtick enters editor place mode, Alt toggles Place/Gizmo, and Tab/Ctrl+Tab
    preserve current editor behavior.
  - Right mouse in editor enters viewport look only while held.
  - Left-drag placement scale and gizmo translate/rotate/scale enter the correct
    transient modes and clear on release, UI capture, or focus loss.
  - Debug toggles from keyboard and UI still mutate the same runtime settings.
- PR gate: `tools\validate_fast.bat` because this is a non-render code refactor.
  If implementation changes runtime launch or simulation/fly semantics beyond
  routing, use `tools\validate_full.bat`.

## Assumptions

- Scope is `Mode + Actions`: input owns interaction mode/action routing and
  debug trace; durable render/physics/UI settings remain in their current
  owners.
- Impact area is runtime input/UI and scene interaction, not DX12 or physics
  solver internals.
- Pre-existing dirty files are user-owned and should not be touched for this
  work.

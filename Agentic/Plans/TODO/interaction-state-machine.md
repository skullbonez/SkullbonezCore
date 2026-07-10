# Interaction State Machine Hardening

Date: 2026-07-09 (consolidated)
Status: In progress — ~45% complete
Impact area: runtime input, editor tools, replay UI, camera policy, tool
physics stepping
Consolidates: `runtime-interaction-state-machine-hardening-plan.md`. The full
design (target type sketches, controller/tool contracts, routing order,
command/event tables, manual test matrix) is in that file's git history; this
version keeps the operating rules and the remaining phases.

## Goal

Mouse selection, camera mode, editor mode, replay mode, launcher, manipulator,
and pointer capture behave as one system:

```text
State machine for ownership.        One workspace, one tool, at most one
Commands for mutation.              pointer gesture owns world input at any
Events for observation.             instant. Everything else is passive
Typed gesture state, not bools.     rendering or notification.
Central input routing.
```

## Design principles (binding for every slice)

1. One authority (`RuntimeInteractionController`) owns interaction state.
2. One pointer gesture owns the mouse at a time.
3. A transition exits the previous owner before entering the next.
4. Exit routines clear transient state completely.
5. Tools request transitions; they never patch global mode state directly.
6. Commands mutate; events notify after successful mutation.

## Already done (summary)

Typed gesture/pointer-capture state on `RuntimeInteractionController` (camera
look, gizmo drag, mouse pickup, replay scrub/velocity/prediction/cause-tree
kinds); `TakeInput()` builds `RuntimeInputSnapshot` and routes pointer input
through `RouteRuntimePointerInput(...)`; camera-label and world-owner writes
are bridge-guarded; editor selection flows through `RuntimeInteractionCommand`
with a result event; picking is centralized behind
`RuntimePickService::TryPickModel(...)`; replay pause state distinguishes
`historicalSamplePaused` from `liveAdvanceHeld`; boundary checks block direct
camera-mode/owner writes and duplicate pick helpers.

## Remaining phases

- [ ] P4. **Camera look + pointer capture.** Cursor hide/restore behind
  controller policy; right-mouse/free-look as `CameraLookGesture`; focus loss
  → one central `CancelPointerCapture`; left-button tool gestures block camera
  look until release. Encode the product policy for Inspect/Launcher rotation.
  Gate: `validate_full`.
- [ ] P5. **Launcher + manipulator tools.** Tool handlers own left-click fire
  and pickup-drag (`MousePickupDragGesture`); transitions clear other
  workspaces' gestures. **Check first:** manipulator drag must not be routed
  through `RunWhileStepHeld` (drag must not depend on holding `Space`). Gate:
  `validate_full` (+ `validate_physics` if impulse/stepping semantics move).
- [ ] P6. **Editor placement + gizmo.** Placement click/preview into
  `EditorPlacement`; gizmo hover/drag into typed `GizmoDragGesture`; editor
  exit clears preview, hot axes, drag capture. Decide whether inspect and
  editor selection share one selection model. Gate: `validate_full`.
- [ ] P8. **Replay interaction migration.** Scrub, velocity edit, prediction
  horizon, and cause-tree drags become replay-owned gesture payloads; replay
  exit cleanup clears all of them plus transient path/branch selection. Gate:
  `validate_full`.
- [ ] P9. **Commands for remaining high-risk mutations** (tool changes,
  gesture begin/end, launcher fire, manipulator begin/end, replay live
  hold/scrub) with events published after success. Gate: `validate_full`.
- [ ] P10. **Delete the bool clusters.** Remove transient booleans replaced by
  gestures, compatibility wrappers that only forward, and duplicated pick
  helpers. Review rule (not a regex ratchet — see engine-cleanup plan 03): no
  direct camera-mode writes outside the bridge, no direct owner writes outside
  the controller, no new replay pause aliases. Gate: `validate_full`.

Coordinate P4/P5 file moves with `TODO/runtime-shell-decomposition.md` D1
(`RunInput.cpp` split) so input code moves once.

## Key manual checks per slice

Focus loss releases capture and cursor; camera look cannot start during any
left-button drag; UI clicks never select world objects behind UI; mouse-up
reaches the gesture that captured mouse-down even over UI; leaving any
workspace mid-drag leaves no live handles or capture.

## Acceptance

- [ ] The runtime input path reads as a router, not a mode maze.
- [ ] `RunCameraMode`/workspace/owner/editor/replay booleans no longer
  describe overlapping "what mode are we in?" state.
- [ ] Every gesture is a typed payload with an owner and a cleanup path.

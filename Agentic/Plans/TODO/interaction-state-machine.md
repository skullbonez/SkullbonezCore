# Interaction State Machine Hardening

Date: 2026-07-10 (reconciled)
Status: Complete - 6/6 remaining phases and all acceptance rows proven
Impact area: runtime input, editor tools, replay UI, camera policy, pointer
capture, tool physics stepping
Owner: `RuntimeInteractionController`

## Goal

Mouse selection, camera mode, editor mode, replay mode, launcher, manipulator,
UI controls, and pointer capture behave as one system: one workspace, one
gesture owner, one cleanup path. Commands mutate; events notify after success;
UI hit testing produces typed actions rather than editing domain state.

## Binding Rules

1. `RuntimeInteractionController` is the sole workspace/gesture/pointer-capture
   authority.
2. One pointer gesture owns input at a time; mouse-up is routed to the owner that
   captured mouse-down, even when the pointer crosses UI.
3. Transitions exit the previous owner completely before entering the next.
4. Focus loss, scene load, workspace exit, and unavailable UI use the same
   cancellation path.
5. Tools request transitions; they do not patch controller, camera, or replay
   mode fields directly.
6. Commands mutate; events are emitted only after successful mutation.

## Reconciled Foundation

Typed gesture/pointer state, central pointer routing, centralized picking,
camera/owner write guards, editor selection commands, replay live-vs-historical
pause distinction, and CPU controller tests exist. The remaining problem is
integration: `RunInput.cpp` and replay/editor UI still construct large condition
and callback graphs around the controller.

## Remaining Phases

- [x] **I4 — Camera look and pointer capture.** Cursor hide/restore behind
  controller policy; focus loss uses one cancel path; left-button tools exclude
  camera look by gesture ownership, not condition order.
- [x] **I5 — Launcher and manipulator.** Tool handlers own fire/pickup drag;
  transitions clear other workspaces. Prove manipulator drag is independent of
  `RunWhileStepHeld`/Space.
- [x] **I6 — Editor placement and gizmo.** Typed placement/gizmo gestures own
  preview, hover, hot axis, drag, release, and editor-exit cleanup. Record the
  inspect/editor selection ownership decision.
- [x] **I8 — Replay gestures.** Scrub, velocity, prediction horizon, and
  cause-tree drags use replay-owned payloads and one transition cleanup path.
- [x] **I9 — Remaining commands/events.** Tool changes, gesture begin/end,
  launcher fire, manipulator begin/end, replay hold/scrub, and camera requests
  route through typed commands with post-success events.
- [x] **I10 — Delete compatibility state.** Remove replaced booleans, direct
  owner/camera writes, forwarding wrappers, duplicate pick helpers, and manual
  capture begin/end paths.

## Dependencies

- UI control/action/gesture geometry is owned by
  `runtime-ui-control-architecture-cleanup.md`; do not implement a second UI
  gesture model here.
- `runtime-shell-decomposition.md` extraction 1 moves orchestration out of
  `Run`; perform the move once after the phase-specific behavior is covered.
- Replay phase I8 coordinates with
  `replay-architecture-and-right-sizing.md` R2.
- All CPU suites become mandatory through `validation-gate-integrity.md`.

## Required Behavioral Matrix

- Focus loss releases capture and restores cursor.
- Camera look cannot begin during any left-button drag.
- UI clicks never select the world behind the UI.
- Release reaches the gesture that captured press, even over UI.
- Scene load/workspace exit mid-drag leaves no live handle/capture/legacy flag.
- Manipulator drag does not require Space.

Each row must have a CPU controller test where possible and an interaction
automation proof for Win32/UI integration.

## Acceptance

- [x] All six remaining phases and behavioral-matrix rows are proven.
- [x] Runtime input reads as routing of snapshots/actions, not a mode maze.
- [x] No overlapping booleans describe current workspace/gesture ownership.
- [x] Every gesture has typed owner, payload, begin/update/cancel/release.
- [x] `Run` no longer owns interaction business methods after runtime-shell
  extraction 1.

## Closure Evidence

- `RuntimeInteractionController` is the sole active workspace, owner, gesture,
  and pointer-capture store. Raw begin/end mutation is private; production and
  tests issue `RuntimeGestureCommand` values and observe only post-success
  `RuntimeGestureEvent` values.
- Editor placement scale, gizmo translate/rotate/scale, manipulator pickup,
  replay scrub, velocity edit, prediction horizon, and cause-tree move/resize
  keep active kind/axis/mode only in `RuntimeInteractionGesture`. The deleted
  replay/editor drag, capture, active-axis, and gizmo-mode mirrors cannot drift
  from controller state.
- Camera mode, launcher fire, editor mode edges, and pointer routing remain
  typed `RuntimeInputAction`/result paths. Selection retains the shared stable
  body/collider-handle decision for inspect and editor workspaces.
- `manipulator_pickup_click.json` proves Space-independent pickup, logical and
  native capture, focus-loss cancellation with cursor restoration, retention
  while the pointer crosses a UI-blocked surface, and release to the original
  owner over that surface.
- Deletion proofs find no production/test `.BeginGesture` or `.EndGesture`
  bypass calls and no stored `mouseCaptured`, replay drag, placement-active,
  gizmo-active/mode, or active-axis compatibility fields.
- The plan-level adversarial review found Debug-only rejection assertions and
  missing native/UI-crossing evidence. Both were fixed; the one required repeat
  review found no blocking, non-blocking, or missing-evidence findings. A final
  main-pass inventory then caught and deleted `RunMousePickupState::active`, the
  last generic gesture mirror; final gates cover the corrected source. See
  `Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md`.

## Final Validation Evidence

- Final focused policy plus Win32 runs passed serially in 28.2s: Debug/Release
  typed placement and fail-closed command tests, then all five click scenarios.
- `tools\validate_replay_scrub.bat`: all scrub/prediction probes passed from
  final source in 69.0s with zero-warning Profile and Debug builds.
- `tools\validate_full.bat`: passed in 72.5s; 131/131 doctest cases and 2,814
  assertions, every standalone CPU lane, zero-warning Profile/Debug builds,
  zero DX12 InfoQueue errors with matching screenshots, standalone/runtime
  physics handle smoke, and the 44,401-line varied baseline byte-exact.
- Comment-style audit: 38 touched source-bearing files checked, 0 deferred, 0
  unchecked; every file retains a complete learning header and local ownership,
  lifetime, invariant, or hazard notes where the changed path is non-obvious.

## Validation

For every slice: CPU umbrella (or explicit
`validate_runtime_interaction_policy.bat` until V1), the relevant interaction
automation, `tools\validate_interaction_clicks.bat`, and `validate_full` for
cross-subsystem runtime changes. Add physics validation when impulse/fixed-step
semantics move and replay scrub when replay gestures move.

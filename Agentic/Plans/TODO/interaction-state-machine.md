# Interaction State Machine Hardening

Date: 2026-07-10 (reconciled)
Status: In progress — 0/6 remaining phases complete; earlier typed-state and
central-picking work is historical foundation
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

- [ ] **I4 — Camera look and pointer capture.** Cursor hide/restore behind
  controller policy; focus loss uses one cancel path; left-button tools exclude
  camera look by gesture ownership, not condition order.
- [ ] **I5 — Launcher and manipulator.** Tool handlers own fire/pickup drag;
  transitions clear other workspaces. Prove manipulator drag is independent of
  `RunWhileStepHeld`/Space.
- [ ] **I6 — Editor placement and gizmo.** Typed placement/gizmo gestures own
  preview, hover, hot axis, drag, release, and editor-exit cleanup. Record the
  inspect/editor selection ownership decision.
- [ ] **I8 — Replay gestures.** Scrub, velocity, prediction horizon, and
  cause-tree drags use replay-owned payloads and one transition cleanup path.
- [ ] **I9 — Remaining commands/events.** Tool changes, gesture begin/end,
  launcher fire, manipulator begin/end, replay hold/scrub, and camera requests
  route through typed commands with post-success events.
- [ ] **I10 — Delete compatibility state.** Remove replaced booleans, direct
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

- [ ] All six remaining phases and behavioral-matrix rows are proven.
- [ ] Runtime input reads as routing of snapshots/actions, not a mode maze.
- [ ] No overlapping booleans describe current workspace/gesture ownership.
- [ ] Every gesture has typed owner, payload, begin/update/cancel/release.
- [ ] `Run` no longer owns interaction business methods after runtime-shell
  extraction 1.

## Validation

For every slice: CPU umbrella (or explicit
`validate_runtime_interaction_policy.bat` until V1), the relevant interaction
automation, `tools\validate_interaction_clicks.bat`, and `validate_full` for
cross-subsystem runtime changes. Add physics validation when impulse/fixed-step
semantics move and replay scrub when replay gestures move.

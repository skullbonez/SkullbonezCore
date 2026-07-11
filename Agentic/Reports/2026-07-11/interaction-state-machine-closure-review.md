# Interaction State Machine Closure Review

Date: 2026-07-11
Plan: `Agentic/Plans/TODO/interaction-state-machine.md`
Outcome: clean after two review findings, the required repeat, and one final main-pass mirror deletion

## Expected outcome

`RuntimeInteractionController` is the single workspace, gesture, and pointer-
capture authority. Every drag carries a typed owner and payload, commands mutate
before events publish, focus/workspace/scene cancellation shares owner cleanup,
and no replay/editor/tool boolean can disagree with the active gesture.

## First adversarial review

- **Blocking:** `BeginGesture` asserted in Debug when a command was rejected,
  while Release returned `false`. That contradicted the fail-closed command
  contract and left malformed/busy-command behavior untested in the development
  configuration.
- **Fix:** rejected begins now return an unchanged transition in every build.
  `InvalidToolGestureWithoutCaptureIsRejected` runs in Debug and Release and
  proves the event remains empty and capture remains unclaimed.
- **Missing evidence:** focus-loss automation asserted logical pointer ownership
  only. It did not prove native capture/cursor restoration or that release still
  reached the press owner after the pointer crossed a UI-blocked surface.
- **Fix:** bounded automation now exposes mouse movement plus native-capture,
  cursor-visibility, and UI-block assertions. The manipulator script proves
  capture while held, focus-loss cleanup, cursor restoration, a second held
  pickup over a UI-blocked surface, and clean release there.

## Required repeat review

No blocking, non-blocking, or missing-evidence findings remain.

Deletion-proof searches return no production/test calls to raw `.BeginGesture`
or `.EndGesture`; both mutations are controller-private. No stored field remains
for mouse capture, replay dragging/horizon/move/resize, editor placement/gizmo
activity or gizmo transform mode, or active replay/editor axes. Local booleans
in presentation/input functions are derived from the current typed gesture and
are not retained authority.

Native capture requests occur only after an accepted typed begin. Release paths
derive the active kind from the controller, end through `RuntimeGestureCommand`,
then release native capture. Focus loss cancels camera, replay, manipulator, and
editor gestures before resetting input edge memory.

## Final main-pass inventory

After the permitted repeat review, the primary implementation review expanded
the generic active-field search and found `RunMousePickupState::active`. It was
a credible ownership mirror even though the earlier named deletion proof had
covered only capture and domain-specific drag spellings. The field is deleted;
pickup routing, physics, overlays, render work, reports, and assertions now
derive activity from `RuntimeInteractionGestureKind::MousePickupDrag`. The final
focused, replay, and broad gates below all ran after this correction. No third
independent review was performed, preserving the plan's one-repeat rule.

## Behavioral matrix evidence

- Focus loss releases logical/native capture and restores the cursor: CPU
  controller test plus manipulator Win32 automation frames 10-13.
- Camera look cannot begin during left-button tools: controller policy tests and
  typed-capture assertions.
- UI cannot steal or redirect a held world gesture: manipulator automation moves
  the pointer onto a UI-blocked surface at frame 22, retains the original owner
  at frame 23, and releases cleanly by frame 25.
- Workspace and scene transitions clear typed gesture/capture state: controller
  transition tests plus the shared focus/scene cleanup paths.
- Manipulator physics advances without Space: CPU policy test and the normal
  manipulator click scenario.
- Inspect/editor selection uses stable body/collider handles; placement and all
  gizmo modes publish typed gesture payloads rather than parallel active flags.

## Validation evidence

- Final serial policy and five-scenario interaction runs passed in 28.2s.
- `tools\validate_replay_scrub.bat`: passed from final source in 69.0s.
- `tools\validate_full.bat`: passed in 72.5s; 131/131 doctest cases and 2,814
  assertions, all standalone CPU lanes, zero-warning Profile/Debug builds, zero
  DX12 InfoQueue errors, matching DX12 screenshots, physics handle smoke, and
  the 44,401-line varied baseline byte-exact.

## Comment audit

The touched-file comment audit checked 38 source-bearing files, with 0 deferred
and 0 unchecked. Learning headers remain complete. Local comments explain the
single gesture-payload authority, post-success event edge, capture lifetime,
replay drag-start values, stable body handles, and focus-loss cancellation.

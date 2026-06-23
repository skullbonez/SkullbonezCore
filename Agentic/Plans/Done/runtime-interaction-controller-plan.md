# Runtime Interaction Controller Plan

Date: 2026-06-23
Status: Draft implementation plan
Impact area: runtime input, replay UI, editor interaction, simulation stepping
Validation for this document-only change: none required

## Goal

Make runtime interaction state explicit and authoritative.

The current camera, replay, editor, launcher, manipulator, cursor, and
simulation-pause rules are spread across booleans such as camera mode, replay
pause, scrub state, editor mode, placement mode, prediction, velocity edit, and
mouse capture. That lets states overlap accidentally. Example: entering editor
from replay can leave scrubbed replay state, prediction state, selection, or
simulation pause alive behind the editor.

Create a dedicated runtime interaction subsystem that owns those transitions,
so entering a workspace or tool mode decisively exits any incompatible
workspace or tool owner first.

## Core Model

Add a dedicated source pair:

- `SkullbonezSource/Runtime/RuntimeInteractionController.h`
- `SkullbonezSource/Runtime/RuntimeInteractionController.cpp`

Suggested owner name: `RuntimeInteractionController`.

The controller should own high-level state and produce a per-frame policy for
Run to consume:

```cpp
enum class RuntimeWorkspace
{
    Live,
    Inspect,
    Edit,
    Replay
};

enum class WorldInteractionOwner
{
    None,
    InspectGizmo,
    EditorPlacement,
    EditorGizmo,
    ReplayScrub,
    ReplayVelocityEdit,
    ReplayPrediction,
    ReplayBranchTarget,
    ReplayCauseTree,
    Launcher,
    Manipulator
};

enum class PhysicsAdvanceState
{
    Disabled,
    Paused,
    Running,
    RunWhileStepHeld
};

enum class CameraLookState
{
    Passive,
    RightMouseLook,
    EditorViewportLook,
    ReplayInspectionLook
};

enum class InteractionExitReason
{
    EnterLive,
    EnterInspect,
    EnterEdit,
    EnterReplay,
    EnterLauncher,
    EnterManipulator,
    ResetScene,
    LoadScene
};
```

`SimulationSystem` should consume only physics policy: advance state, fixed or
variable stepping, time scale, model collection, and physics callbacks. It
should not know about camera modes, editor state, replay state, or UI capture.

## Workspace Semantics

### Live

- Passive scene/demo operation.
- Physics follows scene settings and time scale.
- No generic world-click mutation.

### Inspect

- Replaces the useful behavior of the old `Free` mode.
- Right mouse uses existing cursor-hide mouse look.
- WASD moves the camera.
- Physics is paused by default.
- Holding `Space` advances simulation at the current scene time scale.
- Left click selects an existing object and shows the transform gizmo for live
  editing.
- Gizmo edits are explicit inspect-gizmo operations; they do not unpause
  simulation.

### Edit

- Owns editor placement and editor gizmos.
- Tilde enters this workspace.
- Entering from replay must cancel replay state before editor state is enabled.

### Replay

- Owns scrubber interaction, replay camera inspection, replay path targets,
  branch target selection, velocity edit/nudge, and prediction controls.
- Replay state may render passive UI while another workspace is active only
  after its active ownership state has been cleared.

### Launcher and Manipulator

Launcher and manipulator can remain camera modes or become tool modes, but their
policy must be explicit:

- `Launcher`: physics runs live; right mouse looks; WASD moves; left click fires.
- `Manipulator`: physics runs live; right mouse looks; WASD moves; left click
  physically drags bodies.
- Entering either tool must first exit the previous world-interaction owner,
  including replay velocity edit/nudge, replay prediction, editor gizmos,
  inspect gizmos, and any mouse capture they hold.

## Required Transitions

All high-level mode changes should go through controller methods such as:

- `EnterLive()`
- `EnterInspect()`
- `EnterEdit()`
- `EnterReplay()`
- `EnterLauncher()`
- `EnterManipulator()`
- `SetCameraMode(...)`
- `SetWorldInteractionOwner(...)`
- `BuildFramePolicy(...)`

Entering a workspace must first call the exit routine for the previous
workspace. Do not let callers toggle scattered booleans directly.

### Mode Transition Cleanup Contract

Every transition between active modes must use the same shape:

1. Read the current `RuntimeWorkspace`, `WorldInteractionOwner`,
   `CameraLookState`, and `PhysicsAdvanceState`.
2. Call `ExitWorldInteractionOwner(previousOwner, reason)`.
3. Call `ExitWorkspace(previousWorkspace, reason)` when the workspace changes.
4. Clear camera look and mouse capture owned by the previous mode.
5. Enter the new workspace or owner from a clean state.

Direct assignment to mode enums should be treated as a bug. New modes are not
responsible for tolerating old UI and input leftovers.

The exit routine for an owner must clear all transient state owned by that
owner:

- hover state
- drag state
- active axes and handles
- mouse capture and cursor hide requests
- pending clicks and held buttons
- path, prediction, or preview state owned by the old mode
- UI panels or affordances that are only valid while the old owner is active

### Replay Velocity Edit/Nudge To Launcher

If the user is in replay velocity edit/nudge and presses the launcher shortcut,
the transition must kill nudge before launcher receives input:

- disable replay velocity edit/nudge
- clear active linear and angular axes
- clear hover, drag, and capture state
- clear prediction/horizon drag state produced by the nudge tool
- clear replay-owned selection or handles that are only valid for velocity edit
- release replay mouse capture and cursor ownership
- set `WorldInteractionOwner::Launcher`
- enter live launcher policy: physics runs, right mouse looks, left click fires

The same rule applies in the other direction. Launcher or manipulator input
must be killed before replay, edit, or inspect ownership can start.

### Entering Edit From Replay

When tilde enters editor while replay is active, the transition must:

- snap scrubber position back to live/current time
- clear historical scrub pause
- clear live replay hold/pause
- clear prediction state and horizon drag
- clear velocity edit state and mouse capture
- clear branch target and restore selection
- clear cause-tree selection, focus, dragging, and resizing
- clear replay path visualizer targets
- cancel replay mouse capture
- exit replay inspection camera
- clear replay-owned object selection

Then enter editor state:

- enable editor workspace
- clear mouse pickup and launcher transient input
- clear placement preview/gizmo drags as needed
- start in a deterministic editor tool state

### Entering Replay From Edit

Entering replay from editor must:

- cancel editor placement
- cancel editor gizmo drag, rotation, and scale
- clear editor placement preview and mouse capture
- preserve no active edit selection unless replay explicitly selects it
- disable editor world-click ownership

## Replay Pause Naming

Split the current ambiguous replay pause concepts.

Current behavior:

- `m_replayScrubber.paused` means the scrubber is parked on a historical replay
  sample.
- `m_replayScrubber.simulationPaused` means the live solver replay is held at
  the current frame for replay tools.

Rename the concepts during the refactor:

```cpp
enum class ReplayScrubState
{
    Live,
    ScrubbedHistoricalSample
};

enum class ReplayLiveAdvance
{
    Running,
    HeldAtCurrentFrame
};
```

The current replay `PAUSE` button controls live solver hold, not historical
scrub pause. Rename the button to `HOLD` / `LIVE`, or fold this behavior into
`Inspect` if the UI becomes workspace-driven.

## Input and UI Rules

- Camera combo and Tab cycle should expose `Demo` or `Scene`, `Inspect`,
  `Launcher`, and `Manipulator`. Do not expose `Free`.
- `F` toggles between passive mode (`Demo` or `Scene`) and `Inspect`.
- `N` toggles between `Launcher` and the previous non-launcher mode.
- Right mouse look is active only when the current policy says camera look owns
  the mouse.
- Cursor hiding is a result of `CameraLookState`, not a permanent side effect of
  a camera mode.
- World clicks are owned by exactly one `WorldInteractionOwner`.
- A new `WorldInteractionOwner` can only be entered through the controller
  transition API. It must never be set by directly assigning an enum or toggling
  an old feature boolean.
- Tool UIs should render from authoritative owner state. If an owner is not
  active, its handles, previews, drag targets, hot axes, and hover affordances
  must not render or consume input.

## Implementation Slices

1. Add `RuntimeInteractionController` with enums, state, and a read-only
   frame-policy builder that mirrors existing behavior.
2. Route `RunFrame::TickPhysics` through the new frame policy without changing
   behavior.
3. Rename `Free` to `Inspect` in user-facing camera mode labels and shortcut
   behavior.
4. Move replay/editor/inspect entry transitions into the controller.
5. Add explicit `WorldInteractionOwner` transitions for inspect gizmo, editor
   placement/gizmo, replay scrub, replay velocity edit/nudge, replay prediction,
   replay branch target, launcher, and manipulator.
6. Add hard cleanup for `Replay -> Edit`, `Edit -> Replay`, and every
   tool-to-tool transition.
7. Move cursor ownership and right-mouse look decisions behind controller policy.
8. Remove obsolete direct boolean toggles once the controller is authoritative.

## Test Plan

During implementation, use focused manual checks and targeted builds only when
they answer a specific question. Final PR-bound validation should use
`tools\validate_full.bat` because this touches `Run*` behavior and simulation
stepping policy.

Focused scenarios:

- Enter `Inspect`; physics pauses; holding `Space` advances; releasing `Space`
  pauses again.
- In `Inspect`, left click selects a body and transform gizmo edits it while
  simulation remains paused.
- In `Manipulator`, left click physically drags bodies and simulation runs live.
- In `Launcher`, left click fires and simulation runs live.
- Open replay, scrub to history, enable prediction/velocity edit/branch target,
  press tilde, and confirm editor opens at current live time with replay state
  cleared.
- In replay velocity edit/nudge, hover or drag an axis, press launcher shortcut,
  and confirm nudge handles disappear, mouse capture releases, prediction state
  clears, physics runs live, and left click fires the launcher only.
- In replay prediction, switch to manipulator and confirm prediction UI/input is
  killed before manipulator drag begins.
- In editor gizmo drag, switch to replay and confirm editor capture, hover, and
  placement state are cleared before replay consumes input.
- In inspect gizmo mode, switch to launcher and confirm the transform gizmo no
  longer consumes clicks.
- Enter replay from editor and confirm editor placement/gizmo state is cleared.
- Camera combo and Tab never expose `Free`.
- Snapshot scenes that previously opened paused in `Free` now open paused in
  `Inspect`.

## Notes

- This plan intentionally treats workspace transition cleanup as the product
  surface. The controller is not just a naming cleanup; it is the guardrail that
  prevents replay, edit, inspect, launcher, and manipulator states from owning
  input at the same time.
- The first behavior-preserving slice should be small. The risky part is not
  adding enums; it is moving ownership of cancellation and transition side
  effects without changing replay determinism.

## Post-Implementation Feedback

- 2026-06-24: Launcher mode and Free/Inspect camera mode should not require a
  right-click hold to rotate the camera. Other modes should require right-click
  hold for camera rotation, and every mode should still expose right-click
  rotate support where camera rotation is valid.

# Replay Scrubber UI Cleanup Plan

## Problem

`Run::TickReplayScrubberInput()` is doing layout, hit testing, hover state,
visibility policy, mouse capture, drag lifecycle, and command execution in one
frame function. That is why simple UI questions turn into long boolean chains:

- Which pixels wake the replay bar?
- Which controls are hovered?
- Which controls may execute right now?
- Which click starts a drag instead of pressing a button?
- Which replay/runtime state transition should happen after the click?

Those are different responsibilities and should not be encoded as one giant
`if` condition.

## Goal

Make the replay scrubber behave like a normal UI surface:

- Every interactive element is a control with an id, geometry, enabled state,
  visibility state, and action.
- Every button or slider action is handled by a named handler.
- Hit testing, hover state, click dispatch, drag capture, and rendering all use
  the same control snapshot.
- `Run::TickReplayScrubberInput()` becomes integration glue, not the owner of
  every replay UI rule.

## Non-Goals

- Do not redesign the visual style.
- Do not change replay file formats, solver/presentation replay semantics, or
  prediction math.
- Do not introduce heap allocation, `std::function`, runtime polymorphism, or
  inheritance in per-frame UI dispatch.
- Do not move core replay state ownership out of `ReplayRuntime`.

## Best-Practice Rules

- One source of geometry truth: draw rectangles and hit rectangles must be built
  from the same layout snapshot.
- Controls own actions: a button should not be recognized by a scattered
  `overSaveButton && visibleUntil >= now` condition; it should be a control
  whose action dispatches to `HandleSaveReplay`.
- Disabled and hidden are different: disabled controls may be drawn or reveal a
  panel, but they must not execute.
- Gesture state is explicit: a slider drag or timeline scrub has one active
  control id, one pointer owner, and one release path.
- Z-order is table order: the first hovered enabled control in the control list
  wins, so overlapping controls stay deterministic.
- Runtime state changes stay in handlers: hit testing should never directly
  mutate replay state.
- Handlers return whether they consumed input.
- Keep the system stack-only and fixed-capacity.

## Proposed Shape

Add a replay scrubber UI surface layer under `SkullbonezSource/Runtime/Replay/`.
Suggested names:

- `ReplayScrubberSurface.h`
- `ReplayScrubberSurface.cpp`
- `ReplayScrubberActions.h` only if the action list becomes too large for the
  surface header

Core data types:

```cpp
enum class ReplayScrubberControlId
{
    TimelineTrack,
    SaveReplay,
    LoadReplay,
    BranchRestore,
    LiveAdvance,
    VelocityEdit,
    PredictionToggle,
    PredictionHorizon,
    RagdollVisuals
};

enum class ReplayScrubberAction
{
    None,
    BeginTimelineScrub,
    SaveReplay,
    LoadReplay,
    RestoreBranch,
    ToggleLiveAdvance,
    ToggleVelocityEdit,
    TogglePrediction,
    BeginPredictionHorizonDrag,
    ToggleRagdollVisuals
};

struct ReplayScrubberControl
{
    ReplayScrubberControlId id;
    ReplayScrubberAction action;
    UI::UIRect drawRect;
    UI::UIRect hitRect;
    bool visible;
    bool enabled;
    bool hovered;
    bool capturesPointer;
};
```

Use a fixed-capacity stack buffer, not a vector:

```cpp
struct ReplayScrubberSurface
{
    ReplayScrubberControl controls[16];
    std::size_t controlCount;
    ReplayScrubberControlId hotControl;
    ReplayScrubberControlId activeControl;
    bool hasHotControl;
    bool hasActiveControl;
    bool pointerRequestsReveal;
};
```

## Target Pipeline

`TickReplayScrubberInput()` should read like this:

```cpp
ReplayScrubberSurface surface;
BuildReplayScrubberSurface( surfaceInput, surface );

ApplyReplayScrubberHoverState( surface, mouse );
ApplyReplayScrubberRevealPolicy( surface, replayRuntime, now );

if ( restorePressed )
{
    return DispatchReplayScrubberAction( ReplayScrubberAction::RestoreBranch, context );
}

if ( leftPressed )
{
    const ReplayScrubberControl* control = FindPressedReplayScrubberControl( surface );
    if ( control )
    {
        return DispatchReplayScrubberAction( control->action, context );
    }
}

TickReplayScrubberActiveGesture( surface, context );
```

The exact function names can change, but this is the desired direction:
state is read once, controls are built once, input dispatch is table-driven, and
handlers perform the mutations.

## Handler Map

Each action gets one named handler. The first pass can keep these as private
helpers near `RunReplayScrubberTools.cpp`; after the surface layer is stable,
move pure handlers out of `Run`.

| Action | Handler | Notes |
| --- | --- | --- |
| `SaveReplay` | `HandleReplayScrubberSaveReplay()` | Calls `SaveReplayBufferFromScrubber()` and refreshes message/visibility. |
| `LoadReplay` | `HandleReplayScrubberLoadReplay()` | Owns the file picker path and status message update. |
| `RestoreBranch` | `HandleReplayScrubberBranchRestore()` | Handles Enter key and Branch button through the same path. |
| `ToggleLiveAdvance` | `HandleReplayScrubberLiveAdvanceToggle()` | Owns pause/play transitions and prediction freeze behavior. |
| `ToggleVelocityEdit` | `HandleReplayScrubberVelocityEditToggle()` | Owns interaction owner changes for velocity edit. |
| `TogglePrediction` | `HandleReplayScrubberPredictionToggle()` | Owns prediction enable/disable, future-position clamp, and dirty marking. |
| `BeginPredictionHorizonDrag` | `HandleReplayScrubberPredictionHorizonPress()` | Begins capture and delegates drag updates to the active gesture tick. |
| `ToggleRagdollVisuals` | `HandleReplayScrubberRagdollVisualToggle()` | Clears only the affected prediction visual cache. |
| `BeginTimelineScrub` | `HandleReplayScrubberTimelinePress()` | Begins timeline capture and delegates drag updates to the active gesture tick. |

Handlers should take a context struct instead of grabbing ad hoc locals:

```cpp
struct ReplayScrubberActionContext
{
    Run& run;
    ReplayRuntime& replay;
    HWND hwnd;
    POINT mouse;
    double now;
    bool loadedPresentation;
    bool solverToolsEnabled;
    RunReplayTrack activeTrack;
};
```

If `Run&` is too broad for long-term cleanup, split the context later into
small function pointers or narrow callbacks. Do that only after the table-driven
surface exists; do not invent a broad compatibility bridge first.

## Phases

### Phase 1: Add Control Vocabulary

- Add `ReplayScrubberControlId`, `ReplayScrubberAction`,
  `ReplayScrubberControl`, and `ReplayScrubberSurface`.
- Keep them local to replay scrubber code at first.
- Build the surface from the existing layout helpers in
  `ReplayOverlayLayout.cpp`.
- Preserve current behavior exactly.

Acceptance:

- `TickReplayScrubberInput()` still uses old handlers, but hover/reveal can be
  expressed from the control table.
- No dynamic allocation.
- No new inheritance.

### Phase 2: Table-Driven Hit Testing

- Replace `overSaveButton`, `overLoadButton`, `overPauseButton`, and similar
  booleans with `FindHotReplayScrubberControl()`.
- Make the prediction horizon hit area a real control hit rectangle, not a
  special boolean expression.
- Make timeline track hit testing a slider control, not a side condition.

Acceptance:

- Reveal policy reads from `surface.pointerRequestsReveal`.
- Hover state is assigned by control id.
- Track drag starts only when the track control is the pressed control.

### Phase 3: Action Dispatch

- Replace the click `else if` ladder with
  `DispatchReplayScrubberAction(action, context)`.
- Route keyboard Enter restore through the same `RestoreBranch` handler as the
  Branch button.
- Each handler returns `true` only when it consumed input.

Acceptance:

- There is one handler per button/slider action.
- The input tick no longer contains command bodies for save, load, prediction
  toggle, velocity toggle, pause/play, or branch restore.

### Phase 4: Explicit Gesture Ownership

- Add active gesture/control state for timeline scrub and prediction horizon
  drag.
- Keep pointer capture begin/end in one gesture helper.
- Remove duplicated capture release paths.

Acceptance:

- Press, drag, release for timeline and horizon use the same lifecycle shape.
- `mouseCaptured`, `dragging`, and `horizonDragging` cannot disagree after
  release or unavailable-surface reset.

### Phase 5: Renderer Consumes the Same Surface

- Update `ReplayOverlayRenderer.cpp` to consume the scrubber surface snapshot or
  a render snapshot derived from it.
- Stop recomputing button hover and draw availability independently in renderer
  and input code.

Acceptance:

- If a control is drawn as hoverable, its hit target matches.
- If a control is disabled, renderer and input agree.
- Layout math remains centralized in `ReplayOverlayLayout.cpp`.

### Phase 6: Shrink Run Integration

- Leave `Run` responsible for process integration: file picker, scene
  interaction transitions, and camera ownership.
- Move pure replay UI policy out of `Run`.
- Keep `ReplayRuntime` as replay state owner; do not move UI rectangles into it.

Acceptance:

- `Run::TickReplayScrubberInput()` is short enough to scan without folding.
- Replay UI rules live in replay UI files, not in the main runtime shell.

## Tests And Validation

For implementation, use targeted validation rather than broad gates while
iterating:

- Focused build: `tools\validate_build.bat Profile`
- Replay scrub gate: `tools\validate_replay_scrub.bat`
- If prediction replay behavior changes: `tools\validate_physics.bat`
- If the renderer consumes a new surface snapshot: add `tools\validate_dx12_renderer.bat`
  before PR-bound work

Documentation-only edits to this plan require no validation.

## Files To Touch

Likely implementation files:

- `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`
- New `SkullbonezSource/Runtime/Replay/ReplayScrubberSurface.h`
- New `SkullbonezSource/Runtime/Replay/ReplayScrubberSurface.cpp`
- Project files if new `.cpp` or `.h` files are added

## Cleanup Targets

Delete or absorb these patterns:

- Long reveal conditions mixing hover and replay state.
- Button-specific `overX` locals scattered through the main tick.
- Click `else if` ladders where each branch repeats `leftPressed`,
  `canTakeMouse`, and `visibleUntil >= now`.
- Separate renderer/input definitions of what a control is.
- Special-case drag start conditions that manually exclude individual buttons.

## Definition Of Done

- Every scrubber button and slider has a control id and action.
- Every action has one named handler.
- One control snapshot feeds hit testing, hover, reveal, dispatch, and rendering.
- Timeline scrub and prediction horizon use explicit active gesture state.
- `Run::TickReplayScrubberInput()` is mostly orchestration.
- No runtime heap growth, no `std::function`, no inheritance, and no broad
  compatibility object.
- Required touched-file comment audit is complete for source edits.
- Required implementation validation has passed and output is recorded.

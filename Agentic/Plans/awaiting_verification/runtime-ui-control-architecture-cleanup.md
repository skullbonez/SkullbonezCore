# Runtime UI Control Architecture Cleanup

## Problem

Runtime UI code is treating controls as scattered boolean expressions instead
of first-class UI objects. The replay scrubber exposed the smell, but the
problem is broader: any button, slider, overlay panel, hot zone, editor tool,
replay tool, or diagnostics control can drift into the same shape.

Bad shape:

- Layout computes one rectangle.
- Rendering recomputes or assumes another rectangle.
- Input code creates `overX` booleans for every button.
- Reveal, hover, disabled state, click, drag, and command execution are mixed
  into long `if` chains.
- Pointer capture and release are repeated per special case.
- A new button means editing multiple unrelated conditions.

That is not a UI architecture. It is a frame-sized accident waiting for the next
feature.

## Goal

Create a common runtime UI pattern for all in-engine UI surfaces:

- Every interactive thing is a control with an id, bounds, state, action, and
  handler.
- Buttons, sliders, toggles, tracks, hot zones, and panels all use the same
  vocabulary.
- Hit testing, hover, focus, active gesture, rendering, and dispatch all read
  from the same per-frame surface snapshot.
- UI code routes actions to named handlers instead of embedding behavior in
  boolean ladders.
- Per-frame UI remains fixed-capacity and allocation-free.

The first implementation slice should use the replay scrubber because it is the
current pain point, but the plan is for the runtime UI system as a whole.

## Scope

Applies to:

- Replay scrubber buttons, timeline track, prediction slider, and hot zone.
- Replay velocity edit, path visualizer, cause tree, prediction controls, and
  related overlay tools.
- Editor tool buttons, gizmo affordances, placement controls, and viewport tool
  controls.
- In-game UI panels, toggles, sliders, tabs, diagnostics controls, and future
  runtime overlays.
- Any new runtime UI that currently wants to add another `overX` boolean or
  click branch.

Does not mean:

- Re-skinning the UI.
- Introducing a retained-mode UI framework.
- Moving gameplay, replay, editor, or renderer ownership into UI widgets.
- Adding heap allocation, `std::function`, inheritance, virtual handlers, or
  callback chains to hot paths.

## Principles

- One source of geometry truth: draw bounds and hit bounds come from the same
  layout snapshot.
- One control owns one action: a button press dispatches a named action; it does
  not leak as `overButton && visible && canTakeMouse` across the caller.
- Hidden, disabled, hovered, focused, pressed, and actively dragging are
  distinct states.
- Z order is explicit and deterministic. The first eligible control in the
  configured order wins.
- Gestures have ownership. A slider drag or timeline scrub has one active
  control id, one pointer capture owner, and one release path.
- Handlers mutate runtime state; hit testing does not.
- Handlers return whether input was consumed.
- UI surfaces are rebuilt from current state every frame; persistent state stays
  with the owning subsystem.
- Keep data plain, small, fixed-capacity, and easy to inspect.

## Common Vocabulary

Introduce shared UI primitives under runtime/UI ownership. Exact paths can
change, but the concepts should be common:

- `RuntimeUiSurface`: A per-frame list of controls for one overlay/panel/tool.
- `RuntimeUiControl`: One interactive element with draw/hit bounds and state.
- `RuntimeUiAction`: Semantic operation requested by a control.
- `RuntimeUiGesture`: Active pointer operation such as dragging a slider.
- `RuntimeUiActionContext`: Narrow context passed to handlers.

Example shape:

```cpp
enum class RuntimeUiControlKind
{
    Panel,
    HotZone,
    Button,
    Toggle,
    Slider,
    Track,
    Tab,
    ToolHandle
};

enum class RuntimeUiAction
{
    None,
    Press,
    Toggle,
    BeginDrag,
    SetValue,
    Open,
    Close
};

struct RuntimeUiControl
{
    uint32_t id;
    RuntimeUiControlKind kind;
    RuntimeUiAction action;
    UI::UIRect drawRect;
    UI::UIRect hitRect;
    bool visible;
    bool enabled;
    bool hovered;
    bool focused;
    bool active;
    bool requestsReveal;
};
```

Surface storage should be fixed-capacity:

```cpp
template <std::size_t Capacity>
struct RuntimeUiSurface
{
    RuntimeUiControl controls[Capacity];
    std::size_t controlCount;
    uint32_t hotControl;
    uint32_t activeControl;
    bool hasHotControl;
    bool hasActiveControl;
    bool consumesPointer;
};
```

If templates are awkward for project style, use domain-specific structs with
fixed arrays. The important part is the model, not the exact generic form.

## Target Frame Pipeline

Each UI surface should follow the same pipeline:

1. Build layout from current screen size and owner state.
2. Add controls to a fixed-capacity surface.
3. Hit test once.
4. Apply hover/focus/active state.
5. Dispatch pressed control action to a named handler.
6. Tick active gesture if one exists.
7. Render from the same surface snapshot.

The orchestration should read like this:

```cpp
RuntimeUiSurface<32> surface;
BuildSurface( ownerState, layoutInput, surface );

HitTestSurface( surface, pointer );
ApplySurfaceRevealPolicy( surface, ownerState, now );

if ( pointer.leftPressed )
{
    const RuntimeUiControl* control = FindPressedControl( surface );
    if ( control )
    {
        consumed = DispatchAction( *control, actionContext );
    }
}

TickActiveGesture( surface, actionContext );
RenderSurface( surface, drawContext );
```

The names can change. The dependency direction should not: layout and hit
testing produce control state, dispatch calls handlers, handlers mutate owners.

## Handler Pattern

Handlers should be named after the semantic action, not the mouse condition.

Good:

- `HandleReplaySavePressed()`
- `HandlePredictionHorizonDragStarted()`
- `HandleEditorPlacementModeToggled()`
- `HandleDiagnosticsTabSelected()`
- `HandleWaterHeightSliderChanged()`

Bad:

- `if ( overSaveButton && leftPressed && visibleUntil >= now )`
- `if ( overPanel || overTrack || dragging || liveAdvanceHeld || ... )`
- `if ( !overButtonA && !overButtonB && !overSlider && inPanel )`

Handler rules:

- Take a small context struct.
- Return `true` when input is consumed.
- Own command-side effects such as save/load/toggle/restore.
- Do not perform hit testing.
- Do not directly recompute layout.
- Do not decide render styling.

## State Ownership

UI surface state is per-frame and disposable:

- bounds
- hover
- focused control
- current press target
- reveal request

Subsystem state remains with its owner:

- replay state in `ReplayRuntime`
- editor state in `RuntimeTools` or editor owner
- in-game UI state in `UI::InGameUI`
- interaction owner and pointer capture in `RuntimeInteractionController`
- camera/tool transitions in `Run`

The UI layer requests actions; it does not become a new global state bag.

## Required Cleanup Targets

Delete or absorb these patterns across runtime UI:

- `overX` locals for every button in a frame function.
- Long reveal conditions mixing pointer hover with subsystem state.
- Click `else if` ladders that repeat `leftPressed`, `canTakeMouse`, and
  visibility checks for every control.
- Separate draw and hit-test geometry for the same visual element.
- Special-case drag start rules that manually exclude every nearby button.
- Repeated mouse capture begin/end code per widget.
- Handlers that are really anonymous blocks inside input tick functions.

## Phases

### Phase 1: Inventory UI Surfaces

Create a checklist of runtime UI surfaces and their current ownership:

- Replay scrubber.
- Replay prediction, velocity edit, path visualizer, and cause tree.
- Editor placement/gizmo/tools.
- In-game UI diagnostics panels and tabs.
- Runtime overlays and hot zones.
- Any automation-only UI controls that need deterministic hit targets.

Acceptance:

- Plan checklist names every in-scope source-bearing UI file.
- Each surface has an owner, input entry point, render entry point, and
  validation path.

### Phase 2: Add Shared Control Vocabulary

Add the smallest common control/surface vocabulary needed by all runtime UI.
Keep it value-based and fixed-capacity.

Acceptance:

- No dynamic allocation.
- No new inheritance.
- No `std::function`.
- Controls can represent button, toggle, slider, track, tab, panel, and hot
  zone.

### Phase 3: First Vertical Slice

Convert replay scrubber first because it is already showing the failure mode.
Do not overfit the names to scrubber.

Acceptance:

- Replay scrubber controls are built into a surface.
- The old `overSaveButton`, `overLoadButton`, `overPauseButton`, and similar
  locals are gone or isolated inside surface construction.
- Reveal policy reads from control/surface state.
- The visible track and prediction horizon are real controls.

### Phase 4: Action Dispatch

Replace per-control click branches with action dispatch.

Acceptance:

- Every button/slider/toggle action has one named handler.
- Keyboard shortcuts that trigger the same command route through the same
  handler as pointer input.
- Input tick functions mostly orchestrate surface build, hit test, dispatch,
  gesture tick, and render handoff.

### Phase 5: Gesture Lifecycle

Introduce one active gesture model for sliders, tracks, drag handles, and other
pointer-capturing controls.

Acceptance:

- Begin, update, cancel, and release are centralized.
- Pointer capture cannot remain stuck after release, unavailable UI, scene load,
  or mode transition.
- Drag state does not require separate `dragging` booleans for every widget
  unless those booleans are compatibility mirrors during migration.

### Phase 6: Renderer And Input Share Snapshots

Make render code consume either the same surface snapshot or a render snapshot
derived directly from it.

Acceptance:

- Drawn hover equals hit-test hover.
- Disabled controls draw and behave consistently.
- Layout math is not duplicated between render and input paths.

### Phase 7: Apply To Other UI

Apply the same pattern to the remaining runtime UI surfaces in small slices:

- Replay secondary tools.
- Editor controls.
- In-game UI diagnostics.
- Runtime overlays and hot zones.

Acceptance:

- New controls are added by adding a control row and handler, not by editing a
  half-dozen unrelated conditions.
- Existing UI behavior is preserved unless the slice explicitly fixes a bug.

## Validation

Documentation-only changes to this plan require no validation.

Implementation slices should use the narrowest matching gate:

- Focused build: `tools\validate_build.bat Profile`
- Replay UI changes: `tools\validate_replay_scrub.bat`
- In-game UI or runtime input routing: `tools\validate_fast.bat`
- Renderer-visible UI changes: `tools\validate_dx12_renderer.bat`
- Editor/world interaction changes: relevant interaction automation plus
  `tools\validate_fast.bat`
- Broad or cross-surface changes: `tools\validate_full.bat`

Any source-bearing implementation slice must also run the touched-file comment
style audit required by `AGENTS.md`.

## Likely Files

Likely first-pass implementation areas:

- `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`
- `SkullbonezSource/Runtime/Replay/*`
- `SkullbonezSource/Runtime/Editor/*`
- `SkullbonezSource/Runtime/Tools/*`
- `SkullbonezSource/Runtime/RunInput.cpp`
- `SkullbonezSource/Runtime/RuntimeInteractionController.*`
- `SkullbonezSource/UI/*`

Potential new shared files:

- `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h`
- `SkullbonezSource/Runtime/UI/RuntimeUiSurface.cpp`
- domain-specific surface files only where they keep ownership clearer, such as
  `ReplayScrubberSurface.*`

## Definition Of Done

- Runtime UI has a common control/action/gesture vocabulary.
- Every interactive element in the converted surfaces has a control id.
- Every button, toggle, slider, track, tab, hot zone, or handle has a named
  action or intentionally no action.
- Every action has a named handler.
- Hit testing, hover, disabled state, active gesture, reveal, and rendering use
  the same surface snapshot.
- Input tick functions no longer contain large button-specific boolean ladders.
- Pointer capture lifecycle is centralized.
- Adding a new button or slider is a local control-table and handler change.
- No runtime heap growth, no inheritance, no broad compatibility bridge, and no
  callback chain in per-frame paths.
- Required source audits and validation are complete for implementation slices.

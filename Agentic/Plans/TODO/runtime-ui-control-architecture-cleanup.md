# Runtime UI Control Architecture Cleanup

Date: 2026-07-10 (promoted into the authoritative TODO inventory)
Status: In progress - 2/7 phases complete
Impact area: replay UI, editor UI, diagnostics UI, input routing, interaction
gesture ownership
Owner: runtime UI surfaces; subsystem commands remain with their domain owners

## Dependencies

- Coordinate gesture ownership with `interaction-state-machine.md`.
- Coordinate removal of `Run::*` UI/replay handlers with
  `runtime-shell-decomposition.md` and
  `replay-architecture-and-right-sizing.md`.
- Register CPU tests through `validation-gate-integrity.md` V1/V2.

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
- camera/tool transitions in `RuntimeInteractionController` and their owning
  camera/tool subsystem

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

| Phase | State | Completion evidence |
|---|---|---|
| U0 Inventory UI surfaces | Complete | 95 tracked source files reconciled below with owner/input/render/gate evidence |
| U1 Shared control vocabulary | Complete | Inline `RuntimeUiSurface` values and four Debug/Release CPU tests pass |
| U2 Replay scrubber vertical slice | Pending | Old scrubber boolean ladder deleted |
| U3 Action dispatch | Pending | Named handler table and shared shortcut path |
| U4 Gesture lifecycle | Pending | Central begin/update/cancel/release tests |
| U5 Shared render/input snapshots | Pending | Draw and hit-test geometry equality tests |
| U6 Remaining runtime surfaces | Pending | Inventory reconciled with zero unchecked files |

### Phase 1: Inventory UI Surfaces

Create a checklist of runtime UI surfaces and their current ownership:

- Replay scrubber.
- Replay prediction, velocity edit, path visualizer, and cause tree.
- Editor placement/gizmo/tools.
- In-game UI diagnostics panels and tabs.
- Runtime overlays and hot zones.
- Any automation-only UI controls that need deterministic hit targets.

Acceptance:

- [x] Plan checklist names every in-scope source-bearing UI file.
- [x] Each surface has an owner, input entry point, render entry point, and
  validation path.

### U0 tracked-file inventory (2026-07-11)

Inventory command:

```powershell
git ls-files SkullbonezSource/UI SkullbonezSource/Runtime/Replay `
  SkullbonezSource/Runtime/Editor SkullbonezSource/Runtime/Tools `
  SkullbonezSource/Runtime/Render SkullbonezSource/Runtime/Scene `
  SkullbonezSource/Runtime | Where-Object { $_ -match '\.(cpp|h|hpp|inl|hlsl)$' }
```

The command is deliberately broader than the checklist. A tracked file is in
scope when it owns, builds, hit-tests, dispatches, draws, or transports an
interactive runtime control. Replay recording/serialization, editor asset
recipes, non-interactive render passes, and scene logic are domain
implementations rather than UI surfaces and remain outside this plan. `U0`
means the file was inspected and classified; `U6` remains open until the file
either consumes the common surface vocabulary or is explicitly proven to be a
cohesive lower-level primitive/domain handler that should remain unchanged.

| U0 | U6 | Tracked source file(s) | Surface and persistent owner | Input entry | Render entry | Validation path |
|---|---|---|---|---|---|---|
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/ReplayInteractionController.cpp`, `SkullbonezSource/Runtime/Replay/ReplayInteractionController.h` | Replay scrub/velocity command application; `ReplayRuntime` | `ReplayRuntime::TickScrubberInput`, `TickVelocityEditInput` | state consumed by replay overlay/velocity render | `validate_replay_scrub`, interaction policy |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`, `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` | Scrubber/prediction/cause-tree geometry; disposable replay layout | replay ticks build layout | `RenderReplayScrubberOverlay` | `validate_replay_scrub`, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp`, `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h` | Replay text/control presentation; `ReplayRuntime` supplies state | no mutation; consumes built surface | `RenderReplayScrubberOverlay` | `validate_replay_scrub`, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`, `SkullbonezSource/Runtime/Replay/ReplayRuntime.h` | Replay workspace, scrubber, prediction, velocity, path, cause tree; `ReplayRuntime` | public replay tick/route methods | public replay render methods and render snapshots | replay scrub, policy, full |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/ReplayRuntimeOwnerViews.h` | Narrow replay input/render owner views; `ReplayRuntime` | input owner view | render owner view | replay scrub, build |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp` | Scrubber buttons, track, prediction slider, hot zone; `ReplayRuntime` | `TickScrubberInput` | layout passed to overlay renderer | replay scrub |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp` | Cause-tree panel rows, drag/resize/hot zones; `ReplayRuntime` | `TickCauseTreeInput` | replay overlay renderer | replay scrub, interaction policy |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/RunReplayQueryTools.cpp` | World-space replay target/path selection; `ReplayRuntime` | `RouteWorldPointer` | path/cause focus overlay | replay scrub, click scenarios |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp` | Velocity gizmo handles; `ReplayRuntime` plus interaction owner | `TickVelocityEditInput` | `RenderVelocityEditOverlay` | replay scrub, click scenarios |
| [x] | [ ] | `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` | Prediction/path visualizer controls and world overlays; `ReplayRuntime` | prediction actions routed by scrubber/world pointer | prediction/path/cause render methods | replay scrub, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h` | Editor overlay snapshot vocabulary; `RuntimeTools` | built after editor input | consumed by tool overlay pass | interaction clicks, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h` | Placement selection/preview state used by controls; `RuntimeTools` | editor placement commands | placement preview overlay | interaction clicks, full |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/EditorTools.cpp`, `SkullbonezSource/Runtime/Editor/EditorTools.h` | Editor mode, shortcuts, placement and gizmo policy; `RuntimeTools` | named editor action/shortcut helpers | state consumed by overlay builders | interaction policy, clicks |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp` | Editor pointer preview/selection and gesture preparation; `RuntimeTools` | `RefreshEditorPointerPreview`, `PrepareEditorPointerSelection`, `PrepareEditorGizmoGesture` | editor tracer/overlay state | interaction policy, clicks |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp` | Translate/rotate/scale handles; interaction controller owns gesture | gizmo hit/update helpers | tracer gizmo geometry | inspect/manipulator scenarios |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp` | Placement action handler; scene/physics owners perform mutation | placement command from editor UI | placement result/preview | interaction clicks, full |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.cpp` | Editor tool overlay construction; `RuntimeTools` | samples post-input tool state | `BuildEditorToolOverlayTrace` | interaction clicks, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp` | World-space editor/replay line and gizmo presentation; `RuntimeTools` | no command mutation | `RunEditorTracer::Render` | DX12 renderer, interaction clicks |
| [x] | [ ] | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`, `SkullbonezSource/Runtime/Tools/RuntimeTools.h` | Persistent editor/tool state and tool render view; `RuntimeTools` | tool/editor routing methods | `BuildRenderView` and tracer access | interaction policy, clicks, full |
| [x] | [ ] | `SkullbonezSource/Runtime/InputFrame.cpp`, `SkullbonezSource/Runtime/InputFrame.h` | Per-frame UI/input snapshot and typed command boundary; input owners | `BuildInputFrame` | post-input state handed to frame rendering | interaction policy, fast |
| [x] | [ ] | `SkullbonezSource/Runtime/InputFrameExecution.cpp` | UI command dispatch to domain owners; input executor | `ExecuteInputFrame` | no direct drawing | interaction policy, fast |
| [x] | [ ] | `SkullbonezSource/Runtime/InteractionAutomationController.cpp`, `SkullbonezSource/Runtime/InteractionAutomationController.h` | Deterministic pointer/control targets and assertions; automation owner | automation step injection | report/screenshot evidence | five click scenarios, replay scrub |
| [x] | [ ] | `SkullbonezSource/Runtime/RunUiTextPass.cpp` | Top-level in-game/replay/diagnostic UI composition; frame composition | consumes immutable frame/UI snapshots | UI text pass and replay overlay render | fast, replay scrub, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/RunFrame.cpp` | Frame ordering only; `Run` composes owners | sequences input before render | sequences UI/tool render passes | full |
| [x] | [ ] | `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h` | Value-only UI/tool render handoff; `RuntimeRenderer` consumes | no input mutation | render-service snapshot | build, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` | Shared disposable control/surface values; domain owners retain persistent state | `ResolvePointer` produces one ordered hot control | domain renderer reads the same control rows | interaction policy, all CPU tests |
| [x] | [ ] | `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp`, `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.h` | Scene-authored UI visibility/options; scene owner | scene load/options command | sampled by in-game UI frame | fast, full |
| [x] | [ ] | `SkullbonezSource/UI/UI.cpp`, `SkullbonezSource/UI/UI.h` | Window, chrome, tabs, capture, scroll, mini-palette; `UI::InGameUI` | `InGameUI::UpdateInput` | `InGameUI::Draw` | fast, interaction clicks, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIInput.cpp`, `SkullbonezSource/UI/UIInput.h` | Immutable device-to-UI pointer/key facts; `InGameUI` | UI input helpers | no direct drawing | interaction policy, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIFrameComposition.cpp`, `SkullbonezSource/UI/UIFrameComposition.h` | Mini-palette/frame composition; `InGameUI` | mini-palette interaction helpers | mini-palette draw composition | interaction clicks, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIEditorMiniPalette.cpp`, `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp` | Editor viewport palette controls; `InGameUI` | palette layout/hit policy | palette drawing | interaction clicks, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIWindowChrome.cpp`, `SkullbonezSource/UI/UIWindowChrome.h` | Window drag/resize/title buttons; `InGameUI` | chrome rectangles consumed by `UpdateInput` | title/chrome drawing | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UILayout.cpp`, `SkullbonezSource/UI/UILayout.h` | Shared deterministic rectangles; disposable layout | bounds builders | same bounds passed to draw | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIButton.cpp`, `SkullbonezSource/UI/UIButton.h` | Button primitive; parent surface owns action | `UIButton::HitTest` | `UIButton::Draw` | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UICheckBox.cpp`, `SkullbonezSource/UI/UICheckBox.h` | Toggle primitive; parent surface owns action | `UICheckBox::HitTest` | checkbox draw | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIComboBox.cpp`, `SkullbonezSource/UI/UIComboBox.h` | Combo/open-row primitive; owning tab retains selection | combo input helpers | combo draw | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIIconButton.cpp`, `SkullbonezSource/UI/UIIconButton.h` | Icon-button primitive; parent surface owns action | icon hit test | icon-button draw | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIScrollBar.cpp`, `SkullbonezSource/UI/UIScrollBar.h` | Scrollbar track/thumb primitive; `InGameUI` owns scroll | scrollbar hit/update | scrollbar draw | CPU gesture tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UISlider.cpp`, `SkullbonezSource/UI/UISlider.h` | Slider track/value primitive; owning tab owns value | slider hit/value helpers | slider draw | CPU gesture/geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UITabBar.cpp`, `SkullbonezSource/UI/UITabBar.h` | Tab selectors; `InGameUI` owns active tab | tab hit test | tab-bar draw | CPU UI geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIBackdropBlur.cpp`, `SkullbonezSource/UI/UIBackdropBlur.h` | Non-interactive UI backdrop resource/presentation; `InGameUI` | none | backdrop draw | DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UICache.cpp`, `SkullbonezSource/UI/UICache.h` | Disposable UI draw-cache key/state; `InGameUI` | invalidated by input/state change | cached draw-list publication | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UICommands.h` | Typed UI-to-domain command values; frame input result | emitted by controls/tabs | no drawing | CPU command tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIDraw.cpp`, `SkullbonezSource/UI/UIDraw.h` | Immediate draw context; renderer resource owner | none | base shape/text emission | DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIDrawList.cpp`, `SkullbonezSource/UI/UIDrawList.h` | Fixed UI draw command storage; `InGameUI` | none | draw-list build/replay | CPU capacity tests, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIDrawWidgets.cpp`, `SkullbonezSource/UI/UIDrawWidgets.h` | Stateless widget styling helpers | consumes control state only | widget drawing | DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UIState.h` | Plain UI rectangles/window state; `InGameUI` | bounds/capture state | consumed across draw paths | CPU geometry tests, fast |
| [x] | [ ] | `SkullbonezSource/UI/UIStyle.cpp`, `SkullbonezSource/UI/UIStyle.h` | Non-interactive palette/metrics | none | styling lookup | DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabCinematic.cpp`, `SkullbonezSource/UI/UITabCinematic.h` | Cinematic controls; `InGameUI` tab state | tab content click/slider update | tab draw | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabControls.cpp`, `SkullbonezSource/UI/UITabControls.h` | Camera/control settings; `InGameUI` tab state | tab content click/slider update | tab draw | fast |
| [x] | [ ] | `SkullbonezSource/UI/UITabEditor.cpp`, `SkullbonezSource/UI/UITabEditor.h` | Editor mode/placement controls; `InGameUI` tab state | tab content click | tab draw | interaction clicks, fast |
| [x] | [ ] | `SkullbonezSource/UI/UITabMemory.cpp`, `SkullbonezSource/UI/UITabMemory.h` | Memory diagnostics controls/overlay; `InGameUI` | tab/overlay input | memory panel/overlay draw | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabOptions.cpp`, `SkullbonezSource/UI/UITabOptions.h` | Render/runtime toggles/sliders; `InGameUI` tab state | tab content click/slider update | tab draw | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabPhysics.cpp`, `SkullbonezSource/UI/UITabPhysics.h` | Physics diagnostics/toggles/sliders; `InGameUI` tab state | tab content click/slider update | tab draw | fast, physics |
| [x] | [ ] | `SkullbonezSource/UI/UITabProfiler.cpp`, `SkullbonezSource/UI/UITabProfiler.h` | Profiler tree/timeline/histogram controls; `InGameUI` | tab/histogram input | tab and overlay draw | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabScene.cpp`, `SkullbonezSource/UI/UITabScene.h` | Scene selector/filter/time controls; `InGameUI` | tab/filter/combo/slider input | tab draw | fast, full |
| [x] | [ ] | `SkullbonezSource/UI/UITabSky.cpp`, `SkullbonezSource/UI/UITabSky.h` | Sky/environment controls; `InGameUI` tab state | tab content click/slider update | tab draw | fast, DX12 renderer |
| [x] | [ ] | `SkullbonezSource/UI/UITabSound.cpp`, `SkullbonezSource/UI/UITabSound.h` | Audio diagnostics/toggles/sliders; `InGameUI` tab state | tab content click/slider update | tab draw | fast |

Reconciliation: 60/60 tracked source files under `SkullbonezSource/UI` are
named above. The 36 additional runtime files are the complete interactive
replay/editor/input/render boundary selected by the stated rule, for 96/96 U0
files checked and zero deferred. The deterministic automation targets are
`attach_target_click.json`, `inspect_gizmo_click.json`,
`launcher_fire_click.json`, `manipulator_pickup_click.json`,
`memory_overlay_f6_toggle.json`, `prediction_determinism_probe.json`,
`prediction_ragdoll_wall_200_predict.json`,
`replay_branch_restore_live_edge.json`, `replay_prediction_click.json`, and
`replay_prediction_simple_verify.json` under `SkullbonezData/interaction/`.
They are validation fixtures, not source-bearing checklist rows.

### Phase 2: Add Shared Control Vocabulary

Add the smallest common control/surface vocabulary needed by all runtime UI.
Keep it value-based and fixed-capacity.

Acceptance:

- [x] No dynamic allocation.
- [x] No new inheritance.
- [x] No `std::function`.
- [x] Controls can represent button, toggle, slider, track, tab, panel, and hot
  zone.

Evidence:

- `RuntimeUiSurface.h` stores controls inline, rejects zero/duplicate ids and
  capacity overflow, and resolves the first visible/enabled hit in authored
  front-to-back order without callbacks or sorting.
- The vocabulary also includes `ToolHandle`, typed control/action ids, distinct
  visible/enabled/hovered/focused/active/reveal state, and disposable hot/active
  surface state. Pointer capture remains in `RuntimeInteractionController`.
- Four CPU tests cover every required kind, bounded storage and identity,
  hidden/disabled/z-order hit behavior, and frame reset.
- `tools\validate_all_cpu_tests.bat` passed in 17.3s on 2026-07-11: 131/131
  doctests with 2,814 assertions, 22/22 interaction-policy cases in both Debug
  and Release, scene-parser tests, and DX12 architecture tests; zero build
  warnings/errors.
- Comment audit: 2/2 touched source-bearing files checked, zero deferred.

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
- CPU control/gesture behavior: `tools\validate_all_cpu_tests.bat` after
  `validation-gate-integrity.md` V1 lands; until then run
  `tools\validate_runtime_interaction_policy.bat` explicitly.
- Replay UI changes: `tools\validate_replay_scrub.bat`
- In-game UI or runtime input routing: `tools\validate_fast.bat`
- Renderer-visible UI changes: `tools\validate_dx12_renderer.bat`
- Editor/world interaction changes: `tools\validate_interaction_clicks.bat`,
  relevant interaction automation, and the CPU interaction-policy tests.
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

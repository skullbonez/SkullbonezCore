# UI Code Cleanup Plan

Created: 2026-06-09

## Purpose

The in-game UI now works across GL, DX11, and DX12, but the implementation has
grown into a large mixed-responsibility module. This plan describes how to clean
it up into maintainable pieces without losing the current UI behavior, renderer
parity, validation coverage, or hot-path performance.

This is a cleanup/refactor plan, not a visual redesign plan. The existing
`Agentic/Plans/in-game-ui-window-plan.md` remains the historical feature plan
for the first UI implementation. This document is the follow-up plan for making
the code base easier to maintain.

## Impact Area

Primary impact:

- UI code under `SkullbonezSource/UI/`.

Possible impact during later phases:

- Runtime input and command application in `SkullbonezSource/SkullbonezRun.*`.
- Win32 cursor/capture plumbing in `SkullbonezSource/SkullbonezWindow.*`.
- Renderer-facing UI draw, blur, scissor, screenshot, or shader paths.
- UI scenes and optional UI validation tools.

Documentation-only updates require no validation. Once implementation starts,
validation requirements depend on the files touched; see the validation section
near the end of this plan.

## Current State Summary

The UI module is useful but tangled:

- `SkullbonezSource/UI/SkullbonezUI.cpp` is roughly 2,800 lines and combines
  layout constants, string formatting, profiler tree logic, scene filtering,
  hit testing, Win32 capture calls, mouse polling, widget state, command
  creation, drawing, animation, scroll logic, and footer controls.
- `SkullbonezSource/UI/SkullbonezUI.h` exposes one large `InGameUI` class with
  many unrelated members: tab state, window placement, combo state, sliders,
  scene filter typing state, profiler expansion state, performance histogram
  history, animation state, blur invalidation, and input capture flags.
- `InGameUIFrameData` is the right general idea: the UI receives a frame
  snapshot and should not mutate engine state directly. It is currently broad,
  but it gives a good boundary to preserve.
- `InGameUIInputResult` is also the right general idea: UI input emits commands
  for `SkullbonezRun` to apply. It has become a flat bag of unrelated commands
  and should be grouped by area.
- Smaller widgets already exist:
  - `UIButton`
  - `UICheckBox`
  - `UIComboBox`
  - `UIIconButton`
  - `UIScrollBar`
  - `UISlider`
  - `UITabBar`
  - `UIBackdropBlur`
  - `UIDrawContext`
- Existing UI validation exists and should be preserved:
  - `tools/validate_ui_stress.bat`
  - `tools/validate_ui.bat`
  - `tools/check_ui_blur.py`
  - `SkullbonezData/scenes/ui_tests.suite`
  - `SkullbonezData/scenes/ui_stress.scene`

The main problem is not that the UI lacks small widgets. The problem is that
the top-level UI object owns all behaviors directly instead of coordinating
specialized pieces.

## Cleanup Goals

1. Preserve current behavior and screenshots during early refactors.
2. Make `InGameUI` a coordinator/facade instead of a monolithic state bag.
3. Separate input, window chrome, layout, tab content, command output, drawing,
   and renderer-adjacent effects.
4. Keep engine mutation outside the UI. The UI should emit typed commands and
   `SkullbonezRun` should apply them.
5. Keep Win32 details out of tabs and widgets.
6. Keep per-frame UI work allocation-free or allocation-bounded.
7. Keep renderer parity stable across GL, DX11, and DX12.
8. Make future UI additions local: a new control should not require editing a
   giant `Draw()`/`UpdateInput()` switch in a 2,800-line file.
9. Make tests and validation tell us when layout, blur, clipping, capture,
   screenshot output, or renderer behavior changes.

## Non-Goals

- Do not replace the engine UI with ImGui or another immediate-mode library.
- Do not redesign the visual style in the first cleanup pass.
- Do not rewrite blur or cached compositing until the core UI responsibilities
  are separated.
- Do not move engine state mutation into UI tab files.
- Do not broaden this into a general engine architecture refactor.

## Target Architecture

### Top-Level Facade

Keep a public `InGameUI` facade so `SkullbonezRun` does not need to know about
all internal UI modules.

Suggested responsibilities:

- Own the durable UI state.
- Accept `InGameUIFrameData`.
- Accept host input or a lightweight input snapshot.
- Dispatch input to the active window/tab/widgets.
- Produce `InGameUICommands`.
- Call tab drawing/building code.
- Forward resource reset/invalidation to renderer-adjacent UI helpers.

`InGameUI` should eventually be small enough to read in one sitting.

### State Types

Split persistent state into focused structs:

- `UIWindowState`
  - visible/minimized/maximized
  - bounds, restore bounds, minimized width
  - drag/resize state
  - animation state
  - scroll state
  - active tab

- `UIInteractionState`
  - mouse position
  - hover/capture target
  - active slider/control id
  - pressed/held state
  - keyboard focus target
  - camera/key blocking flags

- `UISceneTabState`
  - scene combo open state
  - filter text
  - filter key debounce
  - combo scroll

- `UIProfilerTabState`
  - expanded marker hashes
  - expand-all mode
  - default expansion applied flag
  - timeline toggle
  - performance histogram samples

- `UIRuntimeControlState`
  - preview values while sliders are dragged
  - selected combo values while interaction is in progress

Keep these in headers only where needed. Prefer private implementation headers
inside `SkullbonezSource/UI/` rather than expanding the public UI surface.

### Input Boundary

Create a UI input layer that hides platform details.

Suggested files:

- `UIInput.h`
- `UIInput.cpp`

Suggested types:

- `UIMouseButtons`
- `UIMouseState`
- `UIKeyboardState`
- `UIInputSnapshot`
- `UICaptureRequest`
- `UIInputController`

The UI should eventually consume a snapshot like:

```cpp
struct UIInputSnapshot
{
    int mouseX = 0;
    int mouseY = 0;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    int wheelDelta = 0;
    bool keyboardFocusActive = false;
    bool keyDown[256] = {};
    bool keyPressed[256] = {};
};
```

The first implementation does not have to create a perfect retained event
queue. The important cleanup is to stop tab code and draw code from reaching
directly into `HWND`, `GetKeyState`, `SetCapture`, or `ReleaseCapture`.

Host-specific capture and cursor decisions can remain in `InGameUI` or a small
`UIHostBridge` until `SkullbonezWindow` is ready for deeper changes.

### Command Boundary

Replace the flat `InGameUIInputResult` shape with grouped commands while keeping
the public return type stable during transition.

Possible command grouping:

```cpp
struct UIRendererCommands
{
    int requestedRendererIndex = -1;
    int requestedWaterReflectionMode = -1;
    bool toggleVsync = false;
};

struct UISceneCommands
{
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool requestDemoScene = false;
    bool saveSceneDefaults = false;
    int requestedSceneIndex = -1;
    int requestedModelCount = -1;
    int requestedSeed = -1;
    float requestedTimeScale = -1.0f;
};

struct UIPhysicsCommands
{
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsSleepPolicy = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleBroadphaseOverlay = false;
    uint32_t togglePhysicsDebugFlags = 0;
    bool stepPhysicsPipelinePrevious = false;
    bool stepPhysicsPipelineNext = false;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
};
```

`SkullbonezRun` should continue to be the only place that applies these
commands to engine state.

### Layout And Style

Create layout/style modules so geometry and colors stop being scattered through
the draw function.

Suggested files:

- `UIStyle.h`
- `UIStyle.cpp`
- `UILayout.h`
- `UILayout.cpp`

Suggested responsibilities:

- Theme colors.
- Font sizes.
- Panel, title bar, footer, tab, scrollbar, and resize handle dimensions.
- Common spacing constants.
- Helpers for footer combo bounds, footer toggle bounds, content rects, tab
  content bounds, title button bounds, minimized rects, max window bounds.
- Pixel snapping policy.

Keep the layout helpers simple and deterministic. This UI is a diagnostics tool,
not a general auto-layout framework.

### Window Chrome

Create a window/chrome module for frame behavior.

Suggested files:

- `UIWindowChrome.h`
- `UIWindowChrome.cpp`

Responsibilities:

- title bar hit testing
- minimize/maximize/restore/close button hit testing
- drag and resize initiation
- resize clamping
- minimize/maximize animation
- title text fitting
- frame outline/title/footer shell drawing

This module should not know how the profiler or physics tab works.

### Tab Modules

Split each tab into its own module. Each tab should have three responsibilities:

1. Measure content height.
2. Handle input for controls inside its content rect.
3. Draw content.

Suggested files:

- `UITabProfiler.h/.cpp`
- `UITabScene.h/.cpp`
- `UITabPhysics.h/.cpp`
- `UITabOptions.h/.cpp`
- `UITabControls.h/.cpp`

Suggested tab interface:

```cpp
struct UITabContext
{
    const InGameUIFrameData& frame;
    const UIStyle& style;
    const UIRect contentRect;
    float scrollY = 0.0f;
    int mouseX = 0;
    int mouseY = 0;
};

struct UITabInputContext
{
    const InGameUIFrameData& frame;
    const UIInputSnapshot& input;
    UIRect contentRect;
    float scrollY = 0.0f;
};
```

Virtual dispatch is optional. For this code base, explicit tab classes owned by
`InGameUI` are likely clearer and cheaper than a heap-allocated polymorphic
tab registry.

### Draw Helpers

Move repeated draw patterns into reusable helpers.

Suggested files:

- `UIDrawPrimitives.h/.cpp`
- `UIDrawWidgets.h/.cpp`

Candidate helpers:

- title button
- pipeline step button
- footer toggle
- section title
- label/value row
- stat cell
- compact stat cell
- profiler row
- histogram panel
- content toggle row

This can happen before introducing a full retained `UIDrawList`. It is a low
risk way to shrink `SkullbonezUI.cpp`.

### Retained Draw List

After responsibility cleanup, move toward a retained draw-list path.

Suggested files:

- `UIDrawList.h`
- `UIDrawList.cpp`
- `UICache.h`
- `UICache.cpp`

Long-term target:

- Tabs and window chrome append commands to a draw list.
- The draw list flushes centrally.
- Static UI can skip rebuilding.
- Window movement can remain a composite-position change where possible.
- Text and quads are tracked in separate or unified batches depending on what
  the renderer path supports.

Do not start with this phase. It is easier and safer after input, tabs, layout,
and draw helpers are already separated.

### Blur And Renderer-Adjacent Code

Keep `UIBackdropBlur` separate and treat it as renderer-adjacent.

Cleanup goals:

- Make invalidation reasons explicit.
- Avoid hiding resource lifecycle changes behind unrelated UI state changes.
- Keep blur sampling/copy/update costs visible in profiler markers.
- Avoid per-frame allocations when the window is static.
- Keep GL/DX11/DX12 behavior equivalent.

Any substantial blur change moves the work from "UI refactor" into
"renderer/shader/hot-path validation" territory.

## Proposed File Layout

Target shape after cleanup:

```text
SkullbonezSource/UI/
  SkullbonezUI.h
  SkullbonezUI.cpp              # facade/coordinator only
  UICommands.h
  UIState.h
  UIInput.h
  UIInput.cpp
  UIStyle.h
  UIStyle.cpp
  UILayout.h
  UILayout.cpp
  UIWindowChrome.h
  UIWindowChrome.cpp
  UIDraw.h
  UIDraw.cpp
  UIDrawWidgets.h
  UIDrawWidgets.cpp
  UITabProfiler.h
  UITabProfiler.cpp
  UITabScene.h
  UITabScene.cpp
  UITabPhysics.h
  UITabPhysics.cpp
  UITabOptions.h
  UITabOptions.cpp
  UITabControls.h
  UITabControls.cpp
  UIButton.*
  UICheckBox.*
  UIComboBox.*
  UIIconButton.*
  UIScrollBar.*
  UISlider.*
  UITabBar.*
  UIBackdropBlur.*
```

Avoid creating every file in one commit. Add files as phases need them.

## Implementation Phases

### Phase 0: Baseline And Audit

Goal: capture current behavior before moving code.

Tasks:

- Run `tools/validate_ui_stress.bat`.
- Run `tools/validate_ui.bat`.
- Confirm `dx12_validation.txt` reports zero validation errors through the
  validation script.
- Keep the generated UI BMP/PNG artifacts available for screenshot comparison.
- Record current UI draw-call counts and `Frame/UI` marker values from
  `ui_*_profiler_timeline_perf.csv`.
- Read current `SkullbonezUI.cpp` and mark extraction boundaries by function:
  state setters, profiler helpers, scene filter helpers, input handling, draw
  helpers, tab drawing, footer drawing.

Expected result:

- No code changes yet.
- Clear baseline.
- Known list of functions to move in later phases.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`

### Phase 1: Extract Constants, Style, And Layout

Goal: shrink the monolith without changing behavior.

Tasks:

- Create `UIStyle.h/.cpp` for colors, font sizes, and theme values.
- Create `UILayout.h/.cpp` for layout constants and pure geometry helpers.
- Move helpers such as footer bounds, minimized rect, scene combo width,
  title-button bounds, and content rect computation.
- Keep existing numeric values unchanged.
- Keep `InGameUI::Draw()` behavior unchanged except for calling helpers.

Success criteria:

- No screenshot-visible layout change.
- `SkullbonezUI.cpp` loses a meaningful block of constants and pure helpers.
- No new dependency on engine runtime state inside layout/style helpers.

Validation:

- Minimum: `tools/validate_ui_stress.bat`.
- Preferred after this phase: `tools/validate_ui.bat`, because layout helpers
  can affect screenshots.

### Phase 2: Extract Draw Widgets

Goal: make drawing code composable while staying immediate/batched.

Tasks:

- Create `UIDrawWidgets.h/.cpp`.
- Move repeated draw helpers:
  - `DrawTitleButton`
  - `DrawPipelineStepButton`
  - footer toggle rendering
  - section titles
  - label/value rows
  - stat cells
  - content toggle rows
  - compact footer stat variants
- Keep `UIDrawContext` as the drawing backend for now.
- Keep `Text2d::FlushQuads()` and `Text2d::FlushText()` sequencing unchanged.

Success criteria:

- Draw helpers are stateless or take explicit state.
- No helper reads `InGameUI` private members directly.
- `InGameUI::Draw()` becomes visibly easier to scan.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`

### Phase 3: Extract Window State And Chrome

Goal: isolate visible/minimized/maximized/drag/resize/animation behavior.

Tasks:

- Create `UIState.h` with `UIWindowState` and possibly `UIInteractionState`.
- Create `UIWindowChrome.h/.cpp`.
- Move:
  - default placement
  - minimized rect logic
  - maximize/restore bounds
  - title fitting
  - animation begin/current shell helpers
  - drag/resize hit testing
  - chrome drawing
- Preserve public `InGameUI` methods:
  - `SetVisible`
  - `ToggleVisible`
  - `SetMinimized`
  - `ToggleMaximizeMinimize`
  - `SetWindowBounds`

Success criteria:

- `InGameUI` owns a `UIWindowState` instead of many individual window fields.
- Drag/resize/minimize behavior remains unchanged.
- No tab-specific code in `UIWindowChrome`.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`

### Phase 4: Extract Input Controller

Goal: isolate mouse/key state handling and platform capture decisions.

Tasks:

- Create `UIInput.h/.cpp`.
- Add `UIInputSnapshot` and helper functions for button edges, wheel delta,
  and key edge detection.
- Move scene filter key debounce into the scene tab state or input controller.
- Keep actual `SetCapture` and `ReleaseCapture` calls in one place.
- Make `BlocksCameraMouse()`, `BlocksKeyboard()`, and
  `WantsNativeMouseCursor()` derive from explicit interaction state.
- Preserve camera behavior:
  - UI hover/capture blocks camera mouse.
  - UI text/filter focus blocks keyboard.
  - Hidden/minimized UI does not unexpectedly consume controls.

Success criteria:

- Tab code receives input state instead of polling keys directly.
- Platform-specific calls are concentrated in one small section.
- Existing UI stress scene still covers click/drag/scroll/combo interactions.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- If `SkullbonezWindow.*` or broader runtime input is touched, use
  `tools/validate_full.bat`.

### Phase 5: Group UI Commands

Goal: make command output readable and safer to extend.

Tasks:

- Create `UICommands.h`.
- Group renderer, scene, physics, water, and UI-only commands.
- Keep a compatibility layer if `SkullbonezRun` still expects the old flat
  `InGameUIInputResult` initially.
- Update `SkullbonezRun` application code in small sections.
- Keep edge-triggered commands distinct from continuous slider values.
- Use explicit sentinel values or `hasX` booleans consistently.

Success criteria:

- The UI command type makes it obvious which subsystem will be affected.
- Adding a new physics toggle no longer requires searching through a giant flat
  result struct.
- Runtime state mutation remains outside UI modules.

Validation:

- If only UI headers/cpp are touched: `tools/validate_ui_stress.bat` and
  `tools/validate_ui.bat`.
- If `SkullbonezRun.*` is touched: `tools/validate_full.bat`.

### Phase 6: Extract Scene Tab

Goal: make the first tab module and prove the pattern.

Tasks:

- Create `UITabScene.h/.cpp`.
- Move scene filtering helpers:
  - scene filter matching
  - filtered count
  - filtered index lookup
  - selected filtered position
  - combo scroll clamp
- Move scene tab input handling:
  - scene combo click/open/close
  - filter typing
  - reset/default/save buttons
  - requested scene index/demo scene command
- Move scene tab drawing.
- Keep all scene filtering deterministic and allocation-free.

Success criteria:

- Scene tab code can be reviewed without reading profiler, physics, footer, or
  window chrome logic.
- Scene combo screenshots remain identical.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`

### Phase 7: Extract Profiler Tab

Goal: separate profiler-specific tree, timeline, and histogram logic.

Tasks:

- Create `UITabProfiler.h/.cpp`.
- Move:
  - profiler marker child detection
  - visible row building
  - timeline segment building
  - marker expansion state
  - default expansion
  - expand all
  - timeline toggle handling
  - performance histogram sampling/drawing
- Keep profiler data read-only from the UI.
- Preserve required `Frame/UI`, `Frame/UI/Quads`, and `Frame/UI/Text` markers
  expected by `check_ui_blur.py`.

Success criteria:

- Profiler tab can evolve independently.
- Timeline numeric checks still pass.
- No profiler-specific code remains in the main UI facade except tab dispatch.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- Inspect profiler timeline CSV output if marker names or timing scopes change.

### Phase 8: Extract Physics, Options, And Controls Tabs

Goal: finish tab separation after the pattern is proven.

Tasks:

- Create `UITabPhysics.h/.cpp`.
- Create `UITabOptions.h/.cpp`.
- Create `UITabControls.h/.cpp`.
- Move each tab's content height, input handling, slider preview state, and
  drawing.
- Keep physics debug flag commands grouped.
- Keep slider preview values local to the tab/control state.
- Keep model-count limits and solver-count cross limits explicit.

Success criteria:

- Each tab has local state and emits grouped commands.
- `ContentHeight()` is delegated to the active tab.
- `InGameUI::Draw()` no longer contains a large tab switch.
- `InGameUI::UpdateInput()` no longer contains a large tab switch.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- If physics runtime behavior is touched outside command plumbing, also run
  `tools/validate_physics.bat`.

### Phase 9: Simplify The Facade

Goal: make `InGameUI` small and explicit.

Tasks:

- Review `SkullbonezUI.h` and remove private members that moved into state or
  tab modules.
- Keep public methods stable unless changing `SkullbonezRun` is clearly worth
  it.
- Remove transitional compatibility helpers.
- Make invariants obvious:
  - one active tab
  - one active capture target
  - one active slider/control
  - UI commands generated once per frame
  - drawing only reads frame data and UI state

Success criteria:

- `SkullbonezUI.cpp` is a coordinator, not a renderer/input/controller blob.
- `SkullbonezUI.h` no longer exposes internal implementation details through
  a huge private member list.
- Future tab changes should not touch window chrome or input plumbing.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- `tools/validate_full.bat` if runtime input/application code changed.

### Phase 10: Retained Draw And Cache Prep

Goal: prepare for cached UI rendering after structural cleanup.

Tasks:

- Add a lightweight `UIDrawList`.
- Convert draw helpers to append commands before flushing.
- Track dirty flags:
  - content dirty
  - layout dirty
  - style dirty
  - interaction dirty
  - viewport dirty
  - blur/source dirty
- Avoid rebuilding tab draw data when only window position changes.
- Add or preserve profiler markers for:
  - `Frame/UI/Input`
  - `Frame/UI/Layout`
  - `Frame/UI/DrawBuild`
  - `Frame/UI/Quads`
  - `Frame/UI/Text`
  - `Frame/UI/Blur`

Success criteria:

- Draw-list conversion does not change screenshots.
- No per-frame heap churn in static UI.
- UI draw and rebuild costs are measurable.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- `tools/validate_perf.bat`

### Phase 11: Optional Blur/Renderer Cleanup

Goal: clean renderer-adjacent UI effects only after core UI architecture is
stable.

Tasks:

- Make blur resource lifetime explicit.
- Audit texture allocations and source capture/update paths.
- Consider a renderer-level scene-color/blur path only if current blur cost or
  quality is a real problem.
- Keep backend transitions and descriptor usage explicit for DX12.
- Keep GL/DX11 state restoration explicit after UI passes.

Success criteria:

- Blur remains visually validated.
- DX12 InfoQueue remains clean.
- Renderer parity remains acceptable.
- UI cache/blur work is measurable in perf output.

Validation:

- `tools/validate_renderers.bat`
- `tools/validate_ui.bat`
- `tools/validate_perf.bat`
- If `SkullbonezRun*` or broad runtime flow is touched, also
  `tools/validate_full.bat`.

## Refactor Rules

- Keep each commit/phase behavior-identical unless the commit message explicitly
  says it changes UI behavior.
- Prefer moving code first, then improving it in a later phase.
- Preserve function names during extraction when doing so helps review.
- Avoid large formatting-only churn mixed with logic changes.
- Do not introduce dynamic allocation in per-frame UI paths.
- Do not introduce new renderer differences between GL, DX11, and DX12.
- Keep all UI text fitting and clipping behavior validated by screenshots.
- Keep current UI scenes passing while adding coverage incrementally.
- Do not remove the existing UI stress scene until a stronger replacement
  exists.

## Risk Areas

### Input Capture And Camera Control

Risk:

- Dragging, scrolling, or typing in the UI can accidentally rotate the camera or
  trigger debug hotkeys.

Mitigation:

- Keep capture state explicit.
- Validate hover/capture/minimized/hidden behavior.
- Preserve `BlocksCameraMouse()`, `BlocksKeyboard()`, and
  `WantsNativeMouseCursor()` semantics during extraction.

### Screenshot And Clipping Regressions

Risk:

- Small layout changes can leak scrolled content, move UI outside bounds, or
  break expected screenshots.

Mitigation:

- Run `tools/validate_ui.bat`.
- Keep `check_ui_blur.py` containment and clipping checks.
- Do layout extraction before any visual changes.

### Renderer Parity

Risk:

- Draw order, flushing, blur, scissor, or render target changes can diverge
  across GL, DX11, and DX12.

Mitigation:

- Avoid renderer changes during early cleanup.
- If renderer code changes, run renderer validation.
- Watch DX12 validation output specifically.

### Hidden Heap Churn

Risk:

- New tab classes or draw lists may allocate every frame.

Mitigation:

- Use fixed arrays where the existing code uses fixed caps.
- Reserve vectors up front if vectors are introduced.
- Measure with UI perf markers and `tools/validate_perf.bat` for hot-path
  phases.

### Command Semantics

Risk:

- Grouping commands can change edge-triggered behavior or slider update timing.

Mitigation:

- Keep compatibility conversion during transition.
- Apply commands in the same order in `SkullbonezRun`.
- Validate stress scenes with repeated interactions.

## Suggested Milestones

### Milestone 1: Monolith Shrink With No Behavior Change

Includes:

- Phase 1 constants/style/layout extraction.
- Phase 2 draw widgets extraction.
- Phase 3 window chrome extraction.

Expected value:

- Big readability improvement.
- Low behavioral risk.
- No renderer changes.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`

### Milestone 2: Safer Interaction Model

Includes:

- Phase 4 input controller.
- Phase 5 grouped commands.

Expected value:

- Less risk when adding controls.
- Cleaner `SkullbonezRun` boundary.
- Platform details no longer spread through UI behavior.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- `tools/validate_full.bat` if runtime/window code changes.

### Milestone 3: Tab Separation

Includes:

- Phase 6 scene tab.
- Phase 7 profiler tab.
- Phase 8 physics/options/controls tabs.

Expected value:

- New UI features become local.
- Easier code review and focused testing.
- Main UI class becomes understandable.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- Additional domain validation if non-UI behavior changes.

### Milestone 4: Facade And Cache Prep

Includes:

- Phase 9 facade cleanup.
- Phase 10 retained draw/cache prep.

Expected value:

- Clear architecture.
- Better performance instrumentation.
- Prepared for one-draw/cached UI improvements.

Validation:

- `tools/validate_ui_stress.bat`
- `tools/validate_ui.bat`
- `tools/validate_perf.bat`

### Milestone 5: Renderer-Adjacent UI Improvements

Includes:

- Phase 11 optional blur/renderer cleanup.

Expected value:

- Better blur/cache quality and performance if needed.
- Clearer renderer ownership.

Validation:

- `tools/validate_renderers.bat`
- `tools/validate_ui.bat`
- `tools/validate_perf.bat`
- `tools/validate_full.bat` when broad runtime paths are touched.

## Validation Matrix

Use repository validation scripts. Do not claim success without command output.

| Change | Required Validation |
|--------|---------------------|
| Documentation-only plan edits | No validation required |
| UI style/layout extraction only | `tools/validate_ui_stress.bat`, preferably `tools/validate_ui.bat` |
| UI draw helper extraction | `tools/validate_ui_stress.bat` and `tools/validate_ui.bat` |
| UI input/capture changes only | `tools/validate_ui_stress.bat` and `tools/validate_ui.bat` |
| Changes touching `SkullbonezRun*` or `SkullbonezWindow*` | `tools/validate_full.bat` |
| UI renderer/shader/blur/cache changes | `tools/validate_renderers.bat`, `tools/validate_ui.bat`, and `tools/validate_perf.bat` |
| Physics tab changes that alter physics runtime behavior | `tools/validate_physics.bat` plus UI validation |
| Broad or uncertain scope | `tools/validate_full.bat` |

## Review Checklist For Each Phase

- Does this phase preserve current UI behavior unless explicitly intended?
- Are changed responsibilities now more local than before?
- Did any tab gain direct engine mutation logic?
- Did any tab gain direct Win32/platform calls?
- Did any per-frame path allocate?
- Did draw order or flush order change?
- Are all renderer-sensitive changes validated across GL, DX11, and DX12?
- Is `SkullbonezRun` still the command application boundary?
- Are screenshots and UI stress scenes still representative?
- Is the phase small enough to debug if validation fails?

## First Concrete Task

Start with Phase 1:

1. Run `tools/validate_ui_stress.bat`.
2. Run `tools/validate_ui.bat`.
3. Add `UIStyle.h/.cpp` and `UILayout.h/.cpp`.
4. Move only constants and pure layout helpers.
5. Keep all numeric values and drawing behavior unchanged.
6. Run `tools/validate_ui_stress.bat`.
7. Run `tools/validate_ui.bat`.

This gives a safe first win and makes the later extraction work easier to
review.

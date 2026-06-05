# In-Game UI Plan

## Goal

Replace the current `0` key overlay cycle with a slick in-game UI window that can be opened, moved, resized, tabbed, scrolled, and driven by the mouse while staying fast enough for the renderer testbed.

The target UX:

- One semi-transparent window over the game.
- Tabs instead of cycling through hidden overlay screens.
- Mouse hit testing, dragging, resizing, tab selection, scroll wheel, and focus.
- Auto-fit sizing based on the active tab contents.
- Auto-hiding scrollbars when the content is larger than the window.
- Cached rendering that only invalidates on real UI changes.
- One draw call for the final cached UI composite when possible.
- Optional frosted/gaussian background blur, implemented honestly as a staged renderer feature because live background blur requires per-frame scene sampling.

## Current Source Read

Relevant current code paths:

- `SkullbonezSource/SkullbonezRun.h`
  - `OverlayMode` is a simple enum: `None`, `Timers`, `SceneStats`, `BarsNormalized`, `BarsAbsolute`, `Keys`.
  - `RunDebugState::overlayMode` stores the active overlay.

- `SkullbonezSource/SkullbonezRun.cpp`
  - `TakeInput()` edge-detects the `0` key and cycles `OverlayMode`.
  - `DrawWindowText()` draws the current overlay directly with immediate-style calls.
  - Overlay content is mixed into runtime code: top text, scene telemetry, profiler bars, keyboard reference, and profiler table.
  - Text/HUD drawing happens after `Render()` and before `Present()`.

- `SkullbonezSource/SkullbonezProfiler.cpp`
  - `Profiler::RenderOverlay()` and `Profiler::RenderBarOverlay()` render their own panels directly.
  - The bar overlay already batches quads and text well, but the profiler still owns UI layout/rendering instead of exposing UI data.

- `SkullbonezSource/SkullbonezText.h/.cpp`
  - SDF text atlas already exists.
  - Text is batched into one draw call per flush.
  - Colored quads can be batched into one draw call with `BatchQuad()` and `FlushQuads()`.
  - Current text and quads are separate batches, so a normal panel is at least two draws.
  - `Render2dQuad()` is immediate and creates one draw per call, so new UI should avoid it.

- `SkullbonezSource/SkullbonezInput.h/.cpp`
  - Input mostly polls `GetKeyState()`.
  - Mouse helpers only get/set global cursor coordinates and center the cursor.
  - There is no retained mouse event queue, no button state, no wheel state, and no UI focus/capture model.

- `SkullbonezSource/SkullbonezWindow.cpp`
  - `WndProc` handles resize, paint, escape key, and destroy.
  - It does not currently handle `WM_MOUSEMOVE`, button down/up, wheel, capture, cursor visibility, or UI input dispatch.
  - Fly/nudge mode hides or changes the cursor and recenters it, so UI input must coordinate with camera input.

- `SkullbonezSource/SkullbonezIRenderBackend.h`
  - Existing backend API supports viewport, blend, depth state, framebuffers, textures, and dynamic VBs.
  - There is no explicit scissor/clip-rect API exposed even though DX12 already tracks a scissor rect internally.
  - There is no generic "update texture" or "render cached UI texture" abstraction yet.

- `SkullbonezSource/SkullbonezFramebuffer*.h/.cpp`
  - Offscreen render targets already exist for GL, DX11, and DX12.
  - These can become the basis of a cached UI surface and, later, scene-color/blur buffers.

## Design Direction

Build an engine-native retained UI, not another immediate HUD screen.

The key idea is to separate UI into four layers:

1. Input/events
2. Widget/layout state
3. Cached draw data
4. Renderer composite

The UI should not rebuild vertices every frame unless input, content, size, tab, theme, or viewport state changes.

## Proposed Types

Add a small UI subsystem:

- `SkullbonezUi.h/.cpp`
  - `UiManager`
  - `UiWindow`
  - `UiTab`
  - `UiContext`
  - `UiStyle`
  - `UiLayoutResult`

- `SkullbonezUiInput.h/.cpp`
  - `UiInputEvent`
  - `UiMouseState`
  - Event queue from `WndProc`
  - Hit testing and capture/focus state

- `SkullbonezUiDraw.h/.cpp`
  - `UiDrawList`
  - `UiDrawCache`
  - Unified quads/glyphs vertex format
  - Dirty flags and cache generation

- Optional later:
  - `SkullbonezUiBlur.h/.cpp`
  - Renderer helpers for scene-color copy/downsample/blur/composite

Keep the first implementation compact. One movable diagnostics window is enough; avoid building a huge general-purpose framework before the first useful screen is working.

## Window Model

The main UI window should have:

- Title bar with drag region.
- Tab strip: `Overview`, `Profiler`, `Scene`, `Physics`, `Renderer`, `Keys`.
- Content area with per-tab layout.
- Resize handle on the lower-right corner.
- Optional auto-fit button or double-click title behavior.
- Saved position, size, selected tab, and collapsed/visible state.

Use pixel-space layout internally. Convert to the existing text/ortho coordinate space only at render time. Pixel-space makes hit testing, scrollbars, text measurements, resize constraints, and persisted positions much cleaner.

Suggested default:

- Size: 520 x 360 pixels, clamped to the current client rect.
- Min size: 320 x 180 pixels.
- Max size: client rect minus margins.
- Default position: 32 px from top-left.
- Visuals: dark glass panel, thin bright border, subtle top highlight, 6-8 px radius if implemented through shader masking.

## Tabs

Move current overlay screens into tabs:

- `Overview`
  - Renderer name
  - Scene name/frame
  - Model count
  - Physics solver
  - FPS/CPU/render/physics summary

- `Profiler`
  - CPU/GPU timing table.
  - Bar view toggle: normalized vs absolute.
  - Refactor profiler rendering so `Profiler` exposes rows/data and the UI owns layout/rendering.

- `Scene`
  - Scene telemetry, energy, current scene path, frame, test status.

- `Physics`
  - Solver mode, debug flags, transparent body alpha, contact linger.
  - Eventually mouse-driven toggles can replace some numeric hotkeys.

- `Renderer`
  - Active backend, vsync/pipeline sync state, water reflection mode, water/terrain visibility.

- `Keys`
  - Keep the current keyboard reference but make it scrollable.

The old `0` key can become "toggle UI visible" rather than "cycle modes". A second shortcut, such as Ctrl+Tab or mouse tabs, can switch tabs. Existing debug hotkeys should keep working while the UI is hidden.

## Mouse Input Plan

Extend `WndProc` to collect UI-relevant events:

- `WM_MOUSEMOVE`
- `WM_LBUTTONDOWN`, `WM_LBUTTONUP`
- `WM_RBUTTONDOWN`, `WM_RBUTTONUP`
- `WM_MOUSEWHEEL`
- `WM_SETCURSOR`
- `WM_CAPTURECHANGED`

Store client-space mouse coordinates and event deltas in an input queue. Dispatch queued events to `UiManager` once per frame before game controls are applied.

Capture rules:

- If UI is visible and the mouse is over the window, UI gets hover.
- If the user presses on a draggable/resizable/scrollable UI region, UI captures until button up.
- While UI captures, camera fly-look and nudge mouse movement should not consume mouse deltas.
- When UI is visible, show the arrow cursor.
- When UI is hidden and fly/nudge mode owns the mouse, preserve the current cursor behavior.

This prevents the classic failure where dragging a UI window also rotates the camera.

## Layout And Resize

Each tab provider should implement:

- `Measure(UiMeasureContext&) -> UiSize`
- `Build(UiBuildContext&, UiRect contentRect)`

The window computes:

- Header height.
- Tab strip height.
- Content viewport size.
- Natural content size for active tab.
- Whether vertical/horizontal scrollbars are needed.

Auto-fit behavior:

- If the user has not manually resized the window, changing tabs can resize to the active tab's natural size, clamped to the screen.
- If the user has manually resized, preserve their size until they choose auto-fit.
- Double-clicking the title bar or pressing an auto-fit icon can resize to content.

Scroll behavior:

- Scrollbars appear only when content exceeds viewport size.
- Scrollbar thumbs fade out after a short idle delay.
- Scroll wheel scrolls the hovered content area.
- Dragging the thumb captures input.
- Always reserve hit testing for invisible scrollbars for a short grace period after interaction so they do not vanish mid-drag.

## Rendering Plan

### Phase 1: Fast Transparent UI Without Blur

Use a retained `UiDrawList` that produces one unified vertex stream for:

- Solid rectangles
- Borders
- Lines
- Text glyphs
- Optional icon glyphs or simple triangles

Use one UI shader and one texture atlas:

- SDF font atlas for text.
- A white atlas texel for solid shapes.
- Per-vertex color and UV.
- Optional per-vertex flags for SDF text vs solid.

This can reduce the whole UI cache render to one draw when rebuilding. If the UI is cached into an offscreen texture, the normal frame path becomes:

1. Draw 3D scene.
2. Composite cached UI texture over the scene with one textured quad.
3. Present.

When the UI is static, do not rebuild the draw list, do not upload UI vertices, and do not rerender the UI cache.

Dirty flags:

- `ContentDirty`: profiler rows, scene stats, key list, tab data changed.
- `LayoutDirty`: tab changed, window resized, content changed natural size.
- `StyleDirty`: theme/color/font changed.
- `PositionDirty`: window moved; if cached in window-local texture, only composite position changes.
- `ViewportDirty`: swap-chain/window resize.
- `InteractionDirty`: hover/pressed/scrollbar fade changed.

Important cache split:

- Cache the window contents in local window coordinates.
- Keep the screen position as a composite parameter.
- Moving the window should not require rebuilding the UI texture.
- Resizing or scrolling does require rebuilding the UI cache.

### Phase 2: Frosted/Gaussian Background

True blur behind the panel cannot be fully cached while the 3D scene moves. The background pixels under the window change every frame, so a real gaussian/frosted effect needs per-frame scene sampling.

Implement blur as an optional renderer feature:

1. Render the main scene to a scene-color texture instead of directly to the swap chain, or copy the backbuffer to a texture where supported.
2. Downsample only when a frosted UI window is visible.
3. Run separable gaussian blur or a cheaper Kawase blur on the relevant region.
4. Composite the cached UI window in one final shader that samples:
   - blurred scene texture for the glass area,
   - original scene texture outside/behind,
   - cached UI alpha/color texture for borders, text, controls, and tint.

Performance knobs:

- `ui_blur_enabled`
- `ui_blur_radius`
- `ui_blur_downsample`
- `ui_blur_quality`

Fallback:

- If blur is disabled or unsupported on a backend, use the same semi-transparent glass tint without blur.

Expected draw-call truth:

- Non-blur mode can hit one final UI composite draw per frame.
- Blur mode will need extra GPU passes. It can still keep the actual UI content cached, but the blur itself is per-frame work.

## Renderer API Changes

Likely backend additions:

- `SetScissorRect(x, y, w, h)` and `SetScissorEnabled(bool)`.
- `CreateOrResizeFramebuffer(width, height, format, flags)` or enough helpers to avoid reallocating UI surfaces every frame.
- `UpdateTexture2D(handle, data, x, y, w, h)` if CPU-side cache upload is chosen.
- `DrawTexturedQuad(textureHandle, dstRect, uvRect, color)` or a general UI composite path.
- Optional scene-color access for blur/composite.

DX12 care points:

- Resource transitions for scene color, blur targets, and UI cache textures.
- Descriptor heap pressure for extra UI/blur textures.
- No sampling from a render target in the same state it is being written.

GL/DX11 care points:

- Restore viewport/scissor state after UI cache rendering.
- Avoid feedback loops when sampling the current backbuffer.

## Performance Rules

No per-frame heap churn in the UI hot path.

Use:

- Fixed-capacity vectors or retained vectors with reserved capacity.
- Dirty-flag rebuilds.
- Stable string buffers for common labels.
- Cached text measurements.
- Window-local cache textures.
- Existing profiler timings to track UI update, UI cache rebuild, blur, and composite separately.

Avoid:

- Rebuilding all tabs every frame.
- Formatting every label every frame when values have not changed.
- Recreating framebuffers or dynamic VBs during resize drags.
- Calling immediate `Render2dQuad()` repeatedly for widgets.

Target:

- Static UI, no blur: 1 composite draw per frame.
- Static UI with blur: cached UI composite plus blur passes.
- Dirty UI frame: bounded rebuild cost, preferably one UI cache draw using a unified shader.
- Scroll/resize/drag: still allocation-free, with measured cost.

## Implementation Phases

### Phase A: Event Plumbing And Toggle

- Add mouse event capture in `WndProc`.
- Add an input queue or mouse state bridge.
- Add `UiManager` lifetime to `SkullbonezRun`.
- Change `0` key to toggle UI visibility.
- Show cursor when UI is open.
- Prevent fly/nudge mouse-look while UI has capture.

Validation when code starts:

- `tools\validate_full.bat` because this touches `SkullbonezRun*` and `SkullbonezWindow*`.

### Phase B: Retained Window, Tabs, And Transparent Rendering

- Add one movable window with a title bar and tab strip.
- Implement hit testing for tabs, drag, and close/collapse if desired.
- Move current `SceneStats`, `Keys`, and `Timers` content into tabs.
- Use existing `Text2d` batching first, but prefer `BatchQuad()` over `Render2dQuad()`.

Validation:

- `tools\validate_full.bat`.
- Manual launch with each renderer to check mouse behavior.

### Phase C: Unified UI Cache

- Add a unified UI vertex format and shader.
- Render UI into an offscreen cache texture only when dirty.
- Composite the cache over the scene every frame.
- Keep movement as a composite-only transform so dragging does not rebuild contents.
- Add profiler markers: `Frame/UI/Input`, `Frame/UI/Rebuild`, `Frame/UI/Composite`.

Validation:

- `tools\validate_renderers.bat` for backend/shader parity.
- `tools\validate_perf.bat` because this is hot-path/performance-sensitive.

### Phase D: Resize, Auto-Fit, And Scrollbars

- Add content measurement per tab.
- Add resize handle and constraints.
- Add auto-fit behavior.
- Add scroll state, wheel handling, auto-hide scrollbars, and scrollbar dragging.
- Add clipping via scissor or shader-side clip rects.

Validation:

- `tools\validate_full.bat`.
- `tools\validate_perf.bat` if scroll/resize rebuilds show up in frame timings.

### Phase E: Frosted Blur

- Add scene-color texture path or safe backbuffer copy path.
- Add downsample and blur shaders for GL, DX11, and DX12.
- Composite blurred background and cached UI.
- Add fallback to non-blur transparent mode.

Validation:

- `tools\validate_renderers.bat`.
- Verify DX12 validation output is zero.
- Run renderer validation more than once if resource barriers or upload buffers are touched.
- `tools\validate_perf.bat`.

### Phase F: Polish And Persistence

- Persist window position, size, selected tab, blur enabled, and opacity in config.
- Add keyboard navigation later if useful.
- Add theme constants in one place.
- Update `Agentic/Reference/runtime-reference.md` key bindings after behavior changes.

Validation:

- Documentation updates alone need no validation.
- Config/runtime behavior changes should use the validation required by the touched code files.

## Open Decisions

- Should `0` open the UI to the last tab, or always open to `Overview`?
- Should the UI pause camera mouse-look whenever visible, or only when hovered/captured?
- Should blur be enabled by default, or default to transparent glass until renderer cost is proven?
- Should UI state persist in `engine.cfg`, a new UI config file, or runtime scene/session state?
- Should we keep old overlay rendering behind a temporary flag until the new UI is stable?

## Recommended First Milestone

Start with a non-blurred, semi-transparent, movable tabbed window.

That first milestone should deliver the big UX win without destabilizing all three renderers. It also creates the right seams for blur and one-draw cached composition later.

Concrete first milestone scope:

- `0` toggles UI visibility.
- Mouse can select tabs and drag the window.
- Window shows `Overview`, `Profiler`, `Scene`, and `Keys`.
- UI is semi-transparent and polished.
- Existing hotkeys still work when the UI is hidden.
- UI input does not fight fly/nudge camera input.
- No per-frame allocations in the UI path.

Once that feels good, move to cached compositing and then frosted blur.

# Graphite Overlay UI Implementation Plan

## Goal

Restyle the in-game diagnostics UI from neon sci-fi chrome to the selected graphite overlay direction:

- matte translucent graphite surfaces
- 8px window corners and 6px control corners
- off-white text with muted grey secondary labels
- quiet sage accent for active states, sliders, and enabled switches
- no cyan glow halos, no hard neon outlines

Impact area: UI rendering and diagnostics UI styling. This should avoid physics, renderer backend, shader, scene, and baseline changes unless validation shows a renderer-specific issue.

## Design Artifacts

- `graphite-overlay-mockup.html`: focused visual target.
- `graphite-overlay-mockup.png`: rendered preview.
- `design-tokens.json`: color, radius, alpha, and sizing values for C++ translation.

## Validation

Current artifact-only work: no validation required.

Implementation validation: defer repository validation until the pre-commit/PR
gate, then run `tools\validate_renderers.bat`.

Reason: the likely implementation adds rounded drawing helpers in `UIDraw.*` and changes the renderer-neutral UI draw path. Even though this is not a backend feature, the result is rendered output that should remain stable across GL, DX11, and DX12.

If the final implementation avoids `UIDraw.*` and changes only local color constants, `tools\validate_fast.bat` may be enough, but the recommended path below uses shared rounded primitives.

## Current Code Shape

The UI is immediate-mode and mostly drawn through:

| File | Role |
|------|------|
| `SkullbonezSource/UI/UIDraw.h/.cpp` | Basic draw API: rect, triangle, outline, text. |
| `SkullbonezSource/UI/UIDrawList.h/.cpp` | Queued UI draw commands for animated/offscreen flush paths. |
| `SkullbonezSource/UI/UIStyle.h/.cpp` | Small existing style holder, currently only cyan accent and footer toggle style. |
| `SkullbonezSource/UI/UIWindowChrome.cpp` | Window shell, title bar, minimized state, title buttons. |
| `SkullbonezSource/UI/UIButton.cpp` | Rectangular push buttons. |
| `SkullbonezSource/UI/UICheckBox.cpp` | Toggle row visual. |
| `SkullbonezSource/UI/UIComboBox.cpp` | Combo field and dropdown. |
| `SkullbonezSource/UI/UISlider.cpp` | Sliders and slider hit testing. |
| `SkullbonezSource/UI/UIDrawWidgets.cpp` | Shared section titles, footer toggles, stat cells, pipeline buttons. |
| `SkullbonezSource/UI/UITab*.cpp` | Tab content that should keep layout and behavior mostly unchanged. |

## Phase 1: Centralize The Graphite Style

Add a richer style layer in `UIStyle.h/.cpp` before changing call sites.

Recommended structs:

- `UIColor`
- `UIPalette`
- `UIRadii`
- `UITextStyle`
- `UIControlStyle`
- `UIWindowStyle`

Keep backward-compatible accessors where helpful, for example leave `AccentCyan()` temporarily but return the graphite accent or replace call sites gradually with `Accent()`.

Style values to include:

- window fill: `0.153, 0.157, 0.153, 0.86`
- raised fill: `0.200, 0.204, 0.192`
- control fill: `0.220, 0.231, 0.216`
- primary text: `0.953, 0.953, 0.941`
- secondary text: `0.725, 0.737, 0.722`
- muted text: `0.561, 0.580, 0.561`
- accent: `0.604, 0.647, 0.561`
- border alpha: `0.12`
- inner border alpha: `0.08`
- window radius: `8.0f`
- control radius: `6.0f`

Keep the first pass fixed-size and token-driven. Do not redesign layout or hit boxes in this phase.

## Phase 2: Add Rounded Drawing Helpers

Add rounded fill helpers to `UIDrawContext`.

Suggested API:

```cpp
void RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a ) const;
void RoundedPanel( const UIRect& bounds, float radius, const UIColor& fill, const UIColor& border ) const;
```

Implementation notes:

- Build `RoundedRect` from existing `Rect` and `Triangle` calls so `UIDrawList` can queue the generated primitives without a new command type.
- Clamp radius to half the smaller dimension.
- Use a small fixed corner segment count, such as 4 or 5, to keep command count cheap.
- Implement `RoundedPanel` as an outer rounded rect in border color, then an inset rounded rect in fill color. This avoids the current square-corner `Outline()` for primary surfaces.
- Keep existing `Outline()` for hairline dividers and non-rounded fallback details.
- Review `UIDrawList::MAX_COMMANDS`; rounded windows and controls increase command count. If needed, raise the limit modestly and watch `UIDrawList::Stats`.

## Phase 3: Restyle Window Chrome

Update `UIWindowChrome.cpp`.

Targets:

- `DrawWindowFrame`
- `DrawWindowAnimationShell`
- `DrawMinimizedWindow`
- `DrawTitleButtons`
- `DrawTitleButton` in `UIDrawWidgets.cpp`

Changes:

- Replace cyan outer glow rectangles with one soft dark shadow and a low-alpha border.
- Use `RoundedPanel` for full window, minimized window, title buttons, and animation shell.
- Use graphite title bar fill instead of teal title fill.
- Use off-white title text and muted button icons.
- Keep title button hit boxes exactly as they are.
- Active/maximized state should use raised graphite, not cyan.
- Close hover may use a restrained warm warning accent, not red neon.

Acceptance:

- Window still reads over dark and bright scenes.
- Minimize/maximize/close buttons remain easy to hit.
- No cyan remains in default chrome.

## Phase 4: Restyle Core Controls

Update controls while preserving behavior and hit boxes.

Files:

- `UIButton.cpp`
- `UICheckBox.cpp`
- `UIComboBox.cpp`
- `UISlider.cpp`
- `UIIconButton.cpp`
- `UIDrawWidgets.cpp`

Changes:

- Buttons: graphite fill, 6px corners, soft border, off-white label.
- Toggles: pill switch, sage enabled fill, neutral grey disabled fill, circular knob.
- Combo fields: graphite field fill, muted border, off-white selected text, simple chevron.
- Dropdowns: solid graphite backer, active/hover rows with raised graphite or sage tint.
- Sliders: 6px rounded track, muted track background, sage fill, smaller calm knob.
- Section titles: off-white or secondary grey; remove gold/yellow headings unless a warning state needs it.
- Footer stats: low-contrast graphite chips and off-white values.
- Pipeline step buttons: rounded square buttons with muted icon triangles.

Acceptance:

- Text contrast stays readable at current font sizes.
- Sliders and toggles retain current hit behavior.
- Combo dropdown still flushes and overlays earlier text correctly.

## Phase 5: Tab And Footer Pass

Update the places where tabs and footer rows are drawn in `SkullbonezUI.cpp` and related helpers.

Changes:

- Active tab gets raised graphite fill and subtle inset border.
- Inactive tabs are text-only or very low-alpha fill.
- Tab separator line uses soft white at low alpha.
- Footer controls use the same graphite token set.
- Remove remaining hard cyan separator bars.

Acceptance:

- Current tab can be identified immediately.
- Inactive tabs do not look disabled.
- Footer remains compact and scan-friendly.

## Phase 6: Verification And Polish

Recommended manual checks before validation:

1. Launch the app with the cinematic scene and hold mode.
2. Check the UI over bright sky, terrain, and water.
3. Switch through Scene, Physics, Cine, Profiler, and Options.
4. Open combo boxes and ensure dropdown surfaces cover earlier text.
5. Drag sliders and confirm hit boxes still align.
6. Minimize, restore, maximize, and resize the UI window.
7. Check profiler histogram readability if enabled.

Before committing PR-bound implementation work, run:

```bat
tools\validate_renderers.bat
```

If `UIDrawList` command count is increased, watch the UI draw stats for command overflow during tab-heavy screens.

## Implementation Order

1. Add style tokens to `UIStyle`.
2. Add `RoundedRect` and `RoundedPanel` helpers.
3. Convert window chrome only.
4. Run a quick local launch to inspect the frame.
5. Convert buttons, toggles, combo boxes, and sliders.
6. Convert shared footer/stat/pipeline widgets.
7. Convert tab strip and remaining cyan separators.
8. Before the PR-bound commit, run `tools\validate_renderers.bat`.
9. Capture before/after screenshots for handoff if the validation output is clean.

## Non-Goals For First Pass

- Do not change tab layout or add new UI workflows.
- Do not refactor tab ownership.
- Do not change physics, renderer backend, shader, scene, or baseline files.
- Do not introduce fonts or external assets.
- Do not add heavy blur effects in the C++ renderer; use opacity and layering first.

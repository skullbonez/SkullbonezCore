# Graphite Overlay UI

This folder collects the design artifacts for the calmer graphite direction.

## Files

| File | Purpose |
|------|---------|
| `graphite-overlay-mockup.html` | Focused browser mockup of the selected graphite overlay direction. |
| `graphite-overlay-mockup.png` | Rendered preview of the focused mockup. |
| `comparison-board.html` | Original three-direction board used to compare graphite, warm grey, and light studio directions. |
| `comparison-board.png` | Desktop render of the comparison board. |
| `comparison-board-mobile.png` | Narrow viewport render of the comparison board. |
| `design-tokens.json` | Palette, alpha, radius, and sizing values to translate into `UIStyle`. |
| `implementation-plan.md` | Step-by-step plan for implementing the graphite UI in the C++ immediate-mode UI. |

## Current Scope

This is a design and planning package only. It does not change runtime code.

Validation now: none required for documentation/design artifacts.

Validation when implemented: use `tools\validate_renderers.bat` because the plan adds rounded drawing helpers and changes rendered UI output across the shared draw path.

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
| (git history) `new-ui-graphite-overlay-implementation-plan.md` | Completed implementation plan for the graphite UI; the plan file was retired in the 2026-07-09 plan consolidation. |

## Current Scope

This folder now keeps the design artifacts for the completed graphite UI pass.

Validation now: none required for documentation/design artifacts.

Pre-commit/PR validation when implemented: use `tools\validate_dx12_renderer.bat` because the plan adds rounded drawing helpers and changes rendered UI output across the shared draw path.

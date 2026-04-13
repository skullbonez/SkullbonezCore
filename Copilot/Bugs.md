# SkullbonezCore — Known Bugs

| # | Bug | Area | Status |
|---|-----|------|--------|
| 1 | Water on the horizon flickers during camera movement | Rendering / Water | ✅ Fixed (`768a567`) |
| 2 | Lighting issues: water is too blue; terrain and skybox are too dark compared to original FFP output | Rendering / Lighting | ✅ Fixed |

## Details

### Bug 1: Flickering water on the horizon
**Fixed** (`768a567`) — replaced horizon quads with a vertex-animated grid water mesh.

### Bug 2: Water colour / scene lighting
**Fixed** — water colour and scene lighting now match the original FFP output.

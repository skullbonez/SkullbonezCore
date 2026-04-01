# SkullbonezCore — Known Bugs

| # | Bug | Area | Status |
|---|-----|------|--------|
| 1 | Water on the horizon flickers during camera movement | Rendering / Water | 🐛 Open |
| 2 | Lighting issues: water is too blue; terrain and skybox are too dark compared to original FFP output | Rendering / Lighting | 🐛 Open |

## Details

### Bug 1: Flickering water on the horizon
The deep water quads (translated ±5000–6300 units) flicker when the camera moves. Likely a z-fighting issue between the water mesh and the far clip plane, or precision loss in the modelview matrix read-back used for the water shader view uniform.

### Bug 2: Water colour / scene lighting
The water appears too blue/dark compared to the original FFP rendering. The terrain and skybox also appear darker than before the shader migration. Root cause is likely the lighting model difference between the FFP fixed pipeline (which applied ambient+diffuse automatically) and the current Gouraud shader. The `uColorTint` workaround partially addresses water brightness but does not fully match the original appearance.

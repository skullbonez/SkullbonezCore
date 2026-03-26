# Ground Shadows for Spheres — Plan

## Problem

Spheres bouncing in the scene have no visual grounding — it's hard to tell how high a ball
is above the terrain. Adding a dark shadow projected onto the terrain beneath each ball
gives depth cues and makes the simulation much more readable.

## Approach — Projected Shadow Decals

Render a semi-transparent dark disc on the terrain surface beneath each sphere. The disc
is drawn inline each frame as a `GL_TRIANGLE_FAN` with a baked alpha gradient (dark center,
transparent edges). Per frame, each shadow is positioned at the terrain surface, oriented
to match the terrain slope via `glRotatef`, and alpha-faded based on height above ground.

**Why this approach?**
- Engine uses fixed-function OpenGL (no shaders, no FBOs, no shadow maps)
- Terrain height and normal are queryable via `Terrain::GetTerrainHeightAt` and `GetTerrainNormalAt`
- Very cheap per-ball: one height query + one normal query + one small triangle fan
- Looks good for the use case (many balls on hilly terrain)

## Architecture

### Modified files
| File | Change |
|------|--------|
| `SkullbonezCommon.h` | Add shadow constants |
| `SkullbonezTerrain.h` | Add `GetTerrainNormalAt` declaration |
| `SkullbonezTerrain.cpp` | Implement `GetTerrainNormalAt` using `LocatePolygon` + cross product |
| `SkullbonezGameModelCollection.h` | Add `RenderShadows` method declaration, terrain include |
| `SkullbonezGameModelCollection.cpp` | Implement `RenderShadows` with terrain-aligned disc rendering |
| `SkullbonezRun.cpp` | Call `RenderShadows` in `DrawPrimitives` after terrain render |

### Shadow rendering algorithm
```
For each sphere:
  1. Get ball position (x, y, z) and radius
  2. Check terrain bounds — skip if ball is off-terrain
  3. Get terrain height at (x, z)
  4. Compute height above ground: ball.y - terrainHeight - radius
  5. If height > SHADOW_MAX_HEIGHT, skip (shadow fully faded)
  6. Compute alpha: SHADOW_MAX_ALPHA * (1 - height / SHADOW_MAX_HEIGHT)
  7. Compute shadow radius: ball_radius * SHADOW_SCALE
  8. Get terrain normal at (x, z) via GetTerrainNormalAt
  9. Rotate disc from Y-up to terrain normal: axis = cross((0,1,0), N), angle = acos(N.y)
  10. Draw GL_TRIANGLE_FAN disc at (x, terrainHeight + SHADOW_OFFSET, z)
      - Center vertex: (0,0,0) color (0,0,0,alpha)
      - Edge vertices: cos/sin ring, color (0,0,0,0) for soft falloff
```

### GL state management
Uses `glPushAttrib` / `glPopAttrib` to save and restore all GL state safely:
```
glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_CURRENT_BIT)
  glDisable(GL_LIGHTING)
  glDisable(GL_TEXTURE_2D)
  glEnable(GL_BLEND)
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
  glEnable(GL_POLYGON_OFFSET_FILL) + glPolygonOffset(-1, -1)
  glDisable(GL_CULL_FACE)
  ... render shadows ...
glPopAttrib()
```

### Render order in DrawPrimitives
```
1. Skybox
2. Game models (spheres)
3. Deep fluid
4. Terrain            ← existing
5. Ground shadows     ← NEW (after terrain so shadows appear on top)
6. Foreground fluid
```

## Constants

| Name | Value | Description |
|------|-------|-------------|
| `SHADOW_MAX_HEIGHT` | `50.0f` | Shadows fade out above this distance from ground |
| `SHADOW_MAX_ALPHA` | `0.5f` | Maximum shadow darkness (at ground level) |
| `SHADOW_OFFSET` | `0.2f` | Y offset above terrain to prevent z-fighting |
| `SHADOW_SEGMENTS` | `16` | Triangle fan segments for disc approximation |
| `SHADOW_SCALE` | `1.2f` | Shadow radius = ball radius × this factor |

## Tasks

| ID | Task | Depends On | Status |
|----|------|------------|--------|
| `shadow-constants` | Add shadow constants to SkullbonezCommon.h | — | ✅ Done |
| `shadow-terrain-normal` | Add `GetTerrainNormalAt` to Terrain | constants | ✅ Done |
| `shadow-render` | Implement `RenderShadows` with terrain-aligned disc | terrain-normal | ✅ Done |
| `shadow-integrate` | Call from `DrawPrimitives` after terrain render | render | ✅ Done |
| `shadow-build` | Build & verify 0 errors, 0 warnings | all above | ✅ Done |

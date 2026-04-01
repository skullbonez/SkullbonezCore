# Ground Shadows for Spheres — Progress

## Task 1: shadow-constants — Add Shadow Constants
**Status:** Pending

### Step 1.1 — Add shadow defines to SkullbonezCommon.h
- Open `SkullbonezSource/SkullbonezCommon.h`
- Locate the physics constants block (after `BROADPHASE_CELL_SIZE` around line 122)
- Add the following defines:
  ```cpp
  #define SHADOW_MAX_HEIGHT       50.0f       // Shadows fade out above this distance from ground
  #define SHADOW_MAX_ALPHA        0.5f        // Maximum shadow darkness (0-1, at ground level)
  #define SHADOW_OFFSET           0.2f        // Y offset above terrain to prevent z-fighting
  #define SHADOW_SEGMENTS         16          // Triangle fan segments for shadow disc
  #define SHADOW_SCALE            1.2f        // Shadow radius multiplier (1.0 = same as ball)
  ```

---

## Task 2: shadow-render — Implement RenderShadows Method
**Status:** Pending

### Step 2.1 — Add terrain include to GameModelCollection header
- Open `SkullbonezSource/SkullbonezGameModelCollection.h`
- Add `#include "SkullbonezTerrain.h"` after the existing includes
- This gives access to `Terrain::GetTerrainHeightAt()` and `Terrain::IsInBounds()`

### Step 2.2 — Add shadow display list member and RenderShadows declaration
- In the private section of the `GameModelCollection` class, add:
  ```cpp
  UINT shadowDisplayList;  // Compiled display list for shadow disc geometry
  ```
- In the public section, add:
  ```cpp
  void RenderShadows(Geometry::Terrain* terrain);  // Renders ground shadows beneath all models
  ```

### Step 2.3 — Compile shadow display list in constructor
- Open `SkullbonezSource/SkullbonezGameModelCollection.cpp`
- Add `<cmath>` include at the top for `cosf`/`sinf`
- In the constructor, after the `reserve` call, compile the unit shadow disc:
  ```cpp
  // compile shadow disc display list (unit radius, baked alpha gradient)
  this->shadowDisplayList = glGenLists(1);
  glNewList(this->shadowDisplayList, GL_COMPILE);
  glBegin(GL_TRIANGLE_FAN);
      glColor4f(0.0f, 0.0f, 0.0f, SHADOW_MAX_ALPHA);
      glVertex3f(0.0f, 0.0f, 0.0f);
      glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
      for(int s = 0; s <= SHADOW_SEGMENTS; ++s)
      {
          float angle = (_2PI * s) / SHADOW_SEGMENTS;
          glVertex3f(cosf(angle), 0.0f, sinf(angle));
      }
  glEnd();
  glEndList();
  ```
- The disc is compiled at unit radius with center alpha = `SHADOW_MAX_ALPHA` and edge alpha = 0
- Trig is computed once at startup, never per-frame
- `_2PI` is already defined in SkullbonezCommon.h

### Step 2.4 — Implement RenderShadows in GameModelCollection.cpp
- Add the following method after `RenderModels`:

```cpp
void GameModelCollection::RenderShadows(Geometry::Terrain* terrain)
{
    if(!terrain) return;

    // save GL state and configure for shadow rendering
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    for(int i = 0; i < (int)this->gameModels.size(); ++i)
    {
        Vector3 pos    = this->gameModels[i].GetPosition();
        float   radius = this->gameModels[i].GetBoundingRadius();

        // skip if ball is outside terrain bounds
        if(!terrain->IsInBounds(pos.x, pos.z)) continue;

        // get terrain height and compute distance from ball bottom to ground
        float groundY = terrain->GetTerrainHeightAt(pos.x, pos.z);
        float height  = pos.y - groundY - radius;
        if(height < 0.0f) height = 0.0f;

        // skip if ball is too high for a visible shadow
        if(height >= SHADOW_MAX_HEIGHT) continue;

        // scale shadow: full size at ground, shrinks to nothing at max height
        float scale = radius * SHADOW_SCALE * (1.0f - height / SHADOW_MAX_HEIGHT);

        // translate to ground position, scale the unit disc, call display list
        glPushMatrix();
        glTranslatef(pos.x, groundY + SHADOW_OFFSET, pos.z);
        glScalef(scale, 1.0f, scale);
        glCallList(this->shadowDisplayList);
        glPopMatrix();
    }

    // restore GL state
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
}
```

    // restore GL state
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
}
```

Key implementation notes:
- The disc is a `GL_TRIANGLE_FAN` with `SHADOW_SEGMENTS + 2` vertices (1 center + segments + 1 closing)
- Center vertex has `alpha` opacity, edge vertices have 0 opacity → soft radial gradient
- Uses `_2PI` constant already defined in SkullbonezCommon.h
- `cosf`/`sinf` generate the circle in the XZ plane (Y is up)
- GL state is saved/restored to not affect subsequent rendering (terrain, fluid)
- Polygon offset pushes shadows slightly toward the camera to prevent z-fighting
- Blending is switched from additive (`GL_ONE`) to standard alpha (`GL_ONE_MINUS_SRC_ALPHA`) for shadows, then restored

---

## Task 3: shadow-integrate — Call from DrawPrimitives
**Status:** Pending

### Step 3.1 — Add shadow rendering call in SkullbonezRun::DrawPrimitives
- Open `SkullbonezSource/SkullbonezRun.cpp`
- Locate the `DrawPrimitives` method
- Find where terrain is rendered (the `cTerrain->Render()` block)
- AFTER the terrain render block, BEFORE the foreground fluid, add:
  ```cpp
  // render ground shadows on top of terrain
  this->cGameModelCollection.RenderShadows(this->cTerrain.get());
  ```
- Shadows must render AFTER terrain (so they appear on top of it) and BEFORE foreground
  fluid (so water covers shadows in submerged areas)

---

## Task 4: shadow-build — Build & Verify
**Status:** Pending

### Step 4.1 — Build Debug configuration
- Run MSBuild with `Configuration=Debug`, `Platform=x86`
- Expect: 0 errors, 0 warnings

### Step 4.2 — Build Release configuration
- Run MSBuild with `Configuration=Release`, `Platform=x86`
- Expect: 0 errors, 0 warnings

### Step 4.3 — Visual verification
- Run the simulation and observe:
  - Dark circular shadows visible beneath all balls
  - Shadows darken when balls are near the ground
  - Shadows fade and disappear when balls are airborne
  - Shadows don't z-fight with the terrain surface
  - No visual artifacts at terrain edges

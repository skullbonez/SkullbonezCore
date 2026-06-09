# Plan 6 — Rendering Performance: Sphere Geometry

## Problem

`SkullbonezHelper::DrawSphere` was called once per ball per frame. It had two performance issues:

1. **Per-call heap allocation**: `gluNewQuadric()` / `gluDeleteQuadric()` on every invocation
   allocated and freed a quadric state object each frame for each ball.

2. **Per-call tessellation**: `gluSphere` tessellates a full sphere (25×25 = 625 vertices,
   ~1200 triangles) on every call. With up to 160 balls at 60fps this is significant CPU work
   submitted to the GL pipeline redundantly — the geometry never changes.

## Approach

- `GLUquadricObj` holds no per-sphere state; radius is passed directly to `gluSphere`.
  A single quadric can be reused for all spheres.
- Compile a **unit sphere** (radius = 1.0) into an OpenGL **display list** once on first use.
  Each ball renders via `glScalef(radius, radius, radius)` + `glCallList` — the tessellation
  runs exactly once for the entire process lifetime.

## Tasks

| # | Change |
|---|--------|
| 1 | Reuse a single `GLUquadricObj*` via function-local static — eliminate per-call `gluNewQuadric/Delete` |
| 2 | Compile unit sphere into a GL display list; replace `gluSphere` call with `glScalef` + `glCallList` |
| 3 | Promote list ID from function-local static to file-scope static (resettable); add `ResetGLResources()` |
| 4 | Call `ResetGLResources()` from `SetInitialOpenGlState()` to rebuild the list after GL context recreate |

## Known Issue Fixed During Implementation

### Display list invalidated on GL context recreate
A GL display list is owned by the context it was compiled in. After `wglDeleteContext` +
`wglCreateContext`, the old list ID is meaningless in the new context. A function-local static
cannot be reset, so the stale ID persisted and `glCallList` drew nothing on the second run.

Fix: Promote `sphereList` to a file-scope static initialised to `0`. `ResetGLResources()` sets
it back to `0`; `DrawSphere` rebuilds it lazily when `0` is detected. `SetInitialOpenGlState`
calls `ResetGLResources()` at the start of every new GL context.

# FFP → Shader-Based Rendering Migration — Implementation Plan

## Overview

Migrate SkullbonezCore's rendering pipeline from OpenGL 1.x Fixed Function Pipeline (FFP) to OpenGL 3.3 Core Profile with GLSL shaders. The goal is a **visual match** of the current output using modern rendering techniques: VBOs/VAOs replace immediate mode, GLSL shaders replace the fixed lighting/texturing pipeline, and a new `SkullbonezMatrix4` class replaces the GL matrix stack. The migration is **incremental** — each phase leaves the project buildable and visually testable.

## Constraints & Assumptions

- **Target:** OpenGL 3.3 Core Profile (widest modern hardware support)
- **Extension Loader:** GLAD (generated .h/.c pair, no external DLL)
- **Platform:** Windows x86 (Win32) only — no portability changes
- **Visual Goal:** Match current FFP output (Gouraud shading, single directional light, textured terrain/skybox/spheres, alpha-blended water + shadows, bitmap text)
- **Build:** Must remain 0 errors, 0 warnings at `/W4` after every phase
- **No GLU:** All `glu*` calls must be replaced (gluPerspective, gluLookAt, gluSphere, gluBuild2DMipmaps) — GLU is not available in Core Profile
- **No display lists:** `glGenLists`/`glNewList`/`glCallList` are removed in Core Profile
- **Assumption:** GLAD files will be generated externally and added to `ThirdPtySource\GLAD\` (glad.h, glad.c, khrplatform.h)
- **Assumption:** Existing `ThirdPtySource\JPEG\` texture loading stays; only the GL upload path changes (`gluBuild2DMipmaps` → `glTexImage2D` + `glGenerateMipmap`)

## Current Architecture (Relevant Parts)

### Rendering Pipeline (per frame)
```
SkullbonezRun::Render()
  → glClear(COLOR|DEPTH)
  → glLoadIdentity()
  → CameraCollection::SetCamera()          → gluLookAt()
  → DrawPrimitives()
      → glLightfv(GL_LIGHT0, GL_POSITION)  [single directional light]
      → SkyBox::Render()                    [6× glBegin(GL_QUADS)]
      → GameModelCollection::RenderModels() [300× display-list sphere via gluSphere]
      → WorldEnvironment::RenderFluid()     [5× glBegin(GL_QUADS)]
      → Terrain::Render()                   [glCallList → display list of GL_TRIANGLE_STRIP]
      → GameModelCollection::RenderShadows()[300× glBegin(GL_TRIANGLE_FAN)]
  → DrawWindowText()
      → Text2d::Render2dText()              [glCallLists → wgl bitmap font]
  → SwapBuffers()
```

### Key Files Affected
| File | FFP Usage |
|------|-----------|
| `SkullbonezWindow.cpp` | `wglCreateContext`, `gluPerspective`, `glOrtho`, `glMatrixMode` |
| `SkullbonezHelper.cpp` | `glEnable(GL_LIGHTING)`, `glLightfv`, `glMaterialfv`, `gluSphere`, `glShadeModel`, `glColorMaterial`, display lists |
| `SkullbonezRun.cpp` | `glClear`, `glLoadIdentity`, `glPushMatrix/Pop`, `glTranslatef`, `glScalef`, `glLightfv`, `glColor3ub` |
| `SkullbonezSkyBox.cpp` | 6× `glBegin(GL_QUADS)`, `glTexCoord2i`, `glVertex3i`, `glNormal3i`, `glDisable(GL_LIGHTING)` |
| `SkullbonezTerrain.cpp` | Display list with `GL_TRIANGLE_STRIP`, `glNormal3f`, `glTexCoord2f`, `glVertex3i` |
| `SkullbonezGameModelCollection.cpp` | `glBegin(GL_TRIANGLE_FAN)`, `glColor4f`, `glVertex3f`, `glPushAttrib/Pop`, `glPolygonOffset` |
| `SkullbonezWorldEnvironment.cpp` | `glBegin(GL_QUADS)`, `glDisable(GL_LIGHTING)`, `glEnable(GL_BLEND)` |
| `SkullbonezText.cpp` | `glCallLists`, `glListBase`, `wglUseFontBitmaps`-style display lists, `glOrtho` |
| `SkullbonezTextureCollection.cpp` | `gluBuild2DMipmaps`, `glGenTextures`, `glBindTexture`, `glTexParameteri` |
| `SkullbonezCameraCollection.cpp` | `gluLookAt`, `glTranslatef` |
| `SkullbonezQuaternion.cpp` | `glMultMatrixf` |
| `SkullbonezBoundingSphere.cpp` | `glPushMatrix`, `glTranslatef`, `glPopMatrix` |
| `SkullbonezGameModel.cpp` | `glPushMatrix`, `glTranslatef`, `glPopMatrix` |
| `SkullbonezCommon.h` | `#include <gl\gl.h>`, `#include <gl\glu.h>`, `#pragma comment(lib, ...)` |

### Math Types (unchanged, extended)
- `SkullbonezVector3` — 3-component float vector (public x,y,z)
- `SkullbonezQuaternion` — orientation storage (private x,y,z,w), has `GlRotateToOrientation()` which calls `glMultMatrixf`
- `SkullbonezRotationMatrix` — 3×3 rotation matrix
- **Missing:** 4×4 matrix type for MVP transforms

## Proposed Architecture

### New Files

| New File | Purpose |
|----------|---------|
| `ThirdPtySource\GLAD\glad.h` | GLAD OpenGL 3.3 Core loader header |
| `ThirdPtySource\GLAD\glad.c` | GLAD loader implementation |
| `ThirdPtySource\GLAD\KHR\khrplatform.h` | GLAD platform types |
| `SkullbonezSource\SkullbonezMatrix4.h` | 4×4 float matrix (projection, view, model) |
| `SkullbonezSource\SkullbonezMatrix4.cpp` | Matrix4 implementation (perspective, lookAt, ortho, translate, rotate, scale, multiply) |
| `SkullbonezSource\SkullbonezShader.h` | Shader program loader (compile, link, uniform setters) |
| `SkullbonezSource\SkullbonezShader.cpp` | Shader implementation |
| `SkullbonezSource\SkullbonezMesh.h` | VAO/VBO mesh wrapper (upload vertex data, draw) |
| `SkullbonezSource\SkullbonezMesh.cpp` | Mesh implementation |
| `SkullbonezData\shaders\lit_textured.vert` | Vertex shader: MVP transform + Gouraud lighting |
| `SkullbonezData\shaders\lit_textured.frag` | Fragment shader: texture sampling + vertex color from lighting |
| `SkullbonezData\shaders\unlit_textured.vert` | Vertex shader: MVP transform only (skybox, water) |
| `SkullbonezData\shaders\unlit_textured.frag` | Fragment shader: texture sample only |
| `SkullbonezData\shaders\shadow.vert` | Vertex shader: MVP + per-vertex alpha |
| `SkullbonezData\shaders\shadow.frag` | Fragment shader: flat black with alpha |
| `SkullbonezData\shaders\text.vert` | Vertex shader: ortho projection for 2D text |
| `SkullbonezData\shaders\text.frag` | Fragment shader: texture atlas sampling for bitmap text |

### Data Flow Change

**Before (FFP):**
```
CPU vertex data → glBegin/glVertex → GPU (immediate)
GL matrix stack → glPushMatrix/glTranslatef/gluPerspective → GPU
GL lighting state → glLightfv/glMaterialfv → GPU
```

**After (Shader):**
```
CPU vertex data → VBO (uploaded once) → VAO (vertex format) → glDrawArrays/glDrawElements
Matrix4 objects (CPU) → uniform mat4 → GPU shader
Light params (CPU) → uniform vec3/vec4 → GPU shader
```

### Shader Uniform Interface (sketch)

```glsl
// lit_textured.vert
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;
uniform vec4 uLightSpecular;
uniform float uShininess;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
```

```glsl
// lit_textured.frag
uniform sampler2D uTexture;
in vec4 vColor;       // Gouraud-computed lighting color
in vec2 vTexCoord;
out vec4 FragColor;
```

### SkullbonezMatrix4 API (sketch)

```cpp
namespace SkullbonezCore::Math {
class Matrix4 {
    float m[16]; // column-major (OpenGL convention)
public:
    Matrix4();   // identity
    static Matrix4 Perspective(float fovDeg, float aspect, float near, float far);
    static Matrix4 Ortho(float left, float right, float bottom, float top, float near, float far);
    static Matrix4 LookAt(const Vector3& eye, const Vector3& center, const Vector3& up);
    static Matrix4 Translate(const Vector3& v);
    static Matrix4 Scale(const Vector3& v);
    static Matrix4 FromQuaternion(const Quaternion& q);
    Matrix4 operator*(const Matrix4& rhs) const;
    Matrix4& operator*=(const Matrix4& rhs);
    const float* Data() const { return m; }
};
}
```

### SkullbonezShader API (sketch)

```cpp
namespace SkullbonezCore::Rendering {
class Shader {
    UINT programID;
public:
    Shader(const char* vertPath, const char* fragPath);
    ~Shader();
    void Use() const;
    void SetMat4(const char* name, const Matrix4& mat) const;
    void SetVec3(const char* name, const Vector3& v) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;
    void SetFloat(const char* name, float val) const;
    void SetInt(const char* name, int val) const;
};
}
```

### SkullbonezMesh API (sketch)

```cpp
namespace SkullbonezCore::Rendering {
class Mesh {
    UINT vao, vbo;
    int vertexCount;
public:
    // Interleaved: position(3f) + normal(3f) + texcoord(2f) = 8 floats/vertex
    Mesh(const float* data, int vertexCount, bool hasNormals, bool hasTexCoords);
    ~Mesh();
    void Draw(GLenum mode = GL_TRIANGLES) const;
};
}
```

## Phases & Tasks

### Phase 1 — Foundation: GLAD + Core Context + Matrix4
> Goal: Create a modern OpenGL 3.3 Core context with GLAD, add a 4×4 matrix class, and keep the existing rendering working via the compatibility profile as a bridge. At the end of this phase the engine boots into a 3.3 context and GLAD is loading all GL functions.

- [ ] Task 1.1: Generate GLAD files for OpenGL 3.3 Core + add to `ThirdPtySource\GLAD\` — `ThirdPtySource\GLAD\glad.h`, `ThirdPtySource\GLAD\glad.c`, `ThirdPtySource\GLAD\KHR\khrplatform.h`
- [ ] Task 1.2: Update `SkullbonezCommon.h` — replace `#include <gl\gl.h>` / `<gl\glu.h>` with `#include "glad.h"`, remove `glu32.lib` pragma — `SkullbonezSource\SkullbonezCommon.h`
- [ ] Task 1.3: Update `SkullbonezWindow.cpp` — use `wglCreateContextAttribsARB` to request 3.3 Core Profile context, call `gladLoadGL()` after `wglMakeCurrent` — `SkullbonezSource\SkullbonezWindow.cpp`, `SkullbonezSource\SkullbonezWindow.h`
- [ ] Task 1.4: Implement `SkullbonezMatrix4` class — `Perspective`, `Ortho`, `LookAt`, `Translate`, `Scale`, `FromQuaternion`, `operator*` — `SkullbonezSource\SkullbonezMatrix4.h`, `SkullbonezSource\SkullbonezMatrix4.cpp`
- [ ] Task 1.5: Add all new source files to `SKULLBONEZ_CORE.vcxproj` and `.vcxproj.filters` — `SKULLBONEZ_CORE.vcxproj`, `SKULLBONEZ_CORE.vcxproj.filters`
- [ ] Task 1.6: Build and verify — engine must compile with 0 errors/warnings. At this stage the context is 3.3 but we temporarily request a compatibility profile so FFP still works while we migrate.

### Phase 2 — Shader Infrastructure
> Goal: Add shader loading/compilation and the Mesh (VAO/VBO) wrapper. No rendering changes yet — these are utilities for subsequent phases.

- [ ] Task 2.1: Implement `SkullbonezShader` — load GLSL from file, compile, link, uniform setters — `SkullbonezSource\SkullbonezShader.h`, `SkullbonezSource\SkullbonezShader.cpp`
- [ ] Task 2.2: Implement `SkullbonezMesh` — VAO/VBO creation from interleaved float arrays, `Draw()` — `SkullbonezSource\SkullbonezMesh.h`, `SkullbonezSource\SkullbonezMesh.cpp`
- [ ] Task 2.3: Write the four shader programs (files only, not yet wired in):
  - `SkullbonezData\shaders\lit_textured.vert` / `.frag` (Gouraud lighting + texture)
  - `SkullbonezData\shaders\unlit_textured.vert` / `.frag` (texture only, no lighting)
  - `SkullbonezData\shaders\shadow.vert` / `.frag` (flat black + per-vertex alpha)
  - `SkullbonezData\shaders\text.vert` / `.frag` (2D ortho text rendering)
- [ ] Task 2.4: Add new files to vcxproj, build and verify 0 errors/0 warnings

### Phase 3 — Migrate Terrain Rendering
> Goal: Replace the terrain display list with a VBO-backed Mesh drawn by the lit_textured shader. This is the largest single mesh and the best proving ground for the new pipeline.

- [ ] Task 3.1: Modify `SkullbonezTerrain` — in `BuildTerrain()`, build an interleaved float array (pos + normal + texcoord) and create a `Mesh` object instead of a display list. Replace `Render()` to bind the lit shader, set MVP + light uniforms, and call `Mesh::Draw(GL_TRIANGLE_STRIP)` — `SkullbonezSource\SkullbonezTerrain.h`, `SkullbonezSource\SkullbonezTerrain.cpp`
- [ ] Task 3.2: Update `SkullbonezRun::DrawPrimitives()` — compute model/view/projection Matrix4 objects on the CPU, pass them to terrain's render method instead of relying on the GL matrix stack — `SkullbonezSource\SkullbonezRun.cpp`
- [ ] Task 3.3: Replace `gluBuild2DMipmaps` in `SkullbonezTextureCollection::CreateJpegTexture()` with `glTexImage2D` + `glGenerateMipmap` — `SkullbonezSource\SkullbonezTextureCollection.cpp`
- [ ] Task 3.4: Build, run, verify terrain looks identical to FFP version

### Phase 4 — Migrate Skybox Rendering
> Goal: Replace skybox immediate mode with a VBO and the unlit_textured shader.

- [ ] Task 4.1: Modify `SkullbonezSkyBox` — build 6-face quad geometry into a `Mesh` (36 vertices, pos + texcoord), replace `Render()` to use the unlit shader with MVP uniforms — `SkullbonezSource\SkullbonezSkyBox.h`, `SkullbonezSource\SkullbonezSkyBox.cpp`
- [ ] Task 4.2: Update `SkullbonezRun::DrawPrimitives()` — compute skybox model matrix (translate to camera + scale) on CPU, pass to skybox render — `SkullbonezSource\SkullbonezRun.cpp`
- [ ] Task 4.3: Build, run, verify skybox looks identical

### Phase 5 — Migrate Sphere (Game Model) Rendering
> Goal: Replace gluSphere display list with a procedurally generated sphere Mesh, rendered with the lit_textured shader.

- [ ] Task 5.1: Add procedural sphere generation to `SkullbonezHelper` or a new utility — generate interleaved vertex data (pos + normal + texcoord) for a UV sphere with the same resolution as the current gluSphere (25 slices × 25 stacks) — `SkullbonezSource\SkullbonezHelper.h`, `SkullbonezSource\SkullbonezHelper.cpp`
- [ ] Task 5.2: Modify `SkullbonezBoundingSphere::DEBUG_RenderCollisionVolume()` — compute a model matrix (translate + scale) instead of glPushMatrix/glTranslatef, pass to shader — `SkullbonezSource\SkullbonezBoundingSphere.cpp`
- [ ] Task 5.3: Modify `SkullbonezGameModel::RenderModel()` and `OrientMesh()` — compute model matrix from position + quaternion using `Matrix4::Translate` * `Matrix4::FromQuaternion` instead of glTranslatef/glMultMatrixf — `SkullbonezSource\SkullbonezGameModel.h`, `SkullbonezSource\SkullbonezGameModel.cpp`
- [ ] Task 5.4: Remove `glMultMatrixf` from `SkullbonezQuaternion::GlRotateToOrientation()` — replace with a method that returns a `Matrix4` (or modify `Matrix4::FromQuaternion` to use the existing quaternion-to-matrix math) — `SkullbonezSource\SkullbonezQuaternion.h`, `SkullbonezSource\SkullbonezQuaternion.cpp`
- [ ] Task 5.5: Build, run, verify 300 textured lit spheres look identical

### Phase 6 — Migrate Water & Shadow Rendering
> Goal: Replace water quads and shadow triangle fans with VBO-backed meshes and shaders.

- [ ] Task 6.1: Modify `SkullbonezWorldEnvironment::RenderFluid()` — create a static quad `Mesh` (4 verts, pos + normal + texcoord), render with unlit shader + blending — `SkullbonezSource\SkullbonezWorldEnvironment.h`, `SkullbonezSource\SkullbonezWorldEnvironment.cpp`
- [ ] Task 6.2: Modify `SkullbonezGameModelCollection::RenderShadows()` — replace per-model `glBegin(GL_TRIANGLE_FAN)` with a shared shadow disc `Mesh` (reusable, instanced per-model via model matrix + alpha uniform). Use the shadow shader — `SkullbonezSource\SkullbonezGameModelCollection.h`, `SkullbonezSource\SkullbonezGameModelCollection.cpp`
- [ ] Task 6.3: Build, run, verify water transparency and shadow fade look identical

### Phase 7 — Migrate Text Rendering
> Goal: Replace display-list bitmap font with a shader-based texture atlas approach.

- [ ] Task 7.1: Modify `SkullbonezText` — generate a font texture atlas (render the 96 ASCII characters to a texture at init time using the existing Windows font approach, or pre-bake an atlas image). Store glyph UVs — `SkullbonezSource\SkullbonezText.h`, `SkullbonezSource\SkullbonezText.cpp`
- [ ] Task 7.2: Replace `Render2dText()` — build a dynamic quad batch (one textured quad per character), upload to a VBO, draw with the text shader in ortho projection — `SkullbonezSource\SkullbonezText.cpp`
- [ ] Task 7.3: Build, run, verify FPS/timing text renders correctly

### Phase 8 — Migrate Camera & Remove FFP Matrix Stack
> Goal: Remove all remaining glMatrixMode, glPushMatrix, glPopMatrix, glLoadIdentity, glTranslatef, glScalef, glRotatef calls. All transforms are now Matrix4 objects on the CPU.

- [ ] Task 8.1: Modify `SkullbonezCameraCollection` — replace `gluLookAt` with `Matrix4::LookAt`, store the view matrix, provide `GetViewMatrix()` — `SkullbonezSource\SkullbonezCameraCollection.h`, `SkullbonezSource\SkullbonezCameraCollection.cpp`
- [ ] Task 8.2: Modify `SkullbonezWindow` — replace `gluPerspective` with `Matrix4::Perspective`, store the projection matrix, provide `GetProjectionMatrix()`. Replace ortho mode switch with `Matrix4::Ortho` — `SkullbonezSource\SkullbonezWindow.h`, `SkullbonezSource\SkullbonezWindow.cpp`
- [ ] Task 8.3: Modify `SkullbonezRun` — remove all `glPushMatrix/glPopMatrix/glTranslatef/glScalef/glLoadIdentity`. Compute per-object model matrices on CPU, pass view+projection from camera/window — `SkullbonezSource\SkullbonezRun.cpp`
- [ ] Task 8.4: Remove `SkullbonezHelper::StateSetup()` FFP state calls (lighting enables, material setup, shade model, etc.) — replace with shader uniform setup — `SkullbonezSource\SkullbonezHelper.h`, `SkullbonezSource\SkullbonezHelper.cpp`
- [ ] Task 8.5: Remove `RigidBody::RotateBodyToOrientation()` (which calls `glMultMatrixf`) — orientation is now applied via Matrix4 in the model matrix — `SkullbonezSource\SkullbonezRigidBody.h`, `SkullbonezSource\SkullbonezRigidBody.cpp`
- [ ] Task 8.6: Build, run, verify everything still works. Grep for any remaining FFP calls — there should be zero.

### Phase 9 — Switch to Core Profile & Cleanup
> Goal: Switch from compatibility profile to true 3.3 Core Profile. Remove all dead FFP code. Remove glu32.lib dependency.

- [ ] Task 9.1: Change `wglCreateContextAttribsARB` flags from compatibility to core profile — `SkullbonezSource\SkullbonezWindow.cpp`
- [ ] Task 9.2: Remove `#include <gl\glu.h>` and `#pragma comment(lib, "glu32.lib")` from `SkullbonezCommon.h` — `SkullbonezSource\SkullbonezCommon.h`
- [ ] Task 9.3: Remove any remaining dead FFP helper functions, unused state constants, or display list references — all affected files
- [ ] Task 9.4: Final build — 0 errors, 0 warnings. Run and verify visual output matches original FFP rendering.

## Implementation Order

1. **Task 1.1** — Generate GLAD (no dependencies)
2. **Task 1.2** — Update includes (depends on 1.1)
3. **Task 1.4** — Matrix4 class (no dependencies, can parallel with 1.2)
4. **Task 1.3** — Core context creation (depends on 1.1, 1.2)
5. **Task 1.5** — vcxproj updates (depends on 1.1–1.4)
6. **Task 1.6** — Build verification (depends on 1.5)
7. **Task 2.1** — Shader class (depends on Phase 1)
8. **Task 2.2** — Mesh class (depends on Phase 1)
9. **Task 2.3** — Write GLSL shaders (no code dependencies, can parallel with 2.1/2.2)
10. **Task 2.4** — Build verification (depends on 2.1–2.3)
11. **Task 3.3** — Fix texture upload (depends on Phase 1 — needed before any shader rendering)
12. **Task 3.1** — Terrain mesh migration (depends on 2.1, 2.2, 2.3, 3.3)
13. **Task 3.2** — Terrain render integration (depends on 3.1, 1.4)
14. **Task 3.4** — Terrain verification
15. **Tasks 4.1–4.3** — Skybox migration (depends on Phase 2)
16. **Tasks 5.1–5.5** — Sphere migration (depends on Phase 2)
17. **Tasks 6.1–6.3** — Water + shadows (depends on Phase 2)
18. **Tasks 7.1–7.3** — Text rendering (depends on Phase 2)
19. **Tasks 8.1–8.6** — Remove FFP matrix stack (depends on Phases 3–7 ALL complete)
20. **Tasks 9.1–9.4** — Core profile switch + cleanup (depends on Phase 8)

**⚠️ Highest-Risk Items:**
- **Task 1.3 (context creation):** `wglCreateContextAttribsARB` requires loading via the old context first (bootstrap problem). Must create a temporary legacy context to load the function pointer, then destroy it and create the real 3.3 context.
- **Task 3.1 (terrain mesh):** The terrain display list uses per-strip `glBegin(GL_TRIANGLE_STRIP)`. Converting to a single VBO requires either using primitive restart or re-indexing to `GL_TRIANGLES`.
- **Phase 8 (matrix stack removal):** This is the phase where all FFP transform code is removed. Every render path must be updated to pass Matrix4 uniforms. If any path is missed, it will be a black screen. Thorough grep for `glPushMatrix`, `glTranslatef`, `glRotatef`, `glScalef`, `glMultMatrixf`, `glLoadIdentity`, `glMatrixMode` is essential before marking this phase done.

## Open Questions

1. **GLAD generation:** Should we generate GLAD files as part of this plan (requires internet access to https://glad.dav1d.de or the glad2 Python package), or will you provide pre-generated files?
2. **Terrain strip topology:** Should we keep `GL_TRIANGLE_STRIP` with primitive restart (`glEnable(GL_PRIMITIVE_RESTART)` + index buffer) or convert to `GL_TRIANGLES` (simpler, slightly more vertices)? Both are valid in 3.3 Core.
3. **Sphere mesh:** Should the procedural sphere match `gluSphere(1.0, 25, 25)` exactly (UV sphere, 25 slices × 25 stacks = ~1250 triangles), or is a different resolution acceptable?
4. **Font atlas:** Should we render the font atlas at runtime using Windows GDI (matching current approach) or pre-bake a PNG atlas into `SkullbonezData\`?

## Out of Scope

- **Visual upgrades** (per-pixel Phong, normal mapping, PBR, HDR, MSAA, post-processing) — these come after the migration is validated
- **Instanced rendering** for the 300 spheres — optimization for later
- **Index buffers (EBO)** — may be added for terrain optimization but not required for visual parity
- **Compute shaders** — GL 3.3 does not support them
- **Multi-platform support** — remains Windows-only
- **Asset pipeline changes** — JPEG loading stays as-is
- **x64 build target** — remains Win32 (x86)
- **FreeType / stb_truetype integration** — bitmap font atlas is sufficient for current text needs

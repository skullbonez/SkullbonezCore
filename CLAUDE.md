# CLAUDE.md — SkullbonezCore

A Windows C++/OpenGL 3D graphics and physics simulation engine originally authored in 2005, currently being migrated from OpenGL Fixed Function Pipeline (FFP) to modern OpenGL 3.3 shader-based rendering.

---

## Session Start Protocol

**At the start of every session, do these steps in order:**

1. Read `Copilot/SessionState.md` — current branch, last commit, phase progress, remaining tasks, known bugs
2. Load all skills by reading each skill file listed in the Skills table below
3. Ask the user: *"I've loaded all skills and read the session state. Ready to continue?"*

Do not begin any work until this protocol is complete.

---

## Skills — Load at Session Start

| Skill | Path |
|-------|------|
| **skore-build-pipeline** (use for every commit) | `Copilot/Skills/skore-build-pipeline/skill.md` |
| skore-render-test | `Copilot/Skills/skore-render-test/skill.md` |
| skore-build | `Copilot/Skills/skore-build/skill.md` |
| skore-cdb-debug | `Copilot/Skills/skore-cdb-debug/skill.md` |
| skore-launch | `Copilot/Skills/skore-launch/skill.md` |

---

## Commit Rules (Mandatory)

- **After every change**, run `skore-build-pipeline` to build, test, update baselines, and run the perf test
- **Every commit must include:**
  1. Updated reference images — run both render test scenes, overwrite `Copilot/Skills/skore-render-test/baseline_*.png`
  2. Performance artifact — run perf test, write JSON to `Copilot/Skills/skore-render-test/perf_history/{commit}.json`
  3. Updated `Copilot/Plans/progress.md`
- **Never commit or push** unless the user explicitly asks
- Build must produce **zero warnings** (`/W4`)

---

## Build

```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=Win32
```

- Output: `Debug\SKULLBONEZ_CORE.exe` / `Release\SKULLBONEZ_CORE.exe`
- Target: **Win32 (x86) — do not change to x64**
- Toolset: v143 (VS2019+), MSBuild v17 / VS2022 Enterprise
- Kill `SKULLBONEZ_CORE.exe` before building (locked exe → LNK1168)
- Python: use `py` command (not `python`); Pillow must be installed

---

## Architecture

**Entry point:** `SkullbonezInit.cpp` (WinMain) → `SkullbonezWindow` (OpenGL context, WGL) → `SkullbonezRun` (main loop)

`SkullbonezRun` owns and orchestrates all subsystems: `Initialise()` brings everything up; `Run()` drives input → physics → collision → render each frame.

### Layer Breakdown

| Layer | Key Classes |
|-------|-------------|
| Platform | `SkullbonezWindow` (Singleton), `SkullbonezInput`, `SkullbonezTimer`, `SkullbonezInit` |
| Rendering | `SkullbonezTextureCollection` (Singleton), `SkullbonezSkyBox` (Singleton), `SkullbonezTerrain`, `SkullbonezHelper`, `SkullbonezText`, `SkullbonezFramebuffer`, `SkullbonezShader`, `SkullbonezMesh` |
| Camera | `SkullbonezCameraCollection` (Singleton, 3 fixed cameras), `SkullbonezCamera` |
| Game Objects | `SkullbonezGameModelCollection` (`std::vector<GameModel>`), `SkullbonezGameModel` |
| Physics | `SkullbonezRigidBody`, `SkullbonezCollisionResponse`, `SkullbonezWorldEnvironment` |
| Collision | `SkullbonezDynamicsObject` (abstract), `SkullbonezBoundingSphere` (concrete) |
| Math | `SkullbonezVector3`, `SkullbonezQuaternion`, `SkullbonezRotationMatrix`, `SkullbonezMatrix4`, `SkullbonezGeometricMath`, `SkullbonezGeometricStructures` |
| Test Harness | `SkullbonezTestScene` |

### Global Config

`SkullbonezSource/SkullbonezCommon.h` is the single global header — it defines all `#define` constants, compile-time config flags (`FULLSCREEN_MODE`, `SCREEN_X`, `SCREEN_Y`, etc.), universal includes, and the `constexpr` FNV-1a hash function used for compile-time string keys.

Key constants: `SCREEN_X=800`, `SCREEN_Y=600`, frustum near=`1.0f` far=`5500.0f`, gravity=`-30.0f`, fluid height=`25.0f`, spatial grid cell=`11.0f`.

---

## Namespace Hierarchy

```
SkullbonezCore::Basics
SkullbonezCore::Environment
SkullbonezCore::Textures
SkullbonezCore::Hardware
SkullbonezCore::Text
SkullbonezCore::Geometry
SkullbonezCore::Math::Vector
SkullbonezCore::Math::Orientation
SkullbonezCore::Math::CollisionDetection
SkullbonezCore::Math::Transformation
SkullbonezCore::Physics
SkullbonezCore::GameObjects
```

---

## Code Conventions

### Naming

- **Classes**: `Skullbonez` prefix, PascalCase — `SkullbonezVector3`, `SkullbonezRigidBody`
- **Files**: one class per file — `SkullbonezFoo.h` / `SkullbonezFoo.cpp`
- **Methods**: PascalCase — `GetPosition()`, `RunPhysics()`, `CalculateGravity()`
- **Members**: camelCase with intent prefixes:
  - `is*` — boolean flags (`isGrounded`, `isResponseRequired`)
  - `f*` — float ctor parameters (`fMass`, `fRadius`)
  - `p*` — pointers (`pWorldEnv`, `pTerrain`)
  - `s*` — static/struct members
  - `c*` — class-instance members (`cTextures`, `cCameras`)
- **Constants**: `UPPER_CASE_UNDERSCORE` in `SkullbonezCommon.h`
- **Header guards**: `#ifndef SKULLBONEZ_FOO_H`

### Compile-Time String Keys (Hashes)

Textures and cameras are looked up via `constexpr` FNV-1a hash constants defined in `SkullbonezCommon.h`:

```cpp
textureCollection.GetTexture(TEXTURE_GROUND);
cameraCollection.GetCamera(CAMERA_FREE);
```

Never use raw string keys at runtime. Add new `constexpr` hash constants to `SkullbonezCommon.h` instead.

### Singletons

`SkullbonezWindow`, `SkullbonezTextureCollection`, `SkullbonezCameraCollection`, and `SkullbonezSkyBox` use static-local singletons (not `new`). After calling `Destroy()`, call `ResetGLResources()` before next use — `Instance()` does not re-initialise.

### Memory / Ownership

- **No raw `new`/`delete`** — use `std::unique_ptr` for owned heap objects
- Fixed-size resource collections (textures, cameras): stack-allocated arrays sized by constants in `SkullbonezCommon.h`
- Variable-size game-object collections: `std::vector` with move semantics
- **Per-frame heap allocations are forbidden** — allocation elimination refactor is complete

### Error Handling

Use `std::runtime_error`. The old `catch(char*)` pattern has been removed — do not reintroduce it.

### Platform & CRT

Windows-only. Use secure CRT variants: `fopen_s`, `strcpy_s`, `vsprintf_s`. Do not introduce POSIX equivalents.

---

## Testing

There are no automated unit tests. Testing is **visual regression + performance regression**, driven by the `skore-render-test` skill.

### Scene Mode (Test Harness)

Data-driven deterministic test runner. Launch modes:

```bat
SKULLBONEZ_CORE.exe --scene path/to/scene.scene   # single scene
SKULLBONEZ_CORE.exe --suite path/to/suite.suite   # multi-scene suite
```

| Scene File | Purpose |
|------------|---------|
| `SkullbonezData/scenes/water_ball_test.scene` | Visual regression — terrain, skybox, sphere, water, shadow |
| `SkullbonezData/scenes/perf_test.scene` | Performance regression — 300 balls, 2×5s passes |
| `SkullbonezData/scenes/legacy_smoke.scene` | Smoke test — legacy mode, 300 balls, physics on |
| `SkullbonezData/scenes/physics_roll.scene` | Physics validation — rolling ball |
| `SkullbonezData/scenes/render_tests.suite` | Suite runner for all render tests |

### Visual Regression

- Captures framebuffer via `glReadPixels` at specified frames, saves as BMP → PNG
- Pixel-by-pixel comparison against baseline PNGs in `Copilot/Skills/skore-render-test/`
- 4 reference images (2 scenes × 2 GL context reset passes)
- Baselines **must be updated every commit**

### Performance Regression

- 300 balls, 2×5s passes, seed 42, physics+text enabled
- Memory sampled every 60 frames via `GetProcessMemoryInfo` (psapi.lib)
- CSV: `Debug/perf_log.csv` — analysed by `Copilot/Skills/skore-render-test/analyze_perf.py`
- Artifacts saved to `Copilot/Skills/skore-render-test/perf_history/{commit}.json`
- **Regression thresholds:** avg/p50 timing >10% = FAIL; p99/p99.9 >20% = FAIL; memory growth >5 MB = FAIL

---

## Project Progress

Overall: **Phases 1–10 complete (33/46 tasks done)**. Next: Phase 7 — Text Rendering.

| Phase | Description | Status |
|-------|-------------|--------|
| P1 Code Quality | `catch(char*)` → exceptions, RAII, unique_ptr | ✅ Complete |
| P2 Warning Cleanup | /W4 zero warnings | ✅ Complete |
| P3 Compile-Time Hashes | FNV-1a texture/camera keys | ✅ Complete |
| P4 Eliminate Dynamic Allocation | vectors, unique_ptr, stack singletons | ✅ Complete |
| TH Test Harness | Scene mode, render tests, perf test, skore-render-test skill | ✅ Complete |
| P5 Shader Foundation | GLAD, Matrix4, Shader, Mesh classes | ✅ Complete |
| P6 Shader Infrastructure | GLSL shaders written | ✅ Complete |
| P7 Terrain | VBO mesh + lit_textured shader | ✅ Complete |
| P8 Skybox | VBO mesh + unlit_textured shader | ✅ Complete |
| P9 Spheres | Procedural sphere mesh, GameModel Matrix4 | ✅ Complete |
| P10 Water & Shadows | FBO reflection, vertex-animated water, GL lifecycle fix | ✅ Complete |
| **Text Rendering** | Font atlas + shader quads (replaces wglUseFontBitmaps) | ⏳ **NEXT** |
| FFP Matrix Elimination | gluLookAt/Perspective → Matrix4, remove matrix stack | ⏳ Pending |
| Core Profile Switch | True Core Profile, remove GLU | ⏳ Pending |

### Remaining Tasks

**Text Rendering (do this next):**
- `p7-font-atlas` — Generate font texture atlas via GDI (96 ASCII chars → GL texture at init)
- `p7-text-render` — Replace `wglUseFontBitmaps` display lists in `SkullbonezText.cpp` with shader quad batch + `text.vert/frag`
- `p7-text-verify` — Verify FPS/physics/render timing text renders correctly

**FFP Matrix Elimination:**
- `p8-camera-matrix` — Replace `gluLookAt` with `Matrix4::LookAt`, expose `GetViewMatrix()`
- `p8-projection-matrix` — Replace `gluPerspective` with `Matrix4::Perspective`, expose `GetProjectionMatrix()`
- `p8-run-cleanup` — Remove `glPushMatrix/glPopMatrix/glTranslatef/glScalef/glLoadIdentity` from `SkullbonezRun.cpp`
- `p8-helper-cleanup` — Remove FFP lighting state from `SkullbonezHelper.cpp`
- `p8-rigidbody-cleanup` — Remove `RotateBodyToOrientation()` / `glMultMatrixf` from `SkullbonezRigidBody.cpp`
- `p8-ffp-verify` — Grep for zero remaining FFP calls

**Core Profile Switch:**
- `p9-core-profile` — Change `wglCreateContextAttribsARB` flags to core profile in `SkullbonezWindow.cpp`
- `p9-remove-glu` — Remove `#include <gl\glu.h>` and `glu32.lib`
- `p9-dead-code` — Remove dead FFP helpers, unused state constants, display list references
- `p9-final-verify` — Final build: 0 errors, 0 warnings, full visual + perf verification

---

## Known Bugs

| # | Bug | Area | Status |
|---|-----|------|--------|
| 1 | Water renders through to the back faces of spheres when they intersect the water surface | Rendering / Water | Open |

---

## Critical Technical Notes

### GL Context Lifecycle

`cRun` destructor **must** fire before `wglDeleteContext`. Enforced via nested scope in `SkullbonezInit.cpp`:

```cpp
{ SkullbonezRun cRun; cRun.Run(); }  // destructor fires here, calls ResetGLResources()
wglDeleteContext(...);               // context deleted after
```

Do not restructure this scope — it prevents dangling GL handle errors.

### Water Rendering (Phase 8 Note)

Water still reads the FFP modelview matrix via `glGetFloatv(GL_MODELVIEW_MATRIX)` because the 5 call sites use `glPushMatrix/glTranslatef` to position each copy. This will be cleaned up in Phase 8 (FFP Matrix Elimination).

### Fly Mode (F Key)

- Toggle with `F` — snaps to free camera, freezes physics + auto-cycle, removes terrain/XZ bounds
- WASD to move, mouse to look, Shift for 3× speed, Space to step simulation while paused
- Exit with `F` — restores cursor, bounds, terrain clamp, resumes cycle

### ThirdPtySource/MemMgr

This directory exists but is unused. Do not re-enable it.

---

## Assets

- Textures: JPEG files in `SkullbonezData\` loaded via bundled `ThirdPtySource\JPEG` library
- Terrain heightmap: `SkullbonezData\terrain.raw` (binary RAW greyscale pixels)
- GLSL shaders: `SkullbonezData\shaders\` (GLSL 3.3, loaded and compiled at init)
- Scene files: `SkullbonezData\scenes\`

---

## Key File Locations

### Skills & Test Tools

| What | Path |
|------|------|
| Build pipeline skill | `Copilot/Skills/skore-build-pipeline/skill.md` |
| Render test skill | `Copilot/Skills/skore-render-test/skill.md` |
| Perf analysis script | `Copilot/Skills/skore-render-test/analyze_perf.py` |
| Perf history artifacts | `Copilot/Skills/skore-render-test/perf_history/` |
| Reference baselines (4 PNG) | `Copilot/Skills/skore-render-test/baseline_*.png` |
| CDB debug skill | `Copilot/Skills/skore-cdb-debug/skill.md` |
| Launch skill | `Copilot/Skills/skore-launch/skill.md` |

### Plans & Docs

| What | Path |
|------|------|
| Main progress tracker | `Copilot/Plans/progress.md` |
| FFP migration master plan | `Copilot/Plans/ffp-to-shader-migration.md` |
| Session state / handoff | `Copilot/SessionState.md` |
| Known bugs | `Copilot/Bugs.md` |
| Test harness design | `Copilot/Plans/test-harness.md` |

### Engine Source (Key Files for Active Work)

| What | Path |
|------|------|
| Global config / hashes | `SkullbonezSource/SkullbonezCommon.h` |
| Main render loop | `SkullbonezSource/SkullbonezRun.h` / `.cpp` |
| Text rendering (Phase 7 target) | `SkullbonezSource/SkullbonezText.h` / `.cpp` |
| Camera / gluLookAt target | `SkullbonezSource/SkullbonezCameraCollection.h` / `.cpp` |
| Window / gluPerspective target | `SkullbonezSource/SkullbonezWindow.h` / `.cpp` |
| GL helper / lighting state | `SkullbonezSource/SkullbonezHelper.h` / `.cpp` |
| Rigid body / glMultMatrixf | `SkullbonezSource/SkullbonezRigidBody.h` / `.cpp` |
| Matrix4 class | `SkullbonezSource/SkullbonezMatrix4.h` / `.cpp` |
| Shader class | `SkullbonezSource/SkullbonezShader.h` / `.cpp` |
| Mesh class | `SkullbonezSource/SkullbonezMesh.h` / `.cpp` |
| Text shaders (Phase 7) | `SkullbonezData/shaders/text.vert` / `text.frag` |

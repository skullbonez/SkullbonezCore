# SkullbonezCore — Session State

## Branch & Last Commit
- Branch: `main`
- Last commit: `f07e164` — Fix broadphase spatial grid: use linked-list pool for non-contiguous cell entries

---

## Project Summary
A Windows C++/OpenGL 3.3 Core Profile 3D physics engine (2005, fully modernized). All rendering uses shader-based pipeline. Profiler subsystem with debug overlay. Pre-commit linting framework.

---

## Overall Progress: ALL PHASES COMPLETE

| Phase | Description | Status |
|-------|-------------|--------|
| P1 Code Quality | `catch(char*)`, RAII, unique_ptr | ✅ Complete |
| P2 Warning Cleanup | /W4 zero warnings | ✅ Complete |
| P3 Compile-Time Hashes | FNV-1a texture/camera keys | ✅ Complete |
| P4 Eliminate Dynamic Allocation | vectors, unique_ptr, stack singletons | ✅ Complete |
| TH Test Harness | Scene mode, render tests, perf test, skore-render-test skill | ✅ Complete |
| P5 Foundation | GLAD (core=3.3), Matrix4, Shader, Mesh classes | ✅ Complete |
| P6 Shader Infra | GLSL shaders written | ✅ Complete |
| P7 Terrain | VBO mesh + lit_textured shader | ✅ Complete |
| P8 Skybox | VBO mesh + unlit_textured shader | ✅ Complete |
| P9 Spheres | Procedural sphere, GameModel Matrix4 | ✅ Complete |
| P10 Water & Shadows | FBO reflection, vertex-animated water, GL lifecycle fix | ✅ Complete |
| Text Rendering | Font atlas + shader quads (replaces wglUseFontBitmaps) | ✅ Complete |
| FFP Matrix Elimination | gluLookAt/Perspective → Matrix4; remove matrix stack | ✅ Complete |
| m_ Rename | All this->member → m_member convention applied | ✅ Complete |
| Core Profile Switch | True Core Profile, remove GLU | ✅ Complete |
| Profiler | PROFILE_BEGIN/END/SCOPED, debug overlay, traffic lights | ✅ Complete |
| Code Quality Infra | Pre-commit hooks, clang-format pipeline verification | ✅ Complete |

---

## Remaining Tasks

None. All planned work is complete.

---

## Uncommitted Changes (DO NOT LOSE)
None.

---

## Text Rendering — Technical Notes

### Font Atlas Layout
- 96 ASCII chars (32–127), 16×6 grid
- `FONT_CELL_W=40`, `FONT_CELL_H=32`, atlas = 640×192 pixels
- GL texture is R8 format, atlas data uploaded top-down → GL v=0 is atlas row 0 (space !)

### UV Orientation (verified correct)
- Atlas row `r` → GL v = `r*32/192` to `(r+1)*32/192`
- Glyph visual TOP in atlas = low v in GL (atlas stored top-down, GL v=0 at bottom)
- Quad: `y0`(screen bottom) → `v1` (high v = glyph bottom), `y1`(screen top) → `v0` (low v = glyph top) → renders right-side-up ✓

### Minification Bleeding (current bug)
- Text at fSize=0.02/0.0175 renders ~19–22px on screen, atlas chars are 32px → ~1.5–1.7× minification
- `GL_LINEAR` without mipmaps: each screen pixel samples ~1.5 texels, footprint extends below v0 into the adjacent row
- Symptom: faint ghost strokes from the row above appear at the top of characters (e.g., macron above 'o', extra stroke above 'S')
- Fix: `glGenerateMipmap` selects the correct mip level for the minification ratio → no cross-row bleeding

---

## Backlog / Future Tasks
| ID | Task | Notes |
|----|------|-------|
| narrowphase-pair-regression | Investigate narrowphase 4x slowdown after broadphase rewrite | Went from 0.0105ms → 0.0442ms avg. Old working impl: `40960d1` (unordered_map). New impl: `f07e164` (flat hash table + linked-list pool). New broadphase appears to generate more candidate pairs. Need to instrument pair count, compare ordering effects on `timeRemaining` early-outs. Absolute cost still negligible (0.04ms) but should be understood. |
| replace-jpeg-lib | Replace ThirdPtySource/JPEG with stb_image | Current JPEG lib is ancient bundled source. stb_image is header-only, modern, supports more formats. |

---

## Pipeline Rules (MANDATORY for every commit)
Every commit must include:
1. Updated reference images — run both render test scenes, overwrite `Copilot/Skills/skore-render-test/baseline_*.png`
2. Performance test artifact — run perf test, write JSON to `Copilot/Skills/skore-render-test/perf_history/{commit}.json`
3. Only send PNGs to the LLM for visual review **if local pixel comparison fails**
4. Update `Copilot/Plans/progress.md` to reflect completed work

Full pipeline steps in `Copilot/Skills/skore-build-pipeline/skill.md`.

---

## Key File Locations

### Skills & Tools
| What | Path |
|------|------|
| **Build pipeline skill** | `Copilot/Skills/skore-build-pipeline/skill.md` |
| Render test skill | `Copilot/Skills/skore-render-test/skill.md` |
| Perf analysis script | `Copilot/Skills/skore-render-test/analyze_perf.py` |
| Perf history artifacts | `Copilot/Skills/skore-render-test/perf_history/` |
| Reference baselines (4 PNG) | `Copilot/Skills/skore-render-test/baseline_*.png` |
| CDB debug skill | `Copilot/Skills/skore-cdb-debug/skill.md` |
| Launch skill | `Copilot/Skills/skore-launch/skill.md` |

### Plans & Docs
| What | Path |
|------|------|
| **Main progress tracker** | `Copilot/Plans/progress.md` |
| **FFP migration master plan** | `Copilot/Plans/ffp-to-shader-migration.md` |
| Test harness design | `Copilot/Plans/test-harness.md` |

### Engine Source (key files for current work)
| What | Path |
|------|------|
| Text rendering **(active bug)** | `SkullbonezSource/SkullbonezText.h` / `.cpp` |
| Global config / hashes | `SkullbonezSource/SkullbonezCommon.h` |
| Main render loop | `SkullbonezSource/SkullbonezRun.h` / `.cpp` |
| Window (context creation) | `SkullbonezSource/SkullbonezWindow.h` / `.cpp` |
| Matrix4 class | `SkullbonezSource/SkullbonezMatrix4.h` / `.cpp` |
| Shader class | `SkullbonezSource/SkullbonezShader.h` / `.cpp` |

### Scene & Shader Files
| What | Path |
|------|------|
| Perf test scene | `SkullbonezData/scenes/perf_test.scene` |
| Render test scene | `SkullbonezData/scenes/water_ball_test.scene` |
| Legacy smoke scene | `SkullbonezData/scenes/legacy_smoke.scene` |
| Text test scene | `SkullbonezData/scenes/text_test.scene` |
| Text debug scene | `SkullbonezData/scenes/text_debug.scene` |
| All GLSL shaders | `SkullbonezData/shaders/` |
| Text shaders | `SkullbonezData/shaders/text.vert` / `text.frag` |

---

## Critical Technical Notes

### GL Context Lifecycle
`cRun` destructor MUST run before `wglDeleteContext`. Enforced via nested scope in `SkullbonezInit.cpp`:
```cpp
{ SkullbonezRun cRun; cRun.Run(); }  // destructor fires here
wglDeleteContext(...);                // then delete context
```

### Singleton Pattern
`SkyBox`, `TextureCollection`, `CameraCollection`, `Window` use static local singletons. After `Destroy()`, `ResetGLResources()` must be called before next use.

### Build Environment
- MSBuild v17 / VS2022 **Professional** (not Enterprise — found at `C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe`)
- Win32 x86, /W4 — must be 0 errors 0 warnings
- Kill `SKULLBONEZ_CORE.exe` before building (locks the exe → LNK1168)
- Python via `py` command (not `python`), Pillow installed
- Output: `Debug\SKULLBONEZ_CORE.exe`
- Screen resolution: 1200×900 (bumped from 800×600 in recent session)

### FFP Matrix Elimination (P8 — COMPLETE)
- Camera: `CameraCollection::SetViewMatrix()` uses `Matrix4::LookAt`; `GetViewMatrix()` exposed
- Projection: `Window::projectionMatrix` set via `Matrix4::Perspective` in `HandleScreenResize`; `GetProjectionMatrix()` exposed
- All `glPushMatrix`, `glTranslatef`, `glScalef`, `glMultMatrixf`, `glLoadIdentity`, `glMatrixMode`, `gluLookAt`, `gluPerspective`, `glGetFloatv(GL_MODELVIEW_MATRIX)`, `glLoadMatrixf`, `glLightfv` removed
- Skybox matrix: `baseView * Translate(eye.x, SKYBOX_RENDER_HEIGHT, eye.z) * Scale(SKY_BOX_SCALE)`

### Fly Mode (F key)
- Toggle with `F` — snaps to free camera, freezes physics + auto-cycle, removes terrain/XZ bounds
- WASD to move, mouse to look, Shift for 3× speed, Space to step simulation while paused
- Exit with `F` — restores cursor, bounds, terrain clamp, resumes cycle

### Perf Test
- 2×5s passes, 300 balls, seed 42, physics+text enabled
- Memory sampled every 60 frames via `GetProcessMemoryInfo` (psapi.lib)
- CSV: `Debug/perf_log.csv` — analysed by `Copilot/Skills/skore-render-test/analyze_perf.py`
- Regression thresholds: avg/p50 timing >10% = FAIL, p99/p99.9 >20% = FAIL, memory >5 MB growth = FAIL

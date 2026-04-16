# SkullbonezCore — Session State

## Branch & Last Commit
- Branch: `main`
- Last commit: `f80a83d` — Merge dx11-port-prep: text rendering + FFP matrix elimination

---

## Project Summary
A Windows C++/OpenGL 3D physics engine (2005) being migrated from Fixed Function Pipeline (OpenGL 1.x) to modern OpenGL 3.3 shader-based rendering. Goal is to extract a render abstraction layer for a future DirectX 11 port (Option A).

---

## Overall Progress: Phases P7+P8 Complete

| Phase | Description | Status |
|-------|-------------|--------|
| P1 Code Quality | `catch(char*)`, RAII, unique_ptr | ✅ Complete |
| P2 Warning Cleanup | /W4 zero warnings | ✅ Complete |
| P3 Compile-Time Hashes | FNV-1a texture/camera keys | ✅ Complete |
| P4 Eliminate Dynamic Allocation | vectors, unique_ptr, stack singletons | ✅ Complete |
| TH Test Harness | Scene mode, render tests, perf test, skore-render-test skill | ✅ Complete |
| P5 Foundation | GLAD, Matrix4, Shader, Mesh classes | ✅ Complete |
| P6 Shader Infra | GLSL shaders written | ✅ Complete |
| P7 Terrain | VBO mesh + lit_textured shader | ✅ Complete |
| P8 Skybox | VBO mesh + unlit_textured shader | ✅ Complete |
| P9 Spheres | Procedural sphere, GameModel Matrix4 | ✅ Complete |
| P10 Water & Shadows | FBO reflection, vertex-animated water, GL lifecycle fix | ✅ Complete |
| Text Rendering | Font atlas + shader quads (replaces wglUseFontBitmaps) | ✅ Complete |
| FFP Matrix Elimination | gluLookAt/Perspective → Matrix4; remove matrix stack | ✅ Complete |
| **Core Profile Switch** | True Core Profile, remove GLU | ⏳ **NEXT** |

---

## Remaining Tasks

### Text Rendering Bug (fix before P9)
| ID | Task |
|----|------|
| `text-bleed-fix` | **IN PROGRESS** — GL_LINEAR minification bleeds adjacent atlas rows into char tops. Fix applied but not yet verified: `glGenerateMipmap` + `GL_LINEAR_MIPMAP_LINEAR` + 1-texel V inset. Needs screenshot verification and commit. |

### Phase 9 — Core Profile Switch
| ID | Task |
|----|------|
| `p9-core-profile` | Change `wglCreateContextAttribsARB` flags from compat → core profile in `SkullbonezWindow.cpp` |
| `p9-remove-glu` | Remove `#include <gl\glu.h>` and `#pragma comment(lib, "glu32.lib")` from `SkullbonezCommon.h` |
| `p9-dead-code` | Remove dead FFP helper functions, unused state constants, display list references |
| `p9-final-verify` | Final build: 0 errors, 0 warnings. Full visual + perf verification. |

### After P9 — DX11 Port Prep
- Extract render abstraction layer (Option A, as agreed with user)

---

## Known Bugs
See `Copilot/Bugs.md` for full details.

1. **Water draws through sphere back faces** — water visible through the back faces of spheres intersecting the water surface; likely a depth test / draw-order issue
2. **Text UV bleeding (IN PROGRESS)** — faint ghost glyphs appear above character tops due to GL_LINEAR minification (text rendered ~20px on screen, atlas is 32px → 1.5× minification bleeds into adjacent atlas rows). Fix: `glGenerateMipmap` + `GL_LINEAR_MIPMAP_LINEAR` + 1-texel V inset. Applied to `SkullbonezText.cpp` but **not yet committed** — needs screenshot verification first.

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
| ID | Task |
|----|------|
| `member-rename-m_` | Rename all member variables to `m_` prefix convention: `m_pBlah` (pointers), `m_fBlah` (floats), `m_isBlah` (bools), `m_blah` (other). Use clang-tidy `readability-identifier-naming` with `--fix`. Do after Phase 9 when codebase is stable. |
| `dx11-abstraction` | Extract render abstraction layer for DX11 port (Option A — agreed with user) |
| `coding-standards` | Normalise line endings (`.gitattributes`) + add editor hints (`.editorconfig`) |

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

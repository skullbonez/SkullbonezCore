# SkullbonezCore — Session State

## Branch & Last Commit
- Branch: `main`
- Last commit: `08a0e36` — Remove SkullbonezData/water.jpg (missed in previous commit)

---

## Project Summary
A Windows C++/OpenGL 3D physics engine (2005) being migrated from Fixed Function Pipeline (OpenGL 1.x) to modern OpenGL 3.3 shader-based rendering. Work is on the `opus-rendering` branch with one commit per logical step.

---

## Overall Progress: Phases 1–6 Complete (33/46 tasks done)

| Phase | Description | Status |
|-------|-------------|--------|
| P1 Code Quality | `catch(char*)`, RAII, unique_ptr | ✅ Complete |
| P2 Warning Cleanup | /W4 zero warnings | ✅ Complete |
| P3 Compile-Time Hashes | FNV-1a texture/camera keys | ✅ Complete |
| P4 Eliminate Dynamic Allocation | vectors, unique_ptr, stack singletons | ✅ Complete |
| TH Test Harness | Scene mode, render tests, perf test, skore-render-test skill | ✅ Complete |
| P5 (shader) Foundation | GLAD, Matrix4, Shader, Mesh classes | ✅ Complete |
| P6 (shader) Shader Infra | GLSL shaders written | ✅ Complete |
| P7 (shader) Terrain | VBO mesh + lit_textured shader | ✅ Complete |
| P8 (shader) Skybox | VBO mesh + unlit_textured shader | ✅ Complete |
| P9 (shader) Spheres | Procedural sphere, GameModel Matrix4 | ✅ Complete |
| P10 (shader) Water & Shadows | Mesh + shaders, GL lifecycle fix | ✅ Complete |
| **P7 Text** | Font atlas + shader quads | ⏳ **NEXT** |
| P8 FFP Matrix Elimination | gluLookAt/Perspective, matrix stack | ⏳ Pending |
| P9 Core Profile Switch | True Core Profile, remove GLU | ⏳ Pending |

---

## Remaining Tasks (13 left)

### Phase 7 — Text Rendering (do this next)
| ID | Task |
|----|------|
| `p7-font-atlas` | Generate font texture atlas (runtime GDI approach — render 96 ASCII chars to GL texture at init) |
| `p7-text-render` | Replace `wglUseFontBitmaps` display lists in `SkullbonezText.cpp` with shader quad batch + `text.vert/frag` |
| `p7-text-verify` | Verify FPS/physics/render timing text renders correctly |

### Phase 8 — FFP Matrix Elimination
| ID | Task |
|----|------|
| `p8-camera-matrix` | Replace `gluLookAt` in `SkullbonezCameraCollection.cpp` with `Matrix4::LookAt`, expose `GetViewMatrix()` |
| `p8-projection-matrix` | Replace `gluPerspective` in `SkullbonezWindow.cpp` with `Matrix4::Perspective`, expose `GetProjectionMatrix()` |
| `p8-run-cleanup` | Remove all `glPushMatrix/glPopMatrix/glTranslatef/glScalef/glLoadIdentity` from `SkullbonezRun.cpp` |
| `p8-helper-cleanup` | Remove FFP lighting state from `SkullbonezHelper.cpp` (`glEnable(GL_LIGHTING)`, `glLightfv`, `glMaterialfv`, `glShadeModel`) |
| `p8-rigidbody-cleanup` | Remove `RotateBodyToOrientation()` / `glMultMatrixf` from `SkullbonezRigidBody.cpp` |
| `p8-ffp-verify` | Grep for zero remaining FFP calls: `glPushMatrix`, `glTranslatef`, `glRotatef`, `glScalef`, `glMultMatrixf`, `glLoadIdentity`, `glMatrixMode`, `gluLookAt`, `gluPerspective` |

### Phase 9 — Core Profile Switch
| ID | Task |
|----|------|
| `p9-core-profile` | Change `wglCreateContextAttribsARB` flags from compat → core profile in `SkullbonezWindow.cpp` |
| `p9-remove-glu` | Remove `#include <gl\glu.h>` and `#pragma comment(lib, "glu32.lib")` from `SkullbonezCommon.h` |
| `p9-dead-code` | Remove dead FFP helper functions, unused state constants, display list references |
| `p9-final-verify` | Final build: 0 errors, 0 warnings. Full visual + perf verification. |

---

## Known Bugs
No open bugs.

## Recent Session Work (this session)
- `fe5b4e4` — Planar water reflection system (FBO, clip plane, projective UV)
- `ea84a37` — Free-fly camera mode (F key toggle, WASD + mouse look)
- `1b52fa8` — Remove unused water texture (WATER_PATH / TEXTURE_WATER)
- `408b3ce` — Don't reset simulation while fly mode is active
- `deab1bd` — Fly mode polish: snap camera, Space advances sim, Shift sprints, no bounds
- `08a0e36` — Remove SkullbonezData/water.jpg

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
| Code cleanup plan/progress | `Copilot/Plans/plan-codecleanup-5.md` / `progress-codecleanup-5.md` |
| Dynamic alloc plan/progress | `Copilot/Plans/plan-eliminatedynamicallocation-4.md` / `progress-eliminatedynamicallocation-4.md` |
| Rendering performance plan | `Copilot/Plans/plan-renderingperformance-6.md` / `progress-renderingperformance-6.md` |
| Broadphase collision | `Copilot/Plans/add-broadphase-collision-opus-plan.md` / `-progress.md` |
| Ground shadows | `Copilot/Plans/add-ground-shadows-opus-plan.md` / `-progress.md` |
| Collision detection (opus) | `Copilot/Plans/improve-collision-detection-opus-plan.md` / `-progress.md` |
| Collision detection (sonnet) | `Copilot/Plans/improve-collision-detection-sonnet-plan.md` / `-progress.md` |
| Terrain rolling | `Copilot/Plans/improve-terrain-rolling-opus-plan.md` / `-progress.md` |

### Engine Source (key files for current work)
| What | Path |
|------|------|
| Text rendering | `SkullbonezSource/SkullbonezText.h` / `.cpp` |
| Camera (gluLookAt target) | `SkullbonezSource/SkullbonezCameraCollection.h` / `.cpp` |
| Window (gluPerspective target) | `SkullbonezSource/SkullbonezWindow.h` / `.cpp` |
| Main render loop | `SkullbonezSource/SkullbonezRun.h` / `.cpp` |
| GL helper / lighting state | `SkullbonezSource/SkullbonezHelper.h` / `.cpp` |
| Rigid body (glMultMatrixf) | `SkullbonezSource/SkullbonezRigidBody.h` / `.cpp` |
| Global config / hashes | `SkullbonezSource/SkullbonezCommon.h` |
| Matrix4 class | `SkullbonezSource/SkullbonezMatrix4.h` / `.cpp` |
| Shader class | `SkullbonezSource/SkullbonezShader.h` / `.cpp` |
| Mesh class | `SkullbonezSource/SkullbonezMesh.h` / `.cpp` |

### Scene & Shader Files
| What | Path |
|------|------|
| Perf test scene | `SkullbonezData/scenes/perf_test.scene` |
| Render test scene | `SkullbonezData/scenes/water_ball_test.scene` |
| Legacy smoke scene | `SkullbonezData/scenes/legacy_smoke.scene` |
| All GLSL shaders | `SkullbonezData/shaders/` |
| Text shaders (Phase 7) | `SkullbonezData/shaders/text.vert` / `text.frag` |

---

## Critical Technical Notes

### GL Context Lifecycle
`cRun` destructor MUST run before `wglDeleteContext`. Enforced via nested scope in `SkullbonezInit.cpp`:
```cpp
{ SkullbonezRun cRun; cRun.Run(); }  // destructor fires here
wglDeleteContext(...);                // then delete context
```
All `ResetGLResources()` calls in destructors depend on this ordering.

### Singleton Pattern
`SkyBox`, `TextureCollection`, `CameraCollection`, `Window` use static local singletons. After `Destroy()`, `ResetGLResources()` must be called before next use (not `Instance()` — that no longer re-initialises).

### Water Rendering Note
Water still reads the FFP modelview matrix via `glGetFloatv(GL_MODELVIEW_MATRIX)` because the 5 call sites use `glPushMatrix/glTranslatef` to position each copy. This will be cleaned up in Phase 8.

### Fly Mode (F key)
- Toggle with `F` — snaps to free camera, freezes physics + auto-cycle, removes terrain/XZ bounds
- WASD to move, mouse to look, Shift for 3× speed, Space to step simulation while paused
- Exit with `F` — restores cursor, bounds, terrain clamp, resumes cycle

### Perf Test
- 2×5s passes, 300 balls, seed 42, physics+text enabled
- Memory sampled every 60 frames via `GetProcessMemoryInfo` (psapi.lib)
- CSV: `Debug/perf_log.csv` — analysed by `Copilot/Skills/skore-render-test/analyze_perf.py`
- Regression thresholds: avg/p50 timing >10% = FAIL, p99/p99.9 >20% = FAIL, memory >5 MB growth = FAIL

### Build Environment
- MSBuild v17 / VS2022 Enterprise, Win32 x86, /W4 — must be 0 errors 0 warnings
- Kill `SKULLBONEZ_CORE.exe` before building (locks the exe → LNK1168)
- Python via `py` command (not `python`), Pillow installed
- Output: `Debug\SKULLBONEZ_CORE.exe`

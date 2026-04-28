# SkullbonezCore — Session State

## Branch & Last Commit
- Branch: `main`
- Last commit: `4db5cf4` — Add commit refs to narrowphase regression todo

---

## Project Summary
A Windows C++/OpenGL 3.3 Core Profile 3D physics engine (2005, fully modernized). All rendering uses shader-based pipeline. Profiler subsystem with debug overlay. Zero-allocation broadphase spatial grid. LOC counter in pipeline.

---

## Overall Progress: ALL PHASES COMPLETE + OPTIMIZATION PASS

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
| Code Quality Infra | clang-format pipeline verification | ✅ Complete |
| Formatting Cleanup | Banner comments removed, `#pragma once`, `inline static`, section comments | ✅ Complete |
| GL Optimization | Remove `glUseProgram(0)`, hoist constant uniforms to init | ✅ Complete |
| Broadphase Optimization | Zero-allocation flat hash table spatial grid (83% faster broadphase) | ✅ Complete |

---

## Recent Session Work (this session)

1. **Formatting cleanup** (`50488ac`): Removed 541 banner comments, `#pragma once` on all 33 headers, `inline static` for all out-of-line statics, `// --- Includes ---` / `// --- Usings ---` section comments, `MaxEmptyLinesToKeep: 2` in `.clang-format`, LOC counter added to pipeline.
2. **GL cleanup** (`40960d1`): Removed all 5 `glUseProgram(0)` calls, removed dead glScale comment. Attempted uniform location cache but reverted (2x regression from `std::string` temporaries in `unordered_map::find`).
3. **Constant uniform hoisting** (`1a8ba3b`): Moved all never-changing uniforms to shader init time across 6 shaders. Terrain render -17.6%, text -2.7%, water -3.4%.
4. **Broadphase optimization** (`ab41b60` → broken, fixed in `f07e164`): Replaced `unordered_map<int64_t, vector<int>>` + `unordered_set` with flat open-addressing hash table, generation stamping, linked-list entry pool, triangular bit array dedup. Result: broadphase -83%, total physics -63%.
5. **MAX_GAME_MODELS** (`7b3c338`, `0db4839`): Global constant in `SkullbonezCommon.h`, assert on exceed, spatial grid derives from it.

---

## Uncommitted Changes (DO NOT LOSE)
None.

---

## Backlog / Future Tasks
| ID | Task | Notes |
|----|------|-------|
| narrowphase-pair-regression | Investigate narrowphase 4x slowdown after broadphase rewrite | Went from 0.0105ms → 0.0442ms avg. Old working impl: `40960d1` (unordered_map). New impl: `f07e164` (flat hash table + linked-list pool). New broadphase appears to generate more candidate pairs. Need to instrument pair count, compare ordering effects on `timeRemaining` early-outs. Absolute cost still negligible (0.04ms) but should be understood. |
| replace-jpeg-lib | Replace ThirdPtySource/JPEG with stb_image | Current JPEG lib is ancient bundled source. stb_image is header-only, modern, supports more formats. |

---

## Known Bugs
| # | Bug | Area | Status |
|---|-----|------|--------|
| 1 | Water renders through to the back faces of spheres when they intersect the water surface | Rendering / Water | Open |

---

## Pipeline Rules (MANDATORY for every commit)
Every commit must include:
1. Updated reference images — run both render test scenes, overwrite `Copilot/Skills/skore-render-test/baseline_*.png`
2. Performance test artifact — run perf test, write JSON to `Copilot/Skills/skore-render-test/perf_history/{commit}.json`
3. Only send PNGs to the LLM for visual review **if local pixel comparison fails**
4. LOC count (informational, Step 5 of pipeline)

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
| LOC counter | `Copilot/Skills/loc_count.py` |

### Plans & Docs
| What | Path |
|------|------|
| **Main progress tracker** | `Copilot/Plans/progress.md` |
| **FFP migration master plan** | `Copilot/Plans/ffp-to-shader-migration.md` |
| Test harness design | `Copilot/Plans/test-harness.md` |

### Engine Source (key files)
| What | Path |
|------|------|
| Global config / hashes / MAX_GAME_MODELS | `SkullbonezSource/SkullbonezCommon.h` |
| Spatial grid (broadphase) | `SkullbonezSource/SkullbonezSpatialGrid.h` / `.cpp` |
| Game model collection (physics loop) | `SkullbonezSource/SkullbonezGameModelCollection.h` / `.cpp` |
| Main render loop | `SkullbonezSource/SkullbonezRun.h` / `.cpp` |
| Text rendering | `SkullbonezSource/SkullbonezText.h` / `.cpp` |
| Window (context creation) | `SkullbonezSource/SkullbonezWindow.h` / `.cpp` |
| Matrix4 class | `SkullbonezSource/SkullbonezMatrix4.h` / `.cpp` |
| Shader class | `SkullbonezSource/SkullbonezShader.h` / `.cpp` |
| Helper (sphere batch render) | `SkullbonezSource/SkullbonezHelper.h` / `.cpp` |

### Scene & Shader Files
| What | Path |
|------|------|
| Perf test scene | `SkullbonezData/scenes/perf_test.scene` |
| Render test scene | `SkullbonezData/scenes/water_ball_test.scene` |
| Legacy smoke scene | `SkullbonezData/scenes/legacy_smoke.scene` |
| All GLSL shaders | `SkullbonezData/shaders/` |

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

### Broadphase Spatial Grid
- Zero-allocation: flat open-addressing hash table (1024 buckets), linked-list entry pool (4096 entries), triangular bit array for pair dedup
- Generation stamping: no clearing needed, just bump counter each frame
- `MAX_GAME_MODELS` (512) in `SkullbonezCommon.h` controls max objects and pair bit array size
- Asserts fire if limit exceeded in `AddGameModel()`

### Constant Uniforms
All uniforms that never change per-frame are set once at shader creation time (not in the render loop). This includes light/material properties, texture sampler indices, identity model matrices, color tints, reflection strengths. Only view/projection/model matrices and dynamic values (time, clip plane, flags) are set per-frame.

### Build Environment
- MSBuild v17 / VS2022 **Professional**
- Win32 x86, /W4 — must be 0 errors 0 warnings
- Kill `SKULLBONEZ_CORE.exe` before building (locks the exe → LNK1168)
- Python via `py` command (not `python`), Pillow installed
- Output: `Debug\SKULLBONEZ_CORE.exe`
- Screen resolution: 1200×900
- No pre-commit hooks (removed — was broken, referencing Python 3.7 that doesn't exist)
- clang-format: `C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe`
- `.clang-format` has `MaxEmptyLinesToKeep: 2` — preserves double blank lines between functions

### Fly Mode (F key)
- Toggle with `F` — snaps to free camera, freezes physics + auto-cycle, removes terrain/XZ bounds
- WASD to move, mouse to look, Shift for 3× speed, Space to step simulation while paused
- Exit with `F` — restores cursor, bounds, terrain clamp, resumes cycle

### Perf Test
- 2×5s passes, 300 balls (configurable via `legacy_balls`), seed 42, physics+text enabled
- Memory sampled every 60 frames via `GetProcessMemoryInfo` (psapi.lib)
- CSV: `Debug/perf_log.csv` — analysed by `Copilot/Skills/skore-render-test/analyze_perf.py`
- Regression thresholds: avg/p50 timing >10% = FAIL, p99/p99.9 >20% = FAIL, memory >5 MB growth = FAIL
- LOC: ~8018 (logical lines, excludes blanks/comments/ThirdPtySource)

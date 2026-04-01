# SkullbonezCore — Session State

## Branch
`opus-rendering`

## Current State
Phases 1–6 of the FFP → Shader migration are complete. Phase 7 (text rendering) is next.

## What Was Done This Session
- Phase 6a: Water rendering migrated from FFP GL_QUADS to shader Mesh — commit `4c51895`
- Phase 6b: Shadow rendering migrated from FFP GL_TRIANGLE_FAN to shader Mesh — commit `b30fe70`
- Performance test infrastructure added — commit `4f85b6e`
- GL resource lifecycle fix (cRun destructor before wglDeleteContext) — commit `bbe5921`
- Perf regression thresholds added — commit `bbe5921`
- `skore-build-pass` renamed to `skore-build-pipeline` with mandatory baselines + perf artifacts per commit — commit `e708d6c`
- Repo restructured: `plans/` → `Copilot/Plans/`, `Skills/` → `Copilot/Skills/`
- `Copilot/Bugs.md` created

## Pipeline Rule (IMPORTANT)
Every commit must include:
1. Updated reference images (`Copilot/Skills/skore-render-test/baseline_*.png`)
2. Performance test JSON artifact (`Copilot/Skills/skore-render-test/perf_history/{commit}.json`)
3. Only send PNGs to the LLM model if local pixel comparison fails

## Remaining Work

### Phase 7 — Text Rendering
- `p7-font-atlas` — Generate font texture atlas (runtime GDI or pre-baked PNG)
- `p7-text-render` — Replace `wglUseFontBitmaps` display lists with shader quads + atlas
- `p7-text-verify` — Verify FPS/timing text renders correctly

### Phase 8 — FFP Matrix Elimination
- `p8-camera-matrix` — Replace `gluLookAt` with `Matrix4::LookAt`
- `p8-projection-matrix` — Replace `gluPerspective` with `Matrix4::Perspective`
- `p8-run-cleanup` — Remove all `glPushMatrix/Pop/Translate/Scale/LoadIdentity` from SkullbonezRun
- `p8-helper-cleanup` — Remove FFP lighting state from SkullbonezHelper
- `p8-rigidbody-cleanup` — Remove `RotateBodyToOrientation` / `glMultMatrixf`
- `p8-ffp-verify` — Grep verify zero remaining FFP calls

### Phase 9 — Core Profile Switch
- `p9-core-profile` — Switch `wglCreateContextAttribsARB` to true Core Profile
- `p9-remove-glu` — Remove `glu32.lib` and `<gl\glu.h>`
- `p9-dead-code` — Remove dead FFP helper functions and constants
- `p9-final-verify` — Final build + visual verification

## Known Bugs
See `Copilot/Bugs.md` for full details.
1. Flickering water on the horizon (z-fighting / precision issue)
2. Lighting: water too blue, terrain + skybox too dark vs original FFP

## Key File Locations
| What | Where |
|------|-------|
| Pipeline skill | `Copilot/Skills/skore-build-pipeline/skill.md` |
| Render test skill | `Copilot/Skills/skore-render-test/skill.md` |
| Perf analysis script | `Copilot/Skills/skore-render-test/analyze_perf.py` |
| Perf history artifacts | `Copilot/Skills/skore-render-test/perf_history/` |
| Reference baselines | `Copilot/Skills/skore-render-test/baseline_*.png` |
| Progress tracker | `Copilot/Plans/progress.md` |
| Migration plan | `Copilot/Plans/ffp-to-shader-migration.md` |
| Perf test scene | `SkullbonezData/scenes/perf_test.scene` |
| Shader files | `SkullbonezData/shaders/` |

## Technical Notes
- GL context lifecycle: `cRun` must be destroyed BEFORE `wglDeleteContext` — enforced via nested scope in `SkullbonezInit.cpp`
- Singletons (SkyBox, TextureCollection, etc.) use `ResetGLResources()` pattern for GL context reset
- Shadow shader uses `length(aPosition.xz)` for alpha fade from centre to edge
- Water quad reads FFP modelview matrix via `glGetFloatv(GL_MODELVIEW_MATRIX)` — still on compat profile
- Perf test: 2×5s passes, 300 balls, seed 42, memory via `GetProcessMemoryInfo`
- Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB

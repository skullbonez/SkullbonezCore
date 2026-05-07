# SkullbonezCore
Graphics/Physics simulation I wrote to break into the games industry back in 2005. Modernised through a series of shader migration and code quality phases.

![alt text](https://github.com/skullbonez/SkullbonezCore/blob/main/SkullbonezCore.png)

---

## Advancements Since 2005

The original codebase used the OpenGL Fixed-Function Pipeline (FFP), raw `new`/`delete`, `catch(char*)` error handling, and runtime string lookups. Here is what has been modernised:

### Tri-Renderer Architecture
The engine now supports three rendering backends, all producing visually identical output:
- **OpenGL 3.3 Core Profile** — GLSL shaders, GLAD loader, no FFP or GLU
- **DirectX 11** — D3D11 HLSL shaders, feature level 11_0
- **DirectX 12** — D3D12 HLSL shaders, explicit resource barriers, in-process debug validation via InfoQueue

The renderer is selected at launch via `--renderer gl` / `--renderer dx11` / `--renderer dx12` (default: GL). All three run in the CI pipeline on every commit and must produce no validation errors.

### Full Shader Pipeline
All rendering migrated from OpenGL FFP to explicit shaders:
- Terrain — VBO mesh + `lit_textured` GLSL shader
- Skybox — VBO mesh + `unlit_textured` shader
- Spheres — procedurally generated mesh, instanced via `Matrix4` orientation
- Water — vertex-animated wave displacement, FBO reflection pass, clip plane
- Shadows — projected quad mesh + `shadow` shader
- Text — GDI-generated font atlas texture, shader quad batch (replaced `wglUseFontBitmaps` display lists)
- All projection / view matrices via `Matrix4::Perspective` / `Matrix4::LookAt` (removed `gluPerspective` / `gluLookAt`)

### Modern C++
- `std::runtime_error` throughout — removed `catch(char*)` and `throw char*`
- `std::unique_ptr` for all heap ownership — no raw `new`/`delete`
- `std::vector` for variable-length collections
- Static local singletons (no `new`)
- Compiled at `/W4` — **zero warnings**

### Performance
- **Zero per-frame heap allocation** — broadphase collision uses a flat open-addressing hash table (1024 buckets) with generation stamping and a linked-list entry pool. No `std::unordered_map`, no `std::vector` clears.
- **Constant uniform hoisting** — uniforms that never change (light/material properties, sampler indices, identity matrices) are set once at shader init, not per-frame.
- Broadphase 83% faster than the original `unordered_map` implementation.

### Compile-Time String Keys
Textures and cameras are looked up by `constexpr` FNV-1a hashes (`TEXTURE_GROUND`, `CAMERA_FREE`, etc.), computed at compile time. No runtime string comparisons.

### CPU Profiler
`PROFILE_BEGIN` / `PROFILE_END` / `PROFILE_SCOPED` markers instrument every subsystem. A debug overlay renders per-frame CPU timing with traffic-light indicators (green / yellow / red against budget). Toggleable with **0**.

### Test Harness
- **Scene mode** (`--scene` / `--suite`): data-driven, deterministic, headless-compatible
- **Visual regression**: `glReadPixels` capture → PNG → pixel comparison against stored baselines
- **Performance regression**: per-frame CPU timing logged to CSV, analysed against prior commit, threshold-gated (>10% avg regression = pipeline failure)
- **Tri-renderer CI**: all three backends validated on every commit

---

## Build

```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=x64
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Profile /p:Platform=x64
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=x64
```

Output: `Debug\SKULLBONEZ_CORE.exe`, `Profile\SKULLBONEZ_CORE.exe`, or `Release\SKULLBONEZ_CORE.exe`

---

## Key Bindings

### Global

| Key | Action |
|-----|--------|
| **Esc** | Quit |
| **F** | Toggle fly mode (free camera). Freezes the camera auto-cycle and physics. Press again to exit. |
| **F2** | Save a scene snapshot to `Scenes/snapshot_XXXX.scene`. Captures full state for bug reproduction. |
| **F3** | Save a screenshot to `Screenshots/screenshot_XXXX.bmp`. |

### Fly Mode (press F to enter)

| Key | Action |
|-----|--------|
| **W / A / S / D** | Move camera forward / left / backward / right |
| **Mouse** | Look around |
| **Shift** | Hold for 3× movement speed |
| **Space** | Step the simulation one frame while paused |
| **F** | Exit fly mode — restores camera auto-cycle and cursor |

### Debug Toggles

| Key | Action | Default |
|-----|--------|---------|
| **1** | Freeze / unfreeze water animation | Animated |
| **2** | Toggle water reflection | ON |
| **3** | Toggle ocean wave displacement | ON |
| **9** | Toggle debug velocity vectors on balls | OFF |
| **0** | Toggle profiler overlay (frame timing text) | ON in legacy mode; OFF in scene mode |
| **G** | Cycle tracked ball index (scene mode only, when ball tracking is active) | — |

---

## Scene Snapshots

Press **F2** during a run to save the full scene state (ball positions, velocities, world settings, camera) to `Scenes/snapshot_XXXX.scene`.

To load a snapshot:

```bat
Debug\SKULLBONEZ_CORE.exe --scene Scenes\snapshot_0000.scene
```

The simulation loads **paused in fly mode**. Navigate with WASD/mouse to confirm the reproduction point, then press **F** to resume. This is the primary workflow for handing off a visual bug — save a snapshot at the moment of the bug, load it fresh to reproduce from exactly that state.

---

## Test Scenes

| Scene | Purpose |
|-------|---------|
| `SkullbonezData/scenes/water_ball_test.scene` | Visual regression — terrain, skybox, sphere, water, shadow |
| `SkullbonezData/scenes/legacy_smoke.scene` | Smoke test — 300 balls, physics on |
| `SkullbonezData/scenes/perf_test.scene` | Performance regression — 300 balls, 2×5 s passes |
| `SkullbonezData/scenes/physics_roll.scene` | Physics validation — rolling ball |

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/water_ball_test.scene
Profile\SKULLBONEZ_CORE.exe --suite SkullbonezData/scenes/render_tests.suite
```

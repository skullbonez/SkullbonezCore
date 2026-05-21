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

## Command-Line Arguments

| Argument | Values | Description |
|----------|--------|-------------|
| `--renderer` | `gl` \| `dx11` \| `dx12` | Select render backend (default: `gl`) |
| `--scene` | `<path>` | Load a scene file and run it, then exit |
| `--suite` | `<path>` | Load a `.suite` file and run all scenes in it |
| `--vsync` | `on` \| `off` | Override vsync (default: on) |
| `--legacy-physics` | _(flag)_ | Start with the legacy swept physics solver active |

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\water_ball_test.scene
Profile\SKULLBONEZ_CORE.exe --suite SkullbonezData\scenes\render_tests.suite --renderer dx11
Debug\SKULLBONEZ_CORE.exe --legacy-physics --vsync off
```

---

## Key Bindings

### Global

| Key | Action |
|-----|--------|
| **Esc** | Quit |
| **F** | Toggle fly mode (free camera). Freezes camera auto-cycle and physics. Press again to exit. |
| **R** | Cycle render backend at runtime: GL → DX11 → DX12 → GL. Preserves full simulation state. |
| **P** | Toggle physics solver: **Impulse** (spheres + boxes, unified contact) ↔ **Legacy** (spheres only, swept). In legacy mode boxes freeze and hide; they reappear on toggle back. |
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

### Debug / Display Toggles

| Key | Action | Default |
|-----|--------|---------|
| **0** | Toggle profiler overlay (frame timing, CPU/GPU markers, traffic-light budget) | ON (legacy); follows scene directive in scene mode |
| **1** | Freeze / unfreeze water animation | Animated |
| **2** | Toggle water reflection pass | ON |
| **3** | Toggle ocean wave displacement | ON |
| **4** | Toggle terrain visibility (hides mesh and shadow decals) | Visible |
| **5** | Toggle water visibility | Visible |
| **9** | Toggle debug velocity vectors on balls | OFF |
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

## Physics

The physics simulation uses rigid body dynamics with quaternion orientation tracking. All sphere-terrain and sphere-sphere collisions are resolved in a single unified contact solver.

### Unified Contact Solver

The original engine used a binary `m_isGrounded` flag to switch between two completely separate code paths — a kinematic "rolling mode" and an impulse-based "bounce mode". The transition between them was discontinuous and produced visible stutter (balls stopping and restarting mid-roll).

This was replaced with a single unified contact impulse solver where bouncing, rolling, and settling all emerge naturally from the same physics:

- **Normal impulse** `jₙ = -(1+e)·vₙ / kₙ` — handles bounce. Restitution `e` drops to zero below a velocity threshold to prevent micro-bouncing at rest.
- **Coulomb friction impulse** — couples linear and angular velocity. Rolling arises naturally: friction decelerates the ball's surface contact velocity, imparting angular velocity in the correct direction without any special-case rolling code.
- **Spin friction** — damps Y-axis spin (world-up axis) that Coulomb friction alone cannot remove, weighted by the terrain normal's upward component.
- `m_isGrounded`, `DampenAngularVelocity()`, and the three old terrain response methods (`SphereVsPlaneRollResponse`, `SphereVsPlaneLinearImpulse`, `SphereVsPlaneAngularImpulse`) were removed entirely.

Contact detection uses a proximity test from the plane equation (`pos.y − radius ≤ terrainHeight + ε`) reusing the polygon already located by the swept intersection — no redundant `GetTerrainHeightAt` call.

### Quaternion / Orientation Fix

The engine's quaternion multiply operator had non-standard signs inherited from the 2005 original, and `GetOrientationMatrix()` returns the transpose of the standard active-rotation matrix (passive/coordinate-transform convention). These two conventions interact: physical angular velocity `ω` is stored in physics space, but feeding it directly to `RotateAboutXYZ` produced visually backwards rotation.

Fix: omega is negated at the point of visual application (`UpdatePosition`, `UpdateRollPosition`, `GetOrientationMatrix`) so the physics solver reads and writes correct physical values while the visual output rotates in the expected direction.

### Sphere-Sphere Angular Convention Fix

`SphereVsSphereAngular` was a 2005 calibration hack with empirically tuned signs. After the terrain solver was rewritten to use proper physical convention, the sphere-sphere output was negated to bring it into alignment — both contact types now use the same sign convention throughout.

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

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

First time on this repo? Start with:

- `FIRST_TIME_SETUP.md` for Windows toolchain setup.
- `AGENTS.md` for the repository-wide AI agent contract.
- `Agentic/README.md` for handoff state, skills, plans, and debugging workflows.

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
| `--scene` | `<path>` | Load a single scene file. Quoted paths supported. |
| `--suite` | `<path>` | Load a `.suite` file (one scene path per line, `#` comments ignored) |
| `--vsync` | `on` \| `off` | Override vsync from `engine.cfg` |
| `--legacy-physics` | _(flag)_ | Start with the legacy swept sphere-only solver |
| `--switch-interval` | `<seconds>` | Auto-cycle renderer (GL → DX11 → DX12 → GL) every N seconds |
| `--time-scale` | `<float>` | Override simulation time multiplier for every scene (e.g. `0.25` = quarter speed) |
| `--fixed-step` | _(flag)_ | Force one physics tick per render frame (deterministic) for every scene |
| `--physics-log` | `<path>` | Write per-frame physics state to a CSV file _(Debug builds only)_ |
| `--gen-atlas` | `[path]` | Generate SDF font atlas to file and exit — no GPU context needed |

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\water_ball_test.scene
Profile\SKULLBONEZ_CORE.exe --suite SkullbonezData\scenes\render_tests.suite --renderer dx11
Debug\SKULLBONEZ_CORE.exe --legacy-physics --vsync off
Debug\SKULLBONEZ_CORE.exe --switch-interval 5 --suite SkullbonezData\scenes\render_tests.suite
Debug\SKULLBONEZ_CORE.exe --time-scale 0.5 --scene SkullbonezData\scenes\water_ball_test.scene
Debug\SKULLBONEZ_CORE.exe --fixed-step --time-scale 2.0 --scene SkullbonezData\scenes\water_ball_test.scene
Debug\SKULLBONEZ_CORE.exe --gen-atlas SkullbonezData\font_atlas.sdf
```

---

## Scene File Reference

Scene files are plain text. Lines beginning with `#` are comments; blank lines are ignored.

### Playback

| Directive | Description |
|-----------|-------------|
| `frames <N>` \| `frames unlimited` | Stop the scene after N rendered frames. Default: unlimited. |
| `exit_on_complete` | Exit the process when the frame count is reached. Without this, the HUD shows `- TEST COMPLETE` and the scene idles. |
| `screenshot_and_exit` | Capture frame 1 as `<SCENENAME>.bmp` then exit immediately. |
| `fixed_step` | One physics tick per render frame at `PHYSICS_FIXED_DT`. Fully deterministic frame-by-frame output. |

### Capture

| Directive | Description |
|-----------|-------------|
| `screenshot <path> frame <N>` | Save a screenshot at frame N. |
| `screenshot <path> ms <N>` | Save a screenshot after N milliseconds. |
| `screenshot_interval <dir> <N>` | Save a screenshot every N frames to `<dir>`. |

### Logging

| Directive | Description |
|-----------|-------------|
| `perf_log <path>` | Write per-frame CPU timing to a CSV file. |
| `perf_log_flush on\|off` | Flush the CSV on every write (default: `off`). |
| `perf_log_flush_interval <N>` | Flush every N frames. |
| `physics_log <path>` | Per-frame physics state CSV. _(Debug builds only)_ |

### Simulation

| Directive | Description |
|-----------|-------------|
| `physics on\|off` | Enable or disable physics simulation (default: `on`). |
| `physics_mode legacy\|solver` | Force a specific solver for this scene, overriding `--legacy-physics`. |
| `time_scale <F>` | Simulation time multiplier (default: `1.0`). |
| `seed <N>` | Random seed for reproducible spawning. Must be > 0. |
| `world <gravity> <fluidHeight> <fluidDensity>` | Override world gravity and fluid parameters. |

### Objects

| Directive | Fields | Description |
|-----------|--------|-------------|
| `ball <name> pos3 radius mass moment restitution [force3 forcePos3] [euler3]` | 8 / 11 / 14 / 17 | Spawn a sphere. Force and initial orientation are optional. |
| `box <name> pos3 half3 mass restitution [euler3] [vel3]` | 9 / 12 / 15 | Spawn an OBB. Orientation and initial velocity are optional. |
| `ball_state <name> pos3 vel3 angVel3 orient4 radius mass restitution inertia3` | 20 | Snapshot format — full dynamic state. Used by F2 save. |
| `legacy_balls <N>` | | Spawn N legacy-solver spheres at random positions. |
| `solver_balls <N>` | | Spawn N impulse-solver spheres at random positions. |
| `solver_boxes <N>` | | Spawn N impulse-solver OBBs at random positions. |

### Camera

| Directive | Description |
|-----------|-------------|
| `camera <name> pos3 view3 up3` | Define a named camera (up to `TOTAL_CAMERA_COUNT`). |
| `track_height <F>` | Height above the tracked ball for the tracking camera. |
| `auto_cycle_interval <F>` | Seconds between per-ball auto screenshots (used with `screenshot_interval`). |

### Rendering

| Directive | Description |
|-----------|-------------|
| `text on\|off` | Show or hide the HUD text overlay. |
| `text_only on\|off` | Suppress all 3D rendering — solid background with large text only. |
| `debug_vectors on\|off` | Show velocity vectors on balls. |
| `vsync on\|off` | Override vsync for this scene. |
| `pipeline_sync on\|off` | Override pipeline sync for this scene. |
| `roll_align on\|off` | Rolling alignment correction. |
| `water_hidden on\|off` | Hide the water surface. |
| `terrain_hidden on\|off` | Hide the terrain mesh and shadow decals. |
| `flat_slope <baseY> <slopeX> <slopeZ>` | Replace the heightmap terrain with a flat tilted plane. |

---

## Key Bindings

### Global

| Key | Action |
|-----|--------|
| **Esc** | Quit |
| **F** | Toggle fly mode (free camera). Freezes camera auto-cycle and physics. Press again to exit. |
| **N** | Toggle nudge mode: free camera + live simulation. Walk into balls/boxes to push them. |
| **R** | Cycle render backend at runtime: GL → DX11 → DX12 → GL. Preserves full simulation state. |
| **P** | Toggle physics solver: **Impulse** (spheres + boxes, unified contact) ↔ **Legacy** (spheres only, swept). In legacy mode boxes freeze and hide; they reappear on toggle back. |
| **Z** | Fire a ball from the camera. Shift = 3× speed. Recycles existing models from the pool. |
| **X** | Fire a box from the camera (impulse solver mode only). Shift = 3× speed. |
| **F2** | Save a scene snapshot to `Scenes/snapshot_XXXX.scene`. Captures full state for bug reproduction. |
| **F3** | Save a screenshot to `Screenshots/screenshot_XXXX.bmp`. |

### Fly / Nudge Mode (press F or N to enter)

| Key | Action |
|-----|--------|
| **W / A / S / D** | Move camera forward / left / backward / right |
| **Mouse** | Look around |
| **Shift** | Hold for 3× movement speed |
| **Space** | Step the simulation one frame (fly mode only — nudge mode keeps it running) |
| **F** | Exit to normal mode — restores camera auto-cycle and cursor |
| **N** | Toggle nudge mode on/off while in free camera

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

The simulation has two physics solvers — **Legacy** (the original 2005 code, spheres only) and **Impulse** (the modern sequential impulse solver, spheres + boxes). Toggle between them at runtime with **P**.

---

### Legacy Solver (2005 original)

The original physics was written as a portfolio piece to demonstrate understanding of physics fundamentals — it solves collisions analytically rather than iteratively.

**Swept sphere-sphere collision** — instead of testing overlap at end-of-frame (which misses fast or small objects), the solver casts a ray through time to find the exact moment of contact:

```
d = p_A − p_B          (relative center vector at frame start)
v = v_A − v_B          (relative velocity this frame)
R = r_A + r_B          (sum of radii — contact when |d + v·t| = R)

Expanding |d + v·t|² = R²:

    a·t² + 2b·t + c = 0
    where  a = v·v,  b = d·v,  c = d·d − R²

    t = (−b − √(b²−ac)) / a      (earliest root = first contact)
```

Early-out tests run cheapest-first: no relative motion, already overlapping, separating this frame, reachability cull, negative discriminant.

**Collision response** uses the standard 1D elastic/inelastic formula:

```
v_A' = ((m_A − e·m_B)·v_A + (1+e)·m_B·v_B) / (m_A + m_B)
v_B' = ((m_B − e·m_A)·v_B + (1+e)·m_A·v_A) / (m_A + m_B)
```

where `e` is the coefficient of restitution (0 = perfectly inelastic, 1 = perfectly elastic). Combined restitution is the geometric mean: `e = √(e_A · e_B)`.

**Limitations:** spheres only (boxes are skipped entirely in legacy mode); no angular coupling at sphere-sphere contacts; variable frame-time dt with no accumulator.

---

### Impulse Solver (current default)

Based on Erin Catto's *Iterative Dynamics with Temporal Coherence* (GDC 2005) — the same algorithm used by Box2D and Bullet. Handles both spheres and oriented bounding boxes (OBBs).

**Contact manifold:**
- Sphere → 1 contact at the lowest pole (center − radius × normal)
- Box → up to 8 vertex contacts, filtered to the deepest cluster

**Normal impulse** — computes the magnitude `j_n` to push two bodies apart:

```
            −(1 + e) · v_n
j_n = ─────────────────────────────────────────────────────────
       1/m_A + 1/m_B + n̂·(I_A⁻¹(r_A×n̂)×r_A) + n̂·(I_B⁻¹(r_B×n̂)×r_B)
```

where:
- `v_n` = relative velocity at the contact point along the normal
- `r_A`, `r_B` = vectors from each center-of-mass to the contact point
- `I⁻¹` = inverse inertia tensor (scalar for spheres, world-space rotated for boxes)
- The denominator is the **effective mass** — the combined resistance of both bodies to linear *and* angular acceleration at the contact

The accumulated impulse is clamped ≥ 0 (push only, never pull), making the constraint one-sided.

**Friction impulse** (Coulomb model) — applied along the two contact tangent axes:

```
|j_t| ≤ μ · j_n
```

Friction naturally produces correct rolling: it decelerates the contact surface velocity and imparts angular velocity in the right direction with no special-case rolling code.

**Additional passes:**
- **Baumgarte stabilisation** — a small bias term corrects residual penetration each frame without introducing energy
- **Position correction** — projects out 40% of remaining penetration directly after the velocity solve
- **Gravitational tipping torque** — applies a small restoring torque to boxes balanced on an edge or vertex
- **Sleep** — zeroes velocity when both linear and angular fall below threshold

---

### Time Step

The physics clock runs at a **fixed 120 Hz** regardless of rendering frame rate:

```
accumulator += frame_dt

while accumulator >= PHYSICS_FIXED_DT:   // PHYSICS_FIXED_DT = 1/120 s ≈ 8.3 ms
    RunPhysics( PHYSICS_FIXED_DT )
    accumulator -= PHYSICS_FIXED_DT
```

This decouples rendering from simulation — a 60 fps render frame runs two physics ticks; a 30 fps frame runs four. The simulation produces the same result regardless of frame rate, which is critical for deterministic visual regression testing.

Scene files can override this with `fixed_step` — one physics tick per render frame at exactly `PHYSICS_FIXED_DT` — giving fully deterministic, frame-index-reproducible output for test scenes.

---

### Switching Solvers at Runtime

Press **P** to toggle solvers mid-simulation. The solver switch:
- Resets the profiler tree (the call hierarchy changes)
- Boxes freeze in legacy mode (only spheres are simulated); they unfreeze when switching back
- All sphere velocities, positions, and angular state are preserved across the switch



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

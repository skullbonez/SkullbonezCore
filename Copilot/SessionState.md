# SkullbonezCore — Session State

> # 🚫 NEVER KILL PROCESSES BY NAME
> **NEVER use `Stop-Process -Name`, `taskkill /IM SKULLBONEZ_CORE.exe`, or any name-based kill.**
> Multiple agents run independent copies of SKULLBONEZ_CORE.exe from different repo folders simultaneously.
> Killing by name will terminate the wrong instance and corrupt another agent's pipeline run.
>
> **Always kill by PID only:** `Stop-Process -Id <PID>` — get the PID from `$proc.Id` when you launched it with `Start-Process -PassThru`.

> # ⏱️ ALL LARGE TASKS MUST BE TIMED
> **Before starting any large task (multi-file refactor, new feature, phase implementation, pipeline run):**
> 1. Note the wall-clock start time
> 2. At completion, record: **elapsed time**, **input tokens**, **output tokens**
> 3. Log it in the session summary so Simon can track cost and velocity
>
> *This applies to: pipeline runs, feature implementations, debugging sessions, refactors, any task expected to take >2 minutes or >10 tool calls.*

## Branch & Last Commit
- Branch: `opt/optimizations-pass`
- Last commit on main: `3ab3e2e` — camera tween reflection fix
- Working branch HEAD: `988b193` — opt-10 immutable per-ball physics cache

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

0. **Optimization pass — opt-11 reduce duplicate collision vector math** (`pending commit`):
   - Refactored `CollisionResponse::RespondCollisionTerrain` to remove duplicated tangent/normal vector decomposition work in the hot collision-response path.
   - Friction solve now computes tangent velocity from a single cached contact-normal projection and uses squared-length checks before the one required `sqrtf`.
   - Rolling-friction path now computes tangent velocity/speed once and reuses that normalized direction, avoiding duplicate `velocity - normal*(velocity·normal)` construction and normalisation work.
   - Roll-alignment gate now consumes the post-friction tangent-speed squared computed from one explicit tangent decomposition.
   - Full pipeline re-run completed with new archive `TestOutput/012_988b193/`.
   - CPU delta vs opt-10 baseline (`011_f087af5`) was mixed/noisy:
     - GL: `Frame` avg **+0.5%**, `Frame/Physics` avg **+1.8%**, `Frame/Physics/Terrain` avg **-0.5%**
     - DX11: `Frame` avg **+0.3%**, `Frame/Physics` avg **+1.1%**, `Frame/Physics/Narrowphase` avg **+0.0%**
     - DX12: `Frame` avg **-2.7%**, `Frame/Physics` avg **-0.4%**, `Frame/Physics/Terrain` avg **-0.5%**
     - No perf_compare hard regressions flagged.

1. **Optimization pass — opt-10 immutable per-ball physics cache** (`988b193`):
   - Added `GameModel::BallPhysicsCache` and now precompute immutable sphere/body values once:
     - radius, radius², volume, inverse volume, projected area, drag coefficient
     - mass, inverse mass, rotational inertia, inverse rotational inertia
   - `GameModel::AddBoundingSphere` now builds this cache once and hot getters (`GetMass`, `GetInvertedMass`, `GetRotationalInertia`, `GetInvertedRotationalInertia`, `GetBoundingRadius`) are now cache-backed.
   - Replaced per-frame submerged-volume shape calls with direct analytic sphere evaluation using cached radius/inverse-volume in `GetSubmergedVolumePercent`.
   - `CollisionResponse` terrain and sphere-sphere paths now consume cached immutable mass/inertia/radius data instead of repeatedly traversing `RigidBody` and shape helper layers.
   - Full pipeline re-run completed with new archive `TestOutput/011_f087af5/`.
   - CPU delta vs opt-09 baseline (`010_27ebbec`):
     - GL: `Frame` avg **-1.1%**, `Frame/Physics` avg **-1.2%**, `Frame/Physics/Narrowphase` avg **-4.2%**, `Frame/Physics/Terrain` avg **-3.8%**
     - DX11: `Frame` avg **+0.8%** (noise), `Frame/Physics` avg **-0.5%**, `Frame/Physics/Narrowphase` avg **-5.0%**
     - DX12: `Frame` avg **+2.3%** (noise), `Frame/Physics` avg **+0.1%** (noise), `Frame/Physics/Narrowphase` avg **-6.9%**, `Frame/Physics/Terrain` avg **-4.2%**
     - No perf_compare hard regressions flagged.

2. **Optimization pass — opt-09 sphere-only fast path** (`f087af5`):
   - Added explicit sphere accessors on `GameModel` (`GetBoundingSphere() const/non-const`) so hot physics code can bypass `std::variant` visitor dispatch in the current sphere-only workload.
   - Replaced variant-dispatch calls in hot paths with direct `BoundingSphere` calls:
     - model-model collision test path (`GetModelCollisionTime`)
     - overlap pushout radii (`StaticOverlapResponseGameModel`)
     - model matrix path (`GetModelMatrix`)
     - terrain bottom offset and debug terrain placement
     - submerged volume percent, projected surface area, drag coefficient, and volume calculations.
   - Kept abstraction safety by preserving `CollisionShape` ownership as a variant-backed member while using sphere-specialized access only in per-frame/hot loops.
   - Full pipeline re-run completed with new archive `TestOutput/010_27ebbec/`.
   - CPU delta vs opt-08 baseline (`009_74ee3a6`):
     - GL: `Frame` avg **-2.5%**, `Frame/Physics` avg **-4.5%**, `Frame/Physics/ApplyForces` avg **-10.6%**
     - DX11: `Frame` avg **-0.8%**, `Frame/Physics` avg **-2.9%**, `Frame/Physics/ApplyForces` avg **-9.0%**
     - DX12: `Frame` avg **-0.5%**, `Frame/Physics` avg **-4.2%**, `Frame/Physics/ApplyForces` avg **-10.8%**
     - No perf_compare hard regressions flagged.

3. **Optimization pass — opt-08 roll/orientation correction gating** (`27ebbec`):
   - Added configurable roll-alignment gates in terrain collision response (`RespondCollisionTerrain`):
     - `roll_align_enabled`
     - `roll_align_interval`
     - `roll_align_min_speed`
     - `roll_align_min_omega`
     - `roll_align_perp_tolerance_deg`
     - `roll_align_max_correction_deg`
   - Pole/orientation correction now skips low-energy contacts and decimates expensive alignment work by interval; correction step is also clamped by max correction angle.
   - Added scene-level override directive `roll_align on|off` in `TestScene`, wired through `SkullbonezRun::LoadScene`.
   - Set `perf_test.scene` to `roll_align off` so benchmark runs use the cheaper path by default without disabling high-fidelity correction globally.
   - Full pipeline re-run completed with archive `TestOutput/009_74ee3a6/` refreshed.
   - CPU delta vs opt-07 baseline (`008_84d2ef0`) was mixed/noisy in this run (no perf_compare hard regressions).

4. **Optimization pass — opt-07 remove per-frame RunPhysics allocations** (`74ee3a6`):
   - Added retained per-model frame buffers as `GameModelCollection` members:
     - `m_timeRemaining` (`std::vector<float>`)
     - `m_groundedThisFrame` (`std::vector<uint8_t>`)
   - `RunPhysics` now reuses these vectors each frame via `assign(...)` instead of constructing temporary `std::vector<float>` and `std::vector<bool>` instances on every frame.
   - Constructor now reserves these buffers to `MAX_GAME_MODELS`; `Clear()` resets them alongside the model collection.
   - Replaced `std::vector<bool>` hot-path grounded flags with byte flags to avoid bit-proxy overhead.
   - Full pipeline re-run completed with new archive `TestOutput/008_84d2ef0/`.
   - CPU delta vs opt-06 baseline (`007_6dcfcce`):
     - GL: `Frame/Physics/ApplyForces` avg **+5.3%** (p50 **+4.6%**), `Frame` avg **+1.6%** (no perf_compare regressions)
     - DX11: `Frame/Physics/ApplyForces` avg **+3.5%**, `Frame` avg **+0.3%** (no perf_compare regressions)
     - DX12: `Frame/Physics/ApplyForces` avg **+3.7%**, `Frame` avg **+1.6%** (no perf_compare regressions)

5. **Optimization pass — opt-06 active bucket tracking in spatial grid** (`84d2ef0`):
   - `SpatialGrid` now tracks active bucket indices for the current generation:
     - added `activeBuckets[TABLE_SIZE]` and `activeBucketCount`,
     - `FindOrCreate` appends a bucket index when a stale slot is first claimed in the frame.
   - `GetCandidatePairs` now iterates only active buckets instead of scanning all `TABLE_SIZE` buckets each frame.
   - Replaced fixed `cellIndices[64]` staging with `cellIndices[MAX_GAME_MODELS]` and explicit overflow guard/assert, eliminating silent bucket truncation when a cell is dense.
   - Full pipeline re-run completed with new archive `TestOutput/007_6dcfcce/`.
   - CPU delta vs opt-05 baseline (`006_12f5ac0`):
     - GL: `Frame/Physics/Broadphase` avg **-17.2%** (p50 **-16.9%**)
     - DX11: `Frame/Physics/Broadphase` avg **-15.8%** (p50 **-16.2%**)
     - DX12: `Frame/Physics/Broadphase` avg **-16.6%** (p50 **-16.6%**)

6. **Optimization pass — opt-05 broadphase cell sweep/tuning** (`6dcfcce`):
   - Ran focused `broadphase_cell` sweeps on `perf_test.scene` for `{8, 11, 16, 24, 32}` and measured `Frame/Physics/Broadphase + Frame/Physics/Narrowphase`.
   - `broadphase_cell = 24.0` gave the best combined CPU cost in the sweep and remained best in follow-up tri-renderer checks against nearby values (`16`, `24`, `32`).
   - Updated defaults to use `24.0`:
     - `SkullbonezConfig::broadphaseCell` default in `SkullbonezConfig.h`
     - `broadphase_cell` in `SkullbonezData/engine.cfg`
   - Full pipeline re-run completed with new archive `TestOutput/006_12f5ac0/`.
   - CPU delta vs opt-04 baseline (`005_7893de3`):
     - GL: `Frame` avg **-20.6%**, `Frame/Physics` avg **-53.2%**, `Frame/Physics/Broadphase` avg **-87.7%**, `Frame/Physics/Narrowphase` avg **-52.0%**
     - DX11: `Frame` avg **-31.3%**, `Frame/Physics` avg **-53.7%**, `Frame/Physics/Broadphase` avg **-88.0%**, `Frame/Physics/Narrowphase` avg **-56.5%**
     - DX12: `Frame` avg **-17.1%**, `Frame/Physics` avg **-52.3%**, `Frame/Physics/Broadphase` avg **-87.0%**, `Frame/Physics/Narrowphase` avg **-45.1%**

7. **Optimization pass — opt-04 narrowphase early-outs** (`12f5ac0`):
   - Reworked `BoundingSphere::CollisionDetect` to use a relative-motion quadratic solve with several low-cost rejects before the discriminant path.
   - Added early-outs for:
     - negligible relative movement,
     - already-overlapping pairs (defer to static overlap resolution path),
     - separating relative velocity,
     - unreachable contact this frame via swept reach-radius cull.
   - Removed per-call vector normalization and displacement-based solve path; narrowphase now solves directly in `t` over relative movement.
   - Full pipeline re-run completed with new archive `TestOutput/005_7893de3/`.
   - CPU delta vs opt-03 baseline (`004_87cc00d`):
     - GL `Frame/Physics/Narrowphase` avg **-9.1%** (p50 **-9.6%**)
     - DX11 `Frame/Physics/Narrowphase` avg **-11.5%** (p50 **-10.2%**)
     - DX12 `Frame/Physics/Narrowphase` avg **-10.5%** (p50 **-11.1%**)

8. **Optimization pass — opt-03 terrain collision cache** (`7893de3`):
   - Added per-quad terrain collision cache in `Terrain`:
     - precomputed plane + upward normal for both triangle A/B in each terrain quad,
     - one-time cache build after terrain postings are translated.
   - Added fast terrain query APIs:
     - `GetTerrainHeightAndPlaneAt(...)` for physics collision detection,
     - `GetTerrainHeightAndNormalAt(...)` now also uses cached data.
   - `GameModel::GetTerrainCollisionTime` now fetches the collision plane and height from the cache instead of calling `LocatePolygon` + `ComputePlane` per frame.
   - Added analytic flat-slope fast handling in the same query path using a precomputed plane/normal (no fabricated triangles for physics queries).
   - Full pipeline re-run completed for this change with new archive `TestOutput/004_87cc00d/`.

9. **Optimization pass — opt-02 sync stall + V-Sync control** (`87cc00d`):
   - Added runtime controls for forced pipeline sync and V-Sync:
     - Engine config keys: `force_pipeline_sync`, `vsync_enabled`
     - Scene directives: `pipeline_sync on|off`, `vsync on|off`
   - `SkullbonezRun` now gates `Frame/PipelineSync` (`Gfx().Finish()`) via config/scene policy instead of forcing it every frame.
   - Added backend-level V-Sync controls in `IRenderBackend` and implemented them for GL/DX11/DX12; removed hardcoded `wglSwapIntervalEXT(1)` from window init.
   - DX12 descriptor heap handling updated for multi-frame flight safety without full-frame CPU/GPU stalls:
     - transient SRV slots are now partitioned per frame allocator (`MAX_TRANSIENT_SRVS` per allocator),
     - shader-visible heap size increased accordingly,
     - transient allocation now enforces per-allocator bounds.
   - Full pipeline re-run completed; DX12 InfoQueue validation is back to 0 errors after the descriptor fix.

10. **Optimization pass — opt-01 vector log gating** (`0008eb9`):
   - Removed the unconditional `#define VECTOR_LOG_ENABLED` path from `SkullbonezRun::UpdateLogic`.
   - Added scene directives in `TestScene` for vector diagnostics:
     - `vector_log on|off` (default off)
     - `vector_log_interval <N>` (default 6)
     - `vector_log_path <path>` (default `Debug/vector_log.csv`)
     - `vector_log_flush on|off` (default off)
   - Added explicit `SkullbonezRun` runtime state and file-handle lifecycle for vector logs (close on scene reload/destructor, throw on open failure).
   - Pipeline run completed (build + tri-renderer suite + baseline update + perf analysis/archive).
   - CPU delta vs pre-change baseline:
     - GL `Frame/Physics` avg **-46.1%**, `Frame/Physics/Broadphase` avg **-60.9%**
     - DX11 `Frame/Physics` avg **-47.3%**, `Frame/Physics/Broadphase` avg **-61.8%**
     - DX12 `Frame/Physics` avg **-47.7%**, `Frame/Physics/Broadphase` avg **-61.2%**

<<<<<<< codex/fix-camera-transition-distortion
1. **Camera tween reflection fix** (`3ab3e2e`):
   - Root cause: `DrawPrimitives()` used the selected camera endpoint for reflection eye/view/up while `CameraCollection::SetCamera()` rendered the main frame from an interpolated tween camera. During transitions, the water reflection texture and sampling VP therefore disagreed.
   - Fix: `CameraCollection` now records the exact camera state used to build `m_currentViewMatrix` each frame and exposes render-camera getters. Water reflection mirror setup now uses those render-camera vectors for the FBO/DXR reflection pass.
   - Validation in this Linux container: `clang-format` and `git diff --check`; full Windows renderer pipeline unavailable here.

2. **SIMD/SSE math optimization pass** (`pending`):
=======
0. **Natural contact solver improvement plan** (`pending`):
   - Added `Copilot/Plans/physics-natural-contact-solver-plan.md`, a detailed physics rewrite roadmap for replacing the remaining rolling/orientation/sphere-sphere hacks with a unified sequential impulse contact solver.
   - Plan covers contact-point velocity, effective mass, normal/restitution impulses, Baumgarte/split impulse stabilization, static/dynamic tangent friction, spin friction, rolling resistance, sphere-sphere angular impulse cleanup, fixed timesteps, validation scenes, and phased implementation.
   - Documentation-only change; no engine behavior changed.

1. **SIMD/SSE math optimization pass** (`pending`):
>>>>>>> main
   - `Matrix4::operator*`: SSE column-outer-product (`#ifdef _DEBUG` scalar loop preserved for debuggability)
   - `Matrix4::FromQuaternion`: replaced RotationMatrix intermediary + 3 basis-vector extractions with direct 9-product formula
   - `Matrix4::ModelFromQuaternionYaw90`: new fused static — T(worldPos) * FromQuat(q) * RotY90 * Scale(r) in ~40 FP ops vs ~500; `#ifdef _DEBUG` step-by-step compose path preserved
   - `GameModel::GetModelMatrix`: single `ModelFromQuaternionYaw90` call
   - `GameModel::GetOrientationUp`: 7-product direct quaternion formula (no matrix build+extract)
   - `BoundingSphere::GetModelMatrix`: 3 SSE scale passes + direct col3 write; `#ifdef _DEBUG` 3-mul chain preserved
   - `Matrix4::ShadowFromNormal`: fused T*RotFromUpToN*Scale — Rodrigues direct from N (1 sqrtf, 0 acosf/cosf/sinf); `#ifdef _DEBUG` acosf round-trip path shows what is eliminated
   - `Terrain::GetTerrainHeightAndNormalAt`: single `LocatePolygon` call returning both height and normal (eliminates double polygon walk in shadow loop)
   - All optimized code paths given verbose derivation comments with ASCII math
   - **Result**: `Balls/MatrixBuild` −73.5%, `Render` −9.6%, `Shadows/MatrixBuild` −15.0%, `Reflection/Balls` −34.3% vs pre-SSE baseline

3. **Floating ball orientation snap fix + SkullbonezLog** (`c626d2e`):
   - Root cause: `pole_align` in `RespondCollisionTerrain` applied uncapped rotation corrections (up to 90°/frame) to submerged balls touching terrain
   - Fix: skip pole alignment entirely when ball centre is below fluid surface — meaningful only on land
   - Ablation tested: water guard alone eliminates all snaps across 1800 frames; no-fix baseline reproduced 7 snaps including a 56.1° jump
   - Added `SkullbonezLog` singleton: `Log().Writef(fileName, fmt, ...)` — lazy file creation per unique filename, `fflush` on every write, auto-close on shutdown; release builds are inline no-ops
   - `Log().Writef()` documented in `agents.md` as canonical debug logging pattern
   - Added `float_snap_test.scene` reproducer (8 floating balls, 1800 frames)
   - Removed `rollAlignRate` config option (rate-limiter approach superseded by fix)
   - Added Step 8.5 (Update SessionState) to `skore-build-pipeline` skill
    
4. **DX12 GPU compute mip generation** (`c6027b3`):
   - New compute shader `generate_mips.hlsl`: cs_5_0, 8×8 thread groups, up to 4 mips/dispatch, NPOT handling (4 cases via SrcDimension bits), group-shared memory reduction for mips 2-4
   - `InitGenMipsPipeline()` + `GenerateMipsGPU()` in DX12 backend — replaces CPU-side mip generation
   - **Critical bug fix**: both DX12 static samplers in the main render root signature had `MaxLOD = 0` (zero-init default of `D3D12_STATIC_SAMPLER_DESC samplers[2] = {}`), silently clamping all sampling to mip 0. Fixed to `D3D12_FLOAT32_MAX`. Also added explicit `MaxAnisotropy = 1`.
   - New scene directives: `water_hidden on/off`, `terrain_hidden on/off` (SkullbonezTestScene + SkullbonezRun)
   - New test assets: `SkullbonezData/scenes/terrain_compare.scene`, `TestOutput/compare_terrain.py` (pixel-level DX11 vs DX12 comparison with RMSE, heatmaps)
   - Window default resolution changed to 960×540 (1/4 of 2560×1440)
   - Text anchoring fixed: top-left, top-right, bottom-right corners at new aspect ratio
   - `text_only` scene mode + `text_test.scene` for diagnosing text at large display sizes
   - Descender cut-off fix: full 48px cell height sampled in `RenderTextInternal`
   - Offline SDF atlas generation: GDI renders at 6× → Felzenszwalb-Huttenlocher EDT → 6×6 box-filter → binary `.sdf` file
   - `--gen-atlas <path>` CLI flag: generates atlas before window/GPU init, then exits
   - SDF shaders (GL + HLSL): `smoothstep` with `fwidth`/`ddx+ddy` adaptive AA
   - Font texture upload changed to bilinear filtering (was nearest-neighbour — root cause of jagged edges)
   - `--scene` / `--suite` argument parsing fixed: both now tokenize correctly so `--renderer dx11` after `--scene path` works
   - Regenerate atlas: `.\Debug\SKULLBONEZ_CORE.exe --gen-atlas SkullbonezData/font_atlas.sdf`

---

## Uncommitted Changes (DO NOT LOSE)
- Pending opt-11 collision-math cleanup changes in:
  - `SkullbonezSource/SkullbonezCollisionResponse.cpp`
- Pipeline artifacts refreshed in:
  - `TestOutput/012_988b193/`
  - `TestOutput/baselines/baseline_{gl,dx11,dx12}_legacy_smoke.png`

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
| **CPU profiler skill** | `Copilot/Skills/skore-cpu-profiler/skill.md` |
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
- Screen resolution: 960×540 (1/4 of 2560×1440)
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
- Regression thresholds: avg/p50 timing >10% = FAIL, memory >5 MB growth = FAIL
- LOC: ~16584 (logical lines, excludes blanks/comments/ThirdPtySource)

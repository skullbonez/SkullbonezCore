# SkullbonezCore Session History

This file is archival. Do not load it by default at the start of an agent session.

## Completed Modernization Themes

- OpenGL fixed-function rendering was replaced with shader-based GL 3.3 Core Profile rendering.
- DirectX 11 and DirectX 12 backends were added for tri-renderer parity.
- Raw ownership was migrated toward RAII and `std::unique_ptr`.
- `catch(char*)` and `throw char*` patterns were removed in favor of `std::runtime_error`.
- Compile-time FNV-1a hashes replaced runtime string lookups for common texture and camera keys.
- `/W4` zero-warning validation is required.
- Scene, visual, physics, and performance validation scripts now live in `tools/`.
- Broadphase collision was rewritten as a zero-allocation flat hash table with generation stamping.
- CPU profiler markers and a debug overlay were added.

## Recent Historical Notes

These notes were formerly mixed into `SessionState.md`; keep adding detailed chronology here instead of expanding the active handoff file.

### Physics Regression System

- Physics log code was moved behind `_DEBUG` guards.
- `exit_on_complete` was added for deterministic validation scenes.
- Physics and perf baselines were consolidated into `TestOutput/baselines/`.

### Renderer Hot Switch

- Returning from DXGI to GL recreates the HWND to avoid stale DWM composition behavior.
- Terrain render resources are reset and rebuilt after renderer switches.
- DX11 teardown clears state and flushes for clean DXGI release.

### DX12 Upload Buffer Race

- DX12 upload memory was partitioned per frame allocator to avoid CPU/GPU overwrite races when forced pipeline sync is disabled.
- Validation focus: renderer parity and DX12 InfoQueue must stay clean.

### DX12 GPU Timer Readback

- Timestamp readback was decoupled from `Finish()` so GPU timing still works when `pipeline_sync` is off.
- `Present()` opportunistically consumes completed readbacks and arms the next fence.

### Optimization Passes

- Perf log flush controls reduced diagnostic I/O noise.
- Shadow instance assembly was pre-sized and written by direct index.
- Collision response vector math was de-duplicated.
- Immutable sphere physics data was cached on each model.
- Sphere-only hot paths bypass variant dispatch where the workload is known to be sphere-based.
- Roll-alignment work was gated and disabled in the perf scene.
- Per-frame `RunPhysics` vectors were retained and reused.
- Spatial grid active bucket tracking avoided scanning all buckets.
- Broadphase cell size was tuned to `24.0`.
- Narrowphase gained relative-motion early-outs.
- Terrain collision planes were cached.
- Pipeline sync and vsync became runtime/scene controls.
- Vector logging was opt-in rather than unconditional.

## Historical Plans and Audits

Use these files when their area matters:

- `Agentic/Plans/progress.md`
- `Agentic/Plans/ffp-to-shader-migration.md`
- `Agentic/Plans/physics-natural-contact-solver-plan.md`
- `Agentic/Plans/agentic-friendliness-implementation.md`
- `Agentic/Audits/`

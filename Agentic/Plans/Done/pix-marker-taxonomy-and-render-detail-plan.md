# PIX Marker Taxonomy And Render Detail Plan

Date: 2026-06-23
Status: Implemented on branch `nightrunner-23rd-june`
Impact area: platform profiler, DX12 PIX markers, runtime render instrumentation, physics worker diagnostics
Validation: `tools\validate_format.bat` passed; `tools\validate_dx12_renderer.bat` passed with DX12 validation errors: 0 and matching screenshots; explicit `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --renderer dx12 --vsync off --frames 2 --scene SkullbonezData\scenes\solver_smoke.scene.json` smoke passed with no profiler-stack/error strings. `tools\validate_full.bat` was run and failed at the physics CSV compare; the same 1193-line physics mismatch starting at line 11742 was reproduced on clean `main` in a detached probe worktree, so it is recorded as a pre-existing validation blocker rather than a regression from this plan.

## Goal

Make PIX captures easier to read at a glance:

- color markers by engine domain instead of hash-random colors,
- distinguish worker/chunk work from main-thread work with a small suffix,
- show useful render pass detail in PIX CPU timing captures,
- keep in-engine profiler marker names, perf CSVs, and UI rows stable unless a
  later slice intentionally changes them.

## Implementation Notes

- Semantic domain colors and execution-context suffixes now live in
  `PlatformProfiler`; detailed suffix/color classification is enabled for
  explicit platform-profiler captures so default Debug/Profile validation keeps
  its lightweight marker behavior.
- Worker scopes can emit `_Worker` PIX ranges, GPU scopes can emit CPU `_Record`
  mirrors, and DX12 command-list events can emit `_GPU` names during explicit
  detailed captures. Stack-close bookkeeping tracks what actually opened, so
  disabling marker emission mid-scope does not create false end-without-begin
  logs.
- Render detail was added for DebugOverlay, VolumetricLight, Tonemap, selected
  subdraws, and the `TransparentBalls` pass audit. The extra render-detail GPU
  profiler scopes are guarded by explicit detailed captures to avoid perturbing
  ordinary validation timing.

The immediate problem is visible in PIX captures: physics has rich nested spans,
while render often appears as a broad `Frame/Render` block with little CPU-track
detail. Worker-lane physics spans also look like plain `Frame/Physics` spans, so
they are harder to identify quickly.

## Current Findings

### Color Is Currently Hash-Based

`PlatformProfiler::ColorForMarker()` currently derives RGB from the marker hash.
This gives stable colors, but not meaningful colors. Physics, render, UI, replay,
and worker scopes can all receive visually unrelated colors.

Relevant files:

- `SkullbonezSource/Core/PlatformProfiler.cpp`
- `SkullbonezSource/Core/PlatformProfiler.h`

### Render Detail Exists, But Not In CPU PIX Rows

Many render passes use `PROFILE_GPU_BEGIN/END`, for example:

- `Frame/Render/Skybox`
- `Frame/Render/CinematicSky`
- `Frame/Render/Reflection`
- `Frame/Render/Reflection/Skybox`
- `Frame/Render/Reflection/Balls`
- `Frame/Render/Balls`
- `Frame/Render/Terrain`
- `Frame/Render/Water`
- `Frame/Shadows/ShadowMap`
- `Frame/UI/Draw`

Those scopes still record CPU elapsed time in the in-engine profiler, but
`Profiler::GpuBegin()` deliberately passes `emitCpuPlatformProfiler=false` to
avoid duplicate CPU PIX ranges. The DX12 backend emits the PIX event on the
command list instead.

Result: PIX GPU captures can show render command-list events, but PIX CPU timing
captures mostly show the broad CPU-side `Frame/Render` range with sparse child
detail.

Relevant files:

- `SkullbonezSource/Core/Profiler.cpp`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`

### Some Render Draw Trace Scopes Lack Profiler Scopes

These scopes have draw-call trace coverage but no matching `PROFILE_GPU_*` or
`PROFILE_*` scope today:

- `Frame/Render/DebugOverlay`
- `Frame/Render/VolumetricLight`
- `Frame/Render/Tonemap`

`Frame/Render/TransparentBalls` is referenced by the GPU-frame summary hash list
but needs a source pass audit before adding or removing anything.

Relevant files:

- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Runtime/RunFrame.cpp`

### Worker Samples Are Separate In-Engine, But Not PIX-Decorated

Normal `PROFILE_*` markers are ignored on worker threads. Worker work uses
`WorkerProfilerScope`, which records bounded worker samples for the in-engine
profiler and the worker-core chart.

That worker scope currently does not emit platform profiler CPU ranges, so PIX
does not get a decorated worker marker from the worker profiler path.

Relevant files:

- `SkullbonezSource/Core/Profiler.cpp`
- `SkullbonezSource/Core/Profiler.h`
- `SkullbonezSource/Core/WorkerPool.cpp`

## Design Principles

1. Preserve existing engine marker identity first.

   Do not initially rename in-engine profiler markers. Keep hashes, CSV columns,
   UI expansion state, and perf history stable. Add PIX-only decorated names in
   the platform profiler path.

2. Make colors semantic, then slightly varied.

   A domain should be recognizable from color alone. Within that domain, apply a
   small deterministic variation from the marker hash so adjacent spans are still
   distinguishable.

3. Use suffixes for execution context, not for domain.

   The path already says `Frame/Physics` or `Frame/Render`. Suffixes should
   answer "where/how did this execute?" Examples: `_Worker`, `_Chunk`, `_Record`,
   `_GPU`.

4. Avoid per-object or per-draw PIX ranges by default.

   PIX should show pass and subsystem structure. The existing draw-call trace is
   the right place for high-cardinality draw detail.

5. No heap allocation in hot marker paths.

   Any decorated PIX name should use bounded stack buffers or cached literals.
   Keep formatting behind the explicit platform-profiler runtime flag.

## Proposed Color Scheme

Use ARGB constants because PIX APIs expect 32-bit colors.

| Domain | Prefix | Base Color | Meaning |
|--------|--------|------------|---------|
| Frame/root | `Frame` | `0xff9aa0a6` | neutral frame structure |
| Physics | `Frame/Physics` | `0xff2f80ed` | simulation, collision, solver |
| Post-physics/debug physics | `Frame/PostPhysics` | `0xff56ccf2` | physics visualization and post-step debug |
| Render | `Frame/Render` | `0xffeb5757` | main render work |
| Shadows | `Frame/Shadows` | `0xff9b51e0` | shadow-map render work |
| UI | `Frame/UI` | `0xfff2c94c` | UI layout, text, draw |
| Replay | `Frame/Replay` | `0xff00b8a9` | replay tools and presentation |
| SoA/data prep | `Frame/SoA` | `0xff27ae60` | data refresh/projection |
| Sync/wait | `Frame/VsyncWait`, `Frame/PipelineSync` | `0xff6c757d` | waiting, sync, idle-ish spans |
| Input/post-draw/misc | other `Frame/*` | `0xffb0bec5` | fallback runtime work |

Modifier rules:

- `_Worker`: keep the domain hue, darken slightly, and reduce saturation a bit.
- `_Chunk`: keep the domain hue, lighten slightly.
- `_Record`: use the domain hue, slightly muted, for CPU command-recording work.
- `_GPU`: use the domain hue, fully saturated, for command-list/GPU events.
- Hash variation: apply a small plus/minus brightness shift, not a new hue.

Implementation direction:

- Replace hash-only color generation in `PlatformProfiler::ColorForMarker()`
  with:
  - classify domain by path prefix,
  - classify execution context by suffix or worker metadata,
  - apply a bounded hash-derived brightness variation,
  - return the final ARGB color.

## Proposed Marker Naming

### Default CPU Scopes

Keep existing names:

```text
Frame/Physics
Frame/Render
Frame/UI/Layout
```

### Worker Scopes

Use PIX-only decorated names first:

```text
Frame/Physics/Integrate/WorkerBodies_Worker
Frame/Physics/Terrain/Detect/WorkerBodies_Worker
Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect/WorkerScanBounds_Worker
```

If per-worker identity is useful in PIX, add an optional compact index:

```text
Frame/Physics/Integrate/WorkerBodies_Worker03
```

Recommendation: start with `_Worker` only. Per-worker identity already exists in
the in-game worker-core chart, and adding `_Worker03` can fragment PIX search
results unless the capture is specifically about scheduler balance.

### Chunk Scopes

Use `_Chunk` only when the marker corresponds to a parallel chunk boundary:

```text
Frame/Physics/Integrate/WorkerBodies_Chunk
```

Optional verbose diagnostic mode:

```text
Frame/Physics/Integrate/WorkerBodies_Chunk07_Worker03
```

Recommendation: do not make verbose chunk labels the default. They are excellent
for a focused scheduling investigation, but noisy in ordinary captures.

### Render GPU Scopes

Keep command-list/GPU events clearly GPU-coded:

```text
Frame/Render/Balls_GPU
Frame/Render/Terrain_GPU
Frame/Render/Water_GPU
Frame/Shadows/ShadowMap_GPU
```

### Render CPU Recording Mirrors

Add CPU PIX mirrors for `PROFILE_GPU_*` scopes so CPU timing captures show render
detail:

```text
Frame/Render/Balls_Record
Frame/Render/Terrain_Record
Frame/Render/Water_Record
Frame/Shadows/ShadowMap_Record
```

This intentionally avoids the old "duplicate same-name CPU/GPU rows" problem.
The CPU row is command recording / render submission work. The GPU row is GPU
execution.

## Implementation Slices

### Slice 1: Semantic PIX Colors

Scope:

- `SkullbonezSource/Core/PlatformProfiler.h`
- `SkullbonezSource/Core/PlatformProfiler.cpp`

Tasks:

1. Add a small path classifier:
   - `Frame/Physics`
   - `Frame/PostPhysics`
   - `Frame/Render`
   - `Frame/Shadows`
   - `Frame/UI`
   - `Frame/Replay`
   - `Frame/SoA`
   - sync/wait markers
   - fallback
2. Add suffix/context classifier:
   - `_Worker`
   - `_Chunk`
   - `_Record`
   - `_GPU`
3. Replace hash-derived full-color selection with semantic base color plus small
   deterministic brightness variation.
4. Keep the function cheap and allocation-free.

Acceptance:

- A PIX capture shows stable domain colors.
- Physics reads blue.
- Render reads red.
- Shadows read purple.
- UI reads yellow.
- Wait/sync reads gray.
- Same-domain children remain distinguishable.

Validation at PR gate:

```bat
tools\validate_fast.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

### Slice 2: Worker PIX Suffixes

Scope:

- `SkullbonezSource/Core/Profiler.h`
- `SkullbonezSource/Core/Profiler.cpp`
- `SkullbonezSource/Core/PlatformProfiler.cpp`

Tasks:

1. Make platform CPU depth thread-local:
   - current `g_cpuDepth` is global,
   - worker CPU PIX ranges need independent per-thread depth.
2. Add platform profiler emission to `WorkerProfilerScope`:
   - constructor calls `PlatformProfiler::CpuBegin()` with a decorated
     `_Worker` name,
   - destructor calls `PlatformProfiler::CpuEnd()`.
3. Use a bounded stack buffer for decorated names:
   - source marker names are path literals,
   - suffix formatting should not allocate.
4. Keep in-engine worker marker registration unchanged:
   - no new marker hashes,
   - no extra CSV columns,
   - no UI expansion churn.
5. Optionally add a separate helper later for verbose `_WorkerNN` labels.

Acceptance:

- Worker-lane physics scopes in PIX are visibly suffixed with `_Worker`.
- They keep the physics blue family.
- The main-thread `Frame/Physics` marker remains unchanged.
- No CPU platform-profiler end-without-begin logs appear.

Validation at PR gate:

```bat
tools\validate_fast.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

If worker marker changes touch runtime worker launch behavior, prefer:

```bat
tools\validate_full.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

### Slice 3: Render CPU Mirrors For GPU Scopes

Scope:

- `SkullbonezSource/Core/Profiler.h`
- `SkullbonezSource/Core/Profiler.cpp`
- `SkullbonezSource/Core/PlatformProfiler.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`

Tasks:

1. Keep `PROFILE_GPU_*` call sites unchanged.
2. In `Profiler::GpuBegin()`:
   - keep internal CPU timer behavior,
   - emit a CPU platform range named `<path>_Record`,
   - emit the backend GPU event named `<path>_GPU`.
3. In `Profiler::GpuEnd()`:
   - close the backend GPU event,
   - close the CPU platform `_Record` event.
4. Preserve stack ordering:
   - CPU platform `_Record` depth is independent from DX12 command-list GPU
     event depth.
5. Keep this behind `PlatformProfiler::IsEnabled()` so normal validation and
   perf runs are unchanged unless explicitly requested.

Acceptance:

- PIX CPU timing captures show render pass children inside `Frame/Render`.
- PIX GPU captures show command-list events with `_GPU` suffix.
- CPU and GPU rows are no longer same-name duplicates.
- `Frame/Render` red box gaps are replaced by named render pass ranges.

Validation at PR gate:

```bat
tools\validate_dx12_renderer.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

If this touches only core profiler code and not DX12 backend behavior, also run:

```bat
tools\validate_fast.bat
```

### Slice 4: Add Missing Render Pass Markers

Scope:

- `SkullbonezSource/Runtime/RunPasses.cpp`
- possibly `SkullbonezSource/Runtime/RunFrame.cpp`

Tasks:

1. Add `PROFILE_GPU_*` or scoped equivalents around active render work in:
   - `Frame/Render/DebugOverlay`
   - `Frame/Render/VolumetricLight`
   - `Frame/Render/Tonemap`
2. Audit `Frame/Render/TransparentBalls`:
   - either add the missing marker where the pass actually exists,
   - or remove it from the GPU summary hash list if the pass no longer exists.
3. Place markers after cheap early-out checks when a disabled feature should not
   appear as active GPU work.
4. Keep draw-call trace scopes aligned with profiler scopes.
5. Do not add per-object/per-draw markers.

Acceptance:

- Render passes with draw-call trace rows also have profiler/PIX ranges.
- Render GPU frame summary no longer references dead or unregistered pass names.
- PIX captures show a coherent render tree.

Validation at PR gate:

```bat
tools\validate_full.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

If the slice also touches DX12 backend profiler code, run:

```bat
tools\validate_dx12_renderer.bat
```

### Slice 5: Render Subdetail Without Noise

Scope:

- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- targeted render helper files only if needed

Candidate submarkers:

- `Frame/Render/PrepareModels`
- `Frame/Render/Reflection/DXR`
- `Frame/Render/Reflection/Planar`
- `Frame/Render/Reflection/Planar/Skybox`
- `Frame/Render/Reflection/Planar/Balls`
- `Frame/Render/Balls/CollisionState`
- `Frame/Render/Balls/LitModels`
- `Frame/Render/Terrain/Draw`
- `Frame/Render/Water/Draw`
- `Frame/Render/VolumetricLight/Draw`
- `Frame/Render/Tonemap/Draw`
- `Frame/Render/DebugOverlay/Broadphase`
- `Frame/Render/DebugOverlay/TornadoField`
- `Frame/Render/DebugOverlay/PhysicsDebug`

Rules:

- Add submarkers only around work that can plausibly matter.
- Keep tiny state-setting clusters inside the parent unless profiling proves
  they need names.
- Do not add markers inside per-model loops unless there is a temporary
  diagnostic flag.
- Prefer one useful marker over five decorative ones.

Acceptance:

- A render-heavy PIX capture answers "where did render time go?" without zooming
  into every draw.
- In-game profiler remains readable.
- Marker count stays under `Profiler::MAX_MARKERS`.

Validation at PR gate:

```bat
tools\validate_full.bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

Run performance validation if marker count or per-frame overhead grows beyond
simple pass-level scopes:

```bat
tools\validate_perf.bat
```

## Suggested Work Order

1. Implement semantic colors first.
2. Add worker `_Worker` PIX suffixes with thread-local CPU depth.
3. Mirror `PROFILE_GPU_*` into CPU PIX ranges as `_Record` and GPU PIX ranges
   as `_GPU`.
4. Add missing render pass markers for DebugOverlay, VolumetricLight, Tonemap,
   and audit TransparentBalls.
5. Add selective render subdetail only after a capture proves the remaining
   broad spans are still too broad.

This order gives immediate readability improvement before touching many render
call sites.

## Risks And Guardrails

### Risk: PIX Name Churn Breaks In-Engine Perf Comparisons

Guardrail:

- Decorate platform-profiler names only at first.
- Keep `Profiler::Marker::name` and hashes unchanged.

### Risk: Worker PIX Ranges Break CPU Stack Accounting

Guardrail:

- Make CPU platform-profiler depth thread-local.
- Add explicit logs for end-without-begin on each thread.
- Smoke with workers enabled and disabled.

### Risk: Render CPU Mirrors Reintroduce Duplicate Confusion

Guardrail:

- Never mirror GPU scopes with the exact same PIX name.
- Use `_Record` for CPU command recording and `_GPU` for command-list events.

### Risk: Too Many Render Markers

Guardrail:

- Keep permanent markers at pass/subpass level.
- Use temporary diagnostic flags for per-object/per-draw work.
- Check marker count after adding subdetail.

### Risk: Platform-Profiler Hot-Path Formatting Allocates

Guardrail:

- Use fixed stack buffers.
- Keep formatting behind `PlatformProfiler::IsEnabled()`.
- Do not use `std::string` in per-scope marker decoration.

## Manual PIX Acceptance Checklist

Capture with:

```bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --profiler --scene SkullbonezData\scenes\perf_test.scene.json --vsync off
```

Check:

- `Frame/Physics` family is blue.
- `Frame/Render` family is red.
- `Frame/Shadows` family is purple.
- `Frame/UI` family is yellow.
- worker-lane physics markers include `_Worker`.
- render pass rows appear inside `Frame/Render` in the CPU timing view.
- command-list/GPU events include `_GPU`.
- CPU render mirrors include `_Record`.
- no platform-profiler stack mismatch logs appear.
- DX12 validation log remains clean when running the selected gate.

## Open Questions

1. Should per-worker labels be enabled by default?

   Recommendation: no. Start with `_Worker`. Add `_WorkerNN` only behind a
   verbose platform-profiler option if scheduler analysis needs it.

2. Should chunk labels be permanent?

   Recommendation: only generic `_Chunk` should be permanent. Verbose chunk and
   worker indices should be diagnostic-only.

3. Should GPU command-list events keep original names or use `_GPU`?

   Recommendation: use `_GPU` once CPU `_Record` mirrors exist. That makes PIX
   CPU and GPU lanes self-explanatory.

4. Should `Frame/PostPhysics` be blue or separate cyan?

   Recommendation: cyan. It visually reads as physics-adjacent without blending
   into the main solver/step spans.

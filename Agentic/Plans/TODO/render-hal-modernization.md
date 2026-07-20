# Render HAL Modernization

Status: Active — 5/6 tasks (M0-M5)
Owner: repository owner; registered 2026-07-20 as campaign plan 6 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding F)
Ledger: M0-M5
Depends on: `../../Reports/2026-07-20/render-graph-completion-closure.md`
(satisfied — the single callback/barrier graph path is now the migration seam).

## Objective

Retire the OpenGL-2-era statefulness from the render HAL now that DX12 is
the only backend. Passes declare their raster state in typed per-pass/per-
bucket descriptions instead of mutating global
`SetDepthTest`/`SetBlendFunc`/`SetPolygonOffset` state between draws;
`PSOKey12` reverse-engineering shrinks toward precompiled per-pass PSO
tables; the DXR one-off interface becomes a typed pass description. The
scope is `IRenderCommandContext` and the DXR facet; the factory, capture,
and diagnostics facets already carry their weight and stay.

## Problem / Evidence

`IRenderCommandContext` is a stateful immediate API
(`SetDepthTest`/`SetBlendFunc`/`SetClipPlane`/`BindTexture(slot)`); the
backend rebuilds PSOs from accumulated state via `PSOKey12` hashing
(`RenderBackendDX12.h:155-176`). The abstraction was built for GL/DX11
parity that no longer exists — it now costs DX12's wins (precompiled PSOs,
parallel recording readiness, bindless) while protecting nothing.
`DispatchReflectionRays` takes eight individual sky texture handles and raw
`float*` matrices through the HAL (`RenderBackendDX12.h:830-846`).
`RenderBackendDX12` implements seven interfaces in one class
(`RenderBackendDX12.h:644-650`).

## Non-Goals

- No visual change; screenshot baselines and zero DX12 validation errors are
  the oracle for every slice, with zero refresh authorized.
- No parallel command recording, multi-queue, or bindless implementation in
  this plan — the modernization makes them *expressible*, it does not build
  them (the job-system plan owns that future).
- No renderer feature work; pass content is untouched.
- `IRenderResourceFactory`, `IRenderCaptureBackend`, `IRenderDiagnostics`,
  `IRenderDeviceLifecycle`, `IRenderShaderDevelopment` interfaces are out of
  scope except where a signature must carry a typed state block.
- No new inheritance; state blocks are value records.

## Binding Decisions

1. A typed `RasterStateDesc` value (depth test/write, blend enable/factors,
   cull, polygon offset, target format expectations) becomes part of pass
   input; draws inside a pass select from that pass's declared state
   buckets. Global mutable raster state on the command context is deleted
   at closure.
2. `PSOKey12` remains the cache identity but is populated from declared
   state descs, not accumulated setter state; state-desc-declared passes may
   precompile their PSOs at resource-creation time. Record cache hit/size
   evidence before/after.
3. Interface math types: `float*` matrix/vector parameters on migrated
   surfaces become the engine's `Matrix4`/`Vector3` types or typed spans.
4. The DXR facet generalizes to a typed reflection-pass description
   (environment texture set as a value record, not eight positional
   handles). Single-purpose is fine; positional soup is not.
5. Dead interface surface (e.g. `SetClipPlane` if unused, redundant state
   getters) is deleted with a usage-inventory proof, not kept "just in
   case".
6. Migration is pass-by-pass behind the plan-5 graph path; a migrated pass
   must not fall back to setter-driven state.
7. Every slice: DX12 gate + bounded stress; slices touching upload/frame
   allocator or PSO/root-signature lifetime run the renderer gate three
   consecutive times (Danger Zones).
8. The dependency-direction closure's bounded Profiler exception is owned here:
   Rendering takes the overlay presenter and GPU-diagnostics binding while Core
   retains marker identity/history as typed read-only views. Do not solve this
   with new inheritance, a callback pack, service bag, global lookup, or host
   pointer.

## M0 Declared-State Contract And Inventory

Inventory date: 2026-07-20. The inventory used the current
`IRenderCommandContext.h` declaration plus exact member-call searches over
tracked `SkullbonezSource` C++ headers and sources. Backend override/forwarding
calls are implementation evidence, not consumers, and are omitted from the
caller column. `Clear` has four command-context calls; unrelated container
`Clear()` matches were rejected by source inspection.

Classification vocabulary:

- **Keep**: remains a dynamic command or graph/resource operation. A later
  typed-parameter cleanup is allowed, but it is not raster-state authority.
- **Migrate**: callers move to a typed declaration or typed draw/upload input;
  the old member is deleted when its last caller moves.
- **Delete**: no replacement command remains. Zero-caller rows can disappear
  directly; getter callers become explicit bucket selection rather than
  save/mutate/restore logic.

| `IRenderCommandContext` member | External callers at M0 | Classification and closure action |
|---|---|---|
| `SetViewport` | `RuntimeRenderPasses.cpp` (8) | **Keep** as dynamic viewport/scissor command; later use a typed `ViewportDesc`. |
| `Clear` | `RuntimeRenderer.cpp` (1), `RuntimeRenderPasses.cpp` (3) | **Migrate** to `ClearTargetDesc` so color/depth values travel with the clear and cannot be hidden mutable state. |
| `SetClearColor` | none | **Delete**; zero caller proof. Its value moves into `ClearTargetDesc`. |
| `SetClearDepth` | none | **Delete**; zero caller proof. Its value moves into `ClearTargetDesc`. |
| `SetDepthTest` | `Text.cpp` (6), `UIFrameComposition.cpp` (2), `RunEditorTracer.cpp` (3), `LauncherLaser.cpp` (2), `RuntimeRenderPasses.cpp` (12) | **Migrate** to `RasterStateDesc::depthTest`; delete the setter at M3. |
| `SetDepthWrite` | `UIFrameComposition.cpp` (2), `LauncherLaser.cpp` (2), `RunEditorTracer.cpp` (2), `PrimitiveBatchRenderer.cpp` (2), `RuntimeRenderPasses.cpp` (12) | **Migrate** to `RasterStateDesc::depthWrite`; delete at M3. |
| `SetBlend` | `PrimitiveBatchRenderer.cpp` (2), `UIFrameComposition.cpp` (2), `Text.cpp` (6), `LauncherLaser.cpp` (2), `RunEditorTracer.cpp` (2), `RuntimeRenderPasses.cpp` (12) | **Migrate** to `RasterStateDesc::blendEnabled`; delete at M3. |
| `SetBlendFunc` | `PrimitiveBatchRenderer.cpp` (1), `UIFrameComposition.cpp` (1), `Text.cpp` (3), `LauncherLaser.cpp` (2), `RunEditorTracer.cpp` (2), `RuntimeRenderPasses.cpp` (4) | **Migrate** to typed source/destination blend factors; delete at M3. |
| `SetCullFace` | `LauncherLaser.cpp` (2), `RunEditorTracer.cpp` (2), `RuntimeRenderPasses.cpp` (3) | **Migrate** from boolean enablement to typed `CullMode`; delete at M3. |
| `SetPolygonOffset` | `RuntimeRenderPasses.cpp` (2) | **Migrate** to `DepthBiasDesc`; delete at M3. |
| `SetClipPlane` | `RuntimeRenderPasses.cpp` (6) | **Migrate** to an optional typed clip-plane pass/draw input. It is live reflection state, so M0 disproves the earlier dead-code example. Delete the indexed toggle at M3/M4. |
| `BindTexture` | `WorldEnvironment.cpp` (1), `TextureCollection.cpp` (1), `UIFrameComposition.cpp` (2), `PrimitiveBatchRenderer.cpp` (3), `Text.cpp` (1), `Shadow.h` (3), `RuntimeRenderPasses.cpp` (2) | **Keep** as draw-resource binding; bindless is a non-goal. Replace raw slot integers with a typed binding index only where touched. |
| `MaterializeGraphTransientResources` | `RuntimeRenderer.cpp` (1) | **Keep**; graph resource-lifetime operation. |
| `ResolveGraphTextureBinding` | `RuntimeRenderer.cpp` (1) | **Keep**; graph-to-backend resource resolution. |
| `ResolveGraphResourceToken` | `RuntimeRenderer.cpp` (5) | **Keep**; graph declaration tokenization. |
| `ResolveGraphBackbufferBinding` | `RuntimeRenderer.cpp` (1) | **Keep**; swap-chain graph binding. |
| `ExecuteGraphTransitions` | `RuntimeRenderer.cpp` (3) | **Keep**; compiled graph barrier execution. |
| `BeginGraphTextureRenderTarget` | `RuntimeRenderPasses.cpp` (1) | **Keep** for the current graph executor; M2 may absorb it into a typed pass-begin command, but not into raster state. |
| `EndGraphTextureRenderTarget` | `RuntimeRenderPasses.cpp` (1) | **Keep** under the same lifetime rule as pass begin. |
| `IsDepthTestEnabled` | `Text.cpp` (3), `UIFrameComposition.cpp` (1), `RunEditorTracer.cpp` (1), `LauncherLaser.cpp` (1), `RuntimeRenderPasses.cpp` (6) | **Delete** after save/restore callers select explicit buckets. |
| `IsDepthWriteEnabled` | `UIFrameComposition.cpp` (1), `LauncherLaser.cpp` (1), `RunEditorTracer.cpp` (1), `RuntimeRenderPasses.cpp` (2) | **Delete** after explicit bucket migration. |
| `IsBlendEnabled` | `Text.cpp` (3), `UIFrameComposition.cpp` (1), `RuntimeRenderPasses.cpp` (6), `LauncherLaser.cpp` (1), `RunEditorTracer.cpp` (1) | **Delete** after explicit bucket migration. |
| `IsCullFaceEnabled` | `LauncherLaser.cpp` (1), `RunEditorTracer.cpp` (1), `RuntimeRenderPasses.cpp` (1) | **Delete** after explicit bucket migration. |
| `GetBlendFunc` | `UIFrameComposition.cpp` (1), `RunEditorTracer.cpp` (1), `RuntimeRenderPasses.cpp` (2), `LauncherLaser.cpp` (1) | **Delete** after explicit bucket migration. |
| `UploadAndDrawDynamicVB` | `LauncherLaser.cpp` (1), `UIFrameComposition.cpp` (1), `RuntimeRenderPasses.cpp` (1), `Text.cpp` (3), `PrimitiveBatchRenderer.cpp` (2) | **Migrate** to typed vertex span plus declared raster bucket; no raw float stream at closure. |
| `DrawLinesColored` | `RunEditorTracer.cpp` (1), `RuntimeRenderPasses.cpp` (1) | **Migrate** to typed colored-line span, `Matrix4`, and declared raster bucket. |
| `DrawTransientColoredTriangles` | `RunEditorTracer.cpp` (2), `RuntimeRenderPasses.cpp` (1) | **Migrate** to typed triangle span, `Matrix4`, style, and declared raster bucket. |
| `UploadInstanceData` | `PrimitiveBatchRenderer.cpp` (6) | **Migrate** raw float/count input to a typed instance span; upload remains separate from raster selection. |
| `DrawInstancedMesh` | `PrimitiveBatchRenderer.cpp` (6) | **Migrate** to a typed instanced-draw description carrying the declared raster bucket. |

### Raster description and pass-local buckets

The source-of-truth shape for M1-M3 is a value-only declaration, not another
mutable command-context selector:

```cpp
struct DepthBiasDesc
{
    bool enabled = false;
    float constant = 0.0f;
    float slopeScaled = 0.0f;
};

struct RenderTargetFormatSet
{
    FixedRenderTargetFormats colorFormats;
    DepthTargetFormat depthFormat = DepthTargetFormat::None;
    uint8_t sampleCount = 1;
};

struct RasterStateDesc
{
    bool depthTest = true;
    bool depthWrite = true;
    bool blendEnabled = false;
    BlendFactor sourceBlend = BlendFactor::One;
    BlendFactor destinationBlend = BlendFactor::Zero;
    CullMode cullMode = CullMode::Back;
    DepthBiasDesc depthBias;
    RenderTargetFormatSet targets;
};

struct PassRasterStateBucket
{
    RasterStateBucketId id; // pass-local index, never durable identity
    RasterStateDesc raster;
};

struct PassRasterStateSet
{
    FixedSpan<const PassRasterStateBucket> buckets;
};
```

Exact engine enum/container names may follow existing fixed-capacity types, but
the semantics above are binding. The graph pass declaration owns the fixed
bucket set. Every draw that can affect a graphics PSO receives a pass-local
`RasterStateBucketId` (or the resolved `const RasterStateDesc&` at the backend
boundary). There is no `SelectRasterState`/`SetCurrentBucket` mutable command.
The DX12 owner builds `PSOKey12` directly from the selected declaration plus
shader, vertex layout, root-signature identity, and target formats. Known pass
buckets may be precompiled during resource creation. Overlay helpers that
temporarily changed state must declare their normal and overlay buckets and
choose explicitly; they must not query or restore ambient state. All bucket
storage is fixed/preallocated before steady gameplay.

`ClearTargetDesc`, typed clip-plane data, typed vertex/instance spans, and
`Matrix4` are adjacent command-data contracts rather than members of
`RasterStateDesc`. This prevents unrelated clear/resource/geometry data from
turning the raster record into a new state bag.

### PSO-cache baseline

The baseline is the G5 one-minute `tools\run_graphics_stress.bat 1` result at
the source tip immediately before this documentation task
(`TestOutput/graphics_stress/latest_stdout.txt`, 2026-07-20 21:31 local;
`latest_exit.txt` = 0). `RenderMemoryStats::psoCacheCount` sampled the in-memory
fixed cache as follows:

| Stress frame | 1 | 1,800 | 3,600 | 5,400 | 7,200 | 9,000 | 10,800 | 12,600 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PSO entries | 0 | 24 | 24 | 24 | 24 | 24 | 24 | 24 |

The existing hit path linearly searches the 96-row fixed array and reuses the
matching pointer; only a miss logs `dx12_pso_cache_miss`, creates a PSO, and
increments `m_psoCacheCount` (`RenderBackendDX12.Pipeline.cpp:498-548`). Thus
the stable 24-entry count across 10,800 sampled frames and repeated scene
reloads proves in-process reuse after warm-up. The current diagnostics expose
entry count but no exact hit counter, so M1 must add fixed monotonic
`psoCacheHitCount`, `psoCacheMissCount`, and `precompiledPsoCount` diagnostics
before the pilot. Before/after evidence records those counters plus entry count;
the M0 baseline is not to be misrepresented as an exact hit total. The mapped
persistent-driver cache remains an independent cold-start accelerator and does
not replace this in-process evidence.

### Profiler dependency seam and owned replacement

`Core/Profiler.h` currently leaks Rendering/Text through five concrete seams:

1. forward declarations of `IRenderCommandContext`, `IRenderDiagnostics`, and
   `TextBatch`;
2. `BindRenderDiagnostics(IRenderDiagnostics*)` and the stored
   `m_renderDiagnostics` borrow;
3. `RenderOverlay(TextBatch&, IRenderCommandContext&, ...)`;
4. `RenderBarOverlay(TextBatch&, IRenderCommandContext&, ...)`; and
5. GPU begin/end, query-result reads, device invalidation, platform GPU events,
   render-memory counters, and draw-call counters implemented by pulling
   `IRenderDiagnostics` from `ProfilerImplementation.cpp`.

M5's owned replacement is a one-way value boundary:

- Core owns marker/counter identity and history and publishes fixed,
  read-only `ProfilerFrameView`/marker/counter spans. It accepts completed
  `GpuTimingSample` values keyed by Core marker identity. Core stores no
  renderer pointer and contains no Rendering/Text declarations.
- Rendering owns a concrete `RenderGpuTimingOwner`. Runtime wiring gives it the
  existing diagnostics/backend lifetime; it records timestamp/platform events
  immediately around Rendering/UI command recording, owns the fixed open-marker
  stack, reads completed queries, and hands a fixed span of `GpuTimingSample`
  values to Core at the frame boundary. GPU profiling call sites receive this
  owner explicitly; no callback, global lookup, service bag, host pointer, or
  new polymorphic interface is permitted.
- Rendering owns a concrete `ProfilerOverlayPresenter`. It consumes a
  `const ProfilerFrameView&`, Rendering-owned memory/draw statistics, the
  current `TextBatch`, and the declared UI render bucket. The two rendering
  methods move out of `Core::Profiler`; render/Tracy counter publication moves
  with the presenter/timing owner rather than Core pulling diagnostics.
- Runtime construction/wiring sequences the value handoff and guarantees GPU
  query invalidation before backend teardown. Core's public GPU macros either
  move to the Rendering timing surface or become CPU-only helpers; they cannot
  preserve the old hidden renderer borrow under a different spelling.

This boundary preserves immediate GPU timestamps and Core-owned history while
restoring the allowed Rendering -> Core dependency direction.

## M1 Declared-State Pilot Evidence

Completed 2026-07-20 on `nightrunner-20th-july`.

The pilot establishes one production path for each required pass shape:

- opaque world: all six `SkyBox` face meshes use one pass-local opaque bucket
  (`depth test/write on`, blending off, back-face cull, no bias). The mesh
  precompiles that declared recipe against the active pass target before the
  first face and every face passes the same bucket on `IMesh::Draw`;
- blended overlay: `LauncherLaser` owns one additive bucket (`depth test/write
  off`, source-alpha/one blend, no cull). It precompiles once per laser resource
  lifetime and passes the bucket on `UploadAndDrawDynamicVB`. Its former five
  state queries, five setter calls, and restore sequence are gone; and
- backend: `Dx12PipelineOwner` has one key builder and one fixed cache for both
  legacy and declared draws. Declared state is an operation value carried
  through `Dx12DrawGate`; it is never copied into the legacy desired-state
  fields. Pass precompile populates the same cache without binding command-list
  state. `RenderMemoryStats` now reports monotonic hit, miss, and precompiled
  creation counters per device epoch.

The M0 and M1 bounded-stress runs use the same one-minute suite/seed. M0 could
observe only entries; M1 adds exact behavior:

| Evidence | M0 | M1 frame 1,800 | M1 frame 10,800 |
|---|---:|---:|---:|
| In-process PSO entries | 24 | 23 | 23 |
| Cache hits | not instrumented | 24,630 | 150,746 |
| Cache misses | not instrumented | 23 | 23 |
| PSOs created by pass precompile | not instrumented | 1 | 1 |

The entry count is one lower than the pre-pilot run, while the exact miss count
is flat for all samples after frame 1,800. This records the improvement without
claiming a causal split for the removed row. The launcher Automation interaction
also exits 0 with `ok=true`, proving the blended pilot submits successfully.

Validation at the final M1 source tip:

| Command | Time | Result |
|---|---:|---|
| `tools\validate_dx12_renderer.bat` run 1 | 87.88 s | PASS; Profile/Debug clean, zero warnings/errors, captures accepted, zero DX12 validation errors |
| `tools\validate_dx12_renderer.bat` run 2 | 55.34 s | PASS; same evidence |
| `tools\validate_dx12_renderer.bat` run 3 | 55.31 s | PASS; same evidence |
| `tools\run_graphics_stress.bat 1` | 61.52 s | PASS; PID-scoped bounded stop, crash-free, stderr empty; 23 misses stable and 150,746 hits by frame 10,800 |
| `tools\validate_perf.bat` | 114.97 s | PASS; zero steady-gameplay allocation violations and no DX12/physics regressions; DX12 avg 0.7511 ms, p99 1.3535 ms |
| `tools\validate_full.bat` | 152.91 s | PASS; CPU/coverage/Automation/DX12/physics, zero DX12 validation errors, accepted screenshots, 44,401 physics lines byte-exact |

Focused iteration also passed a Profile build (20.97 s, zero warnings/errors),
Automation build (18.73 s, zero warnings/errors), all 329 doctests / 61,354
assertions (3.63 s), a five-frame normal sky launch (3.34 s), and the 90-frame
launcher interaction (2.74 s, `ok=true`). Comment-style audit: 16/16 touched
source-bearing files inspected, zero deferred; the two added null-mesh methods
are trivial test-double no-ops and require no learning header.

## M2 Full-Pass Migration Evidence

Completed 2026-07-20 on `nightrunner-20th-july`.

Every production graphics submission now selects a declared raster bucket:

- terrain color/shadow, calm/ocean water, primitive opaque/transparent/shadow
  batches, and convex hull draws carry complete mesh/dynamic/instanced recipes;
- text, UI previews, cinematic sky, volumetric light, and tonemap fullscreen
  quads carry depth-disabled buckets instead of save/mutate/restore sequences;
- tornado triangles, replay depth-hint/visible ribbons, and both debug-line
  producers carry explicit blended or specialized line buckets; and
- the DX12 transient-triangle, debug-line, and instanced-mesh paths accept the
  declaration at the operation boundary. The specialized line PSO validates
  its immutable depth-disabled, unblended, two-sided recipe.

Production pass/helper `.cpp` files contain zero raster setter or state-query
calls. The legacy interface/backend rows remain only for M3 deletion; declared
draws do not copy their values into legacy desired state.

The same bounded stress suite/seed shows four fewer warmed PSO rows than M1 and
no late misses:

| Evidence | M1 frame 1,800 | M2 frame 1,800 | M2 frame 10,800 | M2 frame 12,600 |
|---|---:|---:|---:|---:|
| In-process PSO entries | 23 | 19 | 19 | 19 |
| Cache hits | 24,630 | 24,634 | 150,750 | 175,960 |
| Cache misses | 23 | 19 | 19 | 19 |
| PSOs created by pass precompile | 1 | 1 | 1 | 1 |

Validation at the final M2 source tip:

| Command | Time | Result |
|---|---:|---|
| `tools\validate_dx12_renderer.bat` run 1 | 79.64 s | PASS; Profile/Debug clean, zero warnings/errors, captures accepted, zero DX12 validation errors |
| `tools\validate_dx12_renderer.bat` run 2 | 56.00 s | PASS; same evidence |
| `tools\validate_dx12_renderer.bat` run 3 | 56.14 s | PASS; same evidence |
| `tools\run_graphics_stress.bat 1` | 62.62 s | PASS; 61.736 s sampled, 13,045 frames/358 scene loads, graceful PID-scoped stop, stderr empty, 19 misses stable and 175,960 hits |
| `tools\validate_full.bat` | 157.12 s | PASS; 329/329 tests and 61,354 assertions, coverage/Automation/DX12/physics, zero DX12 errors, accepted screenshots, 44,401 physics lines byte-exact |

Focused iteration also passed Profile and Automation builds (zero
warnings/errors), a five-frame DX12 launch (2.11 s), and the launcher
interaction (3.50 s, `ok=true`). Comment-style audit: 12/12 touched
source-bearing files inspected, zero deferred. Formatting pipeline passed after
scoped formatting of the touched files only.

## M3 Setter-Retirement Evidence

Completed 2026-07-20 on `nightrunner-20th-july`.

The command boundary now has one declared-state path:

- `IRenderCommandContext` and `IMesh` expose no raster setters, raster-state
  queries, or bucketless graphics-draw overloads;
- `Dx12PipelineOwner` retains command bindings and output state only. It builds
  every `PSOKey12` directly from the draw's `RasterStateDesc`; the former
  desired-raster fields, reconstruction fallback, and setter/query forwarding
  methods are deleted;
- dynamic, line, transient-triangle, and instance uploads use bounded
  `std::span<const float>` values. Line and transient transforms use
  `Matrix4`, and instanced submission uses `InstancedMeshDrawDesc`; malformed
  dynamic/transient packed-span divisibility is rejected before upload; and
- the three tracked debug visualizers omitted from M0's initial `rg` inventory
  were found by the M3 compile proof and migrated too. Collision debug solids
  select explicit opaque/translucent buckets; broadphase and physics overlays
  select the immutable debug-line bucket. This corrects the M0 caller inventory
  without weakening its closure classification.

The DX12 architecture reset test now pins command-binding reset state without
any raster fields. Exact stress telemetry is unchanged from M2: 19 entries and
misses from frame 1,800 through 12,600, hits rise 24,634 -> 175,960, and one
PSO is created by pass precompile. The run reached 13,149 frames and 361 scene
loads before graceful PID-scoped shutdown; stderr was empty.

Validation at the final M3 source tip:

| Command | Time | Result |
|---|---:|---|
| `tools\validate_dx12_renderer.bat` run 1 | 79.46 s | PASS; Profile/Debug clean, zero warnings/errors, captures accepted, zero DX12 validation errors |
| `tools\validate_dx12_renderer.bat` run 2 | 56.07 s | PASS; same evidence |
| `tools\validate_dx12_renderer.bat` run 3 | 55.78 s | PASS; same evidence |
| `tools\run_graphics_stress.bat 1` | 62.28 s | PASS; 13,149 frames/361 scene loads, graceful PID-scoped stop, stderr empty, 19 misses stable and 175,960 hits |
| `tools\validate_perf.bat` | 110.79 s | PASS; absolute budgets and DX12/physics comparisons pass; DX12 frame avg 0.7051 ms, p99 1.3105 ms |
| `tools\validate_full.bat` | 144.36 s | PASS; 329/329 tests and 61,354 assertions, coverage/Automation/DX12/physics, zero DX12 errors, accepted screenshots, 44,401 physics lines byte-exact |
| `tools\validate_format.bat` | 13.41 s | PASS; 276 headers aligned and all source files formatted |

Focused iteration passed the Profile build (12.72 s, zero warnings/errors) and
all DX12 architecture tests (22.64 s). Comment-style audit: 23/23 touched
source-bearing files inspected, zero deferred. The format gate initially found
two touched headers needing the repository alignment post-pass; scoped repair
was applied and every formal gate then passed.

## M4 Typed-DXR And Dead-Surface Evidence

Completed 2026-07-20 on `nightrunner-20th-july`.

The optional raytracing facet now carries complete operation values:

- `RaytracingSetupDesc` owns the terrain/sphere geometry descriptions and fixed
  instance capacity passed into initialization;
- `WaterReflectionRayDesc` owns typed `Matrix4`/`Vector3` camera, light, water,
  sky, time, and environment-texture inputs for one dispatch;
- TLAS rebuild accepts `std::span<const Matrix4>` rather than a flat float
  buffer plus count, while the bounded runtime scratch array remains owned by
  `RuntimeRenderer`; and
- the unused dispatch width/height and always-zero terrain/sphere BLAS
  parameters are deleted. Backend-owned output dimensions and BLAS resources
  remain the single authorities.

M0's adjacent dead rows are closed with source proof. `SetClearColor` and
`SetClearDepth` had no tracked external callers; their retained backend fields
and forwarding rows are gone, and `ClearTargetDesc` carries each clear's
values. `IRenderCommandContext::SetClipPlane` was a no-op with two reflection
callers; both calls and the no-op row are gone. The real typed clip-plane state
in `PrimitiveBatchRenderer` and `CollisionVisualizer` remains because their
shaders consume it.

The Debug render suite wrote affirmative machine evidence to
`Debug/runtime_events.log`: `dxr_capability supported=1 tier=11`. It exited 0,
captured all three render scenes, and had empty stderr. The bounded stress suite
then exercised its deterministic three-mode reflection churn for 12,663 frames
and 348 scene loads. PSO telemetry stayed at 19 entries/misses, with hits rising
24,634 -> 175,960 and one pass-precompiled PSO; stderr was empty and shutdown
was graceful.

Validation at the final M4 source tip:

| Command | Time | Result |
|---|---:|---|
| `tools\validate_dx12_renderer.bat` | 78.0 s | PASS; Profile/Debug clean, zero warnings/errors, captures accepted, zero DX12 validation errors |
| `tools\run_graphics_stress.bat 1` | 61.0 s | PASS; 12,663 frames/348 scene loads, graceful PID-scoped stop, empty stderr, stable PSO misses |
| Debug DXR-capability render-suite probe | 7.0 s | PASS; exit 0, empty stderr, `supported=1 tier=11` recorded |
| `tools\validate_full.bat` | 156.0 s | PASS; 329/329 tests and 61,354 assertions, coverage/Automation/DX12/physics, zero DX12 errors, accepted screenshots, 44,401 physics lines byte-exact |

Focused iteration passed the Profile build after correcting one local name
collision (10.23 s, zero warnings/errors); the preceding failed compile was
16.84 s and emitted no warnings. Comment-style audit: 10/10 touched
source-bearing files inspected, zero deferred. Scoped clang-format/header
alignment, `git diff --check`, and CodeGraph sync completed before validation.

## Tasks

- [x] M0 — Contract and inventory: enumerate every `IRenderCommandContext`
  member with its callers; classify keep-as-is / migrate-to-state-desc /
  delete-with-proof; define `RasterStateDesc` and the per-pass state-bucket
  shape; record the PSO cache baseline (entry count, hit behavior) for the
  render test suite. Inventory the `Core/Profiler.h` Rendering/Text seam and
  define its Rendering-owned presenter/GPU-timing boundary per binding decision
  8. Output: inventory + contract committed into this plan.
  No validation (documentation).
- [x] M1 — State-desc pilot: migrate two structurally different passes
  (one opaque world pass, one blended overlay/UI pass) to declared state
  with precompiled PSOs; prove baseline-identical output and record PSO
  cache evidence. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [x] M2 — Full pass migration: remaining passes move to declared state;
  per-draw setter calls disappear from pass bodies. Validation:
  `tools\validate_dx12_renderer.bat` ×3 + `tools\run_graphics_stress.bat 1`.
- [x] M3 — Setter retirement: delete the global raster-state setters/getters
  from `IRenderCommandContext` and their backend state tracking; `PSOKey12`
  population is state-desc-only. Typed math types per binding decision 3 on
  migrated signatures. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [x] M4 — DXR facet: replace `DispatchReflectionRays`/`InitDXR` positional
  surfaces with typed descriptions per binding decision 4; delete dead
  interface rows found in M0 with usage proof. Validation:
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
  (DXR-capable machine evidence recorded; the suite's DXR scenes are the
  oracle).
- [ ] M5 — Closure: grep proofs (no raster setters on the interface, no
  setter calls in passes, no `float*` matrices on migrated surfaces);
  `Core/Profiler.h` has no `Rendering` or `Text` types and no hidden renderer
  callback/global lookup;
  before/after PSO cache and perf numbers recorded; DX12-architecture CPU
  test target updated to pin the new contract; independent rubber-duck
  review (single end-of-plan). Validation: `tools\validate_full.bat` +
  `tools\validate_perf.bat` + `tools\run_graphics_stress.bat 1` at closure
  tip.

## Acceptance

- `IRenderCommandContext` carries no global mutable raster state; passes
  declare state; PSOs for declared passes precompile.
- DXR facet is typed; dead surface deleted with inventory proof.
- All screenshot baselines identical, zero DX12 validation errors, perf
  gate passes with recorded numbers (frame time must not regress outside
  noise; PSO-compile hitches must not appear in steady frames).
- Independent review clear.

## Validation Summary

Per-slice DX12 gate (+×3 where Danger Zones demand) + bounded stress with
recorded evidence; perf gate on M1/M3/M5; `validate_full` at closure.

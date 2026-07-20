# Render HAL Modernization

Status: Active — 1/6 tasks (M0-M5)
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

## Tasks

- [x] M0 — Contract and inventory: enumerate every `IRenderCommandContext`
  member with its callers; classify keep-as-is / migrate-to-state-desc /
  delete-with-proof; define `RasterStateDesc` and the per-pass state-bucket
  shape; record the PSO cache baseline (entry count, hit behavior) for the
  render test suite. Inventory the `Core/Profiler.h` Rendering/Text seam and
  define its Rendering-owned presenter/GPU-timing boundary per binding decision
  8. Output: inventory + contract committed into this plan.
  No validation (documentation).
- [ ] M1 — State-desc pilot: migrate two structurally different passes
  (one opaque world pass, one blended overlay/UI pass) to declared state
  with precompiled PSOs; prove baseline-identical output and record PSO
  cache evidence. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [ ] M2 — Full pass migration: remaining passes move to declared state;
  per-draw setter calls disappear from pass bodies. Validation:
  `tools\validate_dx12_renderer.bat` ×3 + `tools\run_graphics_stress.bat 1`.
- [ ] M3 — Setter retirement: delete the global raster-state setters/getters
  from `IRenderCommandContext` and their backend state tracking; `PSOKey12`
  population is state-desc-only. Typed math types per binding decision 3 on
  migrated signatures. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [ ] M4 — DXR facet: replace `DispatchReflectionRays`/`InitDXR` positional
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

# SkullbonezCore — DX12 render and shader audit

18 September 2026 · Source audit and compiled-shader inspection · Rectification work pending

**Fix measurement reliability first, then hull submission and UI geometry.** The renderer already has useful batching, culling, persistent descriptors, PSO caching and a render graph. The largest source-confirmed waste is the convex-hull path, which rebuilds and uploads mesh data per object and view. UI is not doing framebuffer readback, but it expands rounded shapes aggressively and retains several immediate draws. Shader optimization should begin with measured post-processing costs rather than blanket arithmetic rewrites.

Read the [follow-up implementation and profiling plan](follow-up-plan.md) after this report. Supporting evidence: [shader inventory](shader-static-inventory.csv), [inspection result](shader-inspection-summary.json), and [reproducible inspection script](inspect_shaders.py).

## Scope and confidence

Audited `C:\SkullbonezCore`, branch `nightrunner-17th-SEP-26`, base commit `c686f653bb4c0102daa1132b5fe171d0ccf4c816`, including existing uncommitted renderer, shader and UI changes. CodeGraph was used first; it reported pending changes, so findings were checked against current files. This is a targeted performance review, not certification of every DX12 lifetime or synchronization path.

Inspected draw submission, UI command replay, text/shape batching, constant uploads, state binding, frame pacing, GPU queries, hull rendering, shadow submission, DXR TLAS construction, and important raster shaders. All **56 compiled stages across 29 shader source files** were disassembled using pinned DXC **1.8.2502.11**. Source/dependency/DXIL hashes matched their manifest entries. This hash check does not replace the repository's full generated-reflection validation.

The host reports **NVIDIA GeForce RTX 3080**, Windows driver version **32.0.15.9186**. No application was launched, no new GPU capture or runtime benchmark was taken, and no implementation, configuration, baseline or user session was changed. Existing performance notes were not treated as fresh measurements. Millisecond impact, frame draw counts, occupancy and register pressure remain unmeasured. Repository builds/tests are not required for these documentation-only deliverables.

## Findings, in priority order

P1 means fix before relying on profiling conclusions or scaling the affected workload. P2 means a concrete optimization opportunity whose frame-time benefit needs measurement. P3 means conditional or lower priority. These are work priorities, not measured rankings.

| ID | Priority | Finding | Evidence strength |
|---|---|---|---|
| R1 | P1 | GPU timer slots lose repeated scopes and can expose stale samples | Confirmed bookkeeping behavior |
| R2 | P1 | Convex hulls rebuild/upload/draw individually in visible and shadow passes | Confirmed scaling cost |
| R3 | P2 | Rounded UI shapes expand into many small quads every submission | Confirmed geometry cost |
| R4 | P2 | UI submission lacks general off-clip rejection; clip boundaries always flush | Confirmed path; live frequency unmeasured |
| R5 | P2 | Crosshair uses eight immediate draws | Confirmed when crosshair is visible |
| R6 | P2 | Constant buffers upload every draw; repeated shader selection dirties bindings | Confirmed backend behavior |
| R7 | P2 | Bloom repeats 13 taps at full output resolution, including a duplicate center fetch | Confirmed shader work when bloom is enabled |
| R8 | P2 | Volumetric light uses 48 march steps at half width/height | Confirmed shader work; quality tradeoff |
| R9 | P2 | Shipping shader assets lack source profiling information | Confirmed build settings |
| R10 | P3 | Large material shaders and shadow kernels warrant specialization experiments | Candidate, not proven bottleneck |
| R11 | P3 | DXR reflection rebuilds TLAS whenever that path renders | Confirmed behavior; conditional benefit |

### R1 — Repair GPU measurement identity and accumulation

[Dx12Diagnostics.cpp:377](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp:377) writes begin/end timestamps to `markerIndex * 2` and `markerIndex * 2 + 1`. Repeating a marker in one frame replaces the prior pair instead of accumulating separate occurrences. [UiDrawSubmission.cpp:259](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:259) uses the same `Frame/UI/Draw` path for multiple submission callers, including badges, operator UI and profiler content. When several execute with timing enabled, the pair represents the last occurrence, not their sum.

[Readback handling:304](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp:304) reads all marker pairs, but [resolve:428](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp:428) writes only the slots used that frame and then clears their mask. Old unwritten pairs can therefore remain in readback memory and satisfy `end > begin`. `GpuTimerInvalidate` also retains indexed result arrays across marker resets, while [RenderGpuTimingOwner.cpp:62](C:/SkullbonezCore/SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp:62) intends epochs to prevent identity reuse. Finally, each present replaces the pending fence; when CPU submission stays ahead of GPU completion, fresh results can be skipped and prior values returned repeatedly.

**Rectify:** bounded per-frame query/readback slots, per-occurrence timestamp allocation, a resolved validity mask, originating frame ID and marker epoch, and explicit sample age/drop counts. Aggregate disjoint occurrences by marker only after completion. Do not sum nested inclusive ranges. Consume ready frames without forcing waits; invalidate old-epoch results. Until repaired, use external captures to rank costs and treat in-app GPU percentiles cautiously.

### R2 — Make hull geometry persistent and instance repeated hulls

[PrimitiveBatchRenderer.cpp:222](C:/SkullbonezCore/SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:222) triangulates every hull face and copies the same 32-float instance payload into every emitted vertex. The dynamic vertex is **40 floats / 160 bytes**. [Visible submission:1168](C:/SkullbonezCore/SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:1168) uploads and draws immediately for each hull; `EndConvexHullBatch` does not consolidate draws. [Shadow submission:1192](C:/SkullbonezCore/SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:1192) repeats expansion and upload, even though much of that payload is irrelevant to depth.

For illustration, a 100-triangle hull uploads `300 × 160 = 48,000` bytes per draw. One hundred such visible hulls cost 4.8 MB per view, or 14.4 MB if all also participate in two shadow views. These are arithmetic examples, not measured scene counts.

**Rectify:** cache render meshes at scene/asset load, retaining hard-face normals and UV seams. Separate immutable vertices/indices from per-instance transforms/materials. Group identical mesh and compatible state within each view; use a smaller shadow instance layout. Unique meshes may still need separate draws. Measure cache reuse by geometry identity before predicting instancing gains. Keep collider identity and editor mutation invalidation explicit.

### R3 — Reduce rounded-widget tessellation

[UiDrawSubmission.cpp:99](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:99) draws an expanded translucent halo plus the main rounded fill. Each fill creates a middle quad and up to three quads for every top/bottom cap row. The upper bound is `1 + 6 × ceil(radius)` quads per fill when the middle exists. An eight-pixel radius with its 8.5-pixel halo can therefore generate up to **104 quads / 624 vertices / 14,976 vertex bytes** for one rounded rectangle. Actual counts depend on fractional edge coverage. [RoundedPanel:99](C:/SkullbonezCore/SkullbonezSource/UI/UIDraw.cpp:99) draws both border and inset fill, multiplying this further.

These quads **are batched**, so 104 quads does not mean 104 draw calls. The costs are CPU expansion, upload traffic, small primitives, blend coverage and eventual capacity flushes. The [quad batch](C:/SkullbonezCore/SkullbonezSource/Rendering/Text.h:63) holds 8,192 quads.

**Rectify:** prototype one analytic rounded-rectangle quad carrying radius, border and color, or a bounded reusable mesh. Preserve antialiasing and straight/premultiplied-alpha conventions. Compare pixel cost as well as geometry reduction: one large analytic quad can shade corner pixels discarded by today's geometry. Do not trade reduced vertices for a slower fragment shader without evidence.

### R4 — Reject invisible UI work and avoid unnecessary batch boundaries

[SubmitCommands:285](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:285) expands rects, rounded rects, triangles and text without a general bounds-vs-effective-clip rejection. The image path does have an intersection check. GPU scissoring protects pixels, but it does not prevent CPU construction, upload or vertex work. Some presenters may already suppress hidden rows; this is a missing general submission guard, not proof that every panel submits invisible content.

Every `PushClip` and `PopClip` flushes shapes/text, even if the effective scissor is unchanged. Images flush before availability/visibility is resolved. Layer boundaries also flush, usually correctly.

**Rectify:** track the effective clip; suppress wholly invisible primitives while preserving clip-stack and layer semantics. Compute conservative text/rotated-text bounds and halo bounds. Resolve an invisible image before flushing. Elide equal-scissor transitions where ordering permits. Add flush-reason counters before attempting wider merging. Never globally sort translucent UI by shader or texture; foreground, popup and text layering must survive.

### R5 — Batch the crosshair

[UiTextPass.cpp:498](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/UiTextPass.cpp:498) issues four shadow quads plus four bright quads through `Render2dQuad`. [Text.cpp:944](C:/SkullbonezCore/SkullbonezSource/Rendering/Text.cpp:944) uploads and draws each immediately. A correctly ordered shape batch can reduce this local sequence from eight draws to one: **seven fewer draws when the crosshair is shown**. This is a structural count, not a predicted FPS gain. Preserve its layer relative to queued text, badges and operator panels.

### R6 — Reuse unchanged constants and avoid redundant state rebinding

[ShaderDX12.cpp:1039](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/ShaderDX12.cpp:1039) always reserves and copies the full constant buffer in `FlushCB`; it clears `m_cbDirty` but does not consult it to reuse an upload. The [pipeline fast path:576](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp:576) still calls this for each draw. Repeated [SetActiveShader:697](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp:697) also unconditionally dirties pipeline bindings.

**Rectify:** reuse an unchanged constant allocation only within its valid upload-arena generation, with content version and lifetime checks. Setters currently mark identical values dirty, so compare or version values at their owner. Avoid redundant shader/PSO/root-signature updates while preserving command-list reset, shader reload and externally disturbed state invalidation. A dirty-bit-only cache can reuse freed/recycled upload addresses and is not sufficient.

### R7–R10 — Shader optimization priorities

| Shader | Confirmed source/DXIL behavior | First experiment |
|---|---|---|
| [post_tonemap:135](C:/SkullbonezCore/SkullbonezData/shaders/post_tonemap.hlsl:135) | 13 bloom taps with per-tap prefilter; 16 compiled sample call sites overall | Pass the original, pre-fog center scene sample into bloom; inspect whether total sites fall to 15. Then compare a reduced-resolution bloom chain, including added passes and bandwidth |
| [post_volumetric_light:140](C:/SkullbonezCore/SkullbonezData/shaders/post_volumetric_light.hlsl:140) | 48 steps, each with scene/depth fetch when in bounds, plus receiver depth: up to 97 fetch executions per shaded output pixel | Compare 24/32/48 steps; gate inactive effects; evaluate depth-aware reconstruction if reducing resolution further |
| [lit_textured_instanced:284](C:/SkullbonezCore/SkullbonezData/shaders/lit_textured_instanced.hlsl:284) | Shadow sampling uses 1, 12 or 16 taps per receiver evaluation; many material/style branches | Profile dominant styles; consider a small set of warmed variants for uniform features, then bounded shadow-quality alternatives |
| [lit_textured](C:/SkullbonezCore/SkullbonezData/shaders/lit_textured.hlsl:431) | Large procedural terrain/material shading and shadow paths | Attribute costly branches before specializing; inspect derivatives and visual stability |
| [text](C:/SkullbonezCore/SkullbonezData/shaders/text.hlsl:133) | One font texture sample plus derivative-based SDF smoothing | Optimize submitted geometry and overlap first; keep readable small text |

The volumetric target is already **half width and half height**, confirmed at [RuntimeRenderer.cpp:1488](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1488). Do not claim half-resolution shafts as a new optimization. Full-resolution bloom is a stronger architectural experiment than shaving isolated `pow` calls. All quality changes need static and moving-camera image comparisons.

Compiled static evidence:

| Pixel shader | DXIL bytes | Texture sample call sites | IR branch sites |
|---|---:|---:|---:|
| lit_textured | 65,400 | 103 | 93 |
| lit_textured_instanced | 64,764 | 52 | 96 |
| post_tonemap | 9,104 | 16 | 7 |
| post_volumetric_light | 6,420 | 3 | 12 |
| text | 4,684 | 1 | 0 |

Static call sites are not dynamic sample counts or GPU instructions. The volumetric loop demonstrates the distinction: three sites can execute up to 97 fetches. Large bytecode and branch counts are leads, not evidence of divergence or a measured bottleneck. Driver register counts and occupancy require hardware profiling.

[bake_shaders.py:57](C:/SkullbonezCore/tools/bake_shaders.py:57) uses `-O3 -Qstrip_debug` with no profiling-symbol output. Add a separate optimized profiling build with source information; keep shipping manifests reproducible. Instructions are in the plan.

### R11 — Avoid rebuilding unchanged reflection acceleration structures

[ReflectionPass:1212](C:/SkullbonezCore/SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1212) invokes TLAS construction when DXR reflection renders. [TLASDX12.cpp:211](C:/SkullbonezCore/SkullbonezSource/Rendering/DX12/TLASDX12.cpp:211) uses `PREFER_FAST_BUILD` with no update mode. Profile paused and moving scenes separately. Cache unchanged topology/transforms; experiment with refit only after tracking topology, BLAS and instance changes correctly. Compare total build-plus-trace time because cheaper updates can worsen traversal. Preserve UAV ordering and per-frame storage lifetimes.

## UI verdict and safeguards

**The reviewed UI path is not abusing readback, shader compilation, or one-draw-per-label rendering. It does have avoidable geometry and submission work.**

- [UIBackdropBlur.cpp:65](C:/SkullbonezCore/SkullbonezSource/UI/UIBackdropBlur.cpp:65) emits an ordinary rounded panel; it does not capture the backbuffer. The baked `UIBackdropBlur.hlsl` contains 25 sample sites and a stale separable-blur description, but the reviewed runtime panel path does not use it. Treat it as asset/documentation cleanup, not an active GPU hotspot.
- Text batches support 4,096 characters and shape batches 8,192 quads; empty flushes do not draw. UI draw lists are bounded at 4,096 commands. Capacity is not a performance budget: expose expanded vertices, upload bytes and actual flushes too.
- [UI.cpp:1971](C:/SkullbonezCore/SkullbonezSource/Runtime/UI/GameUI/UI.cpp:1971) can replay cached commands during position-only dragging; previews intentionally bypass this. Cached commands still pass through tessellation/upload, so this does not eliminate GPU work.
- The renderer already uses view culling, primitive instancing, stable descriptor indices and PSO caching. Normal frame pacing waits for the next allocator slot rather than globally draining all work every draw. Preserve these mechanisms.

Add UI acceptance checks for hidden/folded panels, deeply scrolled content, Targets previews, simultaneous profiler/memory overlays, narrow windows and repeated open/close cycles. Measure UI-on minus UI-off at identical scene/camera state. Record diagnostics overhead separately. Zero overflow, zero steady UI allocations and bounded memory are mandatory; numerical time budgets should be set from the agreed frame target and measured baseline.

## Audit outcome

There is enough source evidence to implement R1–R6 with focused correctness tests. R7–R11 need controlled capture-driven experiments before committing to architectural changes. No claimed runtime gain, clean DX12 validation result, or completed optimization is implied by this report. The follow-up plan defines the measurements and acceptance needed to make those claims.

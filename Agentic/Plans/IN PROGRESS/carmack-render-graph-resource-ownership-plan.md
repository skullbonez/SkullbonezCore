# Carmack Render Graph Resource Ownership Plan

Date: 2026-06-28
Status: In progress
Impact area: DX12 renderer, render graph, resource lifetime, validation, performance
Validation note: plan-only edits require no validation. PR-bound render graph,
resource barrier, descriptor, render target, or screenshot-visible work requires
`tools\validate_dx12_renderer.bat`; allocation-sensitive render work also
requires `tools\validate_perf.bat`.
Batching policy: do not run heavy repository validation for every small Carmack
slice. Cheap checks such as `git diff --check`, formatting, focused static
guardrails, graph diagnostics, or targeted builds may run per slice when useful.
Heavy gates (`tools\validate_dx12_renderer.bat`, `tools\validate_full.bat`,
`tools\validate_perf.bat`, and deep/stress gates) should run after a batch of
up to 10 completed slices, before plan completion, or before PR handoff,
whichever comes first. Run a heavy gate earlier only when the slice changes
barrier/resource lifetime, screenshot-visible pass behavior, transient
allocation policy, descriptor ownership, or leaves uncertainty that cheap checks
cannot answer.

## Completed Slices

- [x] 2026-06-28: Moved the ordinary `SkyboxPass` under live render graph
  callback ownership. `RuntimeRenderer::RenderFrame()` now schedules the
  non-cinematic skybox through `ExecuteSkyboxThroughRenderGraph()`, declaring
  the `SwapchainBackbuffer` render-target write, dry-running the callback before
  live command recording, and preserving the existing `SkyPass::Render()` body.
  Frame graph diagnostics now record `skybox_callback_owned=true`, and
  `tools\check_runtime_boundaries.py` blocks direct `m_skyPass.Render(...)`
  scheduling from returning beside the graph helper.
  Comment-style audit: inspected `RunRender.cpp`, `RuntimeRenderer.h`,
  `RenderSceneSnapshot.h`, `RenderPipeline.cpp`, and
  `tools\check_runtime_boundaries.py`; the new callback handoff documents the
  graph scheduling invariant and reuses the existing pass/resource mental model
  headers.
  Validation:
  `cmd /c tools\validate_full.bat` passed from
  `TestOutput\validation\agent_logs\skybox_graph_callback_validate_full.log`,
  including project filters, runtime boundaries, Profile and Debug builds with
  0 warnings, DX12 InfoQueue reporting 0 validation errors, matching DX12
  screenshots (`TestOutput\validation\dx12_renderer\20260628T090432Z\manifest.json`),
  standalone physics smoke, and byte-exact `physics_regression_solver.csv`.
  `Debug\dx12_frame_graph_actual.txt` shows `skybox_callback_owned=true` and
  `SkyboxPass execution=Callback`.
- [x] 2026-06-28: Added the runtime-facing manual-barrier guardrail for
  graph-owned pass families. `tools\check_runtime_boundaries.py` now scans
  `RunRender.cpp`, `RunPasses.cpp`, and `RunUiTextPass.cpp` for DX12-style
  manual barrier calls (`ResourceBarrier`, `D3D12_RESOURCE_BARRIER`) and
  backend transition helper calls (`ExecuteGraphTransition`,
  `ExecuteGraphUavBarrier`). Synthetic tests prove graph resource declarations
  and comment/string mentions are allowed while direct barriers and backend
  transition helpers are rejected. This protects migrated runtime pass code
  from sidestepping graph declarations; backend-local BLAS/TLAS/upload/DXR
  barriers remain explicit open debt under Barrier Ownership. Comment-style
  audit: inspected `tools\check_runtime_boundaries.py`; the learning header now
  names scheduling and manual-barrier regression as graph-ownership guardrails.
  Rubber-duck review found one blocking scope gap (`RunUiTextPass.cpp`) and two
  non-blocking synthetic-test gaps; all were fixed before commit.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors
  (`TestOutput\validation\agent_logs\render_graph_manual_barrier_guardrail_runtime_boundaries_after_review.log`);
  `tools\validate_fast.bat` passed
  (`TestOutput\validation\agent_logs\render_graph_manual_barrier_guardrail_validate_fast_after_review.log`).
- [x] 2026-06-28: Moved `CinematicSceneBegin` / `SceneTargetPass::Begin` into
  render graph callback ownership with declared `CinematicSceneColor` and
  `CinematicSceneDepth` writes, a dry-run before live callback execution,
  `scene_target_callback_owned` executed-frame diagnostics, and a runtime
  boundary guardrail blocking the old direct `m_sceneTargetPass.Begin(...)`
  scheduling path. `CinematicSceneDepth` intentionally imports with
  `Unknown` initial access in this transitional graph because `FramebufferDX12`
  still owns the concrete first-frame/steady-state depth transition.
  Validation:
  `python tools\check_runtime_boundaries.py` passed in 3.40s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 57.78s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.85s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_dx12_renderer.log`);
  DX12 artifact manifests:
  `TestOutput\validation\dx12_renderer\20260628T000233Z\manifest.json` and
  `TestOutput\validation\dx12_renderer\20260628T000400Z\manifest.json`
  with screenshot comparison summaries in the same directories;
  `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --scene
  SkullbonezData\scenes\cinematic_volumetric.scene.json --cinematic` passed in
  2.15s and produced a frame graph with `scene_target_callback_owned=true`,
  `CinematicSceneBegin execution=Callback`, and `CinematicSceneDepth
  initial=Unknown`
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_cinematic_frame_graph.txt`);
  `tools\validate_full.bat` passed in 27.19s
  (`TestOutput\validation\agent_logs\scene_target_graph_callback_validate_full.log`).
  Comment-style audit: inspected the touched source-bearing files
  (`RunRender.cpp`, `RuntimeRenderer.h`, `RenderSceneSnapshot.h`,
  `RenderPipeline.cpp`, and `check_runtime_boundaries.py`) against
  `Agentic\Reference\comment-style-guide.md`; existing learning headers remain
  present, and the new graph/FBO depth handoff is documented next to the
  declaration.
  Rubber-duck review: Poincare found no blockers, flagged the scene-depth
  initial-state assumption as non-blocking, and confirmed the `Unknown`
  handoff fix resolved that concern.
- [x] 2026-06-28: Added a counted runtime-boundary guardrail for render graph
  `AddExternalResource(..., RenderGraphResourceAccess::Unknown)` use in
  `RunRender.cpp` and `RenderPipeline.cpp`. The only allowed Unknown access is
  the current `CinematicSceneDepth` DX12 framebuffer handoff in each file, and
  new migrated resources with Unknown initial access now fail
  `tools\check_runtime_boundaries.py` unless they get an explicit plan-owned
  allowlist entry. Synthetic checker tests cover the allowed existing handoff,
  a rejected new two-argument Unknown graph resource, a rejected three-argument
  Unknown graph resource with a native pointer, and a rejected comma-bearing
  dynamic-name Unknown graph resource. Comment-style audit:
  inspected `tools\check_runtime_boundaries.py`; the learning header and the
  new allowlist comment document the guardrail invariant and the temporary FBO
  ownership reason.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.29s
  (`TestOutput\validation\agent_logs\render_graph_unknown_access_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 17.95s
  (`TestOutput\validation\agent_logs\render_graph_unknown_access_guardrail_validate_fast.log`).
  Rubber-duck review: Singer found a blocking false negative in the first regex
  version for valid three-argument `AddExternalResource(..., Unknown, nativePtr)`
  calls and comma-bearing dynamic resource-name expressions. The checker now
  uses a balanced argument splitter for `AddExternalResource`, includes those
  synthetic rejection cases, and validation was rerun after the fix. Singer
  rechecked the final parser and found no remaining blockers; residual risk is
  limited to the checker being source-scanner based rather than a full C++
  parser.
- [x] 2026-06-28: Completed the render graph inventory snapshot against the
  Carmack problem statement. The inventory below lists every `RuntimeRenderer`
  pass, classifies current production execution as callback-owned or manual,
  records the frame resources still owned outside the graph, and names the DX12
  transition/barrier and descriptor hot spots that block graph-owned resource
  lifetime. Validation was not run for this plan-only documentation slice; the
  snapshot records the latest branch evidence to use before moving the next pass
  family. Rubber-duck review found no blocker: the graph is useful as a
  scheduling/declaration bridge today, but transient allocation, descriptor
  lifetime, and most world pass execution remain explicit unfinished work.

## Current Handoff Slice

- [x] 2026-06-28: Implemented and committed the current render graph ownership
  slice far enough to unblock the branch build and preserve a clean handoff.
  `RenderGraph` now has API-neutral transient resource descriptors, descriptor
  needs, first/last-pass lifetime records, compatible non-overlap alias planning,
  frame-end release records, and allocation/reuse/high-water diagnostics.
  `Dx12ArchUnitTests` has CPU-only coverage for transient lifetime planning,
  compatible transient reuse, descriptor counts, and unused transient rejection.
  `RuntimeRenderer` now schedules `ShadowPass`, `ReflectionPass`,
  opaque/transparent/focus-fade `ObjectPass`, `TerrainPass`, `WaterPass`, replay
  prediction ghosts, and the already-migrated post/UI/debug passes through graph
  callbacks. `RenderSceneSnapshot` and `RenderPipeline` expose callback-owned
  status for those pass families, and `tools\check_runtime_boundaries.py` blocks
  direct scheduling calls from returning.
  Build break fixed before commit: the new `RunRender.cpp` callback data structs
  now fully qualify `SkullbonezCore::Rendering::ShadowFrameData`, which restores
  well-formed aggregate initialization for reflection, object, terrain, and
  replay-ghost pass inputs.
  Validation run before the explicit "no more validation" stop:
  `git diff --check` passed; `python tools\check_runtime_boundaries.py` passed
  with 0 errors in 5.11s
  (`TestOutput\validation\agent_logs\render_graph_completion_runtime_boundaries_final.log`);
  `tools\validate_format.bat` passed in 7.37s
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_format_final.log`);
  `tools\validate_build.bat Profile` first reproduced the build break in
  164.47s, then passed after the namespace fix in 5.71s with 0 warnings and 0
  errors
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_build_profile_final2.log`);
  `tools\validate_fast.bat` passed in 73.44s
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_fast_final.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.34s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_dx12_renderer_final.log`,
  manifest `TestOutput\validation\dx12_renderer\20260628T102812Z\manifest.json`);
  `tools\validate_perf.bat` completed in 23.52s with exit code 0, but its log
  warns that DX12 perf comparison was skipped for machine mismatch and that the
  unrelated `physics_bench` comparison showed regressions
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_perf_final.log`).
  `tools\validate_full.bat` was started before the stop; its build and DX12
  phases passed, then physics validation failed because
  `physics_regression_solver.csv` produced 373701 rows versus the 20001-row
  baseline
  (`TestOutput\validation\agent_logs\render_graph_completion_validate_full_final.log`).
  No further validation was run after the user said "no more validation".
  Remaining plan work after this commit: backend-created transient DX12
  resources/descriptors, live graph-emitted barriers/UAV ordering, and a clean
  broad physics/full gate remain open.

## Problem Statement

The Carmack-test verdict credited DX12 validation and render-graph progress but
did not call the render architecture finished. The graph records pass/resource
intent and can execute callback-owned passes, but transient resource ownership,
descriptor lifetime, and full production pass/resource scheduling are not yet
owned end to end by the graph.

## Goal

Make the render graph the production owner of frame pass execution, resource
state transitions, transient render-target lifetime, and validation diagnostics,
while keeping DX12-specific emission inside the backend executor.

## Success Bar

- All frame passes declare their reads, writes, and callback execution through
  the graph.
- Ordinary render targets and transient resources are allocated, reused, and
  released through graph-owned lifetime policy.
- Manual barriers are reduced to reviewed compatibility cases with guardrails.
- DX12 validation remains zero-error and screenshot baselines stay clean.

## Related Plans

- `Agentic/Plans/render-graph-irender-interface-plan.md` is the active renderer
  umbrella plan. Use this Carmack plan as the graph-resource-ownership
  acceptance checklist for that work.
- `Agentic/Plans/IN PROGRESS/carmack-render-backend-capability-plan.md` covers
  capability-interface narrowing. This plan covers production pass execution,
  transient resource lifetime, descriptor lifetime, and barrier ownership.
- `Agentic/Plans/runtime-static-allocation-policy-plan.md` owns the allocation
  policy that graph transient-resource work must satisfy.

## Implementation Checklist

### Inventory

- [x] List every `RuntimeRenderer` pass and whether it is callback-owned,
  declaration-only, or manual.
- [x] List every render target, framebuffer, backbuffer, depth target, shadow map,
  reflection target, volumetric target, water target, and UI/dynamic buffer used
  in a frame.
- [x] List every manual transition, UAV barrier, resource release hook, and
  backend state change that is not graph-owned.
- [x] List all DX12 descriptor heaps, descriptor ranges, and resource handles used
  by migrated passes.
- [x] Capture the current DX12 validation and screenshot baseline evidence before
  moving the next pass family.

#### Inventory Snapshot, 2026-06-28

Production pass ownership in `RuntimeRenderer`:

| Pass or phase | Production execution owner | Current graph role |
|---------------|---------------------------|--------------------|
| Backbuffer clear | Manual in `RuntimeRenderer::RenderFrame()` | Diagnostic-only in executed-frame dump. |
| `ShadowPass` terrain/object maps | Callback-owned in current slice; Profile, fast, and DX12 validated | Live `ShadowMapPass` callback wrapper declares terrain/object shadow-map writes. |
| `SkyPass` cubemap/cinematic sky | Ordinary cubemap sky is callback-owned; cinematic begin calls sky through callback-owned scene target | Live `SkyboxPass` callback for ordinary backbuffer write; cinematic begin callback handoff. |
| `ReflectionPass` raster or DXR | Callback-owned in current slice; Profile, fast, and DX12 validated | Live callback wrapper declares raster color/depth or DXR reflection writes plus object-shadow reads. |
| `SceneTargetPass::Begin` | Callback-owned when cinematic target is live | Live `CinematicSceneBegin` graph callback with color/depth writes. |
| `ObjectPass` opaque | Callback-owned in current slice; Profile, fast, and DX12 validated | Live callback wrapper declares frame color/depth writes and object-shadow reads. |
| `TerrainPass` | Callback-owned in current slice; Profile, fast, and DX12 validated | Live callback wrapper declares frame color/depth writes and terrain-shadow reads. |
| `WaterPass` | Callback-owned in current slice; Profile, fast, and DX12 validated | Live callback wrapper declares reflection reads and frame color/depth writes. |
| `TornadoVisualPass` | Callback-owned | Live graph callback with color/depth writes. |
| `ObjectPass` transparent/focus fade | Callback-owned in current slice; Profile, fast, and DX12 validated | Live callback wrapper declares frame color/depth writes and object-shadow reads. |
| Replay prediction ghosts | Callback-owned in current slice; Profile, fast, and DX12 validated | New `ReplayPredictionGhostPass` callback wrapper declares frame color/depth writes and optional object-shadow reads. |
| `DebugOverlayPass` | Callback-owned | Live graph callback with color/depth writes. |
| `VolumetricPass` | Callback-owned when cinematic volumetric is enabled and ready | Live `VolumetricLightPass` callback with scene color/depth reads and volumetric write. |
| `TonemapPass` | Callback-owned when cinematic target is live | Live `ToneMapPass` callback with scene/volumetric reads and backbuffer write. |
| `UiTextPass` | Callback-owned in the late UI frame path | Live `UiTextPass` callback writing the backbuffer. |
| Present | Manual lifecycle call in `RunFrame.cpp` | Diagnostic-only `Present` pass in executed-frame dump. |

Frame resources and owners:

| Resource | Current owner | Graph name or status |
|----------|---------------|----------------------|
| Swap-chain backbuffer | DX12 backend/swap chain | `SwapchainBackbuffer`, imported external. |
| Main depth stencil | DX12 backend | `MainDepthStencil`, imported external. |
| Cinematic HDR color/depth | `CinematicScenePassResources::hdrTarget` framebuffer | `CinematicSceneColor` and `CinematicSceneDepth`; depth still imports with `Unknown` in handoff paths. |
| Terrain/object shadow maps | `ShadowPassResources::terrainTarget` and `objectTarget` framebuffers | `TerrainShadowMapDepth` and `ObjectShadowMapDepth`; diagnostic graph only. |
| Raster reflection color/depth | `ReflectionPassResources::target` framebuffer | `RasterReflectionColor` and `RasterReflectionDepth`; diagnostic graph only. |
| DXR reflection texture | DX12 raytracing backend UAV/SRV texture | `DxrReflectionTexture`; diagnostic graph only for write/read intent. |
| Volumetric light target | `VolumetricLightPassResources::target` framebuffer | `VolumetricLight`; callback-owned pass uses imported external target. |
| Fullscreen quad dynamic VB | `FullscreenPassResources::quadVB` | Not graph-owned; shared by sky, volumetric, and tonemap. |
| UI/text font texture and dynamic VBs | `Text2d` static resources plus `UiTextPass` scheduling | Not graph-owned; UI pass only declares the backbuffer write. |
| Material/object textures and instanced meshes | Asset, texture, and render helper systems | Not graph-owned; bound through existing engine texture/mesh handles. |

Manual transition, barrier, and release hot spots:

| Hot spot | Current owner | Graph gap |
|----------|---------------|-----------|
| `FramebufferDX12::Bind/Unbind` | FBO object transitions color/depth between render target/depth write and shader-resource states. | Graph records intent but does not own FBO first-state or steady-state transitions. |
| `RenderBackendDX12::PrepareDraw` / backbuffer prep | Backend transitions present/backbuffer state before drawing. | Backbuffer transitions are backend-owned outside compiled graph execution. |
| Texture upload and mip generation | `RenderBackendDX12.Textures.cpp` uses graph executor helpers and UAV barriers. | Not tied to frame pass declarations or transient resource policy. |
| Mesh upload | `MeshDX12.cpp` emits final vertex-buffer transition through graph executor helper. | Load-time resource transition, not frame graph-owned. |
| DXR BLAS/TLAS builds | `BLASDX12.cpp` and `TLASDX12.cpp` emit raw UAV `ResourceBarrier()` calls after acceleration-structure builds. | UAV ordering is not graph-declared yet. |
| DXR reflection texture | `RenderBackendDX12.DXR.cpp` owns UAV/SRV descriptors and state toggling. | Graph has diagnostic resource name only; descriptor and state lifetime stay backend-local. |
| Backbuffer readback | `RenderBackendDX12.Readback.cpp` transitions to copy source and restores state. | Capture path is outside frame graph ownership. |
| Pass resource release | `RuntimeRenderer::ReleaseBackendOwnedResources()` and pass `ReleaseGpuResources()` methods. | Release order is manual, consumer-before-producer, not graph lifetime policy. |

Descriptor and handle inventory:

| Descriptor or handle family | Current owner | Graph ownership gap |
|-----------------------------|---------------|---------------------|
| RTV/DSV CPU descriptor rows | `Dx12CpuDescriptorAllocator` and framebuffer/swap-chain resources. | Graph does not allocate render/depth descriptors for transient targets. |
| Static SRV/UAV rows | `Dx12DescriptorAllocator::AllocateStatic()` for textures, FBO SRVs, null descriptors, and DXR views. | Lifetime follows backend objects, not graph resource descriptors. |
| Transient shader-visible ranges | `Dx12DescriptorAllocator::AllocateTransient()` / `AllocateTransientRange()` during draw/dispatch. | Per-frame descriptor reuse is backend-owned and not tied to graph pass lifetimes. |
| Engine texture handles | Backend texture registry, `IFramebuffer` texture handles, `IRenderRayTracing::GetReflectionUAVTexture()`. | Graph records names, not handles; binding still uses ad hoc runtime/backend lookups. |
| Native DX12 resource pointers | Optional diagnostic identity in `RenderGraphResourceDesc::nativeResource`. | Diagnostic-only; graph must not dereference or release native resources. |

Latest validation evidence available before the next pass-family migration:

- `tools\validate_dx12_renderer.bat` passed in 17.64s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_dx12_renderer.log`).
- `tools\validate_full.bat` passed in 28.76s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_dxr_capability_validate_full.log`).
- Prior render-graph callback/Unknown-access guardrail slices have their own
  DX12 and fast validation logs listed in Completed Slices above.

### Pass Execution Ownership

- [x] Move one additional low-risk pass family under graph callback execution.
- [x] Keep pass body behavior unchanged during the first migration slice.
- [x] Require each callback-owned pass to declare at least one read or write.
- [x] Validate callback dry-run before execute mode.
- [x] Record callback-owned status in frame graph diagnostics.
- [x] Move ordinary `SkyboxPass` scheduling under a graph callback with a
  declared `SwapchainBackbuffer` write and `skybox_callback_owned` diagnostics.
- [x] Repeat pass migration in small slices until every production render pass
  body is graph callback-owned. Current slice moved the remaining world pass
  bodies and replay ghosts behind graph callbacks. Present/backbuffer lifecycle,
  backend-created resources, and backend-local barriers remain tracked below.

### Resource Declaration

- [ ] Give every frame resource a stable graph name.
- [ ] Assign concrete `RenderGraphResourceAccess` values for every read/write.
- [ ] Replace `Unknown` access with explicit initial or imported states where the
  backend can know them.
- [ ] Add subresource declarations where one texture uses different subresources.
- [ ] Keep native DX12 resource identity diagnostic-only in API-neutral graph records.

### Transient Resource Lifetime

- [x] Add transient resource descriptors for graph-owned render targets.
  Implemented in `RenderGraphTransientResourceDesc`; direct unit execution is
  still deferred, but Profile/fast/DX12 builds compile the added coverage.
- [ ] Add a graph allocator or backend executor path that creates transient DX12
  resources from descriptors.
- [x] Reuse transient resources only when lifetimes do not overlap and descriptor
  compatibility is proven.
  Implemented in `RenderGraph::Compile()` through compatible non-overlap alias
  planning and allocation diagnostics.
- [ ] Release transient resources through graph/backend lifetime policy, not pass
  destructors scattered across runtime code.
  In-flight graph release diagnostics exist; backend-owned pass destructors still
  own existing imported FBOs.
- [x] Record resource allocation, reuse, high-water, and release diagnostics.
  Implemented in `RenderGraphTransientAllocationDiagnostics`.
- [ ] Ensure transient allocation does not introduce steady-frame heap growth.

### Barrier Ownership

- [ ] Route graph-compiled transition records through the DX12 graph executor for
  live barrier emission.
- [ ] Route UAV ordering through explicit graph-owned policy.
- [ ] Remove pass-local or backend-local barriers after graph output is proven
  equivalent.
- [ ] Add diagnostics that compare expected graph transitions with emitted DX12
  barriers during validation.
- [ ] Fail validation on unknown states that should be concrete by the migrated
  phase.

### Descriptor And Resource Binding

- [ ] Name which system owns descriptor allocation for graph-created resources.
- [ ] Make descriptor lifetime follow graph resource lifetime.
- [ ] Keep material/object descriptor tables separate from transient frame target
  descriptors.
- [ ] Ensure graph-owned resources can be sampled, rendered into, and captured by
  screenshot validation without ad hoc backend lookups.

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to reject direct scheduling of
  passes that have migrated to graph callback ownership.
- [x] Add guardrails for new manual barriers in migrated pass families.
- [x] Add guardrails for new `Unknown` access in migrated resources unless
  explicitly allowlisted.
- [x] Add synthetic checker tests for migrated pass scheduling and manual-barrier
  rejection.

## Validation Checklist

- [ ] Batch heavy validation after up to 10 completed Carmack slices, before plan
  completion, or before PR handoff; do not run DX12/full/perf gates for every
  tiny pass migration unless the change is high-risk.
- [ ] For plan-only edits: no validation required.
- [x] After pass-migration batches: include
  `tools\validate_dx12_renderer.bat` in the next heavy validation batch.
- [x] For barrier or resource lifetime changes: include
  `tools\validate_dx12_renderer.bat` in the next heavy validation batch and
  verify `dx12_validation.txt` is zero-error.
- [x] For transient allocation or descriptor storage changes: include
  `tools\validate_perf.bat` in the next heavy validation batch.
  Current slice completed with exit code 0, but the log includes machine-mismatch
  and unrelated `physics_bench` regression warnings.
- [ ] For broad runtime render host changes: include `tools\validate_full.bat`
  in the next heavy validation batch.
  Current slice attempted this before the stop. Build and DX12 phases passed,
  then physics validation failed on `physics_regression_solver.csv` row count;
  no more validation was run after the user requested it.
- [x] Save manifest paths and screenshot diff artifacts in the handoff.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to verify graph declarations match actual pass behavior.
- [x] Ask the reviewer to inspect resource lifetime, descriptor lifetime, and barrier ordering.
- [x] Ask the reviewer to look for false confidence from declaration-only graph output.
- [x] Ask the reviewer to check for new per-frame allocation in graph diagnostics.
- [x] Resolve blocking review findings before committing PR-bound code.

## Definition Of Done

- [ ] Production frame pass execution is graph-owned.
- [ ] Transient frame resources are graph-owned or explicitly imported.
- [ ] Manual barriers are gone from migrated pass families or explicitly reviewed.
- [x] DX12 validation and screenshot baselines pass at batch gates, plan
  completion, or PR handoff.
- [ ] Performance evidence shows graph ownership did not introduce recurring
  steady-frame allocation or measurable hot-path regression.

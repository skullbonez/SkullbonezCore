# DX12 Render Graph Completion Plan

Status: proposed  
Created: 2026-06-16  
Branch: `codex/post-pr73-roadmap`  
Scope: DX12 resource-state ownership, render graph execution, framebuffer/backbuffer/readback/DXR/mip barriers

## Purpose

Finish the render graph migration all the way from the current diagnostic
skeleton to production resource-state ownership.

The current renderer already has a useful `RenderGraph` contract:

- resources and passes can be named,
- graph declarations compile into API-neutral transition records,
- the backend dumps graph transitions next to live DX12 transition barriers,
- `VolumetricLightPass` and `ToneMapPass` are marked as handoff-reviewed
  declarations.

What is not done: live rendering still depends on hand-written DX12 barriers in
`Clear()`, `PrepareDraw()`, `FramebufferDX12::Bind/Unbind()`,
`DispatchReflectionRays()`, `GenerateMipsGPU()`, screenshot readback, dynamic
geometry upload, and `Present()`.

The goal is to make the graph the owner of resource transitions without turning
the renderer into a risky rewrite. The migration should move one resource family
at a time, keep pass bodies recognizable, and use DX12 validation after every
production slice.

## Validation Rule

This plan touches DX12 renderer state and barriers. Each implementation slice
must run the DX12 renderer gate before commit:

```bat
tools\validate_dx12_renderer.bat
```

If a slice changes validation tooling, run:

```bat
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
```

If a slice changes hot per-frame barrier scheduling or graph compilation in a
way that could affect frame cost, also run:

```bat
tools\validate_perf.bat
```

The final completion PR should run `tools\validate_full.bat` after the narrower
DX12 renderer gate is already clean.

## Current State

Source anchors:

| Area | Current source |
|------|----------------|
| Diagnostic graph declaration | `SkullbonezSource/SkullbonezRenderBackendDX12.cpp`, `DumpFrameGraphSkeleton()` |
| Graph compile contract | `SkullbonezSource/SkullbonezRenderGraph.h/.cpp` |
| Graph/live comparison dump | `Debug/dx12_frame_graph_skeleton.txt` |
| Live transition wrapper | `RenderBackendDX12::TransitionBarrier()` |
| Live barrier sample records | `LiveBarrierRecordDX12` |
| FBO manual barriers | `SkullbonezSource/SkullbonezFramebufferDX12.cpp`, `Bind()`/`Unbind()` |
| Backbuffer manual barriers | `Clear()`, `PrepareDraw()`, `Present()`, screenshot readback |
| DXR reflection manual barriers | `SkullbonezSource/SkullbonezRenderBackendDX12.DXR.cpp` |
| GPU mip manual barriers | `SkullbonezSource/SkullbonezRenderBackendDX12.Textures.cpp` |

Important constraints:

- DX12 is the only runtime renderer.
- The graph must stay API-neutral at the engine contract layer.
- D3D12 types should remain inside DX12 backend code or DX12-specific helper
  files.
- The first live graph slice should not move draw commands, pass order, or
  shader binding. It should only move barrier ownership.
- The old diagnostic comparison remains useful until every live transition is
  graph-owned.

## Definition Of Done

The render graph migration is complete when:

- production transition barriers are emitted by a graph-owned DX12 execution
  path, not scattered ad hoc call sites,
- the frame graph is built from the actual frame path, not only a diagnostic
  superset,
- off-screen framebuffer color/depth, swapchain backbuffers, DXR reflection,
  texture mip generation, screenshot readback, dynamic upload transitions, and
  present transitions are all represented as graph resources/uses,
- graph transition records carry enough resource identity to diagnose exact
  resources, subresources, and UAV ordering points,
- raw `ID3D12GraphicsCommandList::ResourceBarrier()` calls are restricted to
  the graph executor and any narrowly documented D3D12 object-build helpers,
- old live-vs-graph mismatch telemetry is replaced by graph-owned barrier
  telemetry,
- `dx12_validation.txt` reports zero DX12 validation errors,
- DX12 screenshot baselines pass,
- final validation is recorded in a report or commit note.

## Migration Principles

1. Keep pass bodies first.
   Move barrier ownership before moving command recording into pass callbacks.

2. Prefer exact resource identity.
   Name-only matching is useful telemetry, but graph execution needs native
   resource identity, subresource coverage, and current-state tracking.

3. Migrate resources by family.
   Do not mix FBO, backbuffer, DXR, mip generation, readback, and buffer upload
   ownership in one commit.

4. Preserve existing state flags until replaced.
   `m_backBufferIsRT`, `m_reflectionInSRVState`, and framebuffer depth state are
   transition crutches. Replace them only after the graph state tracker proves
   equivalent behavior.

5. Keep low-level emission boring.
   The graph executor can still call `ResourceBarrier()`. The win is that only
   the executor decides when and why transitions happen.

6. Treat diagnostics as part of the feature.
   Every migrated slice should improve the dump enough that a future failure
   identifies the pass, resource, before state, after state, and source helper.

## Phase 0: Barrier Inventory And Baseline Dump

Status: not started

Goal: make the current manual barrier surface explicit before replacing it.

Work:

- List every production `ResourceBarrier()`, `TransitionBarrier()`, and
  `RecordLiveBarrier()` call.
- Classify each barrier as:
  - framebuffer color/depth,
  - swapchain backbuffer,
  - screenshot readback,
  - DXR reflection,
  - GPU mip generation,
  - texture upload finalization,
  - dynamic/static buffer upload,
  - BLAS/TLAS/SBT build support,
  - one-off shutdown recovery.
- Capture the current graph dump shape from a DX12 renderer validation run.
- Identify which resources have native pointer identity today and which are
  still name-only in the graph skeleton.

Deliverable:

- Update this plan with an inventory table, or add a short dated report under
  `Agentic/Reports/`.

Validation:

- Source inspection does not require validation.
- If a baseline dump is generated through a renderer launch, quote the launch
  command and relevant output in the report.

Commit boundary:

- Documentation-only commit if only the inventory/report changes.

## Phase 1: Add A DX12 Graph Barrier Executor In Dry-Run Mode

Status: not started

Goal: introduce the production-shaped execution helper without changing command
recording behavior yet.

Work:

- Add a DX12-specific graph execution helper near the backend, for example
  `Dx12RenderGraphExecutor` or a tightly scoped `RenderBackendDX12` helper.
- Translate `RenderGraphResourceAccess` to `D3D12_RESOURCE_STATES` in one shared
  place.
- Accept a `RenderGraphCompileResult` plus pass/resource metadata and produce a
  list of candidate DX12 barriers.
- Add a mode that records what would be emitted, but does not call
  `ResourceBarrier()`.
- Add source labels such as `GraphDryRun:FramebufferUnbind`,
  `GraphDryRun:BackbufferPresent`, and `GraphDryRun:DxrReflection`.
- Keep `TransitionBarrier()` as the live path during this phase.
- Add unit coverage in `Agentic\Tests\Dx12ArchUnitTests` for:
  - Unknown initial access does not emit a fake COMMON transition,
  - Present to render target maps correctly,
  - Render target to shader resource maps correctly,
  - UAV access requests are identified even before UAV barrier emission exists.

Expected result:

- No behavior change.
- A single helper owns graph-to-DX12 transition translation.
- The dump can show graph dry-run barriers next to legacy live barriers.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for the dry-run executor plus unit test updates.

## Phase 2: Add Resource State Identity And Subresource Coverage

Status: not started

Goal: make graph-owned barriers precise enough for real DX12 execution.

Work:

- Extend graph transition records to include subresource scope:
  - all subresources,
  - a single mip/slice,
  - future range support if needed.
- Add graph support for UAV ordering points, separate from transition barriers.
  A UAV barrier is not a state transition, so it should not be modeled as
  `before -> after`.
- Add a backend resource-state tracker keyed by native resource pointer and
  subresource. It should answer:
  - current known state,
  - whether a state is unknown because legacy code still owns it,
  - which pass last changed it.
- Add exact native identity for:
  - swapchain backbuffers,
  - main depth,
  - framebuffer color textures,
  - framebuffer depth textures,
  - DXR reflection texture,
  - uploaded textures used by mip generation,
  - dynamic/static buffers that need upload transitions.
- Avoid leaking D3D12 types into engine-neutral interfaces. If framebuffer
  identity must cross an interface, use opaque diagnostic handles or a
  renderer-neutral resource snapshot, then downcast only inside the DX12 backend
  where unavoidable.
- Add or update comments so future agents understand the difference between:
  - graph handle,
  - native D3D12 resource pointer,
  - descriptor index,
  - engine texture handle.

Expected result:

- Graph diagnostics can identify exact FBO color/depth resources.
- Mip-generation transitions can eventually model individual subresources.
- State flags such as framebuffer depth state have a migration target.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for graph data shape and state-tracker scaffolding.
- No production barrier replacement unless the diff remains very small.

## Phase 3: First Production Graph-Owned Barrier Slice

Status: not started

Goal: replace one real manual barrier path with graph-owned emission.

Preferred target:

- Off-screen framebuffer color/depth transitions around a single post-style
  pass boundary, ideally the cinematic scene-to-volumetric/tonemap path.

Why this target:

- It avoids swapchain present ownership.
- It avoids DXR dispatch complexity.
- It uses existing pass resources: scene color, scene depth, volumetric target,
  and fullscreen tonemap.
- The skeleton already marks `VolumetricLightPass` and `ToneMapPass` as
  `HandoffValidated`.

Selection gate:

- Before editing production barriers, confirm the target resource has:
  - exact native pointer identity,
  - known current state before the transition,
  - one expected after state,
  - no aliasing with the pass write target,
  - a clean graph/live match in the diagnostic dump.

Work:

- Add a tiny graph declaration for the chosen boundary.
- Compile it.
- Emit the compiled transition through the DX12 graph executor.
- Record it as graph-owned telemetry, for example
  `GraphOwned:VolumetricSceneRead`.
- Remove the equivalent hand-written transition for that boundary only.
- Keep all neighboring pass body code unchanged.
- Add a debug assertion or diagnostic warning if the graph tries to emit a
  transition with unknown source state.

Expected result:

- One production barrier is graph-owned.
- Rendering output is unchanged.
- The dump proves the migrated transition came from graph execution.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for the first graph-owned production transition.

Rollback:

- Restore the one removed manual transition and leave the executor/dry-run
  scaffolding in place.

## Phase 4: Move FramebufferDX12 Bind/Unbind To Graph-Owned Transitions

Status: not started

Goal: make all off-screen framebuffer color/depth state changes graph-owned.

Work:

- Add explicit color state tracking to `FramebufferDX12`; depth already has a
  local state but should become tracker-backed.
- Replace `FramebufferDX12::Bind()` color SRV-to-RTV transition with a graph
  declaration and graph-owned emission.
- Replace `FramebufferDX12::Bind()` depth SRV/depth-read-to-depth-write
  transition with graph-owned emission.
- Replace `FramebufferDX12::Unbind()` color RTV-to-SRV transition with
  graph-owned emission.
- Replace `FramebufferDX12::Unbind()` depth-write-to-SRV transition with
  graph-owned emission.
- Preserve `SetRenderingToFBO()`, texture-slot clearing, saved RTV/DSV restore,
  viewport, and pass body behavior.
- Update the graph dump so FBO transitions show the framebuffer purpose:
  shadow terrain, shadow object, raster reflection, cinematic scene,
  volumetric light, or fallback unnamed FBO.

Expected result:

- Shadow-map, reflection, cinematic scene, volumetric, and any other FBO
  transitions are no longer hand-written in `FramebufferDX12`.
- Raw `ResourceBarrier()` use in `FramebufferDX12.cpp` is gone.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for framebuffer graph-owned transitions.

Extra review points:

- Shadow depth sampling must still work.
- Cinematic depth sampling must still work.
- Reflection target readback by water must still work.
- No FBO texture may be sampled while bound for writing.

## Phase 5: Move Backbuffer Begin/End Transitions To The Graph

Status: not started

Goal: move swapchain backbuffer state changes out of `Clear()`, `PrepareDraw()`,
and `Present()`.

Work:

- Represent each swapchain backbuffer as a graph resource with exact native
  identity and current state.
- Replace `Clear()`'s present-to-render-target transition with graph-owned
  emission.
- Replace `PrepareDraw()`'s lazy present-to-render-target transition with
  graph-owned emission.
- Replace `Present()`'s render-target-to-present transition with graph-owned
  emission.
- Keep `m_backBufferIsRT` during the first version as a compatibility shadow.
- Once graph state tracking is proven, replace `m_backBufferIsRT` with a query
  to the state tracker or a narrow facade method.
- Preserve shutdown's defensive final present transition until normal present
  ownership is proven. Then migrate shutdown recovery separately.

Expected result:

- The swapchain render/present state path is graph-owned during normal frames.
- Present remains the same DXGI call; only the barrier ownership changes.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for normal-frame backbuffer graph transitions.
- A separate commit for shutdown/recovery cleanup if needed.

Extra review points:

- Screenshot captures can happen before or after frame rendering.
- `Clear()` must still work when the frame begins from PRESENT state.
- `PrepareDraw()` must still handle draw calls that happen before explicit clear.

## Phase 6: Move Screenshot Readback Transitions To The Graph

Status: not started

Goal: make temporary backbuffer copy-source transitions graph-owned.

Work:

- Model readback as a short graph scope:
  - current backbuffer state to `CopySource`,
  - copy operation,
  - `CopySource` back to the exact previous state.
- Preserve the existing fence wait, readback buffer lifetime, row footprint, and
  image copy logic.
- Record the previous state from the graph state tracker instead of manually
  deriving it from `m_backBufferIsRT`.
- Keep the readback buffer itself outside transition modeling unless a future
  validation issue requires it; readback buffers are CPU mapped and effectively
  COMMON for this path.

Expected result:

- `CaptureBackbuffer()` no longer calls `TransitionBarrier()` directly.
- Screenshots work before clear, after render, and during scene-driven capture.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for screenshot readback transitions.

## Phase 7: Move DXR Reflection Transitions And UAV Ordering To The Graph

Status: not started

Goal: make DXR reflection's SRV/UAV state and write-read ordering graph-owned.

Work:

- Model `DxrReflectionTexture` as one graph resource with exact native identity.
- Add graph declarations for:
  - SRV to UAV before `DispatchRays`,
  - UAV ordering after `DispatchRays`,
  - UAV to SRV before raster water samples the result.
- Ensure UAV barriers are emitted as UAV barriers, not fake transitions.
- Replace `m_reflectionInSRVState` with graph state tracking after one
  validation cycle where both agree.
- Keep DXR root signature, SBT, TLAS build, descriptor heap binding, and shader
  parameters unchanged.

Expected result:

- `DispatchReflectionRays()` no longer owns manual reflection texture barriers.
- Water still samples the DXR reflection result safely.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for DXR reflection graph barriers.

Extra review points:

- Validate scenes with DXR reflection enabled.
- Confirm raster reflection fallback still works.
- Confirm the UAV barrier appears in diagnostics as an ordering point.

## Phase 8: Move GPU Mip Generation To Graph-Owned Subresource Barriers

Status: not started

Goal: migrate the most complex transition path after subresource support is
proven elsewhere.

Work:

- Represent texture mip subresources in graph transitions.
- Model:
  - mip 0 `CopyDest` to `NonPixelShaderResource`,
  - destination mips `CopyDest` to `UnorderedAccess`,
  - per-dispatch UAV ordering,
  - generated mips `UnorderedAccess` to `NonPixelShaderResource`,
  - all subresources `NonPixelShaderResource` to `PixelShaderResource`.
- Preserve existing descriptor allocation and compute dispatch logic.
- Keep null UAV descriptor padding unchanged.
- Replace direct `RecordLiveBarrier()` and `ResourceBarrier()` calls in
  `GenerateMipsGPU()`.

Expected result:

- GPU-generated mip transitions are graph-owned and subresource-aware.
- The graph executor supports both all-subresource and per-subresource
  transition emission.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Commit boundary:

- One commit for graph-owned mip generation transitions.

Extra review points:

- Texture upload final state must still match shader sampling expectations.
- The debug layer must not report invalid subresource states.

## Phase 9: Move Texture Upload And Dynamic Buffer Finalization Barriers

Status: not started

Goal: remove remaining upload/finalization transition calls from texture and
dynamic geometry code.

Work:

- Model uploaded textures as graph resources during creation/finalization.
- Replace texture final `CopyDest` to `PixelShaderResource` transitions with
  graph-owned emission.
- Model dynamic/static vertex buffer finalization where buffers move from
  `CopyDest` to vertex/non-pixel shader readable states.
- Decide whether BLAS/TLAS build resource barriers belong in the same graph
  executor or in a lower-level acceleration-structure build helper. Document the
  choice before editing them.

Expected result:

- Texture and dynamic geometry upload paths no longer call `TransitionBarrier()`
  directly for final states.
- Remaining raw barriers are either graph-owned or explicitly deferred
  acceleration-structure internals.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

Add performance validation if per-frame dynamic geometry barriers are moved:

```bat
tools\validate_perf.bat
```

Commit boundary:

- One commit for texture upload finalization.
- One commit for dynamic/static buffer finalization if touched.

## Phase 10: Build The Actual Per-Frame Render Graph

Status: not started

Goal: replace the diagnostic superset graph with a graph built from the actual
frame path.

Work:

- Add a `RenderGraphFrameBuilder` or equivalent runtime/backend collaboration
  point.
- Build graph resources from the live frame:
  - active swapchain backbuffer,
  - main depth,
  - shadow FBOs if shadows are active,
  - raster reflection FBO if raster reflection is active,
  - DXR reflection texture if DXR reflection is active,
  - cinematic scene target if cinematic mode is active,
  - volumetric target if volumetric lighting is active,
  - any readback/capture resources for this frame.
- Build only the passes that will run this frame.
- Preserve current pass body calls from `SkullbonezRun::DrawPrimitives()`.
- Before each pass body, ask the graph executor to apply transitions for that
  pass.
- After the frame, dump the actual graph and transition summary.
- Keep the old skeleton dump around temporarily as `dx12_frame_graph_skeleton`
  only if it still adds review value; otherwise replace it with the actual
  graph dump.

Expected result:

- The graph is no longer only a diagnostic superset.
- Optional paths such as cinematic rendering and DXR reflection are represented
  only when they execute.
- Pass order remains visibly owned by the current runtime while barrier
  scheduling moves to the graph.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

If runtime frame orchestration changes substantially:

```bat
tools\validate_full.bat
```

Commit boundary:

- One commit for actual frame graph construction.
- One commit for replacing/deprecating the skeleton dump if needed.

## Phase 11: Remove Legacy Manual Barrier APIs From Production Paths

Status: not started

Goal: make graph-owned barrier emission the enforceable production rule.

Work:

- Rename the raw emission helper to make it private and graph-owned, for example
  `EmitDx12BarrierFromGraph()` or `EmitTransitionBarrierUnchecked()`.
- Remove or deprecate `TransitionBarrier()` as a general backend helper.
- Delete live-vs-graph matching that compares against legacy manual barriers
  once no legacy path remains.
- Replace it with graph execution telemetry:
  - emitted transition count,
  - emitted UAV barrier count,
  - skipped same-state transition count,
  - unknown-state warning count,
  - per-pass transition details.
- Add a source search check to docs or validation notes:
  - `ResourceBarrier(` should appear only in graph executor and documented
    low-level D3D12 build helpers.
- Update comments in old call sites so they describe graph ownership rather
  than hand-written DX12 barrier policy.

Expected result:

- Production pass code no longer hand-rolls DX12 resource transitions.
- The graph executor is the normal barrier path.

Validation:

```bat
tools\validate_dx12_renderer.bat
tools\validate_perf.bat
```

Commit boundary:

- One commit for API cleanup and telemetry replacement.

## Phase 12: Move Pass Command Recording Into Graph Callbacks

Status: not started

Goal: complete the architectural shift from "runtime calls passes and graph
applies barriers" to "graph owns pass execution order and barriers."

This phase should start only after graph-owned barrier emission is stable.

Work:

- Add optional pass callbacks to `RenderGraphPassDesc` or a parallel executable
  graph structure.
- Keep callbacks small and call the existing pass methods first.
- Move one fullscreen/post pass into graph callback execution.
- Then migrate:
  - volumetric,
  - tonemap,
  - sky,
  - shadow,
  - reflection,
  - terrain/object/water/debug overlay.
- Keep resource declaration and command recording adjacent enough that reviewers
  can see pass inputs/outputs and pass body together.
- Do not move physics, scene loading, or asset creation into the graph.

Expected result:

- The graph owns pass order, pre-pass transitions, command callback execution,
  and post-frame diagnostics.
- `SkullbonezRun::DrawPrimitives()` becomes a frame-graph builder and high-level
  render coordinator rather than the direct pass scheduler.

Validation:

```bat
tools\validate_dx12_renderer.bat
```

If runtime orchestration changes broadly:

```bat
tools\validate_full.bat
```

Commit boundary:

- Multiple small commits, one pass family at a time.

## Phase 13: Final Documentation, Reports, And Guardrails

Status: not started

Goal: leave the completed migration understandable and hard to regress.

Work:

- Update `Agentic/Reference/render-backend-portability-contract.md`.
- Update `Agentic/Reference/skullbonez-core-class-structure.md`.
- Update `Agentic/SessionState.md` with the new current state.
- Add a dated validation report under `Agentic/Reports/`.
- Add a short source-search checklist for future PR reviewers:
  - where graph execution lives,
  - where raw barriers are allowed,
  - how to inspect the frame graph dump,
  - what validation command proves the migration.
- Update this plan's statuses or move it to `Agentic/Plans/Done/` after the
  final validated commit lands.

Validation:

Documentation-only updates require no validation by themselves. If this phase
is part of the final code PR, include the final code validation output:

```bat
tools\validate_full.bat
```

Commit boundary:

- One docs/report commit after final code validation.

## Suggested Commit Sequence

1. `docs: inventory dx12 render barrier migration`
2. `feat: add dry-run dx12 graph barrier executor`
3. `feat: add graph resource state identity tracking`
4. `fix: move first post barrier to graph ownership`
5. `feat: move framebuffer transitions to render graph`
6. `feat: move backbuffer transitions to render graph`
7. `feat: move screenshot readback transitions to render graph`
8. `feat: move dxr reflection barriers to render graph`
9. `feat: move mip generation barriers to render graph`
10. `feat: move upload finalization barriers to render graph`
11. `feat: build actual frame render graph`
12. `refactor: remove legacy manual barrier path`
13. `feat: execute render passes through render graph callbacks`
14. `docs: record completed render graph migration`

The exact subject prefixes can change, but each commit body should record:

- what resource family moved,
- which manual barriers were removed,
- what graph diagnostics now show,
- validation command and meaningful result.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Incorrect initial state emits invalid barrier | Keep unknown-state diagnostics fatal for graph-owned production transitions. |
| Graph state tracker disagrees with legacy flags | Run one slice with both tracked and compared before deleting the old flag. |
| FBO migration affects many passes at once | Start with one post boundary, then migrate all `FramebufferDX12` transitions after identity tracking is stable. |
| UAV barriers are modeled as fake transitions | Add explicit graph UAV ordering records before DXR and mip migration. |
| Mip subresources regress texture sampling | Do mip migration only after subresource graph support has unit tests and FBO/backbuffer transitions are stable. |
| Present/readback paths fight over backbuffer state | Model screenshot readback as restore-to-previous-state and keep a compatibility shadow until validated. |
| Per-frame graph compilation adds overhead | Keep compile data small, avoid per-draw graph declarations, and run `tools\validate_perf.bat` after hot paths move. |
| Diagnostics become too noisy | Bound detailed dumps and summarize counts, but keep full pass/resource/source labels for failures. |

## Open Design Choices

Resolve these before Phase 3:

- Should graph-owned transition emission live in a new `Dx12RenderGraphExecutor`
  file, or stay as a narrow `RenderBackendDX12` helper until more pass execution
  moves out?
- Should framebuffer native identity be exposed through a renderer-neutral
  diagnostic snapshot, or should the DX12 backend downcast `IFramebuffer` only
  inside backend-owned graph building?
- Should acceleration-structure build barriers be graph-owned, or documented as
  lower-level DXR build internals that remain outside the frame graph?
- Should the first actual frame graph be built in `SkullbonezRunRender.cpp`, the
  backend, or a new render orchestration module that can see both pass resources
  and backend resource identities?

Preferred initial answers:

- Put graph emission in a small DX12-specific executor file so raw
  `ResourceBarrier()` calls have a single obvious home.
- Use renderer-neutral opaque resource snapshots where possible; avoid adding
  D3D12 types to `IFramebuffer`.
- Defer BLAS/TLAS internals until all frame image resources are graph-owned.
- Build the actual per-frame graph near `SkullbonezRunRender.cpp` first, because
  that code knows which optional passes are active.

## Final Acceptance Checklist

- [ ] `rg -n "ResourceBarrier\\(" SkullbonezSource` shows raw calls only in the
      graph executor and documented D3D12 object-build internals.
- [ ] `rg -n "TransitionBarrier\\(" SkullbonezSource` shows no general-purpose
      production helper usage outside graph execution.
- [ ] `FramebufferDX12::Bind()` and `FramebufferDX12::Unbind()` do not emit raw
      DX12 barriers.
- [ ] `Clear()`, `PrepareDraw()`, `Present()`, and `CaptureBackbuffer()` do not
      hand-roll backbuffer transitions.
- [ ] `DispatchReflectionRays()` uses graph-owned SRV/UAV transitions and UAV
      ordering.
- [ ] `GenerateMipsGPU()` uses graph-owned subresource transitions and UAV
      ordering.
- [ ] The graph dump is based on the actual executed frame path.
- [ ] Graph telemetry names pass, resource, subresource/all-subresources, before
      access, after access, and source.
- [ ] `dx12_validation.txt` reports zero errors.
- [ ] DX12 screenshot baseline comparison passes.
- [ ] Final `tools\validate_full.bat` passes for the completed PR-bound branch.

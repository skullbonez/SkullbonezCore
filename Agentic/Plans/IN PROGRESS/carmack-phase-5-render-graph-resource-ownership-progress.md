# Carmack Phase 5 Render Graph Resource Ownership Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: Phase 5 - Render Graph Resource Ownership

## Current Status

- Status: Not implemented in this progress file; this document makes the Phase 5 work actionable.
- Current source lookup found graph compiler support for transient lifetimes and compiler-side alias diagnostics in `SkullbonezSource/Rendering/RenderGraph.cpp`.
- Current source lookup found DX12 backend materialization scaffolding in `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`, including `MaterializeGraphTransientResources`, `ReleaseGraphTransientResources`, `GraphTransientResourceDX12`, and `GraphTransientMaterializationStatsDX12`.
- Current source lookup found the only obvious graph transient production-like resource is still diagnostic skeleton output: `GraphTransientProbeColor` in `RenderBackendDX12::DumpFrameGraphSkeleton`.
- Current live runtime graph paths in `SkullbonezSource/Runtime/RunRender.cpp` still import production resources with `AddExternalResource`; `VolumetricLight` in `RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph` is a likely first live transient candidate because `VolumetricLightPass` writes it and `ToneMapPass` samples it in the same graph.

## Action Checklist

- [ ] Change `RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph` in `SkullbonezSource/Runtime/RunRender.cpp` so `VolumetricLight` is declared with `RenderGraph::AddTransientResource` instead of `RenderGraph::AddExternalResource` when `m_volumetricPass.CanRender(frame)` is true.
- [ ] Change or extend the pass callback data in `SkullbonezSource/Runtime/RunRender.cpp` so `ExecuteVolumetricGraphCallback` and `ExecuteTonemapGraphCallback` can bind and sample the graph-owned `VolumetricLight` target, not only `m_systems.renderPasses.volumetricLight.target`.
- [ ] Change `VolumetricPass::Render` in `SkullbonezSource/Runtime/RunPasses.cpp` to write the graph-owned transient when provided, while preserving the legacy `VolumetricLightPassResources::target` path as fallback.
- [ ] Change `TonemapPass::Render` in `SkullbonezSource/Runtime/RunPasses.cpp` to sample the graph-owned volumetric transient SRV when `volumetricReady` is true, while preserving the current legacy fallback to `scene.hdrTarget->GetColorTextureHandle()`.
- [ ] Change the render graph/backend binding surface in `SkullbonezSource/Rendering/RenderGraph.h`, `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`, and `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` so a callback-owned pass can resolve a graph transient allocation to the needed RTV/SRV/DSV/UAV handles without exposing broad DX12 ownership to runtime pass code.
- [ ] Fix backend same-compile transient aliasing in `RenderBackendDX12::MaterializeGraphTransientResources` in `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`; specifically prove or replace the `!candidate.usedThisCompile` reuse gate so compatible non-overlapping allocations with the same compiler `poolSlot` reuse one backend resource in the same compile.
- [ ] Add or extend architecture-unit coverage in `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp` for backend/materializer pool-slot reuse, not just compiler-side `RenderGraph::Compile()` reuse.
- [ ] Record runtime evidence from `Debug/dx12_frame_graph_skeleton.txt` and DX12 diagnostics showing a production frame path writes and/or samples a graph-owned transient that is not only `DeclarationOnly` skeleton metadata.
- [ ] Regenerate any intended visual baselines only if the live graph-owned transient changes pixels; otherwise record unchanged DX12 screenshot validation evidence.
- [ ] Remove or clearly downgrade the diagnostic-only `GraphTransientProbeColor` proof in `RenderBackendDX12::DumpFrameGraphSkeleton` only after the production transient path and evidence are in place.
- [ ] Record all final evidence paths back in `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md` during the implementation handoff, not in this progress file alone.

## Likely Files And Tools To Inspect

- `SkullbonezSource/Rendering/RenderGraph.h`
- `SkullbonezSource/Rendering/RenderGraph.cpp`
- `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h`
- `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `tools\validate_dx12_arch_tests.bat`
- `tools\validate_dx12_renderer.bat`
- `tools\check_runtime_boundaries.py`
- `tools\validate_project_filters.py`

## Dependencies

- Keep legacy backend-owned targets working until the graph-owned path passes DX12 screenshot validation.
- Do not remove shutdown/lifetime hooks such as `RenderBackendDX12::ReleaseGraphTransientResources` until replacement lifetime accounting is proven.
- Preserve callback-owned render graph execution in `RuntimeRenderer` while adding resource ownership; do not move broad runtime state into `RenderGraph`.
- Any source-bearing implementation slice must apply `Agentic/Reference/comment-style-guide.md` and run `Agentic/Skills/comment-style-audit/skill.md` on touched source files.

## Evidence To Collect

- `tools\validate_dx12_arch_tests.bat` output proving transient compiler/materializer alias coverage.
- `tools\validate_dx12_renderer.bat` output proving DX12 validation errors 0 and screenshots matching or intentionally updated baselines.
- `Debug/dx12_frame_graph_skeleton.txt` or a newer graph dump showing the production transient name, writes, reads, pool slot, descriptor count, and reuse status.
- DX12 log events for graph transient creation/reuse/release, including `dx12_graph_transient_release`.
- `git diff --check` output before final handoff.
- Comment-style audit output for every touched `.cpp`, `.h`, `.hpp`, `.inl`, or `.hlsl` file.

## Validation Note

This progress-file creation is documentation-only; no repository validation scripts are required now. The implementation slice for Phase 5 will be DX12/render-graph work and should run `tools\validate_dx12_arch_tests.bat` for focused graph coverage and `tools\validate_dx12_renderer.bat` for the required DX12 screenshot/InfoQueue gate before PR-bound handoff. Run `tools\validate_full.bat` only at the final Carmack branch handoff gate.

## Open Risks And Questions

- Is `VolumetricLight` the right first production transient, or should the first slice target a smaller target such as a non-cinematic post resource with lower screenshot churn?
- Should graph transient lookup be exposed through `RenderGraphPassContext`, a narrow render-resource interface, or a DX12-only callback helper?
- Does the backend materializer need to key reuse strictly by compiler `poolSlot` instead of descriptor equality plus `usedThisCompile`?
- How should descriptor lifetime diagnostics distinguish static graph-owned SRV/UAV rows from existing content SRVs in `ReportArchitectureStats`?
- What runtime evidence should replace the current diagnostic `GraphTransientProbeColor` skeleton proof once a production transient exists?

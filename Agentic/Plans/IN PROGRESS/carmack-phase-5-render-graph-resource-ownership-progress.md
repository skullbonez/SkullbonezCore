# Carmack Phase 5 Render Graph Resource Ownership Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: Phase 5 - Render Graph Resource Ownership

## Current Status

- Status: Implemented, validated, and rubber-duck reviewed on `nightrunner-29th-june`; pending commit.
- Current source lookup found graph compiler support for transient lifetimes and compiler-side alias diagnostics in `SkullbonezSource/Rendering/RenderGraph.cpp`.
- DX12 backend graph transient records now live in `SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h`, with backend materialization and release implemented in `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`.
- `RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph` now declares `VolumetricLight` as a graph transient when the volumetric pass is active, materializes it through the render command context, and passes the graph-owned binding to `VolumetricPass` and `TonemapPass`.
- The old `GraphTransientProbeColor` skeleton diagnostic remains as a cheap backend smoke probe, but it is no longer the Phase 5 proof. The production evidence is `Debug/dx12_cinematic_post_graph.txt` and `Debug/dx12_frame_graph_actual.txt`.

## Action Checklist

- [x] Change `RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph` in `SkullbonezSource/Runtime/RunRender.cpp` so `VolumetricLight` is declared with `RenderGraph::AddTransientResource` instead of `RenderGraph::AddExternalResource` when `m_volumetricPass.CanRender(frame)` is true.
- [x] Change or extend the pass callback data in `SkullbonezSource/Runtime/RunRender.cpp` so `ExecuteVolumetricGraphCallback` and `ExecuteTonemapGraphCallback` can bind and sample the graph-owned `VolumetricLight` target, not only `m_systems.renderPasses.volumetricLight.target`.
- [x] Change `VolumetricPass::Render` in `SkullbonezSource/Runtime/RunPasses.cpp` to write the graph-owned transient when provided, while preserving the legacy `VolumetricLightPassResources::target` path as fallback.
- [x] Change `TonemapPass::Render` in `SkullbonezSource/Runtime/RunPasses.cpp` to sample the graph-owned volumetric transient SRV when `volumetricReady` is true, while preserving the current legacy fallback to `scene.hdrTarget->GetColorTextureHandle()`.
- [x] Change the render graph/backend binding surface in `SkullbonezSource/Rendering/RenderGraph.h`, `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`, and `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` so a callback-owned pass can resolve a graph transient allocation to the needed RTV/SRV/DSV/UAV handles without exposing broad DX12 ownership to runtime pass code.
- [x] Fix backend same-compile transient aliasing in `RenderBackendDX12::MaterializeGraphTransientResources` in `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`; specifically prove or replace the `!candidate.usedThisCompile` reuse gate so compatible non-overlapping allocations with the same compiler `poolSlot` reuse one backend resource in the same compile.
- [x] Add or extend architecture-unit coverage in `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp` for backend/materializer pool-slot reuse, not just compiler-side `RenderGraph::Compile()` reuse.
- [x] Record runtime evidence from `Debug/dx12_frame_graph_actual.txt` and `Debug/dx12_cinematic_post_graph.txt` showing a production frame path writes and samples a graph-owned transient that is not only `DeclarationOnly` skeleton metadata.
- [x] Regenerate any intended visual baselines only if the live graph-owned transient changes pixels; otherwise record unchanged DX12 screenshot validation evidence.
- [x] Remove or clearly downgrade the diagnostic-only `GraphTransientProbeColor` proof in `RenderBackendDX12::DumpFrameGraphSkeleton` only after the production transient path and evidence are in place.
- [x] Record all final evidence paths back in `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md` during the implementation handoff, not in this progress file alone.

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

- `git diff --check`: passed.
- `tools\validate_format.bat`: passed, all source files correctly formatted.
- `tools\validate_dx12_arch_tests.bat`: passed, including `DX12 graph transient pool-slot reuse allows same-compile alias`.
- `tools\validate_dx12_renderer.bat`: passed. Latest manifest:
  `TestOutput\validation\dx12_renderer\20260629T151157Z\manifest.json`; summary:
  `TestOutput\validation\dx12_renderer\20260629T151157Z\summary.json`; InfoQueue:
  `TestOutput\validation\dx12_renderer\20260629T151157Z\dx12_validation.txt` reports 0.
- Focused launch:
  `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --scene SkullbonezData/scenes/water_ball_test.scene.json --cinematic on --frames 2`.
  Fresh graph dumps were written at 2026-06-30 01:14 local time:
  `Debug\dx12_cinematic_post_graph.txt` and `Debug\dx12_frame_graph_actual.txt`.
- `Debug\dx12_cinematic_post_graph.txt` shows `VolumetricLight external=false`, `transient kind=Texture2D format=RGBA16F size=892x480 descriptors=RTV|SRV`, a VolumetricLightPass write, a ToneMapPass read, and transitions `PixelShaderResource -> RenderTarget -> PixelShaderResource`.
- `Debug\dx12_frame_graph_actual.txt` shows `cinematic_render=true`, `volumetric_callback_owned=true`, `volumetric_ready=true`, `tonemap_callback_owned=true`, `volumetric_texture_handle=22`, and `TransientAllocations: resource=VolumetricLight slot=0 first_pass=9 last_pass=10 descriptors=2 reused=false released_at_frame_end=true`.
- Root `dx12_validation.txt` after the focused launch reports 0.
- Touched-file comment audit: 12 source-bearing files inspected against `Agentic/Reference/comment-style-guide.md`; 0 deferred.
- Rubber-duck review: Linnaeus reported no blocking resource-state, lifetime, fallback, aliasing, comment-standard, or validation issues. Residual note accepted: `released_at_frame_end` is a graph-lifetime diagnostic, while the DX12 physical transient pool is retained and released through `RenderBackendDX12::ReleaseGraphTransientResources`.

## Validation Note

This implementation slice touched DX12/render-graph code, so the focused graph coverage and DX12 renderer gate were required and passed. The final Phase 6 Carmack handoff still owns the broad `tools\validate_full.bat` gate.

## Residual Risks And Notes

- `VolumetricLight` was a good first production transient because one graph pass writes it and the next graph pass samples it inside the cinematic post graph.
- The runtime sees only `RenderGraphTextureBinding` through `IRenderCommandContext`; DX12 pool records stay backend-owned.
- `GraphTransientProbeColor` is intentionally retained as a cheap skeleton/materializer smoke probe. It should not be cited as the production proof for Phase 5.
- `released_at_frame_end` describes the graph planner's logical lifetime and descriptor accounting for the compiled frame graph. It does not mean the DX12 physical pool destroys and recreates the texture every frame; pooled resources remain available for reuse until backend shutdown/release.
- The focused launch wrapper returns before the GUI process flushes graph dumps; wait for the generated files before reading them.

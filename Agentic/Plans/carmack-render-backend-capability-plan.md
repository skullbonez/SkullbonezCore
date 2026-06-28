# Carmack Render Backend Capability Plan

Date: 2026-06-28
Status: In progress
Impact area: DX12 renderer, render interfaces, runtime render host, tools, tests
Validation note: plan-only edits require no validation. PR-bound renderer
interface or DX12 backend changes require `tools\validate_dx12_renderer.bat`.
If hot-path command submission, descriptor upload, dynamic geometry, or telemetry
storage changes, also run `tools\validate_perf.bat`.

## Completed Slices

- [x] 2026-06-28: Split the capture/readback capability into
  `IRenderCaptureBackend.h`, added `GfxCapture()` as the narrow borrowed
  capability accessor, gave the capture interface a capture-only
  `SupportsBackbufferCapture()` query, and moved screenshot/backdrop readback
  call sites away from `Gfx().CaptureBackbuffer` and broad
  `RenderCapabilities` access.
  Validation:
  `tools\validate_fast.bat` passed in 116.80s
  (`TestOutput\validation\agent_logs\render_capture_capability_validate_fast.log`);
  `tools\validate_project_filters.bat` passed in 0.86s
  (`TestOutput\validation\agent_logs\render_capture_capability_project_filters.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.23s with 0 DX12 validation
  errors and matching DX12 screenshots
  (`TestOutput\validation\agent_logs\render_capture_capability_validate_dx12_renderer.log`).
  Rubber-duck review: Lagrange found a blocking broad `RenderCapabilities`
  leak on the first pass; the follow-up review confirmed the blocker resolved
  and found no new blockers.
- [x] 2026-06-28: Formalized the wide render backend surface into
  `IRenderDeviceLifecycle`, `IRenderResourceFactory`, `IRenderCommandContext`,
  and `IRenderDiagnostics`, while keeping `IRenderBackend` as a temporary
  inherited aggregate for existing `Gfx()` call sites. `IRenderRayTracing`
  remains separate and is not inherited by the facade. New interface headers
  include ownership/lifetime learning headers, and
  `tools\validate_project_filters.py` now recognizes the new render interface
  files. Rubber-duck review found no code blockers; non-blocking notes about
  broad `RenderCapabilities` metadata and `GfxRayTracing()` sharing the
  compatibility header remain tracked for later call-site/header migration.
  Validation:
  `tools\validate_project_filters.bat` passed in 0.86s
  (`TestOutput\validation\agent_logs\render_capability_interfaces_project_filters.log`);
  `tools\validate_fast.bat` passed in 124.41s
  (`TestOutput\validation\agent_logs\render_capability_interfaces_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.92s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_capability_interfaces_validate_dx12_renderer.log`).
- [x] 2026-06-28: Strengthened `tools\check_runtime_boundaries.py` so
  `IRenderBackend` must remain a methodless temporary aggregate of named render
  capabilities. The guardrail rejects direct methods on `IRenderBackend`, blocks
  `IRenderBackend` from inheriting `IRenderRayTracing`, and keeps the existing
  DXR method-declaration and direct `Gfx().<raytracing>` checks in force.
  Synthetic checker tests cover allowed aggregate inheritance plus rejected
  direct methods and rejected raytracing inheritance. Rubber-duck review found
  no blockers; the reviewer noted the direct-method regex is intentionally a
  common-declaration ratchet, not a full C++ parser for exotic forms. Validation
  evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors in
  4.85s
  (`TestOutput\validation\agent_logs\render_backend_aggregate_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 18.03s
  (`TestOutput\validation\agent_logs\render_backend_aggregate_guardrail_validate_fast.log`).
- [x] 2026-06-28: Extended `RuntimeRenderServices` with the borrowed render
  command capability plus a captured ready flag. `RuntimeRenderer::RenderFrame()`
  now uses the borrowed command context for frame clears and the borrowed ready
  flag for the shadow-map decision, reducing `RunRender.cpp` direct `Gfx()`
  calls from 2 to 1. The global-service checker allowlist was lowered to match
  the new count.
  Comment-style audit: inspected `RuntimeRenderInputs.h`, `RunRender.cpp`, and
  `tools\check_runtime_boundaries.py`; the touched source retains learning
  headers, and the new render-service fields include a `Lifetime:` note for the
  borrowed backend command facet.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.69s
  (`TestOutput\validation\agent_logs\runtime_render_services_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 23.60s
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.81s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.30s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\runtime_render_services_validate_full.log`).
  Rubber-duck review: McClintock found no code blockers after the context was
  narrowed to the command capability. The reviewer flagged stale DX12/full
  evidence from before that narrowing, plus two non-blocking clarity issues; the
  validation was rerun, an `Invariant:` comment was added near the readiness
  guard, and the global plan's broad borrowed-context checkbox was left open.
- [x] 2026-06-28: Carried the borrowed render command capability from
  `RuntimeRenderServices` into `RenderFrameContext` and migrated the
  `RunPasses.cpp` texture-slot hygiene helpers (`ClearRenderTextureSlotsExcept`,
  `ClearAllRenderTextureSlots`, and `BindRenderTextureSlots`) off direct
  `Gfx()` access. `RunPasses.cpp` direct `Gfx()` debt fell from 98 to 96; the
  broad render-pass backend migration remains open because viewport, clear,
  depth/blend/cull, dynamic geometry, and resource creation calls still use the
  compatibility facade.
  Comment-style audit: inspected `RuntimeRenderPasses.h`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the new
  `RenderFrameContext` command pointer has a `Lifetime:` note, and
  `RunPasses.cpp` asserts the borrowed pointer is bound before helper use.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\render_frame_command_context_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.35s
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.01s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.25s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_frame_command_context_validate_full.log`).
  Rubber-duck review: Hubble found no blockers. Non-blocking notes: the command
  pointer remains nullable/assert-only for future direct default construction,
  the slice should be recorded as texture-slot helper progress only, and the
  `Gfx()` guardrail is still a counted ratchet rather than a semantic per-call
  classifier.
- [x] 2026-06-28: Migrated generated cinematic sky depth/blend state in
  `SkyPass::RenderCinematicSky()` from direct `Gfx()` calls to the borrowed
  `IRenderCommandContext` already carried by `RenderFrameContext`. This removes
  eight more `RunPasses.cpp` `Gfx()` calls and lowers the file allowlist from
  96 to 88; viewport, clear, resource creation, dynamic geometry, and many
  other pass calls still use the compatibility facade and remain open debt.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the touched sky pass state block keeps
  the existing pass-contract comments and no new ownership is introduced.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.81s
  (`TestOutput\validation\agent_logs\sky_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.45s
  (`TestOutput\validation\agent_logs\sky_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.68s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\sky_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.45s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\sky_command_state_validate_full.log`).
  Rubber-duck review: Hilbert found no blockers. Non-blocking note: the sky
  pass still restores depth write from the depth-test snapshot, which matches
  the pre-existing behavior but remains state-contract debt for a later command
  state cleanup.
- [x] 2026-06-28: Migrated `WaterPass::Render()` depth/blend state save,
  mutation, and restore from direct `Gfx()` calls to the borrowed
  `IRenderCommandContext` already carried by `RenderFrameContext`. This removes
  twelve more `RunPasses.cpp` `Gfx()` calls and lowers the file allowlist from
  88 to 76; shadow, reflection, tornado, volumetric, tonemap, viewport, clear,
  resource creation, and DXR paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the water pass keeps the existing
  pass-contract comments and introduces no new ownership.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.84s
  (`TestOutput\validation\agent_logs\water_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.55s
  (`TestOutput\validation\agent_logs\water_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.55s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\water_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.22s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\water_command_state_validate_full.log`).
  Rubber-duck review: Dewey found no blockers. Non-blocking note: the
  `Gfx()` guardrail remains a ceiling ratchet, not semantic same-file
  classification, so future slices still need focused review.
- [x] 2026-06-28: Migrated `TornadoVisualPass::Render()` depth/blend/cull state
  save, mutation, restore, and transient colored-triangle draw from direct
  `Gfx()` calls to the borrowed `IRenderCommandContext` already carried by
  `RenderFrameContext`. This removes sixteen more `RunPasses.cpp` `Gfx()` calls
  and lowers the file allowlist from 76 to 60; shadow, reflection, volumetric,
  tonemap, viewport, clear, resource creation, and DXR paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and this slice introduces no new ownership or local
  rendering vocabulary.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.72s
  (`TestOutput\validation\agent_logs\tornado_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.34s
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.58s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.89s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tornado_command_state_validate_full.log`).
  Rubber-duck review: Hume found no blockers. Non-blocking note: the DX12
  validation suite is broad renderer coverage rather than tornado-scene-specific
  visual proof, so this slice is recorded as a command-context/global-access
  ratchet with full renderer safety-net evidence.
- [x] 2026-06-28: Migrated `ShadowPass::RenderShadowMap()` viewport/clear,
  depth/blend/cull state, polygon offset, and final viewport restore from
  direct `Gfx()` calls to the `IRenderCommandContext` already passed into the
  helper. This removes sixteen more `RunPasses.cpp` `Gfx()` calls and lowers
  the file allowlist from 60 to 44; shadow target creation, reflection,
  volumetric, tonemap, resource creation, and DXR paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and this slice preserves the existing shadow-map state
  restore contract instead of folding behavior cleanup into the migration.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.78s
  (`TestOutput\validation\agent_logs\shadow_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.36s
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.02s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.80s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\shadow_command_state_validate_full.log`).
  Rubber-duck review: Huygens found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy shadow state restore semantics rather than
  fixing depth-write/cull restore debt, the viewport restore relies on window
  and backend dimensions staying in lockstep, and the DX12 validation suite is
  broad renderer coverage rather than a shadow-scene-specific visual proof.
- [x] 2026-06-28: Migrated `VolumetricPass::Render()` viewport and
  depth/blend state save, mutation, restore, and final viewport restore from
  direct `Gfx()` calls to the borrowed `IRenderCommandContext` already carried
  by `RenderFrameContext`. This removes twelve more `RunPasses.cpp` `Gfx()`
  calls and lowers the file allowlist from 44 to 32; the fullscreen quad
  helper, resource creation, reflection, tonemap, and DXR paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and this slice preserves the existing screen-space pass
  state restore contract instead of folding behavior cleanup into the migration.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.89s
  (`TestOutput\validation\agent_logs\volumetric_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.08s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.87s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\volumetric_command_state_validate_full.log`).
  Rubber-duck review: Meitner found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  volumetric-scene-specific visual proof.
- [x] 2026-06-28: Migrated `TonemapPass::Render()` viewport and depth/blend
  state save, mutation, and restore from direct `Gfx()` calls to the borrowed
  `IRenderCommandContext` already carried by `RenderFrameContext`. This removes
  eleven more `RunPasses.cpp` `Gfx()` calls and lowers the file allowlist from
  32 to 21; the fullscreen quad helper, resource creation, reflection, and DXR
  paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and this slice preserves the existing screen-space pass
  state restore contract instead of folding behavior cleanup into the migration.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.00s
  (`TestOutput\validation\agent_logs\tonemap_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.77s
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.39s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.77s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\tonemap_command_state_validate_full.log`).
  Rubber-duck review: Gibbs found no blockers. Non-blocking notes: this slice
  intentionally preserves legacy depth-write restore semantics, the viewport
  restore relies on window and backend dimensions staying in lockstep, and the
  DX12 validation suite is broad renderer coverage rather than
  tonemap-scene-specific visual proof.
- [x] 2026-06-28: Migrated `SceneTargetPass::Begin()` HDR target viewport and
  clear calls from direct `Gfx()` access to the borrowed
  `IRenderCommandContext` already carried by `RenderFrameContext`. This removes
  two more `RunPasses.cpp` `Gfx()` calls and lowers the file allowlist from 21
  to 19; the fullscreen quad helper, resource creation, reflection, and DXR
  paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and the existing scene-target invariant already explains
  why world rendering switches to the HDR target before post effects.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.98s
  (`TestOutput\validation\agent_logs\scene_target_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.71s
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.44s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.01s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\scene_target_command_state_validate_full.log`).
  Rubber-duck review: Raman found no blockers. Non-blocking note: the `Gfx()`
  guardrail remains a count ratchet rather than semantic per-call tracking, so
  future slices still need focused diff review.
- [x] 2026-06-28: Migrated the shared `DrawFullscreenQuad()` helper from direct
  `Gfx().UploadAndDrawDynamicVB()` access to an explicit borrowed
  `IRenderCommandContext&`, then passed the existing command context from the
  cinematic sky, volumetric-light, and tonemap call sites. This removes one
  more `RunPasses.cpp` `Gfx()` call and lowers the file allowlist from 19 to
  18; fullscreen quad resource creation/destruction, framebuffer creation,
  reflection, and DXR paths remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and the existing fullscreen vertex-contract comment still
  explains the shared helper behavior.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.85s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.99s
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.26s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 29.49s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_full.log`);
  `tools\validate_perf.bat` completed in 22.48s with exit 0
  (`TestOutput\validation\agent_logs\fullscreen_quad_command_draw_validate_perf.log`).
  Perf review note: the DX12 comparison was skipped for machine mismatch, and
  the script reported physics-bench warnings outside this render-helper slice.
  Rubber-duck review: Bohr found no blockers. Non-blocking notes: perf evidence
  is advisory/noisy on this machine, and the `Gfx()` guardrail remains
  count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Migrated the planar reflection pass viewport, clear,
  clip-plane enable/disable, and final viewport restore from direct `Gfx()`
  calls to the borrowed `IRenderCommandContext` already carried by
  `RenderFrameContext`. This removes five more `RunPasses.cpp` `Gfx()` calls
  and lowers the file allowlist from 18 to 13; reflection capability queries,
  DXR dispatch, framebuffer creation, size queries, and fullscreen quad
  resource lifetime remain open.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; both touched source-bearing files retain
  learning headers, and the reflection block already carries the two-path
  concept, planar viewport invariant, and water-surface clip-plane reason.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 5.11s
  (`TestOutput\validation\agent_logs\reflection_command_state_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 24.90s
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_fast.log`);
  `tools\validate_dx12_renderer.bat` passed in 18.38s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.97s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\reflection_command_state_validate_full.log`).
  Rubber-duck review: Nietzsche found no blockers and verified no stale
  allowlist mismatch: actual total `Gfx()` count 208, allowlist total 208, and
  `RunPasses.cpp` actual/allowlist 13/13. Non-blocking note: the `Gfx()`
  guardrail remains count-based rather than semantic per-call tracking.
- [x] 2026-06-28: Routed remaining `RunPasses.cpp` framebuffer creation,
  fullscreen dynamic vertex-buffer creation/destruction, render-target size
  queries, and reflection capability queries through borrowed render services
  instead of direct `Gfx()` access. `RuntimeRenderInputs` now carries
  `IRenderResourceFactory` and `IRenderDiagnostics`; `RenderFrameContext`
  carries diagnostics and sampled window dimensions; pass resource release
  receives a nullable resource factory so failed backend init still resets
  CPU-side handles without calling `Gfx()`. This removes the final thirteen
  `RunPasses.cpp` direct `Gfx()` calls and lowers the global counted `Gfx()`
  allowlist from 208 to 196. `GfxRayTracing()` remains a narrow DXR capability
  accessor, and stricter phase-only resource-factory plumbing remains open.
  Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RuntimeRenderPasses.h`, `RuntimeRenderer.h`, `Run.cpp`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; all touched
  source-bearing files keep learning headers, and the new/changed comments name
  borrowed service lifetime, shutdown hazards, and frame-only diagnostics
  contracts.
  Validation:
  `python tools\check_runtime_boundaries.py` passed with 0 errors in 4.87s
  (`TestOutput\validation\agent_logs\render_resource_services_runtime_boundaries_rerun.log`);
  `tools\validate_fast.bat` passed in 104.57s
  (`TestOutput\validation\agent_logs\render_resource_services_validate_fast_final.log`);
  `tools\validate_dx12_renderer.bat` passed in 17.71s with 0 DX12 validation
  errors and matching screenshots
  (`TestOutput\validation\agent_logs\render_resource_services_validate_dx12_renderer.log`);
  `tools\validate_full.bat` passed in 28.13s, including DX12 and byte-exact
  physics baseline validation
  (`TestOutput\validation\agent_logs\render_resource_services_validate_full.log`).
  Rubber-duck review: an in-session rubber-duck pass found one blocking teardown
  hazard where `ReleaseBackendOwnedRenderResources()` could call `Gfx()`
  unconditionally after failed backend init; the release path now takes a
  nullable resource factory and resets CPU-side handles even when no backend is
  live. Residual non-blocking notes: resource-factory access is still carried by
  the frame context rather than a dedicated create/rebuild context, and the
  `Gfx()` guardrail remains count-based.

## Problem Statement

The Carmack-test verdict flagged `IRenderBackend` and global `Gfx()` as a wide
compatibility contract. DX12 is the only runtime renderer, but callers still see
one interface that mixes lifecycle, resources, state, capture, profiler markers,
debug lines, dynamic buffers, instancing, and other capability-specific work.

## Goal

Split render callers toward narrow capability interfaces so each system receives
only the render service it needs. Keep `IRenderBackend` only as a temporary DX12
compatibility facade while call sites migrate.

## Success Bar

- New render code depends on named capability interfaces or context structs, not
  on the full `IRenderBackend`.
- Existing passes receive narrow services through `RuntimeRenderHost` or render
  contexts.
- `IRenderBackend.h` shrinks over time and cannot grow casually.
- DX12 validation remains zero-error with screenshots matching committed
  baselines.

## Related Plans

- `Agentic/Plans/render-graph-irender-interface-plan.md` is the active renderer
  umbrella plan. Use this Carmack plan as the capability-interface acceptance
  checklist for the backend split portion of that work.
- `Agentic/Plans/carmack-render-graph-resource-ownership-plan.md` covers graph
  execution and transient resource lifetime. If one implementation slice touches
  both plans, use the union of their validation and review checklists.
- `Agentic/Plans/carmack-global-service-lifetime-plan.md` owns the broader
  process-global cleanup. This plan owns render capability access, including
  shrinking direct `Gfx()` use in renderer-facing paths.

## Implementation Checklist

### Inventory

- [ ] List every method in `SkullbonezSource/Rendering/IRenderBackend.h`.
- [ ] Categorize methods into lifecycle, frame state, resource creation, texture binding, capture/readback, draw tracing, GPU profiling, platform markers, dynamic geometry, debug lines, instancing, and compatibility.
- [ ] Run `rg "Gfx\\(|IRenderBackend|SetGfxBackend|DestroyGfxBackend|IsGfxReady" SkullbonezSource`.
- [ ] Map each call site to its required capability.
- [ ] Mark any call site that currently asks for the wide backend only because no narrow service exists yet.

### Capability Interfaces

- [x] Keep `IRenderCaptureBackend` as the capture/readback surface and move capture-only callers to it.
- [x] Add or formalize `IRenderDeviceLifecycle` for init, shutdown, resize, present, finish, flush, and vsync.
- [x] Add or formalize `IRenderResourceFactory` for shader, mesh, framebuffer, texture, instanced mesh, and dynamic buffer creation.
- [x] Add or formalize `IRenderCommandContext` for frame draw state, clears, viewport, blend, depth, cull, texture binding, and draw calls.
- [x] Add or formalize `IRenderDiagnostics` for draw-call trace, GPU timers, platform markers, and backend validation metadata.
- [x] Keep `IRenderRayTracing` separate; do not move DXR methods back onto the wide backend.
- [x] Document ownership and lifetime for each capability interface in its header.

### Call-Site Migration

- [ ] Route runtime render passes through `RuntimeRenderHost` capability groups rather than direct wide-backend access.
- [x] Route runtime render-pass texture-slot hygiene helpers through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 98 to 96.
- [x] Route generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 96 to 88.
- [x] Route water depth/blend state through `RenderFrameContext`'s borrowed
  `IRenderCommandContext` and lower the `RunPasses.cpp` direct `Gfx()`
  allowlist from 88 to 76.
- [x] Route tornado visual depth/blend/cull state and transient triangle draw
  through `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 76 to 60.
- [x] Route shadow-map viewport/clear/depth/blend/cull/polygon-offset state
  through the existing `IRenderCommandContext` argument and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 60 to 44.
- [x] Route volumetric pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 44 to 32.
- [x] Route tonemap pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 32 to 21.
- [x] Route HDR scene-target viewport/clear through `RenderFrameContext`'s
  borrowed `IRenderCommandContext` and lower the `RunPasses.cpp` direct
  `Gfx()` allowlist from 21 to 19.
- [x] Route the shared fullscreen quad dynamic draw helper through a borrowed
  `IRenderCommandContext` and lower the `RunPasses.cpp` direct `Gfx()`
  allowlist from 19 to 18.
- [x] Route planar reflection viewport/clear/clip-plane state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` and lower the
  `RunPasses.cpp` direct `Gfx()` allowlist from 18 to 13.
- [x] Route `RunPasses.cpp` framebuffer creation, fullscreen dynamic
  vertex-buffer lifetime, render-target size queries, and reflection
  capability queries through borrowed `IRenderResourceFactory`,
  `IRenderDiagnostics`, and sampled window dimensions; remove the
  `RunPasses.cpp` direct `Gfx()` allowlist by lowering it from 13 to 0.
- [ ] Pass capture/readback capability only to screenshot and validation paths.
- [ ] Pass dynamic geometry capability only to UI text, debug overlays, and transient draw helpers.
- [ ] Pass GPU profiler capability only to profiler marker code.
- [ ] Pass resource factory capability only during resource creation/rebuild phases.
- [ ] Keep command submission and draw state capability out of scene parsing, physics, asset registration, and diagnostics formatting.
- [ ] Remove direct wide-backend includes from call sites after migration.

### DX12 Adapter Shape

- [x] Have `RenderBackendDX12` implement the narrow capability interfaces through the temporary `IRenderBackend` aggregate; direct base-list or adapter migration is still pending.
- [ ] Keep adapter methods thin enough that DX12 state ownership remains in the backend implementation.
- [ ] Do not introduce new per-frame heap allocation through adapters.
- [ ] Preserve backend-owned resource release order during shutdown, resize, and rebuild.
- [ ] Keep InfoQueue validation and DRED/debug-layer setup inside DX12-owned code.

### Compatibility Facade Retirement

- [x] Leave `IRenderBackend` forwarding or aggregating capabilities only while callers migrate.
- [x] Add a plan-local table of remaining `IRenderBackend` methods after each slice.
- [ ] Delete facade methods when no direct caller needs them.
- [ ] Update `RenderCapabilities` if capability discovery moves to narrower services.
- [ ] Remove stale comments that imply GL/DX11 parity or multi-backend runtime selection.

Remaining `IRenderBackend` surface after the 2026-06-28 capability-interface slice:

| Capability | Public callable surface still reachable through `IRenderBackend&` | Notes |
|------------|-----------------|-------|
| Own methods | 0 | The facade declares no render methods of its own. |
| `IRenderDeviceLifecycle` | 10 | Init/shutdown/resize/present/finish/flush/vsync/size still reachable for compatibility. |
| `IRenderResourceFactory` | 9 | Resource creation/destruction remains inherited until creation paths receive the narrower factory. |
| `IRenderCommandContext` | 22 | Draw state, texture binding, dynamic geometry upload/draw, debug lines, transient triangles, and instanced draw calls remain inherited until render passes migrate. |
| `IRenderDiagnostics` | 16 | Draw trace, GPU timers, platform markers, renderer name, and broad `RenderCapabilities` metadata remain inherited. |
| `IRenderCaptureBackend` | 2 | Capture remains separately callable through `GfxCapture()`; inherited access remains only because `IRenderBackend` is the temporary aggregate. |
| `IRenderRayTracing` | 0 | DXR methods are still reachable only through `GfxRayTracing()` / `IRenderRayTracing`, not through `IRenderBackend`. |

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to reject new DXR methods on `IRenderBackend`.
- [x] Add a ratchet for `IRenderBackend.h` method count or public declaration count.
- [ ] Add a guardrail that blocks new direct `Gfx()` calls outside approved bootstrap and compatibility files.
- [x] Add synthetic checker tests for allowed narrow capability use and rejected wide-backend use.
- [x] Teach project-filter validation about any new render interface files.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [x] For capability header or DX12 backend changes: run `tools\validate_dx12_renderer.bat`.
- [x] For runtime render orchestration changes: run `tools\validate_full.bat` if multiple areas are touched.
- [ ] For upload, dynamic geometry, telemetry, or per-frame adapter changes: run `tools\validate_perf.bat`.
- [x] Verify `dx12_validation.txt` reports zero DX12 validation errors.
- [x] Verify DX12 screenshots match committed baselines unless a visual change is intentional and reviewed.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to inspect whether new interfaces are genuinely narrower or just the old backend split into names.
- [x] Ask the reviewer to look for new hidden global access or new `Gfx()` use.
- [x] Ask the reviewer to check DX12 resource lifetime and shutdown/resize behavior.
- [x] Record review findings in a report or this plan.
- [x] Resolve blocking review findings before committing PR-bound code.

## Definition Of Done

- [ ] Ordinary runtime render pass code no longer depends on the full `IRenderBackend`.
- [x] `IRenderBackend.h` is reduced to a temporary facade or deleted.
- [ ] Guardrails prevent re-widening the backend contract.
- [x] DX12 renderer validation passes with zero InfoQueue errors and matching screenshots.

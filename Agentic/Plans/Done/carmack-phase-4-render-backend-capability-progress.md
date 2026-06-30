# Carmack Phase 4 Render Backend Capability Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: Phase 4 - Render Backend Capability

## Current Status

- Status: closed as an evidence-only phase. No source edit is required.
- Impact area reviewed: DX12 renderer capability interfaces, runtime render pass
  boundaries, runtime-boundary tooling, and evidence docs.
- Validation for this progress-only edit: no repository validation is required,
  but the phase recorded focused guardrail evidence.
- CodeGraph is present and current:
  `TestOutput/validation/agent_logs/carmack_phase4_runtime_boundaries.log`
  follows a `codegraph status .` run that reported `[OK] Index is up to date`.
- `SkullbonezSource/Rendering/IRenderBackend.h` remains a temporary aggregate
  with no own render methods except the virtual destructor. It inherits
  lifecycle, resource factory, command, diagnostics, and capture capabilities,
  and does not inherit `IRenderRayTracing`.
- `SkullbonezSource/Rendering/IRenderRayTracing.h` remains the separate DXR
  capability and owns `InitDXR`, `DispatchReflectionRays`, `BuildTLAS`,
  reflection UAV texture access, shutdown, and static mesh GPU address queries.
- Runtime pass bodies remain clean: the targeted scan for `Gfx()`,
  `GfxRayTracing()`, and `IRenderBackend` in `RunPasses.cpp`,
  `RuntimeRenderPasses.h`, `RuntimeRenderInputs.h`, and `RunUiTextPass.cpp`
  found no matches.
- The regenerated Phase 0 classification has renderer-related compatibility
  rows, but no Phase 4 capability leak: `Run::Render()` is the accepted
  composition-root borrow path, pass bodies are clean, and remaining normal
  runtime renderer globals are transferred to Phase 2/5-era service lifetime and
  render-ownership work instead of this capability-interface phase.

## Checklist

- [x] Record the current `IRenderBackend` surface from
  `SkullbonezSource/Rendering/IRenderBackend.h`. Expected result: only the
  destructor is declared directly; lifecycle, resource factory, command,
  diagnostics, and capture behavior is inherited from named capability
  interfaces.
- [x] Remove any direct render operation that has reappeared in
  `SkullbonezSource/Rendering/IRenderBackend.h`. Change it into the matching
  named capability interface instead of widening the facade.
  Result: not applicable; no direct render operations reappeared on the facade.
- [x] Remove any `IRenderRayTracing` inheritance or DXR method declarations from
  `SkullbonezSource/Rendering/IRenderBackend.h`. Keep DXR methods in
  `SkullbonezSource/Rendering/IRenderRayTracing.h`.
  Result: not applicable; `IRenderBackend` does not inherit
  `IRenderRayTracing`, and DXR methods remain in `IRenderRayTracing.h`.
- [x] Record the current DXR borrow path from
  `SkullbonezSource/Runtime/RunRender.cpp`: `Run::Render()` may borrow
  `GfxRayTracing()` at the composition root and pass an optional
  `IRenderRayTracing*` through `RuntimeRenderServices` and `RenderFrameContext`.
- [x] Remove or change any direct `Gfx()` / `GfxRayTracing()` /
  `IRenderBackend` access found in runtime pass bodies. In
  `SkullbonezSource/Runtime/RunPasses.cpp`, use `RenderCommands(frame)`,
  `RenderResources(resources)`, `RenderDiagnostics(frame)`, or
  `frame.renderRayTracing` instead.
  Result: not applicable; pass-body scan found no direct backend access.
- [x] Regenerate a pass-boundary scan and record the output:
  `rg -n "Gfx\\(|GfxRayTracing\\(|IRenderBackend" SkullbonezSource\Runtime\RunPasses.cpp SkullbonezSource\Runtime\Render\RuntimeRenderPasses.h SkullbonezSource\Runtime\Render\RuntimeRenderInputs.h SkullbonezSource\Runtime\RunUiTextPass.cpp`.
  Expected result: no matches.
- [x] Reconfirm `tools/check_runtime_boundaries.py` still blocks direct
  `IRenderBackend` methods, `IRenderBackend` raytracing inheritance,
  raytracing declarations on `IRenderBackend`, direct `Gfx().BuildTLAS` style
  calls, and unclassified global renderer service access.
- [x] Regenerate runtime-boundary evidence when implementation scope allows:
  `python tools\check_runtime_boundaries.py --repo . --json-out TestOutput\validation\runtime_boundaries\carmack_phase4_runtime_boundaries.json`.
  Record both the console result and the JSON path.
- [x] Consume the Phase 0 regenerated global-service classification. Filter for
  `Gfx()`, `GfxRayTracing()`, `IRenderBackend`, render-pass classifications,
  and normal-runtime-path renderer hits. Resolve only render-backend capability
  issues in this phase; leave broader service lifetime work to Phase 2.
- [x] Record final Phase 4 evidence paths in this progress file, and only copy
  them into the authoritative plan when the task scope explicitly allows
  editing `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`.

## Likely Files And Tools

- `SkullbonezSource/Rendering/IRenderBackend.h`
- `SkullbonezSource/Rendering/IRenderRayTracing.h`
- `SkullbonezSource/Rendering/IRenderCommandContext.h`
- `SkullbonezSource/Rendering/IRenderResourceFactory.h`
- `SkullbonezSource/Rendering/IRenderDiagnostics.h`
- `SkullbonezSource/Rendering/IRenderDeviceLifecycle.h`
- `SkullbonezSource/Rendering/IRenderCaptureBackend.h`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- `tools/check_runtime_boundaries.py`
- `tools/validate_runtime_boundaries.bat`
- `tools/validate_project_filters.py`
- `codegraph status .`
- `codegraph explore "IRenderBackend IRenderRayTracing RuntimeRenderInputs RuntimeRenderPasses RunRender RunPasses check_runtime_boundaries"`
- `rg -n "Gfx\\(|GfxRayTracing\\(|IRenderBackend" SkullbonezSource tools`

## Dependencies

- Phase 0 should provide a regenerated
  `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`
  and summary, or a new dated replacement path.
- Phase 2 owns broad global-service lifetime cleanup. Phase 4 should only fix
  renderer capability leaks that touch `IRenderBackend`, `Gfx()`,
  `GfxRayTracing()`, runtime pass bodies, or render-backend guardrails.
- Phase 5 owns render graph resource ownership. If a Phase 4 fix starts moving
  graph transients, resource-state barriers, descriptor lifetime, or production
  graph execution, stop and hand that part to Phase 5.
- Any source-bearing edit must follow
  `Agentic/Reference/comment-style-guide.md` and run the comment-style audit
  over touched source before handoff.

## Evidence To Collect

- CodeGraph status: `codegraph status .` reported `[OK] Index is up to date`.
- CodeGraph exploration:
  `IRenderBackend IRenderRayTracing RuntimeRenderServices RenderFrameContext
  Run::Render RuntimeRenderInputs RuntimeRenderPasses RunPasses GfxRayTracing`
  confirmed the composition-root borrow path in `Run::Render()` and narrow
  runtime render service inputs.
- Interface inventory:
  `TestOutput/validation/agent_logs/carmack_phase4_render_backend_inventory.log`.
  `IRenderBackend` base interfaces are `IRenderDeviceLifecycle`,
  `IRenderResourceFactory`, `IRenderCommandContext`, `IRenderDiagnostics`, and
  `IRenderCaptureBackend`; own method surface is only `~IRenderBackend()`.
- DXR inventory:
  `IRenderRayTracing.h` owns `InitDXR`, `DispatchReflectionRays`, `BuildTLAS`,
  `GetReflectionUAVTexture`, `ShutdownDXR`,
  `GetInstancedMeshStaticVBVA`, and `GetInstancedMeshStaticStride`.
- Pass-body scan:
  `TestOutput/validation/agent_logs/carmack_phase4_pass_boundary_scan.log`
  reports `PHASE4_PASS_BOUNDARY_SCAN_RESULT=no matches`.
- Runtime-boundary checker:
  `TestOutput/validation/agent_logs/carmack_phase4_runtime_boundaries.log`
  reports `Runtime boundary summary:
  TestOutput\validation\runtime_boundaries\carmack_phase4_runtime_boundaries.json
  (0 errors)`, `PASS: Runtime boundary validation passed`, and
  `PHASE4_RUNTIME_BOUNDARIES_EXIT=0`; elapsed 4.9s.
- Renderer classification summary:
  `TestOutput/validation/agent_logs/carmack_phase4_renderer_classification_summary.log`.
  It found 191 renderer-related rows: 110 render pass, 27 normal runtime path,
  28 diagnostics, 23 test/tool, 2 asset lookup, and 1 OS callback bridge.
  Runtime pass-body backend hits were empty. The two `RunRender.cpp` hits are
  the accepted composition-root borrow:
  `IRenderBackend& renderBackend = Gfx();` and
  `IsGfxRayTracingReady() ? &GfxRayTracing() : nullptr;`.
- Classification disposition: direct render pass globals in rendering helpers,
  text, UI, sky, terrain, and tool/debug visualizers are accepted existing
  compatibility debt outside this narrow runtime-pass capability phase.
  Normal-runtime renderer globals in `Run.cpp`, `RunFrame.cpp`, `RunInput.cpp`,
  `Runtime/Scene/RunScene.cpp`, and `WorldEnvironment.cpp` are transferred to
  Phase 2 service-lifetime and Phase 5 render-ownership work.
- Source-change audit: not applicable; Phase 4 is docs/evidence only.

## Validation Note

This Phase 4 closure is documentation/evidence-only, so no repository validation
script is required for the commit. Focused evidence was still collected with
`tools/check_runtime_boundaries.py`, targeted `rg`, CodeGraph, and classification
summary scans. For future Phase 4 source work, do not run repository validation
while iterating unless a focused probe answers a specific question. At PR/commit
gate, use the narrowest matching validation:

- `python tools\check_runtime_boundaries.py --repo .` and
  `tools\validate_runtime_boundaries.bat` for boundary-only changes.
- `tools\validate_fast.bat` plus the changed script for edits under `tools/`.
- `tools\validate_dx12_renderer.bat` for render interface, runtime render pass,
  DX12 backend, shader, or screenshot-sensitive changes.
- `tools\validate_full.bat` if Phase 4 changes multiple runtime/render areas or
  the validation scope is uncertain.

## Open Risks And Questions

- `Run::Render()` still uses `Gfx()` and `GfxRayTracing()` as the composition
  root. That is accepted for this phase; do not move it without a broader
  service-lifetime design.
- `RuntimeRenderHost` remains a broad bridge. Do not grow it to hide a renderer
  capability leak; prefer existing frame/resource contexts or a named narrow
  capability.
- `tools/check_runtime_boundaries.py` uses regex guardrails and synthetic tests,
  not a full C++ parser. Exotic declarations should still get manual review.
- Files outside runtime pass bodies, such as `Rendering/Helper.cpp`,
  `Rendering/Text.cpp`, `UI/UI.cpp`, `World/Terrain.cpp`, and
  `Runtime/Editor/LauncherLaser.cpp`, still contain compatibility `Gfx()`
  access. They are not Phase 4 blockers because the facade is still narrow and
  the runtime pass-body boundary is clean.

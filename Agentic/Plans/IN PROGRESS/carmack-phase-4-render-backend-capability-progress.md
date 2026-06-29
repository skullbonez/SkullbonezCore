# Carmack Phase 4 Render Backend Capability Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: Phase 4 - Render Backend Capability

## Current Status

- Status: not closed. This progress file makes Phase 4 actionable; it does not
  change the authoritative plan or any source file.
- Impact area for future implementation: DX12 renderer capability interfaces,
  runtime render pass boundaries, runtime-boundary tooling, and evidence docs.
- Validation for this progress-only edit: no repository validation required.
- CodeGraph is present and current as of this handoff: `codegraph status .`
  reported an up-to-date index.
- Current spot-check evidence suggests this phase should stay narrow:
  `SkullbonezSource/Rendering/IRenderBackend.h` is a temporary aggregate with no
  own render methods, `SkullbonezSource/Rendering/IRenderRayTracing.h` owns the
  DXR methods, and a targeted scan found no `Gfx()`, `GfxRayTracing()`, or
  `IRenderBackend` hits in `SkullbonezSource/Runtime/RunPasses.cpp`,
  `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`,
  `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`, or
  `SkullbonezSource/Runtime/RunUiTextPass.cpp`.
- Treat `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`
  and `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
  as prior evidence only until Phase 0 regenerates them from the final current
  source tree.

## Checklist

- [ ] Record the current `IRenderBackend` surface from
  `SkullbonezSource/Rendering/IRenderBackend.h`. Expected result: only the
  destructor is declared directly; lifecycle, resource factory, command,
  diagnostics, and capture behavior is inherited from named capability
  interfaces.
- [ ] Remove any direct render operation that has reappeared in
  `SkullbonezSource/Rendering/IRenderBackend.h`. Change it into the matching
  named capability interface instead of widening the facade.
- [ ] Remove any `IRenderRayTracing` inheritance or DXR method declarations from
  `SkullbonezSource/Rendering/IRenderBackend.h`. Keep DXR methods in
  `SkullbonezSource/Rendering/IRenderRayTracing.h`.
- [ ] Record the current DXR borrow path from
  `SkullbonezSource/Runtime/RunRender.cpp`: `Run::Render()` may borrow
  `GfxRayTracing()` at the composition root and pass an optional
  `IRenderRayTracing*` through `RuntimeRenderServices` and `RenderFrameContext`.
- [ ] Remove or change any direct `Gfx()` / `GfxRayTracing()` /
  `IRenderBackend` access found in runtime pass bodies. In
  `SkullbonezSource/Runtime/RunPasses.cpp`, use `RenderCommands(frame)`,
  `RenderResources(resources)`, `RenderDiagnostics(frame)`, or
  `frame.renderRayTracing` instead.
- [ ] Regenerate a pass-boundary scan and record the output:
  `rg -n "Gfx\\(|GfxRayTracing\\(|IRenderBackend" SkullbonezSource\Runtime\RunPasses.cpp SkullbonezSource\Runtime\Render\RuntimeRenderPasses.h SkullbonezSource\Runtime\Render\RuntimeRenderInputs.h SkullbonezSource\Runtime\RunUiTextPass.cpp`.
  Expected result: no matches.
- [ ] Reconfirm `tools/check_runtime_boundaries.py` still blocks direct
  `IRenderBackend` methods, `IRenderBackend` raytracing inheritance,
  raytracing declarations on `IRenderBackend`, direct `Gfx().BuildTLAS` style
  calls, and unclassified global renderer service access.
- [ ] Regenerate runtime-boundary evidence when implementation scope allows:
  `python tools\check_runtime_boundaries.py --repo . --json-out TestOutput\validation\agent_logs\carmack_phase4_runtime_boundaries.json`.
  Record both the console result and the JSON path.
- [ ] Consume the Phase 0 regenerated global-service classification. Filter for
  `Gfx()`, `GfxRayTracing()`, `IRenderBackend`, render-pass classifications,
  and normal-runtime-path renderer hits. Resolve only render-backend capability
  issues in this phase; leave broader service lifetime work to Phase 2.
- [ ] Record final Phase 4 evidence paths in this progress file, and only copy
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

- CodeGraph status line showing the index is current, or a note that CodeGraph
  was unavailable and targeted `rg` was used.
- `IRenderBackend.h` inventory: own methods, base interfaces, and confirmation
  that `IRenderRayTracing` is not inherited.
- `IRenderRayTracing.h` inventory: DXR methods remain on the separate capability.
- Pass-body scan output for `RunPasses.cpp`, `RuntimeRenderPasses.h`,
  `RuntimeRenderInputs.h`, and `RunUiTextPass.cpp`.
- Runtime-boundary checker output and optional JSON artifact from
  `tools/check_runtime_boundaries.py --repo . --json-out ...`.
- Regenerated global-service classification rows for renderer labels, with each
  `render pass` or `normal runtime path` renderer hit marked as resolved,
  accepted compatibility debt, or transferred to Phase 2.
- For any source change: comment-style audit note, `git diff --check`, and the
  smallest required PR-gate validation logs.

## Validation Note

This progress-file creation is documentation-only, so no repository validation
script is required. For future Phase 4 source work, do not run repository
validation while iterating unless a focused probe answers a specific question.
At PR/commit gate, use the narrowest matching validation:

- `python tools\check_runtime_boundaries.py --repo .` and
  `tools\validate_runtime_boundaries.bat` for boundary-only changes.
- `tools\validate_fast.bat` plus the changed script for edits under `tools/`.
- `tools\validate_dx12_renderer.bat` for render interface, runtime render pass,
  DX12 backend, shader, or screenshot-sensitive changes.
- `tools\validate_full.bat` if Phase 4 changes multiple runtime/render areas or
  the validation scope is uncertain.

## Open Risks And Questions

- The prior classification summary reports renderer global hits, but the
  authoritative plan calls out stale evidence. Do not close Phase 4 from that
  report without regenerating current data.
- `Run::Render()` still uses `Gfx()` and `GfxRayTracing()` as the composition
  root. Decide whether any new finding belongs in this narrow phase or in Phase
  2 global-service lifetime work before changing it.
- `RuntimeRenderHost` remains a broad bridge. Do not grow it to hide a renderer
  capability leak; prefer existing frame/resource contexts or a named narrow
  capability.
- `tools/check_runtime_boundaries.py` uses regex guardrails and synthetic tests,
  not a full C++ parser. Exotic declarations should still get manual review.
- Files outside runtime pass bodies, such as `Rendering/Helper.cpp`,
  `Rendering/Text.cpp`, `UI/UI.cpp`, and `Runtime/Editor/LauncherLaser.cpp`,
  still contain compatibility `Gfx()` access. Only pull them into Phase 4 if the
  regenerated classification identifies a render-backend capability violation.

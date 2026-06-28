# Carmack Global Service Lifetime Plan

Date: 2026-06-28
Status: In progress
Impact area: runtime ownership, rendering, assets, input/window, scene system,
tooling, tests
Validation note: plan-only edits require no validation. PR-bound implementation
should choose the narrowest gate from `AGENTS.md`; broad lifetime changes usually
require `tools\validate_full.bat`.

## Completed Slices

- [x] 2026-06-28: Added a counted global-service access ratchet to
  `tools\check_runtime_boundaries.py`. The guardrail ignores comments and string
  literals, preserves the current compatibility surface as counted debt, and
  fails on new or grown calls to `Cfg()`, `Gfx()`, `GfxRayTracing()`,
  `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, named service
  singletons, generic non-named `Class::Instance()` access, `pInstance`
  singleton storage, and mutable `g_*` process globals. This is a new-code
  ratchet only; per-site bootstrap/shutdown/OS-bridge classification and source
  migration remain open. Validation: `python tools\check_runtime_boundaries.py`
  passed in 4.81s
  (`TestOutput\validation\agent_logs\global_service_guardrail_runtime_boundaries.log`);
  `tools\validate_fast.bat` passed in 18.01s
  (`TestOutput\validation\agent_logs\global_service_guardrail_validate_fast.log`).
  Comment-style audit: inspected the touched tool script
  `tools\check_runtime_boundaries.py` against
  `Agentic\Reference\comment-style-guide.md`; the learning header remains
  present, and the new ratchet/allowlist comments state the invariant and
  limits of the guardrail.
- [x] 2026-06-28: Extended runtime render services so renderer-facing frame code
  can borrow an explicit render command capability instead of reaching through
  `Gfx()` for these frame operations.
  `RuntimeRenderer::RenderFrame()` now clears through the borrowed command
  context and checks the captured render-ready flag; `RunRender.cpp` direct
  `Gfx()` calls fell from 2 to 1, and the guardrail allowlist was lowered to
  that new count. Comment-style audit: inspected `RuntimeRenderInputs.h`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the new context
  fields have a `Lifetime:` note for the borrowed command facet and no ownership
  moved out of `Run`.
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
  narrowed to the command capability. The review's stale-validation blocker was
  resolved by rerunning DX12 and full validation after the final source edit,
  and the broad borrowed-context checklist item remains open for future
  non-render contexts.
- [x] 2026-06-28: Threaded the borrowed render command capability into
  `RenderFrameContext` and switched the runtime render-pass texture-slot hygiene
  helpers away from direct `Gfx()` access. This lowers `RunPasses.cpp` direct
  `Gfx()` debt from 98 to 96 and the plan's total counted `Gfx()` surface from
  293 to 291. The broader render-pass/global-service migration remains open
  because the same file still has many compatibility-facade calls for viewport,
  clear, depth/blend/cull, draw state, resource creation, and DXR decisions.
  Comment-style audit: inspected `RuntimeRenderPasses.h`, `RunPasses.cpp`,
  `RunRender.cpp`, and `tools\check_runtime_boundaries.py`; the command pointer
  is lifetime-annotated and asserted before helper use.
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
  Rubber-duck review: Hubble found no blockers; the reviewer called out the
  nullable/assert-only command pointer and counted-ratchet limits as residual
  risks to keep visible in later slices.
- [x] 2026-06-28: Routed generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 96 to 88
  and the plan's total counted `Gfx()` surface from 291 to 283. The broad
  normal-path render-pass migration remains open because other state,
  viewport, clear, resource, dynamic geometry, and DXR paths still reach the
  compatibility facade.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
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
- [x] 2026-06-28: Routed `WaterPass::Render()` depth/blend state through
  `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of direct
  `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 88 to 76
  and the plan's total counted `Gfx()` surface from 283 to 271. The broad
  render-pass/global-service migration remains open for shadow, reflection,
  tornado, volumetric, tonemap, viewport, clear, resource creation, and DXR
  paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
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
- [x] 2026-06-28: Routed `TornadoVisualPass::Render()` depth/blend/cull state
  and transient colored-triangle draw through `RenderFrameContext`'s borrowed
  `IRenderCommandContext` instead of direct `Gfx()` calls. This lowers
  `RunPasses.cpp` direct `Gfx()` debt from 76 to 60 and the plan's total
  counted `Gfx()` surface from 271 to 255. The broad render-pass/global-service
  migration remains open for shadow, reflection, volumetric, tonemap, viewport,
  clear, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  frame command context and does not introduce new ownership.
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
- [x] 2026-06-28: Routed `ShadowPass::RenderShadowMap()` viewport/clear,
  depth/blend/cull state, polygon offset, and final viewport restore through
  its existing `IRenderCommandContext` argument instead of direct `Gfx()` calls.
  This lowers `RunPasses.cpp` direct `Gfx()` debt from 60 to 44 and the plan's
  total counted `Gfx()` surface from 255 to 239. The broad
  render-pass/global-service migration remains open for shadow target creation,
  reflection, volumetric, tonemap, resource creation, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current shadow-map state restore contract.
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
- [x] 2026-06-28: Routed `VolumetricPass::Render()` viewport and depth/blend
  state through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead
  of direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from
  44 to 32 and the plan's total counted `Gfx()` surface from 239 to 227. The
  broad render-pass/global-service migration remains open for the fullscreen
  quad helper, resource creation, reflection, tonemap, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
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
- [x] 2026-06-28: Routed `TonemapPass::Render()` viewport and depth/blend state
  through `RenderFrameContext`'s borrowed `IRenderCommandContext` instead of
  direct `Gfx()` calls. This lowers `RunPasses.cpp` direct `Gfx()` debt from 32
  to 21 and the plan's total counted `Gfx()` surface from 227 to 216. The broad
  render-pass/global-service migration remains open for the fullscreen quad
  helper, resource creation, reflection, and DXR paths.
  Comment-style audit: inspected `RunPasses.cpp` and
  `tools\check_runtime_boundaries.py`; the change uses the existing borrowed
  command context and preserves the current screen-space state restore
  contract.
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

## Current Counted Global-Service Surface

This snapshot is the guardrail allowlist as of 2026-06-28. Counts are taken from
source-bearing files under `SkullbonezSource/` after stripping comments and
string literals.

| Pattern | Current count |
|---------|---------------|
| `Cfg()` | 239 |
| `Gfx()` | 216 |
| `GfxRayTracing()` | 5 |
| `ActiveAssetSystem()` | 4 |
| `CreateShaderFromActiveAssets()` | 16 |
| `TextureCollection::Instance()` | 3 |
| `CameraCollection::Instance()` | 4 |
| `Window::Instance()` | 8 |
| `SkyBox::Instance()` | 2 |
| `WorkerPool::Instance()` | 18 |
| `Profiler::Instance()` | 27 |
| Generic non-named `Class::Instance()` | 7 |
| `pInstance` | 20 |
| Mutable `g_*` process global | 86 |

## Problem Statement

The Carmack-test verdict flagged remaining globals and singletons as a serious
encapsulation risk. Some globals are legitimate OS callback bridges, but normal
runtime/render/asset/scene paths still use process-global service access where
explicit lifetime and ownership would be easier to reason about.

## Goal

Replace normal-path global service access with explicit service contexts,
borrowed interfaces, or composition-root wiring. Keep unavoidable OS callback
bridges tiny, named, and fenced.

## Success Bar

- New runtime, render, physics, scene, asset, UI, and diagnostics code does not
  call process-global service accessors.
- Existing global access is either removed, isolated to bootstrap/shutdown, or
  documented as an OS callback bridge.
- Service lifetime order is explicit in `Run`, `EngineContext`, or narrower
  subsystem contexts.
- Guardrails reject new normal-path global access.

## Related Plans

- `Agentic/Plans/global-service-context-plan.md` is the active umbrella plan for
  removing normal-path global service access. Use this Carmack plan as the
  final acceptance checklist for the encapsulation bar.
- `Agentic/Plans/carmack-render-backend-capability-plan.md` owns renderer
  capability access and `Gfx()` migration in renderer-facing code. This plan
  owns the broader service-lifetime and callback-bridge rules.
- `Agentic/Plans/runtime-static-allocation-policy-plan.md` owns dynamic
  allocation policy for any new context storage introduced during this cleanup.

## Implementation Checklist

### Inventory

- [x] Run `rg "Gfx\\(|GfxRayTracing\\(|Cfg\\(|ActiveAssetSystem\\(|CreateShaderFromActiveAssets\\(|::Instance\\(|pInstance|g_[A-Za-z_]" SkullbonezSource`.
- [ ] Classify each hit as `bootstrap`, `shutdown`, `OS callback bridge`,
  `normal runtime path`, `render pass`, `asset lookup`, `diagnostics`, or
  `test/tool`.
- [x] Record the current allowlist in this plan before changing source.
- [ ] Identify service lifetime owners already available in `Run`,
  `EngineContext`, `RuntimeRenderHost`, `RuntimeTools`, `DiagnosticsRuntime`, and
  `SceneController`.

### Service Context Shape

- [ ] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared.
- [x] Define or extend a `RenderServices`/`RenderContext` for renderer-facing
  services instead of direct `Gfx()` calls.
- [ ] Define or extend an `AssetContext` for asset lookup and source records
  instead of `ActiveAssetSystem()`.
- [ ] Define or extend an `InputEventBuffer` or input bridge for Win32 callback
  accumulators.
- [ ] Define or extend a `WindowService` or explicit window reference for window
  queries and title/resize behavior.
- [ ] Keep contexts borrowed and lifetime-annotated; do not create a new global
  service locator under a nicer name.

### Remove Normal-Path Globals

- [ ] Route render pass backend access through render capability/context
  arguments.
- [x] Route render pass texture-slot hygiene helpers through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route generated cinematic sky depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route water depth/blend state through `RenderFrameContext`'s borrowed
  command context instead of direct `Gfx()` calls.
- [x] Route tornado visual depth/blend/cull state and transient triangle draw
  through `RenderFrameContext`'s borrowed command context instead of direct
  `Gfx()` calls.
- [x] Route shadow-map viewport/clear/depth/blend/cull/polygon-offset state
  through its existing command context argument instead of direct `Gfx()` calls.
- [x] Route volumetric pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [x] Route tonemap pass viewport/depth/blend state through
  `RenderFrameContext`'s borrowed command context instead of direct `Gfx()`
  calls.
- [ ] Route shader and texture creation through an asset/render context passed
  from runtime-owned services.
- [ ] Replace `ActiveAssetSystem()` in scene parsing and editor tools with an
  explicit asset context.
- [ ] Replace `TextureCollection::Instance()` normal-path lookups with runtime
  owned texture service references.
- [ ] Replace `CameraCollection::Instance()` normal-path lookups with explicit
  camera service references.
- [ ] Replace `Window::Instance()` normal-path lookups with explicit window
  service references after bootstrap.
- [ ] Replace `SkyBox::Instance()` with runtime/world-owned skybox lifetime or a
  scene-render resource owner.
- [ ] Keep config reads grouped through launch/runtime config context where
  possible; do not spread new `Cfg()` calls.

### OS Callback Bridges

- [ ] Keep Win32 input globals only behind a tiny bridge if callback signatures
  require process-static state.
- [ ] Add comments naming who samples, resets, and owns each callback
  accumulator.
- [ ] Add an explicit bind/unbind lifecycle for callback bridge state.
- [ ] Ensure callback bridge teardown cannot leave dangling service pointers.
- [ ] Add focused tests or debug assertions for callback bridge lifecycle.

### Lifetime Order

- [ ] Document startup bind order for renderer, assets, textures, window,
  cameras, input, diagnostics, and scene services.
- [ ] Document shutdown unbind order and backend resource release order.
- [ ] Add assertions that borrowed service pointers are bound before use.
- [x] Add a local assertion before runtime render-pass texture-slot helpers use
  `RenderFrameContext::renderCommands`.
- [ ] Add assertions that services are unbound before destruction when callbacks
  can fire late.
- [ ] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers.

### Guardrails

- [x] Extend `tools\check_runtime_boundaries.py` to block new normal-path
  `Gfx()`, `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, and
  singleton `Instance()` calls outside allowlisted bootstrap/bridge files.
  2026-06-28 note: implemented as a counted new-code ratchet for current source
  files; the stricter bootstrap/bridge classification is still open under
  Inventory.
- [x] Add counted allowlists for remaining globals and lower them after each
  migration slice.
- [x] Add synthetic checker tests that reject a new normal-path global service
  access.
- [ ] Add a review checklist entry asking whether a new dependency should be a
  borrowed context instead of a global.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [x] For runtime-wide lifetime or startup/shutdown changes: run `tools\validate_full.bat`.
- [x] For renderer service access changes: run `tools\validate_dx12_renderer.bat`.
- [ ] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: run `tools\validate_full.bat`.
- [ ] For input/window changes: run `tools\validate_full.bat`; add focused
  launch/click validation if interaction behavior changes.
- [x] For guardrail-tooling changes: run `python tools\check_runtime_boundaries.py`
  and `tools\validate_fast.bat`.
- [x] Quote validation output and log paths in the handoff.

## Independent Review Checklist

- [x] Ask a rubber-duck reviewer to distinguish legitimate callback bridges from
  avoidable service locators.
- [x] Ask the reviewer to inspect startup/shutdown lifetime order and borrowed
  pointer safety.
- [x] Ask the reviewer to search for new normal-path global access.
- [x] Record review findings in a report or this plan.
- [x] Resolve blocking findings before committing PR-bound code.

Reviewer notes, 2026-06-28:

- Ampere blocked the first guardrail draft because it only watched named
  renderer/asset/window singleton access and missed plan-named `Cfg()`, generic
  `::Instance()`, `pInstance`, and `g_*` process globals.
- Ampere also flagged that file-level counts are a ratchet, not a substitute
  for classifying every remaining call as bootstrap, shutdown, OS callback
  bridge, or normal runtime debt.
- The guardrail was expanded to cover the missing patterns and string/comment
  false positives were removed. The broader per-site classification remains
  intentionally unchecked.
- Ampere re-checked the expanded guardrail and found no remaining blockers
  before commit. Residual non-blocking note: the `g_*` pattern counts references
  as well as declarations, which is conservative but acceptable for this
  new-code ratchet.

## Definition Of Done

- [ ] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals.
- [ ] Remaining globals are bootstrap-only, shutdown-only, or OS callback bridges
  with explicit lifecycle comments.
- [ ] Guardrails prevent new global service access from creeping back in.
- [ ] Required validation passes for the touched implementation areas.

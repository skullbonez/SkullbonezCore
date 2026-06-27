# Carmack Global Service Lifetime Plan

Date: 2026-06-28
Status: Draft
Impact area: runtime ownership, rendering, assets, input/window, scene system,
tooling, tests
Validation note: plan-only edits require no validation. PR-bound implementation
should choose the narrowest gate from `AGENTS.md`; broad lifetime changes usually
require `tools\validate_full.bat`.

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

- [ ] Run `rg "Gfx\\(|GfxRayTracing\\(|Cfg\\(|ActiveAssetSystem\\(|CreateShaderFromActiveAssets\\(|::Instance\\(|pInstance|g_[A-Za-z_]" SkullbonezSource`.
- [ ] Classify each hit as `bootstrap`, `shutdown`, `OS callback bridge`,
  `normal runtime path`, `render pass`, `asset lookup`, `diagnostics`, or
  `test/tool`.
- [ ] Record the current allowlist in this plan before changing source.
- [ ] Identify service lifetime owners already available in `Run`,
  `EngineContext`, `RuntimeRenderHost`, `RuntimeTools`, `DiagnosticsRuntime`, and
  `SceneController`.

### Service Context Shape

- [ ] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared.
- [ ] Define or extend a `RenderServices`/`RenderContext` for renderer-facing
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
- [ ] Add assertions that services are unbound before destruction when callbacks
  can fire late.
- [ ] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers.

### Guardrails

- [ ] Extend `tools\check_runtime_boundaries.py` to block new normal-path
  `Gfx()`, `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()`, and
  singleton `Instance()` calls outside allowlisted bootstrap/bridge files.
- [ ] Add counted allowlists for remaining globals and lower them after each
  migration slice.
- [ ] Add synthetic checker tests that reject a new normal-path global service
  access.
- [ ] Add a review checklist entry asking whether a new dependency should be a
  borrowed context instead of a global.

## Validation Checklist

- [ ] For plan-only edits: no validation required.
- [ ] For runtime-wide lifetime or startup/shutdown changes: run `tools\validate_full.bat`.
- [ ] For renderer service access changes: run `tools\validate_dx12_renderer.bat`.
- [ ] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: run `tools\validate_full.bat`.
- [ ] For input/window changes: run `tools\validate_full.bat`; add focused
  launch/click validation if interaction behavior changes.
- [ ] Quote validation output and log paths in the handoff.

## Independent Review Checklist

- [ ] Ask a rubber-duck reviewer to distinguish legitimate callback bridges from
  avoidable service locators.
- [ ] Ask the reviewer to inspect startup/shutdown lifetime order and borrowed
  pointer safety.
- [ ] Ask the reviewer to search for new normal-path global access.
- [ ] Record review findings in a report or this plan.
- [ ] Resolve blocking findings before committing PR-bound code.

## Definition Of Done

- [ ] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals.
- [ ] Remaining globals are bootstrap-only, shutdown-only, or OS callback bridges
  with explicit lifecycle comments.
- [ ] Guardrails prevent new global service access from creeping back in.
- [ ] Required validation passes for the touched implementation areas.

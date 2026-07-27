# Render Backend Service Bag Removal

Date: 2026-07-26
Status: IN PROGRESS — RB0 closed on 2026-07-27 with every construction,
transport, and consumption site classified; RB1 is binding. Originally drafted
from the 2026-07-26 from-source architecture review of
`nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 6 of the Architecture Follow-Up Campaign
Round 5. Sequences before `runtime-frame-view-retirement`. 1/4 phases complete.
Impact area: `Runtime/Render/RuntimeRenderHost.h`, `Runtime/App/Run.*`,
`Runtime/Render/RenderResourceLifecycle.h`, `Runtime/Render/RuntimeRenderer.h`,
`Runtime/Capture/RuntimeStressController.h`, `Runtime/RuntimeFrameViews.h`
Owner: runtime render
Priority: Medium-High — the repository has already ruled this shape a service
bag once and it survived.

## Problem And Evidence (measured 2026-07-26)

`RuntimeRenderBackendView` (`Runtime/Render/RuntimeRenderHost.h:150`) is a struct
of eleven nullable raw pointers to concrete DX12 owner types:

```
Dx12RenderDevice*      renderDevice
Dx12FrameOwner*        renderFrame
Dx12GraphTransientPool* renderGraph
Dx12ResourceBuilder*   renderResources
Dx12TextureOwner*      renderTextures
Dx12GeometryOwner*     renderGeometry
Dx12Diagnostics*       renderDiagnostics
Dx12BackbufferCapture* backbufferCapture
Dx12RaytracingOwner*   raytracing
Dx12ShaderDevelopment* shaderDevelopment
Dx12ImGuiRendererOwner* developmentUiRenderer   [development builds]
```

Three properties make it the banned shape rather than a value:

1. **Capability presence is a null check at every use site.** The struct carries
   `RequireBackbufferCapture()` (`:167`) which Lane-F terminates when the pointer
   is null. That method exists because callers cannot know from the type whether
   the capability is present. "Does this backend do raytracing" is answered by
   testing a pointer eleven fields into a bag.

2. **The repository has already ruled it.** The
   `concrete-parameter-bag-elimination` PB3 review rejected it as a composer
   parameter in exactly these terms — "The first ownership review rejected an
   intermediate `RuntimeRenderBackendView` composer parameter because it merely
   moved the service bag"
   (`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb3-render-ui.md:44-48`).
   PB3 removed it from the composer boundary and recorded that "RuntimeRenderer
   retains all backend authority." It did not remove the bag itself, so it
   survives as `Run::m_renderBackendView` (`Runtime/App/Run.h:171`), as a
   constructor parameter to `Run` (`:246`), `RuntimeRenderer` (`Runtime/Render/RuntimeRenderer.h:233`),
   and `RenderResourceLifecycle` (`Runtime/Render/RenderResourceLifecycle.h:50`),
   as a `RuntimeStressController` parameter (`Runtime/Capture/RuntimeStressController.h:81`),
   and as a member of `RuntimeFramePresentationView` (`Runtime/RuntimeFrameViews.h:171`).

3. **It is the only place Runtime names eleven concrete DX12 types at once.**
   `AGENTS.md` requires Rendering contracts to stay feature-neutral and polices
   that with a name check. This struct shows the rule is guarding vocabulary while
   the actual backend coupling is concentrated in one type that no rule catches.
   That is a finding about the enforcement, not an argument for adding a
   render interface — `render-interface-retirement` (RH0-RH5, closed 2026-07-22)
   deliberately deleted all ten render interfaces, and that ruling stands.

## Goal

Backend capabilities reach their consumers as concrete owner references supplied
by the owner that has them, with optional capabilities expressed as an explicit
presence decision at one composition point rather than as a nullable field
inspected everywhere.

## Non-Goals

- **No new render interface, virtual dispatch, or type erasure.** The
  `render-interface-retirement` ruling is binding: concrete DX12 owners stay
  non-polymorphic. This plan removes a bag; it does not reintroduce an
  abstraction.
- No behavior change. DX12 baselines, `dx12_validation.txt` zero-error status,
  capture output, and stress behavior are preserved exactly.
- No change to which capabilities are optional. Raytracing, shader development,
  and the development UI renderer stay optional; only how their absence is
  expressed changes.
- No replacement aggregate under another suffix. A `RenderBackendOperands` or
  `Dx12CapabilitySet` that carries the same eleven pointers is a closure failure.
- No scope on the four `RuntimeFrame*View` structs beyond removing this one
  member from `RuntimeFramePresentationView`.
- No change to `Rendering/` layer ownership or the DX12 binding ABI.

## Phases

- [x] **RB0 — Census consumers and capability requirements.**
  For every construction and consumption site of `RuntimeRenderBackendView`,
  record which of the eleven pointers it actually dereferences, whether it
  null-tests them, and which owner already holds the concrete pointer at that
  point. Separately classify the three optional capabilities: who decides presence,
  when, and what the current behavior is when absent. Record every
  `RequireBackbufferCapture()` call site and what it would do if the capability
  were expressed as a required constructor operand instead. Acceptance: each site
  has a minimum required pointer set; no site's needs are unknown; the report
  states whether any consumer genuinely needs more than a handful of owners, which
  would indicate a consumer that should itself be decomposed.
  Closed 2026-07-27. Successful startup is the sole eleven-pointer
  construction, no external consumer needs more than five owners, and only the
  ratified renderer/lifecycle authority touches the eight-owner render epoch.
  All four capture accessor calls are required-capability rediscovery.
  Evidence:
  `../../Reports/2026-07-27/render-backend-service-bag-removal-rb0-census.md`.

- [ ] **RB1 — Supply required capabilities as concrete operands.**
  Replace the bag with the concrete owner references each consumer needs.
  `RuntimeRenderer` already retains backend authority per PB3, so most consumers
  should receive nothing new — they should ask `RuntimeRenderer`. Delete the
  member from `Run` and the parameters from `Run`, `RuntimeRenderer`,
  `RenderResourceLifecycle`, and `RuntimeStressController` where RB0 shows the
  consumer needs a narrower set. Acceptance: no consumer receives a pointer it
  does not dereference; no operation exceeds 12 parameters; DX12 validation is
  zero-error; DX12 baselines unchanged.

- [ ] **RB2 — Make optional capability presence an explicit composition decision.**
  Raytracing, shader development, and the development UI renderer become an
  explicit presence decision made once where the backend is composed, not a
  nullable field. `RequireBackbufferCapture()` disappears: the capture owner is
  either a required operand of the operations that capture, or the operation is not
  reachable without it. Remove the member from `RuntimeFramePresentationView`
  (`RuntimeFrameViews.h:171`). Acceptance:
  `rg -n 'RuntimeRenderBackendView' SkullbonezSource SkullbonezTests` returns no
  rows; no Lane-F fatal remains for a capability that could have been a required
  operand; screenshot/readback, raytraced reflection, shader reload, and the
  ImGui development surface all behave identically.

- [ ] **RB3 — Reconcile, review, and hand off.**
  Complete the comment audit for every touched header, correcting the ownership
  claims in `RuntimeRenderHost.h` and `RuntimeFrameViews.h` that describe the
  removed type. Obtain one independent ownership review asking: did the eleven
  pointers reappear in one type under any name, did any consumer gain backend
  authority it did not have, and is any optional capability still expressed as a
  nullable field inspected at more than one site. Acceptance: review clear;
  `validate_dx12_renderer.bat` run three consecutive times with
  `dx12_validation.txt` at zero errors, `run_graphics_stress.bat 1` crash-free
  with recorded runtime and exit evidence, `validate_full.bat` and
  `validate_perf.bat` pass with no baseline refresh.

## Dependencies And Decisions

- Depends on `governance-shape-to-judgment-conversion` G1 for the review test.
- Sequences before `runtime-frame-view-retirement` so FV2 does not have to
  preserve this member.
- Binding prior ruling carried forward: PB3's finding that `RuntimeRenderer`
  retains all backend authority and that no backend service view crosses the
  composer boundary. This plan extends that to the remaining sites.
- Binding prior ruling carried forward: `render-interface-retirement` deleted all
  render interfaces. No phase of this plan may add one back.

## Acceptance

- `RuntimeRenderBackendView` does not exist and is not replaced.
- No consumer holds a backend pointer it does not use.
- Optional capability presence is decided once at composition, not tested at each
  use.
- Zero DX12 validation errors; DX12 baselines and capture output unchanged.

## Validation

Per the File To Validation Mapping, every DX12 modification requires the renderer
gate plus bounded graphics stress:

- `tools\validate_dx12_renderer.bat` run three consecutive times (upload-buffer /
  frame-allocator danger zone), with `dx12_validation.txt` verified at zero errors
- `tools\run_graphics_stress.bat 1` — record command, measured runtime, and
  successful exit
- `tools\validate_perf.bat`
- `tools\validate_full.bat` — `Run*` and `Runtime/*` changed

# Render Interface Retirement — Admit The DX12 Engine

Date: 2026-07-22
Status: Registered — 0/6 phases complete
Impact area: Rendering HAL headers, DX12 backend, runtime render host/passes,
UI render context, primitive/text renderers
Owner: rendering

## Owner Ruling (2026-07-22)

DX12 is the terminal runtime backend. The abstract render interface layer is
retired. This supersedes the retained-HAL exception recorded at
`render-hal-modernization` M0/M5; that plan modernized the seam, this one
deletes it.

## Problem And Evidence (2026-07-22 census)

- Ten interface headers survive with exactly one implementation each:
  `IRenderDeviceLifecycle`, `IRenderResourceFactory`,
  `IRenderCommandContext`, `IRenderDiagnostics`, `IRenderCaptureBackend`,
  `IRenderRayTracing`, `IRenderShaderDevelopment`, plus the resource-object
  interfaces `IShader`, `IMesh`, `IFramebuffer`.
- `RenderBackendDX12` inherits all seven backend interfaces in one class —
  an interface-aggregation monolith. Its interior is already decomposed
  into concrete owners (`Dx12RenderDevice`, `Dx12DescriptorHeaps`,
  `Dx12FrameOwner`, `Dx12TextureOwner`, `Dx12PipelineOwner`,
  `Dx12GeometryOwner`, capture/diagnostics/raytracing owners).
- GL and DX11 are deleted; the interfaces buy zero backend portability and
  cost a permanent layer of naming, virtual dispatch, and header
  indirection. The dispatch cost is negligible (calls are coarse-grained);
  the conceptual cost is not: every capability addition edits an interface,
  the backend, and a view struct.

## Goal

Concrete DX12 types at every rendering seam. Consumers borrow the
*narrowest concrete owner* that answers their need — not one wide backend
facade — so retiring the interfaces also narrows access instead of merely
renaming it. Zero virtual dispatch remains in the render submission path.

## Non-Goals

- No visual or behavioral change: committed DX12 baselines and thresholds
  are untouched; zero DX12 validation errors remains the bar.
- No replacement mega-facade. Collapsing seven interfaces into one concrete
  class with the union surface is a closure failure under god-object rule 8.
  The census in RH0 must show each consumer's access got narrower or stayed
  equal, never wider.
- No render-graph redesign, pass-order change, or shader work.
- No renames for their own sake; `RenderBackendDX12` keeps its name unless
  RH4 finds a concrete confusion cost.

## Phases

- [ ] RH0. Census. For each of the ten interfaces: every implementer (prove
  single), every consumer file, and the member subset each consumer actually
  calls. From that, record the per-seam target: which concrete owner type
  replaces the interface at each consumer (e.g. `UIRenderContext` fields
  become concrete resource/command/diagnostics owner borrows;
  `RuntimeRenderBackendView` becomes a concrete-owner view). Confirm no
  null/headless implementer exists for text-only runs — if one is found, RH0
  records the replacement policy (skip-at-call-site, as `Run::Render`
  already does) before any deletion starts. Documentation-only.
- [ ] RH1. Cold surfaces. Retire `IRenderCaptureBackend`,
  `IRenderShaderDevelopment`, and `IRenderRayTracing`; consumers take the
  concrete capture/shader-development/raytracing owners. Renderer gate plus
  bounded stress run.
- [ ] RH2. Resource surfaces. Retire `IRenderResourceFactory`,
  `IRenderDiagnostics`, and the resource-object interfaces
  `IShader`/`IMesh`/`IFramebuffer`; devirtualize the DX12 resource types and
  update `UIRenderContext`, asset, text, and primitive-batch consumers to
  the concrete owners chosen in RH0. Renderer gate plus stress run.
- [ ] RH3. Command surface. Retire `IRenderCommandContext` — the hot seam.
  Passes, `PrimitiveBatchRenderer`, `Text`, and UI draw submission record
  against the concrete command recorder. Verify by inspection and by the
  perf gate that no pass regressed; this is the one slice that also runs
  `tools\validate_perf.bat`.
- [ ] RH4. Lifecycle and cleanup. Retire `IRenderDeviceLifecycle`, delete
  all ten interface headers, remove now-dead `virtual`/`override` and
  virtual destructors, and re-point `RuntimeRenderBackendView` and the
  `Run` wiring at concrete types. Update `Agentic/Tests/Dx12ArchUnitTests`
  expectations in the same commit (registered CPU umbrella target).
- [ ] RH5. Closure. Rerun the RH0 census from final source proving zero
  interface rows remain and per-consumer access narrowed or held equal.
  Comment-style audit of touched files, independent review, final gates
  three-run renderer repeat (danger-zone upload/frame allocator row applies
  if frame-owner seams moved).

## Review Proof (must return no rows at RH5)

```powershell
rg -n 'class I(Render|Shader|Mesh|Framebuffer)' SkullbonezSource/Rendering
rg -n '\bvirtual\b' SkullbonezSource/Rendering --glob '!Rendering/DX12/RenderDeviceDX12.h'
```

Any surviving `virtual` in Rendering outside a justified row recorded in
this plan's exception table is a closure failure.

## Dependencies And Decisions

- Independent of the other 2026-07-22 plans; sequenced first because it is
  mechanical, shrinks headers/recompiles for everything after it, and its
  gates are the cheapest to repeat.
- Decision for RH0 to record: whether `Dx12ArchUnitTests` rely on interface
  mocking (if so, the tests move to concrete-owner seams in the same phase
  that deletes the interface they mock — never a separate follow-up).
- Exception table (owner / reason / deletion condition) starts empty; any
  interface or virtual that must survive gets a row here or the phase does
  not close.

## Acceptance

- Zero `IRender*`/`IShader`/`IMesh`/`IFramebuffer` interface classes remain;
  review proofs return no rows.
- No consumer's reachable render surface widened (RH5 census diff).
- Zero DX12 validation errors; committed screenshots within existing
  thresholds; zero warnings at `/W4`.
- Every task records its mandatory crash-free
  `tools\run_graphics_stress.bat 1` command, measured runtime, and exit
  evidence (inventory rule 10).

## Validation

Every task: `tools\validate_dx12_renderer.bat` then
`tools\run_graphics_stress.bat 1`. RH3 adds `tools\validate_perf.bat`.
RH4/RH5 add `tools\validate_all_cpu_tests.bat` (arch-test changes) and the
final task runs `tools\validate_full.bat` from the closure tip.

# Render Interface Retirement — Admit The DX12 Engine

Date: 2026-07-22
Status: Active — 5/6 phases complete
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

- [x] RH0. Census. For each of the ten interfaces: every implementer (prove
  production-single and record test doubles), every consumer file, and the
  member subset each consumer actually
  calls. From that, record the per-seam target: which concrete owner type
  replaces the interface at each consumer (e.g. `UIRenderContext` fields
  become concrete resource/command/diagnostics owner borrows;
  `RuntimeRenderBackendView` becomes a concrete-owner view). Confirm no
  null/headless implementer exists for text-only runs — if one is found, RH0
  records the replacement policy (skip-at-call-site, as `Run::Render`
  already does) before any deletion starts. Documentation-only.
- [x] RH1. Cold surfaces. Retire `IRenderCaptureBackend`,
  `IRenderShaderDevelopment`, and `IRenderRayTracing`; consumers take the
  concrete capture/shader-development/raytracing owners. Renderer gate plus
  bounded stress run.
- [x] RH2. Resource surfaces. Retire `IRenderResourceFactory`,
  `IRenderDiagnostics`, and the resource-object interfaces
  `IShader`/`IMesh`/`IFramebuffer`; devirtualize the DX12 resource types and
  update `UIRenderContext`, asset, text, and primitive-batch consumers to
  the concrete owners chosen in RH0. Renderer gate plus stress run.
- [x] RH3. Command surface. Retire `IRenderCommandContext` — the hot seam.
  Passes, `PrimitiveBatchRenderer`, `Text`, and UI draw submission record
  against the concrete command recorder. Verify by inspection and by the
  perf gate that no pass regressed; this is the one slice that also runs
  `tools\validate_perf.bat`.
- [x] RH4. Lifecycle and cleanup. Retire `IRenderDeviceLifecycle`, delete
  the final two interface headers, remove now-dead `virtual`/`override` and
  virtual destructors, and re-point `RuntimeRenderBackendView` and the
  `Run` wiring at concrete types. Update `Agentic/Tests/Dx12ArchUnitTests`
  expectations in the same commit (registered CPU umbrella target).
- [ ] RH5. Closure. Rerun the RH0 census from final source proving zero
  interface rows remain and per-consumer access narrowed or held equal.
  Comment-style audit of touched files, independent review, final gates
  three-run renderer repeat (danger-zone upload/frame allocator row applies
  if frame-owner seams moved).

RH0 evidence and the per-consumer access baseline are recorded in
[`../../Reports/2026-07-22/render-interface-retirement-rh0-census.md`](../../Reports/2026-07-22/render-interface-retirement-rh0-census.md).
The census corrected the registration evidence: production has one implementer
per interface, but capture has two test implementations and resource factory,
shader, and mesh each have one test double. All five test implementations are
deletion-bound to RH1/RH2; none is an exception.

## RH1 Evidence — Cold Surfaces (2026-07-22)

- Deleted `IRenderCaptureBackend`, `IRenderShaderDevelopment`, and
  `IRenderRayTracing`. Runtime now borrows `Dx12BackbufferCapture`,
  `Dx12ShaderDevelopment`, and `Dx12RaytracingOwner` directly; shared
  raytracing descriptions remain value-only in `RenderRaytracingTypes.h`.
- Removed both capture test implementations. Capture tests now exercise the
  pure bounded result-folding policy, so tests no longer manufacture an
  alternate renderer implementation.
- Moved the shader reload drain/publication transaction into
  `Dx12ShaderDevelopment`, the high-level DXR transaction into
  `Dx12RaytracingOwner`, and the finish/reopen transaction into
  `Dx12FrameOwner`. No replacement facade, callback pack, or backend pointer
  was introduced.
- Comment-style audit inspected all 35 touched source/tool files: 35 compliant,
  zero deferred. The retired-symbol search returned no rows.
- Resolved blockers without stopping the campaign: the focused build exposed
  one capture-test link dependency and one DXR member-name collision; the fast
  gate then exposed two implementation-format rows, one header-format row, and
  the missing filter-prefix rule for the new value header. Each was fixed and
  the owning gate rerun to PASS. Two shell-timeout interruptions produced no
  gate verdict; their exact MSBuild PIDs were stopped before the clean rerun.
- `tools\validate_project_filters.bat`: PASS in 2.6 s, 725 project items and
  725 filter items, zero errors.
- `tools\validate_fast.bat`: PASS in 23.0 s, 338/338 tests and 68,642/68,642
  assertions, zero warnings/errors.
- `tools\validate_dx12_renderer.bat`: PASS in 23.8 s, 43 shader stages fresh,
  zero InfoQueue errors, all three committed screenshots accepted.
- `tools\run_graphics_stress.bat 1`: PASS in 60.9 s, 12,027 frames and 330
  scene loads, empty stderr, crash-free PID-scoped timeout (PID 29132).

## RH2 Evidence — Resource Surfaces (2026-07-22)

- Deleted `IRenderResourceFactory`, `IRenderDiagnostics`, `IShader`, `IMesh`,
  and `IFramebuffer`. Static meshes, shaders, and framebuffers are concrete
  DX12 objects; shared framebuffer and diagnostics records live in the
  value-only `RenderResourceTypes.h` and `RenderDiagnosticsTypes.h` headers.
- Split the former resource-factory reach into `Dx12ResourceBuilder` for cold
  shader/static-mesh/framebuffer construction, `Dx12TextureOwner` for texture
  IO, and `Dx12GeometryOwner` for bounded dynamic/instanced geometry. Runtime,
  UI, text, collision visualization, and launcher laser contexts now name only
  the concrete owners they use.
- Published `Dx12Diagnostics` directly. It owns draw/visibility traces, timer
  and platform-profiler operations, capability queries, and read-only memory
  snapshots; `RenderBackendDX12` no longer inherits or forwards the diagnostic
  surface.
- Deleted `TestRenderResourceDoubles.h`. Terrain tests use physics-only value
  construction, while the main and standalone scene-parser CPU projects
  explicitly exclude native renderer object code through the same
  `SKULLBONEZ_RENDER_FREE_TESTS` lane.
- Comment-style audit inspected all 69 touched source/tool files: 69 compliant,
  zero deferred. The five retired-symbol search returned no rows; only the
  command and lifecycle interface rows expected for RH3/RH4 remain.
- Resolved blockers without stopping: the initial focused build forced the
  absent v143 toolset; concrete devirtualization then exposed consumer-owner
  mismatches and CPU-test link dependencies; the first fast gate found 13
  formatting rows; and the first CPU umbrella found the standalone scene-parser
  project missing the renderer-free definition. Each was corrected and its
  complete owning gate rerun to PASS.
- `tools\validate_project_filters.bat`: PASS in 2.8 s, 722 project items and
  722 filter items, zero errors.
- `tools\validate_fast.bat`: PASS in 86.9 s, 338/338 tests and 68,641/68,641
  assertions, Profile/Debug builds with zero warnings/errors.
- `tools\validate_all_cpu_tests.bat`: PASS in 73.9 s; unit/coverage,
  interaction-policy, scene-parser, and DX12 architecture lanes all passed.
- `tools\validate_dx12_renderer.bat`: PASS in 24.4 s, 43 shader stages fresh,
  zero InfoQueue errors, all three committed screenshots accepted.
- `tools\run_graphics_stress.bat 1`: PASS in 61.1 s, 12,694 frames and 348
  scene loads, zero upload flushes/drops, empty stderr, crash-free PID-scoped
  timeout (PID 38888).

## RH3 Evidence — Command Surface (2026-07-22)

- Deleted `IRenderCommandContext`. Shared raster, clear, instancing, and
  transient-triangle records now live in value-only `RenderCommandTypes.h`;
  that header grants no command capability.
- Split command authority across existing concrete owners instead of creating
  a union facade: `Dx12FrameOwner` owns viewport/clear, the graph-transient
  owner resolves graph resources and transitions, `Dx12TextureOwner` binds
  textures, and `Dx12GeometryOwner` records dynamic/instanced submission.
  Runtime, pass, UI, replay-overlay, text, primitive, terrain, and world
  extension contexts borrow only the owners they use.
- `RenderBackendDX12` no longer inherits the command interface. The geometry
  owner binds stable device/frame/pipeline/diagnostics owners at backend setup;
  no backend pointer, callback pack, hot virtual dispatch, or replacement
  command facade was introduced.
- Comment-style audit inspected all 55 extant touched source/tool files: 55
  compliant, zero deferred. Dependency-direction and replay-boundary review
  proofs returned no rows. No replay growth privilege appeared or expanded.
- Resolved blockers without stopping: the first focused build could not find
  MSBuild on `PATH`; the discovered VS 2022 toolchain then exposed four missing
  concrete-owner includes, a world-extension owner split, and a texture-owner
  name collision. The first project-filter gate found the new value header's
  missing prefix. The first performance run reported a transient measured
  regression after 88.5 s; an unchanged-source rerun passed every absolute and
  comparison lane, so no baseline or threshold moved.
- Focused Profile x64 build: PASS in 16.1 s with zero errors.
- `tools\validate_project_filters.bat`: clean final PASS in 2.6 s, 722 project
  items and 722 filter items, zero errors.
- `tools\validate_fast.bat`: PASS in 89.4 s, Profile/Debug builds and unit
  tests clean with zero warnings/errors.
- `tools\validate_dx12_renderer.bat`: PASS in 24.5 s, 43 shader stages fresh,
  zero InfoQueue errors, all three committed screenshots accepted.
- `tools\validate_perf.bat`: unchanged-source rerun PASS in 75.0 s; allocation
  guard, absolute budgets, and DX12/physics comparisons report no regression.
- `tools\run_graphics_stress.bat 1`: PASS in 60.9 s, 12,313 frames and 338
  scene loads, empty stderr, crash-free PID-scoped timeout (PID 42836).

## RH4 Evidence — Lifecycle And Cleanup (2026-07-22)

- Deleted `IRenderDeviceLifecycle`, the last render interface header.
  `RenderBackendDX12` is now a non-polymorphic startup/shutdown composition root
  and is not published through `RuntimeRenderBackendView`.
- Moved present, mutation drain, terminal release drain, and resize authority to
  `Dx12FrameOwner`. Runtime receives `Dx12FrameOwner` for those transactions and
  `Dx12RenderDevice` for extent/VSync/device state; scene, window, command,
  stress, and resource-release consumers no longer receive their union.
- Removed the final render `virtual`, `override`, and virtual-destructor
  declarations. The DX12 architecture result-contract assertions now target
  `Dx12FrameOwner` and remain in the registered mandatory CPU umbrella.
- Comment-style audit inspected all 25 extant touched source/tool files: 25
  compliant, zero deferred. Retired-interface, interface-class, concrete
  virtual/override declaration, dependency-direction, and downward-Replay
  proofs all returned no rows. No Replay growth privilege appeared or expanded.
- Resolved the final review finding without stopping: after the first green
  gate set, moved frame-lifecycle fatal paths still named the old aggregate
  backend owner. Their owner labels and explanatory comments were corrected,
  then the full renderer/runtime/stress gate set was rerun from the final tip.
- Focused Profile x64 build: PASS in 23.9 s with zero warnings/errors.
- `tools\validate_project_filters.bat`: PASS in 2.6 s, 721 project items and
  721 filter items, zero errors.
- `tools\validate_all_cpu_tests.bat`: PASS in 112.7 s; unit/coverage,
  interaction-policy, scene-parser, and DX12 architecture lanes all passed.
- `tools\validate_full.bat` final tip: PASS in 236.3 s; mandatory CPU umbrella,
  all five runtime processes, zero DX12 errors, accepted screenshots, and the
  44,401-line physics baseline byte-exact.
- `tools\validate_dx12_renderer.bat` final tip: PASS in 23.0 s, 43 shader
  stages fresh, zero InfoQueue errors, all three committed screenshots accepted.
- `tools\run_graphics_stress.bat 1` final tip: PASS in 60.9 s, 12,605 frames
  and 346 scene loads, zero upload flushes/drops, empty stderr, crash-free
  PID-scoped timeout (PID 46552).

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

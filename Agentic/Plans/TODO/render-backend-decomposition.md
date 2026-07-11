# Render Backend Decomposition

Date: 2026-07-10 (source reconciled)
Status: In progress — 4/8 remaining checklist items complete; completed facet
and render-graph decisions are historical evidence, not part of this count
Impact area: DX12 renderer, backend ownership, runtime render wiring
Owner: DX12 device/render layer

## Scope Decision

DX12 explicit helpers own live transition/UAV barrier emission. RenderGraph
owns pass/resource declarations, callback scheduling, and transient texture
lifetime; it is not being expanded into a barrier compiler. `IRenderBackend`
is deleted and narrow lifecycle/resource/command/diagnostics/capture/raytracing
facets are the current engine-facing boundary.

## Problem

`RenderBackendDX12` remains the aggregate concrete owner behind those facets.
Texture/upload/descriptor state, PSO cache state, DXR state, frame/device state,
readback, and profiling share one large class. Device/swap-chain/command-list
aliases have already been replaced with `Dx12RenderDevice` accessors, but
borrowed `m_factory`, `m_commandQueue`, and `m_commandAllocators[]` aliases
remain and must stay synchronized with the device owner.

Failure propagation is a prerequisite concern, not a side effect of splitting:
the current backend also ignores some `SbResult` and HRESULT outcomes. That work
is owned by `dx12-failure-propagation.md` and should land before owner moves.

## Checklist

### A. Concrete owner split

- [x] A1. Texture owner: texture creation, upload reservations, descriptors,
  SRV registration, mip generation, and texture-handle table.
- [x] A2. Pipeline owner: root signature/bytecode recipe, raster/depth/blend/RTV
  state, PSO cache, and draw-preparation state.
- [x] A3. DXR owner: device5/command-list capability, BLAS/TLAS/SBT, reflection
  resources, and DXR failure state. It must be a real owner, not a forwarding shim.
- [x] A4. Re-evaluate the remainder as the slim device/frame owner; continue
  only where a coherent independent owner remains.

### B. Resource capability design

- [ ] B1. Inventory each `IRenderResourceFactory` consumer by shader, mesh,
  framebuffer, texture, dynamic geometry, and instancing need. Keep one factory
  only if the matrix proves it is cohesive; otherwise expose value-based narrow
  capabilities without adding hot-path polymorphism.

### C. Device ownership and aliases

- [ ] C1. Remove or formally rebind the remaining factory/queue/allocator
  aliases. Acceptance: shutdown/partial-init/device recreation cannot leave any
  backend member pointing at released device-owner storage.

### D. Engine-facing cleanup

- [ ] D1. Remove direct concrete-backend calls where an existing narrow facet
  suffices; keep DX12 types out of engine-facing headers.
- [ ] D2. Verify capture/readback and platform-profiler marker paths after each
  split; register their CPU tests with the validation umbrella.

### A1-A2 evidence (2026-07-11)

- `Dx12TextureOwner` owns the texture handle table, persistent/bound SRV rows,
  null binding, mip root signature/PSO, creation/upload/mip path, and terminal
  release. Deleted texture and FBO rows are reclaimed before registry growth.
- `Dx12PipelineOwner` owns the ordinary root signature, active shader recipe,
  bounded PSO cache, fixed-function/target state, and draw dirty-state fast
  path. It receives only explicit device, command-list, recording, texture-
  binding, and descriptor dependencies per draw; it has no backend friendship
  or stored host pointer.
- Pipeline desired state resets to reusable defaults at both initialization and
  shutdown. The allocation-free architecture test covers every reset field.
- The independent review initially blocked reciprocal authority and incomplete
  reinitialization. Both findings were fixed; the final follow-up reported no
  remaining credible blocker.
- Validation: `validate_all_cpu_tests` passed all four CPU lanes in 29.849s;
  three consecutive `validate_dx12_renderer` runs passed with zero InfoQueue
  errors and matching screenshots (39.836s, 22.893s, 22.825s); and
  `run_graphics_stress.bat 1` completed crash-free in 60.865s.

### A3-A4 evidence (2026-07-12)

- `Dx12RaytracingOwner` owns optional Device5/command-list4 capability, the
  bounded fallback result, root signature and raytracing pipeline, reflection
  UAV/SRV resource identity, persistently mapped constants, fixed instance
  rows, BLAS/TLAS, and SBT. It has no backend pointer/reference, friendship,
  callback pack, or polymorphic hot-path boundary.
- Initialization reports whether BLAS commands were recorded. The backend
  remains the sole close/submit/fence authority, then calls the owner's
  completion phase only after the wait proves scratch memory is releasable.
  Dispatch reports raster-state invalidation as a value; the coordinator
  applies it to the texture and pipeline siblings.
- Reflection handle publication remains a coordinator boundary: the owner
  exposes only its SRV row, and `ShutdownDXR` unregisters any published texture
  handle before releasing the reflection resource.
- Remainder decision: no fourth owner was extracted in A4. The surviving core
  state is one fence-coupled device/frame lifetime: command epoch and submitted
  work, frame allocators, descriptor/upload arenas, backbuffers, deferred
  releases, and render-graph transient targets. Dynamic resource-factory
  registries are deliberately left for B1's consumer matrix; queue/allocator
  aliases for C1; capture/readback and profiler state for D2. Extracting any of
  those here would pre-empt their binding decisions rather than slim the core.
- Focused iteration evidence: `tools\validate_build.bat Profile` passed with
  zero warnings and zero errors. The DX12 architecture cases passed;
  `validate_dx12_renderer` passed in 29.175s with zero InfoQueue errors and
  matching screenshots; `run_graphics_stress.bat 1` completed crash-free in
  60.867s.

## Dependencies

1. `dx12-failure-propagation.md` D0-D3 before A1-A3.
2. `validation-gate-integrity.md` V1 before new DX12 CPU tests.
3. `runtime-shell-decomposition.md` extraction 5 consumes the stable facet/
   owner result; avoid moving the same render hook twice.
4. Binding order is fixed: A2 establishes the concrete pipeline/root-signature
   owner before `shader-pipeline-modernization.md` P1-P3 changes its bytecode
   and binding contract. `shadow-edge-quality.md` S1 then extends the surviving
   consolidated contract. Do not create a temporary second owner in either
   dependent plan.

## Acceptance

- [ ] Texture, pipeline, DXR, and device/frame state have named concrete owners.
- [ ] No borrowed alias can dangle across partial init, shutdown, or recreation.
- [ ] No new bridge/adapter/callback shim appears on render hot paths.
- [ ] All state-changing failures propagate according to the DX12 failure plan.
- [ ] DX12 validation is zero and screenshots match on every slice.

## Validation

| Slice | Gate |
|---|---|
| CPU owner/state changes | CPU umbrella + `tools\validate_dx12_arch_tests.bat` |
| Any backend owner split | previous tests + `tools\validate_dx12_renderer.bat` |
| Upload/frame allocator adjacency | renderer gate three consecutive runs |
| Profiling marker changes | renderer gate + `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` |
| Device lifecycle | DX12 failure probes + renderer gate + full gate |

The renderer gate alone is not CPU architecture-test evidence until
`validation-gate-integrity.md` V2 integrates the umbrella into the broad gate.

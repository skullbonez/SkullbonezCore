# DX12 Backend State Census And Proposed Owner Map

Date: 2026-07-18
Branch: `nightrunner-17th-july`
Source inspected: `cffce392e9511756e04b3335ae19b2590ab0d6eb`
Plan: `Agentic/Plans/TODO/dx12-backend-ownership-decomposition.md`
Status: D0 complete; owner map, names, branch, and frame-sequencing decision
ratified for D1-D7 implementation.

## Current Surface

`RenderBackendDX12.h` is 1,095 source lines plus its final newline. The backend
class begins at line 639 and implements the seven retained consumer interfaces
without changing their declarations. Its private section directly declares six
existing concrete owners, one GPU-timer record, descriptor heaps/allocators,
capture retention, graph-transient pools, diagnostics counters, device policy,
and command defaults. This census covers every private constant, data member,
inline access helper, and declared helper in `RenderBackendDX12:648-834`.

## Complete Private-State Census

| Current private member(s) | Current responsibility | Target owner |
|---|---|---|
| `FRAME_COUNT` | Compile-time in-flight frame count | Keep only as `Dx12FrameOwner::FRAME_COUNT`; delete the backend alias after dependent arrays move. |
| `MAX_RTV_DESCRIPTORS`, `MAX_DSV_DESCRIPTORS`, `MAX_STATIC_SRVS`, `MAX_TRANSIENT_SRVS` | Descriptor heap capacities | `Dx12DescriptorHeaps`. |
| `UPLOAD_BUFFER_SIZE` | Per-frame upload capacity | `Dx12FrameOwner`; it owns upload/fence reuse policy. |
| `TIMER_HEAP_MARKERS`, `TIMER_HEAP_SIZE` | Timestamp query capacity | `Dx12Diagnostics`. |
| `m_textureOwner` | Texture registry, mip pipeline, binding rows | Keep existing `Dx12TextureOwner` as a composed owner. Descriptor operations become borrowed `Dx12DescriptorHeaps` capabilities. |
| `m_pipelineOwner` | Raster root signature, PSO cache, target/draw state, and currently shader reload rows | Keep existing `Dx12PipelineOwner`; D5 moves reload registry/staging authority to `Dx12ShaderDevelopment`. |
| `m_geometryOwner` | Dynamic/instanced geometry and fixed overlay shader rows | Keep existing `Dx12GeometryOwner`; D5 lends it shader candidates during cold reload. |
| `m_raytracingOwner` | DXR capability and resource lifetime | Keep existing `Dx12RaytracingOwner`. |
| `m_reflectionTextureHandle` | Engine texture handle published for the DXR reflection SRV | Move into `Dx12RaytracingOwner`; its lifetime is created, exposed, and cleared with DXR. |
| `m_gpuTimers` | Query heap, readback, results, validity, frequency, and covering fence | `Dx12Diagnostics`. |
| `m_renderDevice` | Factory/device/queue/swap chain/command list/fence lifecycle | Keep existing `Dx12RenderDevice` as a composed owner. |
| `m_frameOwner` | Recording epoch, submission, allocator/upload reuse, retirement, back-buffer access, profiler stack | Keep existing `Dx12FrameOwner` as a composed owner; it borrows descriptors and diagnostics rather than owning their state. |
| `m_recreationGeneration` | Successful resize/device-publication generation | `Dx12RenderDevice`; generation advances only after a fully published resize. |
| `m_rtvHeap`, `m_dsvHeap`, `m_srvHeap`, `m_srvStagingHeap` | CPU output heaps plus persistent/transient shader-visible rows | `Dx12DescriptorHeaps`. |
| `m_rtvDescSize`, `m_dsvDescSize`, `m_srvDescSize` | Device descriptor increments | `Dx12DescriptorHeaps`. |
| `m_backBufferRTVs`, `m_mainDSV` | Published output descriptor handles | `Dx12DescriptorHeaps`; handles are invalidated and republished with the heaps/resources. |
| `m_rtvDescriptors`, `m_dsvDescriptors` | Bounded CPU-only row allocation | `Dx12DescriptorHeaps`. |
| `m_depthStencil` | Main depth resource paired with swap-chain extent | `Dx12RenderDevice`; the descriptor owner owns only its DSV identity, not the resource. |
| `m_uncertainReadbackResources`, `m_uncertainReadbackResourceCount` | Bounded COM quarantine after an unprovable screenshot submission | `Dx12BackbufferCapture`. |
| `m_width`, `m_height` | Published device/swap-chain extent | `Dx12RenderDevice`. |
| `m_isVsyncEnabled`, `m_allowTearing` | Present policy/capability | `Dx12RenderDevice`. |
| `m_frameDrawCallCount`, `m_frameDrawCallHighWater` | Per-frame and run-high-water diagnostics | `Dx12Diagnostics`. |
| `m_frameVisibilityStats`, `m_drawCallTrace` | Visibility and scoped draw diagnostics | `Dx12Diagnostics`. |
| `m_clearColor`, `m_clearDepth` | Current raster clear intent | `Dx12PipelineOwner`; these values apply to its current output targets. |
| `m_graphTransientResources`, `m_graphTransientBindings`, `m_graphTransientStats` | Physical graph pool, logical-to-physical mapping, and materialization report | `Dx12GraphTransientPool`. |
| `m_graphRenderTargetActive`, `m_activeGraphRenderTarget` | Active graph-target transaction | `Dx12GraphTransientPool`. |
| `m_savedGraphRTV`, `m_savedGraphDSV`, `m_savedGraphRTVFormat` | Saved raster output restored after a graph-target transaction | `Dx12GraphTransientPool`; it borrows the pipeline owner to save/restore rather than retaining heap pointers. |

The backend composition root keeps concrete owner objects as direct members;
it keeps no raw heap, resource, allocator, counter, pool, readback, reload, or
diagnostic state of its own after D6.

## Private Helper Attribution

| Current helper(s) | Target |
|---|---|
| `Device`, `SwapChain`, `CommandList`, `ReportDeviceLost` | Delete thin backend accessors where callers can receive a typed device/frame capability; device-loss reporting moves to `Dx12RenderDevice` plus diagnostics value output. |
| `WaitForGpu`, `EnsureCommandListOpen`, `SubmitClosedCommandList`, `AssignDeferredResourceReleaseFence`, `ReleaseCompletedDeferredResources`, profiler suspend/restore/assert helpers | Existing `Dx12FrameOwner`. Backend retains only the top-level call order in `Init`/`Present`/`Finish`/`Shutdown`. |
| `ConfigureFaultInjection`, `WriteFaultInjectionProbeReport`, `TryConsumeGpuTimerReadback`, `ReportArchitectureStats` | `Dx12Diagnostics`. Operational fault checks cross a narrow capability into `Dx12FrameOwner`; no diagnostics owner backpointer is retained. |
| `CreateDepthStencil`, `CreateDepthStencilResource` | `Dx12RenderDevice`; creation follows device/swap-chain extent. |
| `PublishDepthStencilView`, `AllocateTransientSRV`, `AllocateTransientSRVRange`, `GetSRVGpuHandle`, `GetRTVHandle`, `GetDSVHandle` | `Dx12DescriptorHeaps`. |
| `TransitionBackbuffer` | Existing `Dx12FrameOwner`; it owns the tracked back-buffer access epoch. |
| `ClearBoundTextureSlotsForSrv` | Existing `Dx12TextureOwner`; descriptor retirement passes the released row as a value. |
| `FindGraphTransientSlot` (x2), `ReleaseGraphTransientResources` | `Dx12GraphTransientPool`. |
| `CheckDXRSupport` | Existing `Dx12RaytracingOwner::ProbeCapability`; delete backend helper if it only delegates. |

## Ratified Owner Map

1. Reuse feature branch `nightrunner-17th-july`.
2. Keep frame begin/close/submit/present sequencing directly on
   `RenderBackendDX12`. `Dx12FrameOwner` already owns the complete recording and
   fence epoch; adding another frame sequencer would duplicate that boundary.
   The backend remains the engine-interface composition root and decides only
   top-level operation order.
3. Use these ratified concrete owner names:

   - `Dx12DescriptorHeaps`
   - `Dx12BackbufferCapture`
   - `Dx12GraphTransientPool`
   - `Dx12Diagnostics`
   - `Dx12ShaderDevelopment`

   These are domain nouns, not generic managers or contexts. Existing
   `Dx12RenderDevice`, `Dx12FrameOwner`, `Dx12TextureOwner`,
   `Dx12PipelineOwner`, `Dx12GeometryOwner`, and `Dx12RaytracingOwner` retain
   their cohesive responsibilities subject to the state moves above.

## Fence And Lifetime Invariants By Cluster

| Cluster | Invariant that moves with the state |
|---|---|
| Descriptors | Static SRV rows are not returned to the free stack until the deferred-release covering fence completes. Transient rows reset only after the owning frame fence completes. Staging and shader-visible static rows retain identical indices. RTV/DSV row reuse follows the retired resource's covering fence. Heap publication is all-or-nothing during device initialization. |
| Capture/readback | Pixel mapping occurs only after the submitted copy's fence completes. A failed/uncertain fence wait detaches the readback COM reference into the fixed quarantine; only a proven terminal queue drain may release it. Capacity exhaustion is fatal with owner diagnostics, never dynamic growth. |
| Graph transients | Logical handles resolve only through the latest compile's binding table. A physical slot is reused only for compiler-proven non-overlap and descriptor-compatible shape. Descriptor rows and resource retirement use the descriptor/frame owners' covering-fence path. Begin/end target transactions are balanced and restore the exact saved RTV/DSV/format. |
| Diagnostics/timers | Timestamp results become valid only after the resolve fence completes; polling cannot map an incomplete readback. Draw/visibility/trace rows reset together at frame start. Fault injection remains cold/diagnostic policy and may fail command recording only through the frame owner's sticky result lane. |
| Shader development | Reload is cold-path only. Candidate shaders, root signatures, mip PSOs, and overlay PSOs are staged completely before publication; any failure leaves all live objects untouched. Registered shader rows unregister before shutdown. No owner stores a backend pointer or gains per-frame policy. |
| Device/frame composition | Device objects outlive every owner that borrows them and shut down after a terminal drain. Allocators, uploads, back buffers, profiler stack, and retirements are reused only after their frame/covering fence. `FRAME_COUNT` remains two. Present sequencing stays at the composition root while epoch state stays in `Dx12FrameOwner`. |

## Evidence And Gate

- CodeGraph was current and supplied the complete class source at the inspected
  tip; `Dx12FrameOwner`, `Dx12RenderDevice`, and graph-transient records were
  reconciled to avoid inventing a duplicate owner.
- The seven inherited interface declarations remain out of scope and unchanged.
- D0 is documentation-only; no repository validation is required.
- The user's instruction to complete every pending MASTER plan authorizes the
  recorded branch and owner decisions. D1 may proceed against this map; later
  tasks must reopen D0 if source evidence contradicts an attribution or fence
  invariant rather than adding a compatibility owner.

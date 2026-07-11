# DX12 Failure-Surface Inventory

Date: 2026-07-10

Plan phase: `Agentic/Plans/TODO/dx12-failure-propagation.md` D0

Scope: all 26 files (16 `.cpp`, 10 `.h`) under
`SkullbonezSource/Rendering/DX12`

## Outcome

The source inventory is complete and has zero unclassified result-bearing call
sites.

| Surface | Call sites | Already captured or checked | Discarded / unsafe | Classification |
|---|---:|---:|---:|---|
| HRESULT-returning DX/DXGI/D3D/COM calls | 101 | 70 | 31 | Lane R; no HRESULT site is a logical Lane F invariant |
| `SbResult` construction/invocation expressions | 115 | 105 | 10 | Lane R; the ten discards are all `WaitForGpu()` |
| API calls with no synchronous result by contract | 44 | 44 | 0 | Infallible-by-contract at the call boundary; asynchronous device loss still surfaces later |

The 115 `SbResult` expressions comprise 63 direct value constructions (29
`Success`, 34 `Failure`) and 52 calls to an `SbResult`-returning function. The
folder defines 24 `SbResult` producers. Every producer/caller flow is classified
below; declarations in headers are not double-counted as calls.

The earlier plan estimate of eight ignored `Close()` calls covered backend
operations only. The complete folder contains nine: those eight plus the
initial close in `Dx12RenderDevice::Init()`.

## Classification Rules

- **Checked / R**: the result is inspected or converted to a checked
  `SbResult`, and failure returns, disables an optional feature, or reports a
  bounded fallback.
- **Lane R fix**: the environment/driver/device operation can fail, but the
  result is discarded or swallowed. The current operation must stop before
  changing state, submitting, reusing memory, or releasing resources.
- **Explicit best-effort R**: failure does not invalidate renderer state, but a
  future `[[nodiscard]]` pass must use a named discard with a reason or retain a
  diagnostic. A bare discarded HRESULT is not accepted.
- **Lane F**: reserved for engine-owned impossible preconditions. There are no
  HRESULT or `SbResult` rows in this inventory that should become Lane F.
  Existing null-owner, capacity, and index invariants around these calls remain
  `SB_FATAL` boundaries.
- **Infallible-by-contract**: the API has no synchronous result. This does not
  claim the GPU cannot fail asynchronously.

## Required Fault-Test Catalogue

| Test id | Required proof |
|---|---|
| `FT-CMD` | Inject allocator reset, list reset, and list close failures independently. No later command is recorded or submitted, and `m_commandListOpen` changes only after success. |
| `FT-WAIT` | Inject queue signal, fence event, and OS wait failures. No allocator/upload/descriptor reuse and no COM resource release may rely on the failed wait. |
| `FT-INIT` | Fail each device/backend initialization operation. The owner returns one Lane R error and rollback leaves no live aliases or false-success feature state. |
| `FT-PRESENT` | Inject `Present`, shutdown-present, and `ResizeBuffers` failures. The frame/resize aborts and device-loss diagnostics retain the failing HRESULT. |
| `FT-MAP` | Inject upload, readback, DXR constants, SBT, and TLAS `Map` failures. No null-pointer `memcpy` occurs and the owning operation returns/disables the feature safely. |
| `FT-QUERY` | Inject timer-frequency, DRED, InfoQueue-message, adapter-memory, and shader-reflection query failures. Optional telemetry is disabled or explicitly skipped without using uninitialized output. |
| `FT-RESOURCE` | Inject resource, heap, root-signature, PSO, framebuffer, mesh, BLAS/TLAS/SBT, and graph-transient creation failures. No dependent draw/dispatch is recorded and partial resources roll back. |
| `FT-OPTIONAL` | Fail naming, debug-layer/filter, tearing, DXR-capability, adapter-cast, window-association, and fullscreen-exit calls. The renderer either keeps a guarded fallback or records an explicit best-effort discard. |
| `FT-SHADER` | Fail compile, root-signature serialization, reflection creation, and each reflection descriptor query. No zero/uninitialized descriptor is consumed. |

## HRESULT Ledger

Every HRESULT call site appears in exactly one row. Counts in this table sum to
101; the `Current handling` column sums to 70 captured/checked and 31 discarded.

| Id | File:line(s) | Count | Operation and owner/caller | Current handling | Required classification, fix, and test |
|---|---|---:|---|---|---|
| H01 | `BLASDX12.cpp:137,160` | 2 | `CreateCommittedResource`; `BLAS::Build` scratch/result buffers | Checked; returns `SbResult::Failure` | Checked / R. Keep rollback and cover both allocations with `FT-RESOURCE`. |
| H02 | `FramebufferDX12.cpp:110,152` | 2 | `CreateCommittedResource`; `FramebufferDX12::Create` color/depth | Checked; logs and returns `false`, releasing color on depth failure | Checked / R. `FT-RESOURCE` must prove the factory returns no usable framebuffer. |
| H03 | `MeshDX12.cpp:88` | 1 | `CreateCommittedResource`; `MeshDX12::Create` vertex buffer | Checked; logs and returns `false` | Checked / R. Cover null factory result with `FT-RESOURCE`. |
| H04 | `ShaderDX12.cpp:127,158,220` | 3 | two `D3DCompile` calls and `D3DReflect`; `ShaderDX12::Compile/ReflectCB` | Checked; logs and returns `false` | Checked / R. Keep the shader factory from exposing partial bytecode; `FT-SHADER`. |
| H05 | `ShaderDX12.cpp:238,244,256,267` | 4 | reflection `GetDesc`, `GetResourceBindingDesc`, constant-buffer `GetDesc`, variable `GetDesc`; `ReflectCB` | **Discarded**; output is consumed even if the call failed | Lane R fix. Stop reflection and return `false` on each failure; `FT-SHADER`. |
| H06 | `RenderBackendDX12.Pipeline.cpp:355` | 1 | `CreateGraphicsPipelineState`; `CreatePSO` | Checked; logs, releases, returns null | Checked / R. Prove no draw binds a missing PSO with `FT-RESOURCE`. |
| H07 | `RenderBackendDX12.DynamicGeometry.cpp:184,480` | 2 | grid-line PSO and instanced static buffer creation | Checked; logs, releases partial object, returns/falls back | Checked / R. Cover both warmup and mesh creation with `FT-RESOURCE`. |
| H08 | `RenderBackendDX12.Textures.cpp:109,196,204,223,485` | 5 | `D3DCompile`, root-signature serialization, root signature, compute PSO, texture resource; `InitGenMipsPipeline/CreateTexture2D` | Checked; init operations return `SbResult`, texture creation returns handle 0 | Checked / R. `FT-SHADER` for compile/serialize and `FT-RESOURCE` for PSO/texture. |
| H09 | `RenderDeviceDX12.cpp:129` | 1 | `D3D12GetDebugInterface`; `EnableDx12DeviceRemovedDiagnostics` | Checked as optional; no DRED settings when unavailable | Checked optional R. `FT-OPTIONAL`. |
| H10 | `RenderDeviceDX12.cpp:151` | 1 | `ID3D12Object::SetName`; `NameDx12Object` | **Discarded** | Explicit best-effort R. Use a named discard reason; naming must not fail resource creation. `FT-OPTIONAL`. |
| H11 | `RenderDeviceDX12.cpp:214,258` | 2 | queue `Signal`, fence `SetEventOnCompletion`; `Dx12FenceTimeline` | Wrapped and checked as `SbResult` | Checked / R. Preserve output-value update only after signal success; `FT-WAIT`. |
| H12 | `RenderDeviceDX12.cpp:832,845` | 2 | upload-buffer `CreateCommittedResource/Map`; `Dx12FrameUploadSystem::Init` | Checked; calls `Shutdown()` and returns `false` | Checked / R internally, but backend caller ignores the returned bool (see init matrix). `FT-INIT` and `FT-MAP`. |
| H13 | `RenderDeviceDX12.cpp:993,1041` | 2 | readback-buffer creation and `Map`; `Dx12ReadbackBuffer` | Checked; returns `false` or null | Checked / R. `FT-RESOURCE` and `FT-MAP`. |
| H14 | `RenderDeviceDX12.cpp:1104,1125,1127,1151,1153,1154,1160` | 7 | debug interface, factory/info-queue `QueryInterface`, tearing query, severity/filter setup; `Dx12RenderDevice::Init` | Four capability/interface results checked; two `SetBreakOnSeverity` and one `PushStorageFilter` **discarded** | Capability calls are checked optional R. Give the three debug-config calls named best-effort handling; `FT-OPTIONAL`. |
| H15 | `RenderDeviceDX12.cpp:1115,1141,1169,1189,1195,1212,1221,1234` | 8 | factory, device, queue, swap chain, swap-chain interface, allocators, command list, fence; `Dx12RenderDevice::Init` | All wrapped, checked, and returned through rollback | Checked / R. Inject each operation and verify rollback with `FT-INIT`. |
| H16 | `RenderDeviceDX12.cpp:1203,1232` | 2 | `MakeWindowAssociation`; initial command-list `Close`; `Dx12RenderDevice::Init` | **Both discarded**; init still succeeds | Window association is explicit best-effort R. Initial close is Lane R and must fail init without marking the list closed; `FT-OPTIONAL`, `FT-CMD`, `FT-INIT`. |
| H17 | `RenderDeviceDX12.cpp:1282` | 1 | `SetFullscreenState(FALSE)`; `Dx12RenderDevice::Shutdown` | **Discarded**, then swap chain is released | Explicit best-effort R with retained diagnostic/reason; `FT-OPTIONAL`. |
| H18 | `RenderBackendDX12.cpp:236,247,253,262,264` | 5 | adapter enumeration/description/cast and local/non-local memory queries; `GetRenderMemoryStats` | Enumeration/description/memory results checked; `ComPtr::As` **discarded** | Optional R. Check the cast or use a named telemetry-only discard; `FT-QUERY`, `FT-OPTIONAL`. |
| H19 | `RenderBackendDX12.cpp:473,478` | 2 | allocator `Reset`, command-list `Reset`; `EnsureCommandListOpen` | **Discarded**; descriptors/root signature bind and `m_commandListOpen=true` follow | Lane R fix. Return `SbResult`; stop after the first failure; state flips only after both succeed. `FT-CMD`. |
| H20 | `RenderBackendDX12.cpp:608,615,616` | 3 | list `Close`, allocator `Reset`, list `Reset`; `FlushUploadBuffer` | **Discarded**; submits after close and reuses after wait | Lane R fix. Abort before submit/reuse on any failure; `FT-CMD`, `FT-WAIT`. |
| H21 | `RenderBackendDX12.cpp:862` | 1 | graph-transient `CreateCommittedResource`; `MaterializeGraphTransientResources` | Checked; stats/log report failure and remove partial pool row | Checked / R. `FT-RESOURCE`. |
| H22 | `RenderBackendDX12.cpp:1101,1121,1125,1126` | 4 | removed reason, DRED interface, breadcrumb/page-fault queries; `ReportDeviceLost` | Results captured; optional interface checked; HRESULTs retained in report | Checked diagnostic R. `FT-QUERY` must prove failed DRED output is not dereferenced as valid. |
| H23 | `RenderBackendDX12.cpp:1311,1329,1351,1370,1410,1467,1493` | 7 | four descriptor heaps, swap-chain buffer, optional query heap, timestamp frequency; `RenderBackendDX12::Init` | First six checked/guarded; `GetTimestampFrequency` **discarded** and timer remains enabled | Lane R fix for frequency: disable/release timer resources or return startup failure. `FT-INIT`, `FT-QUERY`. |
| H24 | `RenderBackendDX12.cpp:1602,1617,1664` | 3 | root serialization/root creation/depth resource; `CreateRootSignature/CreateDepthStencil` | Checked and converted to `SbResult` | Checked / R. `FT-SHADER`, `FT-RESOURCE`. |
| H25 | `RenderBackendDX12.cpp:1728,1744,1779,1788,1792` | 5 | list close, shutdown present, InfoQueue interface/messages; `Shutdown` | Interface checked; close, present, and two `GetMessage` calls **discarded** | Close/present are Lane R and must prevent unsafe submit/release assumptions. Message reads need checked or named diagnostic discard. `FT-CMD`, `FT-WAIT`, `FT-PRESENT`, `FT-QUERY`. |
| H26 | `RenderBackendDX12.cpp:1970,1984` | 2 | list `Close`, swap-chain `Present`; `Present` | Close **discarded**, then submitted; DXGI present checked and device-loss reported | Make close Lane R and abort before queue/present. Existing present handling stays checked. `FT-CMD`, `FT-PRESENT`. |
| H27 | `RenderBackendDX12.cpp:2079,2106` | 2 | list `Close`; `Finish/FlushGPU` | **Discarded**, then submitted | Lane R fix. Do not submit, wait, or reopen after close failure; `FT-CMD`. |
| H28 | `RenderBackendDX12.cpp:2140,2164` | 2 | `ResizeBuffers/GetBuffer`; `Resize` | Checked and returned as `SbResult` | Checked / R, but the preceding ignored wait makes release unsafe. `FT-PRESENT`, `FT-WAIT`. |
| H29 | `RenderBackendDX12.DXR.cpp:93,104` | 2 | DXR feature query/device5 interface; `CheckDXRSupport` | Checked optional; leaves DXR disabled | Checked optional R. `FT-OPTIONAL`. |
| H30 | `RenderBackendDX12.DXR.cpp:186,194,291,303,336` | 5 | RT root serialization/root, state object/interface, reflection UAV resource | Checked and returned as `SbResult` | Checked / R. `FT-SHADER`, `FT-RESOURCE`. |
| H31 | `RenderBackendDX12.DXR.cpp:409,458,468,500,511` | 5 | command-list4 interface, constants resource/map, two list closes; `InitDXR` | Interface/resource checked; `Map` and both `Close` calls **discarded** | Map and closes are Lane R. Disable DXR and do not write/submit/release on failure; `FT-MAP`, `FT-CMD`, `FT-WAIT`. |
| H32 | `RenderBackendDX12.Readback.cpp:128` | 1 | list `Close`; `CaptureBackbuffer` | **Discarded**, then submitted; later wait is checked | Lane R fix. Abort capture before submit and retain backbuffer state; `FT-CMD`. |
| H33 | `SBTDX12.cpp:148,165` | 2 | SBT resource creation/map; `SBT::Build` | Resource checked; `Map` **discarded** before `memset/memcpy` | Lane R fix. Return failure and release buffer on map failure; `FT-MAP`, `FT-RESOURCE`. |
| H34 | `TLASDX12.cpp:97,131,148,182` | 4 | TLAS instance/scratch/result creation and per-frame instance `Map`; `TLAS::Init/Build` | Creations checked; `Map` **discarded** before `memcpy` | Creation stays checked. Make build map failure abort dispatch/rebuild without memory access; `FT-MAP`, `FT-RESOURCE`. |
| **Total** |  | **101** |  | **70 checked/captured; 31 discarded** | **Zero unclassified** |

## Explicit Command, Wait, Map, Present, And Query Lists

### Ignored `Close()` HRESULTs: 9

| File:line | Caller | Unsafe continuation |
|---|---|---|
| `RenderDeviceDX12.cpp:1232` | `Dx12RenderDevice::Init` | Init reports success and later assumes the new list is closed. |
| `RenderBackendDX12.cpp:608` | `FlushUploadBuffer` | Submits the list, waits, and resets allocator/list. |
| `RenderBackendDX12.cpp:1728` | `Shutdown` | Submits and proceeds toward resource release. |
| `RenderBackendDX12.cpp:1970` | `Present` | Submits and presents. |
| `RenderBackendDX12.cpp:2079` | `Finish` | Submits, waits, consumes timers, and reopens. |
| `RenderBackendDX12.cpp:2106` | `FlushGPU` | Submits, waits, and reopens. |
| `RenderBackendDX12.DXR.cpp:500` | `InitDXR` failure cleanup | Submits partial BLAS work, waits, releases scratch/result dependencies. |
| `RenderBackendDX12.DXR.cpp:511` | `InitDXR` success path | Submits BLAS work, waits, releases scratch buffers. |
| `RenderBackendDX12.Readback.cpp:128` | `CaptureBackbuffer` | Submits copy work and waits. |

### Ignored command allocator/list `Reset()` HRESULTs: 4

| File:line | Caller | Unsafe continuation |
|---|---|---|
| `RenderBackendDX12.cpp:473` | `EnsureCommandListOpen` | Resets the list even if allocator reset failed. |
| `RenderBackendDX12.cpp:478` | `EnsureCommandListOpen` | Binds state and sets `m_commandListOpen=true`. |
| `RenderBackendDX12.cpp:615` | `FlushUploadBuffer` | Resets the list and reuses transient storage. |
| `RenderBackendDX12.cpp:616` | `FlushUploadBuffer` | Marks the list open and resumes recording. |

### `WaitForGpu()` `SbResult` calls: 11 total, 10 ignored

| File:line | Caller | Handling / hazard |
|---|---|---|
| `RenderBackendDX12.cpp:612` | `FlushUploadBuffer` | **Ignored**; allocator/list/upload storage is reused. |
| `RenderBackendDX12.cpp:1735` | `Shutdown` | **Ignored**; GPU-owned resources are released. |
| `RenderBackendDX12.cpp:1745` | `Shutdown` | **Ignored** after shutdown present; swap-chain/device teardown follows. |
| `RenderBackendDX12.cpp:2071` | `Finish` early path | **Ignored**; timer consumption follows. |
| `RenderBackendDX12.cpp:2084` | `Finish` normal path | **Ignored**; timer consumption and reopen follow. |
| `RenderBackendDX12.cpp:2099` | `FlushGPU` early path | **Ignored**. |
| `RenderBackendDX12.cpp:2111` | `FlushGPU` normal path | **Ignored**; command list is reopened. |
| `RenderBackendDX12.cpp:2128` | `Resize` | **Ignored**; back buffers/depth are released and resized. |
| `RenderBackendDX12.DXR.cpp:504` | `InitDXR` failure cleanup | **Ignored**; terrain BLAS temporary resources are released. |
| `RenderBackendDX12.DXR.cpp:515` | `InitDXR` success path | **Ignored**; both BLAS scratch buffers are released. |
| `RenderBackendDX12.Readback.cpp:132` | `CaptureBackbuffer` | Checked and returned before map/read. |

### `Map()` HRESULTs: 5

| File:line | Caller | Handling |
|---|---|---|
| `RenderDeviceDX12.cpp:845` | `Dx12FrameUploadSystem::Init` | Checked; rollback and `false`. |
| `RenderDeviceDX12.cpp:1041` | `Dx12ReadbackBuffer::MapRead` | Checked; null returned. |
| `RenderBackendDX12.DXR.cpp:468` | `InitDXR` constants | **Ignored**; mapped pointer is used later. |
| `SBTDX12.cpp:165` | `SBT::Build` | **Ignored**; immediate `memset/memcpy`. |
| `TLASDX12.cpp:182` | `TLAS::Build` | **Ignored**; immediate per-frame `memcpy`. |

### Present/resize HRESULTs: 3

- `RenderBackendDX12.cpp:1984`: normal `Present`, checked and device-loss
  reported.
- `RenderBackendDX12.cpp:1744`: shutdown present, ignored before the second wait
  and teardown.
- `RenderBackendDX12.cpp:2140`: `ResizeBuffers`, checked, but reached only after
  an ignored `WaitForGpu()` and release of existing buffers.

### Query/diagnostic HRESULTs

- Checked/captured: adapter enumeration/description and two memory queries
  (`RenderBackendDX12.cpp:236,247,262,264`), device removed reason and DRED
  interface/output (`1101,1121,1125,1126`), DXR feature/interface queries
  (`RenderBackendDX12.DXR.cpp:93,104,303,409`), tearing capability
  (`RenderDeviceDX12.cpp:1125,1127`), and optional query-heap creation
  (`RenderBackendDX12.cpp:1467`).
- Discarded: adapter `ComPtr::As` (`RenderBackendDX12.cpp:253`), timestamp
  frequency (`1493`), InfoQueue message reads (`1788,1792`), and shader
  reflection descriptor queries (`ShaderDX12.cpp:238,244,256,267`).

## `SbResult` Producer And Caller Ledger

The table groups all 24 producers and all 52 producer-call expressions. The 63
direct `Success`/`Failure` constructions are the terminal returns inside these
same producers.

| Producer(s) | File:line(s) | Calls in this folder | Current handling and required action |
|---|---|---|---|
| `Dx12BackendInitResult` | `RenderBackendDX12.cpp:132` | `1311,1329,1351,1370,1410` (5) | Every result assigned, checked, and returned. Checked / R. |
| `Dx12BackendOperationResult` | `RenderBackendDX12.cpp:147` | `1672,1994,2150,2164` (4) | Immediately returned or assigned/checked. Checked / R. |
| `Dx12TextureStartupResult` | `RenderBackendDX12.Textures.cpp:65` | `195,204,223` (3) | Assigned/checked and returned. Checked / R. |
| `Dx12StartupResult` | `RenderDeviceDX12.cpp:63` | `1115,1141,1169,1188,1195,1211,1221,1234` (8) | Assigned/checked and returned under rollback. Checked / R. |
| `Dx12RuntimeResult` | `RenderDeviceDX12.cpp:79` | `214,258` (2) | Assigned/checked in fence timeline. Checked / R. |
| `Dx12FenceTimeline::Signal` | `RenderDeviceDX12.cpp:199` | `RenderDeviceDX12.cpp:232`; `RenderBackendDX12.cpp:2006` (2) | Both assigned/checked. Out value advances only after success. |
| `Dx12FenceTimeline::SignalAndWait` | `RenderDeviceDX12.cpp:225` | `RenderBackendDX12.cpp:303` (1) | Returned through `WaitForGpu`. Checked / R. |
| `Dx12FenceTimeline::WaitForValue` | `RenderDeviceDX12.cpp:241` | `RenderDeviceDX12.cpp:237`; `RenderBackendDX12.cpp:459,2043`; `RenderBackendDX12.Profiler.cpp:75` (4) | The direct return and `Present` wait propagate. `EnsureCommandListOpen` logs then returns `void`, allowing its caller to continue; make that path Lane R. Timer-only swallowing is acceptable only after primary frame failure propagation is established and explicitly documented. |
| `RenderBackendDX12::WaitForGpu` | `RenderBackendDX12.cpp:291` | 11 calls listed above | One checked, ten discarded. All ten require D3 propagation or explicit harmless handling for uninitialised early paths. |
| `Dx12RenderDevice::Init` | `RenderDeviceDX12.cpp:1078` | `RenderBackendDX12.cpp:1250` (1) | Checked and returned. |
| `RenderBackendDX12::Init` | `RenderBackendDX12.cpp:1239` | No in-folder caller | Producer is classified; repository-wide caller audit belongs to D1. It incorrectly ignores `m_uploadSystem.Init` (bool), noted below. |
| `CreateRootSignature`, `CreateDepthStencil`, `InitGenMipsPipeline` | `RenderBackendDX12.cpp:1508,1642`; `RenderBackendDX12.Textures.cpp:83` | root `1442` (1), depth `1425,2174` (2), mips `1447` (1) | All four calls assigned/checked. |
| `CreateRTRootSignature`, `CreateRTPipeline`, `CreateReflectionUAV` | `RenderBackendDX12.DXR.cpp:113,206,311` | `423,428,436` (3) | Assigned/checked; failure calls `ShutdownDXR` and returns. |
| `BLAS::Build` | `BLASDX12.cpp:70` | `RenderBackendDX12.DXR.cpp:475,486` (2) | Assigned/checked; second-failure cleanup is unsafe only because close/wait results are discarded. |
| `TLAS::Init` | `TLASDX12.cpp:74` | `RenderBackendDX12.DXR.cpp:526` (1) | Assigned/checked. |
| `SBT::Build` | `SBTDX12.cpp:75` | `RenderBackendDX12.DXR.cpp:535` (1) | Assigned/checked; internal map remains unsafe. |
| `RenderBackendDX12::InitDXR` | `RenderBackendDX12.DXR.cpp:385` | No in-folder caller | Producer classified. Capability absence intentionally returns success with DXR guarded off; repository-wide discard audit is D1. |
| `CaptureBackbuffer` | `RenderBackendDX12.Readback.cpp:66` | No in-folder caller | Producer classified. It propagates wait/map/create failure but not list-close failure. Repository-wide callers are D1. |
| `RenderBackendDX12::Present/Resize` | `RenderBackendDX12.cpp:1924,2121` | No in-folder callers | Producers classify normal DXGI failure as Lane R; repository-wide callers are D1. |

No `SbResult::Success()` or `SbResult::Failure()` construction is a discarded
temporary: all 63 are returned directly (including the DXR failure passed
through the returned cleanup lambda).

## Initialization And Shutdown Matrix

| Path | Good evidence | Gap that later phases must close |
|---|---|---|
| `Dx12RenderDevice::Init` | Eight core HRESULTs use `Dx12StartupResult`; rollback owns partial COM state. | Initial list `Close` is ignored. `MakeWindowAssociation` and three debug-info configuration results need named best-effort handling. |
| `RenderBackendDX12::Init` | Device, descriptor heaps, back buffers, depth, root signature, and gen-mips return checked `SbResult`. Optional query-heap creation is guarded. | `m_uploadSystem.Init(...)` at line 1440 returns `bool` and is ignored, so init may report success without upload buffers. `GetTimestampFrequency` is ignored. Warmup methods with `void` results (`EnsureGridLinePipeline`, `EnsureTransientTriangleShader`) can leave optional pipelines absent and need an explicit guarded-feature contract in D4. |
| `InitDXR` | Root signature, RTPSO, UAV, both BLAS, TLAS, and SBT `SbResult` values are checked. Capability absence is a guarded raster fallback. | Constants `Map`, two closes, and two waits are ignored. Failure can submit invalid state or release BLAS scratch before completion. |
| `CaptureBackbuffer` | Readback creation/map and `WaitForGpu` are checked. | Close is ignored before submit. |
| `RenderBackendDX12::Shutdown` | Partial device init delegates to the device owner; optional InfoQueue interface is guarded. | Close, first wait, shutdown present, second wait, and InfoQueue message results are ignored; release assumes GPU completion. |
| `Dx12RenderDevice::Shutdown` | COM releases and `Unmap` have no HRESULT. | `SetFullscreenState` and Win32 `CloseHandle` results are ignored; both require explicit best-effort policy/diagnostics. |
| `ShutdownDXR`, BLAS/TLAS/SBT reset | `Unmap` and COM `Release` have no HRESULT. | Safety depends on the caller proving GPU completion; current ignored DXR waits do not provide that proof. |

## Infallible-By-Contract Ledger

These 44 call expressions are not missing HRESULT checks because the API has no
synchronous result. They remain downstream of command-state and fence safety.

| Operation | Count | File:line evidence |
|---|---:|---|
| `CreateRenderTargetView` | 4 | `FramebufferDX12.cpp:178`; `RenderBackendDX12.cpp:883,1421,2171` |
| `CreateDepthStencilView` | 3 | `FramebufferDX12.cpp:188`; `RenderBackendDX12.cpp:891,1689` |
| `CreateShaderResourceView` | 7 | `FramebufferDX12.cpp:202,217`; `RenderBackendDX12.cpp:901,1400`; `RenderBackendDX12.DXR.cpp:372`; `RenderBackendDX12.Textures.cpp:307,566` |
| `CreateUnorderedAccessView` | 4 | `RenderBackendDX12.cpp:911`; `RenderBackendDX12.DXR.cpp:353`; `RenderBackendDX12.Textures.cpp:247,333` |
| `CopyDescriptorsSimple` | 6 | `RenderBackendDX12.DXR.cpp:359,377,755`; `RenderBackendDX12.Pipeline.cpp:572`; `RenderBackendDX12.Textures.cpp:252,339` |
| `GetCopyableFootprints` | 2 | `RenderBackendDX12.Readback.cpp:87`; `RenderBackendDX12.Textures.cpp:513` |
| `GetRaytracingAccelerationStructurePrebuildInfo` | 2 | `BLASDX12.cpp:109`; `TLASDX12.cpp:120` |
| `BuildRaytracingAccelerationStructure` | 2 | `BLASDX12.cpp:186`; `TLASDX12.cpp:206` |
| `ExecuteCommandLists` | 8 | `RenderBackendDX12.cpp:611,1731,1977,2082,2109`; `RenderBackendDX12.DXR.cpp:503,514`; `RenderBackendDX12.Readback.cpp:131` |
| `Unmap` | 5 | `RenderBackendDX12.DXR.cpp:861`; `RenderDeviceDX12.cpp:867,1059`; `SBTDX12.cpp:179`; `TLASDX12.cpp:184` |
| `ResolveQueryData` | 1 | `RenderBackendDX12.cpp:1953` |
| **Total** | **44** | **Zero unclassified** |

`ExecuteCommandLists` is only synchronously result-less. Device removal may be
reported later by present, signal, fence wait, or `GetDeviceRemovedReason`, so a
successful call does not justify ignoring the next result-bearing boundary.

## Reconciliation Commands

The inventory was built after `codegraph status .` reported an up-to-date index
(446 files, 13,219 nodes, 39,089 edges), followed by focused CodeGraph
exploration and source confirmation. Re-run these checks after D1-D4 edits:

```powershell
codegraph explore "SkullbonezSource/Rendering/DX12 HRESULT SbResult WaitForGpu Close Reset Map Present query init shutdown failure handling"
rg -n --no-heading "SbResult|\.ok\b" SkullbonezSource/Rendering/DX12
rg -n --no-heading "HRESULT|FAILED\s*\(|SUCCEEDED\s*\(" SkullbonezSource/Rendering/DX12
rg -n --no-heading -- "->(Close|Reset|Map|Present|ResizeBuffers|GetBuffer|Signal|SetEventOnCompletion|GetTimestampFrequency|QueryInterface|CheckFeatureSupport)\s*\(" SkullbonezSource/Rendering/DX12
rg -n --no-heading "WaitForGpu\s*\(" SkullbonezSource/Rendering/DX12
```

Machine reconciliation must continue to apply the typed exclusions recorded in
this report: custom `Reset()` methods are not HRESULT calls, the line-58 word
`Present` is a comment, `FrameFence().Signal` returns `SbResult` rather than raw
HRESULT, and `ID3D12Resource::GetDesc()` returns a value while the four shader
reflection descriptor calls return HRESULT.

## D0 Closure

- HRESULT rows: **101 / 101 classified**.
- `SbResult` construction/invocation expressions: **115 / 115 classified**.
- Ignored `WaitForGpu`: **10 / 10 enumerated**.
- Ignored `Close`: **9 / 9 enumerated**.
- Ignored allocator/list `Reset`: **4 / 4 enumerated**.
- `Map`: **5 / 5 enumerated** (3 unsafe, 2 checked).
- Present/resize: **3 / 3 enumerated** (1 unsafe, 2 checked).
- Infallible-by-contract rows: **44 / 44 classified**.
- Unclassified rows: **0**.

D0 is therefore complete. No source behavior changed; D1-D5 remain open.

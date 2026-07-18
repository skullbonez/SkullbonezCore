# Small Findings H0 Rulings Census

Date: 2026-07-18  
Branch: `nightrunner-17th-july`  
Source tip measured: `fb1ab285e`  
Owner: cross-cutting engine hygiene

## Ratified decisions

- Continue on the existing feature branch `nightrunner-17th-july`.
- `Runtime/Init.cpp` owns one process-lifetime `LockOrderValidator` beside the
  startup-owned `WorkerPool`. `WorkerPool` receives it explicitly, and its
  actual queue mutex becomes the only `TrackedMutex`. `Instance()` and both
  function-local validator/state singletons are deleted. Debug owns the graph;
  Profile/Release retain ordinary mutex semantics with validation compiled out.
- `Dx12PipelineOwner` issues a nonzero monotonic `uint64_t` root-signature
  identity after each successful root-signature creation. `PSOKey12` hashes the
  value, never the COM address. The owner clears cached PSOs before releasing a
  signature, so a recreated signature cannot alias an old key even if COM
  reuses an address. A global counter was rejected because identity and cache
  lifetime belong to the same concrete pipeline owner.
- The 19 translation units listed below are the complete ratified JSON
  cold-boundary allowlist. The fence must compute transitive reachability, not
  merely scan direct includes. These TUs perform startup/configuration, authored
  data, asset, automation, replay-artifact, capture/report, or explicit editor/
  shader-development IO; JSON objects do not cross their public owner APIs.
- Split `UITabProfiler.cpp` at the histogram owner seam. Histogram option
  caching, sampling, input, and drawing move together; the profiler tree/
  timeline tab remains the coordinator. Retain `UITabMemory.cpp`: its overlay,
  replay-policy controls, samples, and reserve-event rows all mutate one
  `UIMemoryOverlayState` and share one scroll/layout transaction.
- Cast policy is qualitative, not a frozen spelling/count ratchet. H3 applies
  the dispositions below. Retained representation or ABI boundaries receive a
  nearby `Why:` comment; typed replacement rows remove the raw boundary.

## Lock-order ownership census

`LockOrderValidator::Instance()` has exactly one executable caller:
`TrackedMutex::TrackedMutex()` in `Core/LockOrderValidator.cpp`. No production
owner currently instantiates `TrackedMutex`; `WorkerPool::m_mutex` is still a
plain `std::mutex`. This makes the singleton both global and operationally
disconnected from the queue it was intended to diagnose.

H1 therefore wires the complete path:

1. `Runtime/Init.cpp` constructs `LockOrderValidator` before `WorkerPool`.
2. `WorkerPool(LockOrderValidator&)` constructs
   `m_mutex("WorkerPool.Queue", validator)`.
3. `TrackedMutex` borrows the validator for the startup-owned pool lifetime.
4. The validator directly owns its graph state; `State()` and `Instance()` are
   removed.

The borrow cannot outlive its owner because `Init` declares the validator
before the pool and C++ destroys locals in reverse declaration order.

## PSO identity options

| Option | Result | Ruling |
|---|---|---|
| Raw `ID3D12RootSignature*` / `const void*` | Address reuse can make a new binding contract collide with an old in-memory PSO entry. | Reject. |
| Process-global generation counter | Avoids address aliasing but creates a new global authority unrelated to the cache lifetime. | Reject. |
| Serialized-root-signature digest | Stable for equal contracts and already exists for the disk cache, but couples the hot in-memory key to persistence hashing and makes recreation semantics implicit. | Reject for the in-memory key; retain for disk-cache provenance. |
| Pipeline-owner-issued `uint64_t` identity | Cache and identity share one owner; recreation advances identity and shutdown clears cache deterministically. | **Ratified.** |

## Complete 153-site cast census

Measured with:

```text
rg -n --no-heading -e 'reinterpret_cast\s*<' -e 'const_cast\s*<' -e '\bvoid\s*\*' SkullbonezSource
```

The result is 153 matches across 43 files. Line numbers identify the H0 source
tip; H3 must reconcile moved lines by symbol.

| File | Sites | Count | Ratified H3 disposition |
|---|---|---:|---|
| `Core/PlatformProfiler.cpp` | 78 | 1 | Retain one character-representation cast in the hash loop; add `Why:` for unsigned-byte hashing. |
| `Core/WorkerPool.cpp` | 162, 255 | 2 | Retain as the private fixed-capacity task-erasure implementation; callers remain templated/typed. Add one queue-boundary `Why:` and no new callback interface. |
| `Core/WorkerPool.h` | 132, 133, 137, 143, 163, 164, 169, 170, 200, 280 | 10 | Retain the bounded internal task record/trampolines required to queue arbitrary stack-owned task types without allocation; document lifetime, fence, and no-polymorphism constraints. |
| `Physics/PhysicsApi.cpp` | 118 | 1 | Replace the public raw byte hash parameter with a typed byte-view helper; keep any single representation cast inside that helper with `Why:`. |
| `Physics/PhysicsFixedList.h` | 319, 321, 326, 331 | 4 | Retain aligned-storage access; add one precise object-lifetime/`std::launder` `Why:` beside the slot helpers. |
| `Rendering/DX12/Dx12BackbufferCapture.cpp` | 127 | 1 | Replace the wrapper-facing mapped pointer with a byte pointer. The D3D12 `Map` ABI cast stays inside the readback owner. |
| `Rendering/DX12/Dx12CachedPsoStore.cpp` | 76, 95, 106, 156, 183, 402, 442 | 7 | Replace owner-facing raw byte ranges with typed byte views; retain Win32 BCrypt/file-mapping ABI conversions locally with `Why:`. |
| `Rendering/DX12/Dx12CachedPsoStore.h` | 49, 70, 83 | 3 | Replace raw byte fields/parameters with typed `uint8_t` ranges. |
| `Rendering/DX12/Dx12RenderGraphExecutor.cpp` | 292 | 1 | Remove the `const_cast` by carrying a typed mutable DX12 resource in the executor transition record. |
| `Rendering/DX12/Dx12RenderGraphExecutor.h` | 60, 135 | 2 | Replace both native-resource fields with `ID3D12Resource*`. |
| `Rendering/DX12/Dx12TextureRegistry.h` | 66 | 1 | Replace const-cast forwarding with a shared typed slot-index lookup used by const/non-const resolvers. |
| `Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` | 494, 499 | 2 | Retain the immediate `ID3D12Resource::Map` ABI result in the narrow helper; add `Why:` and expose typed mapped bytes after validation. |
| `Rendering/DX12/RenderBackendDX12.DXR.cpp` | 519 | 1 | Retain D3D12 `Map` ABI locally; convert immediately to the typed destination and add `Why:`. |
| `Rendering/DX12/RenderBackendDX12.h` | 157 | 1 | H2: replace `PSOKey12::rootSignature` with owner-issued `uint64_t rootSignatureIdentity`. |
| `Rendering/DX12/RenderBackendDX12.Textures.cpp` | 944 | 1 | Replace const-cast forwarding with shared typed slot lookup. |
| `Rendering/DX12/RenderDeviceDX12.cpp` | 960, 1143, 1157 | 3 | Keep the D3D12 `Map` ABI pointer only inside the device/readback owner; change `MapRead`'s return to typed bytes and add `Why:`. |
| `Rendering/DX12/RenderDeviceDX12.h` | 775 | 1 | Replace `MapRead` return with `const uint8_t*`. |
| `Rendering/DX12/SBTDX12.cpp` | 115, 121, 126, 132, 171 | 5 | Shader identifiers and `Map` are D3D12 ABI pointers. Retain locally, type as `const uint8_t*` immediately, and add `Why:` for the ABI seam. |
| `Rendering/DX12/ShaderBytecodeManifest.cpp` | 75, 92, 428 | 3 | Retain BCrypt/COM reflection ABI conversions; add `Why:` at the two narrow seams and remove avoidable mutable string cast by using a writable byte buffer where practical. |
| `Rendering/DX12/ShaderDX12.cpp` | 484, 920, 1001, 1019 | 4 | Remove the `const_cast` by making the pipeline's active-shader borrow const-correct; change shader bytecode accessors/constant upload to typed byte ranges, with D3D12 conversion at creation. |
| `Rendering/DX12/ShaderDX12.h` | 150, 160, 163 | 3 | Replace public raw byte parameters/returns with typed byte ranges/pointers. |
| `Rendering/DX12/TLASDX12.cpp` | 194 | 1 | Retain D3D12 `Map` ABI locally, convert immediately to typed instance bytes, and add `Why:`. |
| `Rendering/IShader.h` | 68 | 1 | Replace the interface raw byte parameter with a typed byte pointer plus size. |
| `Rendering/RenderGraph.cpp` | 250, 375 | 2 | Replace native resource with a typed render-resource token; keep any callback erasure solely inside the graph's typed registration trampoline and document it. |
| `Rendering/RenderGraph.h` | 166, 293, 359, 378, 579, 598 | 6 | Add typed pass registration so runtime callbacks receive their concrete payload; replace native-resource fields/parameters with the typed token. A single private non-owning erased record may remain with a lifetime `Why:`. |
| `Rendering/RenderPipeline.cpp` | 64 | 1 | Remove the raw callback signature by registering the no-payload diagnostic pass through the typed graph API. |
| `Rendering/Text.cpp` | 298, 305, 337, 370 | 4 | Retain GDI `CreateDIBSection`/`SelectObject` ABI conversions; add one nearby `Why:` and expose pixels as typed `DWORD` rows immediately. |
| `Runtime/Allocation/RuntimeAllocationTracker.cpp` | 66, 104, 248, 252, 293, 298, 307, 309, 328, 335, 338, 346, 367, 394, 397, 663, 668, 673, 678, 683, 688, 693, 698, 703, 708, 713, 718, 723, 728, 733, 738, 743, 748, 753, 758 | 35 | Retain: allocator ABI signatures, raw allocation blocks, alignment arithmetic, return-address/stack-capture addresses, and required global `new`/`delete` signatures. Add comments at the allocator, stack-capture, and global-operator groups rather than per overload. |
| `Runtime/Audio/ContactAudioService.cpp` | 542 | 1 | Retain XAudio2 byte-buffer ABI conversion and add `Why:`. |
| `Runtime/CameraCollection.cpp` | 502 | 1 | Remove pointer-as-diagnostic identity; report the terrain owner's semantic present/absent state. |
| `Runtime/CaptureSystem.cpp` | 60 | 1 | Replace raw write input with a typed byte view; keep `fwrite` conversion inside the helper if required. |
| `Runtime/Editor/RunEditorTracer.cpp` | 94 | 1 | Replace raw replay-submission hash input with the shared typed byte-view helper. |
| `Runtime/Input.cpp` | 88, 89 | 2 | Retain `%p` Win32 diagnostics conversions and add one `Why:` naming the variadic ABI. |
| `Runtime/InteractionAutomationController.cpp` | 118 | 1 | Route scalar hashing through the typed byte-view helper; keep one documented representation cast there. |
| `Runtime/InteractionAutomationReportWriter.cpp` | 89 | 1 | Route scalar hashing through the typed byte-view helper; keep one documented representation cast there. |
| `Runtime/Render/RuntimeRenderer.cpp` | 325, 337, 357, 375, 387, 406, 417, 516, 532, 542, 552, 579, 591 | 13 | Replace raw callback signatures with typed RenderGraph pass registration and concrete borrowed payloads. |
| `Runtime/Replay/ReplayRecorder.cpp` | 956 | 1 | Replace raw hashing parameter with the shared typed byte-view helper. |
| `Runtime/Replay/ReplayV2Artifact.cpp` | 160, 177, 179, 183, 761, 2498, 2579 | 7 | Replace internal append/read/hash entry points with typed byte views; retain only standard stream `char*` representation conversions with `Why:`. |
| `Runtime/Replay/ReplayVisualPacket.h` | 68 | 1 | Replace lambda raw parameter with typed byte view/scalar helper. |
| `Runtime/Replay/ReplayVisualPacketFingerprint.cpp` | 48 | 1 | Route scalar hashing through the typed byte-view helper; keep one documented representation cast there. |
| `Runtime/RuntimeDiagnostics.cpp` | 90, 120, 121, 130, 137, 158, 246 | 7 | Retain PSAPI/VirtualQuery address and structure ABI conversions; add `Why:` at the working-set walk and process-counters seams. |
| `Runtime/Startup/StartupCrashLogging.cpp` | 143, 194 | 2 | Retain DbgHelp variable-tail symbol storage and exception-address ABI; add `Why:`. |
| `Runtime/Window.cpp` | 193, 203, 204, 205, 246, 314 | 6 | Retain required Win32 message/user-data/handle conversions; add `Why:` at WndProc ownership recovery and handle seams. |

Count reconciliation: **153 / 153 ruled; 0 unruled**.

## JSON transitive reachability

A quoted-include graph over all `.cpp`, `.h`, `.hpp`, and `.inl` files under
`SkullbonezSource/` found 19 reachable translation units. Direct includes in
`EditorPlacementAssets.h` account for three editor TUs; the include in
`AuthoredSceneParserSchema.h` accounts for the five parser TUs.

| Ratified cold-boundary TU | Cold operation |
|---|---|
| `Rendering/DX12/ShaderBytecodeManifest.cpp` | shader manifest load/reload |
| `Runtime/Audio/ContactAudioService.cpp` | startup audio-bank decode |
| `Runtime/DemoDirector.cpp` | authored demo recipe load |
| `Runtime/Editor/RunEditorObjectPlacement.cpp` | editor placement asset schema |
| `Runtime/Editor/RunEditorPlacementAssets.cpp` | editor placement asset schema |
| `Runtime/Editor/RunEditorTools.cpp` | explicit editor import/export action |
| `Runtime/Editor/RunEditorTracer.cpp` | editor-authored placement schema reachability |
| `Runtime/InteractionAutomationController.cpp` | automation input decode |
| `Runtime/InteractionAutomationReportWriter.cpp` | automation report serialization |
| `Runtime/Replay/ReplayV2Artifact.cpp` | explicit replay artifact IO |
| `Runtime/Scene/RunScene.cpp` | scene/config load boundary |
| `Runtime/Scene/SceneRuntimeCreate.cpp` | scene construction input boundary |
| `Runtime/Startup/StartupLaunchResolution.cpp` | startup suite/launch decode |
| `Scene/AuthoredSceneParser.cpp` | authored scene decode |
| `Scene/AuthoredSceneParserAssets.cpp` | authored asset decode |
| `Scene/AuthoredSceneParserBodies.cpp` | authored body decode |
| `Scene/AuthoredSceneParserPresentation.cpp` | authored presentation decode |
| `Scene/AuthoredSceneParserRuntime.cpp` | authored runtime-settings decode |
| `Scene/SceneSnapshotWriter.cpp` | explicit scene save action |

H4 extends an existing compile-boundary checker with this exact TU allowlist
and proves the computed reachability set equals it. This is an architectural
quality gate: it prevents dependency leakage and does not budget or freeze a
historical spelling count.

## UI measurements and seam ruling

| TU | Lines | Function bodies | State/owner ruling |
|---|---:|---:|---|
| `UI/UITabProfiler.cpp` | 2,328 | 47 | Split. Lines 404-963 contain histogram selection/cache/drawing helpers; lines 1,285-1,960 contain histogram input/sample/draw public behavior. These move as one histogram implementation owner behind the existing `UIProfilerTabState`. Tree/timeline orchestration remains in the original TU. |
| `UI/UITabMemory.cpp` | 1,507 | 22 | Retain. All helpers participate in one memory overlay and replay-reserve policy panel, share `UIMemoryOverlayState`, and are sequenced by one input/layout/draw transaction. There is no independent lifetime or dependency authority to extract. |

No behavioral baseline, golden, screenshot, authored data, render-interface, or
`FRAME_COUNT` change is authorized by these rulings.

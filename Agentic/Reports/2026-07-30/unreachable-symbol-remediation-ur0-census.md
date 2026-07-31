# Unreachable Symbol Remediation UR0 Census

Date: 2026-07-30
Status: COMPLETE
Closure: `Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md`

## Outcome

The original 407-row provisional scan was corrected before deletion. Exact
COFF joins, internal-linkage exclusion, literal and variadic arity handling,
constructor/destructor relays, cross-TU inlining relays, callback/template
roots, and the mandatory Automation object graph reduced it to 246 review
rows. That is a scanner correction, not a deletion claim.

- Corrected rows: 246
- Classification: 157 no-reference, 58 test-only, 26 own-TU-only, and 5 own-TU-and-test-only.
- Compiler mapping: 245 exact and 1 intentional missing mapping.
- Initial owner decisions: 168 delete and 78 retain-owner.
- Compile correction: `InputRouter::Reset()` changed from delete to a
  private retain-owner lifecycle operation after Debug/Profile compilation
  proved its constructor call.
- Configuration correction: six apparent rows are Automation production
  roots. Five deleted APIs and their two parser dependencies were restored;
  `ImGuiEditorOwner::CopyStatus()` simply left the ruled population.
- Ordinary function removals: 181 total — 167 corrected-census removals,
  12 first-order cascade removals, and two final diagnostics cascade leaves.
- Final ruled population: 79 retain-owner rows, including the private
  `InputRouter::Reset()` lifecycle operation.

The one missing mapping is `EngineLog::CloseAllForTests()`. Its
`SKULLBONEZ_TEST_ENGINE_LOG` native-diagnostics object and direct test call
are explicit owner evidence, so it remains a retained test seam.

## Corrected Census Decisions

Every corrected row is checked below. The disposition records the original
UR0 owner judgement; the compile-corrected `InputRouter::Reset()` exception
is called out inline.

Evidence text explicitly labeled historical below records the first
Debug/Profile adjudication. The configuration-complete Automation rebuild
then removed the six false rows named above and compiled every remaining
deletion; the final strict proof uses all three object roots.

### App exit arbitration

- [x] `SkullbonezSource/Runtime/App/ApplicationExitState.cpp` — `bool ApplicationExitState::HasOwnedFailure() const noexcept`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestApplicationExitState.cpp verifies owned/unowned result precedence.

### Assets built-in shader contract

- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const char* BuiltInShaderBaseName( const char* logicalNameOrBaseName )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only the internal helper has live production use.

### Assets::AssetSystem

- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const AssetLibrarySourceAsset* AssetSystem::FindAssetLibrarySourceAssetById( AssetId id ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const SourceAssetRecord* AssetSystem::FindSourceAsset( const char* logicalName ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; declaration and definition are the only occurrences.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const SourceAssetRecord* AssetSystem::FindSourceAssetById( AssetId id ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const TextureSourceAsset* AssetSystem::FindTextureSourceAsset( const char* logicalName ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const TextureSourceAsset* AssetSystem::FindTextureSourceAssetById( AssetId id ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const std::string& AssetSystem::GetDataRoot() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production, test, callback, export, or same-TU caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const std::vector<AssetLibrarySourceAsset>& AssetSystem::GetAssetLibrarySourceAssets() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `const std::vector<ShaderSourceAsset>& AssetSystem::GetShaderSourceAssets() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `size_t AssetSystem::GetAssetLibrarySourceAssetCount() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `size_t AssetSystem::GetShaderSourceAssetCount() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `size_t AssetSystem::GetSourceAssetCount() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `size_t AssetSystem::GetTextureSourceAssetCount() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Assets/AssetSystem.cpp` — `void AssetSystem::Clear()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### AuthoredScene Lane-R parsing API

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `AuthoredScene AuthoredScene::LoadFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: It has no production caller; parser tests use TryLoadFromFile to observe failure without partial publication.
- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `AuthoredScene AuthoredScene::LoadFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path, const Assets::AssetSystem& assets )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no invocation or dynamic dispatch mechanism.

### AuthoredScene Lane-R style parser

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `AuthoredScene AuthoredScene::LoadStyleFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: It has no production caller; parser tests use TryLoadStyleFromFile for observable failure.
- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `AuthoredScene AuthoredScene::LoadStyleFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path, const Assets::AssetSystem& assets )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no invocation or dynamic dispatch mechanism.

### AuthoredScene asset-library parse contract

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `int AuthoredScene::GetAssetLibraryCount() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: Scene parser tests use it to verify library count and ordering.

### AuthoredScene schema contract

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `uint32_t AuthoredScene::GetSchemaVersion() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: SceneParserUnitTests asserts it for parsed fixtures at multiple test sites.

### AuthoredScene transactional parser

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `SkullbonezCore::Core::SbResult AuthoredScene::TryLoadFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path, AuthoredScene& outScene )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: Scene parser unit tests call it with fixtures and verify failed parsing does not publish partial state.

### AuthoredScene transactional style parser

- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` — `SkullbonezCore::Core::SbResult AuthoredScene::TryLoadStyleFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path, AuthoredScene& outScene )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: Scene parser unit tests call it with fixtures and verify failure remains non-publishing.

### CameraCollection retired hash overload

- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `const Vector3& CameraCollection::GetCameraTranslation( uint32_t hash )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Overload-level search found only its declaration/definition; exact compiler mapping has no reference.

### CameraCollection retired surface

- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `Camera CameraCollection::GetCameraDelta()`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=own-tu-only, own-TU=1, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `bool CameraCollection::IsCameraTweening()`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `bool CameraCollection::IsPrimaryLocked()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `const Vector3& CameraCollection::GetPrimaryMovementBuffer()`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `void CameraCollection::OverrideRenderCameraForFrame( const Vector3& position, const Vector3& view, const Vector3& up )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `void CameraCollection::RelativeUpdate( uint32_t hash, float yMin, float yMax )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` — `void CameraCollection::SetPrimaryUp( const Vector3& vUp )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.

### CaptureController retired surface

- [x] `SkullbonezSource/Runtime/Capture/CaptureController.cpp` — `SkullbonezCore::Core::SbResult CaptureController::SaveBackbufferBmp( Rendering::Dx12BackbufferCapture& backend, const char* path )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Capture/CaptureController.cpp` — `bool CaptureController::IsScreenshotDue( bool isSceneMode, int currentFrame, double elapsedMs ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.

### Core::EngineLog native-diagnostics seam

- [x] `SkullbonezSource/Core/Log.cpp` — `void EngineLog::CloseAllForTests()`
  - Census: no-reference; mapping=missing; decision=retain-owner.
  - Evidence: tools/validate_native_diagnostics.py compiles with SKULLBONEZ_TEST_ENGINE_LOG; ASan Log.obj emits ?CloseAllForTests@EngineLog@Core@SkullbonezCore@@QEAAXXZ and TestRuntimeContracts calls it.

### Core::Environment::Timer

- [x] `SkullbonezSource/Core/Timer.cpp` — `bool Timer::IncrementFrameCount()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Core/Timer.cpp` — `int Timer::GetCurrentFPS()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Core/Timer.cpp` — `void Timer::StoreFpsAndResetFrameCounter()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Core::PlatformProfiler

- [x] `SkullbonezSource/Core/PlatformProfiler.cpp` — `void CpuMarker( const char* name, uint32_t hash )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Core::SbDiagnosticStore bounded diagnostic copy

- [x] `SkullbonezSource/Core/SbResult.cpp` — `SbDiagnosticCopyStatus SbDiagnosticStore::CopyDiagnostic( SbDiagnosticIdentity identity, char ( &owner )[OWNER_CAPACITY], char ( &message )[MESSAGE_CAPACITY] ) const noexcept`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestSbResult directly exercises all identity-status branches and exact bounded output.

### Core::SbDiagnosticStore lease identity

- [x] `SkullbonezSource/Core/SbResult.cpp` — `SbDiagnosticIdentity SbResult::DiagnosticIdentity() const noexcept`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestSbResult and runtime-contract tests directly verify copy/move identity stability and stale/cross-store outcomes.

### Core::Threading::TrackedMutex

- [x] `SkullbonezSource/Core/LockOrderValidator.cpp` — `bool TrackedMutex::try_lock()`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact symbol; reported own-TU occurrence is m_inner.try_lock(), not recursive reachability.
- [x] `SkullbonezSource/Core/LockOrderValidator.cpp` — `uint32_t LockOrderValidator::RegisterLock( const char* name )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: TrackedMutex constructor calls RegisterLock; WorkerPool owns a production TrackedMutex.

### Core::Threading::WorkerPool

- [x] `SkullbonezSource/Core/WorkerPool.cpp` — `bool WorkerPool::IsInitialised() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestRuntimeContracts calls it.
- [x] `SkullbonezSource/Core/WorkerPool.cpp` — `int WorkerPool::GetMinParallelItems() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestRuntimeContracts calls it.

### Core::WorkerProfilerScope

- [x] `SkullbonezSource/Core/Profiler.cpp` — `void Profiler::RecordWorkerSample( const char* fullPath, uint32_t hash, int workerIndex, int64_t startTicks, int64_t endTicks )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: WorkerProfilerScope destructor calls RecordWorkerSample; WorkerPool header templates construct the scope around production worker chunks.

### DX12 render-graph diagnostics

- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp` — `std::string Dx12ResourceStateToString( D3D12_RESOURCE_STATES state )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### DiagnosticsRuntime retired surface

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `DiagnosticsController& DiagnosticsRuntime::Diagnostics()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const CaptureController& DiagnosticsRuntime::Capture() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const DiagnosticsController& DiagnosticsRuntime::Diagnostics() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const DiagnosticsRuntime::UIStressState& DiagnosticsRuntime::UIStress() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const RunPerfLogState& DiagnosticsRuntime::PerfLog() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `const char* DiagnosticsRuntime::MainMemoryDumpPath() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### DiagnosticsRuntime unused forwarding wrapper

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `bool DiagnosticsRuntime::PhysicsDiagnosticsEnabled() const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Receiver-level review distinguishes live controller methods from this unused wrapper.
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` — `void DiagnosticsRuntime::LogPerfMemory( int pass, const char* checkpoint )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Receiver-level review distinguishes live controller methods from this unused wrapper.

### Dx12CachedPsoStore persistent-cache identity contract

- [x] `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp` — `bool Dx12CachedPsoStore::BuildPersistentEntryNameForTest( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const std::array<std::uint8_t, DIGEST_BYTES>& manifestDigest, const std::array<std::uint8_t, DIGEST_BYTES>& rootSignatureDigest, wchar_t ( &outName )[ENTRY_NAME_CHARS] )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestDx12CachedPsoStore.cpp uses it to prove content-address stability, pointer independence, and invalidation boundaries.

### Dx12DescriptorAllocator allocation path

- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `bool Dx12DescriptorAllocator::CanAllocateTransientRange( UINT count ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Only tests reference it; production uses the allocator's actual reservation operation.

### Dx12Diagnostics

- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` — `void Dx12Diagnostics::PlatformProfilerGpuMarker( const char* name, uint32_t hash )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: The stable Debug/Profile symbol census reports no external reference; it is not virtual, exported, address-taken, or callback-registered.

### Dx12FenceTimeline

- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `Dx12FenceTimelineStats Dx12FenceTimeline::GetStats() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### Dx12FenceTimeline lifecycle

- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `void Dx12FenceTimeline::Init( ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE eventHandle )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12RenderDevice initialization calls it.
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `void Dx12FenceTimeline::Reset()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12RenderDevice shutdown calls it.

### Dx12FrameOwner

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `void Dx12FrameOwner::ActivateShader( ShaderDX12* shader )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no external, same-TU, or test reference and no dynamic invocation mechanism.

### Dx12FrameOwner constant upload reservation policy

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveConstantUpload( UINT64 size )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12UploadReservations exposes it to live shader/resource upload paths.
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `void Dx12FrameOwner::CancelPendingConstantUpload()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12UploadReservations forwards cancellation for live upload error paths.

### Dx12FrameOwner deferred descriptor retirement

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `void Dx12FrameOwner::RetireStaticDescriptor( UINT descriptorIndex )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12ResourceRelease forwards to it for framebuffer, transient-resource, and backend resource owners.

### Dx12FrameOwner draw gate

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `bool Dx12FrameOwner::PrepareDraw()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12DrawGate forwards to this method and is consumed by MeshDX12, FramebufferDX12, and dynamic geometry.

### Dx12FrameOwner framebuffer-bind gate

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `bool Dx12FrameOwner::PrepareFramebufferBind()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12DrawGate forwards to it and live framebuffer binding paths consume the gate.

### Dx12FrameOwner geometry upload reservation policy

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes, RenderUploadCategory vertexCategory )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12UploadReservations exposes it to live geometry upload paths.

### Dx12FrameOwner pipeline draw gate

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `bool Dx12FrameOwner::PreparePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh, const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& rasterState )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: The live draw gate reaches it from mesh and dynamic-geometry rendering.

### Dx12FrameOwner pipeline precompile gate

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `bool Dx12FrameOwner::PrecompilePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh, const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& declaredRasterState )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: The live pipeline-owner precompile path reaches it through the draw capability boundary.

### Dx12FrameOwner upload reservation policy

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12UploadReservations forwards to it for live shader, resource, texture, and dynamic-geometry consumers.
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `bool Dx12FrameOwner::PrepareUploadReservation( UINT64 size, UINT64 alignment, RenderUploadCategory category )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: The live reserve methods call it before publishing upload addresses.

### Dx12FrameOwner upload-address mapping

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `uint8_t* Dx12FrameOwner::UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12UploadReservations forwards it to live upload writers.

### Dx12FrameOwner upload-submit transaction

- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `SkullbonezCore::Core::SbResult Dx12FrameOwner::FlushUploadBuffer()`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: PrepareUploadReservation reaches it when the upload arena requires submission before a reservation.
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `int Dx12FrameOwner::SuspendProfilerForSubmit( const char* reason )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: It is reached by the live FlushUploadBuffer and upload-reservation preparation chain.
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` — `void Dx12FrameOwner::RestoreProfilerAfterSubmit( int depth )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: It is the paired completion step in the live FlushUploadBuffer chain.

### Dx12GraphTransientPool const slot lookup

- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` — `const GraphTransientResourceDX12* Dx12GraphTransientPool::FindSlot( RenderGraphResourceHandle resource ) const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Exact decorated-symbol relocations reach this overload from Resolve.

### Dx12GraphTransientPool mutable slot lookup

- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` — `GraphTransientResourceDX12* Dx12GraphTransientPool::FindSlot( RenderGraphResourceHandle resource )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Exact decorated-symbol relocations reach this overload from ExecuteTransitions, BeginRenderTarget, and EndRenderTarget.

### Dx12GraphTransientPool transition execution

- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp` — `Dx12RenderGraphExecutionResult ExecuteDx12RenderGraphTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled, const Dx12RenderGraphExecutionDesc& desc )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Production executes transitions through Dx12GraphTransientPool helpers; this symbol has only test reachability.

### Dx12RaytracingOwner

- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp` — `const SkullbonezCore::Core::SbResult& Dx12RaytracingOwner::FeatureResult() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### Dx12TextureOwner

- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` — `uint32_t Dx12TextureOwner::FindHandleForSrv( UINT srvIndex ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no invocation and no dynamic dispatch mechanism.

### Dx12TextureOwner const handle resolution

- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` — `const TextureEntryDX12* Dx12TextureOwner::ResolveEntry( uint32_t handle ) const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Exact decorated-symbol relocations reach this overload from two const query methods.

### Dx12TextureOwner handle-validity diagnostics

- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` — `void Dx12TextureOwner::ReportStaleHandle( uint32_t handle ) const`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: Live bind, unload, and query operations reach it after failed handle resolution.

### Dx12TextureOwner mutable handle resolution

- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` — `TextureEntryDX12* Dx12TextureOwner::ResolveEntry( uint32_t handle )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Exact decorated-symbol relocations reach this overload from three mutation/bind methods.

### Dx12UploadArena lifecycle

- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `void Dx12UploadArena::Init( ID3D12Resource* resource, uint8_t* mappedPtr, UINT64 capacityBytes )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12FrameUploadSystem calls it during upload-system initialization.
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `void Dx12UploadArena::Reset()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12FrameUploadSystem calls it during reset/shutdown.

### Dx12UploadArena telemetry

- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` — `Dx12UploadArenaStats Dx12UploadArena::GetStats() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Dx12FrameUploadSystem consumes the snapshot in its live statistics path.

### Editor collider lookup retired index wrapper

- [x] `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` — `bool TryResolveEditorBodyCollider( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, int modelIndex, const PhysicsBodyRecord*& outBody, const ColliderRecord*& outCollider )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Source shows only forwarding to the handle overload and no call of this exact signature.

### EditorCommandHistory bounded ring

- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` — `std::size_t EditorCommandHistory::StoredCount() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestEditorCommandHistory.cpp verifies capacity pressure and Clear.

### EditorPlacementAssets retired surface

- [x] `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp` — `bool TryComputeEditorHouseWorldBounds( const EditorHouseDefinition& house, const Vector3& terrainPoint, const RotationMatrix& orientation, Vector3& outMin, Vector3& outMax )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### EditorTracer retired surface

- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` — `void EditorTracer::AddReplayFutureTargetMarker( const Vector3& position, const Quaternion& orientation, const CollisionShapeReference& shape, int depth )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### Gameplay::TornadoField

- [x] `SkullbonezSource/Gameplay/TornadoField.cpp` — `Vector3 TornadoField::SampleAcceleration( const Vector3& position ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Gameplay::TornadoGameplay

- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp` — `void TornadoGameplay::CaptureReplayState( TornadoGameplayReplayState& outState ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp` — `void TornadoGameplay::RestoreReplayState( const TornadoGameplayReplayState& state )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Gameplay::TornadoSystem

- [x] `SkullbonezSource/Gameplay/TornadoField.cpp` — `Vector3 TornadoSystem::SampleAcceleration( const Vector3& position ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### InputActions frame output

- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `void InputActions::Reset()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: InputRouter::BeginFrame calls output.Reset(); RunInputPhase is the production root.

### InputController retired diagnostic component

- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `RuntimeInputMode RuntimeInputContext::PreviousMode() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph shows no-reference; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `RuntimeInputTransition RuntimeInputContext::TransitionAt( int historyIndex ) const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact graph shows own-tu-only; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `bool RuntimeInputContext::AppFocused() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph shows no-reference; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `bool RuntimeInputContext::UIBlocksKeyboard() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph shows no-reference; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `bool RuntimeInputContext::UIBlocksMouse() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph shows no-reference; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `const char* InputController::DescribeAction( RuntimeInputAction action )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact graph shows own-tu-only; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `const char* InputController::DescribeMode( RuntimeInputMode mode )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact graph shows own-tu-only; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `const char* InputController::DescribeSource( RuntimeInputActionSource source )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact graph shows own-tu-only; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `int RuntimeInputContext::TransitionCount() const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact graph shows own-tu-only; any own-TU edges remain inside the unrooted diagnostic component.
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` — `void InputController::DescribeLastTransitions( const RuntimeInputContext& context, char* out, std::size_t outSize )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph shows no-reference; any own-TU edges remain inside the unrooted diagnostic component.

### InputRouter pointer arbitration

- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `RuntimePointerRouteStage RuntimePointerArbitration::Winner() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestInputRouter.cpp verifies BeginStage/FinishStage winner selection.

### InputRouter retired surface

- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `InputKeySnapshot InputKeySnapshot::FromDownKeys( const int* virtualKeys, std::size_t keyCount )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `bool InputActions::Empty() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `const InputActionEvent* InputActions::Data() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `const InputActions& InputRouter::Actions() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `const RuntimeInputContext& InputRouter::RuntimeContext() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Input/InputRouter.cpp` — `void InputRouter::Reset()`
  - Census: test-only; mapping=exact; decision=retain-owner (compile-corrected; private).
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference. Profile/Debug compilation proved the constructor edge that the first scanner graph missed.

### LauncherLaser unused full-clear operation

- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` — `void LauncherLaser::Clear()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Live paths use Update, ResetResources and snapshot restore; no exact Clear caller exists.

### Math::CollisionDetection::BoundingBox

- [x] `SkullbonezSource/Physics/BoundingBox.cpp` — `float BoundingBox::GetSubmergedVolumePercent( float fluidSurfaceHeight ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; GetShapeSubmergedVolumePercent has no production root.

### Math::CollisionDetection::BoundingSphere

- [x] `SkullbonezSource/Physics/BoundingSphere.cpp` — `float BoundingSphere::GetSubmergedVolumePercent( float m_fluidSurfaceHeight ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; GetShapeSubmergedVolumePercent has no production root.

### Math::CollisionDetection::ConvexHullShape

- [x] `SkullbonezSource/Physics/ConvexHullShape.cpp` — `float ConvexHullShape::GetSubmergedVolumePercent( float fluidSurfaceHeight ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; GetShapeSubmergedVolumePercent has no production root.

### Math::CollisionDetection::ConvexHullShape loading

- [x] `SkullbonezSource/Physics/ConvexHullShape.cpp` — `ConvexHullShape ConvexHullShape::LoadFromFile( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only tests call it while production call sites use TryLoadFromFile.

### Math::CollisionDetection::SpatialGrid pure membership seam

- [x] `SkullbonezSource/Physics/SpatialGrid.cpp` — `void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs, bool restrictToPairSourceCells )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestSpatialGrid invokes it directly with caller-reserved output; production uses filtered candidate generation.

### Math::Orbital analytical oracle

- [x] `SkullbonezSource/Maths/OrbitalMechanics.cpp` — `float HohmannDepartureDeltaV( float r1, float r2, float mu )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestOrbitalMechanics calls it.
- [x] `SkullbonezSource/Maths/OrbitalMechanics.cpp` — `float HohmannTransferSeconds( float r1, float r2, float mu )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestOrbitalMechanics calls it.

### Math::Orientation::Quaternion

- [x] `SkullbonezSource/Maths/Quaternion.cpp` — `Quaternion Quaternion::GetQtnRotatedAboutX( float fRadians )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Maths/Quaternion.cpp` — `Quaternion Quaternion::GetQtnRotatedAboutY( float fRadians )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Maths/Quaternion.cpp` — `Quaternion Quaternion::GetQtnRotatedAboutZ( float fRadians )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Maths/Quaternion.cpp` — `void Quaternion::RotateAboutXYZ( const Vector3& vRadians )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestQuaternion calls it.
- [x] `SkullbonezSource/Maths/Quaternion.cpp` — `void Quaternion::RotateAboutXYZ( float xRadians, float yRadians, float zRadians )`
  - Census: own-tu-and-test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production root.

### Math::Transformation::Matrix4

- [x] `SkullbonezSource/Maths/Matrix4.cpp` — `Matrix4 Matrix4::Perspective( float fovDegrees, float aspect, float nearPlane, float farPlane )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestMatrix4 calls it.
- [x] `SkullbonezSource/Maths/Matrix4.cpp` — `Matrix4 Matrix4::RotateAxis( float angleDeg, float axisX, float axisY, float axisZ )`
  - Census: own-tu-and-test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; own-TU calls are inside ShadowFromNormal and tests call it directly.
- [x] `SkullbonezSource/Maths/Matrix4.cpp` — `Matrix4 Matrix4::Scale( const Vector3& v )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; the ambiguous lexical production occurrence resolves to a scalar Scale overload.
- [x] `SkullbonezSource/Maths/Matrix4.cpp` — `Matrix4 Matrix4::ShadowFromNormal( float tx, float ty, float tz, const Vector3& N, float scale )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestMatrix4 calls it.

### Math::Transformation::RotationMatrix

- [x] `SkullbonezSource/Maths/RotationMatrix.cpp` — `void RotationMatrix::Identity()`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestQuaternion calls it.

### Math::Visibility::Frustum

- [x] `SkullbonezSource/Maths/Frustum.cpp` — `const FrustumPlane& Frustum::Plane( int index ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestFrustum calls it.

### MeshDX12 resource lifecycle

- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp` — `void MeshDX12::ResetResources()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census finds no invocation or dynamic dispatch mechanism.

### Physics::ColliderStore

- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `ColliderRecord* ColliderStore::MutableRecordForHandle( PhysicsColliderHandle handle )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `bool ColliderStore::Empty() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `const ColliderRecord* ColliderStore::Data() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only TestPhysicsHandles calls it.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `std::span<ColliderRecord> ColliderStore::MutableRecords()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::ColliderStore topology transaction

- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `PhysicsColliderHandle ColliderStore::CreateColliderRecord( const ColliderRecord& initialRecord, const CollisionShape& shape )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; tests alone call it and can supply explicit authoring and hull identity defaults.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `PhysicsColliderHandle ColliderStore::CreateColliderRecord( const ColliderRecord& initialRecord, const CollisionShape& shape, const ColliderAuthoringRecord& initialAuthoringRecord )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; tests alone call it and can supply explicit HullShapeIdentity.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `bool ColliderStore::UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record, const CollisionShape& shape )`
  - Census: own-tu-and-test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; own-TU edge reaches the full overload and tests are the only root.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `bool ColliderStore::UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record, const CollisionShape& shape, const ColliderAuthoringRecord& authoringRecord )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test root.
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — `bool ColliderStore::UpdateRecordForModelIndex( int modelIndex, const ColliderRecord& record, const CollisionShape& shape )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::PersistentContactSolveTransaction body storage

- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` — `void PersistentContactSolveTransaction::ReserveSceneCapacity( std::size_t bodyCapacity )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: PhysicsContactSolverStage::ReserveSceneCapacity calls m_solveTransaction.ReserveSceneCapacity.

### Physics::PersistentContactSolveTransaction lifecycle

- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` — `void PersistentContactSolveTransaction::Clear()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: PhysicsContactSolverStage::Clear and RestoreReplayState call m_solveTransaction.Clear.

### Physics::PersistentContactSolveTransaction memory census

- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` — `uint64_t PersistentContactSolveTransaction::CollectDynamicMemoryBytes() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: PhysicsContactSolverStage::CollectDynamicMemoryBytes adds m_solveTransaction.CollectDynamicMemoryBytes().

### Physics::PhysicsBodyStore

- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — `bool PhysicsBodyStore::ApplyBodyImpulse( PhysicsBodyHandle body, const Vector3& impulse, const Vector3& localApplicationPoint )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only tests call it; production PhysicsEngine sets pending impulse and wakes the solver.
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — `bool PhysicsBodyStore::ConsumePendingBodyImpulse( int modelIndex )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; only tests call it.
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — `bool PhysicsBodyStore::Empty() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — `bool PhysicsBodyStore::WakeBody( PhysicsBodyHandle body )`
  - Census: own-tu-and-test-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; production PhysicsEngine wake owns solver sleep/island state instead.
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — `const PhysicsBodyRecord* PhysicsBodyStore::Data() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::PhysicsEngine

- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp` — `bool PhysicsEngine::SetDiagnosticsSuppressed( bool suppressed )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; reported own-TU occurrence is the same-name call on m_world, not a caller of this wrapper.
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp` — `bool PhysicsEngine::ShouldEmitCollisionTimeDiagnostics() const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; reported own-TU occurrence is the same-name call on m_world, not a caller of this wrapper.
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp` — `bool PhysicsEngine::ShouldEmitStepDiagnostics() const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; reported own-TU occurrence is the same-name call on m_world, not a caller of this wrapper.
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp` — `void PhysicsEngine::ValidatePhysicsStoreMappings( int modelCount ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::PhysicsNarrowphaseStage deterministic island ordering

- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` — `bool PhysicsNarrowphaseStage::ObjectNarrowphaseIslandPrecedesByMinPairIndex( const ObjectNarrowphaseIsland& a, const ObjectNarrowphaseIsland& b )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: BuildObjectNarrowphaseIslands passes this function as the comparator to std::sort.

### Physics::PhysicsNarrowphaseStage island work item

- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` — `void PhysicsNarrowphaseStage::ProcessObjectNarrowphaseIsland( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain, std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<const std::pair<int, int>> candidatePairs, PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining, std::span<const PersistentContactCacheEntry> persistentContactCache, const ObjectNarrowphaseStepPolicy& policy, Core::Profiler* profiler, int islandIndex )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ObjectNarrowphaseIslandStage::operator() calls it; WorkerPool::ParallelForNoAlloc dispatches that typed work item.

### Physics::PhysicsSleepController

- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` — `bool PhysicsSleepController::IsPointJointPair( const PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> pointJointConstraints, int bodyA, int bodyB ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::PhysicsStepDiagnostics

- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` — `bool PhysicsStepDiagnostics::CanRecordPipelineStage() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### Physics::Ragdoll

- [x] `SkullbonezSource/Physics/Ragdoll.cpp` — `float Ragdoll::DefaultEditorScale()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; declaration and definition are the only occurrences.

### ReflectionPass lifecycle

- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — `void ReflectionPass::LogResourceLifecycleStep( const char* phase, const char* step ) const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: EnsureGpuResources and ReleaseGpuResources call it before target mutation.

### RenderDefaultsStore request queue

- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp` — `RenderDefaultsRequestType RenderDefaultsStore::PendingTypeAt( std::size_t index ) const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestOwnerRequestQueues verifies pending request order and type.

### RenderGraph diagnostic formatting

- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `const char* ToString( RenderGraphPassExecutionOwner owner )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live graph text formatting calls this exact overload.
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `const char* ToString( RenderGraphQueueType queue )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live graph text formatting calls this exact overload.
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `const char* ToString( RenderGraphResourceFormat format )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live graph text formatting calls this exact overload.
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `const char* ToString( RenderGraphResourceKind kind )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live graph text formatting calls this exact overload.

### RenderGraph ranged callback execution

- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `RenderGraphCallbackExecutionResult RenderGraph::ExecuteCallbacks( RenderGraphCallbackExecutionMode mode ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: The live owner is the ranged overload; the stable census finds no caller of this wrapper.

### RenderGraphCompileResult lifecycle

- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` — `void RenderGraphCompileResult::Clear()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: RenderGraph::Compile calls it directly.

### RenderInstanceStore

- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` — `bool RenderInstanceStore::Empty() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` — `const RenderInstanceRecord* RenderInstanceStore::Data() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### RenderInstanceStore snapshot refresh

- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` — `void RenderInstanceStore::Refresh( const RenderInstancePresentationRecord* presentation, int presentationCount, const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, float presentationAlpha )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: The live vector overload calls it from SceneWorld::Refresh.
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` — `void RenderInstanceStore::Refresh( const std::vector<RenderInstancePresentationRecord>& presentation, const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, float presentationAlpha )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: SceneWorld::Refresh reaches it and it delegates to the pointer/count implementation.

### RenderResourceLifecycle retired surface

- [x] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` — `const Rendering::PrimitiveBatchRenderer& RenderResourceLifecycle::PrimitiveBatches() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### Replay V2 presentation-only schema

- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` — `bool ReplayV2Artifact::SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestReplayArtifact creates deterministic, legacy, and adversarial presentation-only artifacts.

### Replay event recorder

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `uint64_t ReplayEventRecorder::CollectMemoryBytes() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestReplayRecorder and TestRuntimeValueSeams verify accounting.

### Replay prediction path presentation

- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` — `ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ReplayPredictionPresentation::RenderPathVisualizer calls it from the production presentation path.

### Replay presentation capacity policy

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplayRecorder::CheckpointCapacityFromConfig() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ReplayRecorder::Configure calls it before live capture storage is sized.
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplayRecorder::SampleCapacityFromConfig() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ReplayRecorder::Configure calls it before live capture storage is sized.

### Replay presentation checkpoints

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `void ReplayRecorder::StoreCheckpointSummary( const ReplayPresentationSample& sample, std::size_t bodyCount )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live presentation capture calls it when a checkpoint is due.

### Replay presentation recorder

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `uint64_t ReplayRecorder::CollectMemoryBytes() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestReplayRecorder and TestRuntimeValueSeams verify accounting.

### Replay presentation recorder ring

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplayRecorder::AcquireSampleSlotIndex()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live ReplayRecorder capture methods call it before storing frames.
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `void ReplayRecorder::PromoteVisualFrameToKeyframe( std::size_t offset )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: AcquireSampleSlotIndex calls it before overwriting a full ring.

### Replay solver capacity policy

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplaySolverRecorder::CheckpointCapacityFromConfig() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ReplaySolverRecorder::Configure calls it before live storage is sized.
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplaySolverRecorder::SampleCapacityFromConfig() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ReplaySolverRecorder::Configure calls it before live storage is sized.

### Replay solver checkpoints

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `void ReplaySolverRecorder::StoreCheckpointSummary( const ReplaySolverFrameSample& sample, std::size_t bodyCount )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live solver capture calls it when a checkpoint is due.

### Replay solver recorder

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `uint64_t ReplaySolverRecorder::CollectMemoryBytes() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestReplayRecorder and TestRuntimeValueSeams verify accounting.

### Replay solver recorder ring

- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `std::size_t ReplaySolverRecorder::AcquireSampleSlotIndex()`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live solver capture calls it before storing frames.
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — `void ReplaySolverRecorder::PromoteSolverFrameToKeyframe( std::size_t offset )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: AcquireSampleSlotIndex calls it before overwriting a full ring.

### ReplayPlanningRuntime retired surface

- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp` — `Physics::ModelRowHint ReplayPlanningRuntime::InterceptTargetModelRow() const noexcept`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp` — `Physics::PhysicsSceneObjectId ReplayPlanningRuntime::InterceptTargetId() const noexcept`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### ReplayPresentation retired overload

- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp` — `bool ReplayPresentation::SetPathTarget( const char* name, int modelIndex, const Physics::PhysicsBodyStore& bodyStore )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact overload has no caller.

### ReplayRuntime retired validation surface

- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` — `ReplayVisualPacket ReplayRuntime::BuildVisualProjectionForValidation( PhysicsEngine& physics, const SceneEntityStore& entities, std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords, const PhysicsBodyStore& bodyStore, RuntimeTools& runtimeTools, const Math::Vector::Vector3& cameraEye, const Math::Vector::Vector3& cameraUp, uint64_t replayReserveGrowthEvents )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph and source review find only declaration/definition.
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` — `bool ReplayRuntime::BuildPredictionArchiveForValidation( std::vector<uint8_t>& outBytes ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph and source review find only declaration/definition.
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` — `bool ReplayRuntime::LoadPredictionArchiveForVerification( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph and source review find only declaration/definition.
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` — `void ReplayRuntime::EnterOfflinePredictionVerification()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph and source review find only declaration/definition.
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` — `void ReplayRuntime::ResetPredictionPresentationVerification()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact graph and source review find only declaration/definition.

### ReplayV2Artifact retired reduced overload

- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` — `bool ReplayV2Artifact::SavePresentationWithSolverHashes( const ReplayRecorder& recorder, const ReplaySolverRecorder& solverRecorder, const char* path, ReplayV2SaveResult* result )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact overload has no source or compiler caller.

### Retired Replay cause-tree layout

- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp` — `UI::UIRect ReplayCauseTreePanelRect( int screenW, int screenH )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: The live planning/operator panel uses its current layout path; graph has no production root.
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp` — `UI::UIRect ReplayCauseTreeRowRect( const UI::UIRect& panel, int visibleRow )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: The live planning/operator panel uses its current layout path; graph has no production root.
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp` — `int ReplayCauseTreeVisibleRowCapacity( const UI::UIRect& panel )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: The live planning/operator panel uses its current layout path; graph has no production root.

### Runtime render pass empty release wrapper

- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — `void DebugOverlayPass::ReleaseGpuResources()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Source review confirms empty body and no exact caller.
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — `void ObjectPass::ReleaseGpuResources()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Source review confirms empty body and no exact caller.

### RuntimeInteractionController retired surface

- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp` — `CameraLookState RuntimeInteractionController::CameraLook() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp` — `PhysicsAdvanceState RuntimeInteractionController::PhysicsAdvance() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp` — `RuntimeInteractionTransition RuntimeInteractionController::SetWorldInteractionOwner( WorldInteractionOwner owner, InteractionExitReason reason )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### RuntimeRenderPasses retired surface

- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — `uint32_t FullscreenQuadPass::QuadVB() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### RuntimeTools retired const accessor

- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` — `const EditorTracer& RuntimeTools::Tracer() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact const-overload identity has no production reference.
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` — `const RunMousePickupState& RuntimeTools::MousePickup() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact const-overload identity has no production reference.
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` — `const RunRayCastTestState& RuntimeTools::RayCastTest() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact const-overload identity has no production reference.

### RuntimeValidationHarness retired surface

- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` — `const SceneAutomationGateTracker& RuntimeValidationHarness::SceneGates() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### Scene cinematic policy

- [x] `SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` — `const SkullbonezCore::Core::CinematicRenderConfig& ActiveSceneCinematicConfig( const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: OperatorEditorFrameComposer calls this const overload in production.

### SceneController retired surface

- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp` — `std::size_t SceneController::PendingRequestCount() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### SceneEntityStore retired surface

- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — `bool SceneEntityStore::IsSimpleRagdollTorso( int modelIndex ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — `const Rendering::RenderMaterial& SceneEntityCreateDesc::GetRenderMaterial() const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — `const char* SceneEntityCreateDesc::GetName() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — `int SceneEntityStore::GroupPartIndexAt( int modelIndex ) const`
  - Census: own-tu-and-test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=own-tu-and-test-only, own-TU=1, tests=1; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — `int SceneEntityStore::RagdollRootModelIndexForPart( int modelIndex ) const`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.

### SceneLoadTransaction load phase

- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` — `SkullbonezCore::Core::SbResult SceneController::Load( const SceneLoadRequest& request, SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions, const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender, const RunStartupState& startup, Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, DiagnosticsRuntime& diagnosticsRuntime, Rendering::Dx12FrameOwner* renderFrame, Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer, SceneLoadTransaction& transaction )`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: SceneLoadTransaction::Load advances phase then calls this exact overload with itself.

### SceneNavigationModel retired surface

- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp` — `SceneLoadRequest LoadDemoScene( SceneSession& scene )`
  - Census: test-only; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=test-only, own-TU=0, tests=1; no rooted Debug/Profile reference.

### SceneSessionState retired surface

- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.cpp` — `const std::vector<std::string>& SceneSession::Queue() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### SceneWorld retired surface

- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` — `bool SceneWorld::ReleaseAttachedFixedTreeParts( int sourceIndex, float releaseImpulseStrength, const Vector3& seedLinearVelocity, const Vector3& seedAngularVelocity )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` — `bool SceneWorld::RestoreReplaySolverWorldSnapshot( const Physics::PhysicsSolverSnapshot& snapshot )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` — `bool SceneWorld::TryGetModelPosition( int index, Vector3& outPosition ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` — `void SceneWorld::CaptureReplaySolverWorldSnapshot( Physics::PhysicsSolverSnapshot& outSnapshot ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Historical pre-Automation evidence: Exact compiler mapping; graph-fixed classification=no-reference, own-TU=0, tests=0; no rooted Debug/Profile reference.

### ShaderDX12

- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` — `const char* ShaderDX12::SourcePath() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### ShaderDX12 binding surface

- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` — `void ShaderDX12::SetVec3( const char* name, const Vector3& v ) const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no invocation and no address-taken callback use.

### ShadowPass lifecycle

- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — `void ShadowPass::LogResourceLifecycleStep( const char* phase, const char* step ) const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: ShadowPass::ReleaseGpuResources calls it for terrain, object, and frame-payload reset steps.

### Simulation fixed-step diagnostics

- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp` — `uint64_t SimulationSystem::DroppedPhysicsTickCount() const noexcept`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestSimulationSystem verifies reset, hitch, and lifecycle accounting.
- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp` — `uint64_t SimulationSystem::PhysicsHitchEventCount() const noexcept`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestSimulationSystem verifies reset, hitch, and lifecycle accounting.

### Terrain authoritative height-map physics construction

- [x] `SkullbonezSource/World/Terrain.cpp` — `SkullbonezCore::Core::SbResult Terrain::TryCreatePhysicsFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* sFileName, int iMapSize, int iStepSize, int iTextureWrap, const SkullbonezCore::Core::EngineConfig& config, std::unique_ptr<Terrain>& outTerrain )`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: TestTerrain.cpp and runtime-contract fatal probes use it to exercise authoritative terrain loading without a render device.

### Terrain immutable construction configuration

- [x] `SkullbonezSource/World/Terrain.cpp` — `const SkullbonezCore::Core::EngineConfig& Terrain::Config() const`
  - Census: no-reference; mapping=exact; decision=retain-owner.
  - Evidence: Live terrain shader/render paths call it.

### Terrain surface sampling

- [x] `SkullbonezSource/World/Terrain.cpp` — `Vector3 Terrain::GetTerrainNormalAt( float xPosition, float zPosition )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### Text2d formatting and batching

- [x] `SkullbonezSource/Rendering/Text.cpp` — `void Text2d::Render2dText( TextBatch& batch, float xPosition, float yPosition, float fSize, const char* cRawText, ... )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Literal-arity-aware stable scanning maps it exactly and finds no invocation.

### Textures::TextureCollection

- [x] `SkullbonezSource/Assets/TextureCollection.cpp` — `int TextureCollection::NumFreeTextureSpaces() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: Exact decorated symbol; no production or test caller.

### UI layout policy

- [x] `SkullbonezSource/UI/UILayout.cpp` — `float MinimizedWidthForTitle( const char* title, int screenW )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### UI style palette

- [x] `SkullbonezSource/UI/UIStyle.cpp` — `const UIColor& AccentCyan()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.
- [x] `SkullbonezSource/UI/UIStyle.cpp` — `const UITextStyle& Text()`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### UIBackdropBlur cache invalidation

- [x] `SkullbonezSource/UI/UIBackdropBlur.cpp` — `UIBackdropBlurInvalidationReason UIBackdropBlur::LastInvalidationReason() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### UICacheState invalidation

- [x] `SkullbonezSource/UI/UICache.cpp` — `uint32_t UICacheState::DirtyFlags() const`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable exact census reports no production or test reference.

### UIDrawList deterministic command-surface contract

- [x] `SkullbonezSource/UI/UIDrawList.cpp` — `uint64_t UIDrawList::Fingerprint() const`
  - Census: test-only; mapping=exact; decision=retain-owner.
  - Evidence: UiBoundaryUnitTests compares fingerprints to prove equal draw surfaces remain stable and changed surfaces differ.

### UIEditorMiniPalette text layout

- [x] `SkullbonezSource/UI/UIEditorMiniPalette.cpp` — `void DrawFittedText( const UIDrawContext& draw, float x, float y, float pxSize, const Style::UIColor& color, const char* value, float maxWidth )`
  - Census: no-reference; mapping=exact; decision=delete.
  - Evidence: The stable census finds no external root; its private EllipsizeToWidth dependency is likewise unrooted.
- [x] `SkullbonezSource/UI/UIEditorMiniPalette.cpp` — `void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: No production or test root reaches either helper.

### UIWindowInteractionOwner

- [x] `SkullbonezSource/UI/UI.cpp` — `bool InGameUI::IsPerformanceHistogramEnabled() const`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: The stable exact census finds no external caller.
- [x] `SkullbonezSource/UI/UI.cpp` — `void InGameUI::SetMemoryOverlayEnabled( bool enabled )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: The stable exact census finds no external caller.
- [x] `SkullbonezSource/UI/UI.cpp` — `void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )`
  - Census: own-tu-only; mapping=exact; decision=delete.
  - Evidence: The stable exact census finds no external caller.

### Window native-message boundary

- [x] `SkullbonezSource/Runtime/App/Window.cpp` — `DevelopmentTools::ImGuiEditorNativeMessageRoute Window::RouteDevelopmentUiMessage( HWND window, UINT message, WPARAM wParam, LPARAM lParam )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: Window.cpp WndProc calls this exact method under SKULLBONEZ_DEVELOPMENT_TOOLS.

### Window size lifecycle

- [x] `SkullbonezSource/Runtime/App/Window.cpp` — `void Window::SetWindowDimensions( int m_width, int m_height )`
  - Census: own-tu-only; mapping=exact; decision=retain-owner.
  - Evidence: Window.cpp WndProc calls this exact integer overload with LOWORD/HIWORD(lParam).

## Cascade Removals

Deleting the first 167 definitions exposed twelve more operations whose
only roots had been retired. Each declaration and definition was removed.

- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp` — `bool PhysicsWorld::SetDiagnosticsSuppressed( bool suppressed )`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp` — `bool PhysicsWorld::ShouldEmitCollisionTimeDiagnostics() const`
- [x] `SkullbonezSource/Runtime/Camera/Camera.cpp` — `void Camera::ApplyDelta( const Camera& delta, const CameraMovementSettings& settings )`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp` — `bool DiagnosticsController::PhysicsDiagnosticsEnabled() const`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp` — `const RunPerfLogState& DiagnosticsController::PerfLog() const`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp` — `const RunPhysicsDiagnosticsState& DiagnosticsController::PhysicsDiagnostics() const`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp` — `void DiagnosticsController::LogPerfMemory( int pass, const char* checkpoint )`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp` — `AuthoredScene AuthoredSceneParser::LoadScene( const char* path )`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp` — `AuthoredScene AuthoredSceneParser::LoadStyle( const char* path )`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp` — `AuthoredScene LoadAuthoredSceneFromFileImpl( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, const char* path, const Assets::AssetSystem* assets )`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp` — `AuthoredScene LoadStyleSceneFromFileImpl( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, const char* path, const Assets::AssetSystem* assets )`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp` — `void UIWindowInteractionOwner::ToggleMaximizeMinimize( int screenW, int screenH, double now )`

Successive synchronized post-cascade scans then exposed two final
diagnostics forwarding leaves:

- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` — `bool PhysicsStepDiagnostics::ShouldEmitCollisionTimeDiagnostics( bool diagnosticsSuppressed ) const`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp` — `bool PhysicsDiagnosticsSink::IsCollisionTimeLogEnabled() const`

## Reconciliation

- [x] 246 corrected rows have exactly one owner decision.
- [x] No ambiguous mapping remains; the only missing mapping has direct
  native-diagnostics build/test evidence.
- [x] Every delete decision was removed without a compatibility alias.
- [x] Every surviving row has an exact `retain-owner` ruling.
- [x] No `repair-plan` ruling remains.

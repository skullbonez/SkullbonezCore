/*
File: SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
Purpose:
  Declares the private DX12 frame epoch and fence-proven resource-retirement owners.

Summary:
  Dx12FrameOwner owns command recording, submission, allocator/upload reuse,
  back-buffer state, profiler suspension, and deferred release for one bounded
  two-frame lifecycle. It borrows Dx12DescriptorHeaps so descriptor rows reset
  and retire only when this owner proves their covering fence. Restricted
  capability views expose only draw, upload, retirement, capture, or diagnostic
  timing/fault operations.

Invariants:
  - FRAME_COUNT remains two unless profiling explicitly justifies added queued latency.
  - Allocators, upload bytes, resources, and borrowed descriptor rows are never reused before their covering fence.
  - Backbuffer access advances only after the corresponding native barrier emits.
  - The first recording/device failure is sticky until a new device lifecycle resets the owner.
  - Capability objects cannot reach unrelated backend state.
  - Diagnostics can inspect the fence timeline but cannot submit or advance it.
  - Retirement diagnostics derive release counts from one input/survivor pair
    and reset at each device-lifecycle boundary.
  - Pass precompile may populate the bounded PSO cache but cannot bind command
    state or submit a draw.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderBackendDX12.PipelineState.h"
#include "RenderDeviceDX12.h"
#include "Dx12DescriptorHeaps.h"
#include "MeshDX12.h"
#include "../../Core/FatalError.h"
#include "../RenderCommandTypes.h"
#include "../RenderGraph.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>

namespace SkullbonezCore
{
namespace Rendering
{

class ShaderDX12;
class Dx12PipelineOwner;
class Dx12TextureOwner;
class Dx12Diagnostics;
class Dx12FrameOwner;
struct Dx12DeferredReleaseOwnerTestAccess;
struct DynamicVBDX12;
struct InstancedMeshDX12;

struct DeferredResourceReleaseDX12
{
    ID3D12Resource* resource = nullptr;
    UINT staticDescriptorIndex = UINT_MAX;                                 // Optional persistent row released by the same covering fence.
    Dx12CpuDescriptorKind cpuDescriptorKind = Dx12CpuDescriptorKind::None; // Typed route back to the descriptor owner.
    UINT cpuDescriptorIndex = UINT_MAX;
    UINT64 fenceValue = 0;
    bool fenceAssigned = false;
};

// Invariant: one diagnostic state derives every reported release fact from the
// same input/survivor pair and retains only the latest observed fence state.
class Dx12RetirementDiagnosticState
{
  public:
    void ObservePendingCount( size_t pendingCount )
    {

        if ( pendingCount > m_pendingHighWater )
        {
            m_pendingHighWater = pendingCount;
        }
    }
    void ObserveRelease( size_t inputCount, size_t survivorCount, bool frameFenceReady, UINT64 completedFence )
    {

        if ( survivorCount > inputCount )
        {
            SB_FATAL( "Dx12RetirementDiagnosticState",
                      "Retirement release diagnostics received impossible accounting. input=%zu survivors=%zu", inputCount,
                      survivorCount );
        }

        m_lastReleaseInputCount = inputCount;
        m_lastReleasedCount = inputCount - survivorCount;
        m_lastReleaseSurvivorCount = survivorCount;
        m_lastFrameFenceReady = frameFenceReady;

        if ( frameFenceReady )
        {
            m_lastObservedCompletedFence = completedFence;
        }
    }
    void Reset()
    {
        m_pendingHighWater = 0;
        m_lastReleaseInputCount = 0;
        m_lastReleasedCount = 0;
        m_lastReleaseSurvivorCount = 0;
        m_lastObservedCompletedFence = 0;
        m_lastFrameFenceReady = false;
    }
    size_t PendingHighWater() const
    {
        return m_pendingHighWater;
    }
    size_t LastReleaseInputCount() const
    {
        return m_lastReleaseInputCount;
    }
    size_t LastReleasedCount() const
    {
        return m_lastReleasedCount;
    }
    size_t LastReleaseSurvivorCount() const
    {
        return m_lastReleaseSurvivorCount;
    }
    UINT64 LastObservedCompletedFence() const
    {
        return m_lastObservedCompletedFence;
    }
    bool LastFrameFenceReady() const
    {
        return m_lastFrameFenceReady;
    }
    [[noreturn]] void FatalExhaustion( size_t capacity, size_t currentCount ) const
    {

        // Hazard: high-water necessarily reaches capacity before a bounded
        // queue can reject its next row. The last release facts distinguish
        // normal saturation from a stalled or never-observed fence.
        SB_FATAL( "Dx12DeferredReleaseOwner",
                  "Retirement capacity exhausted. owner=Rendering/DX12 phase=quarantine "
                  "capacity=%zu count=%zu high_water=%zu "
                  "last_release_input=%zu last_released=%zu last_survivors=%zu fence_ready=%d "
                  "last_completed_fence=%llu",
                  capacity, currentCount, PendingHighWater(), LastReleaseInputCount(), LastReleasedCount(),
                  LastReleaseSurvivorCount(), LastFrameFenceReady() ? 1 : 0,
                  static_cast<unsigned long long>( LastObservedCompletedFence() ) );
    }

  private:
    size_t m_pendingHighWater = 0;
    size_t m_lastReleaseInputCount = 0;
    size_t m_lastReleasedCount = 0;
    size_t m_lastReleaseSurvivorCount = 0;
    UINT64 m_lastObservedCompletedFence = 0;
    bool m_lastFrameFenceReady = false;
};

// Lifetime: resources invalidated while command work may still reference them
// are quarantined here until a covering fence or terminal drain proves release.
class Dx12DeferredReleaseOwner
{
  public:

    // Bounded above the 128 static-row heap so every row can retire alongside
    // a resource while leaving headroom for resource-only readbacks/uploads.
    // The stress churn is the runtime high-water proof for this fixed queue.
    static constexpr size_t MAX_PENDING_RETIREMENTS = 512;
    void Quarantine( ID3D12Resource* resource, UINT descriptorIndex = UINT_MAX,
                     Dx12CpuDescriptorKind cpuKind = Dx12CpuDescriptorKind::None, UINT cpuDescriptorIndex = UINT_MAX )
    {

        if ( !resource && descriptorIndex == UINT_MAX && cpuKind == Dx12CpuDescriptorKind::None )
        {
            return;
        }

        if ( m_pendingCount >= MAX_PENDING_RETIREMENTS )
        {
            m_diagnostics.FatalExhaustion( MAX_PENDING_RETIREMENTS, m_pendingCount );
        }

        DeferredResourceReleaseDX12 retired;
        retired.resource = resource;
        retired.staticDescriptorIndex = descriptorIndex;
        retired.cpuDescriptorKind = cpuKind;
        retired.cpuDescriptorIndex = cpuDescriptorIndex;
        m_pending[m_pendingCount++] = retired;
        m_diagnostics.ObservePendingCount( m_pendingCount );
    }
    void QuarantineStaticDescriptor( UINT descriptorIndex )
    {

        if ( descriptorIndex != UINT_MAX )
        {
            Quarantine( nullptr, descriptorIndex );
        }
    }
    void AssignFence( UINT64 fenceValue );
    void ReleaseCompleted( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors, Dx12SubmittedWorkState& submittedWork,
                           bool releaseUnfenced );
    void ResetForDevice()
    {

        if ( m_pendingCount != 0 )
        {
            SB_FATAL( "Dx12DeferredReleaseOwner",
                      "Retirement diagnostics reset crossed a live queue. owner=Rendering/DX12 phase=device_reset "
                      "count=%zu",
                      m_pendingCount );
        }

        m_diagnostics.Reset();
    }
    void ResetAfterShutdown()
    {

        if ( m_pendingCount != 0 )
        {
            SB_FATAL( "Dx12DeferredReleaseOwner",
                      "Retirement diagnostics reset crossed a live queue. owner=Rendering/DX12 phase=shutdown_reset "
                      "count=%zu",
                      m_pendingCount );
        }

        m_diagnostics.Reset();
    }
    bool Empty() const;
    size_t Count() const;
    size_t HighWater() const
    {
        return m_diagnostics.PendingHighWater();
    }

  private:
    friend struct Dx12DeferredReleaseOwnerTestAccess;
    std::array<DeferredResourceReleaseDX12, MAX_PENDING_RETIREMENTS> m_pending = {};
    size_t m_pendingCount = 0;
    Dx12RetirementDiagnosticState m_diagnostics;
};

struct Dx12PlatformProfilerGpuScopeDX12
{
    static constexpr size_t NAME_CHARS = 256;
    char name[NAME_CHARS] = {};
    uint32_t hash = 0;
};

// Capability: draw callers may enter a recording epoch, but cannot submit,
// wait, retire resources, or inspect fault/profiler state through this handle.
class Dx12DrawGate
{
  public:
    explicit Dx12DrawGate( Dx12FrameOwner& owner ) : m_owner( owner )
    {
    }
    bool PrepareDraw();
    bool PrepareFramebufferBind();
    bool PreparePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                              const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& rasterState );
    bool PrecompilePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                 const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& declaredRasterState );
    bool CanRecord() const;

  private:
    Dx12FrameOwner& m_owner;
};

// Capability: shader/dynamic-geometry callers may reserve and fill frame
// upload rows, but cannot reach submission, retirement, fault, or PIX policy.
class Dx12UploadReservations
{
  public:
    explicit Dx12UploadReservations( Dx12FrameOwner& owner ) : m_owner( owner )
    {
    }
    D3D12_GPU_VIRTUAL_ADDRESS
    ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category = RenderUploadCategory::TextureRows );
    D3D12_GPU_VIRTUAL_ADDRESS
    ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes, RenderUploadCategory vertexCategory );
    D3D12_GPU_VIRTUAL_ADDRESS ReserveConstantUpload( UINT64 size );
    void CancelPendingConstantUpload();
    uint8_t* UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const;

  private:
    Dx12FrameOwner& m_owner;
};

// Capability: resource wrappers may surrender one COM reference for
// fence-proven release; they cannot inspect or advance the retirement queue.
class Dx12ResourceRelease
{
  public:
    explicit Dx12ResourceRelease( Dx12FrameOwner& owner ) : m_owner( owner )
    {
    }
    void Retire( ID3D12Resource* resource );
    void Retire( ID3D12Resource* resource, UINT descriptorIndex );
    void Retire( ID3D12Resource* resource, UINT descriptorIndex, Dx12CpuDescriptorKind cpuKind, UINT cpuDescriptorIndex );
    void RetireStaticDescriptor( UINT descriptorIndex );

  private:
    Dx12FrameOwner& m_owner;
};

struct Dx12CaptureSubmitOutcome
{
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();
    bool readbackUseUncertain = false;
    const char* failedOperation = nullptr;
};

// Capability: screenshot capture may record against the current backbuffer and
// synchronously submit it, but cannot access uploads, retirement, descriptors,
// fault policy, or unrelated pipeline state.
class Dx12CaptureFrame
{
  public:
    explicit Dx12CaptureFrame( Dx12FrameOwner& owner ) : m_owner( owner )
    {
    }

    SkullbonezCore::Core::SbResult EnsureOpen();
    bool HasFailure() const;
    const SkullbonezCore::Core::SbResult& CurrentResult() const;
    RenderGraphResourceAccess BackBufferAccess() const;
    bool TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after );
    ID3D12Device* Device() const;
    ID3D12GraphicsCommandList* CommandList() const;
    ID3D12Resource* BackBuffer() const;
    Dx12CaptureSubmitOutcome SubmitAndWait();

  private:
    Dx12FrameOwner& m_owner;
};

// Capability: diagnostics may record timestamp queries, inspect only the frame
// fence timeline, and configure cold fault policy. It cannot submit work,
// advance frame indices, reach uploads/descriptors, or mutate pipeline state.
class Dx12DiagnosticsFrame
{
  public:
    explicit Dx12DiagnosticsFrame( Dx12FrameOwner& owner ) : m_owner( owner )
    {
    }
    SkullbonezCore::Core::SbResult EnsureOpen();
    bool CanRecord() const;
    ID3D12GraphicsCommandList* CommandList() const;
    bool FrameFenceReady() const;
    UINT64 CompletedFenceValue() const;
    SkullbonezCore::Core::SbResult WaitForFenceValue( UINT64 fenceValue ) const;
    void ConfigureFaultInjection( const char* token );

  private:
    Dx12FrameOwner& m_owner;
};

// Concept: one owner governs the complete command/frame epoch.
//
// It owns every state row whose invariant crosses Close, Execute, Signal, Wait,
// allocator reuse, PIX suspension, upload reuse, or deferred release. The three
// references are stable composition relationships, not rebindable context.
class Dx12FrameOwner
{
  public:

    // Why: two frame owners bound uncapped input-to-display latency and restore
    // the smoother camera pacing observed before the three-frame experiment.
    // Raise this to three only if profiling proves allocator-reuse waits are
    // limiting a GPU-heavy workload enough to justify the extra queued frame.
    static constexpr int FRAME_COUNT = 2;

    // Capacity: each frame owns 32 MiB, so the two-frame configuration reserves
    // 64 MiB. Steady runtime drops a bounded draw instead of growing this arena.
    static constexpr UINT64 UPLOAD_BUFFER_SIZE = 32ull * 1024ull * 1024ull;
    static constexpr int PROFILER_STACK_CAPACITY = 64;

    Dx12FrameOwner( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Dx12RenderDevice& device,
                    Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures, Dx12DescriptorHeaps& descriptors );

    Dx12DrawGate& DrawGate()
    {
        return m_drawGate;
    }
    Dx12UploadReservations& UploadReservations()
    {
        return m_uploadReservations;
    }
    Dx12ResourceRelease& ResourceRelease()
    {
        return m_resourceRelease;
    }
    Dx12CaptureFrame& CaptureFrame()
    {
        return m_captureFrame;
    }
    Dx12DiagnosticsFrame& DiagnosticsFrame()
    {
        return m_diagnosticsFrame;
    }
    SkullbonezCore::Core::SbResult EnsureOpen();

    // Presents one completed frame and advances the fence/allocator epoch.
    SkullbonezCore::Core::SbResult Present( Dx12Diagnostics& diagnostics );

    // Drains the current recording epoch, publishes completed timer readback,
    // then reopens command recording for the next runtime operation.
    SkullbonezCore::Core::SbResult FinishAndReopen( Dx12Diagnostics& diagnostics );

    // Runtime mutation drain: closes/submits/waits/reopens or returns Lane R.
    SkullbonezCore::Core::SbResult FlushGPU();

    // Terminal drain: proves release safety without reopening a failed epoch.
    SkullbonezCore::Core::SbResult DrainForResourceRelease();

    // Replaces swap-chain-sized resources only after a successful mutation drain.
    SkullbonezCore::Core::SbResult Resize( int width, int height );
    SkullbonezCore::Core::SbResult SubmitClosed();
    SkullbonezCore::Core::SbResult WaitForGpu();
    SkullbonezCore::Core::SbResult FlushUploadBuffer();
    SkullbonezCore::Core::SbResult CommitClose( HRESULT result, const char* operation );
    SkullbonezCore::Core::SbResult CommitWait( const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult RetainFailure( const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult RetainDeviceLoss( const char* operation, HRESULT result );
    SkullbonezCore::Core::SbResult SignalFrame( UINT64& outFenceValue );
    SkullbonezCore::Core::SbResult WaitForFrameFence( UINT64 fenceValue );

    // Capability targets: these methods implement the operation inside the
    // owner; capability subobjects only forward their restricted surface.
    bool PrepareDraw();
    bool PrepareFramebufferBind();
    bool PreparePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                              const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& rasterState );
    bool PrecompilePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                 const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& declaredRasterState );
    D3D12_GPU_VIRTUAL_ADDRESS
    ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category = RenderUploadCategory::TextureRows );
    D3D12_GPU_VIRTUAL_ADDRESS
    ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes, RenderUploadCategory vertexCategory );
    D3D12_GPU_VIRTUAL_ADDRESS ReserveConstantUpload( UINT64 size );
    void CancelPendingConstantUpload();
    uint8_t* UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const;
    uint64_t UploadFlushCount() const
    {
        return m_uploadFlushCount;
    }
    uint64_t UploadDropCount() const
    {
        return m_uploadDropCount;
    }
    const SkullbonezCore::Core::SbResult& CurrentResult() const
    {
        return m_recording.CurrentResult();
    }
    bool HasFailure() const
    {
        return m_recording.HasFailure();
    }
    bool CanRecord() const
    {
        return m_recording.CanRecord();
    }
    bool IsOpen() const
    {
        return m_recording.IsOpen();
    }
    bool DeviceHealthy() const
    {
        return m_deviceHealth.CanIssueDeviceWork();
    }
    bool DeviceLost() const
    {
        return m_deviceHealth.IsLost();
    }
    bool HasSubmittedWork() const
    {
        return m_submittedWork.HasSubmittedWork();
    }
    bool CanReleaseWithoutFence() const
    {
        return m_submittedWork.CanReleaseWithoutFence();
    }
    void AbandonSubmittedWork()
    {
        m_submittedWork.AbandonForRemovedDevice();
    }
    void ResetForDevice();
    void ResetAfterShutdown();
    ID3D12Resource*& RenderTarget( UINT index )
    {
        return m_renderTargets[index];
    }
    ID3D12Resource* RenderTarget( UINT index ) const
    {
        return m_renderTargets[index];
    }
    UINT FrameIndex() const
    {
        return m_frameIndex;
    }
    UINT AllocatorIndex() const
    {
        return m_allocatorIndex;
    }
    UINT64 FrameFenceValue( UINT index ) const
    {
        return m_frameFenceValues[index];
    }
    void SetFrameFenceValue( UINT index, UINT64 value )
    {
        m_frameFenceValues[index] = value;
    }
    void AdvanceFrameIndices();
    void RefreshFrameIndex();
    RenderGraphResourceAccess BackBufferAccess() const
    {
        return m_backBufferAccess;
    }
    void SetBackBufferAccess( RenderGraphResourceAccess access )
    {
        m_backBufferAccess = access;
    }
    bool TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after );
    Dx12DescriptorHeaps& Descriptors()
    {
        return m_descriptors;
    }
    const Dx12DescriptorHeaps& Descriptors() const
    {
        return m_descriptors;
    }
    Dx12FrameUploadSystem& Uploads()
    {
        return m_uploads;
    }
    const Dx12FrameUploadSystem& Uploads() const
    {
        return m_uploads;
    }
    void RetireResource( ID3D12Resource* resource );
    void RetireResource( ID3D12Resource* resource, UINT descriptorIndex );
    void RetireResource( ID3D12Resource* resource, UINT descriptorIndex, Dx12CpuDescriptorKind cpuKind,
                         UINT cpuDescriptorIndex );
    void RetireStaticDescriptor( UINT descriptorIndex );
    void AssignRetirementFence( UINT64 fenceValue )
    {
        m_retirement.AssignFence( fenceValue );
    }
    void ReleaseCompletedRetirements( bool releaseUnfenced );
    bool RetirementEmpty() const
    {
        return m_retirement.Empty();
    }
    size_t RetirementCount() const
    {
        return m_retirement.Count();
    }
    int ProfilerDepth() const
    {
        return m_profilerStackState.Depth();
    }
    Dx12PlatformProfilerGpuScopeDX12& ProfilerScope( int index )
    {
        return m_profilerScopes[index];
    }
    bool CommitProfilerBegin()
    {
        return m_profilerStackState.CommitBegin( PROFILER_STACK_CAPACITY );
    }
    bool CommitProfilerEnd()
    {
        return m_profilerStackState.CommitEnd();
    }
    void AssertProfilerClosed( const char* reason ) const;
    int SuspendProfilerForSubmit( const char* reason );
    void RestoreProfilerAfterSubmit( int suspendedDepth );
    void BeginProfilerEvent( const char* name, uint32_t hash );
    void EndProfilerEvent();
    ID3D12Device* Device() const;
    ID3D12GraphicsCommandList* CommandList() const;

    // Frame/output commands remain on the owner that already governs the
    // recording epoch and active pipeline target.
    void SetViewport( int x, int y, int width, int height );
    void Clear( const ClearTargetDesc& target );

  private:
    friend class Dx12DiagnosticsFrame;
    void WriteFaultProbe() const;
    bool PrepareUploadReservation( UINT64 size, UINT64 alignment, RenderUploadCategory category );

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Dx12RenderDevice& m_device;
    Dx12PipelineOwner& m_pipeline;
    Dx12TextureOwner& m_textures;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12CommandRecordingState m_recording;
    Dx12SubmittedWorkState m_submittedWork;
    Dx12DeviceHealthState m_deviceHealth;
    Dx12FaultInjectionState m_faultInjection;
    Dx12PlatformProfilerGpuStackState m_profilerStackState;
    std::array<Dx12PlatformProfilerGpuScopeDX12, PROFILER_STACK_CAPACITY> m_profilerScopes = {};
    Dx12FrameUploadSystem m_uploads;
    Dx12DeferredReleaseOwner m_retirement;
    ID3D12Resource* m_renderTargets[FRAME_COUNT] = {};
    UINT64 m_frameFenceValues[FRAME_COUNT] = {};
    UINT m_allocatorIndex = 0;
    UINT m_frameIndex = 0;
    RenderGraphResourceAccess m_backBufferAccess = RenderGraphResourceAccess::Present;
    D3D12_GPU_VIRTUAL_ADDRESS m_pendingConstantAddress = 0;
    UINT64 m_pendingConstantBytes = 0;
    uint64_t m_uploadFlushCount = 0;
    uint64_t m_uploadDropCount = 0;
    uint64_t m_uploadCategoryDropCount[RENDER_UPLOAD_CATEGORY_COUNT] = {};
    Dx12DrawGate m_drawGate;
    Dx12UploadReservations m_uploadReservations;
    Dx12ResourceRelease m_resourceRelease;
    Dx12CaptureFrame m_captureFrame;
    Dx12DiagnosticsFrame m_diagnosticsFrame;
};
} // namespace Rendering
} // namespace SkullbonezCore

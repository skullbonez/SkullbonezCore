/*
File: SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp
Purpose:
  Owns the DX12 device/presentation epoch, depth surface, fences, command
  allocators, and frame pacing.

Summary:
  Owns the device and presentation epoch: native device
  objects, published extent/generation, VSync/tearing policy,
  main depth surface, fences, command allocators, and frame pacing.

Glossary:
  Shader-visible descriptor heap: Descriptor table the GPU can index from bound
  root tables; rows must not be overwritten until the frame fence proves use is
  complete.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderDeviceDX12.h"
#include "../../Core/SbDiagnosticStore.h"
#include "RenderBackendDX12.CommandRecordingState.h"

#include "../../Core/FatalError.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <d3d12sdklayers.h>
#include <sstream>

namespace SkullbonezCore
{
namespace Rendering
{

static inline SkullbonezCore::Core::SbResult Dx12StartupResult( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                                HRESULT hr, const char* msg )
{

    if ( FAILED( hr ) )
    {

        // Lane R: adapter, driver, swap-chain, and Win32 event creation can fail
        // because of the host environment. Report the failing DX12 startup step
        // to the process bootstrap instead of escaping through an exception.
        return resultDiagnostics.Failure( "Rendering/DX12", "%s (HRESULT 0x%08X)", msg ? msg : "DX12 startup call failed",
                                          static_cast<unsigned int>( hr ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


static inline SkullbonezCore::Core::SbResult Dx12RuntimeResult( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                                HRESULT hr, const char* msg )
{

    if ( FAILED( hr ) )
    {
        return resultDiagnostics.Failure( "Rendering/DX12", "%s (HRESULT 0x%08X)", msg ? msg : "DX12 runtime call failed",
                                          static_cast<unsigned int>( hr ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

struct Dx12RenderDeviceInitRollback
{
    explicit Dx12RenderDeviceInitRollback( Dx12RenderDevice& device ) : target( device )
    {
    }

    ~Dx12RenderDeviceInitRollback()
    {

        if ( !committed )
        {
            target.Shutdown();
        }
    }

    void Commit()
    {
        committed = true;
    }

    Dx12RenderDevice& target;
    bool committed = false;
};

void EnableDx12DeviceRemovedDiagnostics()
{

    // DRED is Direct3D's "black box recorder" for device removal. A device can
    // be removed when the GPU hangs, the driver resets, or DX12 detects a serious
    // memory/access problem. Turning this on before device creation asks the
    // runtime to keep breadcrumbs and page-fault data so the crash report can
    // point back to named command lists and resources instead of leaving only a
    // generic "device lost" error.
    //
    // This is best-effort. Some systems may not expose the settings interface,
    // and that should not stop the renderer from starting. When it is available,
    // forcing it on is useful for validation builds because GPU failures are
    // otherwise very hard to connect back to the render pass or resource that
    // caused them.
    ID3D12DeviceRemovedExtendedDataSettings* settings = nullptr;

    if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &settings ) ) ) )
    {
        settings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
        settings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
        settings->Release();
    }
}


void NameDx12Object( ID3D12Object* object, const wchar_t* name )
{

    // SetName is a diagnostic label, not a rendering dependency. Ignore failure
    // because the engine should still run on systems where a debug-name call is
    // unavailable or rejected, but always try because PIX, DRED, and InfoQueue
    // messages become far easier for humans to read when objects are named.
    //
    // Example: without names, a device-removed report might only mention a
    // command-list pointer. With names, it can say "Skullbonez DX12 Main Command
    // List" or "Skullbonez DX12 Frame Upload Buffer 1", which is immediately
    // actionable.

    if ( object && name && name[0] != L'\0' )
    {
        object->SetName( name );
    }
}


void NameDx12ObjectIndexed( ID3D12Object* object, const wchar_t* prefix, UINT index )
{

    // Many DX12 objects come in small per-frame arrays. Give each object the
    // same readable prefix plus an index so debugging output can distinguish
    // "backbuffer 0" from "backbuffer 1" or "upload buffer 0" from "upload
    // buffer 1".
    wchar_t name[128] = {};
    const wchar_t* safePrefix = ( prefix && prefix[0] != L'\0' ) ? prefix : L"Skullbonez DX12 Object";
    swprintf_s( name, L"%ls %u", safePrefix, index );
    NameDx12Object( object, name );
}


void Dx12FenceTimeline::Init( ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE eventHandle )
{

    // The queue is where GPU work is submitted. The fence is the counter the
    // queue updates when that work reaches a known completion point. The event
    // is a normal Windows event used to put the CPU thread to sleep while it
    // waits for the fence instead of spinning in a loop.
    m_queue = queue;
    m_fence = fence;
    m_eventHandle = eventHandle;
    m_lastSignaledValue = 0;
}


void Dx12FenceTimeline::Reset()
{

    // Reset only forgets the borrowed pointers. The backend/device still owns
    // and releases the command queue, fence, and event handle.
    m_queue = nullptr;
    m_fence = nullptr;
    m_eventHandle = nullptr;
    m_lastSignaledValue = 0;
}


bool Dx12FenceTimeline::IsReady() const
{
    return m_queue && m_fence && m_eventHandle;
}


SkullbonezCore::Core::SbResult Dx12FenceTimeline::Signal( UINT64& outValue )
{
    outValue = 0;

    if ( !IsReady() )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 fence timeline used before Init." );
    }

    // Signal creates the next completion marker on the GPU timeline. The queue
    // does not write this value immediately. It writes the value after every
    // command submitted before this Signal() has finished on the GPU.
    // Lane R: queue/fence calls can fail because the device or driver is gone.
    // Keep the timeline value unchanged unless the marker was actually queued.
    const UINT64 value = m_lastSignaledValue + 1;
    const SkullbonezCore::Core::SbResult signalResult = Dx12RuntimeResult( m_resultDiagnostics,
                                                                           m_queue->Signal( m_fence, value ),
                                                                           "DX12 command queue Signal failed" );

    if ( !signalResult.Ok() )
    {
        return signalResult;
    }

    m_lastSignaledValue = value;
    outValue = value;
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12FenceTimeline::WaitForValue( UINT64 value ) const
{

    if ( value == 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( !IsReady() )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 fence timeline used before Init." );
    }

    // GetCompletedValue is the non-blocking check. If the GPU has already
    // reached this marker, the CPU can continue immediately. Otherwise, ask the
    // fence to fire the Windows event when it reaches the value and sleep until
    // that happens.

    if ( m_fence->GetCompletedValue() < value )
    {
        const SkullbonezCore::Core::SbResult eventResult = Dx12RuntimeResult( m_resultDiagnostics,
                                                                              m_fence->SetEventOnCompletion( value,
                                                                                                             m_eventHandle ),
                                                                              "DX12 fence SetEventOnCompletion failed" );

        if ( !eventResult.Ok() )
        {
            return eventResult;
        }

        const DWORD waitResult = WaitForSingleObject( m_eventHandle, INFINITE );

        if ( waitResult != WAIT_OBJECT_0 )
        {
            return m_resultDiagnostics.Failure( "Rendering/DX12", "DX12 fence wait failed (wait result 0x%08X)",
                                                static_cast<unsigned int>( waitResult ) );
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}


UINT64 Dx12FenceTimeline::CompletedValue() const
{

    if ( !m_fence )
    {
        return 0;
    }

    return m_fence->GetCompletedValue();
}


void Dx12CpuDescriptorAllocator::Init( ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT capacity, const char* heapName )
{

    if ( !heap || descriptorSize == 0 || capacity == 0 || capacity > MAX_TRACKED_CPU_DESCRIPTORS )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 CPU descriptor allocator received invalid heap geometry. descriptorSize=%u capacity=%u",
                  descriptorSize, capacity );
    }

    // Concept: RTV and DSV descriptor heaps are CPU-side tables of view rows.
    //
    // RTV means Render Target View: a row that lets the output merger write
    // color pixels into a texture. DSV means Depth Stencil View: a row that lets
    // depth testing read/write depth and stencil data. The device tells us the
    // byte stride between rows because descriptor sizes are implementation
    // details, not C++ struct sizes the engine can hard-code.
    m_heap = heap;
    m_descriptorSize = descriptorSize;
    m_capacity = capacity;
    m_next = 0;
    m_used = 0;
    m_freeCount = 0;
    std::fill_n( m_free, MAX_TRACKED_CPU_DESCRIPTORS, 0u );
    std::fill_n( m_allocated, MAX_TRACKED_CPU_DESCRIPTORS, false );
    m_heapName = ( heapName && heapName[0] != '\0' ) ? heapName : "unknown";
}


void Dx12CpuDescriptorAllocator::Reset()
{
    m_heap = nullptr;
    m_descriptorSize = 0;
    m_capacity = 0;
    m_next = 0;
    m_used = 0;
    m_freeCount = 0;
    std::fill_n( m_free, MAX_TRACKED_CPU_DESCRIPTORS, 0u );
    std::fill_n( m_allocated, MAX_TRACKED_CPU_DESCRIPTORS, false );
    m_heapName = "unknown";
}


Dx12CpuDescriptorAllocation Dx12CpuDescriptorAllocator::Allocate()
{

    if ( !m_heap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor allocator used before Init." );
    }

    UINT index = UINT_MAX;

    if ( m_freeCount > 0 )
    {
        index = m_free[--m_freeCount];
    }
    else if ( m_next < m_capacity )
    {
        index = m_next++;
    }

    if ( index == UINT_MAX )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor heap exhausted. heap=%s used=%u capacity=%u", m_heapName, m_used,
                  m_capacity );
    }

    // Allocation means "reserve the next unused row in the descriptor table."
    // It does not touch the resource itself. The caller will write the actual
    // view record with CreateRenderTargetView or CreateDepthStencilView.
    Dx12CpuDescriptorAllocation allocation;
    allocation.index = index;
    allocation.cpuHandle = CpuHandle( allocation.index );
    m_allocated[index] = true;
    ++m_used;
    return allocation;
}


void Dx12CpuDescriptorAllocator::Free( UINT index )
{

    if ( index >= m_capacity || !m_allocated[index] || m_freeCount >= m_capacity || m_used == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor free rejected. heap=%s index=%u used=%u capacity=%u", m_heapName,
                  index, m_used, m_capacity );
    }

    m_allocated[index] = false;
    --m_used;
    m_free[m_freeCount++] = index;
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12CpuDescriptorAllocator::CpuHandle( UINT index ) const
{

    if ( !m_heap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor heap unavailable." );
    }

    if ( index >= m_capacity )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor index out of range. index=%u capacity=%u", index, m_capacity );
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>( index ) * m_descriptorSize;
    return handle;
}


Dx12CpuDescriptorAllocatorStats Dx12CpuDescriptorAllocator::GetStats() const
{
    Dx12CpuDescriptorAllocatorStats stats;
    stats.heapName = m_heapName;
    stats.capacity = m_capacity;
    stats.used = m_used;
    return stats;
}


void Dx12DescriptorAllocator::Init( ID3D12DescriptorHeap* shaderVisibleHeap, ID3D12DescriptorHeap* stagingHeap,
                                    UINT descriptorSize, UINT staticCapacity, UINT transientCapacityPerFrame,
                                    UINT frameCount )
{
    const UINT64 shaderVisibleCapacity = static_cast<UINT64>( staticCapacity ) +
                                         ( static_cast<UINT64>( transientCapacityPerFrame ) * frameCount );

    if ( !shaderVisibleHeap || !stagingHeap || descriptorSize == 0 || staticCapacity == 0 ||
         transientCapacityPerFrame == 0 || frameCount == 0 || shaderVisibleCapacity > 0xffffffffull )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "Invalid DX12 descriptor allocator init description. descriptor_size=%u static_capacity=%u "
                  "transient_capacity_per_frame=%u frame_count=%u shader_visible_capacity=%llu",
                  descriptorSize, staticCapacity, transientCapacityPerFrame, frameCount,
                  static_cast<unsigned long long>( shaderVisibleCapacity ) );
    }

    if ( staticCapacity > MAX_TRACKED_STATIC_DESCRIPTORS )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 static descriptor tracking capacity exceeded. requested=%u tracking_capacity=%u",
                  staticCapacity, MAX_TRACKED_STATIC_DESCRIPTORS );
    }

    // The allocator only stores borrowed heap pointers and the table geometry.
    // "descriptorSize" is the byte stride between heap rows. Different heap
    // types can have different descriptor sizes, so DX12 requires us to query it
    // from the device instead of assuming sizeof(some struct).
    m_shaderVisibleHeap = shaderVisibleHeap;
    m_stagingHeap = stagingHeap;
    m_descriptorSize = descriptorSize;
    m_staticCapacity = staticCapacity;
    m_transientCapacityPerFrame = transientCapacityPerFrame;
    m_frameCount = frameCount;
    m_nextStatic = 0;
    m_staticUsed = 0;
    m_staticHighWater = 0;
    m_freeStaticCount = 0;
    std::fill_n( m_freeStatic, MAX_TRACKED_STATIC_DESCRIPTORS, 0u );
    std::fill_n( m_staticAllocated, MAX_TRACKED_STATIC_DESCRIPTORS, false );
    m_nextTransientInFrame = 0;
    m_transientPeakThisRun = 0;
    m_currentFrame = 0;
}


void Dx12DescriptorAllocator::Reset()
{
    m_shaderVisibleHeap = nullptr;
    m_stagingHeap = nullptr;
    m_descriptorSize = 0;
    m_staticCapacity = 0;
    m_transientCapacityPerFrame = 0;
    m_frameCount = 0;
    m_nextStatic = 0;
    m_staticUsed = 0;
    m_staticHighWater = 0;
    m_freeStaticCount = 0;
    std::fill_n( m_freeStatic, MAX_TRACKED_STATIC_DESCRIPTORS, 0u );
    std::fill_n( m_staticAllocated, MAX_TRACKED_STATIC_DESCRIPTORS, false );
    m_nextTransientInFrame = 0;
    m_transientPeakThisRun = 0;
    m_currentFrame = 0;
}


void Dx12DescriptorAllocator::ResetFrame( UINT frameIndex )
{

    if ( m_frameCount == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 descriptor allocator used before Init." );
    }

    if ( frameIndex >= m_frameCount )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 descriptor allocator frame index out of range. frameIndex=%u frameCount=%u",
                  frameIndex, m_frameCount );
    }

    // The backend calls this only after the fence for this frame allocator has
    // completed. That means every command list that used this frame's transient
    // descriptor range is done, so it is safe for the CPU to refill the same
    // descriptor slots for the next frame recorded with this allocator.
    //
    // The important idea: the CPU and GPU run at different times. ResetFrame()
    // is not just clearing a counter; it is declaring that the GPU has finished
    // reading every descriptor in this frame's temporary range.
    m_currentFrame = frameIndex;
    m_nextTransientInFrame = 0;
}


UINT Dx12DescriptorAllocator::AllocateStatic()
{

    // Lifetime: freed rows enter this list only after the frame retirement
    // fence completes, so reuse cannot change a descriptor still visible to an
    // in-flight command list.
    UINT index = UINT_MAX;

    if ( m_freeStaticCount > 0 )
    {
        index = m_freeStatic[--m_freeStaticCount];
    }
    else if ( m_nextStatic < m_staticCapacity )
    {
        index = m_nextStatic++;
    }

    if ( index == UINT_MAX )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 static SRV heap exhausted. owner=Rendering/DX12 used=%u capacity=%u high_water=%u", m_staticUsed,
                  m_staticCapacity, m_staticHighWater );
    }

    if ( m_staticAllocated[index] )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 static SRV free-list returned an allocated row. index=%u", index );
    }

    m_staticAllocated[index] = true;
    ++m_staticUsed;
    m_staticHighWater = (std::max)( m_staticHighWater, m_staticUsed );
    return index;
}


void Dx12DescriptorAllocator::FreeStatic( UINT index )
{

    if ( index >= m_staticCapacity || !m_staticAllocated[index] )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 static SRV free rejected. index=%u capacity=%u allocated=%u", index,
                  m_staticCapacity,
                  index < MAX_TRACKED_STATIC_DESCRIPTORS ? static_cast<UINT>( m_staticAllocated[index] ) : 0u );
    }

    if ( m_freeStaticCount >= m_staticCapacity || m_staticUsed == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 static SRV free-list accounting corrupt. free=%u used=%u capacity=%u",
                  m_freeStaticCount, m_staticUsed, m_staticCapacity );
    }

    m_staticAllocated[index] = false;
    --m_staticUsed;
    m_freeStatic[m_freeStaticCount++] = index;
}


UINT Dx12DescriptorAllocator::AllocateTransient()
{

    // Transient slots are short-lived copies into the shader-visible heap.
    // They are cheap to allocate during command recording, but the CPU must not
    // reuse a slot until the GPU has finished every command list that saw it.
    // The per-frame range below gives each frame allocator its own descriptor
    // block, matching the command allocator and upload arena lifetime.
    //
    // The returned index is still a descriptor-table row number. It is not a
    // texture handle and it is not a pointer. The caller usually copies an
    // existing static descriptor into this row, then binds this row's GPU handle
    // for the draw or dispatch being recorded.

    if ( m_nextTransientInFrame >= m_transientCapacityPerFrame )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 transient SRV heap exhausted for current frame allocator. frame=%u used=%u "
                  "capacity_per_frame=%u",
                  m_currentFrame, m_nextTransientInFrame, m_transientCapacityPerFrame );
    }

    const UINT index = m_staticCapacity + ( m_currentFrame * m_transientCapacityPerFrame ) + m_nextTransientInFrame;
    ++m_nextTransientInFrame;
    m_transientPeakThisRun = (std::max)( m_transientPeakThisRun, m_nextTransientInFrame );
    return index;
}


UINT Dx12DescriptorAllocator::AllocateTransientRange( UINT count )
{

    if ( count == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 transient descriptor range count must be greater than zero." );
    }

    if ( m_frameCount == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 descriptor allocator used before Init." );
    }

    if ( count > m_transientCapacityPerFrame || m_nextTransientInFrame > m_transientCapacityPerFrame - count )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 transient SRV range exhausted for current frame allocator. frame=%u requested=%u used=%u "
                  "capacity_per_frame=%u",
                  m_currentFrame, count, m_nextTransientInFrame, m_transientCapacityPerFrame );
    }

    const UINT index = m_staticCapacity + ( m_currentFrame * m_transientCapacityPerFrame ) + m_nextTransientInFrame;
    m_nextTransientInFrame += count;
    m_transientPeakThisRun = (std::max)( m_transientPeakThisRun, m_nextTransientInFrame );
    return index;
}


UINT Dx12DescriptorAllocator::ShaderVisibleCapacity() const
{
    return static_cast<UINT>( static_cast<UINT64>( m_staticCapacity ) +
                              ( static_cast<UINT64>( m_transientCapacityPerFrame ) * m_frameCount ) );
}


void Dx12DescriptorAllocator::ValidateShaderVisibleIndex( UINT index, const char* context ) const
{
    const UINT capacity = ShaderVisibleCapacity();

    if ( index >= capacity )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 shader-visible descriptor index out of range. context=%s index=%u capacity=%u "
                  "static_capacity=%u transient_capacity_per_frame=%u frame_count=%u",
                  context ? context : "unknown", index, capacity, m_staticCapacity, m_transientCapacityPerFrame,
                  m_frameCount );
    }
}


void Dx12DescriptorAllocator::ValidateStagingIndex( UINT index, const char* context ) const
{

    if ( index >= m_staticCapacity )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 staging descriptor index out of range. context=%s index=%u static_capacity=%u",
                  context ? context : "unknown", index, m_staticCapacity );
    }
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorAllocator::ShaderVisibleCpuHandle( UINT index ) const
{

    // CPU handles are used when the engine writes or copies a descriptor into a
    // heap slot. GPU handles are separate because shaders see GPU addresses, not
    // CPU pointers.
    //
    // The CPU handle is for setup work only. The shader cannot use it. Common
    // uses are CreateShaderResourceView, CreateUnorderedAccessView, or
    // CopyDescriptorsSimple into the shader-visible heap.

    if ( !m_shaderVisibleHeap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 shader-visible descriptor heap unavailable." );
    }

    ValidateShaderVisibleIndex( index, "shader-visible CPU handle" );

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>( index ) * m_descriptorSize;
    return handle;
}


D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorAllocator::ShaderVisibleGpuHandle( UINT index ) const
{

    // The GPU handle is the value bound to a root descriptor table. A shader
    // follows this handle to find the descriptor that describes the texture or
    // UAV it should read or write.
    //
    // This value is what root descriptor tables use. Once bound, command lists
    // can execute later on the GPU, so the descriptor row it points at must not
    // be overwritten until the frame fence says that GPU work is complete.

    if ( !m_shaderVisibleHeap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 shader-visible descriptor heap unavailable." );
    }

    ValidateShaderVisibleIndex( index, "shader-visible GPU handle" );

    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_shaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>( index ) * m_descriptorSize;
    return handle;
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorAllocator::StagingCpuHandle( UINT index ) const
{

    // Staging handles point to CPU-only descriptor rows. Keeping persistent
    // descriptors here gives the renderer a stable template it can copy into
    // transient shader-visible rows without reconstructing the view description
    // every frame.

    if ( !m_stagingHeap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 staging descriptor heap unavailable." );
    }

    ValidateStagingIndex( index, "staging CPU handle" );

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_stagingHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>( index ) * m_descriptorSize;
    return handle;
}


void Dx12DescriptorAllocator::PublishStaticDescriptor( ID3D12Device* device, UINT index ) const
{

    if ( !device )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 static descriptor publication requires a device." );
    }

    ValidateStagingIndex( index, "bindless static descriptor publication" );

    // Lifetime: the same static index is written once for a live resource and
    // reused only after the frame retirement fence. Direct heap indexing is
    // therefore no weaker than the old staging-to-transient copy lifetime.
    device->CopyDescriptorsSimple( 1, ShaderVisibleCpuHandle( index ), StagingCpuHandle( index ),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


Dx12DescriptorAllocatorStats Dx12DescriptorAllocator::GetStats() const
{
    Dx12DescriptorAllocatorStats stats;
    stats.staticCapacity = m_staticCapacity;
    stats.staticUsed = m_staticUsed;
    stats.staticHighWater = m_staticHighWater;
    stats.transientCapacityPerFrame = m_transientCapacityPerFrame;
    stats.transientUsedThisFrame = m_nextTransientInFrame;
    stats.transientPeakThisRun = m_transientPeakThisRun;
    stats.currentFrame = m_currentFrame;
    return stats;
}


void Dx12UploadArena::Init( ID3D12Resource* resource, uint8_t* mappedPtr, UINT64 capacityBytes )
{

    // Upload buffers are created once and kept mapped. Persistent mapping is a
    // normal DX12 upload-heap pattern: the CPU writes through mappedPtr, and the
    // GPU later reads the same bytes through resource->GetGPUVirtualAddress().
    m_resource = resource;
    m_mappedPtr = mappedPtr;
    m_capacityBytes = capacityBytes;
    m_currentOffset = 0;
    m_peakBytes = 0;
    std::fill_n( m_categoryUsedBytes, RENDER_UPLOAD_CATEGORY_COUNT, UINT64 { 0 } );
    std::fill_n( m_categoryPeakBytes, RENDER_UPLOAD_CATEGORY_COUNT, UINT64 { 0 } );
}


void Dx12UploadArena::Reset()
{
    m_resource = nullptr;
    m_mappedPtr = nullptr;
    m_capacityBytes = 0;
    m_currentOffset = 0;
    m_peakBytes = 0;
    std::fill_n( m_categoryUsedBytes, RENDER_UPLOAD_CATEGORY_COUNT, UINT64 { 0 } );
    std::fill_n( m_categoryPeakBytes, RENDER_UPLOAD_CATEGORY_COUNT, UINT64 { 0 } );
}


void Dx12UploadArena::ResetFrame()
{

    // Resetting the byte cursor is safe only after the backend has waited for
    // this frame allocator's fence. At that point, the GPU can no longer read
    // old constants or dynamic vertices from this upload buffer.
    //
    // ResetFrame() does not clear memory. It only says future allocations may
    // reuse the bytes from the beginning because the old bytes are no longer in
    // flight on the GPU.
    m_currentOffset = 0;
    std::fill_n( m_categoryUsedBytes, RENDER_UPLOAD_CATEGORY_COUNT, UINT64 { 0 } );
}


bool Dx12UploadArena::CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const
{

    // The backend uses this probe to decide whether it must submit and wait
    // before recording more upload-heavy work. It prevents wraparound writes
    // into bytes already used earlier in the same command list.
    //
    // Returning false is not automatically fatal. The backend may submit the
    // current command list, wait for the GPU, reset the arena, and continue.
    return CanReserveDx12UploadRange( m_currentOffset, m_capacityBytes, sizeBytes, alignment );
}


D3D12_GPU_VIRTUAL_ADDRESS
Dx12UploadArena::Allocate( UINT64 sizeBytes, UINT64 alignment, RenderUploadCategory category )
{

    if ( !m_resource || !m_mappedPtr )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload arena used before Init." );
    }

    // DX12 resources often require aligned addresses. For example, constant
    // buffer views must start on 256-byte boundaries. The caller specifies the
    // rule it needs, and the arena advances to the next legal byte offset.
    //
    // The returned value is deliberately a GPU address because that is what the
    // draw/copy commands need. Call GetMappedPtr() with that address when CPU
    // code needs to fill the allocation.
    const UINT64 alignedOffset = AlignOffset( m_currentOffset, alignment );

    if ( !CanReserveDx12UploadRange( m_currentOffset, m_capacityBytes, sizeBytes, alignment ) )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload buffer exhausted. requested=%llu alignedOffset=%llu capacity=%llu",
                  static_cast<unsigned long long>( sizeBytes ), static_cast<unsigned long long>( alignedOffset ),
                  static_cast<unsigned long long>( m_capacityBytes ) );
    }

    m_currentOffset = alignedOffset + sizeBytes;
    m_peakBytes = (std::max)( m_peakBytes, m_currentOffset );
    const std::size_t categoryIndex = static_cast<std::size_t>( category );

    if ( categoryIndex >= RENDER_UPLOAD_CATEGORY_COUNT )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload allocation used an invalid category. category=%zu", categoryIndex );
    }

    m_categoryUsedBytes[categoryIndex] += sizeBytes;
    m_categoryPeakBytes[categoryIndex] = (std::max)( m_categoryPeakBytes[categoryIndex],
                                                     m_categoryUsedBytes[categoryIndex] );

    return m_resource->GetGPUVirtualAddress() + alignedOffset;
}


uint8_t* Dx12UploadArena::GetMappedPtr( D3D12_GPU_VIRTUAL_ADDRESS address ) const
{

    if ( !m_resource || !m_mappedPtr )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload arena used before Init." );
    }

    // Allocation returns a GPU virtual address because that is what command
    // lists bind. To fill the bytes from the CPU, translate the GPU address
    // back into an offset from the persistently mapped upload pointer.
    //
    // This translation works only for addresses allocated from this arena. The
    // bounds checks below catch accidental use of an address from another upload
    // buffer or a stale address after the arena has been reset.
    const D3D12_GPU_VIRTUAL_ADDRESS base = m_resource->GetGPUVirtualAddress();

    if ( address < base )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload address outside current frame arena. address=%llu base=%llu",
                  static_cast<unsigned long long>( address ), static_cast<unsigned long long>( base ) );
    }

    const UINT64 offset = address - base;

    if ( offset >= m_capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload address outside current frame arena. offset=%llu capacity=%llu",
                  static_cast<unsigned long long>( offset ), static_cast<unsigned long long>( m_capacityBytes ) );
    }

    return m_mappedPtr + offset;
}


Dx12UploadArenaStats Dx12UploadArena::GetStats() const
{
    Dx12UploadArenaStats stats;
    stats.capacityBytes = m_capacityBytes;
    stats.usedBytes = m_currentOffset;
    stats.peakBytes = m_peakBytes;
    std::copy_n( m_categoryUsedBytes, RENDER_UPLOAD_CATEGORY_COUNT, stats.categoryUsedBytes );
    std::copy_n( m_categoryPeakBytes, RENDER_UPLOAD_CATEGORY_COUNT, stats.categoryPeakBytes );
    return stats;
}


UINT64 Dx12UploadArena::AlignOffset( UINT64 offset, UINT64 alignment ) const
{

    // Alignment means "start this allocation at an address divisible by N." The
    // formula below rounds up to the next legal byte offset. When alignment is 1
    // or 0, every byte position is legal, so no rounding is needed.
    return AlignDx12UploadOffset( offset, alignment );
}


Dx12FrameUploadSystem::~Dx12FrameUploadSystem()
{
    Shutdown();
}


bool Dx12FrameUploadSystem::Init( ID3D12Device* device, UINT frameCount, UINT64 capacityBytes, UINT64 persistentTailBytes,
                                  const wchar_t* debugNamePrefix )
{
    Shutdown();

    if ( !device || frameCount == 0 || frameCount > MAX_FRAME_COUNT || capacityBytes == 0 ||
         persistentTailBytes >= capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12", "Invalid DX12 frame upload system init description. frameCount=%u capacityBytes=%llu",
                  frameCount, static_cast<unsigned long long>( capacityBytes ) );
    }

    m_frameCount = frameCount;
    m_capacityBytes = capacityBytes;
    m_persistentTailBytes = persistentTailBytes;

    const wchar_t* safeName = ( debugNamePrefix && debugNamePrefix[0] != L'\0' ) ? debugNamePrefix
                                                                                 : L"Skullbonez DX12 Frame Upload Buffer";

    for ( UINT i = 0; i < frameCount; ++i )
    {
        D3D12_HEAP_PROPERTIES heapProps = {};

        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = capacityBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        const HRESULT createResult = device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                      IID_PPV_ARGS( &m_resources[i] ) );

        if ( FAILED( createResult ) )
        {
            Shutdown();
            return false;
        }

        NameDx12ObjectIndexed( m_resources[i], safeName, i );

        // Why: ID3D12Resource::Map writes through the native void-pointer ABI;
        // ValidateDx12MappedPointer immediately publishes typed upload bytes.
        void* mappedPointer = nullptr;
        const HRESULT mapResult = m_resources[i]->Map( 0, nullptr, &mappedPointer );
        const Dx12MappedPointerResult checkedMap = ValidateDx12MappedPointer( m_resultDiagnostics, mapResult, mappedPointer,
                                                                              "frame upload resource Map" );

        if ( !checkedMap.result.Ok() )
        {
            Shutdown();
            return false;
        }

        m_mappedPtrs[i] = checkedMap.bytes;

        // The arena owns ordinary frame byte-range accounting. Its fixed tail
        // is excluded so retained GPU commands can survive ResetFrame without
        // allocating another upload heap.
        m_arenas[i].Init( m_resources[i], m_mappedPtrs[i], capacityBytes - persistentTailBytes );
    }

    return true;
}


void Dx12FrameUploadSystem::Shutdown()
{

    for ( UINT i = 0; i < MAX_FRAME_COUNT; ++i )
    {

        if ( m_resources[i] )
        {
            m_resources[i]->Unmap( 0, nullptr );
            m_resources[i]->Release();
            m_resources[i] = nullptr;
        }

        m_mappedPtrs[i] = nullptr;
        m_arenas[i].Reset();
    }

    m_frameCount = 0;
    m_capacityBytes = 0;
    m_persistentTailBytes = 0;
}


void Dx12FrameUploadSystem::ResetFrame( UINT frameIndex )
{
    ValidateFrameIndex( frameIndex );
    m_arenas[frameIndex].ResetFrame();
}


bool Dx12FrameUploadSystem::CanAllocate( UINT frameIndex, UINT64 sizeBytes, UINT64 alignment ) const
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].CanAllocate( sizeBytes, alignment );
}


D3D12_GPU_VIRTUAL_ADDRESS
Dx12FrameUploadSystem::Allocate( UINT frameIndex, UINT64 sizeBytes, UINT64 alignment, RenderUploadCategory category )
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].Allocate( sizeBytes, alignment, category );
}


uint8_t* Dx12FrameUploadSystem::GetMappedPtr( UINT frameIndex, D3D12_GPU_VIRTUAL_ADDRESS address ) const
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].GetMappedPtr( address );
}


UINT64 Dx12FrameUploadSystem::OffsetFromAddress( UINT frameIndex, D3D12_GPU_VIRTUAL_ADDRESS address ) const
{
    ValidateFrameIndex( frameIndex );
    ID3D12Resource* resource = m_arenas[frameIndex].Resource();

    if ( !resource )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload resource unavailable." );
    }

    const D3D12_GPU_VIRTUAL_ADDRESS base = resource->GetGPUVirtualAddress();

    if ( address < base )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload address is before current frame resource. address=%llu base=%llu",
                  static_cast<unsigned long long>( address ), static_cast<unsigned long long>( base ) );
    }

    const UINT64 offset = address - base;

    if ( offset >= m_capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 upload address is outside current frame resource. offset=%llu capacity=%llu",
                  static_cast<unsigned long long>( offset ), static_cast<unsigned long long>( m_capacityBytes ) );
    }

    return offset;
}


ID3D12Resource* Dx12FrameUploadSystem::Resource( UINT frameIndex ) const
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].Resource();
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameUploadSystem::PersistentTailAddress( UINT frameIndex ) const
{
    ValidateFrameIndex( frameIndex );
    return m_resources[frameIndex]->GetGPUVirtualAddress() + ( m_capacityBytes - m_persistentTailBytes );
}


uint8_t* Dx12FrameUploadSystem::PersistentTailPointer( UINT frameIndex ) const
{
    ValidateFrameIndex( frameIndex );
    return m_mappedPtrs[frameIndex] + ( m_capacityBytes - m_persistentTailBytes );
}


Dx12UploadArenaStats Dx12FrameUploadSystem::GetStats( UINT frameIndex ) const
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].GetStats();
}


void Dx12FrameUploadSystem::ValidateFrameIndex( UINT frameIndex ) const
{

    if ( frameIndex >= m_frameCount )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 frame upload index out of range. frameIndex=%u frameCount=%u", frameIndex,
                  m_frameCount );
    }
}


Dx12ReadbackBuffer::~Dx12ReadbackBuffer()
{
    Reset();
}


bool Dx12ReadbackBuffer::InitBuffer( ID3D12Device* device, UINT64 sizeBytes, const wchar_t* debugName )
{
    Reset();

    if ( !device || sizeBytes == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "Invalid DX12 readback buffer init description. sizeBytes=%llu",
                  static_cast<unsigned long long>( sizeBytes ) );
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Buffers are effectively COMMON on readback heaps. The CPU reads by Map()
    // after a fence proves the GPU copy/resolve has completed.
    const HRESULT hr = device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr, IID_PPV_ARGS( &m_resource ) );

    if ( FAILED( hr ) )
    {
        m_resource = nullptr;
        m_sizeBytes = 0;
        return false;
    }

    NameDx12Object( m_resource, debugName ? debugName : L"Skullbonez DX12 Readback Buffer" );
    m_sizeBytes = sizeBytes;
    return true;
}


void Dx12ReadbackBuffer::Reset()
{

    if ( m_resource )
    {
        m_resource->Release();
        m_resource = nullptr;
    }

    m_sizeBytes = 0;
}


const uint8_t* Dx12ReadbackBuffer::MapRead( UINT64 sizeBytes ) const
{

    if ( !m_resource )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 readback buffer unavailable." );
    }

    if ( sizeBytes > m_sizeBytes )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 readback map range exceeds buffer size. requested=%llu capacity=%llu",
                  static_cast<unsigned long long>( sizeBytes ), static_cast<unsigned long long>( m_sizeBytes ) );
    }

    // Why: ID3D12Resource::Map writes through the native void-pointer ABI. The
    // readback owner narrows it immediately and returns immutable bytes.
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>( sizeBytes ) };

    // Lane R: Map can fail after device removal or readback-memory pressure.
    // Callers own the report boundary, so return null instead of unwinding.

    if ( FAILED( m_resource->Map( 0, &readRange, &mappedData ) ) )
    {
        return nullptr;
    }

    return static_cast<const uint8_t*>( mappedData );
}


void Dx12ReadbackBuffer::UnmapNoWrite() const
{

    if ( !m_resource )
    {
        return;
    }

    // The CPU only reads from this buffer. Passing an empty write range tells
    // the runtime there are no CPU-written bytes to flush back toward the GPU.
    D3D12_RANGE writeRange = { 0, 0 };
    m_resource->Unmap( 0, &writeRange );
}


ID3D12Resource* Dx12ReadbackBuffer::DetachAfterUncertainSubmission()
{
    ID3D12Resource* detached = m_resource;
    m_resource = nullptr;
    m_sizeBytes = 0;
    return detached;
}


Dx12ReadbackBufferStats Dx12ReadbackBuffer::GetStats() const
{
    Dx12ReadbackBufferStats stats;
    stats.sizeBytes = m_sizeBytes;
    stats.ready = m_resource != nullptr;
    return stats;
}


Dx12RenderDevice::~Dx12RenderDevice()
{
    Shutdown();
}


SkullbonezCore::Core::SbResult Dx12RenderDevice::Init( const Dx12RenderDeviceInitDesc& desc )
{
    Shutdown();

    if ( !desc.hwnd || desc.width == 0 || desc.height == 0 || desc.frameCount == 0 || desc.frameCount > MAX_FRAME_COUNT )
    {
        SB_FATAL( "RenderDeviceDX12", "Invalid DX12 render device init description. width=%u height=%u frameCount=%u",
                  desc.width, desc.height, desc.frameCount );
    }

    Dx12RenderDeviceInitRollback rollback( *this );
    m_frameCount = desc.frameCount;
    m_allocatorIndex = 0;

    // DRED must be enabled before the D3D12 device is created. Think of DRED as
    // the GPU crash recorder: if the driver removes the device, the report can
    // include breadcrumbs for named queues, command lists, and resources.
    EnableDx12DeviceRemovedDiagnostics();

    UINT factoryFlags = 0;
    {
        ID3D12Debug* debugController = nullptr;

        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
            debugController->Release();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    // The DXGI factory is the Windows-facing graphics object. It creates the
    // swap chain and answers platform questions such as "can this swap chain
    // present without VSync tearing restrictions?"
    SkullbonezCore::Core::SbResult startupResult = Dx12StartupResult( m_resultDiagnostics,
                                                                      CreateDXGIFactory2( factoryFlags,
                                                                                          IID_PPV_ARGS( &m_factory ) ),
                                                                      "CreateDXGIFactory2 failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    {
        IDXGIFactory5* factory5 = nullptr;
        BOOL allowTearing = FALSE;

        if ( SUCCEEDED( m_factory->QueryInterface( IID_PPV_ARGS( &factory5 ) ) ) )
        {

            if ( FAILED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                        sizeof( allowTearing ) ) ) )
            {
                allowTearing = FALSE;
            }

            factory5->Release();
        }

        m_allowTearing = allowTearing == TRUE;
    }

    // The D3D12 device is the factory for GPU resources, descriptor heaps, root
    // signatures, PSOs, fences, and command objects. The backend borrows this
    // pointer, but this device layer owns its COM lifetime.
    startupResult = Dx12StartupResult( m_resultDiagnostics,
                                       D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ),
                                       "D3D12CreateDevice failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    NameDx12Object( m_device, L"Skullbonez DX12 Device" );

    {
        ID3D12InfoQueue* infoQueue = nullptr;

        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &infoQueue ) ) ) )
        {
            infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );
            infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, TRUE );

            D3D12_MESSAGE_SEVERITY denySeverities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter = {};

            filter.DenyList.NumSeverities = _countof( denySeverities );
            filter.DenyList.pSeverityList = denySeverities;
            infoQueue->PushStorageFilter( &filter );
            infoQueue->Release();
        }
    }

    // The graphics queue is where finished command lists are submitted. The CPU
    // records work into a command list; the queue is the doorway to the GPU.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    startupResult = Dx12StartupResult( m_resultDiagnostics,
                                       m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ),
                                       "CreateCommandQueue failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    NameDx12Object( m_commandQueue, L"Skullbonez DX12 Graphics Queue" );

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = desc.frameCount;
    scDesc.Width = desc.width;
    scDesc.Height = desc.height;
    scDesc.Format = desc.backBufferFormat;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;
    scDesc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    IDXGISwapChain1* swapChain1 = nullptr;
    startupResult = Dx12StartupResult( m_resultDiagnostics,
                                       m_factory->CreateSwapChainForHwnd( m_commandQueue, desc.hwnd, &scDesc, nullptr,
                                                                          nullptr, &swapChain1 ),
                                       "CreateSwapChainForHwnd failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    startupResult = Dx12StartupResult( m_resultDiagnostics, swapChain1->QueryInterface( IID_PPV_ARGS( &m_swapChain ) ),
                                       "SwapChain QueryInterface failed" );

    if ( !startupResult.Ok() )
    {
        swapChain1->Release();
        return startupResult;
    }

    swapChain1->Release();
    m_factory->MakeWindowAssociation( desc.hwnd, DXGI_MWA_NO_ALT_ENTER );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    for ( UINT i = 0; i < m_frameCount; ++i )
    {

        // A command allocator is the memory backing one batch of recorded GPU
        // commands. It must not be reset until the frame fence proves the GPU
        // has finished executing commands recorded into it.
        startupResult = Dx12StartupResult( m_resultDiagnostics,
                                           m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                             IID_PPV_ARGS( &m_commandAllocators[i] ) ),
                                           "CreateCommandAllocator failed" );

        if ( !startupResult.Ok() )
        {
            return startupResult;
        }

        NameDx12ObjectIndexed( m_commandAllocators[i], L"Skullbonez DX12 Command Allocator", i );
    }

    startupResult = Dx12StartupResult( m_resultDiagnostics,
                                       m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                    m_commandAllocators[0], nullptr,
                                                                    IID_PPV_ARGS( &m_commandList ) ),
                                       "CreateCommandList failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    NameDx12Object( m_commandList, L"Skullbonez DX12 Main Command List" );

    // Invariant: the backend's recording epoch starts closed. Do not publish a
    // device whose initial command list failed to enter that state.
    startupResult = Dx12StartupResult( m_resultDiagnostics, m_commandList->Close(), "Initial command list Close failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    startupResult = Dx12StartupResult( m_resultDiagnostics,
                                       m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ),
                                       "CreateFence failed" );

    if ( !startupResult.Ok() )
    {
        return startupResult;
    }

    NameDx12Object( m_fence, L"Skullbonez DX12 Frame Fence" );
    m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

    if ( !m_fenceEvent )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "CreateEvent (frame fence) failed" );
    }

    m_frameFence.Init( m_commandQueue, m_fence, m_fenceEvent );

    rollback.Commit();
    return SkullbonezCore::Core::SbResult::Success();
}


void Dx12RenderDevice::Shutdown()
{

    // Invariant: this low-level owner does not submit or wait. Normal runtime
    // teardown reaches it only after RenderBackendDX12 has proven queue/present
    // completion; Init rollback reaches it before any command-list submission.
    m_frameFence.Reset();

    if ( m_depthStencil )
    {
        m_depthStencil->Release();
        m_depthStencil = nullptr;
    }

    if ( m_fence )
    {
        m_fence->Release();
        m_fence = nullptr;
    }

    if ( m_fenceEvent )
    {
        CloseHandle( m_fenceEvent );
        m_fenceEvent = nullptr;
    }

    if ( m_commandList )
    {
        m_commandList->Release();
        m_commandList = nullptr;
    }

    for ( UINT i = 0; i < MAX_FRAME_COUNT; ++i )
    {

        if ( m_commandAllocators[i] )
        {
            m_commandAllocators[i]->Release();
            m_commandAllocators[i] = nullptr;
        }
    }

    if ( m_swapChain )
    {
        m_swapChain->SetFullscreenState( FALSE, nullptr );
        m_swapChain->Release();
        m_swapChain = nullptr;
    }

    if ( m_commandQueue )
    {
        m_commandQueue->Release();
        m_commandQueue = nullptr;
    }

    if ( m_device )
    {
        m_device->Release();
        m_device = nullptr;
    }

    if ( m_factory )
    {
        m_factory->Release();
        m_factory = nullptr;
    }

    m_frameCount = 0;
    m_frameIndex = 0;
    m_allocatorIndex = 0;
    m_allowTearing = false;
    m_vsyncEnabled = true;
    m_width = 0;
    m_height = 0;
    m_recreationGeneration = 0;
}


void Dx12RenderDevice::PublishInitialExtent( int width, int height )
{

    // Invariant: generation zero means no complete render-device epoch has been
    // published. Initial publication creates generation one exactly once.

    if ( width <= 0 || height <= 0 || m_recreationGeneration != 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "Invalid initial DX12 extent publication. width=%d height=%d generation=%llu", width,
                  height, static_cast<unsigned long long>( m_recreationGeneration ) );
    }

    m_width = width;
    m_height = height;
    m_recreationGeneration = 1;
}


uint64_t Dx12RenderDevice::PublishResizedExtent( int width, int height )
{

    if ( width <= 0 || height <= 0 || m_recreationGeneration == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "Invalid resized DX12 extent publication. width=%d height=%d generation=%llu", width,
                  height, static_cast<unsigned long long>( m_recreationGeneration ) );
    }

    m_width = width;
    m_height = height;
    return ++m_recreationGeneration;
}


SkullbonezCore::Core::SbResult Dx12RenderDevice::CreateDepthStencilResource( int width, int height,
                                                                             ID3D12Resource*& outResource ) const
{

    // Concept: the main depth surface is part of the published presentation
    // epoch because its dimensions must match the swap-chain color surfaces.
    // Creation returns a candidate; ReplaceDepthStencil is the publication step.
    outResource = nullptr;

    if ( !m_device || width <= 0 || height <= 0 )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Invalid depth-stencil creation request. width=%d height=%d",
                                            width, height );
    }

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = static_cast<UINT64>( width );
    description.Height = static_cast<UINT>( height );
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;

    const HRESULT createResult = m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &description,
                                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                                                    IID_PPV_ARGS( &outResource ) );

    if ( FAILED( createResult ) || !outResource )
    {
        return Dx12RuntimeResult( m_resultDiagnostics, FAILED( createResult ) ? createResult : E_FAIL,
                                  "CreateCommittedResource (depth stencil) failed" );
    }

    NameDx12Object( outResource, L"Skullbonez DX12 Main Depth Stencil" );
    return SkullbonezCore::Core::SbResult::Success();
}


ID3D12Resource* Dx12RenderDevice::ReplaceDepthStencil( ID3D12Resource* replacement )
{

    // Lifetime: replacement transfers one COM reference into this owner. The
    // caller receives the old reference and must retire or release it.
    ID3D12Resource* previous = m_depthStencil;
    m_depthStencil = replacement;
    return previous;
}


void Dx12RenderDevice::ReportDeviceLost( const char* context, HRESULT result ) const
{
    const HRESULT removedReason = m_device ? m_device->GetDeviceRemovedReason() : result;
    SkullbonezCore::Core::Log().WriteEventf( "dx12_device_lost context=%s result=0x%08lX removed_reason=0x%08lX",
                                             context ? context : "unknown", static_cast<unsigned long>( result ),
                                             static_cast<unsigned long>( removedReason ) );

    FILE* file = nullptr;
    fopen_s( &file, "dx12_device_lost.txt", "a" );

    if ( file )
    {
        fprintf( file, "context=%s result=0x%08lX removed_reason=0x%08lX\n", context ? context : "unknown",
                 static_cast<unsigned long>( result ), static_cast<unsigned long>( removedReason ) );
    }

    if ( m_device )
    {
        ID3D12DeviceRemovedExtendedData* dred = nullptr;

        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &dred ) ) ) )
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};

            D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};

            const HRESULT breadcrumbResult = dred->GetAutoBreadcrumbsOutput( &breadcrumbs );
            const HRESULT pageFaultResult = dred->GetPageFaultAllocationOutput( &pageFault );

            SkullbonezCore::Core::Log()
                .WriteEventf( "dx12_dred context=%s breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX "
                              "page_fault_va=0x%llX existing_allocations=%p recent_freed_allocations=%p",
                              context ? context : "unknown", static_cast<unsigned long>( breadcrumbResult ),
                              breadcrumbs.pHeadAutoBreadcrumbNode, static_cast<unsigned long>( pageFaultResult ),
                              static_cast<unsigned long long>( pageFault.PageFaultVA ),
                              pageFault.pHeadExistingAllocationNode, pageFault.pHeadRecentFreedAllocationNode );

            if ( file )
            {
                fprintf( file,
                         "dred breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX "
                         "page_fault_va=0x%llX existing_allocations=%p recent_freed_allocations=%p\n",
                         static_cast<unsigned long>( breadcrumbResult ), breadcrumbs.pHeadAutoBreadcrumbNode,
                         static_cast<unsigned long>( pageFaultResult ),
                         static_cast<unsigned long long>( pageFault.PageFaultVA ), pageFault.pHeadExistingAllocationNode,
                         pageFault.pHeadRecentFreedAllocationNode );
            }

            dred->Release();
        }
        else if ( file )
        {
            fprintf( file, "dred unavailable\n" );
        }
    }

    if ( file )
    {
        fprintf( file, "---\n" );
        fclose( file );
    }
}


ID3D12CommandAllocator* Dx12RenderDevice::CommandAllocator( UINT index ) const
{

    if ( index >= m_frameCount )
    {
        return nullptr;
    }

    return m_commandAllocators[index];
}


UINT Dx12RenderDevice::AdvanceAllocatorIndex()
{

    if ( m_frameCount == 0 )
    {
        return 0;
    }

    m_allocatorIndex = ( m_allocatorIndex + 1 ) % m_frameCount;
    return m_allocatorIndex;
}


UINT Dx12RenderDevice::RefreshFrameIndexFromSwapChain()
{

    if ( !m_swapChain )
    {
        m_frameIndex = 0;
        return m_frameIndex;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return m_frameIndex;
}

} // namespace Rendering
} // namespace SkullbonezCore

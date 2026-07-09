/*
File: SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp
Purpose:
  Owns low-level DX12 device objects, fences, command allocators, and frame pacing.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads or writes
  depth/stencil data for depth testing.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  DRED (Device Removed Extended Data): DX12 diagnostic report for GPU device
  loss, breadcrumbs, and page-fault clues.
  PIX: Microsoft GPU debugger/profiler that can read engine markers and DX12
  object names.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  through reference-counted objects.
  Descriptor heap: Table of descriptor rows that tell the GPU how to interpret
  resources for reads, writes, or render targets.
  Shader-visible descriptor heap: Descriptor table the GPU can index from bound
  root tables; rows must not be overwritten until the frame fence proves use is
  complete.
  PSO (Pipeline State Object): Compiled draw or raytracing state bundle.
  Root signature: Binding contract that tells command lists where shaders find
  descriptor tables and constants.
  Resource state: DX12 usage mode for a resource, such as render target,
  shader read, copy source, or present.
  Fence: GPU timeline counter used to prove command allocators, upload bytes,
  and transient descriptors are no longer in flight.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderDeviceDX12.h"

#include "../../Core/FatalError.h"

#include <algorithm>
#include <cwchar>
#include <d3d12sdklayers.h>
#include <sstream>
#include <stdexcept>

namespace SkullbonezCore
{
namespace Rendering
{

static inline Basics::SbResult Dx12StartupResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: adapter, driver, swap-chain, and Win32 event creation can fail
        // because of the host environment. Report the failing DX12 startup step
        // to the process bootstrap instead of escaping through an exception.
        return Basics::SbResult::Failure( "Rendering/DX12",
                                          "%s (HRESULT 0x%08X)",
                                          msg ? msg : "DX12 startup call failed",
                                          static_cast<unsigned int>( hr ) );
    }
    return Basics::SbResult::Success();
}


static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
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


UINT64 Dx12FenceTimeline::Signal()
{
    if ( !IsReady() )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 fence timeline used before Init." );
    }

    // Signal creates the next completion marker on the GPU timeline. The queue
    // does not write this value immediately. It writes the value after every
    // command submitted before this Signal() has finished on the GPU.
    const UINT64 value = ++m_lastSignaledValue;
    if ( FAILED( m_queue->Signal( m_fence, value ) ) )
    {
        throw std::runtime_error( "DX12 command queue Signal failed" );
    }
    return value;
}


UINT64 Dx12FenceTimeline::SignalAndWait()
{
    // This is the "drain the GPU" path. It is intentionally blocking: the CPU
    // asks the GPU to signal a new value, then waits until that exact value is
    // complete. Use it for shutdown, resize, and rare mid-frame flushes, not for
    // normal per-draw work.
    const UINT64 value = Signal();
    WaitForValue( value );
    return value;
}


void Dx12FenceTimeline::WaitForValue( UINT64 value ) const
{
    if ( value == 0 )
    {
        return;
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
        if ( FAILED( m_fence->SetEventOnCompletion( value, m_eventHandle ) ) )
        {
            throw std::runtime_error( "DX12 fence SetEventOnCompletion failed" );
        }
        WaitForSingleObject( m_eventHandle, INFINITE );
    }
}


UINT64 Dx12FenceTimeline::CompletedValue() const
{
    if ( !m_fence )
    {
        return 0;
    }
    return m_fence->GetCompletedValue();
}


Dx12FenceTimelineStats Dx12FenceTimeline::GetStats() const
{
    Dx12FenceTimelineStats stats;
    stats.lastSignaledValue = m_lastSignaledValue;
    stats.completedValue = CompletedValue();
    return stats;
}


void Dx12CpuDescriptorAllocator::Init( ID3D12DescriptorHeap* heap,
                                       UINT descriptorSize,
                                       UINT capacity,
                                       const char* heapName )
{
    if ( !heap || descriptorSize == 0 || capacity == 0 )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 CPU descriptor allocator received invalid heap geometry. descriptorSize=%u capacity=%u",
                  descriptorSize,
                  capacity );
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
    m_heapName = ( heapName && heapName[0] != '\0' ) ? heapName : "unknown";
}


void Dx12CpuDescriptorAllocator::Reset()
{
    m_heap = nullptr;
    m_descriptorSize = 0;
    m_capacity = 0;
    m_next = 0;
    m_heapName = "unknown";
}


Dx12CpuDescriptorAllocation Dx12CpuDescriptorAllocator::Allocate()
{
    if ( !m_heap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor allocator used before Init." );
    }
    if ( m_next >= m_capacity )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 CPU descriptor heap exhausted. heap=%s used=%u capacity=%u",
                  m_heapName,
                  m_next,
                  m_capacity );
    }

    // Allocation means "reserve the next unused row in the descriptor table."
    // It does not touch the resource itself. The caller will write the actual
    // view record with CreateRenderTargetView or CreateDepthStencilView.
    Dx12CpuDescriptorAllocation allocation;
    allocation.index = m_next++;
    allocation.cpuHandle = CpuHandle( allocation.index );
    return allocation;
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12CpuDescriptorAllocator::CpuHandle( UINT index ) const
{
    if ( !m_heap || m_descriptorSize == 0 )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 CPU descriptor heap unavailable." );
    }
    if ( index >= m_capacity )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 CPU descriptor index out of range. index=%u capacity=%u",
                  index,
                  m_capacity );
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
    stats.used = m_next;
    return stats;
}


void Dx12DescriptorAllocator::Init( ID3D12DescriptorHeap* shaderVisibleHeap,
                                    ID3D12DescriptorHeap* stagingHeap,
                                    UINT descriptorSize,
                                    UINT staticCapacity,
                                    UINT transientCapacityPerFrame,
                                    UINT frameCount )
{
    const UINT64 shaderVisibleCapacity =
        static_cast<UINT64>( staticCapacity ) + ( static_cast<UINT64>( transientCapacityPerFrame ) * frameCount );
    if ( !shaderVisibleHeap || !stagingHeap || descriptorSize == 0 || staticCapacity == 0 ||
         transientCapacityPerFrame == 0 || frameCount == 0 || shaderVisibleCapacity > 0xffffffffull )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "Invalid DX12 descriptor allocator init description. descriptor_size=%u static_capacity=%u "
                  "transient_capacity_per_frame=%u frame_count=%u shader_visible_capacity=%llu",
                  descriptorSize,
                  staticCapacity,
                  transientCapacityPerFrame,
                  frameCount,
                  static_cast<unsigned long long>( shaderVisibleCapacity ) );
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
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 descriptor allocator frame index out of range. frameIndex=%u frameCount=%u",
                  frameIndex,
                  m_frameCount );
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
    // Static slots are stable descriptor IDs. Texture handles and persistent
    // render views can keep referring to them because they are never recycled
    // during the frame loop.
    //
    // This is the right allocation path when the slot index becomes part of a
    // longer-lived engine record, such as TextureEntryDX12::srvIndex.
    if ( m_nextStatic >= m_staticCapacity )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 static SRV heap exhausted. used=%u capacity=%u",
                  m_nextStatic,
                  m_staticCapacity );
    }
    return m_nextStatic++;
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
        SB_FATAL(
            "RenderDeviceDX12",
            "DX12 transient SRV heap exhausted for current frame allocator. frame=%u used=%u capacity_per_frame=%u",
            m_currentFrame,
            m_nextTransientInFrame,
            m_transientCapacityPerFrame );
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
                  m_currentFrame,
                  count,
                  m_nextTransientInFrame,
                  m_transientCapacityPerFrame );
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
                  context ? context : "unknown",
                  index,
                  capacity,
                  m_staticCapacity,
                  m_transientCapacityPerFrame,
                  m_frameCount );
    }
}


void Dx12DescriptorAllocator::ValidateStagingIndex( UINT index, const char* context ) const
{
    if ( index >= m_staticCapacity )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 staging descriptor index out of range. context=%s index=%u static_capacity=%u",
                  context ? context : "unknown",
                  index,
                  m_staticCapacity );
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


Dx12DescriptorAllocatorStats Dx12DescriptorAllocator::GetStats() const
{
    Dx12DescriptorAllocatorStats stats;
    stats.staticCapacity = m_staticCapacity;
    stats.staticUsed = m_nextStatic;
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
}


void Dx12UploadArena::Reset()
{
    m_resource = nullptr;
    m_mappedPtr = nullptr;
    m_capacityBytes = 0;
    m_currentOffset = 0;
    m_peakBytes = 0;
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
}


bool Dx12UploadArena::CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const
{
    // The backend uses this probe to decide whether it must submit and wait
    // before recording more upload-heavy work. It prevents wraparound writes
    // into bytes already used earlier in the same command list.
    //
    // Returning false is not automatically fatal. The backend may submit the
    // current command list, wait for the GPU, reset the arena, and continue.
    const UINT64 alignedOffset = AlignOffset( m_currentOffset, alignment );
    return alignedOffset + sizeBytes <= m_capacityBytes;
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12UploadArena::Allocate( UINT64 sizeBytes, UINT64 alignment )
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
    if ( alignedOffset + sizeBytes > m_capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 upload buffer exhausted. requested=%llu alignedOffset=%llu capacity=%llu",
                  static_cast<unsigned long long>( sizeBytes ),
                  static_cast<unsigned long long>( alignedOffset ),
                  static_cast<unsigned long long>( m_capacityBytes ) );
    }

    m_currentOffset = alignedOffset + sizeBytes;
    m_peakBytes = (std::max)( m_peakBytes, m_currentOffset );
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
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 upload address outside current frame arena. address=%llu base=%llu",
                  static_cast<unsigned long long>( address ),
                  static_cast<unsigned long long>( base ) );
    }

    const UINT64 offset = address - base;
    if ( offset >= m_capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 upload address outside current frame arena. offset=%llu capacity=%llu",
                  static_cast<unsigned long long>( offset ),
                  static_cast<unsigned long long>( m_capacityBytes ) );
    }
    return m_mappedPtr + offset;
}


Dx12UploadArenaStats Dx12UploadArena::GetStats() const
{
    Dx12UploadArenaStats stats;
    stats.capacityBytes = m_capacityBytes;
    stats.usedBytes = m_currentOffset;
    stats.peakBytes = m_peakBytes;
    return stats;
}


UINT64 Dx12UploadArena::AlignOffset( UINT64 offset, UINT64 alignment ) const
{
    // Alignment means "start this allocation at an address divisible by N." The
    // formula below rounds up to the next legal byte offset. When alignment is 1
    // or 0, every byte position is legal, so no rounding is needed.
    if ( alignment <= 1 )
    {
        return offset;
    }
    return ( ( offset + alignment - 1 ) / alignment ) * alignment;
}


Dx12FrameUploadSystem::~Dx12FrameUploadSystem()
{
    Shutdown();
}


bool Dx12FrameUploadSystem::Init( ID3D12Device* device,
                                  UINT frameCount,
                                  UINT64 capacityBytes,
                                  const wchar_t* debugNamePrefix )
{
    Shutdown();

    if ( !device || frameCount == 0 || frameCount > MAX_FRAME_COUNT || capacityBytes == 0 )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "Invalid DX12 frame upload system init description. frameCount=%u capacityBytes=%llu",
                  frameCount,
                  static_cast<unsigned long long>( capacityBytes ) );
    }

    m_frameCount = frameCount;
    m_capacityBytes = capacityBytes;

    const wchar_t* safeName =
        ( debugNamePrefix && debugNamePrefix[0] != L'\0' ) ? debugNamePrefix : L"Skullbonez DX12 Frame Upload Buffer";
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

        ThrowIfFailed( device->CreateCommittedResource( &heapProps,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr,
                                                        IID_PPV_ARGS( &m_resources[i] ) ),
                       "CreateCommittedResource (frame upload) failed" );
        NameDx12ObjectIndexed( m_resources[i], safeName, i );

        ThrowIfFailed( m_resources[i]->Map( 0, nullptr, reinterpret_cast<void**>( &m_mappedPtrs[i] ) ),
                       "Map frame upload buffer failed" );

        // The arena owns byte-range accounting for this resource. The system
        // owns the COM resource and its persistent CPU Map() pointer.
        m_arenas[i].Init( m_resources[i], m_mappedPtrs[i], capacityBytes );
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


D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameUploadSystem::Allocate( UINT frameIndex, UINT64 sizeBytes, UINT64 alignment )
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].Allocate( sizeBytes, alignment );
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
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 upload address is before current frame resource. address=%llu base=%llu",
                  static_cast<unsigned long long>( address ),
                  static_cast<unsigned long long>( base ) );
    }
    const UINT64 offset = address - base;
    if ( offset >= m_capacityBytes )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 upload address is outside current frame resource. offset=%llu capacity=%llu",
                  static_cast<unsigned long long>( offset ),
                  static_cast<unsigned long long>( m_capacityBytes ) );
    }
    return offset;
}


ID3D12Resource* Dx12FrameUploadSystem::Resource( UINT frameIndex ) const
{
    ValidateFrameIndex( frameIndex );
    return m_arenas[frameIndex].Resource();
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
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 frame upload index out of range. frameIndex=%u frameCount=%u",
                  frameIndex,
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
        SB_FATAL( "RenderDeviceDX12",
                  "Invalid DX12 readback buffer init description. sizeBytes=%llu",
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
    const HRESULT hr = device->CreateCommittedResource( &heapProps,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &desc,
                                                        D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr,
                                                        IID_PPV_ARGS( &m_resource ) );
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


void* Dx12ReadbackBuffer::MapRead( UINT64 sizeBytes ) const
{
    if ( !m_resource )
    {
        SB_FATAL( "RenderDeviceDX12", "DX12 readback buffer unavailable." );
    }
    if ( sizeBytes > m_sizeBytes )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "DX12 readback map range exceeds buffer size. requested=%llu capacity=%llu",
                  static_cast<unsigned long long>( sizeBytes ),
                  static_cast<unsigned long long>( m_sizeBytes ) );
    }

    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>( sizeBytes ) };
    ThrowIfFailed( m_resource->Map( 0, &readRange, &mappedData ), "Map readback buffer failed" );
    return mappedData;
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


Basics::SbResult Dx12RenderDevice::Init( const Dx12RenderDeviceInitDesc& desc )
{
    Shutdown();

    if ( !desc.hwnd || desc.width == 0 || desc.height == 0 || desc.frameCount == 0 ||
         desc.frameCount > MAX_FRAME_COUNT )
    {
        SB_FATAL( "RenderDeviceDX12",
                  "Invalid DX12 render device init description. width=%u height=%u frameCount=%u",
                  desc.width,
                  desc.height,
                  desc.frameCount );
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
    Basics::SbResult startupResult = Dx12StartupResult( CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &m_factory ) ),
                                                        "CreateDXGIFactory2 failed" );
    if ( !startupResult.ok )
    {
        return startupResult;
    }

    {
        IDXGIFactory5* factory5 = nullptr;
        BOOL allowTearing = FALSE;
        if ( SUCCEEDED( m_factory->QueryInterface( IID_PPV_ARGS( &factory5 ) ) ) )
        {
            if ( FAILED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &allowTearing,
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
    startupResult = Dx12StartupResult( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ),
                                       "D3D12CreateDevice failed" );
    if ( !startupResult.ok )
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
    startupResult = Dx12StartupResult( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ),
                                       "CreateCommandQueue failed" );
    if ( !startupResult.ok )
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
    startupResult = Dx12StartupResult(
        m_factory->CreateSwapChainForHwnd( m_commandQueue, desc.hwnd, &scDesc, nullptr, nullptr, &swapChain1 ),
        "CreateSwapChainForHwnd failed" );
    if ( !startupResult.ok )
    {
        return startupResult;
    }
    startupResult = Dx12StartupResult( swapChain1->QueryInterface( IID_PPV_ARGS( &m_swapChain ) ),
                                       "SwapChain QueryInterface failed" );
    if ( !startupResult.ok )
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
        startupResult = Dx12StartupResult(
            m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocators[i] ) ),
            "CreateCommandAllocator failed" );
        if ( !startupResult.ok )
        {
            return startupResult;
        }
        NameDx12ObjectIndexed( m_commandAllocators[i], L"Skullbonez DX12 Command Allocator", i );
    }

    startupResult = Dx12StartupResult( m_device->CreateCommandList( 0,
                                                                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                    m_commandAllocators[0],
                                                                    nullptr,
                                                                    IID_PPV_ARGS( &m_commandList ) ),
                                       "CreateCommandList failed" );
    if ( !startupResult.ok )
    {
        return startupResult;
    }
    NameDx12Object( m_commandList, L"Skullbonez DX12 Main Command List" );
    m_commandList->Close();

    startupResult = Dx12StartupResult( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ),
                                       "CreateFence failed" );
    if ( !startupResult.ok )
    {
        return startupResult;
    }
    NameDx12Object( m_fence, L"Skullbonez DX12 Frame Fence" );
    m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( !m_fenceEvent )
    {
        return Basics::SbResult::Failure( "Rendering/DX12", "CreateEvent (frame fence) failed" );
    }
    m_frameFence.Init( m_commandQueue, m_fence, m_fenceEvent );

    rollback.Commit();
    return Basics::SbResult::Success();
}


void Dx12RenderDevice::Shutdown()
{
    m_frameFence.Reset();

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

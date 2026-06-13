#include "SkullbonezRenderDeviceDX12.h"

#include <algorithm>
#include <cwchar>
#include <d3d12sdklayers.h>
#include <stdexcept>

namespace SkullbonezCore
{
namespace Rendering
{

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


void Dx12DescriptorAllocator::Init( ID3D12DescriptorHeap* shaderVisibleHeap,
                                    ID3D12DescriptorHeap* stagingHeap,
                                    UINT descriptorSize,
                                    UINT staticCapacity,
                                    UINT transientCapacityPerFrame,
                                    UINT frameCount )
{
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
        throw std::runtime_error( "DX12 descriptor allocator used before Init" );
    }
    if ( frameIndex >= m_frameCount )
    {
        throw std::runtime_error( "DX12 descriptor allocator frame index out of range" );
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
        throw std::runtime_error( "DX12 static SRV heap exhausted" );
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
        throw std::runtime_error( "DX12 transient SRV heap exhausted for current frame allocator" );
    }

    const UINT index = m_staticCapacity + ( m_currentFrame * m_transientCapacityPerFrame ) + m_nextTransientInFrame;
    ++m_nextTransientInFrame;
    m_transientPeakThisRun = (std::max)( m_transientPeakThisRun, m_nextTransientInFrame );
    return index;
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
        throw std::runtime_error( "DX12 shader-visible descriptor heap unavailable" );
    }

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
        throw std::runtime_error( "DX12 shader-visible descriptor heap unavailable" );
    }

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
        throw std::runtime_error( "DX12 staging descriptor heap unavailable" );
    }

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
        throw std::runtime_error( "DX12 upload arena used before Init" );
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
        throw std::runtime_error( "DX12 upload buffer exhausted" );
    }

    m_currentOffset = alignedOffset + sizeBytes;
    m_peakBytes = (std::max)( m_peakBytes, m_currentOffset );
    return m_resource->GetGPUVirtualAddress() + alignedOffset;
}


uint8_t* Dx12UploadArena::GetMappedPtr( D3D12_GPU_VIRTUAL_ADDRESS address ) const
{
    if ( !m_resource || !m_mappedPtr )
    {
        throw std::runtime_error( "DX12 upload arena used before Init" );
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
        throw std::runtime_error( "DX12 upload address outside current frame arena" );
    }

    const UINT64 offset = address - base;
    if ( offset >= m_capacityBytes )
    {
        throw std::runtime_error( "DX12 upload address outside current frame arena" );
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

} // namespace Rendering
} // namespace SkullbonezCore

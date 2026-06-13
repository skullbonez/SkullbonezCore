#include "SkullbonezRenderDeviceDX12.h"

#include <algorithm>
#include <stdexcept>

namespace SkullbonezCore
{
namespace Rendering
{

void Dx12DescriptorAllocator::Init( ID3D12DescriptorHeap* shaderVisibleHeap,
                                    ID3D12DescriptorHeap* stagingHeap,
                                    UINT descriptorSize,
                                    UINT staticCapacity,
                                    UINT transientCapacityPerFrame,
                                    UINT frameCount )
{
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
    m_currentFrame = frameIndex;
    m_nextTransientInFrame = 0;
}


UINT Dx12DescriptorAllocator::AllocateStatic()
{
    // Static slots are stable descriptor IDs. Texture handles and persistent
    // render views can keep referring to them because they are never recycled
    // during the frame loop.
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
    m_currentOffset = 0;
}


bool Dx12UploadArena::CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const
{
    // The backend uses this probe to decide whether it must submit and wait
    // before recording more upload-heavy work. It prevents wraparound writes
    // into bytes already used earlier in the same command list.
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
    if ( alignment <= 1 )
    {
        return offset;
    }
    return ( ( offset + alignment - 1 ) / alignment ) * alignment;
}

} // namespace Rendering
} // namespace SkullbonezCore

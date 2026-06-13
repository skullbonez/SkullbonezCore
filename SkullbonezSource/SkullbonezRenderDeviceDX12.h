#pragma once

#include <cstdint>
#include <d3d12.h>

namespace SkullbonezCore
{
namespace Rendering
{

// Compact counters for the DX12 descriptor allocator. A descriptor is a small
// GPU-facing record that tells a shader how to read a resource, such as a
// texture or unordered-access target. These numbers make heap pressure visible
// without exposing raw heap pointers to every render pass.
struct Dx12DescriptorAllocatorStats
{
    UINT staticCapacity = 0;
    UINT staticUsed = 0;
    UINT transientCapacityPerFrame = 0;
    UINT transientUsedThisFrame = 0;
    UINT transientPeakThisRun = 0;
    UINT currentFrame = 0;
};

/* -- Dx12DescriptorAllocator ------------------------------------------------------------------------------------------------------------------------------------

    DX12 does not let shaders read a texture just because the CPU has an
    ID3D12Resource pointer. The shader needs a descriptor, and draw/dispatch
    commands need that descriptor to live in a shader-visible heap until the GPU
    is finished reading it.

    This allocator is the first explicit owner for that policy:

    - Static descriptors live for the lifetime of a texture or persistent view.
      They are the source-of-truth slots used by loaded textures, generated
      null descriptors, and DXR reflection views.
    - Transient descriptors are per-frame copies used for root descriptor
      tables. They are reset only when the backend switches to a frame allocator
      whose fence has completed, so the CPU does not overwrite descriptors that
      an in-flight command list can still read.
    - CPU handles and GPU handles are calculated in one place. Keeping this
      pointer arithmetic out of pass code is an important step toward the
      planned Dx12RenderDevice and later render graph, where passes should ask
      for resources by engine concepts instead of manually walking descriptor
      heaps.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12DescriptorAllocator
{
  public:
    void Init( ID3D12DescriptorHeap* shaderVisibleHeap,
               ID3D12DescriptorHeap* stagingHeap,
               UINT descriptorSize,
               UINT staticCapacity,
               UINT transientCapacityPerFrame,
               UINT frameCount );
    void Reset();
    void ResetFrame( UINT frameIndex );

    UINT AllocateStatic();
    UINT AllocateTransient();

    D3D12_CPU_DESCRIPTOR_HANDLE ShaderVisibleCpuHandle( UINT index ) const;
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleGpuHandle( UINT index ) const;
    D3D12_CPU_DESCRIPTOR_HANDLE StagingCpuHandle( UINT index ) const;

    Dx12DescriptorAllocatorStats GetStats() const;

  private:
    ID3D12DescriptorHeap* m_shaderVisibleHeap = nullptr;
    ID3D12DescriptorHeap* m_stagingHeap = nullptr;
    UINT m_descriptorSize = 0;
    UINT m_staticCapacity = 0;
    UINT m_transientCapacityPerFrame = 0;
    UINT m_frameCount = 0;
    UINT m_nextStatic = 0;
    UINT m_nextTransientInFrame = 0;
    UINT m_transientPeakThisRun = 0;
    UINT m_currentFrame = 0;
};

// Compact counters for the per-frame upload arena. Upload memory is CPU-visible
// memory that the GPU can read. It is convenient, but it is only safe when each
// in-flight frame gets its own slice and the slice is not reused until the
// frame fence proves the GPU is done with it.
struct Dx12UploadArenaStats
{
    UINT64 capacityBytes = 0;
    UINT64 usedBytes = 0;
    UINT64 peakBytes = 0;
};

/* -- Dx12UploadArena --------------------------------------------------------------------------------------------------------------------------------------------

    The upload arena owns one frame's scratch upload memory. The CPU writes
    constants, dynamic vertices, instance data, and texture upload rows here.
    Draw commands then hand the GPU virtual address to DX12.

    The safety rule is simple but critical: never write new CPU data over bytes
    the GPU might still read. The backend already waits on a per-frame fence
    before reusing a command allocator. This class ties upload offsets to that
    same frame boundary so future render-device code has one clear place to
    reset and measure upload memory.

    The allocator also handles DX12 alignment requirements. Constant buffers,
    texture rows, and vertex data all have different alignment needs, and doing
    that math at every call site makes lifetime bugs easier to miss.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12UploadArena
{
  public:
    void Init( ID3D12Resource* resource, uint8_t* mappedPtr, UINT64 capacityBytes );
    void Reset();
    void ResetFrame();

    bool CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const;
    D3D12_GPU_VIRTUAL_ADDRESS Allocate( UINT64 sizeBytes, UINT64 alignment );
    uint8_t* GetMappedPtr( D3D12_GPU_VIRTUAL_ADDRESS address ) const;

    ID3D12Resource* Resource() const
    {
        return m_resource;
    }

    Dx12UploadArenaStats GetStats() const;

  private:
    UINT64 AlignOffset( UINT64 offset, UINT64 alignment ) const;

    ID3D12Resource* m_resource = nullptr;
    uint8_t* m_mappedPtr = nullptr;
    UINT64 m_capacityBytes = 0;
    UINT64 m_currentOffset = 0;
    UINT64 m_peakBytes = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore

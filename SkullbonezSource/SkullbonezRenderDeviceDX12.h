#pragma once

#include <cstdint>
#include <d3d12.h>

namespace SkullbonezCore
{
namespace Rendering
{

/* -- DX12 diagnostics helpers ----------------------------------------------------------------------------------------------------------------------------------

    Debug names and DRED are not rendering features by themselves, but they are
    part of the DX12 architecture because DX12 makes the engine responsible for
    GPU lifetime and synchronization. When a GPU error happens, the useful
    question is not "which raw pointer failed?" but "was that the swap-chain
    back buffer, the upload arena, or a command allocator?"

    These helpers keep that diagnostic policy in the DX12 device layer:

    - EnableDx12DeviceRemovedDiagnostics asks the runtime to keep breadcrumbs
      and page-fault data if the GPU device is removed.
    - NameDx12Object attaches human-readable names to D3D12 objects. PIX, DRED,
      and the debug layer can then report meaningful object names.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
void EnableDx12DeviceRemovedDiagnostics();
void NameDx12Object( ID3D12Object* object, const wchar_t* name );
void NameDx12ObjectIndexed( ID3D12Object* object, const wchar_t* prefix, UINT index );

// Compact counters for the frame fence timeline.
//
// Plain-language version:
//
// The CPU records commands into command lists, then submits those command lists
// to the GPU. The GPU usually executes them later. That means CPU code can be
// several frames ahead of the GPU unless we deliberately synchronize.
//
// A fence is a GPU-visible counter. The CPU asks the command queue to write a
// value into the fence after all previously submitted GPU work completes. Later,
// the CPU reads the fence to answer: "has the GPU reached that point yet?"
struct Dx12FenceTimelineStats
{
    UINT64 lastSignaledValue = 0;
    UINT64 completedValue = 0;
};

/* -- Dx12FenceTimeline ------------------------------------------------------------------------------------------------------------------------------------------

    What is a fence timeline?

    A fence timeline is the renderer's ordered list of GPU completion points.
    Each call to Signal() creates a new point on that timeline:

    1. CPU submits commands to the command queue.
    2. CPU calls Signal().
    3. The GPU eventually finishes all earlier commands.
    4. The command queue writes the signaled value into the fence.
    5. CPU code can safely reuse resources protected by that value.

    Why does the renderer need this helper?

    DX12 does not automatically protect command allocators, upload buffers, or
    transient descriptor slots from being reused too early. The engine has to
    ask the fence whether the GPU is done. Centralizing that logic keeps the
    rule readable:

    - signal after submitting work,
    - store the returned value on the frame/resource that must be protected,
    - wait for that value before resetting the protected memory.

    This class does not own the queue, fence, or Windows event yet. The current
    backend still creates and releases those raw objects. The class owns the
    policy for signal values and waits, which is the part future Dx12RenderDevice
    code should keep.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12FenceTimeline
{
  public:
    void Init( ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE eventHandle );
    void Reset();

    bool IsReady() const;
    UINT64 Signal();
    UINT64 SignalAndWait();
    void WaitForValue( UINT64 value ) const;

    UINT64 CompletedValue() const;
    UINT64 LastSignaledValue() const
    {
        return m_lastSignaledValue;
    }

    Dx12FenceTimelineStats GetStats() const;

  private:
    ID3D12CommandQueue* m_queue = nullptr;
    ID3D12Fence* m_fence = nullptr;
    HANDLE m_eventHandle = nullptr;
    UINT64 m_lastSignaledValue = 0;
};

// Compact counters for the DX12 descriptor allocator.
//
// Plain-language version:
//
// A texture, buffer, or unordered-access target is the actual storage. A
// descriptor is not the storage. A descriptor is a small lookup record that says
// "resource X should be interpreted as this kind of shader input/output, with
// this format, mip range, and view type." Shaders do not receive C++ pointers to
// resources. They receive descriptor-table GPU handles that point into a
// descriptor heap, then the hardware reads descriptors from that heap.
//
// These counters answer the practical questions a non-DX12 engineer usually
// needs first:
//
// - How many persistent descriptor slots did the renderer consume?
// - How many per-frame temporary descriptor slots did it need at peak?
// - Which frame allocator currently owns the temporary range?
//
// The numbers are intentionally stored in terms of slots, not bytes, because a
// descriptor heap behaves like a table. The allocator hands out table indices.
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

    What is a descriptor allocator?

    Think of a DX12 descriptor heap as a fixed-size table owned by the renderer.
    Each row in that table is a descriptor. A descriptor is a hardware-readable
    record that describes how a shader should see a resource. For example:

    - "this ID3D12Resource is a 2D texture SRV"
    - "read mips 0 through 4"
    - "interpret the pixels as RGBA8"
    - "this UAV can be written by a compute shader"

    A descriptor allocator does not allocate textures or GPU memory. It only
    hands out unused row numbers in that descriptor table and converts those row
    numbers into the CPU/GPU handles DX12 wants.

    Why do we need one?

    Older APIs often hide this table management in the driver. DX12 exposes it.
    If two systems accidentally write different descriptors into the same heap
    row, a shader may sample the wrong texture. If the CPU overwrites a temporary
    descriptor while an in-flight GPU command still points at it, the GPU can
    read stale or incorrect data. Those bugs look like random rendering
    corruption, so the lifetime rule must be explicit in code.

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

    Glossary for this class:

    - Static descriptor:
      A long-lived heap slot. Loaded textures, generated fallback descriptors,
      and persistent render views use these. The slot index remains stable.

    - Transient descriptor:
      A short-lived per-frame heap slot. The renderer copies a static descriptor
      into this range when a root descriptor table needs a shader-visible
      binding. The whole transient range is reset only when that frame's fence
      proves the GPU is no longer using it.

    - Staging heap:
      CPU-only descriptor storage used as the source of truth. Shaders cannot
      read this heap directly. It is useful because persistent descriptors can
      live here and be copied into the shader-visible heap as needed.

    - Shader-visible heap:
      Descriptor storage the GPU can read through root descriptor tables.
      Descriptors placed here must obey GPU lifetime rules.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12DescriptorAllocator
{
  public:
    // Bind the allocator to the two descriptor heaps it manages. The allocator
    // does not own the COM objects; the backend/device owns and releases them.
    // The allocator owns the slot accounting policy for those heaps.
    void Init( ID3D12DescriptorHeap* shaderVisibleHeap,
               ID3D12DescriptorHeap* stagingHeap,
               UINT descriptorSize,
               UINT staticCapacity,
               UINT transientCapacityPerFrame,
               UINT frameCount );

    // Drop all heap pointers and counters during shutdown. After Reset(), any
    // allocation or handle lookup is invalid until Init() runs again.
    void Reset();

    // Start using the transient descriptor range for one frame allocator.
    // This must happen only after that frame allocator is safe to reuse.
    void ResetFrame( UINT frameIndex );

    // Reserve one long-lived descriptor slot. Use this for descriptors whose
    // index will be stored in texture/render-resource records.
    UINT AllocateStatic();

    // Reserve one temporary descriptor slot from the current frame's range.
    // Use this for descriptor copies needed while recording this frame.
    UINT AllocateTransient();

    // CPU handle into the shader-visible heap. The CPU uses this to write or
    // copy a descriptor into a shader-readable slot.
    D3D12_CPU_DESCRIPTOR_HANDLE ShaderVisibleCpuHandle( UINT index ) const;

    // GPU handle into the shader-visible heap. Command lists bind this value so
    // shaders can follow it to the descriptor table.
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleGpuHandle( UINT index ) const;

    // CPU handle into the staging heap. This heap is not shader-visible; it is
    // the persistent source used for descriptor copies.
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

// Compact counters for the per-frame upload arena.
//
// Plain-language version:
//
// Upload memory is CPU-visible staging memory. The CPU writes bytes into it,
// then the GPU reads those bytes later from the command list. It is commonly
// used for frame constants, dynamic vertices, instance data, and texture upload
// rows.
//
// The dangerous part is timing. The CPU can start recording a later frame while
// the GPU is still reading an earlier frame. If both frames write into the same
// bytes, the GPU can read the newer data by accident. These counters help show
// how much upload memory each frame used and how close the renderer came to the
// arena capacity.
struct Dx12UploadArenaStats
{
    UINT64 capacityBytes = 0;
    UINT64 usedBytes = 0;
    UINT64 peakBytes = 0;
};

/* -- Dx12UploadArena --------------------------------------------------------------------------------------------------------------------------------------------

    What is an upload arena?

    An upload arena is a simple bump allocator for one frame's CPU-to-GPU
    staging buffer.

    "Bump allocator" means it starts at byte 0 and moves a cursor forward every
    time code asks for space. It does not free individual allocations. At the
    next safe frame boundary, ResetFrame() moves the cursor back to byte 0 and
    the whole arena can be reused.

    Why does DX12 need this?

    Many pieces of render data are rebuilt every frame:

    - camera and pass constants,
    - object transforms,
    - dynamic/debug vertices,
    - instance tables,
    - texture rows during uploads.

    The CPU writes that data into an upload buffer because normal GPU-only
    memory is not directly writable by the CPU. Draw and copy commands then
    refer to the upload buffer by GPU virtual address.

    The safety rule is simple but critical: never write new CPU data over bytes
    the GPU might still read. The backend already waits on a per-frame fence
    before reusing a command allocator. This class ties upload offsets to that
    same frame boundary so future render-device code has one clear place to
    reset and measure upload memory.

    The allocator also handles DX12 alignment requirements. Constant buffers,
    texture rows, and vertex data all have different alignment needs, and doing
    that math at every call site makes lifetime bugs easier to miss.

    Glossary for this class:

    - GPU virtual address:
      The numeric address a command list gives to the GPU. This is not a CPU
      pointer and cannot be used with memcpy.

    - Mapped pointer:
      The CPU pointer returned by Map(). The CPU writes bytes through this
      pointer before the GPU reads them.

    - Alignment:
      Some DX12 bindings must begin at specific byte boundaries. For example,
      constant-buffer data must be 256-byte aligned. The arena rounds the cursor
      forward to the next legal boundary before each allocation.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12UploadArena
{
  public:
    // Attach this arena to one persistently mapped upload buffer. The arena does
    // not own the resource; the backend/device owns release and Unmap.
    void Init( ID3D12Resource* resource, uint8_t* mappedPtr, UINT64 capacityBytes );

    // Forget the resource and counters during shutdown.
    void Reset();

    // Reuse the full arena for a new frame after the owning frame fence has
    // completed. This is not safe at arbitrary times.
    void ResetFrame();

    // Probe whether the next allocation would fit. The backend uses this to
    // decide whether it must submit/wait before recording more upload-heavy work.
    bool CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const;

    // Reserve bytes and return the GPU address command lists should bind.
    D3D12_GPU_VIRTUAL_ADDRESS Allocate( UINT64 sizeBytes, UINT64 alignment );

    // Translate a GPU address returned by Allocate() back to a CPU pointer so
    // the caller can fill the allocation with memcpy or structured writes.
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

/*
File: SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h
Purpose:
  Owns low-level DX12 device objects, fences, command allocators, and frame pacing.

Summary:
  RenderDeviceDX12.h owns low-level DX12 device objects, fences, command
  allocators, and frame pacing. As a public header, keep edits anchored on
  DX12 ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
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
  Upload arena: Fixed, persistently mapped per-frame byte range used for
    constants, vertices, instances, and resource-copy rows.
  Cold flush: Submit/wait/reset retry allowed outside steady gameplay when an
    upload reservation does not fit.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../IRenderDiagnostics.h"
#include "../../Runtime/Allocation/RuntimeAllocationTracker.h"

#include <cstdint>
#include <limits>
#include <d3d12.h>
#include <dxgi1_5.h>

namespace SkullbonezCore
{
namespace Rendering
{

enum class Dx12UploadOverflowAction
{
    Allocate,
    FlushAndRetry,
    DropCaller
};

struct Dx12UploadReservationResolution
{
    bool allowed = false;
    bool dropped = false;
    bool coldRetryAttempted = false;
};

// Lane R: steady render reservations fail at the draw boundary. Cold lifecycle
// phases retain the legacy submit/wait retry because their stalls do not become
// frame hitches.
inline Dx12UploadOverflowAction SelectDx12UploadOverflowAction( bool fits,
                                                                Runtime::Allocation::RuntimeAllocationPhase phase )
{
    if ( fits )
    {
        return Dx12UploadOverflowAction::Allocate;
    }
    switch ( phase )
    {
    case Runtime::Allocation::RuntimeAllocationPhase::SteadyGameplay:
    case Runtime::Allocation::RuntimeAllocationPhase::Physics:
    case Runtime::Allocation::RuntimeAllocationPhase::Render:
    case Runtime::Allocation::RuntimeAllocationPhase::Replay:
        return Dx12UploadOverflowAction::DropCaller;
    default:
        return Dx12UploadOverflowAction::FlushAndRetry;
    }
}

// Executes the same branch production uses while allowing CPU tests to supply
// a counted cold-retry callback. Steady phases never invoke that callback.
template <typename ColdRetry>
Dx12UploadReservationResolution
ResolveDx12UploadReservation( bool fits, Runtime::Allocation::RuntimeAllocationPhase phase, ColdRetry coldRetry )
{
    const Dx12UploadOverflowAction action = SelectDx12UploadOverflowAction( fits, phase );
    if ( action == Dx12UploadOverflowAction::Allocate )
    {
        return { true, false, false };
    }
    if ( action == Dx12UploadOverflowAction::DropCaller )
    {
        return { false, true, false };
    }
    return { coldRetry(), false, true };
}

// Hazard: callers may probe a synthetic UINT64-sized request. Saturating the
// aligned offset prevents wraparound from turning overflow into a false fit.
inline UINT64 AlignDx12UploadOffset( UINT64 offset, UINT64 alignment )
{
    if ( alignment <= 1 )
    {
        return offset;
    }
    const UINT64 remainder = offset % alignment;
    const UINT64 padding = remainder == 0 ? 0 : alignment - remainder;
    return offset <= ( std::numeric_limits<UINT64>::max )() - padding ? offset + padding
                                                                      : ( std::numeric_limits<UINT64>::max )();
}

inline bool CanReserveDx12UploadRange( UINT64 offset, UINT64 capacity, UINT64 size, UINT64 alignment )
{
    const UINT64 alignedOffset = AlignDx12UploadOffset( offset, alignment );
    return alignedOffset <= capacity && size <= capacity - alignedOffset;
}

/* -- DX12 diagnostics helpers
----------------------------------------------------------------------------------------------------------------------------------

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

/* -- Dx12FenceTimeline
------------------------------------------------------------------------------------------------------------------------------------------

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
    Basics::SbResult Signal( UINT64& outValue );
    Basics::SbResult WaitForValue( UINT64 value ) const;

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

// A CPU descriptor allocation is one row in a CPU-only DX12 descriptor heap.
//
// Plain-language version:
//
// RTV and DSV descriptors are not read directly by shaders. The CPU records
// command-list calls that bind those descriptors to the Output Merger stage,
// which is the fixed-function part of the GPU that writes final color and depth
// pixels. Because shaders do not follow GPU descriptor-table handles for RTVs
// and DSVs, these heap rows need CPU handles only.
struct Dx12CpuDescriptorAllocation
{
    UINT index = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
};

// Compact counters for CPU-only view descriptor heaps.
//
// "Used" is the number of descriptor table rows consumed. It does not mean
// texture memory was allocated. The actual render-target or depth texture lives
// in an ID3D12Resource; this allocator only tracks the view records used to bind
// those resources for rendering.
struct Dx12CpuDescriptorAllocatorStats
{
    const char* heapName = "unknown";
    UINT capacity = 0;
    UINT used = 0;
};

/* -- Dx12CpuDescriptorAllocator
-------------------------------------------------------------------------------------------------------------------------------

    What is a CPU descriptor allocator?

    A descriptor heap is a table of small "view records." Each record tells DX12
    how a resource should be used. For RTV and DSV heaps, those records answer
    questions such as:

    - "when the pixel shader finishes, write color pixels into this texture"
    - "use this depth texture for depth testing"
    - "interpret this resource with this format and view dimension"

    The important split for non-GPU readers:

    - ID3D12Resource is the actual memory, such as a back buffer, reflection
      color texture, or depth texture.
    - A descriptor is only the binding description for that memory.
    - A descriptor allocator does not create textures. It reserves table rows
      where those binding descriptions can be written.

    Why do RTV/DSV descriptors get their own allocator when they are simple?

    The old backend used loose counters named m_nextRTV and m_nextDSV. That was
    compact, but it hid the policy: swap-chain buffers take stable RTV rows,
    the main depth buffer takes a stable DSV row, and framebuffer objects consume
    additional long-lived rows. Giving those counters a named allocator makes
    descriptor usage show up in diagnostics and keeps all descriptor row
    assignment behind one concept.

    This allocator is intentionally simpler than Dx12DescriptorAllocator below.
    RTV/DSV heaps are CPU-only in this renderer, so there is no GPU-visible
    handle and no per-frame transient range. Stable swap-chain rows are
    overwritten for the same engine object; framebuffer rows return through the
    shared retirement fence before the free list may assign them to a new one.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12CpuDescriptorAllocator
{
  public:
    static constexpr UINT MAX_TRACKED_CPU_DESCRIPTORS = 256;
    void Init( ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT capacity, const char* heapName );
    void Reset();

    Dx12CpuDescriptorAllocation Allocate();
    void Free( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle( UINT index ) const;

    Dx12CpuDescriptorAllocatorStats GetStats() const;

  private:
    ID3D12DescriptorHeap* m_heap = nullptr;
    UINT m_descriptorSize = 0;
    UINT m_capacity = 0;
    UINT m_next = 0;
    UINT m_used = 0;
    UINT m_freeCount = 0;
    UINT m_free[MAX_TRACKED_CPU_DESCRIPTORS] = {};
    bool m_allocated[MAX_TRACKED_CPU_DESCRIPTORS] = {};
    const char* m_heapName = "unknown";
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
    UINT staticHighWater = 0;                                    // Peak simultaneously live persistent rows for this device epoch.
    UINT transientCapacityPerFrame = 0;
    UINT transientUsedThisFrame = 0;
    UINT transientPeakThisRun = 0;
    UINT currentFrame = 0;
};

// Generation-tagged opaque id used by the texture registry. The low 24 bits
// store slot+1 so zero remains the null handle; the high 8 bits change whenever
// a tombstone slot is reused. Eight bits deliberately bound the handle to the
// existing uint32_t ABI; after 255 reuses the generation wraps, so callers must
// still release stale ids rather than treating generation tags as eternal IDs.
struct Dx12TextureHandleCodec
{
    static constexpr uint32_t SLOT_BITS = 24u;
    static constexpr uint32_t SLOT_MASK = ( 1u << SLOT_BITS ) - 1u;

    static uint32_t Encode( size_t slot, uint8_t generation )
    {
        return ( static_cast<uint32_t>( generation ) << SLOT_BITS ) | static_cast<uint32_t>( slot + 1u );
    }
    static bool Decode( uint32_t handle, size_t& slot, uint8_t& generation )
    {
        const uint32_t encodedSlot = handle & SLOT_MASK;
        generation = static_cast<uint8_t>( handle >> SLOT_BITS );
        if ( encodedSlot == 0 || generation == 0 )
        {
            return false;
        }
        slot = static_cast<size_t>( encodedSlot - 1u );
        return true;
    }
    static uint8_t NextGeneration( uint8_t generation )
    {
        const uint8_t next = static_cast<uint8_t>( generation + 1u );
        return next == 0 ? 1 : next;
    }
};

/* -- Dx12DescriptorAllocator
------------------------------------------------------------------------------------------------------------------------------------

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

    Typical texture binding flow in this renderer:

    1. Texture load creates the actual ID3D12Resource containing image pixels.
    2. AllocateStatic() reserves a stable descriptor row for that texture.
    3. CreateShaderResourceView writes an SRV descriptor for the texture into
       the CPU-only staging heap at that row.
    4. Before a draw, AllocateTransient() reserves a per-frame shader-visible
       row.
    5. CopyDescriptorsSimple copies the staging descriptor into that transient
       shader-visible row.
    6. SetGraphicsRootDescriptorTable binds the transient row's GPU handle.
    7. The pixel shader follows that handle to read the descriptor, then follows
       the descriptor to sample the texture.

    Step 4 is the reason this class exists. If every draw wrote directly into a
    single shared shader-visible row, a later CPU draw setup could overwrite the
    row while an earlier GPU draw is still using it. Splitting temporary rows by
    frame fence makes that lifetime visible and enforceable.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12DescriptorAllocator
{
  public:
    static constexpr UINT MAX_TRACKED_STATIC_DESCRIPTORS = 4096;
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
    //
    // Plain-language rule: if another object will remember this descriptor
    // number after the current draw call, it needs a static slot.
    UINT AllocateStatic();

    // Return a persistent row only after the frame owner has proved that no
    // in-flight command list can still follow a copied descriptor from it.
    void FreeStatic( UINT index );

    // Reserve one temporary descriptor slot from the current frame's range.
    // Use this for descriptor copies needed while recording this frame.
    //
    // Plain-language rule: if this descriptor exists only so the next draw or
    // dispatch can bind a GPU-visible table row, use a transient slot.
    UINT AllocateTransient();

    // Reserve a contiguous temporary descriptor range.
    //
    // Some DX12 root descriptor tables are not "one independent descriptor";
    // they are a compact array starting at one GPU handle. A shader that reads
    // t0..t7 or u0..u3 expects the next descriptors to live in the next rows of
    // the same heap. Repeated single-slot allocations happen to be contiguous
    // today because this allocator is linear, but that invariant belongs in the
    // API. This method checks the full range before consuming any rows, so an
    // exhaustion failure cannot leave the caller with a half-reserved table.
    UINT AllocateTransientRange( UINT count );

    // Reports whether a complete range fits without mutating counters. Fatal
    // allocation policy remains in AllocateTransientRange; tests and callers
    // may use this only to reason about capacity before committing a table.
    bool CanAllocateTransientRange( UINT count ) const;

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
    UINT ShaderVisibleCapacity() const;
    void ValidateShaderVisibleIndex( UINT index, const char* context ) const;
    void ValidateStagingIndex( UINT index, const char* context ) const;

    ID3D12DescriptorHeap* m_shaderVisibleHeap = nullptr;
    ID3D12DescriptorHeap* m_stagingHeap = nullptr;
    UINT m_descriptorSize = 0;
    UINT m_staticCapacity = 0;
    UINT m_transientCapacityPerFrame = 0;
    UINT m_frameCount = 0;
    UINT m_nextStatic = 0;
    UINT m_staticUsed = 0;                                       // Persistent rows currently owned by live or retiring resources.
    UINT m_staticHighWater = 0;                                  // Peak m_staticUsed; churn must not increase it indefinitely.
    UINT m_freeStaticCount = 0;
    UINT m_freeStatic[MAX_TRACKED_STATIC_DESCRIPTORS] = {};      // Fence-proven reusable row stack.
    bool m_staticAllocated[MAX_TRACKED_STATIC_DESCRIPTORS] = {}; // Double-free/accounting guard.
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
    UINT64 categoryUsedBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {}; // Current frame totals by owner.
    UINT64 categoryPeakBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {}; // Run high-water per owner.
};

/* -- Dx12UploadArena
--------------------------------------------------------------------------------------------------------------------------------------------

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

    // Probe whether the next allocation would fit without arithmetic wrap. The
    // frame owner uses the current runtime phase to drop or cold-flush.
    bool CanAllocate( UINT64 sizeBytes, UINT64 alignment ) const;

    // Reserve bytes and return the GPU address command lists should bind.
    D3D12_GPU_VIRTUAL_ADDRESS Allocate( UINT64 sizeBytes, UINT64 alignment, RenderUploadCategory category );

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
    UINT64 m_categoryUsedBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {};
    UINT64 m_categoryPeakBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {};
};

/* -- Dx12FrameUploadSystem
-------------------------------------------------------------------------------------------------------------------------------------

    What is the frame upload system?

    Upload memory is the renderer's "CPU writes it, GPU reads it later" lane.
    The CPU uses it for things that change during a frame: constants, dynamic
    vertices, instance data, and texture rows. The GPU cannot safely read from a
    normal C++ heap pointer, so DX12 gives us upload resources: buffers that the
    CPU can Map() and the GPU can read by GPU virtual address.

    Why is this more than an array of buffers?

    The CPU and GPU are not synchronized by default. The CPU may start recording
    frame 2 while the GPU is still executing frame 1. If both frames write into
    the same upload bytes, frame 1 can accidentally read frame 2's data. The
    frame upload system gives each in-flight frame allocator its own upload
    arena and resets that arena only after the frame fence says reuse is safe.

    This class owns the actual upload ID3D12Resource objects and their mapped
    CPU pointers. Dx12UploadArena remains the per-frame byte allocator wrapped
    around each resource.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12FrameUploadSystem
{
  public:
    static constexpr UINT MAX_FRAME_COUNT = 4;

    Dx12FrameUploadSystem() = default;
    ~Dx12FrameUploadSystem();
    Dx12FrameUploadSystem( const Dx12FrameUploadSystem& ) = delete;
    Dx12FrameUploadSystem& operator=( const Dx12FrameUploadSystem& ) = delete;

    bool Init( ID3D12Device* device, UINT frameCount, UINT64 capacityBytes, const wchar_t* debugNamePrefix );
    void Shutdown();

    void ResetFrame( UINT frameIndex );
    bool CanAllocate( UINT frameIndex, UINT64 sizeBytes, UINT64 alignment ) const;
    D3D12_GPU_VIRTUAL_ADDRESS
    Allocate( UINT frameIndex, UINT64 sizeBytes, UINT64 alignment, RenderUploadCategory category );
    uint8_t* GetMappedPtr( UINT frameIndex, D3D12_GPU_VIRTUAL_ADDRESS address ) const;
    UINT64 OffsetFromAddress( UINT frameIndex, D3D12_GPU_VIRTUAL_ADDRESS address ) const;
    ID3D12Resource* Resource( UINT frameIndex ) const;
    Dx12UploadArenaStats GetStats( UINT frameIndex ) const;

  private:
    void ValidateFrameIndex( UINT frameIndex ) const;

    ID3D12Resource* m_resources[MAX_FRAME_COUNT] = {};
    uint8_t* m_mappedPtrs[MAX_FRAME_COUNT] = {};
    Dx12UploadArena m_arenas[MAX_FRAME_COUNT];
    UINT m_frameCount = 0;
    UINT64 m_capacityBytes = 0;
};

struct Dx12ReadbackBufferStats
{
    UINT64 sizeBytes = 0;
    bool ready = false;
};

/* -- Dx12ReadbackBuffer
----------------------------------------------------------------------------------------------------------------------------------------

    What is a readback buffer?

    Most GPU resources live in fast GPU-only memory. The CPU cannot just take a
    pointer to the back buffer or a query heap and read bytes from it. To bring
    data back to the CPU, DX12 uses a READBACK heap:

    1. Create a buffer in READBACK memory.
    2. Record a GPU copy or ResolveQueryData into that buffer.
    3. Wait for the fence that proves the copy finished.
    4. Map the readback buffer and copy bytes into normal CPU containers.

    This helper owns that READBACK resource and the Map/Unmap policy. It keeps
    the important lifetime fact visible: mapping is valid only after the GPU copy
    has completed. The caller still controls the fence wait because different
    readbacks have different timing needs. Screenshots block immediately; GPU
    timer readback usually polls without stalling the frame.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12ReadbackBuffer
{
  public:
    Dx12ReadbackBuffer() = default;
    ~Dx12ReadbackBuffer();
    Dx12ReadbackBuffer( const Dx12ReadbackBuffer& ) = delete;
    Dx12ReadbackBuffer& operator=( const Dx12ReadbackBuffer& ) = delete;

    bool InitBuffer( ID3D12Device* device, UINT64 sizeBytes, const wchar_t* debugName );
    void Reset();

    bool IsReady() const
    {
        return m_resource != nullptr;
    }

    ID3D12Resource* Resource() const
    {
        return m_resource;
    }

    void* MapRead( UINT64 sizeBytes ) const;
    void UnmapNoWrite() const;
    // Transfers the COM reference without releasing it. Use only when a failed
    // fence wait cannot prove that the GPU has stopped using the resource.
    ID3D12Resource* DetachAfterUncertainSubmission();
    Dx12ReadbackBufferStats GetStats() const;

  private:
    ID3D12Resource* m_resource = nullptr;
    UINT64 m_sizeBytes = 0;
};

struct Dx12RenderDeviceInitDesc
{
    HWND hwnd = nullptr;
    UINT width = 0;
    UINT height = 0;
    UINT frameCount = 0;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

/* -- Dx12RenderDevice
------------------------------------------------------------------------------------------------------------------------------------------

    What is the render device layer?

    This is the owner for the "engine talks to DirectX" objects that every DX12
    renderer needs before it can draw a triangle:

    - DXGI factory: talks to Windows about adapters, monitors, and swap chains.
    - D3D12 device: creates GPU resources, descriptor heaps, PSOs, fences, and
      command objects.
    - command queue: submits recorded command lists to the GPU.
    - swap chain: owns the presentable back buffers shown by the window.
    - command allocators and command list: CPU-side command recording storage.
    - frame fence and event: proves when the GPU is done with submitted work.

    Why split this out of RenderBackendDX12?

    RenderBackendDX12 owns the public render capability implementations. It
    knows about engine concepts such as textures, meshes, framebuffers, draw
    state, screenshots, DXR reflection, and UI/debug drawing. The low-level DX12
    device objects have a different job: they establish and retire the GPU timeline.

    Keeping device ownership here gives the renderer a clear boundary:

    - this class owns raw COM lifetime for factory/device/queue/swapchain/fence,
    - the backend may borrow those pointers internally through accessors,
    - DX12 helper classes ask the device owner for native handles instead of
      treating RenderBackendDX12's migration aliases as the lifetime owner,
    - shutdown has one place to release the core DX12 objects in a safe order.

    This is not yet the final renderer device from the architecture plan. It is
    the first ownership extraction: enough to remove device/swapchain/fence
    creation and release from the backend facade while preserving current draw
    behavior.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Dx12RenderDevice
{
  public:
    static constexpr UINT MAX_FRAME_COUNT = 4;

    Dx12RenderDevice() = default;
    ~Dx12RenderDevice();
    Dx12RenderDevice( const Dx12RenderDevice& ) = delete;
    Dx12RenderDevice& operator=( const Dx12RenderDevice& ) = delete;

    Basics::SbResult Init( const Dx12RenderDeviceInitDesc& desc );
    void Shutdown();

    bool IsReady() const
    {
        return m_device != nullptr;
    }

    IDXGIFactory4* Factory() const
    {
        return m_factory;
    }

    IDXGISwapChain3* SwapChain() const
    {
        return m_swapChain;
    }

    ID3D12Device* Device() const
    {
        return m_device;
    }

    ID3D12CommandQueue* GraphicsQueue() const
    {
        return m_commandQueue;
    }

    ID3D12GraphicsCommandList* CommandList() const
    {
        return m_commandList;
    }

    ID3D12CommandAllocator* CommandAllocator( UINT index ) const;

    Dx12FenceTimeline& FrameFence()
    {
        return m_frameFence;
    }

    const Dx12FenceTimeline& FrameFence() const
    {
        return m_frameFence;
    }

    UINT FrameCount() const
    {
        return m_frameCount;
    }

    UINT FrameIndex() const
    {
        return m_frameIndex;
    }

    UINT AllocatorIndex() const
    {
        return m_allocatorIndex;
    }

    UINT AdvanceAllocatorIndex();
    UINT RefreshFrameIndexFromSwapChain();

    bool AllowTearing() const
    {
        return m_allowTearing;
    }

  private:
    IDXGIFactory4* m_factory = nullptr;
    IDXGISwapChain3* m_swapChain = nullptr;
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12GraphicsCommandList* m_commandList = nullptr;
    ID3D12CommandAllocator* m_commandAllocators[MAX_FRAME_COUNT] = {};
    ID3D12Fence* m_fence = nullptr;
    HANDLE m_fenceEvent = nullptr;
    Dx12FenceTimeline m_frameFence;
    UINT m_frameCount = 0;
    UINT m_frameIndex = 0;
    UINT m_allocatorIndex = 0;
    bool m_allowTearing = false;
};

} // namespace Rendering
} // namespace SkullbonezCore

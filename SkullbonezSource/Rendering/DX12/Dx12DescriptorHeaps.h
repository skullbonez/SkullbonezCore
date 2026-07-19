/*
File: SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h
Purpose:
  Owns every DX12 descriptor heap, row allocator, and published output handle.

Summary:
  Descriptor storage is one device-epoch domain. This owner creates and names
  the RTV, DSV, shader-visible, and staging heaps; allocates their rows; and
  keeps per-frame transient reuse behind the covering-fence boundary.

Glossary:
  Static row: Persistent SRV/UAV identity copied from staging to the same
    shader-visible index.
  Transient row: Per-frame shader-visible row reusable only after that frame's
    fence completes.
  Output row: CPU-only RTV or DSV record used by the output-merger stage.

Invariants:
  - Heap publication is all-or-nothing for one device epoch.
  - Static and CPU output rows return only after covering-fence retirement.
  - Transient rows reset only for the allocator index whose fence completed.
  - FRAME_COUNT remains owned by Dx12FrameOwner and is supplied at Init.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - Agentic/Reports/2026-07-18/dx12-backend-owner-census.md
*/
#pragma once

#include "RenderDeviceDX12.h"

#include <array>
#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
enum class Dx12CpuDescriptorKind : uint8_t
{
    None,
    Rtv,
    Dsv
};

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
struct Dx12DevelopmentUiDescriptor
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
};

struct Dx12DevelopmentUiDescriptorStats
{
    UINT used = 0;
    UINT capacity = 0;
    UINT highWater = 0;
    uint64_t allocations = 0;
    uint64_t frees = 0;
};
#endif

class Dx12DescriptorHeaps
{
  public:
    static constexpr UINT MAX_RTV_DESCRIPTORS = 32;
    static constexpr UINT MAX_DSV_DESCRIPTORS = 16;
    static constexpr UINT MAX_STATIC_SRVS = 128;
    static constexpr UINT MAX_TRANSIENT_SRVS = 2048;
    static constexpr UINT MAX_FRAME_COUNT = 3;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    static constexpr UINT MAX_DEVELOPMENT_UI_SRVS = 16;
#endif

    SkullbonezCore::Core::SbResult Init( ID3D12Device* device, UINT frameCount );
    void Shutdown();
    void ResetFrame( UINT frameIndex );

    UINT AllocateStatic();
    UINT AllocateTransient();
    UINT AllocateTransientRange( UINT count );
    void FreeStatic( UINT index );
    Dx12DescriptorAllocatorStats GetStats() const;
    D3D12_CPU_DESCRIPTOR_HANDLE StagingCpuHandle( UINT index ) const;
    D3D12_CPU_DESCRIPTOR_HANDLE ShaderVisibleCpuHandle( UINT index ) const;
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleGpuHandle( UINT index ) const;
    void PublishStaticDescriptor( ID3D12Device* device, UINT index ) const;
    void Bind( ID3D12GraphicsCommandList* commandList ) const;

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    Dx12DevelopmentUiDescriptor AllocateDevelopmentUi();
    void FreeDevelopmentUi( D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle );
    void BindDevelopmentUi( ID3D12GraphicsCommandList* commandList ) const;
    ID3D12DescriptorHeap* DevelopmentUiHeap() const
    {
        return m_developmentUiSrvHeap;
    }
    Dx12DevelopmentUiDescriptorStats DevelopmentUiStats() const;
#endif

    Dx12CpuDescriptorAllocation AllocateRtv();
    Dx12CpuDescriptorAllocation AllocateDsv();
    void FreeCpu( Dx12CpuDescriptorKind kind, UINT index );
    Dx12CpuDescriptorAllocatorStats RtvStats() const;
    Dx12CpuDescriptorAllocatorStats DsvStats() const;

    void PublishBackBufferRtv( ID3D12Device* device, UINT frameIndex, ID3D12Resource* resource );
    void RepublishBackBufferRtv( ID3D12Device* device, UINT frameIndex, ID3D12Resource* resource ) const;
    D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtv( UINT frameIndex ) const;
    void PublishMainDsv( ID3D12Device* device, ID3D12Resource* resource );
    D3D12_CPU_DESCRIPTOR_HANDLE MainDsv() const
    {
        return m_mainDsv;
    }

  private:
    ID3D12DescriptorHeap* m_rtvHeap = nullptr;
    ID3D12DescriptorHeap* m_dsvHeap = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    ID3D12DescriptorHeap* m_srvStagingHeap = nullptr;
    Dx12CpuDescriptorAllocator m_rtvRows;
    Dx12CpuDescriptorAllocator m_dsvRows;
    Dx12DescriptorAllocator m_srvRows;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_FRAME_COUNT> m_backBufferRtvs = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_mainDsv = {};
    UINT m_frameCount = 0;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Invariant: vendor texture identities occupy a distinct fixed table, so
    // ImGui never receives the engine bindless heap or its row allocator.
    ID3D12DescriptorHeap* m_developmentUiSrvHeap = nullptr;
    std::array<uint8_t, MAX_DEVELOPMENT_UI_SRVS> m_developmentUiRows = {};
    UINT m_developmentUiDescriptorSize = 0;
    UINT m_developmentUiUsed = 0;
    UINT m_developmentUiHighWater = 0;
    uint64_t m_developmentUiAllocations = 0;
    uint64_t m_developmentUiFrees = 0;
#endif
};
} // namespace Rendering
} // namespace SkullbonezCore

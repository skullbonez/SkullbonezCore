/*
File: SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp
Purpose:
  Creates, publishes, allocates, and destroys the complete DX12 descriptor domain.

Summary:
  One device-epoch transaction creates all four heaps and binds the fixed row
  allocators. Callers receive typed row operations, never raw ownership of a
  heap or allocator.

Glossary:
  Staging heap: CPU-only persistent descriptor templates.
  Shader-visible heap: GPU-readable table containing static and per-frame rows.

Invariants:
  - Partial initialization is rolled back before a recoverable result returns.
  - Output handles are invalid outside the owning device epoch.
  - Capacity exhaustion is fatal in the fixed allocators; no heap grows.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h
  - SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp
*/
#include "Dx12DescriptorHeaps.h"

#include "../../Core/FatalError.h"
#include "../../Core/SbDiagnosticStore.h"

#include <algorithm>

using namespace SkullbonezCore::Rendering;

namespace
{
SkullbonezCore::Core::SbResult DescriptorInitResult( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                     HRESULT result, const char* operation )
{
    if ( FAILED( result ) )
    {
        // Lane R: descriptor heap creation depends on device/driver capacity.
        return resultDiagnostics.Failure( "Dx12DescriptorHeaps", "%s (HRESULT 0x%08X)", operation,
                                          static_cast<unsigned int>( result ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}
} // namespace

SkullbonezCore::Core::SbResult Dx12DescriptorHeaps::Init( ID3D12Device* device, UINT frameCount )
{
    Shutdown();

    if ( !device || frameCount == 0 || frameCount > MAX_FRAME_COUNT )
    {
        return m_resultDiagnostics.Failure( "Dx12DescriptorHeaps",
                                            "Invalid descriptor initialization. device=%p frames=%u max=%u", device,
                                            frameCount, MAX_FRAME_COUNT );
    }

    m_frameCount = frameCount;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = MAX_RTV_DESCRIPTORS;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    SkullbonezCore::Core::SbResult result = DescriptorInitResult( m_resultDiagnostics,
                                                                  device->CreateDescriptorHeap( &desc,
                                                                                                IID_PPV_ARGS( &m_rtvHeap ) ),
                                                                  "CreateDescriptorHeap (RTV) failed" );

    if ( !result.Ok() )
    {
        Shutdown();
        return result;
    }

    NameDx12Object( m_rtvHeap, L"Skullbonez DX12 RTV Heap" );
    m_rtvRows.Init( m_rtvHeap, device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV ),
                    MAX_RTV_DESCRIPTORS, "RTV" );

    desc = {};
    desc.NumDescriptors = MAX_DSV_DESCRIPTORS;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    result = DescriptorInitResult( m_resultDiagnostics, device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_dsvHeap ) ),
                                   "CreateDescriptorHeap (DSV) failed" );

    if ( !result.Ok() )
    {
        Shutdown();
        return result;
    }

    NameDx12Object( m_dsvHeap, L"Skullbonez DX12 DSV Heap" );
    m_dsvRows.Init( m_dsvHeap, device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV ),
                    MAX_DSV_DESCRIPTORS, "DSV" );

    desc = {};
    desc.NumDescriptors = MAX_STATIC_SRVS + ( MAX_TRANSIENT_SRVS * frameCount );
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = DescriptorInitResult( m_resultDiagnostics, device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvHeap ) ),
                                   "CreateDescriptorHeap (SRV) failed" );

    if ( !result.Ok() )
    {
        Shutdown();
        return result;
    }

    NameDx12Object( m_srvHeap, L"Skullbonez DX12 Shader Visible SRV Heap" );

    desc = {};
    desc.NumDescriptors = MAX_STATIC_SRVS;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    result = DescriptorInitResult( m_resultDiagnostics,
                                   device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvStagingHeap ) ),
                                   "CreateDescriptorHeap (staging) failed" );

    if ( !result.Ok() )
    {
        Shutdown();
        return result;
    }

    NameDx12Object( m_srvStagingHeap, L"Skullbonez DX12 SRV Staging Heap" );
    m_srvRows.Init( m_srvHeap, m_srvStagingHeap,
                    device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ), MAX_STATIC_SRVS,
                    MAX_TRANSIENT_SRVS, frameCount );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Concept: development UI textures use a separate bounded shader-visible
    // table. The vendor backend can bind this heap without gaining authority
    // over the engine's global SM6.6 descriptor rows.
    desc = {};
    desc.NumDescriptors = MAX_DEVELOPMENT_UI_SRVS;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = DescriptorInitResult( m_resultDiagnostics,
                                   device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_developmentUiSrvHeap ) ),
                                   "CreateDescriptorHeap (development UI) failed" );

    if ( !result.Ok() )
    {
        Shutdown();
        return result;
    }

    NameDx12Object( m_developmentUiSrvHeap, L"Skore DX12 Development UI SRV Heap" );
    m_developmentUiDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
#endif
    return SkullbonezCore::Core::SbResult::Success();
}

void Dx12DescriptorHeaps::Shutdown()
{
    m_srvRows.Reset();
    m_rtvRows.Reset();
    m_dsvRows.Reset();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( m_developmentUiSrvHeap )
    {
        m_developmentUiSrvHeap->Release();
        m_developmentUiSrvHeap = nullptr;
    }

    m_developmentUiRows = {};
    m_developmentUiDescriptorSize = 0;
    m_developmentUiUsed = 0;
    m_developmentUiHighWater = 0;
    m_developmentUiAllocations = 0;
    m_developmentUiFrees = 0;
#endif

    if ( m_srvStagingHeap )
    {
        m_srvStagingHeap->Release();
        m_srvStagingHeap = nullptr;
    }

    if ( m_srvHeap )
    {
        m_srvHeap->Release();
        m_srvHeap = nullptr;
    }

    if ( m_dsvHeap )
    {
        m_dsvHeap->Release();
        m_dsvHeap = nullptr;
    }

    if ( m_rtvHeap )
    {
        m_rtvHeap->Release();
        m_rtvHeap = nullptr;
    }

    m_backBufferRtvs = {};
    m_mainDsv = {};
    m_frameCount = 0;
}

void Dx12DescriptorHeaps::ResetFrame( UINT frameIndex )
{
    m_srvRows.ResetFrame( frameIndex );
}

UINT Dx12DescriptorHeaps::AllocateStatic()
{
    return m_srvRows.AllocateStatic();
}

UINT Dx12DescriptorHeaps::AllocateTransient()
{
    return m_srvRows.AllocateTransient();
}

UINT Dx12DescriptorHeaps::AllocateTransientRange( UINT count )
{
    return m_srvRows.AllocateTransientRange( count );
}

void Dx12DescriptorHeaps::FreeStatic( UINT index )
{
    m_srvRows.FreeStatic( index );
}

Dx12DescriptorAllocatorStats Dx12DescriptorHeaps::GetStats() const
{
    return m_srvRows.GetStats();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorHeaps::StagingCpuHandle( UINT index ) const
{
    return m_srvRows.StagingCpuHandle( index );
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorHeaps::ShaderVisibleCpuHandle( UINT index ) const
{
    return m_srvRows.ShaderVisibleCpuHandle( index );
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12DescriptorHeaps::ShaderVisibleGpuHandle( UINT index ) const
{
    return m_srvRows.ShaderVisibleGpuHandle( index );
}

void Dx12DescriptorHeaps::PublishStaticDescriptor( ID3D12Device* device, UINT index ) const
{
    if ( !device )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Cannot publish a static descriptor without a device. index=%u", index );
    }

    m_srvRows.PublishStaticDescriptor( device, index );
}

void Dx12DescriptorHeaps::Bind( ID3D12GraphicsCommandList* commandList ) const
{
    if ( !commandList || !m_srvHeap )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Cannot bind the shader-visible heap. command_list=%p heap=%p", commandList,
                  m_srvHeap );
    }

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    commandList->SetDescriptorHeaps( 1, heaps );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
Dx12DevelopmentUiDescriptor Dx12DescriptorHeaps::AllocateDevelopmentUi()
{
    if ( !m_developmentUiSrvHeap || m_developmentUiDescriptorSize == 0u )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor allocation has no live heap." );
    }

    for ( UINT index = 0; index < MAX_DEVELOPMENT_UI_SRVS; ++index )
    {
        if ( m_developmentUiRows[index] != 0u )
        {
            continue;
        }

        m_developmentUiRows[index] = 1u;
        ++m_developmentUiUsed;
        ++m_developmentUiAllocations;
        m_developmentUiHighWater = (std::max)( m_developmentUiHighWater, m_developmentUiUsed );

        Dx12DevelopmentUiDescriptor descriptor;
        descriptor.cpuHandle = m_developmentUiSrvHeap->GetCPUDescriptorHandleForHeapStart();
        descriptor.cpuHandle.ptr += static_cast<SIZE_T>( index ) * m_developmentUiDescriptorSize;
        descriptor.gpuHandle = m_developmentUiSrvHeap->GetGPUDescriptorHandleForHeapStart();
        descriptor.gpuHandle.ptr += static_cast<UINT64>( index ) * m_developmentUiDescriptorSize;
        return descriptor;
    }

    // Lane F: the vendor callback cannot return allocation failure. Capacity is
    // a fixed owner contract, so exhaustion terminates with actionable usage.
    SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor heap exhausted. owner=DearImGui capacity=%u high_water=%u",
              MAX_DEVELOPMENT_UI_SRVS, m_developmentUiHighWater );
}

void Dx12DescriptorHeaps::FreeDevelopmentUi( D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle )
{
    if ( !m_developmentUiSrvHeap || m_developmentUiDescriptorSize == 0u )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor free has no live heap." );
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = m_developmentUiSrvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = m_developmentUiSrvHeap->GetGPUDescriptorHandleForHeapStart();

    if ( cpuHandle.ptr < cpuBase.ptr || gpuHandle.ptr < gpuBase.ptr )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor free received a foreign handle." );
    }

    const SIZE_T cpuOffset = cpuHandle.ptr - cpuBase.ptr;
    const UINT64 gpuOffset = gpuHandle.ptr - gpuBase.ptr;

    if ( cpuOffset % m_developmentUiDescriptorSize != 0u || gpuOffset % m_developmentUiDescriptorSize != 0u ||
         cpuOffset / m_developmentUiDescriptorSize != gpuOffset / m_developmentUiDescriptorSize )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor handles do not name the same row." );
    }

    const UINT index = static_cast<UINT>( cpuOffset / m_developmentUiDescriptorSize );

    if ( index >= MAX_DEVELOPMENT_UI_SRVS || m_developmentUiRows[index] == 0u || m_developmentUiUsed == 0u )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Development UI descriptor double/invalid free. index=%u", index );
    }

    m_developmentUiRows[index] = 0u;
    --m_developmentUiUsed;
    ++m_developmentUiFrees;
}

void Dx12DescriptorHeaps::BindDevelopmentUi( ID3D12GraphicsCommandList* commandList ) const
{
    if ( !commandList || !m_developmentUiSrvHeap )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Cannot bind development UI descriptors. command_list=%p heap=%p", commandList,
                  m_developmentUiSrvHeap );
    }

    ID3D12DescriptorHeap* heaps[] = { m_developmentUiSrvHeap };
    commandList->SetDescriptorHeaps( 1, heaps );
}

Dx12DevelopmentUiDescriptorStats Dx12DescriptorHeaps::DevelopmentUiStats() const
{
    Dx12DevelopmentUiDescriptorStats stats;
    stats.used = m_developmentUiUsed;
    stats.capacity = MAX_DEVELOPMENT_UI_SRVS;
    stats.highWater = m_developmentUiHighWater;
    stats.allocations = m_developmentUiAllocations;
    stats.frees = m_developmentUiFrees;
    return stats;
}
#endif

Dx12CpuDescriptorAllocation Dx12DescriptorHeaps::AllocateRtv()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_rtvRows.GetStats();

    if ( stats.used >= stats.capacity )
    {
        // Invariant: output rows are fixed device-epoch storage; exhaustion is
        // a budget failure, never permission to grow a runtime heap.
        SB_FATAL( "Dx12DescriptorHeaps", "DX12 RTV heap exhausted. heap=%s used=%u capacity=%u",
                  stats.heapName ? stats.heapName : "unknown", stats.used, stats.capacity );
    }

    return m_rtvRows.Allocate();
}

Dx12CpuDescriptorAllocation Dx12DescriptorHeaps::AllocateDsv()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_dsvRows.GetStats();

    if ( stats.used >= stats.capacity )
    {
        // Invariant: output rows are fixed device-epoch storage; exhaustion is
        // a budget failure, never permission to grow a runtime heap.
        SB_FATAL( "Dx12DescriptorHeaps", "DX12 DSV heap exhausted. heap=%s used=%u capacity=%u",
                  stats.heapName ? stats.heapName : "unknown", stats.used, stats.capacity );
    }

    return m_dsvRows.Allocate();
}

void Dx12DescriptorHeaps::FreeCpu( Dx12CpuDescriptorKind kind, UINT index )
{
    if ( index == UINT_MAX || kind == Dx12CpuDescriptorKind::None )
    {
        return;
    }

    if ( kind == Dx12CpuDescriptorKind::Rtv )
    {
        m_rtvRows.Free( index );
        return;
    }

    if ( kind == Dx12CpuDescriptorKind::Dsv )
    {
        m_dsvRows.Free( index );
        return;
    }

    SB_FATAL( "Dx12DescriptorHeaps", "Unknown CPU descriptor kind. kind=%u", static_cast<unsigned int>( kind ) );
}

Dx12CpuDescriptorAllocatorStats Dx12DescriptorHeaps::RtvStats() const
{
    return m_rtvRows.GetStats();
}

Dx12CpuDescriptorAllocatorStats Dx12DescriptorHeaps::DsvStats() const
{
    return m_dsvRows.GetStats();
}

void Dx12DescriptorHeaps::PublishBackBufferRtv( ID3D12Device* device, UINT frameIndex, ID3D12Resource* resource )
{
    if ( !device || !resource )
    {
        SB_FATAL( "Dx12DescriptorHeaps",
                  "Cannot publish a back-buffer RTV without a device and resource. device=%p resource=%p", device,
                  resource );
    }

    if ( frameIndex >= m_frameCount )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Back-buffer RTV index out of range. index=%u frames=%u", frameIndex,
                  m_frameCount );
    }

    if ( m_backBufferRtvs[frameIndex].ptr == 0 )
    {
        m_backBufferRtvs[frameIndex] = AllocateRtv().cpuHandle;
    }

    device->CreateRenderTargetView( resource, nullptr, m_backBufferRtvs[frameIndex] );
}

void Dx12DescriptorHeaps::RepublishBackBufferRtv( ID3D12Device* device, UINT frameIndex, ID3D12Resource* resource ) const
{
    if ( !device || !resource )
    {
        SB_FATAL( "Dx12DescriptorHeaps",
                  "Cannot republish a back-buffer RTV without a device and resource. device=%p resource=%p", device,
                  resource );
    }

    if ( frameIndex >= m_frameCount || m_backBufferRtvs[frameIndex].ptr == 0 )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Cannot republish missing back-buffer RTV. index=%u frames=%u", frameIndex,
                  m_frameCount );
    }

    device->CreateRenderTargetView( resource, nullptr, m_backBufferRtvs[frameIndex] );
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DescriptorHeaps::BackBufferRtv( UINT frameIndex ) const
{
    if ( frameIndex >= m_frameCount )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Back-buffer RTV read out of range. index=%u frames=%u", frameIndex, m_frameCount );
    }

    return m_backBufferRtvs[frameIndex];
}

void Dx12DescriptorHeaps::PublishMainDsv( ID3D12Device* device, ID3D12Resource* resource )
{
    if ( !device || !resource )
    {
        SB_FATAL( "Dx12DescriptorHeaps", "Cannot publish the main DSV without a device and resource. device=%p resource=%p",
                  device, resource );
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_DSV_FLAG_NONE;

    if ( m_mainDsv.ptr == 0 )
    {
        m_mainDsv = AllocateDsv().cpuHandle;
    }

    device->CreateDepthStencilView( resource, &desc, m_mainDsv );
}

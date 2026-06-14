// --- Includes ---
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


// --- Usings ---
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

// --- RenderBackendDX12 Textures methods ---


void RenderBackendDX12::InitGenMipsPipeline()
{
    // -------------------------------------------------------------------------
    // Compile the compute shader from HLSL source
    // -------------------------------------------------------------------------
    std::string csPath = std::string( DATA_ROOT ) + "shaders/generate_mips.hlsl";
    std::ifstream csFile( csPath, std::ios::binary );
    if ( !csFile.is_open() )
    {
        throw std::runtime_error( "Cannot open generate_mips.hlsl: " + csPath );
    }
    std::string csSource( ( std::istreambuf_iterator<char>( csFile ) ),
                          std::istreambuf_iterator<char>() );

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> csBlob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile( csSource.c_str(), csSource.size(), csPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_cs", "cs_5_0", compileFlags, 0, csBlob.GetAddressOf(), errors.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        std::string msg = "generate_mips.hlsl CS compile failed: ";
        if ( errors )
        {
            msg += reinterpret_cast<const char*>( errors->GetBufferPointer() );
        }
        throw std::runtime_error( msg );
    }
    errors.Reset();

    // -------------------------------------------------------------------------
    // Root signature
    // -------------------------------------------------------------------------
    // Param 0: 4 root constants (b0)
    D3D12_ROOT_PARAMETER1 params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: SRV descriptor table (t0)
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 2: UAV descriptor table (u0-u3, 4 consecutive slots)
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static LinearClamp sampler at s0
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 3;
    rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1;
    rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob;
    ThrowIfFailed( D3D12SerializeVersionedRootSignature( &rsDesc, rsBlob.GetAddressOf(), errors.GetAddressOf() ),
                   "GenerateMips root signature serialization failed" );
    errors.Reset();

    ThrowIfFailed( m_device->CreateRootSignature( 0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS( &m_genMipsRS ) ),
                   "CreateRootSignature (genMips) failed" );
    NameDx12Object( m_genMipsRS, L"Skullbonez DX12 Generate Mips Root Signature" );

    // -------------------------------------------------------------------------
    // Compute PSO
    // -------------------------------------------------------------------------
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_genMipsRS;
    psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
    ThrowIfFailed( m_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &m_genMipsPSO ) ),
                   "CreateComputePipelineState (genMips) failed" );
    // A compute PSO is the compute-shader version of a pipeline object: it
    // stores the compiled CS bytecode plus the root signature describing which
    // descriptors/constants the shader can access.
    NameDx12Object( m_genMipsPSO, L"Skullbonez DX12 Generate Mips Compute PSO" );

    // -------------------------------------------------------------------------
    // Null UAV descriptor — used to pad unused UAV table slots so the
    // debug layer doesn't complain about unbound descriptors.
    // -------------------------------------------------------------------------
    m_genMipsNullUAV = AllocateStaticSRV();

    D3D12_UNORDERED_ACCESS_VIEW_DESC nullUAVDesc = {};
    nullUAVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    nullUAVDesc.Texture2D.MipSlice = 0;

    // Create null UAV (null resource = "nothing bound") in staging heap
    m_device->CreateUnorderedAccessView( nullptr, nullptr, &nullUAVDesc, GetSRVStagingCpuHandle( m_genMipsNullUAV ) );

    // Copy to shader-visible heap so it can be referenced by descriptor tables
    D3D12_CPU_DESCRIPTOR_HANDLE svDst = m_srvDescriptors.ShaderVisibleCpuHandle( m_genMipsNullUAV );
    m_device->CopyDescriptorsSimple( 1, svDst, GetSRVStagingCpuHandle( m_genMipsNullUAV ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


void RenderBackendDX12::GenerateMipsGPU( ID3D12Resource* tex, DXGI_FORMAT fmt, UINT w, UINT h, UINT numMips )
{
    if ( numMips <= 1 )
    {
        return;
    }

    // Transition mip 0 from COPY_DEST to NON_PIXEL_SHADER_RESOURCE so the compute
    // shader can sample it. (Subsequent source mips are transitioned at end of each batch.)
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = 0;
        m_commandList->ResourceBarrier( 1, &b );
    }

    UINT srcMip = 0;
    UINT srcMipW = w;
    UINT srcMipH = h;

    while ( srcMip < numMips - 1 )
    {
        UINT mipsToGenerate = (std::min)( numMips - 1 - srcMip, 4u );
        UINT dstW = (std::max)( srcMipW >> 1, 1u );
        UINT dstH = (std::max)( srcMipH >> 1, 1u );

        // ------------------------------------------------------------------
        // Source SRV: single-level view of the source mip.
        //
        // A mip is one resolution level of a texture. The compute shader reads
        // one source mip and writes up to four smaller destination mips. This
        // descriptor says "when the shader reads this SRV, expose only mip N."
        //
        // This descriptor is transient because it is useful only while this
        // command list records the current mip-generation dispatch.
        // ------------------------------------------------------------------
        UINT srcSrvIdx = AllocateTransientSRV();
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = fmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = srcMip;
            srvDesc.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvDescriptors.ShaderVisibleCpuHandle( srcSrvIdx );
            m_device->CreateShaderResourceView( tex, &srvDesc, cpuHandle );
        }

        // ------------------------------------------------------------------
        // UAV slots: 4 consecutive transient slots (u0=base, u1=+1, u2=+2,
        // u3=+3).
        //
        // UAV means Unordered Access View: the shader can write through it.
        // The generate-mips shader writes up to four destination mips in one
        // dispatch. The root signature expects four UAV table entries every
        // time, so unused entries receive a null UAV descriptor rather than a
        // missing table row.
        // ------------------------------------------------------------------
        UINT uavBase = AllocateTransientSRV(); // u0
        AllocateTransientSRV();                // u1
        AllocateTransientSRV();                // u2
        AllocateTransientSRV();                // u3

        for ( UINT i = 0; i < 4; ++i )
        {
            UINT uavIdx = uavBase + i;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvDescriptors.ShaderVisibleCpuHandle( uavIdx );

            if ( i < mipsToGenerate )
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = fmt;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = srcMip + 1 + i;
                m_device->CreateUnorderedAccessView( tex, nullptr, &uavDesc, cpuHandle );
            }
            else
            {
                // Pad with null UAV to keep the debug layer happy
                D3D12_CPU_DESCRIPTOR_HANDLE nullSrc = GetSRVStagingCpuHandle( m_genMipsNullUAV );
                m_device->CopyDescriptorsSimple( 1, cpuHandle, nullSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            }
        }

        // ------------------------------------------------------------------
        // Transition destination mips COPY_DEST → UNORDERED_ACCESS
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.Subresource = srcMip + 1 + i;
            m_commandList->ResourceBarrier( 1, &b );
        }

        // ------------------------------------------------------------------
        // Dispatch the compute shader
        // ------------------------------------------------------------------
        struct GenMipsCB
        {
            UINT NumMipLevels;
            UINT SrcDimension;
            float TexelSizeX;
            float TexelSizeY;
        };
        GenMipsCB cb;
        cb.NumMipLevels = mipsToGenerate;
        cb.SrcDimension = ( ( srcMipW & 1 ) ? 1u : 0u ) | ( ( srcMipH & 1 ) ? 2u : 0u );
        cb.TexelSizeX = 1.0f / static_cast<float>( dstW );
        cb.TexelSizeY = 1.0f / static_cast<float>( dstH );

        m_commandList->SetComputeRootSignature( m_genMipsRS );
        m_commandList->SetPipelineState( m_genMipsPSO );
        m_commandList->SetComputeRoot32BitConstants( 0, 4, &cb, 0 );
        m_commandList->SetComputeRootDescriptorTable( 1, GetSRVGpuHandle( srcSrvIdx ) );
        m_commandList->SetComputeRootDescriptorTable( 2, GetSRVGpuHandle( uavBase ) );

        UINT groupsX = ( dstW + 7 ) / 8;
        UINT groupsY = ( dstH + 7 ) / 8;
        m_commandList->Dispatch( groupsX, groupsY, 1 );

        // UAV barrier: ensures writes complete before next SRV read or UAV write
        {
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = tex;
            m_commandList->ResourceBarrier( 1, &uavBarrier );
        }

        // ------------------------------------------------------------------
        // Transition output mips UNORDERED_ACCESS → NON_PIXEL_SHADER_RESOURCE
        // so the next batch can read them as a source SRV.
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = srcMip + 1 + i;
            m_commandList->ResourceBarrier( 1, &b );
        }

        srcMip += mipsToGenerate;
        srcMipW = (std::max)( srcMipW >> mipsToGenerate, 1u );
        srcMipH = (std::max)( srcMipH >> mipsToGenerate, 1u );
    }

    // All mips are now in NON_PIXEL_SHADER_RESOURCE. Transition ALL_SUBRESOURCES
    // to PIXEL_SHADER_RESOURCE for use in pixel shaders.
    TransitionBarrier( tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    // Force full rebind of graphics state on the next draw call, since we
    // switched root signatures and PSO for the compute dispatch.
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


uint32_t RenderBackendDX12::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool /*linearFilter*/ )
{
    EnsureCommandListOpen();

    // Resolve format and bytes-per-pixel
    DXGI_FORMAT fmt;
    int bytesPerPixel;
    if ( channels == 1 )
    {
        fmt = DXGI_FORMAT_R8_UNORM;
        bytesPerPixel = 1;
    }
    else
    {
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        bytesPerPixel = 4;
    }

    // Convert RGB → RGBA if needed; srcData always has bytesPerPixel channels after this.
    std::vector<uint8_t> rgba;
    const uint8_t* srcData = data;
    if ( channels == 3 )
    {
        rgba.resize( (size_t)w * h * 4 );
        for ( int i = 0; i < w * h; ++i )
        {
            rgba[i * 4 + 0] = data[i * 3 + 0];
            rgba[i * 4 + 1] = data[i * 3 + 1];
            rgba[i * 4 + 2] = data[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        srcData = rgba.data();
        bytesPerPixel = 4;
    }

    // Compute full mip count (log2 of the larger dimension + 1)
    UINT numMips = 1;
    if ( generateMips && m_genMipsPSO )
    {
        UINT mw = static_cast<UINT>( w );
        UINT mh = static_cast<UINT>( h );
        while ( mw > 1 || mh > 1 )
        {
            mw = (std::max)( mw >> 1, 1u );
            mh = (std::max)( mh >> 1, 1u );
            ++numMips;
        }
    }

    // Create the texture resource on the Default Heap.
    // ALLOW_UNORDERED_ACCESS is required when generating mips via compute shader.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = static_cast<UINT64>( w );
    texDesc.Height = static_cast<UINT>( h );
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = static_cast<UINT16>( numMips );
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = ( numMips > 1 )
                        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                        : D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* texResource = nullptr;
    ThrowIfFailed( m_device->CreateCommittedResource(
                       &defaultHeap,
                       D3D12_HEAP_FLAG_NONE,
                       &texDesc,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       nullptr,
                       IID_PPV_ARGS( &texResource ) ),
                   "CreateCommittedResource (texture) failed" );

    // -------------------------------------------------------------------------
    // Upload mip 0 only. GetCopyableFootprints for 1 subresource gives the
    // GPU row-pitch-aligned layout we must write to in the upload buffer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints
    // -------------------------------------------------------------------------
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp0 = {};
    UINT rowCount0;
    UINT64 rowSize0, mip0Bytes;
    m_device->GetCopyableFootprints( &texDesc, 0, 1, 0, &fp0, &rowCount0, &rowSize0, &mip0Bytes );

    FlushUploadBufferIfNeeded( mip0Bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    D3D12_GPU_VIRTUAL_ADDRESS uploadBase = SubAllocateUpload( mip0Bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    const UINT64 baseOffset = uploadBase - m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress();
    uint8_t* uploadDst = GetUploadPtr( uploadBase );

    const UINT srcRowPitch = static_cast<UINT>( w ) * static_cast<UINT>( bytesPerPixel );
    for ( UINT row = 0; row < rowCount0; ++row )
    {
        memcpy( uploadDst + row * fp0.Footprint.RowPitch,
                srcData + row * srcRowPitch,
                srcRowPitch );
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = texResource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_uploadBuffers[m_allocatorIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = fp0;
    srcLoc.PlacedFootprint.Offset = baseOffset + fp0.Offset;

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // -------------------------------------------------------------------------
    // Generate remaining mips on the GPU (compute shader), or transition
    // directly to PIXEL_SHADER_RESOURCE for single-mip textures.
    // -------------------------------------------------------------------------
    if ( numMips > 1 )
    {
        GenerateMipsGPU( texResource, fmt, static_cast<UINT>( w ), static_cast<UINT>( h ), numMips );
        // GenerateMipsGPU ends with all subresources in PIXEL_SHADER_RESOURCE state.
    }
    else
    {
        TransitionBarrier( texResource,
                           D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    }

    // Create a Shader Resource View exposing the full mip chain.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
    UINT srvIdx = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = numMips;
    m_device->CreateShaderResourceView( texResource, &srvDesc, GetSRVStagingCpuHandle( srvIdx ) );

    // Register in texture array (1-based handle)
    TextureEntryDX12 entry = {};
    entry.resource = texResource;
    entry.srvIndex = srvIdx;
    entry.owned = true;
    m_textures.push_back( entry );
    return static_cast<uint32_t>( m_textures.size() );
}


void RenderBackendDX12::BindTexture( uint32_t handle, int slot )
{
    if ( slot < 0 || slot >= TEXTURE_SLOT_COUNT )
    {
        return;
    }
    UINT newSlot;
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        newSlot = UINT_MAX;
    }
    else
    {
        newSlot = m_textures[handle - 1].srvIndex;
    }
    if ( m_boundTexSlot[slot] != newSlot )
    {
        m_boundTexSlot[slot] = newSlot;
        m_texBindingsDirty = true;
    }
}


void RenderBackendDX12::DeleteTexture( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }
    auto& entry = m_textures[handle - 1];
    if ( entry.owned && entry.resource )
    {
        entry.resource->Release();
    }
    entry.resource = nullptr;
    entry.srvIndex = UINT_MAX;
    entry.owned = false;
}


UINT RenderBackendDX12::RegisterSRV( UINT srvIndex )
{
    TextureEntryDX12 entry = {};
    entry.resource = nullptr;
    entry.srvIndex = srvIndex;
    entry.owned = false;
    m_textures.push_back( entry );
    return (uint32_t)m_textures.size(); // 1-based handle
}


void RenderBackendDX12::UnregisterSRV( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }
    auto& entry = m_textures[handle - 1];
    entry.resource = nullptr;
    entry.srvIndex = UINT_MAX;
    entry.owned = false;
}

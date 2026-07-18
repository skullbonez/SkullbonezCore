/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp
Purpose:
  Implements Dx12TextureOwner's texture registry, uploads, descriptors, and mips.

Summary:
  Dx12TextureOwner maps stable engine handles to owned resources and persistent
  SRV rows. It borrows the active backend command stream during upload or mip
  dispatch but stores no backend pointer across calls.

Glossary:
  Upload arena: Frame-scoped CPU-visible staging memory used for texture rows
  before CopyTextureRegion moves them into GPU-owned texture memory.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Texture graph state advances only after its transition barrier was recorded;
    upload failure returns before row copies dereference the staging pointer.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "Dx12RenderGraphExecutor.h"
#include "../../Runtime/WindowConstants.h"
#include "ShaderDX12.h"
#include "ShaderBytecodeManifest.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <memory>
#include <vector>
#include <wrl/client.h>


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
    SkullbonezCore::Core::Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u",
                                             name,
                                             nextIndex,
                                             capacity );
    SkullbonezCore::Core::Log().FlushAll();
}

static inline SkullbonezCore::Core::SbResult Dx12TextureStartupResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: generate-mips setup depends on baked shader assets and driver
        // resource creation. Startup reports that environment failure
        // through the render lifecycle result path.
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "%s (HRESULT 0x%08X)",
                                                        msg ? msg : "DX12 texture startup call failed",
                                                        static_cast<unsigned int>( hr ) );
    }
    return SkullbonezCore::Core::SbResult::Success();
}

// --- RenderBackendDX12 Textures methods ---


bool Dx12TextureCommands::Transition( const char* passName,
                                      const char* resourceName,
                                      ID3D12Resource* resource,
                                      RenderGraphResourceAccess before,
                                      RenderGraphResourceAccess after,
                                      UINT subresource )
{
    if ( !resource || before == after )
    {
        return true;
    }
    if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().ok )
    {
        return false;
    }
    Dx12RenderGraphSingleTransitionDesc desc;
    desc.commandList = m_frame.CommandList();
    desc.resource = resource;
    desc.before = before;
    desc.after = after;
    desc.subresource = subresource;
    const Dx12RenderGraphBarrierRecord record =
        ExecuteDx12RenderGraphSingleTransition( "Dx12Texture", passName, resourceName, desc );
    if ( !record.hasConcreteStates || !record.hasNativeResource || record.missingCommandList ||
         record.beforeState == record.afterState || !record.emitted )
    {
        SB_FATAL( "Dx12TextureOwner",
                  "Texture transition did not emit one concrete barrier. pass=%s resource=%s",
                  passName ? passName : "unknown",
                  resourceName ? resourceName : "unknown" );
    }
    return true;
}


bool Dx12TextureCommands::UavBarrier( const char* passName, const char* resourceName, ID3D12Resource* resource )
{
    if ( !resource )
    {
        return true;
    }
    if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().ok )
    {
        return false;
    }
    Dx12RenderGraphUavBarrierDesc desc;
    desc.commandList = m_frame.CommandList();
    desc.resource = resource;
    const Dx12RenderGraphUavBarrierRecord record =
        ExecuteDx12RenderGraphUavBarrier( "Dx12Texture", passName, resourceName, desc );
    if ( !record.hasNativeResource || record.missingCommandList || !record.emitted )
    {
        SB_FATAL( "Dx12TextureOwner",
                  "Texture UAV barrier did not emit one native barrier. pass=%s resource=%s",
                  passName ? passName : "unknown",
                  resourceName ? resourceName : "unknown" );
    }
    return true;
}


SkullbonezCore::Core::SbResult Dx12TextureOwner::Initialize( Dx12TextureCommands& commands )
{
    // Runtime allocation policy: size every handle slot before steady gameplay.
    // Insert reuses tombstones and fails fatally instead of growing this vector.
    m_registry.Initialize( commands.StaticDescriptorCapacity() );
    // Concept: mip generation is a tiny compute pipeline owned with textures.
    //
    // Runtime texture loading copies only the original image into mip 0. This
    // compute shader downsamples that image into smaller mip levels on the GPU
    // so minified textures stay stable and do not shimmer at distance.
    std::string csPath = std::string( DATA_ROOT ) + "shaders/generate_mips.hlsl";
    ComPtr<ID3DBlob> csBlob;
    ComPtr<ID3DBlob> errors;
    std::string loadError;
    if ( !LoadManifestCurrentShaderBytecode( csPath.c_str(), "cs", csBlob, loadError ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Baked generate-mips shader rejected: %s",
                                                        loadError.c_str() );
    }

    // Concept: this root signature is the binding contract for generate_mips.hlsl.
    //
    // Root parameter [0] is four inline constants. Parameter [1] is the source
    // SRV table, and parameter [2] is a UAV table with up to four destination
    // mip rows. The compute shader and C++ setup must agree exactly on these
    // slots.
    //
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
    SkullbonezCore::Core::SbResult startupResult = Dx12TextureStartupResult(
        D3D12SerializeVersionedRootSignature( &rsDesc, rsBlob.GetAddressOf(), errors.GetAddressOf() ),
        "GenerateMips root signature serialization failed" );
    if ( !startupResult.ok )
    {
        return startupResult;
    }
    errors.Reset();

    startupResult = Dx12TextureStartupResult( commands.Device()->CreateRootSignature( 0,
                                                                                      rsBlob->GetBufferPointer(),
                                                                                      rsBlob->GetBufferSize(),
                                                                                      IID_PPV_ARGS( &m_genMipsRS ) ),
                                              "CreateRootSignature (genMips) failed" );
    if ( !startupResult.ok )
    {
        // Lifetime: a failed COM creation is not allowed to leave a partially
        // published mip pipeline behind for a later reusable initialization.
        if ( m_genMipsRS )
        {
            m_genMipsRS->Release();
            m_genMipsRS = nullptr;
        }
        return startupResult;
    }
    NameDx12Object( m_genMipsRS, L"Skullbonez DX12 Generate Mips Root Signature" );

    // Compute PSO: compiled compute shader plus the root signature above. It is
    // separate from graphics PSOs because no vertex/pixel/raster state exists
    // for a pure compute dispatch.
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_genMipsRS;
    psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
    startupResult = Dx12TextureStartupResult(
        commands.Device()->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &m_genMipsPSO ) ),
        "CreateComputePipelineState (genMips) failed" );
    if ( !startupResult.ok )
    {
        if ( m_genMipsPSO )
        {
            m_genMipsPSO->Release();
            m_genMipsPSO = nullptr;
        }
        m_genMipsRS->Release();
        m_genMipsRS = nullptr;
        return startupResult;
    }
    // A compute PSO is the compute-shader version of a pipeline object: it
    // stores the compiled CS bytecode plus the root signature describing which
    // descriptors/constants the shader can access.
    NameDx12Object( m_genMipsPSO, L"Skullbonez DX12 Generate Mips Compute PSO" );

    // Hazard: the UAV descriptor table always exposes four rows, but the last
    // dispatch batch may generate fewer than four mips. Fill unused rows with a
    // typed null UAV so the debug layer sees a complete descriptor table and
    // the shader never follows an uninitialized descriptor.
    // Lifetime: the mip pipeline is initialized once per device epoch. Its
    // typed null row is process-lifetime and disappears with the descriptor
    // heaps rather than entering the runtime retirement queue.
    m_genMipsNullUAV = commands.AllocateStaticSrv();

    D3D12_UNORDERED_ACCESS_VIEW_DESC nullUAVDesc = {};
    nullUAVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    nullUAVDesc.Texture2D.MipSlice = 0;

    // The null resource means "nothing bound", but the descriptor row itself is
    // valid. Store the template in the CPU-only staging heap first.
    commands.Device()->CreateUnorderedAccessView( nullptr,
                                                  nullptr,
                                                  &nullUAVDesc,
                                                  commands.StagingCpuHandle( m_genMipsNullUAV ) );

    // Copy the null descriptor to a shader-visible row so dispatches can bind it
    // in the same table as real destination mips.
    D3D12_CPU_DESCRIPTOR_HANDLE svDst = commands.ShaderVisibleCpuHandle( m_genMipsNullUAV );
    commands.Device()->CopyDescriptorsSimple( 1,
                                              svDst,
                                              commands.StagingCpuHandle( m_genMipsNullUAV ),
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12TextureOwner::PrepareGenerateMipsShaderReload( ID3D12Device* device,
                                                                                  ID3D12PipelineState*& candidate )
{
    candidate = nullptr;
    if ( !m_genMipsRS )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Generate-mips root signature unavailable for reload" );
    }
    const std::string csPath = std::string( DATA_ROOT ) + "shaders/generate_mips.hlsl";
    ComPtr<ID3DBlob> csBlob;
    std::string loadError;
    if ( !LoadManifestCurrentShaderBytecode( csPath.c_str(), "cs", csBlob, loadError ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Reloaded generate-mips shader rejected: %s",
                                                        loadError.c_str() );
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_genMipsRS;
    desc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    const HRESULT result = device->CreateComputePipelineState( &desc, IID_PPV_ARGS( &candidate ) );
    if ( FAILED( result ) || !candidate )
    {
        if ( candidate )
        {
            candidate->Release();
            candidate = nullptr;
        }
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Reloaded generate-mips PSO creation failed (HRESULT 0x%08X)",
                                                        static_cast<unsigned int>( result ) );
    }
    return SkullbonezCore::Core::SbResult::Success();
}


void Dx12TextureOwner::AdoptGenerateMipsShaderReload( ID3D12PipelineState* candidate )
{
    // Lifetime: the caller drained the GPU and prepared candidate completely.
    // This no-fail swap is the compute half of the all-shader reload commit.
    if ( m_genMipsPSO )
    {
        m_genMipsPSO->Release();
    }
    m_genMipsPSO = candidate;
    NameDx12Object( m_genMipsPSO, L"Skullbonez DX12 Generate Mips Compute PSO" );
}


bool Dx12TextureOwner::GenerateMips( Dx12TextureCommands& commands,
                                     ID3D12Resource* tex,
                                     DXGI_FORMAT fmt,
                                     UINT w,
                                     UINT h,
                                     UINT numMips,
                                     bool& graphicsStateInvalidated )
{
    if ( numMips <= 1 )
    {
        return true;
    }

    // Transition mip 0 from COPY_DEST to SHADER_RESOURCE so compute can sample it
    // now and later pixel passes can sample the same texture without another
    // read-only transition. Stress runs flip those consumers rapidly.
    if ( !commands.Transition( "GenerateMipsMip0",
                               "TextureMip0",
                               tex,
                               RenderGraphResourceAccess::CopyDest,
                               RenderGraphResourceAccess::ShaderResource,
                               0 ) )
    {
        return false;
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
        UINT srcSrvIdx = commands.AllocateTransientSrv();
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = fmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = srcMip;
            srvDesc.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = commands.ShaderVisibleCpuHandle( srcSrvIdx );
            commands.Device()->CreateShaderResourceView( tex, &srvDesc, cpuHandle );
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
        UINT uavBase = commands.AllocateTransientSrvRange( 4 ); // u0..u3, checked as one contiguous table range

        for ( UINT i = 0; i < 4; ++i )
        {
            UINT uavIdx = uavBase + i;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = commands.ShaderVisibleCpuHandle( uavIdx );

            if ( i < mipsToGenerate )
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = fmt;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = srcMip + 1 + i;
                commands.Device()->CreateUnorderedAccessView( tex, nullptr, &uavDesc, cpuHandle );
            }
            else
            {
                // Pad with null UAV to keep the debug layer happy
                D3D12_CPU_DESCRIPTOR_HANDLE nullSrc = commands.StagingCpuHandle( m_genMipsNullUAV );
                commands.Device()->CopyDescriptorsSimple( 1,
                                                          cpuHandle,
                                                          nullSrc,
                                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            }
        }

        // ------------------------------------------------------------------
        // Transition destination mips COPY_DEST → UNORDERED_ACCESS
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            if ( !commands.Transition( "GenerateMipsCopyToUav",
                                       "TextureMip",
                                       tex,
                                       RenderGraphResourceAccess::CopyDest,
                                       RenderGraphResourceAccess::UnorderedAccess,
                                       srcMip + 1 + i ) )
            {
                return false;
            }
        }

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

        // The compute bind invalidates the ordinary graphics recipe. Report the
        // fact to the coordinator as a value; the texture owner never reaches
        // sideways into its pipeline-owner sibling.
        graphicsStateInvalidated = true;
        commands.CommandList()->SetComputeRootSignature( m_genMipsRS );
        commands.CommandList()->SetPipelineState( m_genMipsPSO );
        commands.CommandList()->SetComputeRoot32BitConstants( 0, 4, &cb, 0 );
        commands.CommandList()->SetComputeRootDescriptorTable( 1, commands.ShaderVisibleGpuHandle( srcSrvIdx ) );
        commands.CommandList()->SetComputeRootDescriptorTable( 2, commands.ShaderVisibleGpuHandle( uavBase ) );

        UINT groupsX = ( dstW + 7 ) / 8;
        UINT groupsY = ( dstH + 7 ) / 8;
        commands.CommandList()->Dispatch( groupsX, groupsY, 1 );

        // Hazard: mip N may be written as a UAV in this dispatch and sampled as
        // an SRV in the next dispatch. The UAV barrier orders those writes
        // before any later read/write work continues.
        if ( !commands.UavBarrier( "GenerateMipsUavOrder", "TextureMips", tex ) )
        {
            return false;
        }

        // ------------------------------------------------------------------
        // Transition output mips UNORDERED_ACCESS -> SHADER_RESOURCE so the next
        // compute batch and later pixel passes both see a legal read state.
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            if ( !commands.Transition( "GenerateMipsUavToSrv",
                                       "TextureMip",
                                       tex,
                                       RenderGraphResourceAccess::UnorderedAccess,
                                       RenderGraphResourceAccess::ShaderResource,
                                       srcMip + 1 + i ) )
            {
                return false;
            }
        }

        srcMip += mipsToGenerate;
        srcMipW = (std::max)( srcMipW >> mipsToGenerate, 1u );
        srcMipH = (std::max)( srcMipH >> mipsToGenerate, 1u );
    }

    // Invariant: all mips end in SHADER_RESOURCE. That combined read-only state
    // is legal for pixel and compute consumers, so scene stress can rebuild and
    // immediately sample textures through either shader stage.

    // Force full rebind of graphics state on the next draw call, since we
    // switched root signatures and PSO for the compute dispatch.
    InvalidateBindings();
    return true;
}


uint32_t Dx12TextureOwner::CreateTexture2D( Dx12TextureCommands& commands,
                                            const uint8_t* data,
                                            int w,
                                            int h,
                                            int channels,
                                            bool generateMips,
                                            bool /*linearFilter*/,
                                            bool& graphicsStateInvalidated )
{
    if ( !commands.EnsureOpen().ok )
    {
        return 0;
    }

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

    // Convert RGB to RGBA if needed; srcData always has bytesPerPixel channels after this.
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
    texDesc.Flags = ( numMips > 1 ) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* texResource = nullptr;
    const HRESULT textureResult = commands.Device()->CreateCommittedResource( &defaultHeap,
                                                                              D3D12_HEAP_FLAG_NONE,
                                                                              &texDesc,
                                                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                                                              nullptr,
                                                                              IID_PPV_ARGS( &texResource ) );
    if ( FAILED( textureResult ) || !texResource )
    {
        // Lane R: texture residency can fail because of the active device or
        // memory budget. The renderer texture handle contract already uses 0
        // for "no usable texture", which TextureCollection reports to callers.
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_texture_create_failed width=%d height=%d mips=%u hresult=0x%08X",
            w,
            h,
            numMips,
            static_cast<unsigned int>( FAILED( textureResult ) ? textureResult : E_FAIL ) );
        SkullbonezCore::Core::Log().FlushAll();
        return 0;
    }

    // -------------------------------------------------------------------------
    // Upload mip 0 only. GetCopyableFootprints for 1 subresource gives the
    // GPU row-pitch-aligned layout we must write to in the upload buffer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints
    // -------------------------------------------------------------------------
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp0 = {};
    UINT rowCount0;
    UINT64 rowSize0, mip0Bytes;
    commands.Device()->GetCopyableFootprints( &texDesc, 0, 1, 0, &fp0, &rowCount0, &rowSize0, &mip0Bytes );

    D3D12_GPU_VIRTUAL_ADDRESS uploadBase = commands.ReserveUpload( mip0Bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    if ( uploadBase == 0 )
    {
        texResource->Release();
        return 0;
    }
    const UINT64 baseOffset = commands.UploadOffset( uploadBase );
    uint8_t* uploadDst = commands.UploadPointer( uploadBase );

    const UINT srcRowPitch = static_cast<UINT>( w ) * static_cast<UINT>( bytesPerPixel );
    for ( UINT row = 0; row < rowCount0; ++row )
    {
        memcpy( uploadDst + row * fp0.Footprint.RowPitch, srcData + row * srcRowPitch, srcRowPitch );
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = texResource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = commands.UploadResource();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = fp0;
    srcLoc.PlacedFootprint.Offset = baseOffset + fp0.Offset;

    commands.CommandList()->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // -------------------------------------------------------------------------
    // Generate remaining mips on the GPU (compute shader), or transition
    // directly to PIXEL_SHADER_RESOURCE for single-mip textures.
    // -------------------------------------------------------------------------
    if ( numMips > 1 )
    {
        if ( !GenerateMips( commands,
                            texResource,
                            fmt,
                            static_cast<UINT>( w ),
                            static_cast<UINT>( h ),
                            numMips,
                            graphicsStateInvalidated ) )
        {
            // Lifetime: mip generation may already have recorded commands that
            // reference this candidate. Quarantine it behind the normal fence
            // path instead of releasing a command-list dependency immediately.
            commands.Retire( texResource );
            return 0;
        }
        // GenerateMipsGPU leaves all subresources in combined shader-resource read state.
    }
    else
    {
        if ( !commands.Transition( "TextureUploadFinalPixelSrv",
                                   "Texture2D",
                                   texResource,
                                   RenderGraphResourceAccess::CopyDest,
                                   RenderGraphResourceAccess::PixelShaderResource ) )
        {
            commands.Retire( texResource );
            return 0;
        }
    }

    // The SRV exposes every generated mip so samplers can choose the right
    // level for minified textures.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
    UINT srvIdx = commands.AllocateStaticSrv();
    NameDx12ObjectIndexed( texResource, L"Skullbonez DX12 Texture2D", srvIdx );
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = numMips;
    commands.Device()->CreateShaderResourceView( texResource, &srvDesc, commands.StagingCpuHandle( srvIdx ) );
    commands.PublishStaticDescriptor( srvIdx );

    // Register in the generation-tagged texture table. The low 24 bits name
    // the slot plus one; the high 8 bits reject stale ids after slot reuse.
    TextureEntryDX12 entry = {};
    entry.resource = texResource;
    entry.srvIndex = srvIdx;
    entry.owned = true;
    return ReuseOrAppend( entry );
}


void Dx12TextureOwner::ClearBoundSlotsForSrv( UINT srvIndex )
{
    // Lifetime: m_boundTexSlot stores descriptor row indices, not texture
    // handles. When an FBO or texture unregisters an SRV, clear any cached slot
    // that still names that row before a later draw can publish the retired
    // descriptor index to a bindless shader.
    if ( srvIndex == UINT_MAX )
    {
        return;
    }

    bool cleared = false;
    for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
    {
        if ( m_boundTexSlot[slot] == srvIndex )
        {
            m_boundTexSlot[slot] = UINT_MAX;
            cleared = true;
        }
    }

    if ( cleared )
    {
        m_texBindingsDirty = true;
    }
}


void Dx12TextureOwner::BindTexture( uint32_t handle, int slot )
{
    if ( slot < 0 || slot >= TEXTURE_SLOT_COUNT )
    {
#ifdef _DEBUG
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_bind_texture_slot_out_of_range handle=%u slot=%d valid_payload_indices=0..%d",
            handle,
            slot,
            TEXTURE_SLOT_COUNT - 1 );
#endif
        return;
    }
    const TextureEntryDX12* entry = ResolveEntry( handle );
    const UINT newSlot = entry ? entry->srvIndex : m_nullTextureSRVIndex;
    if ( m_boundTexSlot[slot] != newSlot )
    {
        m_boundTexSlot[slot] = newSlot;
        m_texBindingsDirty = true;
    }
}


void Dx12TextureOwner::DeleteTexture( Dx12TextureCommands& commands, uint32_t handle )
{
    TextureEntryDX12* entry = ResolveEntry( handle );
    if ( !entry )
    {
        return;
    }
    ClearBoundSlotsForSrv( entry->srvIndex );
    if ( entry->owned && entry->resource )
    {
        commands.Retire( entry->resource, entry->srvIndex );
    }
    else
    {
        commands.RetireStaticDescriptor( entry->srvIndex );
    }
    entry->resource = nullptr;
    entry->srvIndex = UINT_MAX;
    entry->owned = false;
}


UINT Dx12TextureOwner::RegisterSRV( UINT srvIndex )
{
    TextureEntryDX12 entry = {};
    entry.resource = nullptr;
    entry.srvIndex = srvIndex;
    entry.owned = false;
    return ReuseOrAppend( entry );
}


UINT Dx12TextureOwner::UnregisterSRV( uint32_t handle )
{
    TextureEntryDX12* entry = ResolveEntry( handle );
    if ( !entry )
    {
        return UINT_MAX;
    }
    const UINT srvIndex = entry->srvIndex;
    ClearBoundSlotsForSrv( srvIndex );
    entry->resource = nullptr;
    entry->srvIndex = UINT_MAX;
    entry->owned = false;
    return srvIndex;
}


void Dx12TextureOwner::Shutdown()
{
    for ( TextureEntryDX12& texture : m_registry.Entries() )
    {
        if ( texture.owned && texture.resource )
        {
            texture.resource->Release();
        }
    }
    m_registry.Clear();
    if ( m_genMipsPSO )
    {
        m_genMipsPSO->Release();
        m_genMipsPSO = nullptr;
    }
    if ( m_genMipsRS )
    {
        m_genMipsRS->Release();
        m_genMipsRS = nullptr;
    }
    m_genMipsNullUAV = UINT_MAX;
    m_nullTextureSRVIndex = UINT_MAX;
    for ( UINT& slot : m_boundTexSlot )
    {
        slot = UINT_MAX;
    }
    m_texBindingsDirty = true;
    m_staleHandleReported = false;
}


UINT Dx12TextureOwner::ResolveBoundSrv( int slot ) const
{
    const UINT bound = m_boundTexSlot[slot];
    return bound == UINT_MAX ? m_nullTextureSRVIndex : bound;
}


void Dx12TextureOwner::SetNullSrvIndex( UINT index )
{
    m_nullTextureSRVIndex = index;
}
void Dx12TextureOwner::MarkBindingsClean()
{
    m_texBindingsDirty = false;
}
void Dx12TextureOwner::InvalidateBindings()
{
    m_texBindingsDirty = true;
}
bool Dx12TextureOwner::BindingsDirty() const
{
    return m_texBindingsDirty;
}
size_t Dx12TextureOwner::RegistryCount() const
{
    return m_registry.Count();
}
size_t Dx12TextureOwner::RegistryCapacity() const
{
    return m_registry.Capacity();
}


UINT Dx12TextureOwner::ResolveSrv( uint32_t handle ) const
{
    const TextureEntryDX12* entry = ResolveEntry( handle );
    return entry ? entry->srvIndex : UINT_MAX;
}


uint32_t Dx12TextureOwner::FindHandleForSrv( UINT srvIndex ) const
{
    const auto& entries = m_registry.Entries();
    for ( size_t index = 0; index < entries.size(); ++index )
    {
        if ( entries[index].srvIndex == srvIndex )
        {
            return Dx12TextureHandleCodec::Encode( index, entries[index].generation );
        }
    }
    return 0;
}


const TextureEntryDX12* Dx12TextureOwner::ResolveEntry( uint32_t handle ) const
{
    const TextureEntryDX12* entry = m_registry.Resolve( handle );
    if ( !entry )
    {
        if ( handle != 0 )
        {
            ReportStaleHandle( handle );
        }
        return nullptr;
    }
    return entry;
}


TextureEntryDX12* Dx12TextureOwner::ResolveEntry( uint32_t handle )
{
    return const_cast<TextureEntryDX12*>( static_cast<const Dx12TextureOwner*>( this )->ResolveEntry( handle ) );
}


uint32_t Dx12TextureOwner::ReuseOrAppend( const TextureEntryDX12& entry )
{
    const uint32_t handle = m_registry.Insert( entry );
    if ( handle == 0 )
    {
        SB_FATAL( "Dx12TextureOwner",
                  "Texture handle slot capacity exhausted. capacity=%u",
                  Dx12TextureHandleCodec::SLOT_MASK );
    }
    return handle;
}


void Dx12TextureOwner::ReportStaleHandle( uint32_t handle ) const
{
#ifdef _DEBUG
    if ( !m_staleHandleReported )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_stale_texture_handle handle=%u", handle );
        m_staleHandleReported = true;
    }
#else
    (void)handle;
#endif
}


uint32_t RenderBackendDX12::CreateTexture2D( const uint8_t* data,
                                             int width,
                                             int height,
                                             int channels,
                                             bool generateMips,
                                             bool linearFilter )
{
    bool graphicsStateInvalidated = false;
    Dx12TextureCommands textureCommands( m_renderDevice, m_frameOwner );
    const uint32_t handle = m_textureOwner.CreateTexture2D( textureCommands,
                                                            data,
                                                            width,
                                                            height,
                                                            channels,
                                                            generateMips,
                                                            linearFilter,
                                                            graphicsStateInvalidated );
    if ( graphicsStateInvalidated )
    {
        m_pipelineOwner.InvalidateCommandState();
    }
    return handle;
}


void RenderBackendDX12::BindTexture( uint32_t handle, int slot )
{
    m_textureOwner.BindTexture( handle, slot );
}


void RenderBackendDX12::DeleteTexture( uint32_t handle )
{
    Dx12TextureCommands textureCommands( m_renderDevice, m_frameOwner );
    m_textureOwner.DeleteTexture( textureCommands, handle );
}

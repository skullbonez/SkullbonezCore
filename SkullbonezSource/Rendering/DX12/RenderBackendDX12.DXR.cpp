/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
Purpose:
  Implements DX12 raytracing setup and dispatch for reflection rendering.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  SBT (Shader Binding Table): DXR table that maps ray records to
  ray-generation, miss, and hit shaders.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  CBV (Constant Buffer View): Descriptor row used when shaders read a packed
  block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  DRED (Device Removed Extended Data): DX12 diagnostic report for GPU device
  loss, breadcrumbs, and page-fault clues.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  through reference-counted objects.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
    must stay explicit.
  - TLAS rebuilds may only consume the terrain instance plus the active model
    instance capacity reserved during DXR initialization.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;
using SkullbonezCore::Basics::SbResult;


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

// --- RenderBackendDX12 DXR methods ---


void RenderBackendDX12::CheckDXRSupport()
{
    m_dxrSupported = false;
    m_device5 = nullptr;
    m_cmdList4 = nullptr;

    // DXR support is optional. The renderer can still run rasterized scenes on
    // DX12 hardware without raytracing, so failing any capability query simply
    // leaves m_dxrSupported false.
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if ( FAILED( Device()->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof( opts5 ) ) ) )
    {
        return;
    }
    if ( opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0 )
    {
        return;
    }

    // Device5/command-list4 expose the DXR entry points. QueryInterface is the
    // COM way to ask whether this device object also supports that newer API.
    if ( FAILED( Device()->QueryInterface( IID_PPV_ARGS( &m_device5 ) ) ) )
    {
        return;
    }

    m_dxrSupported = true;
}


SbResult RenderBackendDX12::CreateRTRootSignature()
{
    // Concept: the raytracing root signature is the binding contract for
    // reflect.rt.hlsl.
    //
    // [0] TLAS SRV: acceleration structure traversed by ray hardware.
    // [1] output UAV: reflection texture written by the ray-generation shader.
    // [2] constants CBV: camera, water plane, and per-dispatch values.
    // [3] texture SRV table: sphere/terrain/sky textures for hit/miss shaders.
    // [s0] sampler: linear wrap sampling for the texture table.
    //
    // Inline descriptors point directly at one GPU virtual address. Descriptor
    // tables point at rows in a shader-visible descriptor heap.
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;

    D3D12_DESCRIPTOR_RANGE1 texRange = {};
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors = 8;
    texRange.BaseShaderRegister = 0;
    texRange.RegisterSpace = 0;

    D3D12_ROOT_PARAMETER1 params[4] = {};
    // Slot 0: TLAS SRV (inline)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 1;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 1: UAV descriptor table
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 2: CBV for RT constants
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 3: SRV descriptor table for sphere + terrain textures
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &texRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static linear-wrap sampler at s0
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 4;
    rootSigDesc.Desc_1_1.pParameters = params;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 1;
    rootSigDesc.Desc_1_1.pStaticSamplers = &samplerDesc;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if ( FAILED(
             D3D12SerializeVersionedRootSignature( &rootSigDesc, signature.GetAddressOf(), error.GetAddressOf() ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "RT root signature serialization failed" );
    }

    // Create the DXR root signature from the serialized blob. Same concept as the raster root
    // signature, but this one defines bindings for raytracing shaders (TLAS, UAV output, CBV, textures).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
    if ( FAILED( Device()->CreateRootSignature( 0,
                                                signature->GetBufferPointer(),
                                                signature->GetBufferSize(),
                                                IID_PPV_ARGS( &m_rtRootSignature ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "CreateRootSignature (RT) failed" );
    }
    NameDx12Object( m_rtRootSignature, L"Skullbonez DX12 Raytracing Root Signature" );
    return SbResult::Success();
}


SbResult RenderBackendDX12::CreateRTPipeline()
{
    // DXR reflection uses checked-in DXIL. Keep compilation in tools/build
    // workflows so runtime startup never shells out or depends on SDK paths.
    std::string dxilPath = std::string( DATA_ROOT ) + "shaders/reflect.rt.dxil";

    FILE* dxilFile = nullptr;
    fopen_s( &dxilFile, dxilPath.c_str(), "rb" );
    if ( !dxilFile )
    {
        return SbResult::Failure( "Rendering/DX12",
                                  "Missing SkullbonezData/shaders/reflect.rt.dxil; rebuild and commit the DXR shader "
                                  "bytecode before using DXR reflection." );
    }
    fseek( dxilFile, 0, SEEK_END );
    long dxilSize = ftell( dxilFile );
    fseek( dxilFile, 0, SEEK_SET );
    std::vector<uint8_t> dxilBlob( (size_t)dxilSize );
    fread( dxilBlob.data(), 1, (size_t)dxilSize, dxilFile );
    fclose( dxilFile );

    // Concept: an RTPSO is assembled from subobjects instead of one flat
    // graphics-pipeline description.
    //
    // The DXIL library supplies shader functions. Hit groups name which closest
    // hit shader handles a geometry category. Shader config declares payload and
    // attribute sizes. Pipeline config caps recursion. The global root signature
    // is the binding contract shared by all those raytracing shaders.
    D3D12_DXIL_LIBRARY_DESC libDesc = {};
    libDesc.DXILLibrary.pShaderBytecode = dxilBlob.data();
    libDesc.DXILLibrary.BytecodeLength = dxilBlob.size();
    libDesc.NumExports = 0; // Export all entry points

    D3D12_HIT_GROUP_DESC terrainHitGroup = {};
    terrainHitGroup.HitGroupExport = L"TerrainHitGroup";
    terrainHitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    terrainHitGroup.ClosestHitShaderImport = L"ClosestHit";

    D3D12_HIT_GROUP_DESC sphereHitGroup = {};
    sphereHitGroup.HitGroupExport = L"SphereHitGroup";
    sphereHitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    sphereHitGroup.ClosestHitShaderImport = L"ClosestHit";

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = 16;  // float3 color + float hitT
    shaderConfig.MaxAttributeSizeInBytes = 8; // float2 barycentrics

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig = {};
    globalRootSig.pGlobalRootSignature = m_rtRootSignature;

    // State objects are assembled from typed subobjects instead of one flat
    // pipeline descriptor.
    D3D12_STATE_SUBOBJECT subobjects[6] = {};

    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &libDesc;

    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &terrainHitGroup;

    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[2].pDesc = &sphereHitGroup;

    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[3].pDesc = &shaderConfig;

    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[4].pDesc = &pipelineConfig;

    subobjects[5].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[5].pDesc = &globalRootSig;

    D3D12_STATE_OBJECT_DESC stateObjDesc = {};
    stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjDesc.NumSubobjects = 6;
    stateObjDesc.pSubobjects = subobjects;

    // Create the DXR Raytracing Pipeline State Object (RTPSO). Unlike a graphics PSO, an RTPSO is
    // built from "subobjects" — a DXIL shader library containing all RT shaders, hit groups that
    // map geometry types to closest-hit shaders, shader config (payload/attribute sizes), pipeline
    // config (max recursion), and the root signature. This is more flexible than graphics PSOs.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-createstateobject
    if ( FAILED( m_device5->CreateStateObject( &stateObjDesc, IID_PPV_ARGS( &m_rtPSO ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "CreateStateObject (RTPSO) failed" );
    }
    // A raytracing state object is the DXR equivalent of a pipeline. It groups
    // the ray-generation, miss, and hit shaders with their shared root binding
    // contract. Naming it makes DRED/PIX output point at the reflection pipeline
    // rather than a generic state object.
    NameDx12Object( m_rtPSO, L"Skullbonez DX12 Reflection Raytracing PSO" );

    // Query the state object for shader identifier lookup (used when building the SBT).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12stateobjectproperties
    if ( FAILED( m_rtPSO->QueryInterface( IID_PPV_ARGS( &m_rtPSOProps ) ) ) || !m_rtPSOProps )
    {
        return SbResult::Failure( "Rendering/DX12", "QueryInterface for RT pipeline shader identifiers failed" );
    }
    return SbResult::Success();
}


SbResult RenderBackendDX12::CreateReflectionUAV( int width, int height )
{
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionInSRVState = false;

    // The reflection texture is the off-screen image written by DXR. It starts
    // in UAV state because the ray-generation shader writes pixels into it.
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = (UINT64)width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Create the reflection UAV texture — this is the output target for DXR ray tracing.
    // Rays are cast from the water surface and the resulting reflections are written here.
    // The ALLOW_UNORDERED_ACCESS flag lets the ray generation shader write to arbitrary pixels.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( Device()->CreateCommittedResource( &heapProps,
                                                    D3D12_HEAP_FLAG_NONE,
                                                    &texDesc,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                    nullptr,
                                                    IID_PPV_ARGS( &m_reflectionUAV ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "Failed to create DXR reflection UAV texture" );
    }
    NameDx12Object( m_reflectionUAV, L"Skullbonez DX12 Reflection UAV Texture" );

    // DispatchRays writes through a UAV row while the later water pass reads the
    // same texture through an SRV row.
    m_reflectionUAVIndex = AllocateStaticSRV();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    Device()->CreateUnorderedAccessView( m_reflectionUAV,
                                         nullptr,
                                         &uavDesc,
                                         GetSRVStagingCpuHandle( m_reflectionUAVIndex ) );

    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapCpu = m_srvDescriptors.ShaderVisibleCpuHandle( m_reflectionUAVIndex );
    Device()->CopyDescriptorsSimple( 1,
                                     srvHeapCpu,
                                     GetSRVStagingCpuHandle( m_reflectionUAVIndex ),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // Create a second descriptor over the same texture for the water shader.
    // Same resource, different view: UAV for writes, SRV for reads.
    m_reflectionSRVIndex = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    Device()->CreateShaderResourceView( m_reflectionUAV, &srvDesc, GetSRVStagingCpuHandle( m_reflectionSRVIndex ) );

    // Copy the SRV template into the shader-visible heap so raster draws can
    // sample the completed reflection texture.
    srvHeapCpu = m_srvDescriptors.ShaderVisibleCpuHandle( m_reflectionSRVIndex );
    Device()->CopyDescriptorsSimple( 1,
                                     srvHeapCpu,
                                     GetSRVStagingCpuHandle( m_reflectionSRVIndex ),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    return SbResult::Success();
}


SbResult RenderBackendDX12::InitDXR( uint64_t terrainVBVA,
                                     int terrainVertCount,
                                     int terrainStride,
                                     uint64_t sphereVBVA,
                                     int sphereVertCount,
                                     int sphereStride,
                                     int maxInstances )
{
    if ( !m_dxrSupported )
    {
        return SbResult::Success();
    }

    // Skip re-initialisation if DXR is already set up (scene reload path). The terrain and sphere
    // meshes (and their BLAS) do not change between scenes — only the TLAS is rebuilt per-frame.
    // The full init path is only needed after a new DX12 device/backend is created, where
    // m_cmdList4 is null and we fall through to the full init below.
    if ( m_cmdList4 )
    {
        return SbResult::Success();
    }

    // Query the command list for the DXR-capable interface. If the runtime
    // cannot provide it, keep raster rendering alive and disable DXR reflection.
    if ( FAILED( CommandList()->QueryInterface( IID_PPV_ARGS( &m_cmdList4 ) ) ) )
    {
        m_dxrSupported = false;
        return SbResult::Success();
    }

    auto failDxrInit = [this]( const SbResult& result )
    {
        ShutdownDXR();
        return result;
    };

    // The binding contract and pipeline must exist before scene acceleration
    // structures can be used by reflection dispatch.
    SbResult setupResult = CreateRTRootSignature();
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }
    setupResult = CreateRTPipeline();
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }

    // Render reflections at 2x viewport size so water distortion has extra
    // detail to sample before the final screen pass.
    setupResult = CreateReflectionUAV( m_width * 2, m_height * 2 );
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }

    // Create RT constant buffer on the upload heap — holds per-frame raytracing parameters
    // (inverse VP matrix, camera position, water height, light position, etc.). Persistently
    // mapped so we can update it every frame without Map/Unmap overhead. 256-byte aligned per DX12 CBV rules.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = 256; // Aligned to 256 bytes for CBV
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if ( FAILED( Device()->CreateCommittedResource( &heapProps,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &bufDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr,
                                                        IID_PPV_ARGS( &m_rtConstantBuffer ) ) ) )
        {
            return failDxrInit( SbResult::Failure( "Rendering/DX12", "Failed to create RT constant buffer" ) );
        }
        NameDx12Object( m_rtConstantBuffer, L"Skullbonez DX12 Raytracing Constants Upload Buffer" );
        m_rtConstantBuffer->Map( 0, nullptr, (void**)&m_rtConstantBufferMapped );
    }

    // Build the static BLAS objects once. The terrain BLAS holds terrain
    // triangles; the sphere BLAS is reused by every moving sphere instance.
    EnsureCommandListOpen();

    setupResult = m_terrainBLAS.Build( m_device5,
                                       m_cmdList4,
                                       (D3D12_GPU_VIRTUAL_ADDRESS)terrainVBVA,
                                       terrainVertCount,
                                       terrainStride,
                                       DXGI_FORMAT_R32G32B32_FLOAT,
                                       true );
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }
    setupResult = m_sphereBLAS.Build( m_device5,
                                      m_cmdList4,
                                      (D3D12_GPU_VIRTUAL_ADDRESS)sphereVBVA,
                                      sphereVertCount,
                                      sphereStride,
                                      DXGI_FORMAT_R32G32B32_FLOAT,
                                      false );
    if ( !setupResult.ok )
    {
        // Why: the terrain BLAS build command may already be recorded. Submit
        // and drain that command before releasing DXR resources during failure
        // cleanup so the command list does not retain references to freed BLAS
        // memory.
        AssertPlatformProfilerGpuStackClosed( "InitDXRFailure" );
        CommandList()->Close();
        m_commandListOpen = false;
        ID3D12CommandList* ppCLs[] = { CommandList() };
        m_commandQueue->ExecuteCommandLists( 1, ppCLs );
        WaitForGpu();
        m_terrainBLAS.ReleaseAfterBuild();
        return failDxrInit( setupResult );
    }

    // Submit and wait for BLAS builds to complete
    AssertPlatformProfilerGpuStackClosed( "InitDXR" );
    CommandList()->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { CommandList() };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Lifetime: scratch buffers are temporary GPU workspaces used only while
    // building acceleration structures. The BLAS result buffers remain alive for
    // ray traversal; scratch can be released after the command list completes.
    m_terrainBLAS.ReleaseAfterBuild();
    m_sphereBLAS.ReleaseAfterBuild();

    // TLAS capacity includes one terrain instance plus the active model buffer.
    // Individual spheres reuse the same sphere BLAS with different transforms.
    m_dxrMaxInstances = std::clamp( maxInstances, 1, MAX_GAME_MODELS );
    setupResult = m_tlas.Init( m_device5, m_dxrMaxInstances + 1 );
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }

    // The SBT is the raytracing dispatch table. It maps the RayGen, Miss,
    // TerrainHitGroup, and SphereHitGroup shader identifiers into GPU-readable
    // records that DispatchRays can follow.
    setupResult = m_sbt.Build( Device(), m_rtPSOProps, L"RayGen", L"Miss", L"TerrainHitGroup", L"SphereHitGroup" );
    if ( !setupResult.ok )
    {
        return failDxrInit( setupResult );
    }
    return SbResult::Success();
}


void RenderBackendDX12::BuildTLAS( const float* instanceTransforms,
                                   int instanceCount,
                                   uint64_t /*terrainBLAS*/,
                                   uint64_t /*sphereBLAS*/ )
{
    if ( !m_dxrSupported || !m_cmdList4 )
    {
        return;
    }
    if ( instanceCount < 0 || instanceCount > MAX_GAME_MODELS ||
         ( m_dxrMaxInstances > 0 && instanceCount > m_dxrMaxInstances ) )
    {
        // Invariant: the TLAS instance buffer was sized during InitDXR for one
        // terrain instance plus the active model capacity. A larger rebuild
        // would overwrite the fixed raytracing instance table.
        SB_FATAL(
            "RenderBackendDX12",
            "DX12 TLAS instance count exceeds active model capacity. requested=%d activeCapacity=%d maxGameModels=%d",
            instanceCount,
            m_dxrMaxInstances,
            MAX_GAME_MODELS );
    }

    // Concept: a TLAS is a scene-level table of instances.
    //
    // Instance 0 is always terrain and uses an identity transform because the
    // terrain BLAS already lives in world space. Instances 1..N are spheres:
    // they all point at the same sphere BLAS, but each descriptor supplies a
    // different world transform and hit-group index.

    // Terrain instance
    D3D12_RAYTRACING_INSTANCE_DESC& terrainInst = m_tlasInstances[0];
    memset( &terrainInst, 0, sizeof( terrainInst ) );
    terrainInst.Transform[0][0] = 1.0f;
    terrainInst.Transform[1][1] = 1.0f;
    terrainInst.Transform[2][2] = 1.0f;
    terrainInst.InstanceMask = 0xFF;
    terrainInst.InstanceContributionToHitGroupIndex = 0;
    terrainInst.AccelerationStructure = m_terrainBLAS.GetResultVA();
    terrainInst.InstanceID = 0;

    // Sphere instances
    for ( int i = 0; i < instanceCount; ++i )
    {
        D3D12_RAYTRACING_INSTANCE_DESC& inst = m_tlasInstances[(size_t)i + 1];
        memset( &inst, 0, sizeof( inst ) );

        // DXR instance transforms store only the upper 3 rows of a 4x4 matrix.
        // The engine matrix arrives as a flat 4x4; copy rotation/scale plus
        // translation into DXR's 3x4 row-major instance layout.
        const float* m = instanceTransforms + i * 16;
        inst.Transform[0][0] = m[0];
        inst.Transform[0][1] = m[4];
        inst.Transform[0][2] = m[8];
        inst.Transform[0][3] = m[12];
        inst.Transform[1][0] = m[1];
        inst.Transform[1][1] = m[5];
        inst.Transform[1][2] = m[9];
        inst.Transform[1][3] = m[13];
        inst.Transform[2][0] = m[2];
        inst.Transform[2][1] = m[6];
        inst.Transform[2][2] = m[10];
        inst.Transform[2][3] = m[14];

        inst.InstanceMask = 0xFF;
        inst.InstanceContributionToHitGroupIndex = 1; // Sphere hit group
        inst.AccelerationStructure = m_sphereBLAS.GetResultVA();
        inst.InstanceID = (UINT)( i + 1 );
    }

    EnsureCommandListOpen();
    m_tlas.Build( m_device5, m_cmdList4, m_tlasInstances.data(), instanceCount + 1 );
}


void RenderBackendDX12::DispatchReflectionRays( const float* invViewProj,
                                                const float* cameraPos,
                                                float waterY,
                                                float time,
                                                const float* lightPos,
                                                int width,
                                                int height,
                                                uint32_t sphereTexHandle,
                                                uint32_t terrainTexHandle,
                                                uint32_t skyUpHandle,
                                                uint32_t skyDownHandle,
                                                uint32_t skyRightHandle,
                                                uint32_t skyLeftHandle,
                                                uint32_t skyFrontHandle,
                                                uint32_t skyBackHandle )
{
    if ( !m_dxrSupported || !m_cmdList4 || !m_rtPSO )
    {
        return;
    }

    (void)width;
    (void)height;

    EnsureCommandListOpen();

    // Hazard: the reflection texture alternates between a writable UAV during
    // DispatchRays and a readable SRV while the water shader samples it. DX12
    // will not infer that transition for us; record it explicitly each frame.
    if ( m_reflectionInSRVState )
    {
        ExecuteGraphTransition( "DxrReflectionSrvToUav",
                                "DxrReflectionTexture",
                                m_reflectionUAV,
                                RenderGraphResourceAccess::PixelShaderResource,
                                RenderGraphResourceAccess::UnorderedAccess );
    }

    // Layout mirrors reflect.rt.hlsl: invVP, camera/water, light/time, and sky
    // colors packed for constant-buffer alignment.
    struct RTConstants
    {
        float invViewProj[16];
        float cameraPos[3];
        float waterY;
        float lightPos[3];
        float time;
        float skyColorTop[3];
        float pad0;
        float skyColorBottom[3];
        float pad1;
    };

    RTConstants cb = {};
    memcpy( cb.invViewProj, invViewProj, 16 * sizeof( float ) );
    cb.cameraPos[0] = cameraPos[0];
    cb.cameraPos[1] = cameraPos[1];
    cb.cameraPos[2] = cameraPos[2];
    cb.waterY = waterY;
    cb.lightPos[0] = lightPos[0];
    cb.lightPos[1] = lightPos[1];
    cb.lightPos[2] = lightPos[2];
    cb.time = time;
    cb.skyColorTop[0] = 0.4f;
    cb.skyColorTop[1] = 0.6f;
    cb.skyColorTop[2] = 0.9f;
    cb.skyColorBottom[0] = 0.7f;
    cb.skyColorBottom[1] = 0.8f;
    cb.skyColorBottom[2] = 0.95f;
    memcpy( m_rtConstantBufferMapped, &cb, sizeof( cb ) );

    // Compute root signature path for raytracing. DXR uses the compute pipeline (not graphics)
    // because ray tracing doesn't use the traditional rasterization pipeline (no vertex/pixel stages).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setcomputerootsignature
    m_cmdList4->SetComputeRootSignature( m_rtRootSignature );

    // Bind the DXR raytracing pipeline state object. SetPipelineState1 is the DXR-specific version
    // that accepts an ID3D12StateObject (RTPSO) instead of a regular ID3D12PipelineState (graphics PSO).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-setpipelinestate1
    m_cmdList4->SetPipelineState1( m_rtPSO );

    // Bind the shader-visible descriptor heap for DXR (same heap as raster, re-bound after compute).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_cmdList4->SetDescriptorHeaps( 1, heaps );

    // Bind the root parameters declared in CreateRTRootSignature():
    // [0] TLAS SRV, [1] output UAV table, [2] constants CBV,
    // [3] texture SRV table.
    m_cmdList4->SetComputeRootShaderResourceView( 0, m_tlas.GetResultVA() );
    m_cmdList4->SetComputeRootDescriptorTable( 1, GetSRVGpuHandle( m_reflectionUAVIndex ) );
    m_cmdList4->SetComputeRootConstantBufferView( 2, m_rtConstantBuffer->GetGPUVirtualAddress() );

    // Root parameter [3] is the material/environment texture table. The shader
    // reads it as t0=sphere, t1=terrain, and t2..t7=sky cube faces.
    const uint32_t texHandles[8] = { sphereTexHandle,
                                     terrainTexHandle,
                                     skyUpHandle,
                                     skyDownHandle,
                                     skyRightHandle,
                                     skyLeftHandle,
                                     skyFrontHandle,
                                     skyBackHandle };
    bool allValid = true;
    for ( int i = 0; i < 8; ++i )
    {
        if ( texHandles[i] == 0 || texHandles[i] > (uint32_t)m_textures.size() )
        {
            allValid = false;
            break;
        }
    }
    if ( allValid )
    {
        // Root parameter [3] is one descriptor table with eight consecutive SRV
        // rows. AllocateTransientSRVRange() checks and reserves all eight rows
        // at once, so an exhausted heap cannot leave a partially reserved table.
        //
        // Contiguous matters because the shader sees this as t0..t7 starting at
        // one base GPU handle. It does not know about our texture registry or
        // individual C++ texture handles.
        UINT slot0 = AllocateTransientSRVRange( 8 );

        for ( int i = 0; i < 8; ++i )
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvDescriptors.ShaderVisibleCpuHandle( slot0 + (UINT)i );
            UINT srcIdx = m_textures[texHandles[i] - 1].srvIndex;
            Device()->CopyDescriptorsSimple( 1,
                                             dst,
                                             GetSRVStagingCpuHandle( srcIdx ),
                                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        m_cmdList4->SetComputeRootDescriptorTable( 3, GetSRVGpuHandle( slot0 ) );
    }

    // DispatchRays — the DXR equivalent of a draw call. This launches one ray per pixel of the
    // reflection texture. The GPU executes the ray generation shader, which casts rays into the
    // scene. When a ray hits geometry, the closest hit shader runs. If nothing is hit, the miss
    // shader runs. Results are written to the reflection UAV texture. The SBT tells the GPU which
    // shader to invoke for each case.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-dispatchrays
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord = m_sbt.RayGenRange();
    dispatchDesc.MissShaderTable = m_sbt.MissRange();
    dispatchDesc.HitGroupTable = m_sbt.HitGroupRange();
    dispatchDesc.Width = (UINT)m_reflectionWidth;
    dispatchDesc.Height = (UINT)m_reflectionHeight;
    dispatchDesc.Depth = 1;

    m_cmdList4->DispatchRays( &dispatchDesc );

    // Hazard: a UAV barrier is an ordering point, not a layout transition. It
    // makes every raytracing write visible before the next pass samples the
    // reflection texture through its SRV descriptor.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    ExecuteGraphUavBarrier( "DxrReflectionWriteOrder", "DxrReflectionTexture", m_reflectionUAV );

    // After ordering the writes, transition the texture into SRV state so the
    // raster water shader can read it.
    ExecuteGraphTransition( "DxrReflectionUavToSrv",
                            "DxrReflectionTexture",
                            m_reflectionUAV,
                            RenderGraphResourceAccess::UnorderedAccess,
                            RenderGraphResourceAccess::PixelShaderResource );
    m_reflectionInSRVState = true;

    // DXR uses the compute root signature/pipeline path. Mark raster state
    // dirty so the next draw restores graphics bindings instead of inheriting
    // raytracing state.
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


uint32_t RenderBackendDX12::GetReflectionUAVTexture() const
{
    // The water renderer speaks in texture handles, not raw descriptor indices.
    // Lazily register the reflection SRV in the normal DX12 texture registry so
    // the DXR output can be bound exactly like an FBO texture.
    if ( m_reflectionSRVIndex == 0 )
    {
        return 0;
    }
    // Reuse an existing registry handle when the reflection SRV has already
    // been exposed to the water path.
    for ( size_t i = 0; i < m_textures.size(); ++i )
    {
        if ( m_textures[i].srvIndex == m_reflectionSRVIndex )
        {
            return (uint32_t)( i + 1 );
        }
    }
    // const_cast is local to this lazy registration path: externally this query
    // remains a handle lookup, while internally the texture registry gains one
    // derived entry for the reflection SRV.
    auto* self = const_cast<RenderBackendDX12*>( this );
    return self->RegisterSRV( m_reflectionSRVIndex );
}


uint64_t RenderBackendDX12::GetInstancedMeshStaticVBVA( uint32_t handle ) const
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return 0;
    }
    const auto& im = m_instancedMeshes[handle - 1];
    return im.staticVB ? im.staticVB->GetGPUVirtualAddress() : 0;
}


int RenderBackendDX12::GetInstancedMeshStaticStride( uint32_t handle ) const
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return 0;
    }
    return m_instancedMeshes[handle - 1].staticStride;
}


void RenderBackendDX12::ShutdownDXR()
{
    m_sbt.Reset();
    m_tlas.Reset();
    m_terrainBLAS.Reset();
    m_sphereBLAS.Reset();

    if ( m_rtConstantBuffer )
    {
        m_rtConstantBuffer->Unmap( 0, nullptr );
        m_rtConstantBuffer->Release();
        m_rtConstantBuffer = nullptr;
        m_rtConstantBufferMapped = nullptr;
    }
    if ( m_reflectionUAV )
    {
        m_reflectionUAV->Release();
        m_reflectionUAV = nullptr;
    }
    if ( m_rtPSOProps )
    {
        m_rtPSOProps->Release();
        m_rtPSOProps = nullptr;
    }
    if ( m_rtPSO )
    {
        m_rtPSO->Release();
        m_rtPSO = nullptr;
    }
    if ( m_rtRootSignature )
    {
        m_rtRootSignature->Release();
        m_rtRootSignature = nullptr;
    }
    if ( m_cmdList4 )
    {
        m_cmdList4->Release();
        m_cmdList4 = nullptr;
    }
    if ( m_device5 )
    {
        m_device5->Release();
        m_device5 = nullptr;
    }
    m_dxrSupported = false;
}

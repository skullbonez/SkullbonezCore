/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
Purpose:
  Implements the concrete DX12 raytracing owner and the backend's narrow
  device/frame coordination around its setup, dispatch, and teardown.

Mental model:
  Dx12RaytracingOwner retains every resource and capability unique to reflected
  ray dispatch. RenderBackendDX12 lends device/frame dependencies per operation
  and remains the only authority that closes, submits, or fences a command list.

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
#include "../../Runtime/WindowConstants.h"
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


// --- Concrete raytracing owner ---


void Dx12RaytracingOwner::ProbeCapability( ID3D12Device* device )
{
    Shutdown();
    m_supported = false;
    m_featureResult = SbResult::Success();
    m_device5 = nullptr;
    m_commandList4 = nullptr;

    // DXR support is optional. The renderer can still run rasterized scenes on
    // DX12 hardware without raytracing, so failing any capability query simply
    // leaves the concrete owner's supported flag false.
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    const HRESULT featureResult = device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof( opts5 ) );
    if ( FAILED( featureResult ) )
    {
        m_featureResult = SbResult::Failure( "Rendering/DX12Optional",
                                             "DXR capability query failed (HRESULT 0x%08X); raster fallback active",
                                             static_cast<unsigned int>( featureResult ) );
        Log().WriteEventf( "dx12_optional_fallback owner=%s message=\"%s\"",
                           m_featureResult.error.owner,
                           m_featureResult.error.message );
        return;
    }
    if ( opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0 )
    {
        m_featureResult =
            SbResult::Failure( "Rendering/DX12Optional", "DXR tier 1.0 is unavailable; raster fallback active" );
        Log().WriteEventf( "dx12_optional_fallback owner=%s message=\"%s\"",
                           m_featureResult.error.owner,
                           m_featureResult.error.message );
        return;
    }

    // Device5/command-list4 expose the DXR entry points. QueryInterface is the
    // COM way to ask whether this device object also supports that newer API.
    const HRESULT deviceInterfaceResult = device->QueryInterface( IID_PPV_ARGS( &m_device5 ) );
    if ( FAILED( deviceInterfaceResult ) )
    {
        m_featureResult =
            SbResult::Failure( "Rendering/DX12Optional",
                               "DXR device interface query failed (HRESULT 0x%08X); raster fallback active",
                               static_cast<unsigned int>( deviceInterfaceResult ) );
        Log().WriteEventf( "dx12_optional_fallback owner=%s message=\"%s\"",
                           m_featureResult.error.owner,
                           m_featureResult.error.message );
        return;
    }

    m_supported = true;
}

bool Dx12RaytracingOwner::Supported() const
{
    return m_supported;
}

bool Dx12RaytracingOwner::Initialized() const
{
    return m_commandList4 != nullptr && m_pipeline != nullptr && m_reflectionTexture != nullptr;
}

const SbResult& Dx12RaytracingOwner::FeatureResult() const
{
    return m_featureResult;
}

void RenderBackendDX12::CheckDXRSupport()
{
    // Re-probe is a publication boundary: revoke any previously exposed
    // reflection handle before the concrete owner replaces its capability and
    // resource lifetime.
    ShutdownDXR();
    m_raytracingOwner.ProbeCapability( Device() );
}


SbResult Dx12RaytracingOwner::CreateRootSignature( ID3D12Device* device )
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
    if ( FAILED( device->CreateRootSignature( 0,
                                              signature->GetBufferPointer(),
                                              signature->GetBufferSize(),
                                              IID_PPV_ARGS( &m_rootSignature ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "CreateRootSignature (RT) failed" );
    }
    NameDx12Object( m_rootSignature, L"Skullbonez DX12 Raytracing Root Signature" );
    return SbResult::Success();
}


SbResult Dx12RaytracingOwner::CreatePipeline()
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
    globalRootSig.pGlobalRootSignature = m_rootSignature;

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
    if ( FAILED( m_device5->CreateStateObject( &stateObjDesc, IID_PPV_ARGS( &m_pipeline ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "CreateStateObject (RTPSO) failed" );
    }
    // A raytracing state object is the DXR equivalent of a pipeline. It groups
    // the ray-generation, miss, and hit shaders with their shared root binding
    // contract. Naming it makes DRED/PIX output point at the reflection pipeline
    // rather than a generic state object.
    NameDx12Object( m_pipeline, L"Skullbonez DX12 Reflection Raytracing PSO" );

    // Query the state object for shader identifier lookup (used when building the SBT).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12stateobjectproperties
    if ( FAILED( m_pipeline->QueryInterface( IID_PPV_ARGS( &m_pipelineProperties ) ) ) || !m_pipelineProperties )
    {
        return SbResult::Failure( "Rendering/DX12", "QueryInterface for RT pipeline shader identifiers failed" );
    }
    return SbResult::Success();
}


SbResult Dx12RaytracingOwner::CreateReflectionTexture( ID3D12Device* device,
                                                       Dx12DescriptorAllocator& descriptors,
                                                       int width,
                                                       int height )
{
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionInSrvState = false;

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
    if ( FAILED( device->CreateCommittedResource( &heapProps,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &texDesc,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_reflectionTexture ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "Failed to create DXR reflection UAV texture" );
    }
    NameDx12Object( m_reflectionTexture, L"Skullbonez DX12 Reflection UAV Texture" );

    // DispatchRays writes through a UAV row while the later water pass reads the
    // same texture through an SRV row.
    // Lifetime: both reflection rows belong to the optional raytracing owner
    // for the device epoch. Resize does not rebuild them; owner shutdown occurs
    // only after the backend drains GPU work and then discards the heaps.
    m_reflectionUavIndex = descriptors.AllocateStatic();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView( m_reflectionTexture,
                                       nullptr,
                                       &uavDesc,
                                       descriptors.StagingCpuHandle( m_reflectionUavIndex ) );

    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapCpu = descriptors.ShaderVisibleCpuHandle( m_reflectionUavIndex );
    device->CopyDescriptorsSimple( 1,
                                   srvHeapCpu,
                                   descriptors.StagingCpuHandle( m_reflectionUavIndex ),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // Create a second descriptor over the same texture for the water shader.
    // Same resource, different view: UAV for writes, SRV for reads.
    m_reflectionSrvIndex = descriptors.AllocateStatic();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView( m_reflectionTexture,
                                      &srvDesc,
                                      descriptors.StagingCpuHandle( m_reflectionSrvIndex ) );

    // Copy the SRV template into the shader-visible heap so raster draws can
    // sample the completed reflection texture.
    srvHeapCpu = descriptors.ShaderVisibleCpuHandle( m_reflectionSrvIndex );
    device->CopyDescriptorsSimple( 1,
                                   srvHeapCpu,
                                   descriptors.StagingCpuHandle( m_reflectionSrvIndex ),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    return SbResult::Success();
}


Dx12RaytracingSetupOutcome Dx12RaytracingOwner::BeginSetup( ID3D12Device* device,
                                                            ID3D12GraphicsCommandList* commandList,
                                                            Dx12DescriptorAllocator& descriptors,
                                                            int renderWidth,
                                                            int renderHeight,
                                                            uint64_t terrainVBVA,
                                                            int terrainVertCount,
                                                            int terrainStride,
                                                            uint64_t sphereVBVA,
                                                            int sphereVertCount,
                                                            int sphereStride )
{
    Dx12RaytracingSetupOutcome outcome;
    if ( !m_supported )
    {
        return outcome;
    }

    // Skip re-initialisation if DXR is already set up (scene reload path). The terrain and sphere
    // meshes (and their BLAS) do not change between scenes — only the TLAS is rebuilt per-frame.
    // The full init path is only needed after a new DX12 device/backend is created, where
    // m_commandList4 is null and we fall through to the full init below.
    if ( m_commandList4 )
    {
        return outcome;
    }

    // Query the command list for the DXR-capable interface. If the runtime
    // cannot provide it, keep raster rendering alive, disable DXR reflection,
    // and retain one bounded reason so diagnostics can explain the fallback.
    const HRESULT commandInterfaceResult = commandList->QueryInterface( IID_PPV_ARGS( &m_commandList4 ) );
    if ( FAILED( commandInterfaceResult ) )
    {
        m_featureResult =
            SbResult::Failure( "Rendering/DX12Optional",
                               "DXR command-list interface query failed (HRESULT 0x%08X); raster fallback active",
                               static_cast<unsigned int>( commandInterfaceResult ) );
        Log().WriteEventf( "dx12_optional_fallback owner=%s message=\"%s\"",
                           m_featureResult.error.owner,
                           m_featureResult.error.message );
        m_supported = false;
        return outcome;
    }

    // The binding contract and pipeline must exist before scene acceleration
    // structures can be used by reflection dispatch.
    SbResult setupResult = CreateRootSignature( device );
    if ( !setupResult.ok )
    {
        outcome.result = setupResult;
        return outcome;
    }
    setupResult = CreatePipeline();
    if ( !setupResult.ok )
    {
        outcome.result = setupResult;
        return outcome;
    }

    // Render reflections at 2x viewport size so water distortion has extra
    // detail to sample before the final screen pass.
    setupResult = CreateReflectionTexture( device, descriptors, renderWidth * 2, renderHeight * 2 );
    if ( !setupResult.ok )
    {
        outcome.result = setupResult;
        return outcome;
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

        if ( FAILED( device->CreateCommittedResource( &heapProps,
                                                      D3D12_HEAP_FLAG_NONE,
                                                      &bufDesc,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                                      nullptr,
                                                      IID_PPV_ARGS( &m_constantBuffer ) ) ) )
        {
            outcome.result = SbResult::Failure( "Rendering/DX12", "Failed to create RT constant buffer" );
            return outcome;
        }
        NameDx12Object( m_constantBuffer, L"Skullbonez DX12 Raytracing Constants Upload Buffer" );
        void* rawMapped = nullptr;
        const HRESULT mapResult = m_constantBuffer->Map( 0, nullptr, &rawMapped );
        const Dx12MappedPointerResult checkedMap =
            ValidateDx12MappedPointer( mapResult, rawMapped, "DXR constant buffer Map" );
        if ( !checkedMap.result.ok )
        {
            outcome.result = checkedMap.result;
            return outcome;
        }
        m_constantBufferMapped = static_cast<uint8_t*>( checkedMap.pointer );
    }

    // Build the static BLAS objects once. The terrain BLAS holds terrain
    // triangles; the sphere BLAS is reused by every moving sphere instance.
    setupResult = m_terrainBlas.Build( m_device5,
                                       m_commandList4,
                                       (D3D12_GPU_VIRTUAL_ADDRESS)terrainVBVA,
                                       terrainVertCount,
                                       terrainStride,
                                       DXGI_FORMAT_R32G32B32_FLOAT,
                                       true );
    if ( !setupResult.ok )
    {
        outcome.result = setupResult;
        return outcome;
    }
    outcome.recordedBuildWork = true;
    setupResult = m_sphereBlas.Build( m_device5,
                                      m_commandList4,
                                      (D3D12_GPU_VIRTUAL_ADDRESS)sphereVBVA,
                                      sphereVertCount,
                                      sphereStride,
                                      DXGI_FORMAT_R32G32B32_FLOAT,
                                      false );
    if ( !setupResult.ok )
    {
        outcome.result = setupResult;
        return outcome;
    }
    return outcome;
}

SbResult Dx12RaytracingOwner::CompleteSetup( ID3D12Device* device, int maxInstances )
{
    // Lifetime: the backend calls this only after its queue fence proves the
    // recorded BLAS builds complete. Scratch can then be released safely.
    m_terrainBlas.ReleaseAfterBuild();
    m_sphereBlas.ReleaseAfterBuild();

    m_maxInstances = std::clamp( maxInstances, 1, MAX_GAME_MODELS );
    SbResult setupResult = m_tlas.Init( m_device5, m_maxInstances + 1 );
    if ( !setupResult.ok )
    {
        return setupResult;
    }

    // The SBT is the raytracing dispatch table. It maps the RayGen, Miss,
    // TerrainHitGroup, and SphereHitGroup shader identifiers into GPU-readable
    // records that DispatchRays can follow.
    setupResult =
        m_sbt.Build( device, m_pipelineProperties, L"RayGen", L"Miss", L"TerrainHitGroup", L"SphereHitGroup" );
    if ( !setupResult.ok )
    {
        return setupResult;
    }
    return SbResult::Success();
}

void Dx12RaytracingOwner::AbortSetup( const SbResult& failure )
{
    // Lane R: optional raytracing setup may fail while raster rendering stays
    // available. Retain one bounded reason after releasing safely drained DXR
    // resources so diagnostics can explain the fallback.
    m_featureResult = failure;
    Shutdown();
}


SbResult RenderBackendDX12::InitDXR( uint64_t terrainVBVA,
                                     int terrainVertCount,
                                     int terrainStride,
                                     uint64_t sphereVBVA,
                                     int sphereVertCount,
                                     int sphereStride,
                                     int maxInstances )
{
    if ( !m_raytracingOwner.Supported() || m_raytracingOwner.Initialized() )
    {
        return SbResult::Success();
    }
    const SbResult openResult = EnsureCommandListOpen();
    if ( !openResult.ok )
    {
        return openResult;
    }

    const Dx12RaytracingSetupOutcome setup = m_raytracingOwner.BeginSetup( Device(),
                                                                           CommandList(),
                                                                           m_frameOwner.Descriptors(),
                                                                           m_width,
                                                                           m_height,
                                                                           terrainVBVA,
                                                                           terrainVertCount,
                                                                           terrainStride,
                                                                           sphereVBVA,
                                                                           sphereVertCount,
                                                                           sphereStride );
    if ( setup.recordedBuildWork )
    {
        // Lifetime: only the frame/device coordinator closes, submits, and
        // fences command work. The raytracing owner reports whether it emitted
        // BLAS commands so the coordinator can prove their completion before
        // scratch memory is released.
        AssertPlatformProfilerGpuStackClosed( "InitDXR command list Close" );
        const SbResult closeResult = m_frameOwner.CommitClose( CommandList()->Close(), "InitDXR command list Close" );
        if ( !closeResult.ok )
        {
            return closeResult;
        }
        const SbResult submitResult = SubmitClosedCommandList();
        if ( !submitResult.ok )
        {
            return submitResult;
        }
        const SbResult waitResult = m_frameOwner.CommitWait( WaitForGpu() );
        if ( !waitResult.ok )
        {
            return waitResult;
        }
    }

    if ( !setup.result.ok )
    {
        m_raytracingOwner.AbortSetup( setup.result );
        return setup.result;
    }
    if ( !m_raytracingOwner.Supported() )
    {
        // Optional command-list capability failure selects raster fallback and
        // is not a fatal renderer initialization error.
        return SbResult::Success();
    }
    const SbResult completeResult = m_raytracingOwner.CompleteSetup( Device(), maxInstances );
    if ( !completeResult.ok )
    {
        m_raytracingOwner.AbortSetup( completeResult );
        return completeResult;
    }
    // Lifetime: publish the water-facing texture handle during cold DXR setup.
    // The steady render query is a pure value read and cannot grow the texture
    // registry under the runtime allocation guard.
    const UINT reflectionSrvIndex = m_raytracingOwner.ReflectionSrvIndex();
    if ( reflectionSrvIndex != 0 )
    {
        m_reflectionTextureHandle = m_textureOwner.RegisterSRV( reflectionSrvIndex );
    }
    return SbResult::Success();
}


void RenderBackendDX12::BuildTLAS( const float* instanceTransforms,
                                   int instanceCount,
                                   uint64_t /*terrainBLAS*/,
                                   uint64_t /*sphereBLAS*/ )
{
    if ( !m_raytracingOwner.Supported() || !EnsureCommandListOpen().ok )
    {
        return;
    }
    const SbResult buildResult = m_raytracingOwner.BuildScene( instanceTransforms, instanceCount );
    if ( !buildResult.ok )
    {
        [[maybe_unused]] const SbResult retainedFailure = m_frameOwner.RetainFailure( buildResult );
    }
}


SbResult Dx12RaytracingOwner::BuildScene( const float* instanceTransforms, int instanceCount )
{
    if ( !m_supported || !m_commandList4 )
    {
        return SbResult::Success();
    }
    if ( instanceCount < 0 || instanceCount > MAX_GAME_MODELS ||
         ( m_maxInstances > 0 && instanceCount > m_maxInstances ) )
    {
        // Invariant: the TLAS instance buffer was sized during InitDXR for one
        // terrain instance plus the active model capacity. A larger rebuild
        // would overwrite the fixed raytracing instance table.
        SB_FATAL(
            "RenderBackendDX12",
            "DX12 TLAS instance count exceeds active model capacity. requested=%d activeCapacity=%d maxGameModels=%d",
            instanceCount,
            m_maxInstances,
            MAX_GAME_MODELS );
    }

    // Concept: a TLAS is a scene-level table of instances.
    //
    // Instance 0 is always terrain and uses an identity transform because the
    // terrain BLAS already lives in world space. Instances 1..N are spheres:
    // they all point at the same sphere BLAS, but each descriptor supplies a
    // different world transform and hit-group index.

    // Terrain instance
    D3D12_RAYTRACING_INSTANCE_DESC& terrainInst = m_instances[0];
    memset( &terrainInst, 0, sizeof( terrainInst ) );
    terrainInst.Transform[0][0] = 1.0f;
    terrainInst.Transform[1][1] = 1.0f;
    terrainInst.Transform[2][2] = 1.0f;
    terrainInst.InstanceMask = 0xFF;
    terrainInst.InstanceContributionToHitGroupIndex = 0;
    terrainInst.AccelerationStructure = m_terrainBlas.GetResultVA();
    terrainInst.InstanceID = 0;

    // Sphere instances
    for ( int i = 0; i < instanceCount; ++i )
    {
        D3D12_RAYTRACING_INSTANCE_DESC& inst = m_instances[(size_t)i + 1];
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
        inst.AccelerationStructure = m_sphereBlas.GetResultVA();
        inst.InstanceID = (UINT)( i + 1 );
    }

    return m_tlas.Build( m_device5, m_commandList4, m_instances.data(), instanceCount + 1 );
}


Dx12RaytracingDispatchOutcome Dx12RaytracingOwner::DispatchReflections( ID3D12Device* device,
                                                                        ID3D12DescriptorHeap* shaderVisibleHeap,
                                                                        Dx12DescriptorAllocator& descriptors,
                                                                        const Dx12TextureOwner& textures,
                                                                        const float* invViewProj,
                                                                        const float* cameraPos,
                                                                        float waterY,
                                                                        float time,
                                                                        const float* lightPos,
                                                                        const float* skyColorTop,
                                                                        const float* skyColorBottom,
                                                                        const uint32_t textureHandles[8] )
{
    Dx12RaytracingDispatchOutcome outcome;
    if ( !m_supported || !m_commandList4 || !m_pipeline )
    {
        return outcome;
    }

    // Hazard: the reflection texture alternates between a writable UAV during
    // DispatchRays and a readable SRV while the water shader samples it. DX12
    // will not infer that transition for us; record it explicitly each frame.
    if ( m_reflectionInSrvState )
    {
        D3D12_RESOURCE_BARRIER transition = {};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = m_reflectionTexture;
        transition.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList4->ResourceBarrier( 1, &transition );
        m_reflectionInSrvState = false;
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
    static constexpr float DEFAULT_SKY_COLOR_TOP[3] = { 0.4f, 0.6f, 0.9f };
    static constexpr float DEFAULT_SKY_COLOR_BOTTOM[3] = { 0.7f, 0.8f, 0.95f };
    const float* resolvedSkyColorTop = skyColorTop ? skyColorTop : DEFAULT_SKY_COLOR_TOP;
    const float* resolvedSkyColorBottom = skyColorBottom ? skyColorBottom : DEFAULT_SKY_COLOR_BOTTOM;
    cb.skyColorTop[0] = resolvedSkyColorTop[0];
    cb.skyColorTop[1] = resolvedSkyColorTop[1];
    cb.skyColorTop[2] = resolvedSkyColorTop[2];
    cb.skyColorBottom[0] = resolvedSkyColorBottom[0];
    cb.skyColorBottom[1] = resolvedSkyColorBottom[1];
    cb.skyColorBottom[2] = resolvedSkyColorBottom[2];
    memcpy( m_constantBufferMapped, &cb, sizeof( cb ) );

    // Compute root signature path for raytracing. DXR uses the compute pipeline (not graphics)
    // because ray tracing doesn't use the traditional rasterization pipeline (no vertex/pixel stages).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setcomputerootsignature
    m_commandList4->SetComputeRootSignature( m_rootSignature );

    // Bind the DXR raytracing pipeline state object. SetPipelineState1 is the DXR-specific version
    // that accepts an ID3D12StateObject (RTPSO) instead of a regular ID3D12PipelineState (graphics PSO).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-setpipelinestate1
    m_commandList4->SetPipelineState1( m_pipeline );

    // Bind the shader-visible descriptor heap for DXR (same heap as raster, re-bound after compute).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
    ID3D12DescriptorHeap* heaps[] = { shaderVisibleHeap };
    m_commandList4->SetDescriptorHeaps( 1, heaps );

    // Bind the root parameters declared in CreateRTRootSignature():
    // [0] TLAS SRV, [1] output UAV table, [2] constants CBV,
    // [3] texture SRV table.
    m_commandList4->SetComputeRootShaderResourceView( 0, m_tlas.GetResultVA() );
    m_commandList4->SetComputeRootDescriptorTable( 1, descriptors.ShaderVisibleGpuHandle( m_reflectionUavIndex ) );
    m_commandList4->SetComputeRootConstantBufferView( 2, m_constantBuffer->GetGPUVirtualAddress() );

    // Root parameter [3] is the material/environment texture table. The shader
    // reads it as t0=sphere, t1=terrain, and t2..t7=sky cube faces.
    bool allValid = true;
    for ( int i = 0; i < 8; ++i )
    {
        if ( textures.ResolveSrv( textureHandles[i] ) == UINT_MAX )
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
        UINT slot0 = descriptors.AllocateTransientRange( 8 );

        for ( int i = 0; i < 8; ++i )
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dst = descriptors.ShaderVisibleCpuHandle( slot0 + (UINT)i );
            UINT srcIdx = textures.ResolveSrv( textureHandles[i] );
            device->CopyDescriptorsSimple( 1,
                                           dst,
                                           descriptors.StagingCpuHandle( srcIdx ),
                                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        m_commandList4->SetComputeRootDescriptorTable( 3, descriptors.ShaderVisibleGpuHandle( slot0 ) );
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

    m_commandList4->DispatchRays( &dispatchDesc );

    // Hazard: a UAV barrier is an ordering point, not a layout transition. It
    // makes every raytracing write visible before the next pass samples the
    // reflection texture through its SRV descriptor.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_reflectionTexture;
    m_commandList4->ResourceBarrier( 1, &uavBarrier );

    // After ordering the writes, transition the texture into SRV state so the
    // raster water shader can read it.
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource = m_reflectionTexture;
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList4->ResourceBarrier( 1, &transition );
    m_reflectionInSrvState = true;

    // DXR uses the compute root signature/pipeline path. Mark raster state
    // dirty so the next draw restores graphics bindings instead of inheriting
    // raytracing state.
    outcome.rasterStateInvalidated = true;
    return outcome;
}


void RenderBackendDX12::DispatchReflectionRays( const float* invViewProj,
                                                const float* cameraPos,
                                                float waterY,
                                                float time,
                                                const float* lightPos,
                                                const float* skyColorTop,
                                                const float* skyColorBottom,
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
    (void)width;
    (void)height;
    if ( !m_raytracingOwner.Supported() || !EnsureCommandListOpen().ok )
    {
        return;
    }

    const uint32_t textureHandles[8] = { sphereTexHandle,
                                         terrainTexHandle,
                                         skyUpHandle,
                                         skyDownHandle,
                                         skyRightHandle,
                                         skyLeftHandle,
                                         skyFrontHandle,
                                         skyBackHandle };
    const Dx12RaytracingDispatchOutcome dispatch = m_raytracingOwner.DispatchReflections( Device(),
                                                                                          m_srvHeap,
                                                                                          m_frameOwner.Descriptors(),
                                                                                          m_textureOwner,
                                                                                          invViewProj,
                                                                                          cameraPos,
                                                                                          waterY,
                                                                                          time,
                                                                                          lightPos,
                                                                                          skyColorTop,
                                                                                          skyColorBottom,
                                                                                          textureHandles );
    if ( !dispatch.result.ok )
    {
        [[maybe_unused]] const SbResult retainedFailure = m_frameOwner.RetainFailure( dispatch.result );
        return;
    }
    if ( dispatch.rasterStateInvalidated )
    {
        // The owner reports state invalidation as a value. It cannot mutate the
        // sibling raster owners or retain a path back into this coordinator.
        m_pipelineOwner.InvalidateCommandState();
        m_textureOwner.InvalidateBindings();
    }
}


UINT Dx12RaytracingOwner::ReflectionSrvIndex() const
{
    return m_reflectionSrvIndex;
}


uint32_t RenderBackendDX12::GetReflectionUAVTexture() const
{
    return m_reflectionTextureHandle;
}


uint64_t RenderBackendDX12::GetInstancedMeshStaticVBVA( uint32_t handle ) const
{
    return m_geometryOwner.StaticVertexBufferAddress( handle );
}


int RenderBackendDX12::GetInstancedMeshStaticStride( uint32_t handle ) const
{
    return m_geometryOwner.StaticVertexStride( handle );
}


void Dx12RaytracingOwner::Shutdown()
{
    m_sbt.Reset();
    m_tlas.Reset();
    m_terrainBlas.Reset();
    m_sphereBlas.Reset();

    if ( m_constantBuffer )
    {
        m_constantBuffer->Unmap( 0, nullptr );
        m_constantBuffer->Release();
        m_constantBuffer = nullptr;
        m_constantBufferMapped = nullptr;
    }
    if ( m_reflectionTexture )
    {
        m_reflectionTexture->Release();
        m_reflectionTexture = nullptr;
    }
    if ( m_pipelineProperties )
    {
        m_pipelineProperties->Release();
        m_pipelineProperties = nullptr;
    }
    if ( m_pipeline )
    {
        m_pipeline->Release();
        m_pipeline = nullptr;
    }
    if ( m_rootSignature )
    {
        m_rootSignature->Release();
        m_rootSignature = nullptr;
    }
    if ( m_commandList4 )
    {
        m_commandList4->Release();
        m_commandList4 = nullptr;
    }
    if ( m_device5 )
    {
        m_device5->Release();
        m_device5 = nullptr;
    }
    m_supported = false;
    m_reflectionUavIndex = 0;
    m_reflectionSrvIndex = 0;
    m_reflectionWidth = 0;
    m_reflectionHeight = 0;
    m_reflectionInSrvState = false;
    m_maxInstances = 0;
}


void RenderBackendDX12::ShutdownDXR()
{
    if ( m_reflectionTextureHandle != 0 )
    {
        // Lifetime: the texture registry borrows this descriptor identity. Drop
        // its public handle before the owner releases the underlying reflection
        // resource so no sibling registry entry survives as a stale tombstone.
        m_textureOwner.UnregisterSRV( m_reflectionTextureHandle );
        m_reflectionTextureHandle = 0;
    }
    m_raytracingOwner.Shutdown();
}

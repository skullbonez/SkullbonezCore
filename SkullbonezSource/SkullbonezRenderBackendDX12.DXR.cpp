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

// --- RenderBackendDX12 DXR methods ---


void RenderBackendDX12::CheckDXRSupport()
{
    m_dxrSupported = false;
    m_device5 = nullptr;
    m_cmdList4 = nullptr;

    // Check raytracing tier
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if ( FAILED( m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof( opts5 ) ) ) )
    {
        return;
    }
    if ( opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0 )
    {
        return;
    }

    // QueryInterface for DXR interfaces
    if ( FAILED( m_device->QueryInterface( IID_PPV_ARGS( &m_device5 ) ) ) )
    {
        return;
    }

    m_dxrSupported = true;
}


void RenderBackendDX12::CreateRTRootSignature()
{
    // RT root signature layout:
    // [0] SRV - TLAS (t0, space1) — inline raw descriptor
    // [1] UAV - output texture (u0) — descriptor table
    // [2] CBV - RT constants (b1) — inline raw descriptor
    // [3] SRV - texture table (t0..t7, space0) — 8-descriptor table:
    //           t0=sphere, t1=terrain, t2=skyUp, t3=skyDown, t4=skyRight, t5=skyLeft, t6=skyFront, t7=skyBack
    // [s0] Static linear-wrap sampler
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
    if ( FAILED( D3D12SerializeVersionedRootSignature( &rootSigDesc, signature.GetAddressOf(), error.GetAddressOf() ) ) )
    {
        throw std::runtime_error( "RT root signature serialization failed" );
    }

    // Create the DXR root signature from the serialized blob. Same concept as the raster root
    // signature, but this one defines bindings for raytracing shaders (TLAS, UAV output, CBV, textures).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
    if ( FAILED( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rtRootSignature ) ) ) )
    {
        throw std::runtime_error( "CreateRootSignature (RT) failed" );
    }
    NameDx12Object( m_rtRootSignature, L"Skullbonez DX12 Raytracing Root Signature" );
}


void RenderBackendDX12::CreateRTPipeline()
{
    // DXR reflection uses checked-in DXIL. Keep compilation in tools/build
    // workflows so runtime startup never shells out or depends on SDK paths.
    std::string dxilPath = std::string( DATA_ROOT ) + "shaders/reflect.rt.dxil";

    // Load compiled DXIL blob
    FILE* dxilFile = nullptr;
    fopen_s( &dxilFile, dxilPath.c_str(), "rb" );
    if ( !dxilFile )
    {
        throw std::runtime_error( "Missing SkullbonezData/shaders/reflect.rt.dxil; rebuild and commit the DXR shader bytecode before using DXR reflection." );
    }
    fseek( dxilFile, 0, SEEK_END );
    long dxilSize = ftell( dxilFile );
    fseek( dxilFile, 0, SEEK_SET );
    std::vector<uint8_t> dxilBlob( (size_t)dxilSize );
    fread( dxilBlob.data(), 1, (size_t)dxilSize, dxilFile );
    fclose( dxilFile );

    // Build RTPSO with subobjects
    // We need: DXIL library, hit groups, shader config, pipeline config, global root signature
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

    // Build state object description with subobjects
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
        throw std::runtime_error( "CreateStateObject (RTPSO) failed" );
    }
    // A raytracing state object is the DXR equivalent of a pipeline. It groups
    // the ray-generation, miss, and hit shaders with their shared root binding
    // contract. Naming it makes DRED/PIX output point at the reflection pipeline
    // rather than a generic state object.
    NameDx12Object( m_rtPSO, L"Skullbonez DX12 Reflection Raytracing PSO" );

    // Query the state object for shader identifier lookup (used when building the SBT).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12stateobjectproperties
    m_rtPSO->QueryInterface( IID_PPV_ARGS( &m_rtPSOProps ) );
}


void RenderBackendDX12::CreateReflectionUAV( int width, int height )
{
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionInSRVState = false;

    // Create the reflection UAV texture
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
    if ( FAILED( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS( &m_reflectionUAV ) ) ) )
    {
        throw std::runtime_error( "Failed to create DXR reflection UAV texture" );
    }
    NameDx12Object( m_reflectionUAV, L"Skullbonez DX12 Reflection UAV Texture" );

    // Create UAV descriptor
    m_reflectionUAVIndex = AllocateStaticSRV();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView( m_reflectionUAV, nullptr, &uavDesc, GetSRVStagingCpuHandle( m_reflectionUAVIndex ) );

    // Also copy to shader-visible heap
    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapCpu = m_srvDescriptors.ShaderVisibleCpuHandle( m_reflectionUAVIndex );
    m_device->CopyDescriptorsSimple( 1, srvHeapCpu, GetSRVStagingCpuHandle( m_reflectionUAVIndex ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // Create SRV for sampling in water shader
    m_reflectionSRVIndex = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView( m_reflectionUAV, &srvDesc, GetSRVStagingCpuHandle( m_reflectionSRVIndex ) );

    // Copy SRV to shader-visible heap
    srvHeapCpu = m_srvDescriptors.ShaderVisibleCpuHandle( m_reflectionSRVIndex );
    m_device->CopyDescriptorsSimple( 1, srvHeapCpu, GetSRVStagingCpuHandle( m_reflectionSRVIndex ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


void RenderBackendDX12::InitDXR( uint64_t terrainVBVA, int terrainVertCount, int terrainStride, uint64_t sphereVBVA, int sphereVertCount, int sphereStride, int maxInstances )
{
    if ( !m_dxrSupported )
    {
        return;
    }

    // Skip re-initialisation if DXR is already set up (scene reload path). The terrain and sphere
    // meshes (and their BLAS) do not change between scenes — only the TLAS is rebuilt per-frame.
    // The full init path is only needed once; on renderer switch the backend is destroyed/recreated,
    // so m_cmdList4 is null and we fall through to the full init below.
    if ( m_cmdList4 )
    {
        return;
    }

    // Get CmdList4 from command list
    if ( FAILED( m_commandList->QueryInterface( IID_PPV_ARGS( &m_cmdList4 ) ) ) )
    {
        m_dxrSupported = false;
        return;
    }

    // Create RT root signature and pipeline
    CreateRTRootSignature();
    CreateRTPipeline();

    // Create reflection UAV at 2x viewport
    CreateReflectionUAV( m_width * 2, m_height * 2 );

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

        if ( FAILED( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_rtConstantBuffer ) ) ) )
        {
            throw std::runtime_error( "Failed to create RT constant buffer" );
        }
        NameDx12Object( m_rtConstantBuffer, L"Skullbonez DX12 Raytracing Constants Upload Buffer" );
        m_rtConstantBuffer->Map( 0, nullptr, (void**)&m_rtConstantBufferMapped );
    }

    // Build BLAS for terrain and sphere
    EnsureCommandListOpen();

    m_terrainBLAS.Build( m_device5, m_cmdList4, (D3D12_GPU_VIRTUAL_ADDRESS)terrainVBVA, terrainVertCount, terrainStride, DXGI_FORMAT_R32G32B32_FLOAT, true );
    m_sphereBLAS.Build( m_device5, m_cmdList4, (D3D12_GPU_VIRTUAL_ADDRESS)sphereVBVA, sphereVertCount, sphereStride, DXGI_FORMAT_R32G32B32_FLOAT, false );

    // Submit and wait for BLAS builds to complete
    AssertPlatformProfilerGpuStackClosed( "InitDXR" );
    m_commandList->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Free scratch memory
    m_terrainBLAS.ReleaseAfterBuild();
    m_sphereBLAS.ReleaseAfterBuild();

    // Init TLAS (sized for maxInstances: terrain + all balls)
    m_tlas.Init( m_device5, maxInstances + 1 );

    // Build SBT
    m_sbt.Build( m_device, m_rtPSOProps, L"RayGen", L"Miss", L"TerrainHitGroup", L"SphereHitGroup" );
}


void RenderBackendDX12::BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t /*terrainBLAS*/, uint64_t /*sphereBLAS*/ )
{
    if ( !m_dxrSupported || !m_cmdList4 )
    {
        return;
    }
    if ( instanceCount < 0 || instanceCount > MAX_GAME_MODELS )
    {
        throw std::runtime_error( "DX12 TLAS instance count exceeds MAX_GAME_MODELS" );
    }

    // Build instance descriptors
    // Instance 0: terrain (identity)
    // Instance 1..N: spheres with their world transforms

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

        // Copy 3x4 transform from the flat float array (row-major 4x4 → DXR 3x4 row-major)
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


void RenderBackendDX12::DispatchReflectionRays( const float* invViewProj, const float* cameraPos, float waterY, float time, const float* lightPos, int width, int height, uint32_t sphereTexHandle, uint32_t terrainTexHandle, uint32_t skyUpHandle, uint32_t skyDownHandle, uint32_t skyRightHandle, uint32_t skyLeftHandle, uint32_t skyFrontHandle, uint32_t skyBackHandle )
{
    if ( !m_dxrSupported || !m_cmdList4 || !m_rtPSO )
    {
        return;
    }

    (void)width;
    (void)height;

    EnsureCommandListOpen();

    // Transition reflection UAV back to writable state if it was left as SRV from previous frame
    if ( m_reflectionInSRVState )
    {
        TransitionBarrier( m_reflectionUAV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    }

    // Update RT constant buffer
    // Layout: float4x4 invVP, float3 cameraPos, float waterY, float3 lightPos, float time, float3 skyTop, pad, float3 skyBottom, pad
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

    // Set the compute root signature for raytracing. DXR uses the compute pipeline (not graphics)
    // because ray tracing doesn't use the traditional rasterization pipeline (no vertex/pixel stages).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setcomputerootsignature
    m_cmdList4->SetComputeRootSignature( m_rtRootSignature );

    // Bind the DXR raytracing pipeline state object. SetPipelineState1 is the DXR-specific version
    // that accepts an ID3D12StateObject (RTPSO) instead of a regular ID3D12PipelineState (graphics PSO).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-setpipelinestate1
    m_cmdList4->SetPipelineState1( m_rtPSO );

    // Bind the shader-visible descriptor heap for DXR (same heap as raster, re-bound after compute).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_cmdList4->SetDescriptorHeaps( 1, heaps );

    // Root params: [0] TLAS SRV, [1] UAV table, [2] CBV, [3] texture SRV table
    m_cmdList4->SetComputeRootShaderResourceView( 0, m_tlas.GetResultVA() );
    m_cmdList4->SetComputeRootDescriptorTable( 1, GetSRVGpuHandle( m_reflectionUAVIndex ) );
    m_cmdList4->SetComputeRootConstantBufferView( 2, m_rtConstantBuffer->GetGPUVirtualAddress() );

    // Bind sphere + terrain + 6 sky face textures at root param [3] (t0..t7)
    const uint32_t texHandles[8] = { sphereTexHandle, terrainTexHandle, skyUpHandle, skyDownHandle, skyRightHandle, skyLeftHandle, skyFrontHandle, skyBackHandle };
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
            m_device->CopyDescriptorsSimple( 1, dst, GetSRVStagingCpuHandle( srcIdx ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
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

    // UAV barrier — ensures all ray tracing writes complete before the water shader reads the texture.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_reflectionUAV;
    m_cmdList4->ResourceBarrier( 1, &barrier );

    // Transition to SRV state for water shader sampling
    TransitionBarrier( m_reflectionUAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    m_reflectionInSRVState = true;

    // Force re-bind of raster state after compute dispatch
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


uint32_t RenderBackendDX12::GetReflectionUAVTexture() const
{
    // Return a texture handle that maps to the reflection SRV
    // The water shader will bind this at t1 instead of the FBO texture
    // We return a handle into the texture registry — but for DXR we just return the SRV index
    // encoded as a texture handle. The caller can pass this to BindTexture.
    // Actually, we need to register the SRV in the texture registry.
    // This is called once, so we can cast-away const for registration.
    if ( m_reflectionSRVIndex == 0 )
    {
        return 0;
    }
    // Find if already registered
    for ( size_t i = 0; i < m_textures.size(); ++i )
    {
        if ( m_textures[i].srvIndex == m_reflectionSRVIndex )
        {
            return (uint32_t)( i + 1 );
        }
    }
    // Register it (const_cast justified: lazy one-time registration)
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

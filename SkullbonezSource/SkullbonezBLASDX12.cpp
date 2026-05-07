// --- Includes ---
// --- DXR Ray Tracing: Bottom-Level Acceleration Structure (BLAS) ---
//
//  A BLAS holds the actual triangle geometry for a single mesh. Think of it as a spatial
//  index (like a BVH tree) that lets the GPU quickly test if a ray hits any triangle in
//  that mesh without checking every triangle one by one.
//
//  Scene (TLAS)
//  +-----------------------------------+
//  | Instance 0: Sphere @ pos (10,5,0) |---> BLAS (sphere mesh triangles)
//  | Instance 1: Sphere @ pos (3,2,7)  |---> BLAS (same sphere mesh, different transform)
//  | Instance 2: Terrain               |---> BLAS (terrain mesh triangles)
//  +-----------------------------------+
//
//  When a ray is cast, the GPU traverses the TLAS to find which instances the ray might hit,
//  then drills down into the BLAS to test against actual triangles. This two-level structure
//  allows the same geometry (BLAS) to appear multiple times at different positions (instances).
//
#include "SkullbonezBLASDX12.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


BLAS::BLAS()
    : m_scratch( nullptr ), m_result( nullptr )
{
}


BLAS::~BLAS()
{
    Reset();
}


void BLAS::Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, D3D12_GPU_VIRTUAL_ADDRESS vbVA, int vertexCount, int vertexStride, DXGI_FORMAT vertexPosFormat, bool preferFastTrace )
{
    // Describe geometry (vertex-only, no index buffer)
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geomDesc.Triangles.VertexBuffer.StartAddress = vbVA;
    geomDesc.Triangles.VertexBuffer.StrideInBytes = (UINT64)vertexStride;
    geomDesc.Triangles.VertexFormat = vertexPosFormat;
    geomDesc.Triangles.VertexCount = (UINT)vertexCount;
    geomDesc.Triangles.IndexBuffer = 0;
    geomDesc.Triangles.IndexCount = 0;
    geomDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

    // Get prebuild info to determine scratch/result sizes
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = preferFastTrace ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geomDesc;

    // Query the driver for how much scratch and result memory the BLAS build will need.
    // The GPU needs temporary "scratch" memory during construction (like a workspace), plus
    // the final "result" buffer where the completed acceleration structure lives.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-getraytracingaccelerationstructureprebuildinfo
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &prebuild );

    if ( prebuild.ResultDataMaxSizeInBytes == 0 )
    {
        throw std::runtime_error( "BLAS: GetRaytracingAccelerationStructurePrebuildInfo returned zero" );
    }

    // Allocate scratch buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = prebuild.ScratchDataSizeInBytes;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Allocate scratch buffer — temporary GPU workspace used during acceleration structure build.
    // This memory is only needed during the build and can be freed afterwards. Must allow
    // unordered access (UAV) because the GPU reads and writes to it during construction.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &m_scratch ) ) ) )
    {
        throw std::runtime_error( "BLAS: Failed to create scratch buffer" );
    }

    // Allocate result buffer
    bufDesc.Width = prebuild.ResultDataMaxSizeInBytes;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Allocate result buffer — this holds the final built BLAS that persists for the lifetime of
    // raytracing. Initial state is RAYTRACING_ACCELERATION_STRUCTURE because it's used directly
    // by the DXR TraceRay hardware.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS( &m_result ) ) ) )
    {
        throw std::runtime_error( "BLAS: Failed to create result buffer" );
    }

    // Build the acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_result->GetGPUVirtualAddress();

    // Record the GPU command to build the Bottom-Level Acceleration Structure. The GPU takes the
    // raw triangle data from the vertex buffer and builds an optimized spatial data structure (BVH)
    // that enables fast ray-triangle intersection testing. This is an async GPU operation.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-buildraytracingaccelerationstructure
    cmdList->BuildRaytracingAccelerationStructure( &buildDesc, 0, nullptr );

    // UAV barrier — ensures the BLAS build completes before any subsequent ray tracing uses it.
    // Without this, the GPU might try to trace rays against a half-built acceleration structure.
    // A UAV barrier is a lightweight sync point (no state transition, just ordering guarantee).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_result;
    cmdList->ResourceBarrier( 1, &barrier );
}


D3D12_GPU_VIRTUAL_ADDRESS BLAS::GetResultVA() const
{
    return m_result ? m_result->GetGPUVirtualAddress() : 0;
}


void BLAS::ReleaseAfterBuild()
{
    if ( m_scratch )
    {
        m_scratch->Release();
        m_scratch = nullptr;
    }
}


void BLAS::Reset()
{
    if ( m_scratch )
    {
        m_scratch->Release();
        m_scratch = nullptr;
    }
    if ( m_result )
    {
        m_result->Release();
        m_result = nullptr;
    }
}

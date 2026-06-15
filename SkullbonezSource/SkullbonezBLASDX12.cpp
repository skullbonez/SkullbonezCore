/*
File: SkullbonezSource/SkullbonezBLASDX12.cpp
Purpose:
  Builds and owns DX12 raytracing bottom-level acceleration structures for mesh geometry.

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
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/SkullbonezBLASDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
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
    // Geometry description tells DXR where the triangle vertices live. This
    // engine path uses non-indexed triangles, so each consecutive group of
    // three position vertices is one triangle.
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

    // Prebuild inputs are the stable recipe for this acceleration structure:
    // bottom-level, one geometry descriptor, and a build flag tuned either for
    // faster ray traversal or faster rebuild time.
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

    // Scratch and result live in the default heap because the GPU builds and
    // traverses these structures directly.
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

    // Allocate scratch buffer: temporary GPU workspace used during acceleration
    // structure build. This memory is only needed during the build and can be
    // freed afterwards. It must allow unordered access because the GPU reads
    // and writes to it during construction.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &m_scratch ) ) ) )
    {
        throw std::runtime_error( "BLAS: Failed to create scratch buffer" );
    }

    // The result buffer is the BLAS itself. Unlike scratch memory, it must stay
    // alive for as long as rays can hit this mesh.
    bufDesc.Width = prebuild.ResultDataMaxSizeInBytes;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Allocate result buffer: this holds the final built BLAS that persists for
    // the lifetime of raytracing. Initial state is
    // RAYTRACING_ACCELERATION_STRUCTURE because DXR TraceRay hardware reads it
    // directly.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS( &m_result ) ) ) )
    {
        throw std::runtime_error( "BLAS: Failed to create result buffer" );
    }

    // Build command: connect the immutable build inputs with the temporary
    // scratch buffer and the persistent result buffer.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_result->GetGPUVirtualAddress();

    // Record the GPU command to build the Bottom-Level Acceleration Structure. The GPU takes the
    // raw triangle data from the vertex buffer and builds an optimized spatial data structure (BVH)
    // that enables fast ray-triangle intersection testing. This is an async GPU operation.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-buildraytracingaccelerationstructure
    cmdList->BuildRaytracingAccelerationStructure( &buildDesc, 0, nullptr );

    // Hazard: BuildRaytracingAccelerationStructure writes through UAV-style
    // memory. This barrier orders the build before any later raytracing pass
    // reads the BLAS. It is not a resource-state transition; it is a visibility
    // guarantee.
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

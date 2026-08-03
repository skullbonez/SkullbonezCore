/*
File: SkullbonezSource/Rendering/DX12/BLASDX12.cpp
Purpose:
  Builds and owns DX12 raytracing bottom-level acceleration structures for mesh geometry.

Summary:
  BLASDX12 owns one bottom-level acceleration structure built from vertex-only
  mesh geometry plus its scratch and result resources for the active device
  epoch.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/BLASDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
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
#include "BLASDX12.h"
#include "../../Core/SbDiagnosticStore.h"
#include "RenderDeviceDX12.h"


using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Core::SbResult;


BLAS::BLAS( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
    : m_resultDiagnostics( resultDiagnostics ), m_scratch( nullptr ), m_result( nullptr )
{
}


BLAS::~BLAS()
{
    Reset();
}


SkullbonezCore::Core::SbResult BLAS::Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList,
                                            D3D12_GPU_VIRTUAL_ADDRESS vbVA, int vertexCount, int vertexStride,
                                            DXGI_FORMAT vertexPosFormat, bool preferFastTrace )
{

    // Geometry description tells DXR where the triangle vertices live. This
    // engine path uses non-indexed triangles, so each consecutive group of
    // three position vertices is one triangle.
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geomDesc.Triangles.VertexBuffer.StartAddress = vbVA;
    geomDesc.Triangles.VertexBuffer.StrideInBytes = static_cast<UINT64>( vertexStride );
    geomDesc.Triangles.VertexFormat = vertexPosFormat;
    geomDesc.Triangles.VertexCount = static_cast<UINT>( vertexCount );
    geomDesc.Triangles.IndexBuffer = 0;
    geomDesc.Triangles.IndexCount = 0;
    geomDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

    // Prebuild inputs are the stable recipe for this acceleration structure:
    // bottom-level, one geometry descriptor, and a build flag tuned either for
    // faster ray traversal or faster rebuild time.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = preferFastTrace ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                                   : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geomDesc;

    // Query the driver for how much scratch and result memory the BLAS build will need.
    // The GPU needs temporary "scratch" memory during construction (like a workspace), plus
    // the final "result" buffer where the completed acceleration structure lives.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-getraytracingaccelerationstructureprebuildinfo
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &prebuild );

    if ( prebuild.ResultDataMaxSizeInBytes == 0 )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12",
                                            "BLAS: GetRaytracingAccelerationStructurePrebuildInfo returned zero" );
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

    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr, IID_PPV_ARGS( &m_scratch ) ) ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "BLAS: Failed to create scratch buffer" );
    }

    NameDx12Object( m_scratch, preferFastTrace ? L"Skullbonez DX12 Terrain BLAS Scratch Buffer"
                                               : L"Skullbonez DX12 Mesh BLAS Scratch Buffer" );

    // The result buffer is the BLAS itself. Unlike scratch memory, it must stay
    // alive for as long as rays can hit this mesh.
    bufDesc.Width = prebuild.ResultDataMaxSizeInBytes;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Allocate result buffer: this holds the final built BLAS that persists for
    // the lifetime of raytracing. Initial state is
    // RAYTRACING_ACCELERATION_STRUCTURE because DXR TraceRay hardware reads it
    // directly.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource

    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
                                                  IID_PPV_ARGS( &m_result ) ) ) )
    {
        ReleaseAfterBuild();
        return m_resultDiagnostics.Failure( "Rendering/DX12", "BLAS: Failed to create result buffer" );
    }

    NameDx12Object( m_result, preferFastTrace ? L"Skullbonez DX12 Terrain BLAS Result Buffer"
                                              : L"Skullbonez DX12 Mesh BLAS Result Buffer" );

    // Build command: connect the immutable build inputs with the temporary
    // scratch buffer and the persistent result buffer.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_result->GetGPUVirtualAddress();

    // Record the GPU command to build the Bottom-Level Acceleration Structure. The GPU takes the
    // raw triangle data from the vertex buffer and builds an optimized spatial data structure (BVH)
    // that enables fast ray-triangle intersection testing. This is an async GPU operation.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-buildraytracingaccelerationstructure
    cmdList->BuildRaytracingAccelerationStructure( &buildDesc, 0, nullptr );

    // Hazard: BuildRaytracingAccelerationStructure writes through UAV-style
    // memory. This barrier orders the build before any later raytracing pass
    // reads the BLAS. It is not a resource-state transition; it is a visibility
    // guarantee.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_result;
    cmdList->ResourceBarrier( 1, &barrier );
    return SkullbonezCore::Core::SbResult::Success();
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

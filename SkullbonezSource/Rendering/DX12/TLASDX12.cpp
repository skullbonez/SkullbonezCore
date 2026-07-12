/*
File: SkullbonezSource/Rendering/DX12/TLASDX12.cpp
Purpose:
  Builds and owns the DX12 raytracing top-level scene acceleration structure.

Summary:
  TLASDX12.cpp builds and owns the DX12 raytracing top-level scene
  acceleration structure. As an implementation unit, keep edits anchored on
  DX12 ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - `m_maxInstances` is the allocation ceiling for the per-frame TLAS instance
    descriptor upload. Frame rebuilds may use a smaller prefix but must not
    exceed the buffers allocated by `Init()`.
  - Instance bytes and build commands are emitted only after Map succeeds and
    returns a non-null pointer.

Related:
  - SkullbonezSource/Rendering/DX12/TLASDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
// --- DXR Ray Tracing: Top-Level Acceleration Structure (TLAS) ---
//
//  The TLAS represents the entire scene for ray tracing. It contains "instances" — each instance
//  points to a BLAS (mesh geometry) and has a transform matrix that positions it in the world.
//  The TLAS is rebuilt every frame because objects move (balls bounce around).
//
//  TLAS (rebuilt per frame)
//  +-----------------------------------------------------+
//  | Instance 0: Terrain (identity transform) --> BLAS    |
//  | Instance 1: Ball #1 @ position (x,y,z)  --> BLAS    |
//  | Instance 2: Ball #2 @ position (x,y,z)  --> BLAS    |
//  | ...                                                  |
//  +-----------------------------------------------------+
//
//  PREFER_FAST_BUILD flag is used because we rebuild every frame (speed > quality tradeoff).
//
#include "TLASDX12.h"
#include "../../Core/FatalError.h"
#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderDeviceDX12.h"
#include <cstring>


using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Basics::SbResult;


TLAS::TLAS() : m_scratch( nullptr ), m_result( nullptr ), m_instanceDescs( nullptr ), m_maxInstances( 0 )
{
}


TLAS::~TLAS()
{
    Reset();
}


SbResult TLAS::Init( ID3D12Device5* device, int maxInstances )
{
    m_maxInstances = maxInstances;

    // Allocate instance desc upload buffer (persistent, rewritten each frame)
    UINT64 instanceSize = (UINT64)maxInstances * sizeof( D3D12_RAYTRACING_INSTANCE_DESC );

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = instanceSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Allocate an upload heap buffer for instance descriptors. Each descriptor tells the GPU where
    // a BLAS is and how to transform it in the scene. This buffer lives in CPU-writable memory
    // (upload heap) because we rewrite instance positions every frame as balls move.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &uploadHeap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufDesc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_instanceDescs ) ) ) )
    {
        return SbResult::Failure( "Rendering/DX12", "TLAS: Failed to create instance desc buffer" );
    }
    NameDx12Object( m_instanceDescs, L"Skullbonez DX12 TLAS Instance Descriptors" );

    // Prebuild info to determine scratch/result sizes (for max instance count)
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = (UINT)maxInstances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    // Query the driver for TLAS scratch/result memory requirements (same concept as BLAS prebuild).
    // We query for the maximum instance count so the allocated buffers can handle any frame.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-getraytracingaccelerationstructureprebuildinfo
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &prebuild );
    // Hazard: this API has no HRESULT; zero capacity is its unusable-output
    // signal. Reject it before creating nominal zero-byte build resources.
    if ( prebuild.ScratchDataSizeInBytes == 0 || prebuild.ResultDataMaxSizeInBytes == 0 )
    {
        Reset();
        return SbResult::Failure( "Rendering/DX12", "TLAS: prebuild info returned zero scratch or result capacity" );
    }

    // Allocate scratch buffer
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    bufDesc.Width = prebuild.ScratchDataSizeInBytes;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // Allocate scratch buffer for TLAS build (temporary GPU workspace, same as BLAS).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &defaultHeap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufDesc,
                                                  D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_scratch ) ) ) )
    {
        Reset();
        return SbResult::Failure( "Rendering/DX12", "TLAS: Failed to create scratch buffer" );
    }
    NameDx12Object( m_scratch, L"Skullbonez DX12 TLAS Scratch Buffer" );

    // Allocate result buffer
    bufDesc.Width = prebuild.ResultDataMaxSizeInBytes;

    // Allocate result buffer that holds the final TLAS (persists across frames, rebuilt in-place).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &defaultHeap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufDesc,
                                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_result ) ) ) )
    {
        Reset();
        return SbResult::Failure( "Rendering/DX12", "TLAS: Failed to create result buffer" );
    }
    NameDx12Object( m_result, L"Skullbonez DX12 TLAS Result Buffer" );
    return SbResult::Success();
}


SbResult TLAS::Build( ID3D12Device5* device,
                      ID3D12GraphicsCommandList4* cmdList,
                      const D3D12_RAYTRACING_INSTANCE_DESC* instances,
                      int instanceCount )
{
    (void)device;

    // Invariant: Init() sizes all TLAS buffers from m_maxInstances. A larger
    // rebuild would overwrite the instance descriptor upload and point the
    // GPU build at memory the TLAS does not own.
    if ( instanceCount > m_maxInstances )
    {
        SB_FATAL( "TLAS", "Instance count exceeds max. requested=%d max=%d", instanceCount, m_maxInstances );
    }

    // Map the instance descriptor buffer to CPU memory and write the new instance transforms.
    // Map/Unmap is the DX12 way of writing CPU data to a GPU-accessible buffer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map
    void* rawMapped = nullptr;
    const HRESULT mapResult = m_instanceDescs->Map( 0, nullptr, &rawMapped );
    const Dx12MappedPointerResult mappedResult =
        ValidateDx12MappedPointer( mapResult, rawMapped, "TLAS instance descriptor Map" );
    if ( !mappedResult.result.ok )
    {
        return mappedResult.result;
    }
    memcpy( mappedResult.pointer, instances, (size_t)instanceCount * sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    m_instanceDescs->Unmap( 0, nullptr );

    // Build inputs tell DXR where the per-instance table lives and how many
    // rows are valid this frame. The result buffer was allocated for the
    // maximum scene size, but each frame may rebuild only a smaller prefix.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = (UINT)instanceCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = m_instanceDescs->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_result->GetGPUVirtualAddress();

    // Record the GPU command to build (or rebuild) the Top-Level Acceleration Structure.
    // This organizes all instance transforms + BLAS pointers into a spatial hierarchy so the
    // GPU can quickly find which instances a ray intersects before drilling into BLAS triangles.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-buildraytracingaccelerationstructure
    cmdList->BuildRaytracingAccelerationStructure( &buildDesc, 0, nullptr );

    // Hazard: BuildRaytracingAccelerationStructure writes through UAV-style
    // memory. This barrier orders those writes before DispatchRays reads the
    // TLAS, preventing rays from traversing a partially rebuilt hierarchy.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_result;
    cmdList->ResourceBarrier( 1, &barrier );
    return SbResult::Success();
}


D3D12_GPU_VIRTUAL_ADDRESS TLAS::GetResultVA() const
{
    return m_result ? m_result->GetGPUVirtualAddress() : 0;
}


void TLAS::Reset()
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
    if ( m_instanceDescs )
    {
        m_instanceDescs->Release();
        m_instanceDescs = nullptr;
    }
    m_maxInstances = 0;
}

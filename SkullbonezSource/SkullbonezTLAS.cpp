// --- Includes ---
#include "SkullbonezTLAS.h"
#include <stdexcept>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


TLAS::TLAS()
    : m_scratch( nullptr ), m_result( nullptr ), m_instanceDescs( nullptr ), m_maxInstances( 0 )
{
}


TLAS::~TLAS()
{
    Reset();
}


void TLAS::Init( ID3D12Device5* device, int maxInstances )
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

    if ( FAILED( device->CreateCommittedResource( &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_instanceDescs ) ) ) )
    {
        throw std::runtime_error( "TLAS: Failed to create instance desc buffer" );
    }

    // Prebuild info to determine scratch/result sizes (for max instance count)
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = (UINT)maxInstances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo( &inputs, &prebuild );

    // Allocate scratch buffer
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    bufDesc.Width = prebuild.ScratchDataSizeInBytes;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if ( FAILED( device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &m_scratch ) ) ) )
    {
        throw std::runtime_error( "TLAS: Failed to create scratch buffer" );
    }

    // Allocate result buffer
    bufDesc.Width = prebuild.ResultDataMaxSizeInBytes;

    if ( FAILED( device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS( &m_result ) ) ) )
    {
        throw std::runtime_error( "TLAS: Failed to create result buffer" );
    }
}


void TLAS::Build( ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, const D3D12_RAYTRACING_INSTANCE_DESC* instances, int instanceCount )
{
    (void)device;

    if ( instanceCount > m_maxInstances )
    {
        throw std::runtime_error( "TLAS: Instance count exceeds max" );
    }

    // Upload instance descriptors
    void* mapped = nullptr;
    m_instanceDescs->Map( 0, nullptr, &mapped );
    memcpy( mapped, instances, (size_t)instanceCount * sizeof( D3D12_RAYTRACING_INSTANCE_DESC ) );
    m_instanceDescs->Unmap( 0, nullptr );

    // Build TLAS
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

    cmdList->BuildRaytracingAccelerationStructure( &buildDesc, 0, nullptr );

    // UAV barrier to ensure TLAS is ready before tracing
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_result;
    cmdList->ResourceBarrier( 1, &barrier );
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

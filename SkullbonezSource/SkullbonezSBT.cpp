// --- Includes ---
#include "SkullbonezSBT.h"
#include <stdexcept>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


// Alignment helpers
static inline UINT64 Align( UINT64 value, UINT64 alignment )
{
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}


SBT::SBT()
    : m_buffer( nullptr ), m_rayGenOffset( 0 ), m_rayGenSize( 0 ), m_missOffset( 0 ), m_missSize( 0 ), m_hitGroupOffset( 0 ), m_hitGroupStride( 0 ), m_hitGroupSize( 0 )
{
}


SBT::~SBT()
{
    Reset();
}


void SBT::Build( ID3D12Device* device, ID3D12StateObjectProperties* props, const wchar_t* rayGenName, const wchar_t* missName, const wchar_t* hitGroupTerrainName, const wchar_t* hitGroupSphereName )
{
    // Shader identifier size is always 32 bytes (D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES)
    const UINT64 shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    const UINT64 recordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT; // 32
    const UINT64 tableAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;   // 64

    // Record sizes (no local root arguments for our simple case)
    UINT64 rayGenRecordSize = Align( shaderIdSize, recordAlignment );
    UINT64 missRecordSize = Align( shaderIdSize, recordAlignment );
    UINT64 hitGroupRecordSize = Align( shaderIdSize, recordAlignment );

    // Table layout: [raygen section][miss section][hit group section]
    // Each section start must be aligned to tableAlignment (64 bytes)
    m_rayGenOffset = 0;
    m_rayGenSize = Align( rayGenRecordSize, tableAlignment );

    m_missOffset = Align( m_rayGenOffset + m_rayGenSize, tableAlignment );
    m_missSize = Align( missRecordSize, tableAlignment );

    m_hitGroupOffset = Align( m_missOffset + m_missSize, tableAlignment );
    m_hitGroupStride = hitGroupRecordSize;
    m_hitGroupSize = hitGroupRecordSize * 2; // Terrain + sphere hit groups

    UINT64 totalSize = m_hitGroupOffset + m_hitGroupSize;

    // Allocate upload heap buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if ( FAILED( device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_buffer ) ) ) )
    {
        throw std::runtime_error( "SBT: Failed to create buffer" );
    }

    // Map and write shader identifiers
    uint8_t* mapped = nullptr;
    m_buffer->Map( 0, nullptr, (void**)&mapped );
    memset( mapped, 0, (size_t)totalSize );

    // Raygen record
    void* rayGenId = props->GetShaderIdentifier( rayGenName );
    memcpy( mapped + m_rayGenOffset, rayGenId, (size_t)shaderIdSize );

    // Miss record
    void* missId = props->GetShaderIdentifier( missName );
    memcpy( mapped + m_missOffset, missId, (size_t)shaderIdSize );

    // Hit group records
    void* terrainHitId = props->GetShaderIdentifier( hitGroupTerrainName );
    memcpy( mapped + m_hitGroupOffset, terrainHitId, (size_t)shaderIdSize );

    void* sphereHitId = props->GetShaderIdentifier( hitGroupSphereName );
    memcpy( mapped + m_hitGroupOffset + m_hitGroupStride, sphereHitId, (size_t)shaderIdSize );

    m_buffer->Unmap( 0, nullptr );
}


D3D12_GPU_VIRTUAL_ADDRESS_RANGE SBT::RayGenRange() const
{
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE range = {};
    range.StartAddress = m_buffer->GetGPUVirtualAddress() + m_rayGenOffset;
    range.SizeInBytes = m_rayGenSize;
    return range;
}


D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE SBT::MissRange() const
{
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE range = {};
    range.StartAddress = m_buffer->GetGPUVirtualAddress() + m_missOffset;
    range.SizeInBytes = m_missSize;
    range.StrideInBytes = Align( D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT );
    return range;
}


D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE SBT::HitGroupRange() const
{
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE range = {};
    range.StartAddress = m_buffer->GetGPUVirtualAddress() + m_hitGroupOffset;
    range.SizeInBytes = m_hitGroupSize;
    range.StrideInBytes = m_hitGroupStride;
    return range;
}


void SBT::Reset()
{
    if ( m_buffer )
    {
        m_buffer->Release();
        m_buffer = nullptr;
    }
}

/*
File: SkullbonezSource/Rendering/DX12/SBTDX12.cpp
Purpose:
  Builds the DX12 raytracing shader binding table that maps ray records to shaders.

Summary:
  SBTDX12.cpp builds the DX12 raytracing shader binding table that maps ray
  records to shaders. As an implementation unit, keep edits anchored on DX12
  ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  SBT (Shader Binding Table): DXR table that maps ray records to
  ray-generation, miss, and hit shaders.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Shader identifiers are copied only after Map succeeds and returns a
    non-null pointer; a failed mapping releases the unsubmitted table buffer.

Related:
  - SkullbonezSource/Rendering/DX12/SBTDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
// --- DXR Ray Tracing: Shader Binding Table (SBT) ---
//
//  The SBT maps ray interactions to shader code. When a ray hits geometry, the GPU uses the SBT
//  to determine which shader to execute. It's laid out as a flat buffer with three sections:
//
//  +------------------+------------------+-----------------------------------+
//  | Ray Generation   | Miss Shader      | Hit Groups (per geometry type)    |
//  | (starts tracing) | (ray hit nothing)| [0] Terrain  [1] Sphere          |
//  +------------------+------------------+-----------------------------------+
//
//  Each "record" contains a 32-byte shader identifier (opaque handle from the RT pipeline) plus
//  optional local root arguments. The GPU indexes into the hit group section based on the
//  InstanceContributionToHitGroupIndex set in each TLAS instance descriptor.
//
#include "SBTDX12.h"
#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderDeviceDX12.h"
#include <cstring>


using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Core::SbResult;


// Alignment helpers
static inline UINT64 Align( UINT64 value, UINT64 alignment )
{
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}


SBT::SBT()
    : m_buffer( nullptr ), m_rayGenOffset( 0 ), m_rayGenSize( 0 ), m_missOffset( 0 ), m_missSize( 0 ),
      m_hitGroupOffset( 0 ), m_hitGroupStride( 0 ), m_hitGroupSize( 0 )
{
}


SBT::~SBT()
{
    Reset();
}


SkullbonezCore::Core::SbResult SBT::Build( ID3D12Device* device,
                                           ID3D12StateObjectProperties* props,
                                           const wchar_t* rayGenName,
                                           const wchar_t* missName,
                                           const wchar_t* hitGroupTerrainName,
                                           const wchar_t* hitGroupSphereName )
{
    if ( !props )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "SBT: missing RT pipeline shader identifier interface" );
    }

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

    void* rayGenId = props->GetShaderIdentifier( rayGenName );
    if ( !rayGenId )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "SBT: Missing ray-generation shader identifier" );
    }
    void* missId = props->GetShaderIdentifier( missName );
    if ( !missId )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "SBT: Missing miss shader identifier" );
    }
    void* terrainHitId = props->GetShaderIdentifier( hitGroupTerrainName );
    if ( !terrainHitId )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "SBT: Missing terrain hit-group shader identifier" );
    }
    void* sphereHitId = props->GetShaderIdentifier( hitGroupSphereName );
    if ( !sphereHitId )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "SBT: Missing sphere hit-group shader identifier" );
    }

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

    // Allocate the SBT buffer on the upload heap (CPU-writable) because it's small and only
    // written once at init. The GPU reads it every DispatchRays call to find shader entry points.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    if ( FAILED( device->CreateCommittedResource( &heapProps,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufDesc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_buffer ) ) ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "SBT: Failed to create buffer" );
    }
    NameDx12Object( m_buffer, L"Skullbonez DX12 Shader Binding Table" );

    // Map the SBT buffer and write the shader identifiers into their respective sections.
    // GetShaderIdentifier retrieves the opaque 32-byte handle that the GPU uses to locate each
    // shader in the RT pipeline state object. We write raygen, miss, and hit group IDs.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12stateobjectproperties-getshaderidentifier
    void* rawMapped = nullptr;
    const HRESULT mapResult = m_buffer->Map( 0, nullptr, &rawMapped );
    const Dx12MappedPointerResult mappedResult = ValidateDx12MappedPointer( mapResult, rawMapped, "SBT buffer Map" );
    if ( !mappedResult.result.ok )
    {
        Reset();
        return mappedResult.result;
    }
    uint8_t* mapped = static_cast<uint8_t*>( mappedResult.pointer );
    memset( mapped, 0, (size_t)totalSize );

    // Raygen record
    memcpy( mapped + m_rayGenOffset, rayGenId, (size_t)shaderIdSize );

    // Miss record
    memcpy( mapped + m_missOffset, missId, (size_t)shaderIdSize );

    // Hit group records
    memcpy( mapped + m_hitGroupOffset, terrainHitId, (size_t)shaderIdSize );

    memcpy( mapped + m_hitGroupOffset + m_hitGroupStride, sphereHitId, (size_t)shaderIdSize );

    m_buffer->Unmap( 0, nullptr );
    return SkullbonezCore::Core::SbResult::Success();
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

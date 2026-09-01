/*
File: SkullbonezSource/Rendering/DX12/MeshDX12.h
Purpose:
  Declares mesh buffers, upload flow, and draw binding for the DX12 renderer.

Summary:
  MeshDX12 retains a default-heap vertex buffer and its draw view plus two
  narrow collaborators: a command-readiness gate and Dx12Diagnostics for
  bounded draw evidence. It retains no aggregate backend or raw trace/counter.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
    must stay explicit.
  - Draw dependencies are stable concrete owners; mesh objects never retain the
    aggregate backend or callbacks into it.
  - One successful native draw records exactly one diagnostic draw row.
  - Declared draws pass their raster bucket through the draw gate; MeshDX12
    never copies it into ambient backend state.

Related:
  - SkullbonezSource/Rendering/DX12/MeshDX12.cpp
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../RenderCommandTypes.h"
#include <d3d12.h>
#include <cstdint>
#include <optional>
#include <span>


namespace SkullbonezCore
{
namespace Rendering
{

class Dx12RenderDevice;
class Dx12PipelineOwner;
class Dx12TextureOwner;
class Dx12DescriptorAllocator;
class Dx12CommandRecordingState;
class Dx12Diagnostics;
class Dx12DrawGate;


// Vertex format enum for PSO input layout selection
enum class VertexFormat12
{
    Pos3,
    Pos3_Tex2,
    Pos3_Norm3_Tex2,
    Pos2_Tex2,
    Pos2
};

// Invariant: the component span always contains exactly the declared vertex
// rows. Construction rejects null, empty, negative, and overflowing layouts
// before resource allocation or upload reservation begins.
class MeshVertexDataView
{
  public:
    static std::optional<MeshVertexDataView> TryCreate( const float* data, int vertexCount, int floatsPerVertex,
                                                        VertexFormat12 format ) noexcept;

    std::span<const float> Components() const noexcept
    {
        return m_components;
    }
    int VertexCount() const noexcept
    {
        return m_vertexCount;
    }
    int FloatsPerVertex() const noexcept
    {
        return m_floatsPerVertex;
    }
    VertexFormat12 Format() const noexcept
    {
        return m_format;
    }
    UINT64 ByteCount() const noexcept
    {
        return static_cast<UINT64>( m_components.size_bytes() );
    }

  private:
    MeshVertexDataView( std::span<const float> components, int vertexCount, int floatsPerVertex,
                        VertexFormat12 format ) noexcept
        : m_components( components ), m_vertexCount( vertexCount ), m_floatsPerVertex( floatsPerVertex ), m_format( format )
    {
    }

    std::span<const float> m_components;
    int m_vertexCount = 0;
    int m_floatsPerVertex = 0;
    VertexFormat12 m_format = VertexFormat12::Pos3;
};

// Lifetime: the slice is valid only while its frame upload arena remains open.
// The factory proves that address, CPU bytes, and backing resource describe the
// same in-bounds reservation before MeshDX12 records a copy command.
class Dx12MeshUploadSlice
{
  public:
    static std::optional<Dx12MeshUploadSlice> TryCreate( D3D12_GPU_VIRTUAL_ADDRESS address, uint8_t* bytes, UINT64 byteCount,
                                                         ID3D12Resource* backing ) noexcept;

    D3D12_GPU_VIRTUAL_ADDRESS Address() const noexcept
    {
        return m_address;
    }
    std::span<uint8_t> Bytes() const noexcept
    {
        return m_bytes;
    }
    ID3D12Resource& Backing() const noexcept
    {
        return *m_backing;
    }

  private:
    Dx12MeshUploadSlice( D3D12_GPU_VIRTUAL_ADDRESS address, std::span<uint8_t> bytes, ID3D12Resource& backing ) noexcept
        : m_address( address ), m_bytes( bytes ), m_backing( &backing )
    {
    }

    D3D12_GPU_VIRTUAL_ADDRESS m_address = 0;
    std::span<uint8_t> m_bytes;
    ID3D12Resource* m_backing = nullptr;
};


class MeshDX12
{

  private:
    Dx12RenderDevice& m_device;
    Dx12DrawGate& m_drawGate;
    Dx12Diagnostics& m_diagnostics;
    ID3D12Resource* m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView;
    int m_vertexCount;
    int m_stride;
    VertexFormat12 m_format;

  public:
    MeshDX12( Dx12RenderDevice& device, Dx12DrawGate& drawGate, Dx12Diagnostics& diagnostics );
    ~MeshDX12();

    bool Create( const MeshVertexDataView& vertices, const Dx12MeshUploadSlice& upload );

    bool PrecompileRasterState( const PassRasterStateBucket& bucket ) const;
    void Draw( const PassRasterStateBucket& bucket ) const;
    int GetVertexCount() const
    {
        return m_vertexCount;
    }
    int GetStride() const
    {
        return m_stride;
    }
    uint64_t GetVertexBufferGPUVA() const
    {
        return m_vertexBuffer ? m_vertexBuffer->GetGPUVirtualAddress() : 0;
    }
    VertexFormat12 GetFormat() const
    {
        return m_format;
    }
};
} // namespace Rendering
} // namespace SkullbonezCore

/*
File: SkullbonezSource/Rendering/DX12/MeshDX12.h
Purpose:
  Declares mesh buffers, upload flow, and draw binding for the DX12 renderer.

Summary:
  MeshDX12 retains its vertex buffer and two narrow concrete collaborators: a
  draw gate for command readiness and Dx12Diagnostics for bounded draw evidence.
  It has no aggregate backend, raw trace, or raw counter reference.

Glossary:
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

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
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../IRenderCommandContext.h"
#include <d3d12.h>
#include <cstdint>


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


/* -- MeshDX12
---------------------------------------------------------------------------------------------------------------------------------------------------

    DX12 static mesh implementation. Holds a committed vertex buffer resource on
    the default heap and exposes the vertex buffer view used by draw calls.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
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

    bool Create( ID3D12Device* device,
                 ID3D12GraphicsCommandList* cmdList,
                 const float* data,
                 int vertexCount,
                 int floatsPerVertex,
                 VertexFormat12 format,
                 D3D12_GPU_VIRTUAL_ADDRESS uploadAddr,
                 uint8_t* uploadPtr,
                 ID3D12Resource* uploadBuffer );

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
    void ResetResources();
};
} // namespace Rendering
} // namespace SkullbonezCore

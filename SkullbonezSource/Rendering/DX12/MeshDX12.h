/*
File: SkullbonezSource/Rendering/DX12/MeshDX12.h
Purpose:
  Declares mesh buffers, upload flow, and draw binding for the DX12 renderer.

Summary:
  MeshDX12.h declares mesh buffers, upload flow, and draw binding for the DX12
  renderer. As a public header, keep edits anchored on DX12 ownership,
  descriptors, resources, and command submission and on the
  glossary/invariants below.

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

Related:
  - SkullbonezSource/Rendering/DX12/MeshDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../IMesh.h"
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
class DrawCallTrace;
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
class MeshDX12 : public IMesh
{

  private:
    Dx12RenderDevice& m_device;
    Dx12DrawGate& m_drawGate;
    DrawCallTrace& m_drawTrace;
    int& m_drawCount;
    ID3D12Resource* m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView;
    int m_vertexCount;
    int m_stride;
    VertexFormat12 m_format;

  public:
    MeshDX12( Dx12RenderDevice& device, Dx12DrawGate& drawGate, DrawCallTrace& drawTrace, int& drawCount );
    ~MeshDX12() override;

    bool Create( ID3D12Device* device,
                 ID3D12GraphicsCommandList* cmdList,
                 const float* data,
                 int vertexCount,
                 int floatsPerVertex,
                 VertexFormat12 format,
                 D3D12_GPU_VIRTUAL_ADDRESS uploadAddr,
                 uint8_t* uploadPtr,
                 ID3D12Resource* uploadBuffer );

    void Draw() const override;
    void DrawInstanced( int instanceCount ) const override;
    int GetVertexCount() const override
    {
        return m_vertexCount;
    }
    int GetStride() const override
    {
        return m_stride;
    }
    uint64_t GetVertexBufferGPUVA() const override
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

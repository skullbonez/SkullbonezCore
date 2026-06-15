/*
File: SkullbonezSource/SkullbonezMeshDX11.h
Purpose:
  Declares mesh buffers and draw binding for the DX11 parity renderer.

Mental model:
  DX11 is a legacy parity renderer. It follows the renderer interface while
  staying close enough to DX12 and OpenGL output for visual comparison.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezMeshDX11.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezIMesh.h"
#include <d3d11.h>


namespace SkullbonezCore
{
namespace Rendering
{

enum class VertexFormatDX
{
    Pos3,
    Pos3_Tex2,
    Pos3_Norm3_Tex2,
    Pos2_Tex2,
    Pos2
};


/* -- MeshDX11 ----------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the IMesh interface.
    Holds a D3D11 vertex buffer and vertex format metadata. Input layouts are created lazily.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class MeshDX11 : public IMesh
{

  private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11Buffer* m_vb;
    mutable ID3D11InputLayout* m_inputLayout;
    int m_vertexCount;
    int m_stride;
    VertexFormatDX m_format;
    mutable const void* m_lastVSBytecode;

    void EnsureInputLayout() const;

  public:
    MeshDX11( ID3D11Device* device, ID3D11DeviceContext* context );
    ~MeshDX11() override;

    bool Create( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords );

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
        return 0;
    }

    VertexFormatDX GetFormat() const
    {
        return m_format;
    }
};
} // namespace Rendering
} // namespace SkullbonezCore
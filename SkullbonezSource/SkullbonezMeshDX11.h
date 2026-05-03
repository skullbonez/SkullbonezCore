#pragma once


// --- Includes ---
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

    VertexFormatDX GetFormat() const
    {
        return m_format;
    }
    int GetStride() const
    {
        return m_stride;
    }
};
} // namespace Rendering
} // namespace SkullbonezCore
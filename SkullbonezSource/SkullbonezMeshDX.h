#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;


namespace SkullbonezCore
{
namespace Rendering
{
class Mesh
{
  private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    int m_vertexCount;
    int m_stride;

  public:
    Mesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, int primitiveMode = 0 );
    ~Mesh() = default;

    void Draw() const;
    int GetVertexCount() const;
};
} // namespace Rendering
} // namespace SkullbonezCore

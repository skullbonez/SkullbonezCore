#pragma once


// --- Includes ---
#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IMesh ------------------------------------------------------------------------------------------------------------------------------------------------------

    Abstract mesh interface. Concrete implementations handle VAO/VBO (OpenGL) or ID3D11Buffer (DirectX).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IMesh
{

  public:
    virtual ~IMesh() = default;

    virtual void Draw() const = 0;
    virtual void DrawInstanced( int instanceCount ) const = 0;
    virtual int GetVertexCount() const = 0;
    virtual int GetStride() const = 0;
    virtual uint64_t GetVertexBufferGPUVA() const = 0; // DXR: returns 0 on GL/DX11
};
} // namespace Rendering
} // namespace SkullbonezCore

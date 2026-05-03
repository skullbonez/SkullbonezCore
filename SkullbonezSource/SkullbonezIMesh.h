#pragma once


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IMesh ------------------------------------------------------------------------------------------------------------------------------------------------------

    Abstract MeshGL interface. Concrete implementations handle VAO/VBO (OpenGL) or ID3D11Buffer (DirectX).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IMesh
{

  public:
    virtual ~IMesh() = default;

    virtual void Draw() const = 0;
    virtual void DrawInstanced( int instanceCount ) const = 0;
    virtual int GetVertexCount() const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

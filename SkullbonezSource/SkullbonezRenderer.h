#pragma once


namespace SkullbonezCore
{
namespace Rendering
{
enum class RenderAPI
{
    OpenGL33,
    DirectX11
};

class IRenderer
{
  public:
    virtual ~IRenderer() = default;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;
    virtual void Clear( float r, float g, float b, float a ) = 0;
    virtual void SetViewport( int x, int y, int width, int height ) = 0;
    virtual void SetClearDepth( float depth ) = 0;

    virtual RenderAPI GetAPI() const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

#pragma once


#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- IFramebuffer -----------------------------------------------------------------------------------------------------------------------------------------------

    Abstract FramebufferGL interface. Concrete implementations handle FBO (OpenGL) or RTV/DSV (DirectX).
    GetColorTextureHandle returns an opaque handle — pass it to IRenderBackend::BindTexture().
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IFramebuffer
{

  public:
    virtual ~IFramebuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;
    virtual uint32_t GetColorTextureHandle() const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void ResetResources() = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

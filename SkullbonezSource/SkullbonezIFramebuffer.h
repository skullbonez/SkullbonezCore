#pragma once


#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{
enum class FramebufferColorFormat
{
    RGBA8,  // Normal 8-bit-per-channel color, good for ordinary reflection targets.
    RGBA16F // Floating-point HDR color, used when bloom/tonemap need values brighter than white.
};

/* -- IFramebuffer -----------------------------------------------------------------------------------------------------------------------------------------------

    Abstract framebuffer interface. Concrete implementations handle FBO (OpenGL) or RTV/DSV (DirectX).
    GetColorTextureHandle returns an opaque handle — pass it to IRenderBackend::BindTexture().
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IFramebuffer
{

  public:
    virtual ~IFramebuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;
    virtual uint32_t GetColorTextureHandle() const = 0;

    // Cinematic post effects need depth as a texture, not just as hidden GPU
    // depth-test storage. Sampling this lets shaders tell sky from terrain/balls.
    virtual uint32_t GetDepthTextureHandle() const = 0;

    // The render path checks this so it can recreate the FBO if the same size
    // exists but the effect needs a different color precision.
    virtual FramebufferColorFormat GetColorFormat() const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void ResetResources() = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

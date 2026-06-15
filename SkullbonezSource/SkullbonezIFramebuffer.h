/*
File: SkullbonezSource/SkullbonezIFramebuffer.h
Purpose:
  Declares the renderer-neutral off-screen framebuffer interface.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads or writes
  depth/stencil data for depth testing.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  FBO (Framebuffer Object): OpenGL-style off-screen render target concept used
  by parity and reflection code.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
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

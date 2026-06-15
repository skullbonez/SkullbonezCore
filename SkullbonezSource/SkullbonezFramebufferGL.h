/*
File: SkullbonezSource/SkullbonezFramebufferGL.h
Purpose:
  Declares off-screen framebuffer resources for the OpenGL parity renderer.

Mental model:
  OpenGL is a legacy parity renderer. It provides a reference path for visual
  comparison while DX12 remains the production renderer.

Glossary:
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
  FBO (Framebuffer Object): OpenGL-style off-screen render target concept used
  by parity and reflection code.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezFramebufferGL.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <glad/gl.h>
#pragma comment( lib, "opengl32.lib" )
#include "SkullbonezCommon.h"
#include "SkullbonezIFramebuffer.h"

namespace SkullbonezCore
{
namespace Rendering
{
/* -- FramebufferGL -----------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of IFramebuffer. Offscreen render target: one RGBA color texture + depth texture.
    Used for the water reflection pre-pass.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FramebufferGL : public IFramebuffer
{

  private:
    GLuint m_fbo;                         // FramebufferGL object
    GLuint m_colorTex;                    // Color attachment texture
    GLuint m_depthTex;                    // Depth attachment texture
    FramebufferColorFormat m_colorFormat; // Color storage format
    int m_width;                          // Texture m_width  (pixels)
    int m_height;                         // Texture m_height (pixels)

    void Build(); // Allocate all GL objects

  public:
    FramebufferGL( int width, int height, FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 ); // Create FBO at given resolution
    ~FramebufferGL() override;                                                                                  // Delete all GL objects

    void Bind() const override;                             // Bind as render target
    void Unbind() const override;                           // Restore default FramebufferGL
    uint32_t GetColorTextureHandle() const override;        // Returns color texture handle
    uint32_t GetDepthTextureHandle() const override;        // Returns depth texture handle
    FramebufferColorFormat GetColorFormat() const override; // Returns color texture storage format
    int GetWidth() const override;                          // Returns FBO width in pixels
    int GetHeight() const override;                         // Returns FBO height in pixels
    void ResetResources() override;                         // Rebuild after GL context recreation
};
} // namespace Rendering
} // namespace SkullbonezCore

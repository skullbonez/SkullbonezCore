#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIFramebuffer.h"

namespace SkullbonezCore
{
namespace Rendering
{
/* -- Framebuffer -----------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of IFramebuffer. Offscreen render target: one RGB color texture + depth renderbuffer.
    Used for the water reflection pre-pass.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Framebuffer : public IFramebuffer
{

  private:
    GLuint m_fbo;      // Framebuffer object
    GLuint m_colorTex; // Color attachment (RGB texture)
    GLuint m_depthRBO; // Depth attachment (renderbuffer)
    int m_width;       // Texture m_width  (pixels)
    int m_height;      // Texture m_height (pixels)

    void Build(); // Allocate all GL objects

  public:
    Framebuffer( int width, int height ); // Create FBO at given resolution
    ~Framebuffer() override;              // Delete all GL objects

    void Bind() const override;                      // Bind as render target
    void Unbind() const override;                    // Restore default framebuffer
    uint32_t GetColorTextureHandle() const override; // Returns color texture handle
    int GetWidth() const override;                   // Returns FBO width in pixels
    int GetHeight() const override;                  // Returns FBO height in pixels
    void ResetResources() override;                  // Rebuild after GL context recreation
};
} // namespace Rendering
} // namespace SkullbonezCore

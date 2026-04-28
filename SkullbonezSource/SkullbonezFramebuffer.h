/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_FRAMEBUFFER_H
#define SKULLBONEZ_FRAMEBUFFER_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Rendering
{
/* -- Framebuffer -----------------------------------------------------------------------------------------------------------------------------------------------

    Offscreen render target: one RGB color texture + depth renderbuffer.
    Used for the water reflection pre-pass.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Framebuffer
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
    ~Framebuffer();                 // Delete all GL objects

    void Bind() const;              // Bind as render target
    void Unbind() const;            // Restore default framebuffer
    GLuint GetColorTexture() const; // Returns color texture handle
    int GetWidth() const;           // Returns FBO width in pixels
    int GetHeight() const;          // Returns FBO height in pixels
    void ResetGLResources();        // Rebuild after GL context recreation
};
} // namespace Rendering
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

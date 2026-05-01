#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Framebuffer
{

  private:
    GLuint m_fbo;      // Framebuffer object
    GLuint m_colorTex; // Color attachment (RGB texture)
    GLuint m_depthRBO; // Depth attachment (renderbuffer)
    int m_width;       // Texture width (pixels)
    int m_height;      // Texture height (pixels)

    void Build();

  public:
    Framebuffer( int width, int height );
    ~Framebuffer();

    void Bind() const;
    void Unbind() const;
    GLuint GetColorTexture() const;
    int GetWidth() const;
    int GetHeight() const;
    void ResetGLResources();
};
} // namespace Rendering
} // namespace SkullbonezCore

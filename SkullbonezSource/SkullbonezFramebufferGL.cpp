// --- Includes ---
#include "SkullbonezFramebufferGL.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


FramebufferGL::FramebufferGL( int m_width, int m_height, FramebufferColorFormat colorFormat )
    : m_fbo( 0 ), m_colorTex( 0 ), m_depthTex( 0 ), m_colorFormat( colorFormat ), m_width( m_width ), m_height( m_height )
{
    Build();
}


FramebufferGL::~FramebufferGL()
{
    if ( m_fbo )
    {
        // Delete the Framebuffer Object (the "virtual screen" we were rendering into).
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteFramebuffers.xhtml
        glDeleteFramebuffers( 1, &m_fbo );
    }
    if ( m_colorTex )
    {
        // Delete the color texture that was attached to the FBO.
        glDeleteTextures( 1, &m_colorTex );
    }
    if ( m_depthTex )
    {
        // Delete the depth texture that was attached to the FBO.
        glDeleteTextures( 1, &m_depthTex );
    }
}


void FramebufferGL::Build()
{
    // --- Framebuffer Object (FBO) Concept ---
    // An FBO is like a "virtual screen" — you can render into it instead of the real screen.
    // The rendered image becomes a texture you can use later (e.g. water reflections:
    // render the scene upside-down into an FBO, then paste that texture onto the water surface).
    //
    //  +------------------+        +------------------+
    //  | FBO              |        | Screen           |
    //  | +-Color Texture-+|        |                  |
    //  | | rendered image ||  --->  | water uses this  |
    //  | +---------------+|        | as a texture     |
    //  | +-Depth Buffer--+|        |                  |
    //  | | depth values   ||        |                  |
    //  | +---------------+|        |                  |
    //  +------------------+        +------------------+

    // --- Step 1: Create the color attachment (a texture the FBO renders INTO) ---

    // Generate and bind a 2D texture for the FBO's color output.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenTextures.xhtml
    glGenTextures( 1, &m_colorTex );
    glBindTexture( GL_TEXTURE_2D, m_colorTex );

    // RGBA8 is the normal "display color" format. RGBA16F is the HDR format used
    // by cinematic rendering so values can go above 1.0 before tonemapping.
    const GLint internalFormat = ( m_colorFormat == FramebufferColorFormat::RGBA16F ) ? GL_RGBA16F : GL_RGBA8;
    const GLenum uploadFormat = GL_RGBA;
    const GLenum uploadType = ( m_colorFormat == FramebufferColorFormat::RGBA16F ) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;

    // Allocate an empty texture at the FBO's resolution. nullptr = no initial pixel data.
    // This texture will be filled by rendering into the FBO.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexImage2D.xhtml
    glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, m_width, m_height, 0, uploadFormat, uploadType, nullptr );

    // Set filtering to LINEAR (smooth) when sampling this texture later.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

    // CLAMP_TO_EDGE prevents texture wrapping at borders — edge pixels are repeated instead
    // of the texture tiling. Important for FBO textures to avoid seam artifacts.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexParameter.xhtml
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glBindTexture( GL_TEXTURE_2D, 0 );

    // --- Step 2: Create the depth attachment (a texture for depth testing and sampling) ---

    // This used to be a renderbuffer, which is fine when depth is only used for
    // hidden-surface testing. Cinematic post effects need to read depth in a
    // shader, so it must be a texture instead.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenTextures.xhtml
    glGenTextures( 1, &m_depthTex );
    glBindTexture( GL_TEXTURE_2D, m_depthTex );

    // Allocate storage for the depth texture at the FBO's resolution. Nearest
    // filtering avoids blending depth values between neighboring pixels, which
    // would make fog and ray masks mushy around object edges.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexImage2D.xhtml
    glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glBindTexture( GL_TEXTURE_2D, 0 );

    // --- Step 3: Assemble the FBO (connect the color texture and depth buffer) ---

    // Create the FBO itself — this is the container that ties the attachments together.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenFramebuffers.xhtml
    glGenFramebuffers( 1, &m_fbo );

    // Bind the FBO — all subsequent attachment calls apply to this FBO.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindFramebuffer.xhtml
    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );

    // Attach our color texture as the FBO's color output (COLOR_ATTACHMENT0).
    // When we render into this FBO, pixel colors go into this texture.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFramebufferTexture2D.xhtml
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0 );

    // Attach the depth texture. Depth testing still works while rendering, and
    // later post-process shaders can sample the same texture to reconstruct fog
    // and light-shaft occlusion.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFramebufferTexture2D.xhtml
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0 );

    // Verify the FBO is properly set up. If any attachment is missing or incompatible,
    // this returns something other than GL_FRAMEBUFFER_COMPLETE and we throw.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCheckFramebufferStatus.xhtml
    if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
    {
        throw std::runtime_error( "FramebufferGL: incomplete FramebufferGL object" );
    }

    // Unbind — rendering will go back to the real screen until we explicitly bind this FBO.
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}


void FramebufferGL::Bind() const
{
    // Redirect all rendering into this FBO instead of the screen. Everything drawn after
    // this call goes into our color texture + depth buffer until Unbind() is called.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindFramebuffer.xhtml
    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
}


void FramebufferGL::Unbind() const
{
    // Bind framebuffer 0 = the default framebuffer = the actual screen. Subsequent
    // rendering goes to the screen again.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindFramebuffer.xhtml
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}


uint32_t FramebufferGL::GetColorTextureHandle() const
{
    return static_cast<uint32_t>( m_colorTex );
}


uint32_t FramebufferGL::GetDepthTextureHandle() const
{
    return static_cast<uint32_t>( m_depthTex );
}

FramebufferColorFormat FramebufferGL::GetColorFormat() const
{
    return m_colorFormat;
}


int FramebufferGL::GetWidth() const
{
    return m_width;
}


int FramebufferGL::GetHeight() const
{
    return m_height;
}


void FramebufferGL::ResetResources()
{
    if ( m_fbo )
    {
        glDeleteFramebuffers( 1, &m_fbo );
        m_fbo = 0;
    }
    if ( m_colorTex )
    {
        glDeleteTextures( 1, &m_colorTex );
        m_colorTex = 0;
    }
    if ( m_depthTex )
    {
        glDeleteTextures( 1, &m_depthTex );
        m_depthTex = 0;
    }
    Build();
}

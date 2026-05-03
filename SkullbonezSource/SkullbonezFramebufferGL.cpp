// --- Includes ---
#include "SkullbonezFramebufferGL.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


FramebufferGL::FramebufferGL( int m_width, int m_height )
    : m_fbo( 0 ), m_colorTex( 0 ), m_depthRBO( 0 ), m_width( m_width ), m_height( m_height )
{
    Build();
}


FramebufferGL::~FramebufferGL()
{
    if ( m_fbo )
    {
        glDeleteFramebuffers( 1, &m_fbo );
    }
    if ( m_colorTex )
    {
        glDeleteTextures( 1, &m_colorTex );
    }
    if ( m_depthRBO )
    {
        glDeleteRenderbuffers( 1, &m_depthRBO );
    }
}


void FramebufferGL::Build()
{
    // Color texture (RGB, no mipmaps, clamped edges)
    glGenTextures( 1, &m_colorTex );
    glBindTexture( GL_TEXTURE_2D, m_colorTex );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glBindTexture( GL_TEXTURE_2D, 0 );

    // Depth renderbuffer
    glGenRenderbuffers( 1, &m_depthRBO );
    glBindRenderbuffer( GL_RENDERBUFFER, m_depthRBO );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT, m_width, m_height );
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );

    // Assemble FBO
    glGenFramebuffers( 1, &m_fbo );
    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0 );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRBO );

    if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
    {
        throw std::runtime_error( "FramebufferGL: incomplete FramebufferGL object" );
    }

    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}


void FramebufferGL::Bind() const
{
    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
}


void FramebufferGL::Unbind() const
{
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}


uint32_t FramebufferGL::GetColorTextureHandle() const
{
    return static_cast<uint32_t>( m_colorTex );
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
    if ( m_depthRBO )
    {
        glDeleteRenderbuffers( 1, &m_depthRBO );
        m_depthRBO = 0;
    }
    Build();
}

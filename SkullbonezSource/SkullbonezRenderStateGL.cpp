// --- Includes ---
#include "SkullbonezRenderState.h"

// --- Usings ---
using namespace SkullbonezCore::Rendering;

void RenderState::SetupInitial()
{
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClearDepth( 1.0f );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glFrontFace( GL_CCW );
}

void RenderState::EnableDepthTest()
{
    glEnable( GL_DEPTH_TEST );
}

void RenderState::DisableDepthTest()
{
    glDisable( GL_DEPTH_TEST );
}

void RenderState::EnableBlending()
{
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

void RenderState::DisableBlending()
{
    glDisable( GL_BLEND );
}

void RenderState::SetClearColor( float r, float g, float b, float a )
{
    glClearColor( r, g, b, a );
}

void RenderState::SetClearDepth( float depth )
{
    glClearDepth( depth );
}

void RenderState::ClearFramebuffer()
{
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           |   .--. .--.   |
                                                                           | )/   | |   \( |
                                                                           |/ \__/   \__/ \|
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             |\         /|
                                                                             |\'"VUUUV"'/|
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* -- INCLUDES ------------------------------------------------------------------------------------------*/
#include "SkullbonezRenderer2D.h"
#include <cmath>

namespace SkullbonezCore
{
namespace Geometry
{
Renderer2D::Renderer2D()
    : m_pLineShader( nullptr ), m_lineVAO( 0 ), m_lineVBO( 0 ), m_maxLineVertices( 10000 ),
      m_currentLineVertices( 0 )
{
}

Renderer2D::~Renderer2D()
{
    Destroy();
}

void Renderer2D::Initialise( int /* screenWidth */, int /* screenHeight */ )
{
    m_pLineShader = std::make_unique<Rendering::Shader>( "SkullbonezData/shaders/line_2d.vert",
                                                         "SkullbonezData/shaders/line_2d.frag" );

    glGenVertexArrays( 1, &m_lineVAO );
    glGenBuffers( 1, &m_lineVBO );

    glBindVertexArray( m_lineVAO );
    glBindBuffer( GL_ARRAY_BUFFER, m_lineVBO );
    glBufferData( GL_ARRAY_BUFFER, sizeof( LineVertex ) * m_maxLineVertices, nullptr, GL_DYNAMIC_DRAW );

    glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( LineVertex ), (void*)offsetof( LineVertex, x ) );
    glEnableVertexAttribArray( 0 );

    glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, sizeof( LineVertex ), (void*)offsetof( LineVertex, r ) );
    glEnableVertexAttribArray( 1 );

    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindVertexArray( 0 );

    m_currentLineVertices = 0;
}

void Renderer2D::Destroy()
{
    if ( m_lineVBO )
    {
        glDeleteBuffers( 1, &m_lineVBO );
        m_lineVBO = 0;
    }
    if ( m_lineVAO )
    {
        glDeleteVertexArrays( 1, &m_lineVAO );
        m_lineVAO = 0;
    }
    m_pLineShader.reset();
}

void Renderer2D::BeginFrame( int /* screenWidth */, int /* screenHeight */ )
{
    m_currentLineVertices = 0;
}

void Renderer2D::DrawLine( float x1, float y1, float x2, float y2, float r, float g, float b, float a )
{
    if ( m_currentLineVertices + 2 > m_maxLineVertices )
    {
        return;
    }

    LineVertex v1{ x1, y1, r, g, b, a };
    LineVertex v2{ x2, y2, r, g, b, a };

    glBindBuffer( GL_COPY_WRITE_BUFFER, m_lineVBO );
    glBufferSubData( GL_COPY_WRITE_BUFFER, m_currentLineVertices * sizeof( LineVertex ), sizeof( LineVertex ), &v1 );
    glBufferSubData( GL_COPY_WRITE_BUFFER, ( m_currentLineVertices + 1 ) * sizeof( LineVertex ), sizeof( LineVertex ), &v2 );
    glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );

    m_currentLineVertices += 2;
}

void Renderer2D::DrawCircle( float cx, float cy, float radius, int segments, float r, float g, float b, float a )
{
    float angleStep = 2.0f * 3.14159265f / segments;

    for ( int i = 0; i < segments; i++ )
    {
        float angle1 = i * angleStep;
        float angle2 = ( i + 1 ) * angleStep;

        float x1 = cx + radius * std::cos( angle1 );
        float y1 = cy + radius * std::sin( angle1 );
        float x2 = cx + radius * std::cos( angle2 );
        float y2 = cy + radius * std::sin( angle2 );

        DrawLine( x1, y1, x2, y2, r, g, b, a );
    }
}

void Renderer2D::EndFrame()
{
    if ( m_currentLineVertices == 0 || !m_pLineShader )
    {
        return;
    }

    Matrix4 orthoProj = Matrix4::Ortho( 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f );
    Matrix4 identity;

    m_pLineShader->Use();
    m_pLineShader->SetMat4( "projection", orthoProj );
    m_pLineShader->SetMat4( "view", identity );
    m_pLineShader->SetMat4( "model", identity );

    glBindVertexArray( m_lineVAO );
    glDrawArrays( GL_LINES, 0, m_currentLineVertices );
    glBindVertexArray( 0 );

    m_currentLineVertices = 0;
}

} // namespace Geometry
} // namespace SkullbonezCore

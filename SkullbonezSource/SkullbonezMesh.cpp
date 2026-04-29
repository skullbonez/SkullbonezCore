// --- Includes ---
#include "SkullbonezMesh.h"


// --- Usings ---
using namespace SkullbonezCore::Rendering;


Mesh::Mesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode )
{
    m_vertexCount = vertexCount;
    m_drawMode = drawMode;

    // Calculate stride: m_position(3) + optional m_normal(3) + optional texcoord(2)
    int floatsPerVertex = 3;
    if ( hasNormals )
    {
        floatsPerVertex += 3;
    }
    if ( hasTexCoords )
    {
        floatsPerVertex += 2;
    }
    int stride = floatsPerVertex * static_cast<int>( sizeof( float ) );

    // Create VAO
    glGenVertexArrays( 1, &m_vao );
    glBindVertexArray( m_vao );

    // Create VBO and upload data
    glGenBuffers( 1, &m_vbo );
    glBindBuffer( GL_ARRAY_BUFFER, m_vbo );
    glBufferData( GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>( m_vertexCount ) * stride,
                  data,
                  GL_STATIC_DRAW );

    // Configure vertex attributes
    int offset = 0;

    // location 0 = aPosition (vec3)
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
    offset += 3 * static_cast<int>( sizeof( float ) );

    // location 1 = aNormal (vec3)
    if ( hasNormals )
    {
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
        offset += 3 * static_cast<int>( sizeof( float ) );
    }

    // location 2 = aTexCoord (vec2)
    if ( hasTexCoords )
    {
        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
    }

    // Unbind
    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}


Mesh::~Mesh()
{
    if ( m_vbo )
    {
        glDeleteBuffers( 1, &m_vbo );
    }
    if ( m_vao )
    {
        glDeleteVertexArrays( 1, &m_vao );
    }
}


void Mesh::Draw() const
{
    glBindVertexArray( m_vao );
    glDrawArrays( m_drawMode, 0, m_vertexCount );
    glBindVertexArray( 0 );
}


int Mesh::GetVertexCount() const
{
    return m_vertexCount;
}


GLuint Mesh::GetVAO() const
{
    return m_vao;
}

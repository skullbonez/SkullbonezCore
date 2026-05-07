// --- Includes ---
#include "SkullbonezMeshGL.h"


// --- Usings ---
using namespace SkullbonezCore::Rendering;


MeshGL::MeshGL( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode )
{
    // --- Mesh Upload Concept ---
    // A "mesh" is a collection of triangles that form a 3D shape. Each triangle has 3 vertices,
    // and each vertex has attributes: position (where it is), normal (which direction it faces),
    // and texture coordinates (which part of a texture image maps to it).
    //
    // The data arrives as a flat float array from CPU memory. We upload it to GPU memory (VBO)
    // and describe the layout (VAO) so the vertex shader knows how to read it.
    //
    // Memory layout per vertex (example with all attributes):
    //   [posX, posY, posZ, normX, normY, normZ, texU, texV]
    //    |--- 3 floats ---|--- 3 floats ------|-- 2 floats-|
    //    |<------------- stride (bytes) ------------------>|

    m_vertexCount = vertexCount;
    m_drawMode = drawMode;

    // Calculate stride: position(3) + optional normal(3) + optional texcoord(2)
    int floatsPerVertex = 3;
    if ( hasNormals )
    {
        floatsPerVertex += 3;
    }
    if ( hasTexCoords )
    {
        floatsPerVertex += 2;
    }
    m_stride = floatsPerVertex * static_cast<int>( sizeof( float ) );

    // Create VAO — remembers all the vertex attribute configuration below.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenVertexArrays.xhtml
    glGenVertexArrays( 1, &m_vao );
    glBindVertexArray( m_vao );

    // Create VBO and upload the mesh vertex data to GPU memory.
    // GL_STATIC_DRAW = uploaded once, drawn many times (ideal for static meshes).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenBuffers.xhtml
    glGenBuffers( 1, &m_vbo );
    glBindBuffer( GL_ARRAY_BUFFER, m_vbo );
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
    glBufferData( GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>( m_vertexCount ) * m_stride,
                  data,
                  GL_STATIC_DRAW );

    // Configure vertex attributes — tell the GPU how to interpret the interleaved data.
    int offset = 0;

    // location 0 = aPosition (vec3) — the 3D position of this vertex in model space.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glVertexAttribPointer.xhtml
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, m_stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
    offset += 3 * static_cast<int>( sizeof( float ) );

    // location 1 = aNormal (vec3) — the surface direction, used for lighting calculations.
    if ( hasNormals )
    {
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, m_stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
        offset += 3 * static_cast<int>( sizeof( float ) );
    }

    // location 2 = aTexCoord (vec2) — UV coordinates that map a 2D texture onto the 3D surface.
    if ( hasTexCoords )
    {
        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, m_stride, reinterpret_cast<void*>( static_cast<intptr_t>( offset ) ) );
    }

    // Unbind to prevent accidental modification.
    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}


MeshGL::~MeshGL()
{
    if ( m_vbo )
    {
        // Free the GPU vertex buffer memory.
        glDeleteBuffers( 1, &m_vbo );
    }
    if ( m_vao )
    {
        // Free the vertex layout descriptor.
        glDeleteVertexArrays( 1, &m_vao );
    }
}


void MeshGL::Draw() const
{
    // Bind the VAO (restores all vertex attribute settings), then draw the mesh.
    // The GPU processes every vertex through the vertex shader, assembles triangles, and
    // runs the fragment shader on each visible pixel.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDrawArrays.xhtml
    glBindVertexArray( m_vao );
    glDrawArrays( m_drawMode, 0, m_vertexCount );
}


void MeshGL::DrawInstanced( int instanceCount ) const
{
    // Same as Draw() but repeats the mesh instanceCount times in a single GPU command.
    // Each instance can have different per-instance attributes (like a model matrix).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDrawArraysInstanced.xhtml
    glBindVertexArray( m_vao );
    glDrawArraysInstanced( m_drawMode, 0, m_vertexCount, instanceCount );
}


int MeshGL::GetVertexCount() const
{
    return m_vertexCount;
}


int MeshGL::GetStride() const
{
    return m_stride;
}

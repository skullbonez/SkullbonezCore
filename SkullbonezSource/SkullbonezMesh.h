#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Rendering
{
/* -- Mesh ------------------------------------------------------------------------------------------------------------------------------------------------------

    VAO/VBO wrapper for interleaved vertex data. Supports flexible vertex formats:
      - Position only (3 floats)
      - Position + Normal (6 floats)
      - Position + TexCoord (5 floats)
      - Position + Normal + TexCoord (8 floats)

    Vertex attribute layout:
      location 0 = aPosition (vec3)
      location 1 = aNormal   (vec3)  [if hasNormals]
      location 2 = aTexCoord (vec2)  [if hasTexCoords]
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Mesh
{

  private:
    GLuint m_vao;      // Vertex Array Object
    GLuint m_vbo;      // Vertex Buffer Object
    int m_vertexCount; // Number of vertices
    GLenum m_drawMode; // GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.

  public:
    Mesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode = GL_TRIANGLES ); // Upload interleaved vertex data
    ~Mesh();                                                                                                        // Destructor: delete VAO/VBO

    void Draw() const;          // Bind VAO and draw
    int GetVertexCount() const; // Get vertex count
    GLuint GetVAO() const;      // Get VAO handle (for instanced draw setup)
};
} // namespace Rendering
} // namespace SkullbonezCore

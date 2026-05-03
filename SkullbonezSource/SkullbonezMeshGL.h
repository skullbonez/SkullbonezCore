#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIMesh.h"

namespace SkullbonezCore
{
namespace Rendering
{
/* -- MeshGL ------------------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of IMesh. VAO/VBO wrapper for interleaved vertex data.
    Supports flexible vertex formats:
      - Position only (3 floats)
      - Position + Normal (6 floats)
      - Position + TexCoord (5 floats)
      - Position + Normal + TexCoord (8 floats)

    Vertex attribute layout:
      location 0 = aPosition (vec3)
      location 1 = aNormal   (vec3)  [if hasNormals]
      location 2 = aTexCoord (vec2)  [if hasTexCoords]
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class MeshGL : public IMesh
{

  private:
    GLuint m_vao;      // Vertex Array Object
    GLuint m_vbo;      // Vertex Buffer Object
    int m_vertexCount; // Number of vertices
    GLenum m_drawMode; // GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.

  public:
    MeshGL( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode = GL_TRIANGLES ); // Upload interleaved vertex data
    ~MeshGL() override;                                                                                               // Destructor: delete VAO/VBO

    void Draw() const override; // Bind VAO and draw
    void DrawInstanced( int instanceCount ) const override;
    int GetVertexCount() const override; // Get vertex count
};
} // namespace Rendering
} // namespace SkullbonezCore

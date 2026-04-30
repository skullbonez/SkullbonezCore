#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"


namespace SkullbonezCore
{
namespace Rendering
{
class Mesh
{
  private:
    GLuint m_vao;
    GLuint m_vbo;
    int m_vertexCount;
    GLenum m_drawMode;

  public:
    Mesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode = GL_TRIANGLES );
    ~Mesh();

    void Draw() const;
    int GetVertexCount() const;
};
} // namespace Rendering
} // namespace SkullbonezCore

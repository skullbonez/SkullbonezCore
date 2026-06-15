/*
File: SkullbonezSource/SkullbonezMeshGL.h
Purpose:
  Declares mesh buffers and draw binding for the OpenGL parity renderer.

Mental model:
  OpenGL is a legacy parity renderer. It provides a reference path for visual
  comparison while DX12 remains the production renderer.

Glossary:
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezMeshGL.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <glad/gl.h>
#pragma comment( lib, "opengl32.lib" )
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
    int m_stride;      // Bytes per vertex
    GLenum m_drawMode; // GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.

  public:
    MeshGL( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode = GL_TRIANGLES ); // Upload interleaved vertex data
    ~MeshGL() override;                                                                                               // Destructor: delete VAO/VBO

    void Draw() const override; // Bind VAO and draw
    void DrawInstanced( int instanceCount ) const override;
    int GetVertexCount() const override; // Get vertex count
    int GetStride() const override;
    uint64_t GetVertexBufferGPUVA() const override
    {
        return 0;
    }
};
} // namespace Rendering
} // namespace SkullbonezCore

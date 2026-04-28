#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezShader.h"
#include "SkullbonezMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


namespace SkullbonezCore
{
namespace Basics
{
/* -- Skullbonez Helper ------------------------------------------------------------------------------------------------------------------------------------------

    Static helper to assist in OpenGL state setup.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezHelper
{

  private:
    static std::unique_ptr<Mesh> sphereMesh;                          // Shared unit sphere VBO
    static std::unique_ptr<Shader> sphereShader;                      // Shared lit_textured m_shader
    inline static float sClipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f }; // default: always pass (GL_CLIP_DISTANCE0 disabled)

    static void BuildSphereMesh( int slices, int stacks ); // Generate UV sphere mesh

  public:
    static void StateSetup();                                                                                                          // Assists in setting up initial open gl state
    static void SetClipPlane( float x, float y, float z, float w );                                                                    // Set sphere shader clip plane (default (0,1,0,1e9) = always pass)
    static void DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent = false ); // Bind shader and set invariant uniforms for batched sphere rendering
    static void DrawSphereBatchModel( const Matrix4& model );                                                                          // Set model matrix and draw one sphere (call between Begin/End)
    static void DrawSphereBatchEnd();                                                                                                  // Unbind shader after batched sphere rendering
    static void ResetGLResources();                                                                                                    // Call after GL context recreated to invalidate cached GL objects
};
} // namespace Basics
} // namespace SkullbonezCore

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_HELPER_H
#define SKULLBONEZ_HELPER_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezShader.h"
#include "SkullbonezMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
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
    static std::unique_ptr<Mesh> sphereMesh;     // Shared unit sphere VBO
    static std::unique_ptr<Shader> sphereShader; // Shared lit_textured m_shader
    static float sClipPlane[4];                  // Active clip plane for sphere m_shader

    static void BuildSphereMesh( int slices, int stacks ); // Generate UV sphere mesh

  public:
    static void StateSetup( void );                                                                                                                // Assists in setting up initial open gl state
    static void SetClipPlane( float x, float y, float z, float w );                                                                                // Set sphere shader clip plane (default (0,1,0,1e9) = always pass)
    static void DrawSphere( const Matrix4& model, const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent = false ); // Draws a sphere with the supplied model/view/projection matrices
    static void DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent = false );            // Setup shader and invariant uniforms for batch rendering
    static void DrawSphereBatchModel( const Matrix4& model );                                                                                      // Render one sphere with only model matrix update (after DrawSphereBatchBegin)
    static void DrawSphereBatchEnd( void );                                                                                                       // Cleanup after batch rendering
    static void ResetGLResources( void );                                                                                                          // Call after GL context recreated to invalidate cached GL objects
};
} // namespace Basics
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

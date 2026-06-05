#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIShader.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"
#include <memory>
#include <utility>
#include <vector>


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


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
    static std::unique_ptr<IShader> sphereShader;                     // Shared lit_textured_instanced shader
    static uint32_t sphereInstMesh;                                   // Instanced mesh handle (via Gfx())
    static int sphereVertexCount;                                     // Per-sphere vertex count
    static std::vector<float> sphereInstanceData;                     // Staging buffer for model matrices + tint
    static uint32_t boxInstMesh;                                      // Instanced mesh handle for box
    static int boxVertexCount;                                        // Per-box vertex count
    static std::vector<float> boxInstanceData;                        // Staging buffer for box model matrices + tint
    inline static float sClipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f }; // default: always pass (GL_CLIP_DISTANCE0 disabled)

    static void BuildSphereMesh( int slices, int stacks ); // Generate UV sphere instanced mesh
    static void BuildBoxMesh();                            // Generate unit cube instanced mesh

  public:
    static void StateSetup();                                                                                                                  // Assists in setting up initial open gl state
    static void SetClipPlane( float x, float y, float z, float w );                                                                            // Set sphere shader clip plane (default (0,1,0,1e9) = always pass)
    static void DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent = false );         // Set up instanced shader uniforms and begin collecting instances
    static void DrawSphereBatchModel( const Matrix4& model, float tintR = 1.0f, float tintG = 1.0f, float tintB = 1.0f );                      // Append model matrix and tint to instance buffer
    static void DrawSphereBatchEnd();                                                                                                          // Upload instance data and issue single instanced draw
    static void DrawBoxBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent = false );            // Set up box instanced draw
    static void DrawBoxBatchModel( const Matrix4& model, float tintR = 1.0f, float tintG = 1.0f, float tintB = 1.0f );                         // Append box model matrix and tint to instance buffer
    static void DrawBoxBatchEnd();                                                                                                             // Upload box instance data and issue single instanced draw
    static void DrawDebugVectors( const Matrix4& viewProj, const std::vector<std::pair<Vector3, Vector3>>& lines, float r, float g, float b ); // Draw a batch of world-space line segments (GL only)
    static void ResetGLResources();                                                                                                            // Call after GL context recreated to invalidate cached GL objects
    static void EnsureSphereMesh();                                                                                                            // Ensure sphere instanced mesh is created (for DXR BLAS init)
    static uint32_t GetSphereInstMeshHandle()
    {
        return sphereInstMesh;
    } // DXR: returns instanced mesh handle for sphere VB access
    static int GetSphereVertexCount()
    {
        return sphereVertexCount;
    } // DXR: returns per-sphere vertex count
};
} // namespace Basics
} // namespace SkullbonezCore

#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIShader.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
struct CinematicRenderConfig;

/* -- Skullbonez Helper ------------------------------------------------------------------------------------------------------------------------------------------

    Static helper for shared primitive render resources and initial GL state setup.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezHelper
{

  private:
    static std::unique_ptr<Rendering::IShader> sphereShader;          // Shared lit_textured_instanced shader
    static uint32_t sphereInstMesh;                                   // Instanced mesh handle (via Gfx())
    static int sphereVertexCount;                                     // Per-sphere vertex count
    static std::vector<float> sphereInstanceData;                     // Staging buffer for model matrices + tint/override
    static uint32_t lowPolySphereInstMesh;                            // Faceted sphere mesh for low-poly cinematic styles
    static int lowPolySphereVertexCount;                              // Per-low-poly-sphere vertex count
    static uint32_t activeSphereInstMesh;                             // Mesh selected for the current sphere batch
    static int activeSphereVertexCount;                               // Vertex count selected for the current sphere batch
    static uint32_t boxInstMesh;                                      // Instanced mesh handle for box
    static int boxVertexCount;                                        // Per-box vertex count
    static std::vector<float> boxInstanceData;                        // Staging buffer for box model matrices + tint/override
    inline static float sClipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f }; // default: always pass (GL_CLIP_DISTANCE0 disabled)

    static void EnsureSphereShader();                             // Create shared instanced lighting shader
    static void BuildSphereMesh( int slices, int stacks );        // Generate UV sphere instanced mesh
    static void BuildLowPolySphereMesh( int slices, int stacks ); // Generate faceted sphere instanced mesh
    static void BuildBoxMesh();                                   // Generate unit cube instanced mesh

  public:
    static void StateSetup();                                                                                                                                                                                                        // Assists in setting up initial open gl state
    static void SetClipPlane( float x, float y, float z, float w );                                                                                                                                                                  // Set sphere shader clip plane (default (0,1,0,1e9) = always pass)
    static void DrawSphereBatchBegin( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false, const CinematicRenderConfig* cinematic = nullptr ); // Set up instanced shader uniforms and begin collecting instances
    static void DrawSphereBatchModel( const Math::Transformation::Matrix4& model, float tintR = 1.0f, float tintG = 1.0f, float tintB = 1.0f, float colorOverride = 0.0f );                                                          // Append model matrix and tint/override to instance buffer
    static void DrawSphereBatchEnd();                                                                                                                                                                                                // Upload instance data and issue single instanced draw
    static void DrawBoxBatchBegin( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false, const CinematicRenderConfig* cinematic = nullptr );    // Set up box instanced draw
    static void DrawBoxBatchModel( const Math::Transformation::Matrix4& model, float tintR = 1.0f, float tintG = 1.0f, float tintB = 1.0f, float colorOverride = 0.0f );                                                             // Append box model matrix and tint/override to instance buffer
    static void DrawBoxBatchEnd();                                                                                                                                                                                                   // Upload box instance data and issue single instanced draw
    static void ResetRenderResources();                                                                                                                                                                                              // Invalidate cached backend-owned meshes and shaders
    static void EnsureSphereMesh();                                                                                                                                                                                                  // Ensure sphere instanced mesh is created (for DXR BLAS init)
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

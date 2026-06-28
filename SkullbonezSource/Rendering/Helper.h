/*
File: SkullbonezSource/Rendering/Helper.h
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Asset system: Runtime-owned registry borrowed to resolve helper shader source
  while helper-owned shader handles remain backend resources.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  Instance buffer: CPU-built per-object payload uploaded so one mesh can draw
  many objects with different transforms/materials.
  Resource factory: Borrowed backend lifetime interface used to create or
  destroy helper-owned mesh, texture, and dynamic-buffer handles.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Helper-owned meshes and shaders are backend resources; reset paths must
    drop them before the renderer device is destroyed.
  - Draw helpers borrow command/resource facets for the current backend only;
    they may cache opaque handles, but not the facets themselves.

Related:
  - SkullbonezSource/Rendering/Helper.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "IShader.h"
#include "IMesh.h"
#include "../Maths/Matrix4.h"
#include "RenderMaterial.h"
#include "Shadow.h"
#include "../Maths/Vector3.h"
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets

namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
}
} // namespace Math
namespace Rendering
{
class IRenderCommandContext;
class IRenderResourceFactory;
} // namespace Rendering
namespace Basics
{
struct CinematicRenderConfig;
struct OrdinaryRenderConfig;

/* -- Skullbonez Helper
------------------------------------------------------------------------------------------------------------------------------------------

    Static helper for shared primitive render resources, especially instanced
    sphere/box/pine batches used by normal rendering and shadow passes.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderHelper
{

  private:
    static std::unique_ptr<Rendering::IShader> sphereShader;      // Shared lit_textured_instanced shader
    static std::unique_ptr<Rendering::IShader> shadowDepthShader; // Shared instanced directional shadow depth shader
    static uint32_t sphereInstMesh;                               // Instanced mesh handle owned by the active resource factory
    static int sphereVertexCount;                                 // Per-sphere vertex count
    static std::vector<float> sphereInstanceData;                 // Per-frame sphere transforms/materials queued between BatchBegin/End.
    static uint32_t lowPolySphereInstMesh;                        // Faceted sphere mesh for low-poly cinematic styles
    static int lowPolySphereVertexCount;                          // Per-low-poly-sphere vertex count
    static uint32_t activeSphereInstMesh;                         // Mesh selected for the current sphere batch
    static int activeSphereVertexCount;                           // Vertex count selected for the current sphere batch
    static uint32_t boxInstMesh;                                  // Instanced mesh handle for box
    static int boxVertexCount;                                    // Per-box vertex count
    static std::vector<float> boxInstanceData;                    // Per-frame box transforms/materials queued between BatchBegin/End.
    static uint32_t pineInstMesh;                                 // Instanced mesh handle for low-poly pine foliage tiers
    static int pineVertexCount;                                   // Per-pine-tier vertex count
    static std::vector<float> pineInstanceData;                   // Per-frame foliage transforms/materials queued between BatchBegin/End.
    inline static float sClipPlane[4] = { 0.0f,
                                          1.0f,
                                          0.0f,
                                          1.0e9f };               // default: always pass (GL_CLIP_DISTANCE0 disabled)

    static void EnsureSphereShader( Rendering::IRenderResourceFactory& renderResources,
                                    const Assets::AssetSystem& assets,
                                    const OrdinaryRenderConfig& ordinaryRender );
    static void EnsureShadowDepthShader( Rendering::IRenderResourceFactory& renderResources,
                                         const Assets::AssetSystem& assets );
    static void BuildSphereMesh( Rendering::IRenderResourceFactory& renderResources,
                                 int slices,
                                 int stacks );                   // Generate UV sphere instanced mesh
    static void BuildLowPolySphereMesh( Rendering::IRenderResourceFactory& renderResources,
                                        int slices,
                                        int stacks );            // Generate faceted sphere instanced mesh
    static void BuildBoxMesh( Rendering::IRenderResourceFactory& renderResources ); // Generate unit cube instanced mesh
    static void BuildPineMesh( Rendering::IRenderResourceFactory& renderResources ); // Generate unit low-poly pine tier mesh

  public:
    static void StateSetup();                                     // Extension point for helper-level setup after renderer init
    static void SetClipPlane( float x, float y, float z, float w );
    static const float* GetClipPlane();
    static void DrawSphereBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                      Rendering::IRenderResourceFactory& renderResources,
                                      const Assets::AssetSystem& assets,
                                      const Math::Transformation::Matrix4& view,
                                      const Math::Transformation::Matrix4& proj,
                                      const float lightPos[4],
                                      const OrdinaryRenderConfig& ordinaryRender,
                                      bool isTransparent = false,
                                      const CinematicRenderConfig* cinematic = nullptr,
                                      const Rendering::ShadowFrameData* shadow = nullptr,
                                      float materialAlpha = 1.0f );
    static void DrawSphereBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );              // Append model matrix and material payload to instance buffer
    static void
    DrawSphereBatchModel( const Math::Transformation::Matrix4& model,
                          float tintR = 1.0f,
                          float tintG = 1.0f,
                          float tintB = 1.0f,
                          float colorOverride = 0.0f );           // Compatibility bridge for old tint/override callers
    static void DrawSphereBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void DrawBoxBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                   Rendering::IRenderResourceFactory& renderResources,
                                   const Assets::AssetSystem& assets,
                                   const Math::Transformation::Matrix4& view,
                                   const Math::Transformation::Matrix4& proj,
                                   const float lightPos[4],
                                   const OrdinaryRenderConfig& ordinaryRender,
                                   bool isTransparent = false,
                                   const CinematicRenderConfig* cinematic = nullptr,
                                   const Rendering::ShadowFrameData* shadow = nullptr,
                                   float materialAlpha = 1.0f );
    static void DrawBoxBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );              // Append box model matrix and material payload to instance buffer
    static void DrawBoxBatchModel( const Math::Transformation::Matrix4& model,
                                   float tintR = 1.0f,
                                   float tintG = 1.0f,
                                   float tintB = 1.0f,
                                   float colorOverride = 0.0f );  // Compatibility bridge for old tint/override callers
    static void DrawBoxBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void DrawConvexHullModel( Rendering::IRenderCommandContext& renderCommands,
                                     Rendering::IRenderResourceFactory& renderResources,
                                     const Assets::AssetSystem& assets,
                                     const Math::CollisionDetection::ConvexHullShape& hull,
                                     const Math::Transformation::Matrix4& model,
                                     const Rendering::RenderMaterial& material,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const float lightPos[4],
                                     const OrdinaryRenderConfig& ordinaryRender,
                                     bool isTransparent = false,
                                     const CinematicRenderConfig* cinematic = nullptr,
                                     const Rendering::ShadowFrameData* shadow = nullptr,
                                     float materialAlpha = 1.0f );
    static void DrawPineBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                    Rendering::IRenderResourceFactory& renderResources,
                                    const Assets::AssetSystem& assets,
                                    const Math::Transformation::Matrix4& view,
                                    const Math::Transformation::Matrix4& proj,
                                    const float lightPos[4],
                                    const OrdinaryRenderConfig& ordinaryRender,
                                    bool isTransparent = false,
                                    const CinematicRenderConfig* cinematic = nullptr,
                                    const Rendering::ShadowFrameData* shadow = nullptr,
                                    float materialAlpha = 1.0f );
    static void DrawPineBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );              // Append pine model matrix and material payload to instance buffer
    static void DrawPineBatchModel( const Math::Transformation::Matrix4& model,
                                    float tintR = 1.0f,
                                    float tintG = 1.0f,
                                    float tintB = 1.0f,
                                    float colorOverride = 0.0f ); // Compatibility bridge for old tint/override callers
    static void DrawPineBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void DrawShadowDepthSphereBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                                 Rendering::IRenderResourceFactory& renderResources,
                                                 const Assets::AssetSystem& assets,
                                                 const Math::Transformation::Matrix4& view,
                                                 const Math::Transformation::Matrix4& proj,
                                                 const CinematicRenderConfig* cinematic = nullptr );
    static void DrawShadowDepthSphereBatchModel( const Math::Transformation::Matrix4& model );
    static void DrawShadowDepthSphereBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void DrawShadowDepthBoxBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                              Rendering::IRenderResourceFactory& renderResources,
                                              const Assets::AssetSystem& assets,
                                              const Math::Transformation::Matrix4& view,
                                              const Math::Transformation::Matrix4& proj );
    static void DrawShadowDepthBoxBatchModel( const Math::Transformation::Matrix4& model );
    static void DrawShadowDepthBoxBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void DrawShadowDepthConvexHullModel( Rendering::IRenderCommandContext& renderCommands,
                                                Rendering::IRenderResourceFactory& renderResources,
                                                const Assets::AssetSystem& assets,
                                                const Math::CollisionDetection::ConvexHullShape& hull,
                                                const Math::Transformation::Matrix4& model,
                                                const Math::Transformation::Matrix4& view,
                                                const Math::Transformation::Matrix4& proj );
    static void DrawShadowDepthPineBatchBegin( Rendering::IRenderCommandContext& renderCommands,
                                               Rendering::IRenderResourceFactory& renderResources,
                                               const Assets::AssetSystem& assets,
                                               const Math::Transformation::Matrix4& view,
                                               const Math::Transformation::Matrix4& proj );
    static void DrawShadowDepthPineBatchModel( const Math::Transformation::Matrix4& model );
    static void DrawShadowDepthPineBatchEnd( Rendering::IRenderCommandContext& renderCommands );
    static void ResetRenderResources( Rendering::IRenderResourceFactory* renderResources = nullptr ); // Invalidate cached backend-owned meshes and shaders
    static void EnsureSphereMesh( Rendering::IRenderResourceFactory& renderResources ); // Create the shared sphere mesh before DXR BLAS construction needs its vertex data.

    // DXR reflection reuses the same sphere vertex buffer as raster rendering.
    // The backend asks for the instanced mesh handle so it can find the static
    // vertex buffer and build the sphere BLAS.
    static uint32_t GetSphereInstMeshHandle()
    {
        return sphereInstMesh;
    }

    // Vertex count is part of the BLAS geometry description. The raytracing
    // build needs triangle-count information even though it does not draw here.
    static int GetSphereVertexCount()
    {
        return sphereVertexCount;
    }
};
} // namespace Basics
} // namespace SkullbonezCore

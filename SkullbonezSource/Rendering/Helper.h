/*
File: SkullbonezSource/Rendering/Helper.h
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  Instance buffer: CPU-built per-object payload uploaded so one mesh can draw
  many objects with different transforms/materials.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Helper-owned meshes and shaders are backend resources; reset paths must
  drop them before the renderer device is destroyed.

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
#include <array>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderResourceFactory;
} // namespace Rendering
namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
}
} // namespace Math
namespace Basics
{
struct CinematicRenderConfig;
class RenderHelper;

struct RenderHelperContext
{
    // Lifetime: borrowed only for the draw/resource call receiving this context.
    // RenderHelper owns primitive GPU handles and batch scratch; renderer
    // services and trace diagnostics stay owned by the frame host.
    Rendering::IRenderResourceFactory& renderResources;
    Rendering::IRenderCommandContext& renderCommands;
    Rendering::IRenderDiagnostics& renderDiagnostics;
    const Assets::AssetSystem& assets;
    const EngineConfig& config;
    RenderHelper& helper;
};

struct RenderHelperState
{
    static constexpr int INSTANCE_MATRIX_FLOATS = 16;
    static constexpr int INSTANCE_MATERIAL_FLOAT4_COUNT = 4;
    static constexpr int INSTANCE_MATERIAL_FLOATS = INSTANCE_MATERIAL_FLOAT4_COUNT * 4;
    static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_MATERIAL_FLOATS;
    // Invariant: mirrors ConvexHullShape::MAX_FACES/MAX_FACE_VERTICES without
    // including the physics hull header in this widely included render helper.
    static constexpr int HULL_MAX_TRIANGLE_VERTICES = 96 * ( 16 - 2 ) * 3;
    static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + 2 + INSTANCE_FLOATS;

    std::unique_ptr<Rendering::IShader> sphereShader;                                   // Shared lit_textured_instanced shader.
    std::unique_ptr<Rendering::IShader> shadowDepthShader;                              // Shared instanced directional shadow depth shader.
    uint32_t sphereInstMesh = 0;                                                        // Instanced mesh handle owned by the active render resource factory.
    int sphereVertexCount = 0;                                                          // Per-sphere vertex count.
    std::vector<float> sphereInstanceData;                                              // Queued sphere transforms/materials between batch begin/end.
    uint32_t lowPolySphereInstMesh = 0;                                                 // Faceted sphere mesh for low-poly cinematic styles.
    int lowPolySphereVertexCount = 0;                                                   // Per-low-poly-sphere vertex count.
    uint32_t activeSphereInstMesh = 0;                                                  // Mesh selected for the current sphere batch.
    int activeSphereVertexCount = 0;                                                    // Vertex count selected for the current sphere batch.
    uint32_t boxInstMesh = 0;                                                           // Instanced mesh handle for boxes.
    int boxVertexCount = 0;                                                             // Per-box vertex count.
    std::vector<float> boxInstanceData;                                                 // Queued box transforms/materials between batch begin/end.
    uint32_t pineInstMesh = 0;                                                          // Instanced mesh handle for low-poly pine foliage tiers.
    int pineVertexCount = 0;                                                            // Per-pine-tier vertex count.
    std::vector<float> pineInstanceData;                                                // Queued pine transforms/materials between batch begin/end.
    float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };                                  // Default: always pass.
    bool sphereBatchTransparent = false;
    bool boxBatchTransparent = false;
    bool pineBatchTransparent = false;
    bool sphereBatchReady = false;
    bool boxBatchReady = false;
    bool pineBatchReady = false;
    uint32_t materialTableTexture = 0;                                                  // Material defaults bound at shader slot t4.
    uint32_t convexHullDynamicVB = 0;                                                   // Dynamic vertex buffer used by immediate convex hull draws.
    std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> convexHullVertexData = {};
};

/* -- Skullbonez Helper
------------------------------------------------------------------------------------------------------------------------------------------

    Owner-local helper for shared primitive render resources, especially
    instanced sphere/box/pine batches used by normal rendering and shadow
    passes.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderHelper
{

  private:
    RenderHelperState m_state;                                                          // Owned primitive render cache and batch scratch.

    void EnsureSphereShader( const RenderHelperContext& context );
    void EnsureShadowDepthShader( const RenderHelperContext& context );
    void BuildSphereMesh( const RenderHelperContext& context, int slices, int stacks ); // Generate UV sphere instanced mesh
    void BuildLowPolySphereMesh( const RenderHelperContext& context,
                                 int slices,
                                 int stacks );                                          // Generate faceted sphere instanced mesh
    void BuildBoxMesh( const RenderHelperContext& context );                            // Generate unit cube instanced mesh
    void BuildPineMesh( const RenderHelperContext& context );                           // Generate unit low-poly pine tier mesh

  public:
    void StateSetup();                                                                  // Extension point for helper-level setup after renderer init
    void SetClipPlane( float x, float y, float z, float w );
    const float* GetClipPlane() const;
    void DrawSphereBatchBegin( const RenderHelperContext& context,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const float lightPos[4],
                               bool isTransparent = false,
                               const CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr,
                               float materialAlpha = 1.0f );
    void DrawSphereBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );                                    // Append model matrix and material payload to instance buffer
    void DrawSphereBatchModel( const Math::Transformation::Matrix4& model,
                               float tintR = 1.0f,
                               float tintG = 1.0f,
                               float tintB = 1.0f,
                               float colorOverride = 0.0f );                            // Compatibility bridge for old tint/override callers
    void DrawSphereBatchEnd( const RenderHelperContext& context );
    void DrawBoxBatchBegin( const RenderHelperContext& context,
                            const Math::Transformation::Matrix4& view,
                            const Math::Transformation::Matrix4& proj,
                            const float lightPos[4],
                            bool isTransparent = false,
                            const CinematicRenderConfig* cinematic = nullptr,
                            const Rendering::ShadowFrameData* shadow = nullptr,
                            float materialAlpha = 1.0f );
    void DrawBoxBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );                                    // Append box model matrix and material payload to instance buffer
    void DrawBoxBatchModel( const Math::Transformation::Matrix4& model,
                            float tintR = 1.0f,
                            float tintG = 1.0f,
                            float tintB = 1.0f,
                            float colorOverride = 0.0f );                               // Compatibility bridge for old tint/override callers
    void DrawBoxBatchEnd( const RenderHelperContext& context );
    void DrawConvexHullModel( const RenderHelperContext& context,
                              const Math::CollisionDetection::ConvexHullShape& hull,
                              const Math::Transformation::Matrix4& model,
                              const Rendering::RenderMaterial& material,
                              const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const float lightPos[4],
                              bool isTransparent = false,
                              const CinematicRenderConfig* cinematic = nullptr,
                              const Rendering::ShadowFrameData* shadow = nullptr,
                              float materialAlpha = 1.0f );
    void DrawPineBatchBegin( const RenderHelperContext& context,
                             const Math::Transformation::Matrix4& view,
                             const Math::Transformation::Matrix4& proj,
                             const float lightPos[4],
                             bool isTransparent = false,
                             const CinematicRenderConfig* cinematic = nullptr,
                             const Rendering::ShadowFrameData* shadow = nullptr,
                             float materialAlpha = 1.0f );
    void DrawPineBatchModel(
        const Math::Transformation::Matrix4& model,
        const Rendering::RenderMaterial& material );                                    // Append pine model matrix and material payload to instance buffer
    void DrawPineBatchModel( const Math::Transformation::Matrix4& model,
                             float tintR = 1.0f,
                             float tintG = 1.0f,
                             float tintB = 1.0f,
                             float colorOverride = 0.0f );                              // Compatibility bridge for old tint/override callers
    void DrawPineBatchEnd( const RenderHelperContext& context );
    void DrawShadowDepthSphereBatchBegin( const RenderHelperContext& context,
                                          const Math::Transformation::Matrix4& view,
                                          const Math::Transformation::Matrix4& proj,
                                          const CinematicRenderConfig* cinematic = nullptr );
    void DrawShadowDepthSphereBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthSphereBatchEnd( const RenderHelperContext& context );
    void DrawShadowDepthBoxBatchBegin( const RenderHelperContext& context,
                                       const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthBoxBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthBoxBatchEnd( const RenderHelperContext& context );
    void DrawShadowDepthConvexHullModel( const RenderHelperContext& context,
                                         const Math::CollisionDetection::ConvexHullShape& hull,
                                         const Math::Transformation::Matrix4& model,
                                         const Math::Transformation::Matrix4& view,
                                         const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthPineBatchBegin( const RenderHelperContext& context,
                                        const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthPineBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthPineBatchEnd( const RenderHelperContext& context );
    void ResetRenderResources(
        Rendering::IRenderResourceFactory* renderResources );                           // Invalidate cached backend-owned meshes and shaders
    void EnsureSphereMesh( const RenderHelperContext& context );                        // Create the shared sphere mesh before DXR BLAS
                                                                 // construction needs its vertex data.
    void EnsureShadowDepthPrimitiveResources(
        const RenderHelperContext& context );                                           // Prewarm primitive shadow meshes and the shared depth shader.

    // DXR reflection reuses the same sphere vertex buffer as raster rendering.
    // The backend asks for the instanced mesh handle so it can find the static
    // vertex buffer and build the sphere BLAS.
    uint32_t GetSphereInstMeshHandle() const
    {
        return m_state.sphereInstMesh;
    }

    // Vertex count is part of the BLAS geometry description. The raytracing
    // build needs triangle-count information even though it does not draw here.
    int GetSphereVertexCount() const
    {
        return m_state.sphereVertexCount;
    }
};
} // namespace Basics
} // namespace SkullbonezCore

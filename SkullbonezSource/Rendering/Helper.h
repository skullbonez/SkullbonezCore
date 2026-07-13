/*
File: SkullbonezSource/Rendering/Helper.h
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Summary:
  Helper.h collects legacy helper routines that bridge engine subsystems. As a
  public header, keep edits anchored on render submission and resource
  lifetime and on the glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  Instance buffer: CPU-built per-object payload uploaded so one mesh can draw
  many objects with different transforms/materials.

Invariants:
  - Helper-owned meshes and shaders are backend resources; the renderer owner
    must destroy the helper before the backend device is destroyed.
  - Primitive batch scopes borrow RenderHelperContext until destruction and
    flush their queued instances exactly once.

Related:
  - SkullbonezSource/Rendering/Helper.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once
#include "../Core/Config.h"


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
namespace Core
{
struct CinematicRenderConfig;
} // namespace Core
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
namespace Runtime
{
class RenderHelper;

struct RenderHelperContext
{
    // Lifetime: commands and diagnostics are borrowed only for the receiving
    // call. RenderHelper may remember the resource factory until its owner
    // destroys the helper for backend teardown/rebuild.
    Rendering::IRenderResourceFactory& renderResources;
    Rendering::IRenderCommandContext& renderCommands;
    Rendering::IRenderDiagnostics& renderDiagnostics;
    const Assets::AssetSystem& assets;
    const SkullbonezCore::Core::EngineConfig& config;
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
    Rendering::IRenderResourceFactory* renderResources = nullptr;                       // Backend factory borrowed while helper handles are live.
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
    enum class PrimitiveBatchKind
    {
        Sphere,
        Box,
        Pine,
        ShadowSphere,
        ShadowBox,
        ShadowPine
    };

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
    explicit RenderHelper( Rendering::IRenderResourceFactory* renderResources = nullptr );
    RenderHelper( const RenderHelper& ) = delete;
    RenderHelper& operator=( const RenderHelper& ) = delete;
    ~RenderHelper();

    class PrimitiveBatchScope
    {
      public:
        PrimitiveBatchScope( PrimitiveBatchScope&& other ) noexcept;
        PrimitiveBatchScope& operator=( PrimitiveBatchScope&& other ) noexcept;
        PrimitiveBatchScope( const PrimitiveBatchScope& ) = delete;
        PrimitiveBatchScope& operator=( const PrimitiveBatchScope& ) = delete;
        ~PrimitiveBatchScope();

        void DrawModel( const Math::Transformation::Matrix4& model, const Rendering::RenderMaterial& material );
        void DrawModel( const Math::Transformation::Matrix4& model,
                        float tintR = 1.0f,
                        float tintG = 1.0f,
                        float tintB = 1.0f,
                        float colorOverride = 0.0f );
        void DrawShadowModel( const Math::Transformation::Matrix4& model );

      private:
        friend class RenderHelper;

        PrimitiveBatchScope( RenderHelper& helper,
                             const RenderHelperContext& context,
                             PrimitiveBatchKind kind );                                 // Batch scopes borrow their context until destruction.
        void EndIfActive();

        RenderHelper* m_helper = nullptr;
        const RenderHelperContext* m_context = nullptr;
        PrimitiveBatchKind m_kind = PrimitiveBatchKind::Sphere;
        bool m_active = false;
    };

    void StateSetup();                                                                  // Extension point for helper-level setup after renderer init
    void SetClipPlane( float x, float y, float z, float w );
    const float* GetClipPlane() const;
    PrimitiveBatchScope BeginSphereBatch( const RenderHelperContext& context,
                                          const Math::Transformation::Matrix4& view,
                                          const Math::Transformation::Matrix4& proj,
                                          const float lightPos[4],
                                          bool isTransparent = false,
                                          const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                          const Rendering::ShadowFrameData* shadow = nullptr,
                                          float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginBoxBatch( const RenderHelperContext& context,
                                       const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj,
                                       const float lightPos[4],
                                       bool isTransparent = false,
                                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                       const Rendering::ShadowFrameData* shadow = nullptr,
                                       float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginPineBatch( const RenderHelperContext& context,
                                        const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj,
                                        const float lightPos[4],
                                        bool isTransparent = false,
                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                        const Rendering::ShadowFrameData* shadow = nullptr,
                                        float materialAlpha = 1.0f );
    PrimitiveBatchScope
    BeginShadowDepthSphereBatch( const RenderHelperContext& context,
                                 const Math::Transformation::Matrix4& view,
                                 const Math::Transformation::Matrix4& proj,
                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    PrimitiveBatchScope BeginShadowDepthBoxBatch( const RenderHelperContext& context,
                                                  const Math::Transformation::Matrix4& view,
                                                  const Math::Transformation::Matrix4& proj );
    PrimitiveBatchScope BeginShadowDepthPineBatch( const RenderHelperContext& context,
                                                   const Math::Transformation::Matrix4& view,
                                                   const Math::Transformation::Matrix4& proj );
    void DrawConvexHullModel( const RenderHelperContext& context,
                              const Math::CollisionDetection::ConvexHullShape& hull,
                              const Math::Transformation::Matrix4& model,
                              const Rendering::RenderMaterial& material,
                              const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const float lightPos[4],
                              bool isTransparent = false,
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                              const Rendering::ShadowFrameData* shadow = nullptr,
                              float materialAlpha = 1.0f );
    void DrawShadowDepthConvexHullModel( const RenderHelperContext& context,
                                         const Math::CollisionDetection::ConvexHullShape& hull,
                                         const Math::Transformation::Matrix4& model,
                                         const Math::Transformation::Matrix4& view,
                                         const Math::Transformation::Matrix4& proj );
    void EnsureSphereMesh( const RenderHelperContext& context );                        // Create the shared sphere mesh before DXR BLAS
                                                                 // construction needs its vertex data.
    void EnsureShadowDepthPrimitiveResources(
        const RenderHelperContext& context );                                           // Prewarm primitive shadow meshes and the shared depth shader.

  private:
    void BindRenderResourceFactory( Rendering::IRenderResourceFactory&
                                        renderResources );                              // Remember the backend factory that owns raw helper handles.
    void ReleaseOwnedRenderResources();                                                 // Destroy helper-owned backend handles before factory teardown.
    void DrawSphereBatchBegin( const RenderHelperContext& context,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const float lightPos[4],
                               bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
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
                            const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
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
    void DrawPineBatchBegin( const RenderHelperContext& context,
                             const Math::Transformation::Matrix4& view,
                             const Math::Transformation::Matrix4& proj,
                             const float lightPos[4],
                             bool isTransparent = false,
                             const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
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
                                          const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    void DrawShadowDepthSphereBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthSphereBatchEnd( const RenderHelperContext& context );
    void DrawShadowDepthBoxBatchBegin( const RenderHelperContext& context,
                                       const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthBoxBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthBoxBatchEnd( const RenderHelperContext& context );
    void DrawShadowDepthPineBatchBegin( const RenderHelperContext& context,
                                        const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthPineBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthPineBatchEnd( const RenderHelperContext& context );

  public:
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
} // namespace Runtime
} // namespace SkullbonezCore

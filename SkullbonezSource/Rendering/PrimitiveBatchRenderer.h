/*
File: SkullbonezSource/Rendering/PrimitiveBatchRenderer.h
Purpose:
  Owns backend resources and bounded instance batches for built-in primitives.

Summary:
  PrimitiveBatchRenderer builds and retains sphere, box, pine, and convex-hull
  GPU resources, then submits visible or shadow-depth batches through a typed
  per-frame rendering context.

Glossary:
  Instance buffer: CPU-built per-object payload uploaded so one mesh can draw
  many objects with different transforms/materials.

Invariants:
  - Builder-owned meshes and shaders are backend resources; RuntimeRenderer
    destroys this owner before the backend device is destroyed.
  - Primitive batch scopes borrow PrimitiveRenderContext until destruction and
    flush their queued instances exactly once.

Related:
  - SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp
  - SkullbonezSource/Rendering/PrimitiveMeshBuilder.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once
#include "../Core/Config.h"


#include "../Core/Common.h"
#include "DX12/ShaderDX12.h"
#include "DX12/MeshDX12.h"
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
class Dx12GeometryOwner;
class Dx12Diagnostics;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class Dx12GeometryOwner;
} // namespace Rendering
namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
}
} // namespace Math
namespace Rendering
{
class PrimitiveBatchRenderer;

struct PrimitiveRenderContext
{

    // Lifetime: commands and diagnostics are borrowed only for the receiving
    // call. PrimitiveBatchRenderer may remember the three concrete resource
    // owners until its owner destroys the renderer for backend teardown/rebuild.
    Rendering::Dx12ResourceBuilder& renderResources;
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12GeometryOwner& renderGeometry;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    const Assets::AssetSystem& assets;
    const SkullbonezCore::Core::EngineConfig& config;
    PrimitiveBatchRenderer& renderer;
};

struct PrimitiveMeshGeometryView
{
    uint32_t instancedMeshHandle = 0;
    int vertexCount = 0;
};

struct PrimitiveBatchRendererState
{
    static constexpr int INSTANCE_MATRIX_FLOATS = 16;
    static constexpr int INSTANCE_MATERIAL_FLOAT4_COUNT = 4;
    static constexpr int INSTANCE_MATERIAL_FLOATS = INSTANCE_MATERIAL_FLOAT4_COUNT * 4;
    static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_MATERIAL_FLOATS;

    // Invariant: mirrors ConvexHullShape::MAX_FACES/MAX_FACE_VERTICES without
    // including the physics hull header in this widely included render helper.
    static constexpr int HULL_MAX_TRIANGLE_VERTICES = 96 * ( 16 - 2 ) * 3;
    static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + 2 + INSTANCE_FLOATS;

    std::unique_ptr<Rendering::ShaderDX12> sphereShader;                                             // Shared lit_textured_instanced shader.
    std::unique_ptr<Rendering::ShaderDX12> shadowDepthShader;                                        // Shared instanced directional shadow depth shader.
    uint32_t sphereInstMesh = 0;                                                                     // Instanced mesh handle owned by the active geometry owner.
    int sphereVertexCount = 0;                                                                       // Per-sphere vertex count.
    std::vector<float> sphereInstanceData;                                                           // Queued sphere transforms/materials between batch begin/end.
    uint32_t lowPolySphereInstMesh = 0;                                                              // Faceted sphere mesh for low-poly cinematic styles.
    int lowPolySphereVertexCount = 0;                                                                // Per-low-poly-sphere vertex count.
    uint32_t activeSphereInstMesh = 0;                                                               // Mesh selected for the current sphere batch.
    int activeSphereVertexCount = 0;                                                                 // Vertex count selected for the current sphere batch.
    uint32_t boxInstMesh = 0;                                                                        // Instanced mesh handle for boxes.
    int boxVertexCount = 0;                                                                          // Per-box vertex count.
    std::vector<float> boxInstanceData;                                                              // Queued box transforms/materials between batch begin/end.
    uint32_t pineInstMesh = 0;                                                                       // Instanced mesh handle for low-poly pine foliage tiers.
    int pineVertexCount = 0;                                                                         // Per-pine-tier vertex count.
    std::vector<float> pineInstanceData;                                                             // Queued pine transforms/materials between batch begin/end.
    float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };                                               // Default: always pass.
    bool sphereBatchTransparent = false;
    bool boxBatchTransparent = false;
    bool pineBatchTransparent = false;
    bool sphereBatchReady = false;
    bool boxBatchReady = false;
    bool pineBatchReady = false;
    Rendering::Dx12ResourceBuilder* renderResources = nullptr;                                       // Backend factory borrowed while helper handles are live.
    Rendering::Dx12TextureOwner* renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    uint32_t materialTableTexture = 0;                                                               // Material defaults bound at shader slot t4.
    uint32_t convexHullDynamicVB = 0;                                                                // Dynamic vertex buffer used by immediate convex hull draws.
    std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> convexHullVertexData = {};
};

/* -- Primitive Batch Renderer
------------------------------------------------------------------------------------------------------------------------------------------

    Owner for shared primitive render resources, especially
    instanced sphere/box/pine batches used by normal rendering and shadow
    passes.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class PrimitiveBatchRenderer
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

    PrimitiveBatchRendererState m_state;                                                             // Owned primitive render cache and batch scratch.

    void EnsureSphereShader( const PrimitiveRenderContext& context );
    void EnsureShadowDepthShader( const PrimitiveRenderContext& context );
    void BuildSphereMesh( const PrimitiveRenderContext& context, int slices, int stacks );           // Generate UV sphere instanced mesh
    void BuildLowPolySphereMesh( const PrimitiveRenderContext& context, int slices,
                                 int stacks );                                                       // Generate faceted sphere instanced mesh
    void BuildBoxMesh( const PrimitiveRenderContext& context );                                      // Generate unit cube instanced mesh
    void BuildPineMesh( const PrimitiveRenderContext& context );                                     // Generate unit low-poly pine tier mesh

  public:
    explicit PrimitiveBatchRenderer( Rendering::Dx12ResourceBuilder* renderResources = nullptr,
                                     Rendering::Dx12TextureOwner* renderTextures = nullptr,
                                     Rendering::Dx12GeometryOwner* renderGeometry = nullptr );
    PrimitiveBatchRenderer( const PrimitiveBatchRenderer& ) = delete;
    PrimitiveBatchRenderer& operator=( const PrimitiveBatchRenderer& ) = delete;
    ~PrimitiveBatchRenderer();

    class PrimitiveBatchScope
    {
      public:
        PrimitiveBatchScope( PrimitiveBatchScope&& other ) noexcept;
        PrimitiveBatchScope& operator=( PrimitiveBatchScope&& other ) noexcept;
        PrimitiveBatchScope( const PrimitiveBatchScope& ) = delete;
        PrimitiveBatchScope& operator=( const PrimitiveBatchScope& ) = delete;
        ~PrimitiveBatchScope();

        void DrawModel( const Math::Transformation::Matrix4& model, const Rendering::RenderMaterial& material );
        void DrawShadowModel( const Math::Transformation::Matrix4& model );

      private:
        friend class PrimitiveBatchRenderer;

        PrimitiveBatchScope( PrimitiveBatchRenderer& renderer, const PrimitiveRenderContext& context,
                             PrimitiveBatchKind kind );                                              // Batch scopes borrow their context until destruction.
        void EndIfActive();

        PrimitiveBatchRenderer* m_renderer = nullptr;
        const PrimitiveRenderContext* m_context = nullptr;
        PrimitiveBatchKind m_kind = PrimitiveBatchKind::Sphere;
        bool m_active = false;
    };

    void SetClipPlane( float x, float y, float z, float w );
    const float* GetClipPlane() const;
    PrimitiveBatchScope BeginSphereBatch( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                          const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                          bool isTransparent = false,
                                          const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                          const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginBoxBatch( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                       bool isTransparent = false,
                                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                       const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginPineBatch( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                        bool isTransparent = false,
                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                        const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope
    BeginShadowDepthSphereBatch( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                 const Math::Transformation::Matrix4& proj,
                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    PrimitiveBatchScope BeginShadowDepthBoxBatch( const PrimitiveRenderContext& context,
                                                  const Math::Transformation::Matrix4& view,
                                                  const Math::Transformation::Matrix4& proj );
    PrimitiveBatchScope BeginShadowDepthPineBatch( const PrimitiveRenderContext& context,
                                                   const Math::Transformation::Matrix4& view,
                                                   const Math::Transformation::Matrix4& proj );
    void DrawConvexHullModel( const PrimitiveRenderContext& context, const Math::CollisionDetection::ConvexHullShape& hull,
                              const Math::Transformation::Matrix4& model, const Rendering::RenderMaterial& material,
                              const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj,
                              const float lightPos[4], bool isTransparent = false,
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                              const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    void DrawShadowDepthConvexHullModel( const PrimitiveRenderContext& context,
                                         const Math::CollisionDetection::ConvexHullShape& hull,
                                         const Math::Transformation::Matrix4& model,
                                         const Math::Transformation::Matrix4& view,
                                         const Math::Transformation::Matrix4& proj );
    void EnsureSphereMesh( const PrimitiveRenderContext& context );                                  // Create the shared sphere mesh before DXR BLAS

    // construction needs its vertex data.
    void EnsureShadowDepthPrimitiveResources( const PrimitiveRenderContext& context );               // Prewarm primitive shadow meshes and the shared depth shader.

  private:
    void BindRenderResourceOwners( Rendering::Dx12ResourceBuilder& renderResources,
                                   Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderGeometry );
    void ReleaseOwnedRenderResources();                                                              // Destroy renderer-owned backend handles before factory teardown.
    void DrawSphereBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4],
                               bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    void DrawSphereBatchModel( const Math::Transformation::Matrix4& model,
                               const Rendering::RenderMaterial& material );                          // Append model matrix and material payload to instance buffer
                               void DrawSphereBatchEnd( const PrimitiveRenderContext& context );
                               void DrawBoxBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
                               void DrawBoxBatchModel( const Math::Transformation::Matrix4& model,
                                                       const Rendering::RenderMaterial& material );  // Append box model matrix and material payload to instance buffer
                               void DrawBoxBatchEnd( const PrimitiveRenderContext& context );
                               void DrawPineBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
                               void DrawPineBatchModel( const Math::Transformation::Matrix4& model,
                                                        const Rendering::RenderMaterial& material ); // Append pine model matrix and material payload to instance buffer
                               void DrawPineBatchEnd( const PrimitiveRenderContext& context );
                               void DrawShadowDepthSphereBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    void DrawShadowDepthSphereBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthSphereBatchEnd( const PrimitiveRenderContext& context );
    void DrawShadowDepthBoxBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthBoxBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthBoxBatchEnd( const PrimitiveRenderContext& context );
    void DrawShadowDepthPineBatchBegin( const PrimitiveRenderContext& context, const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthPineBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthPineBatchEnd( const PrimitiveRenderContext& context );

  public:

    // DXR consumes a value view instead of reaching into builder-owned state.
    PrimitiveMeshGeometryView SphereGeometry() const
    {
        return { m_state.sphereInstMesh, m_state.sphereVertexCount };
    }
};
} // namespace Rendering
} // namespace SkullbonezCore

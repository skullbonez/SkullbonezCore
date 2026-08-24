/*
File: SkullbonezSource/Rendering/PrimitiveBatchRenderer.h
Purpose:
  Owns backend resources and bounded instance batches for built-in primitives.

Summary:
  PrimitiveBatchRenderer builds and retains sphere, box, pine, and convex-hull
  GPU resources, then submits visible or shadow-depth batches from focused
  lighting, shader-path, geometry, and command values.

Glossary:
  Instance buffer: CPU-built per-object payload uploaded so one mesh can draw
  many objects with different transforms/materials.

Invariants:
  - Builder-owned meshes and shaders are backend resources; RuntimeRenderer
    destroys this owner before the backend device is destroyed.
  - Primitive batch scopes retain only their renderer and batch kind until
    destruction, then flush queued instances exactly once.
  - A scope can submit only while active and only through the visible/shadow
    method matching its kind; resource-owner identity is fixed for its epoch.

Related:
  - SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp
  - SkullbonezSource/Rendering/PrimitiveMeshBuilder.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once
#include "../Core/Config.h"


#include "../Core/Common.h"
#include "../Core/FatalError.h"
#include "DX12/ShaderDX12.h"
#include "DX12/MeshDX12.h"
#include "../Maths/Matrix4.h"
#include "RenderMaterial.h"
#include "Shadow.h"
#include "../Maths/Vector3.h"
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
struct CinematicRenderConfig;
} // namespace Core
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
struct PrimitiveBatchRendererTestAccess;

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

    std::unique_ptr<Rendering::ShaderDX12> sphereShader;                                                                // Shared lit_textured_instanced shader.
    std::unique_ptr<Rendering::ShaderDX12> shadowDepthShader;                                                           // Shared instanced directional shadow depth shader.
    uint32_t sphereInstMesh = 0;                                                                                        // Instanced mesh handle owned by the active geometry owner.
    int sphereVertexCount = 0;                                                                                          // Per-sphere vertex count.
    std::vector<float> sphereInstanceData;                                                                              // Queued sphere transforms/materials between batch begin/end.
    uint32_t lowPolySphereInstMesh = 0;                                                                                 // Faceted sphere mesh for low-poly cinematic styles.
    int lowPolySphereVertexCount = 0;                                                                                   // Per-low-poly-sphere vertex count.
    uint32_t activeSphereInstMesh = 0;                                                                                  // Mesh selected for the current sphere batch.
    int activeSphereVertexCount = 0;                                                                                    // Vertex count selected for the current sphere batch.
    uint32_t boxInstMesh = 0;                                                                                           // Instanced mesh handle for boxes.
    int boxVertexCount = 0;                                                                                             // Per-box vertex count.
    std::vector<float> boxInstanceData;                                                                                 // Queued box transforms/materials between batch begin/end.
    uint32_t pineInstMesh = 0;                                                                                          // Instanced mesh handle for low-poly pine foliage tiers.
    int pineVertexCount = 0;                                                                                            // Per-pine-tier vertex count.
    std::vector<float> pineInstanceData;                                                                                // Queued pine transforms/materials between batch begin/end.
    float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };                                                                  // Default: always pass.
    bool sphereBatchTransparent = false;
    bool boxBatchTransparent = false;
    bool pineBatchTransparent = false;
    bool sphereBatchReady = false;
    bool boxBatchReady = false;
    bool pineBatchReady = false;
    bool convexHullBatchReady = false;
    bool convexHullBatchTransparent = false;
    Rendering::Dx12ResourceBuilder* renderResources = nullptr;                                                          // Backend factory borrowed while helper handles are live.
    Rendering::Dx12TextureOwner* renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    uint32_t materialTableTexture = 0;                                                                                  // Material defaults bound at shader slot t4.
    uint32_t convexHullDynamicVB = 0;                                                                                   // Dynamic vertex buffer used by immediate convex hull draws.
    std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> convexHullVertexData = {};
};

enum class PrimitiveBatchKind
{
    Sphere,
    Box,
    Pine,
    ShadowSphere,
    ShadowBox,
    ShadowPine
};

// Concept: a batch scope owns one draw-mode lease. Moving transfers the lease;
// closing or moving from it makes every later draw a fatal lifecycle error.
class PrimitiveBatchScopeLifecycle
{
  public:
    PrimitiveBatchScopeLifecycle() = default;
    PrimitiveBatchScopeLifecycle( const void* renderer, PrimitiveBatchKind kind ) noexcept
        : m_renderer( renderer ), m_kind( kind ), m_active( true )
    {
    }
    PrimitiveBatchScopeLifecycle( PrimitiveBatchScopeLifecycle&& other ) noexcept
        : m_renderer( other.m_renderer ), m_kind( other.m_kind ), m_active( other.m_active )
    {
        other.m_active = false;
    }
    PrimitiveBatchScopeLifecycle& operator=( PrimitiveBatchScopeLifecycle&& other ) noexcept
    {
        if ( this != &other )
        {
            m_renderer = other.m_renderer;
            m_kind = other.m_kind;
            m_active = other.m_active;
            other.m_active = false;
        }

        return *this;
    }
    PrimitiveBatchScopeLifecycle( const PrimitiveBatchScopeLifecycle& ) = delete;
    PrimitiveBatchScopeLifecycle& operator=( const PrimitiveBatchScopeLifecycle& ) = delete;

    void RequireVisible() const
    {
        Require( false );
    }
    void RequireShadow() const
    {
        Require( true );
    }
    void Close() noexcept
    {
        m_active = false;
    }
    bool Active() const noexcept
    {
        return m_active;
    }
    PrimitiveBatchKind Kind() const noexcept
    {
        return m_kind;
    }

  private:
    void Require( bool shadowDraw ) const
    {
        const bool visibleKind = m_kind == PrimitiveBatchKind::Sphere || m_kind == PrimitiveBatchKind::Box ||
                                 m_kind == PrimitiveBatchKind::Pine;
        const bool shadowKind = m_kind == PrimitiveBatchKind::ShadowSphere || m_kind == PrimitiveBatchKind::ShadowBox ||
                                m_kind == PrimitiveBatchKind::ShadowPine;

        if ( !m_active || !m_renderer || ( shadowDraw ? !shadowKind : !visibleKind ) )
        {
            SB_FATAL( "Rendering/PrimitiveBatchScope",
                      "Primitive batch scope misuse. active=%d renderer=%d kind=%u requested=%s",
                      m_active ? 1 : 0, m_renderer ? 1 : 0, static_cast<unsigned int>( m_kind ),
                      shadowDraw ? "shadow" : "visible" );
        }
    }

    const void* m_renderer = nullptr;
    PrimitiveBatchKind m_kind = PrimitiveBatchKind::Sphere;
    bool m_active = false;
};

// Invariant: the first complete backend tuple becomes the identity for this
// renderer epoch; subsequent binds may repeat it but cannot replace one slot.
class PrimitiveResourceOwnerIdentity
{
  public:
    void Bind( const void* resources, const void* textures, const void* geometry )
    {
        const bool resourcesMatch = !m_resources || m_resources == resources;
        const bool texturesMatch = !m_textures || m_textures == textures;
        const bool geometryMatch = !m_geometry || m_geometry == geometry;

        if ( !resourcesMatch || !texturesMatch || !geometryMatch )
        {
            SB_FATAL( "Rendering/PrimitiveBatchRenderer",
                      "Primitive resource owner identity changed during a live renderer epoch. resources=%d textures=%d "
                      "geometry=%d",
                      resourcesMatch ? 1 : 0, texturesMatch ? 1 : 0, geometryMatch ? 1 : 0 );
        }

        m_resources = resources;
        m_textures = textures;
        m_geometry = geometry;
    }

  private:
    const void* m_resources = nullptr;
    const void* m_textures = nullptr;
    const void* m_geometry = nullptr;
};

class PrimitiveBatchRenderer
{

  private:
    friend struct PrimitiveBatchRendererTestAccess;

    PrimitiveBatchRendererState m_state;                                                                                // Owned primitive render cache and batch scratch.
    PrimitiveResourceOwnerIdentity m_resourceOwnerIdentity;                                                             // Stable backend tuple for this renderer epoch.

    void EnsureSphereShader( const char* shaderBaseName, const SkullbonezCore::Core::OrdinaryRenderConfig& lighting );
    void EnsureShadowDepthShader( const char* shaderBaseName );
    bool BindShader( Rendering::ShaderDX12& shader, const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                     const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection,
                     const float lightPosition[4], const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                     const Rendering::ShadowFrameData* shadow, int primitiveShape, bool receiveShadows,
                     float materialAlpha );
    void BuildSphereMesh( int slices, int stacks );                                                                     // Generate UV sphere instanced mesh
    void BuildLowPolySphereMesh( int slices, int stacks );                                                              // Generate faceted sphere instanced mesh
    void BuildBoxMesh();                                                                                                // Generate unit cube instanced mesh
    void BuildPineMesh();                                                                                               // Generate unit low-poly pine tier mesh

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
        friend struct PrimitiveBatchRendererTestAccess;

        PrimitiveBatchScope( PrimitiveBatchRenderer& renderer, PrimitiveBatchKind kind );
        void EndIfActive();

        PrimitiveBatchRenderer* m_renderer = nullptr;
        PrimitiveBatchScopeLifecycle m_lifecycle;
    };

    void SetClipPlane( float x, float y, float z, float w );
    const float* GetClipPlane() const;
    PrimitiveBatchScope BeginSphereBatch( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                                          const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                          const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                          bool isTransparent = false,
                                          const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                          const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginBoxBatch( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                                       const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                       bool isTransparent = false,
                                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                       const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope BeginPineBatch( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                                        const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj, const float lightPos[4],
                                        bool isTransparent = false,
                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                                        const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    PrimitiveBatchScope
    BeginShadowDepthSphereBatch( const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                 const Math::Transformation::Matrix4& proj,
                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    PrimitiveBatchScope BeginShadowDepthBoxBatch( const char* shaderBaseName,
                                                  const Math::Transformation::Matrix4& view,
                                                  const Math::Transformation::Matrix4& proj );
    PrimitiveBatchScope BeginShadowDepthPineBatch( const char* shaderBaseName,
                                                   const Math::Transformation::Matrix4& view,
                                                   const Math::Transformation::Matrix4& proj );
    void BeginConvexHullBatch( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting, const char* shaderBaseName,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                               const Rendering::ShadowFrameData* shadow, float materialAlpha );
    void DrawConvexHullModel( const Math::CollisionDetection::ConvexHullShape& hull,
                              const Math::Transformation::Matrix4& model,
                              const Rendering::RenderMaterial& material );
    void EndConvexHullBatch();
    void DrawShadowDepthConvexHullModel( const char* shaderBaseName,
                                         const Math::CollisionDetection::ConvexHullShape& hull,
                                         const Math::Transformation::Matrix4& model,
                                         const Math::Transformation::Matrix4& view,
                                         const Math::Transformation::Matrix4& proj );
    void EnsureSphereMesh();                                                                                           // Create the shared sphere mesh before DXR BLAS

    // construction needs its vertex data.
    void EnsureShadowDepthPrimitiveResources( const char* shaderBaseName );                                             // Prewarm primitive shadow meshes and the shared depth shader.

  private:
    void BindRenderResourceOwners( Rendering::Dx12ResourceBuilder& renderResources,
                                   Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderGeometry );
    void ReleaseOwnedRenderResources();                                                                                 // Destroy renderer-owned backend handles before factory teardown.
    void DrawSphereBatchBegin( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting, const char* shaderBaseName,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4],
                               bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    void DrawSphereBatchModel( const Math::Transformation::Matrix4& model, const Rendering::RenderMaterial& material ); // Append model matrix and material payload to instance buffer
                               void DrawSphereBatchEnd();
                               void DrawBoxBatchBegin( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                               const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
                               void DrawBoxBatchModel( const Math::Transformation::Matrix4& model,
                                                       const Rendering::RenderMaterial& material );                     // Append box model matrix and material payload to instance buffer
                               void DrawBoxBatchEnd();
                               void DrawPineBatchBegin( const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                               const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj, const float lightPos[4], bool isTransparent = false,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                               const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
                               void DrawPineBatchModel( const Math::Transformation::Matrix4& model,
                                                        const Rendering::RenderMaterial& material );                    // Append pine model matrix and material payload to instance buffer
                               void DrawPineBatchEnd();
                               void DrawShadowDepthSphereBatchBegin( const char* shaderBaseName,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr );
    void DrawShadowDepthSphereBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthSphereBatchEnd();
    void DrawShadowDepthBoxBatchBegin( const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                       const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthBoxBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthBoxBatchEnd();
    void DrawShadowDepthPineBatchBegin( const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& proj );
    void DrawShadowDepthPineBatchModel( const Math::Transformation::Matrix4& model );
    void DrawShadowDepthPineBatchEnd();

  public:

    // DXR consumes a value view instead of reaching into builder-owned state.
    PrimitiveMeshGeometryView SphereGeometry() const
    {
        return { m_state.sphereInstMesh, m_state.sphereVertexCount };
    }
};
} // namespace Rendering
} // namespace SkullbonezCore

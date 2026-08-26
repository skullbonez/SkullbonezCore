/*
File: SkullbonezSource/World/Terrain.h
Purpose:
  Stores terrain mesh, height queries, and terrain rendering resources.

Summary:
  Terrain factories validate RAW height-map dimensions and derive pixel, post,
  and quad counts before constructing the shared authored grid. Cached height,
  normal, and plane queries then let Physics build contact rows without
  reconstructing triangle planes each tick.

Glossary:
  RAW (Raw Heightmap): Uncompressed terrain height byte data used to author the
  shared render/collision post grid.
  Terrain post: Authoritative height sample shared by terrain rendering and
  collision queries.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Render, shadow, DXR, and collision consumers use the same post positions;
    there is no render-only height interpolation surface.
  - Height-map construction accepts only positive, exactly divisible dimensions
    that produce at least one quad, and every consumer uses the same checked
    pixel/post/quad counts.
  - Terrain color and shadow draws consume caller-owned raster buckets; terrain
    does not infer fixed-function state from renderer history.
  - Render-resource release preserves rebuild borrows; reset validates the full
    config/asset/builder set, and color submission requires a clip plane.

Related:
  - SkullbonezSource/World/Terrain.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/SbResult.h"
#include "../Core/FatalError.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/GeometricMath.h"
#include "../Physics/PhysicsTerrainView.h"
#include <cstddef>
#include <memory>
#include <cstdint>
#include <vector>


namespace SkullbonezCore
{
namespace Core
{
class Profiler;
class SbDiagnosticStore;
} // namespace Core
namespace Assets
{
class AssetSystem;
}

namespace Rendering
{
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class MeshDX12;
struct PassRasterStateBucket;
class ShaderDX12;
struct ShadowFrameData;
} // namespace Rendering

namespace Geometry
{
// Lifetime: terrain preserves its rebuild borrows when GPU resources are
// released; only backend rebinding replaces this complete identity tuple.
class TerrainRenderRebuildLease
{
  public:
    void Bind( const void* config, const void* assets, const void* resources ) noexcept
    {
        m_config = config;
        m_assets = assets;
        m_resources = resources;
    }
    bool Complete() const noexcept
    {
        return m_config && m_assets && m_resources;
    }
    void PreserveAcrossResourceRelease() const noexcept
    {
        // Intentional no-op: release destroys GPU objects, not the stable
        // rebuild-service lease used by the subsequent reset.
    }
    void Require( const char* operation ) const
    {
        if ( !Complete() )
        {
            SB_FATAL( "World/Terrain",
                      "Terrain render-resource operation requires complete backend-epoch bindings. operation=%s config=%d "
                      "assets=%d resources=%d",
                      operation ? operation : "unknown", m_config ? 1 : 0, m_assets ? 1 : 0, m_resources ? 1 : 0 );
        }
    }

  private:
    const void* m_config = nullptr;
    const void* m_assets = nullptr;
    const void* m_resources = nullptr;
};

struct TerrainRenderLifecycleTestAccess;

class Terrain
{
    struct ValidatedHeightMapTag
    {
    };

  public:
    static constexpr float FLAT_SLOPE_EXTENT = 1000.0f; // XZ extent of the analytic flat slope play area

    static SkullbonezCore::Core::SbResult
    TryCreateFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* fileName, int mapSize,
                            int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                            Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& resources,
                            std::unique_ptr<Terrain>& outTerrain ); // recoverable factory for external RAW height-map input.
    static SkullbonezCore::Core::SbResult
    TryCreatePhysicsFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* fileName, int mapSize,
                                   int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                                   std::unique_ptr<Terrain>& outTerrain ); // CPU-domain load path with no renderer double.

    // Invariant: only the factories can name ValidatedHeightMapTag. Height-map
    // construction therefore receives positive, divisible dimensions and one
    // checked set of pixel/post/quad counts before any field is initialized.
    Terrain( ValidatedHeightMapTag, const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem* assets,
             Rendering::Dx12ResourceBuilder* resources, int mapSize, int stepSize, int textureWrap, std::size_t pixelCount,
             int postsPerSide, std::size_t postCount, std::size_t quadCount );
    Terrain(
        float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
        Assets::AssetSystem& assets,
        Rendering::Dx12ResourceBuilder& resources ); // Flat analytic slope constructor: y = slopeBaseY + slopeX*x + slopeZ*z
    Terrain( float slopeBaseY, float slopeX, float slopeZ,
             const SkullbonezCore::Core::EngineConfig& config ); // Physics-only analytic slope; owns no GPU resources.
    ~Terrain();

    void Render( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection,
                 Rendering::Dx12TextureOwner& textures, const float* lightPosition, const float* clipPlane,
                 const Rendering::PassRasterStateBucket& rasterState,
                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                 const Rendering::ShadowFrameData* shadow = nullptr,
                 const Rendering::ShadowFrameData* detailShadow =
                     nullptr ); // Terrain color pass with optional broad and tight shadow inputs.
    void RenderShadowDepth( Core::Profiler* profiler, const Math::Transformation::Matrix4& lightView,
                            const Math::Transformation::Matrix4& lightProjection,
                            const Rendering::PassRasterStateBucket& rasterState,
                            const SkullbonezCore::Core::CinematicRenderConfig* cinematic =
                                nullptr ); // Depth-only terrain caster pass for directional shadows.
    void
    BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                        Rendering::Dx12ResourceBuilder& resources ); // Borrow rebuild-only services for terrain resources.
    void EnsureRenderResources( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                                Rendering::Dx12ResourceBuilder& resources ); // Lazily rebuilds missing backend resources.
    void EnsureShadowDepthResources();                                       // Prewarms the terrain shadow caster shader.
    void ResetRenderResources();   // Rebuild backend-specific mesh/shader resources after a device reset or resize
    void ReleaseRenderResources(); // Releases backend-specific mesh/shader resources without rebuilding.

    // Borrowed mesh pointer for DXR BLAS construction; Terrain retains ownership.
    Rendering::MeshDX12* GetMesh() const
    {
#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
        return nullptr;
#else
        return m_terrainMesh.get();
#endif
    }
    std::vector<float> BuildRenderVertexData() const; // CPU value projection shared by DX12 upload and geometry tests.

    // Cached maximum Y height supports cheap airborne early-outs.
    float GetMaxHeight() const
    {
        return m_maxTerrainHeight;
    }

    // Cached minimum Y height supports cheap below-terrain checks.
    float GetMinHeight() const
    {
        return m_minTerrainHeight;
    }
    XZBounds GetXZBounds();
    Triangle LocatePolygon( float xPosition, float zPosition ); // Orthographic X/Z lookup; see

    // http://www.simoneschbach.com/images/FindingArbitraryPolygon.gif
    bool IsInBounds( float xPosition, float zPosition ); // World X/Z coordinates inside the terrain collision domain.

    // Returns the lowest finite float outside the collision domain so camera
    // and presentation callers can treat missing terrain as no vertical support.
    // isFluidMin applies only to valid terrain samples.
    float GetTerrainHeightAt( float xPosition, float zPosition, bool isFluidMin = false );

    // Surface normal used by contact rows and slope alignment.
    void GetTerrainHeightAndNormalAt(
        float xPosition, float zPosition, float& outHeight,
        Math::Vector::Vector3& outNormal ); // Combined lookup: one cached-cell query instead of two.
    void GetTerrainHeightAndPlaneAt( float xPosition, float zPosition, float& outHeight,
                                     Plane& outPlane ); // Physics fast path: direct cached plane plus height lookup.
    Physics::PhysicsTerrainView
    PhysicsView() const noexcept; // Detached scene-lifetime collision view registered with Physics.

  private:
    friend struct TerrainRenderLifecycleTestAccess;

    enum class RequiredRenderResourceFailure
    {
        None,
        Mesh,
        Shader
    };

    static RequiredRenderResourceFailure
    TryPublishRenderReadyCandidate( std::unique_ptr<Terrain>& outTerrain, std::unique_ptr<Terrain>& candidate,
                                    bool meshReady, bool shaderReady ) noexcept;

    std::uint32_t displayListReference; // Legacy display-list token retained for serialized state.

    // Why: the standalone CPU test executable validates the authoritative
    // height/collision values without linking native renderer object code.
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
    std::unique_ptr<Rendering::MeshDX12>
        m_terrainMesh; // Renderer-owned terrain vertex/index storage consumed by the active shader.
    std::unique_ptr<Rendering::ShaderDX12> m_terrainShader; // Lit+textured m_shader program
    std::unique_ptr<Rendering::ShaderDX12> m_shadowDepthShader;
    // Lifetime: scene construction prepares this CPU upload image once. Device
    // loss may recreate GPU resources during Render, but it must only read this
    // retained storage and never rebuild a heap-backed vector in that phase.
    std::vector<float> m_renderVertexData;
#endif
    std::vector<TerrainPost> m_postData;     // Shared render/collision heightfield posts.
    std::vector<std::uint8_t> m_terrainData; // Cold RAW bytes released after post construction.
    std::vector<Physics::PhysicsTerrainCell> m_cachedCollisionData;
    int m_mapSize;                                      // Size of map (pixels length)
    int m_stepSize;                                     // Steps size between posts
    int m_textureWrap;                                  // Number of times to wrap texture over m_terrain
    int m_postsPerSide;                                 // Terrain postings per side of m_terrain
    std::size_t m_pixelCount;                           // Checked RAW byte count: mapSize squared.
    std::size_t m_postCount;                            // Checked authoritative post count: postsPerSide squared.
    std::size_t m_quadCount;                            // Checked collision/render cell count: (postsPerSide - 1) squared.
    int m_terrainSizeWorldCoords;                       // size per side of m_terrain in world coordinates
    float m_maxTerrainHeight;                           // Maximum Y height across all posts (computed once at build time)
    float m_minTerrainHeight;                           // Minimum Y height across all posts (computed once at build time)
    const SkullbonezCore::Core::EngineConfig* m_config; // Borrowed runtime config for terrain scale/render settings.
    Assets::AssetSystem* m_assets;                      // Borrowed asset registry for terrain shaders.
    Rendering::Dx12ResourceBuilder* m_resources;        // Borrowed cold builder for terrain mesh/shaders.
    TerrainRenderRebuildLease m_renderLease;            // Authoritative preserved rebuild-borrow tuple.
    void RequireRenderBindings( const char* operation ) const;
    static void RequireClipPlane( const float* clipPlane )
    {
        if ( !clipPlane )
        {
            SB_FATAL( "World/Terrain", "Terrain color pass requires a clip plane before draw submission." );
        }
    }

    // Flat slope mode
    bool m_isFlatSlope;
    float m_slopeBaseY;
    float m_slopeX;
    float m_slopeZ;
    Plane m_flatSlopePlane;
    Math::Vector::Vector3 m_flatSlopeNormal;

    Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
             Assets::AssetSystem* assets,
             Rendering::Dx12ResourceBuilder* resources ); // Shared analytic-slope construction shell.

    static SkullbonezCore::Core::SbResult
    TryValidateHeightMapDimensions( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, int mapSize, int stepSize,
                                    std::size_t& outPixelCount, int& outPostsPerSide, std::size_t& outPostCount,
                                    std::size_t& outQuadCount ); // recoverable boundary before tagged construction.

    SkullbonezCore::Core::SbResult
    LoadTerrainData( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                     const char* fileName ); // Cold RAW byte load used to construct authoritative posts.
    const SkullbonezCore::Core::EngineConfig&
    Config() const;                 // Runtime config must be bound before terrain queries or rebuilds.
    void InitialiseTerrainShader(); // Lit terrain shader setup for the active backend.
    void BuildTerrain();            // Physics-authoritative terrain posts are rebuilt from raw height data.
    void BuildCollisionCache();     // Precomputes per-quad triangle planes + normals for physics queries
    void QueryCollisionData( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal,
                             Plane* outPlane );
    void QueryCollisionDataUnchecked( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal,
                                      Plane* outPlane );
    void TranslatePostings();  // Centers authored posts into world space.
    void GenerateNormals();    // Post normals are shared by lighting and terrain contacts.
    void BuildMesh();          // Builds renderer geometry from the collision-authoritative posts.
    void BuildFlatSlopeMesh(); // Analytic flat slope scenes bypass RAW height data but still need vertex storage.
    int GetPixelHeightAt( int worldXCoordinate, int worldZCoordinate ); // RAW pixel height before terrain post translation.
};
} // namespace Geometry
} // namespace SkullbonezCore

/*
File: SkullbonezSource/World/Terrain.h
Purpose:
  Stores terrain mesh, height queries, and terrain rendering resources.

Summary:
  Terrain builds one authored post grid from RAW height data and shares it with
  render and collision consumers. Cached height, normal, and plane queries let
  Physics build contact rows without reconstructing triangle planes each tick.

Glossary:
  RAW (Raw Heightmap): Uncompressed terrain height byte data used to author the
  shared render/collision post grid.
  Terrain post: Authoritative height sample shared by terrain rendering and
  collision queries.
  VBO (Vertex Buffer Object): Legacy engine term for renderer-owned terrain
  vertex/index storage; the DX12 path backs it through MeshDX12.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Render, shadow, DXR, and collision consumers use the same post positions;
    there is no render-only height interpolation surface.
  - Terrain color and shadow draws consume caller-owned raster buckets; terrain
    does not infer fixed-function state from renderer history.

Related:
  - SkullbonezSource/World/Terrain.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/GeometricMath.h"
#include "../Physics/PhysicsTerrainView.h"
#include "../Rendering/RenderCommandTypes.h"
#include "../Rendering/Shadow.h"
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
class MeshDX12;
class ShaderDX12;
} // namespace Rendering

namespace Geometry
{

class Terrain
{

  public:
    static constexpr float FLAT_SLOPE_EXTENT = 1000.0f;                                               // XZ extent of the analytic flat slope play area
    struct PhysicsOnlyHeightMapTag
    {
    };

    static SkullbonezCore::Core::SbResult
    TryCreateFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* fileName, int mapSize,
                            int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                            Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& resources,
                            std::unique_ptr<Terrain>& outTerrain );                                   // Lane R factory for external RAW height-map input.
    static SkullbonezCore::Core::SbResult
    TryCreatePhysicsFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* fileName, int mapSize,
                                   int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                                   std::unique_ptr<Terrain>& outTerrain );                            // CPU-domain load path with no renderer double.
    Terrain( int mapSize, int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
             Assets::AssetSystem& assets,
             Rendering::Dx12ResourceBuilder& resources );                                             // Construction shell used by TryCreateFromHeightMap; step

    // size feeds both pixels and physics posts.
    Terrain( PhysicsOnlyHeightMapTag, int mapSize, int stepSize, int textureWrap,
             const SkullbonezCore::Core::EngineConfig& config );                                      // CPU-only shell used by the value-producing load path.
    Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
             Assets::AssetSystem& assets,
             Rendering::Dx12ResourceBuilder& resources );                                             // Flat analytic slope constructor: y = slopeBaseY + slopeX*x + slopeZ*z
    Terrain( float slopeBaseY, float slopeX, float slopeZ,
             const SkullbonezCore::Core::EngineConfig& config );                                      // Physics-only analytic slope; owns no GPU resources.
    ~Terrain();

    void Render( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection,
                 Rendering::Dx12TextureOwner& textures, const float* lightPosition, const float* clipPlane,
                 const Rendering::PassRasterStateBucket& rasterState,
                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr,
                 const Rendering::ShadowFrameData* shadow = nullptr,
                 const Rendering::ShadowFrameData* detailShadow = nullptr );                          // Terrain color pass with optional broad and tight shadow inputs.
    void RenderShadowDepth( Core::Profiler* profiler, const Math::Transformation::Matrix4& lightView,
                            const Math::Transformation::Matrix4& lightProjection,
                            const Rendering::PassRasterStateBucket& rasterState,
                            const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr ); // Depth-only terrain caster pass for directional shadows.
    void
    BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                        Rendering::Dx12ResourceBuilder& resources );                                  // Borrow rebuild-only services for terrain resources.
    void EnsureRenderResources( const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                                Rendering::Dx12ResourceBuilder& resources );                          // Lazily rebuilds missing backend resources.
    void EnsureShadowDepthResources();                                                                // Prewarms the terrain shadow caster shader.
    void ResetRenderResources();                                                                      // Rebuild backend-specific mesh/shader resources after a device reset or resize
    void ReleaseRenderResources();                                                                    // Releases backend-specific mesh/shader resources without rebuilding.

    // Borrowed mesh pointer for DXR BLAS construction; Terrain retains ownership.
    Rendering::MeshDX12* GetMesh() const
    {
#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
        return nullptr;
#else
        return m_terrainMesh.get();
#endif
    }
    std::vector<float> BuildRenderVertexData() const;                                                 // CPU value projection shared by DX12 upload and geometry tests.

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
    Triangle LocatePolygon( float xPosition, float zPosition );                                       // Orthographic X/Z lookup; see

    // http://www.simoneschbach.com/images/FindingArbitraryPolygon.gif
    bool IsInBounds( float xPosition, float zPosition );                                              // World X/Z coordinates inside the terrain collision domain.
    float GetTerrainHeightAt( float xPosition, float zPosition,
                              bool isFluidMin = false );                                              // Height sample; isFluidMin asks water tests for the lowest terrain support.

    // Surface normal used by contact rows and slope alignment.
    void GetTerrainHeightAndNormalAt( float xPosition, float zPosition, float& outHeight,
                                      Math::Vector::Vector3& outNormal );                             // Combined lookup: one cached-cell query instead of two.
    void GetTerrainHeightAndPlaneAt( float xPosition, float zPosition, float& outHeight,
                                     Plane& outPlane );                                               // Physics fast path: direct cached plane plus height lookup.
    Physics::PhysicsTerrainView PhysicsView() const noexcept;                                         // Detached scene-lifetime collision view registered with Physics.

  private:
    std::uint32_t displayListReference;                                                               // Legacy display-list token retained for serialized state.

    // Why: the standalone CPU test executable validates the authoritative
    // height/collision values without linking native renderer object code.
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
    std::unique_ptr<Rendering::MeshDX12>
        m_terrainMesh;                                                                                // Renderer-owned terrain vertex/index storage consumed by the active shader.
    std::unique_ptr<Rendering::ShaderDX12> m_terrainShader;                                           // Lit+textured m_shader program
    std::unique_ptr<Rendering::ShaderDX12> m_shadowDepthShader;
#endif
    std::vector<TerrainPost> m_postData;                                                              // Shared render/collision heightfield posts.
    std::vector<std::uint8_t> m_terrainData;                                                          // Cold RAW bytes released after post construction.
    std::vector<Physics::PhysicsTerrainCell> m_cachedCollisionData;
    int m_mapSize;                                                                                    // Size of map (pixels length)
    int m_stepSize;                                                                                   // Steps size between posts
    int m_textureWrap;                                                                                // Number of times to wrap texture over m_terrain
    int m_postsPerSide;                                                                               // Terrain postings per side of m_terrain
    int m_terrainSizeWorldCoords;                                                                     // size per side of m_terrain in world coordinates
    float m_maxTerrainHeight;                                                                         // Maximum Y height across all posts (computed once at build time)
    float m_minTerrainHeight;                                                                         // Minimum Y height across all posts (computed once at build time)
    const SkullbonezCore::Core::EngineConfig* m_config;                                               // Borrowed runtime config for terrain scale/render settings.
    Assets::AssetSystem* m_assets;                                                                    // Borrowed asset registry for terrain shaders.
    Rendering::Dx12ResourceBuilder* m_resources;                                                      // Borrowed cold builder for terrain mesh/shaders.

    // Flat slope mode
    bool m_isFlatSlope;
    float m_slopeBaseY;
    float m_slopeX;
    float m_slopeZ;
    Plane m_flatSlopePlane;
    Math::Vector::Vector3 m_flatSlopeNormal;

    Terrain( int mapSize, int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
             Assets::AssetSystem* assets,
             Rendering::Dx12ResourceBuilder* resources );                                             // Shared CPU/GPU height-map construction shell.
    Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
             Assets::AssetSystem* assets,
             Rendering::Dx12ResourceBuilder* resources );                                             // Shared analytic-slope construction shell.

    SkullbonezCore::Core::SbResult
    LoadTerrainData( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                     const char* fileName );                                                          // Cold RAW byte load used to construct authoritative posts.
    const SkullbonezCore::Core::EngineConfig& Config() const;                                         // Runtime config must be bound before terrain queries or rebuilds.
    void InitialiseTerrainShader();                                                                   // Lit terrain shader setup for the active backend.
    void BuildTerrain();                                                                              // Physics-authoritative terrain posts are rebuilt from raw height data.
    void BuildCollisionCache();                                                                       // Precomputes per-quad triangle planes + normals for physics queries
    void QueryCollisionData( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal,
                             Plane* outPlane );
    void QueryCollisionDataUnchecked( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal,
                                      Plane* outPlane );
    void TranslatePostings();                                                                         // Centers authored posts into world space.
    void GenerateNormals();                                                                           // Post normals are shared by lighting and terrain contacts.
    void BuildMesh();                                                                                 // Builds renderer geometry from the collision-authoritative posts.
    void BuildFlatSlopeMesh();                                                                        // Analytic flat slope scenes bypass RAW height data but still need vertex storage.
    int GetPixelHeightAt( int worldXCoordinate, int worldZCoordinate );                               // RAW pixel height before terrain post translation.
};
} // namespace Geometry
} // namespace SkullbonezCore

/*
File: SkullbonezSource/World/Terrain.h
Purpose:
  Stores terrain mesh, height queries, and terrain rendering resources.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  DXR (DirectX Raytracing): DirectX 12 feature used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  RAW (Raw Heightmap): Uncompressed terrain height byte data used to author
  coarse physics posts and denser render-only samples.
  Terrain post: Physics-authoritative height sample on the coarse collision
  grid.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  VBO (Vertex Buffer Object): Legacy engine term for renderer-owned terrain
  vertex/index storage; the DX12 path backs it through IMesh.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/World/Terrain.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "../Maths/Matrix4.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/GeometricMath.h"
#include "../Rendering/IMesh.h"
#include "../Rendering/IShader.h"
#include "../Rendering/Shadow.h"
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
class IRenderResourceFactory;
}

namespace Geometry
{
/* -- Terrain
----------------------------------------------------------------------------------------------------------------------------------------------------

    Represents a texturable terrain geometry that must be loaded from a .RAW file.  Also provides information to assist
with collision detection.

    Layman physics map:
      Terrain is both render mesh and collision surface. Physics code queries
      height, normal, and plane at an X/Z location, then builds contact rows
      against that surface. The cached collision data exists so the physics loop
      does not rebuild triangle planes every tick.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Terrain
{

  public:
    static constexpr float FLAT_SLOPE_EXTENT = 1000.0f;                    // XZ extent of the analytic flat slope play area

    static Basics::SbResult
    TryCreateFromHeightMap( const char* sFileName,
                            int iMapSize,
                            int iStepSize,
                            int iTextureWrap,
                            const Basics::EngineConfig& config,
                            Assets::AssetSystem& assets,
                            Rendering::IRenderResourceFactory& resources,
                            std::unique_ptr<Terrain>& outTerrain );        // Lane R factory for external RAW height-map input.
    Terrain( int iMapSize,
             int iStepSize,
             int iTextureWrap,
             const Basics::EngineConfig& config,
             Assets::AssetSystem& assets,
             Rendering::IRenderResourceFactory& resources );               // Construction shell used by TryCreateFromHeightMap; step
                                                             // size feeds both pixels and physics posts.
    Terrain( float slopeBaseY,
             float slopeX,
             float slopeZ,
             const Basics::EngineConfig& config,
             Assets::AssetSystem& assets,
             Rendering::IRenderResourceFactory&
                 resources );                                              // Flat analytic slope constructor: y = slopeBaseY + slopeX*x + slopeZ*z
    ~Terrain();

    void Render( const Math::Transformation::Matrix4& view,
                 const Math::Transformation::Matrix4& projection,
                 Rendering::IRenderCommandContext& commands,
                 const float* lightPosition,
                 const float* clipPlane,
                 const Basics::CinematicRenderConfig* cinematic = nullptr,
                 const Rendering::ShadowFrameData* shadow =
                     nullptr );                                            // Terrain color pass with optional cinematic and shadow inputs.
    void RenderShadowDepth( const Math::Transformation::Matrix4& lightView,
                            const Math::Transformation::Matrix4& lightProjection,
                            const Basics::CinematicRenderConfig* cinematic =
                                nullptr );                                 // Depth-only terrain caster pass for directional shadows.
    void BindRenderContexts(
        const Basics::EngineConfig& config,
        Assets::AssetSystem& assets,
        Rendering::IRenderResourceFactory& resources );                    // Borrow rebuild-only services for terrain resources.
    void
    EnsureRenderResources( const Basics::EngineConfig& config,
                           Assets::AssetSystem& assets,
                           Rendering::IRenderResourceFactory& resources ); // Lazily rebuilds missing backend resources.
    void EnsureShadowDepthResources();                                     // Prewarms the terrain shadow caster shader.
    void ResetRenderResources();                                           // Rebuild backend-specific mesh/shader resources after a device reset or resize
    void ReleaseRenderResources();                                         // Releases backend-specific mesh/shader resources without rebuilding.
    // Borrowed mesh pointer for DXR BLAS construction; Terrain retains ownership.
    Rendering::IMesh* GetMesh() const
    {
        return m_terrainMesh.get();
    }
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
    Triangle LocatePolygon( float xPosition, float zPosition );            // Orthographic X/Z lookup; see
                                               // http://www.simoneschbach.com/images/FindingArbitraryPolygon.gif
    bool IsInBounds( float xPosition, float zPosition );                   // World X/Z coordinates inside the terrain collision domain.
    float GetTerrainHeightAt(
        float xPosition,
        float zPosition,
        bool isFluidMin = false );                                         // Height sample; isFluidMin asks water tests for the lowest terrain support.
    Math::Vector::Vector3
    GetTerrainNormalAt( float xPosition, float zPosition );                // Surface normal used by contact rows and slope alignment.
    void GetTerrainHeightAndNormalAt(
        float xPosition,
        float zPosition,
        float& outHeight,
        Math::Vector::Vector3& outNormal );                                // Combined lookup: one cached-cell query instead of two.
    void GetTerrainHeightAndPlaneAt( float xPosition,
                                     float zPosition,
                                     float& outHeight,
                                     Plane& outPlane );                    // Physics fast path: direct cached plane plus height lookup.

  private:
    struct CachedTriangleData
    {
        Plane m_plane;                                                     // Plane equation for one terrain triangle.
        Math::Vector::Vector3 m_normal;                                    // Cached upward normal for contact/friction directions.
    };

    struct CachedQuadData
    {
        CachedTriangleData m_triangleA;
        CachedTriangleData m_triangleB;
    };

    UINT displayListReference;                                             // Reference to the display list (retained for fallback)
    std::unique_ptr<Rendering::IMesh>
        m_terrainMesh;                                                     // Renderer-owned terrain vertex/index storage consumed by the active shader.
    std::unique_ptr<Rendering::IShader> m_terrainShader;                   // Lit+textured m_shader program
    std::unique_ptr<Rendering::IShader> m_shadowDepthShader;
    std::vector<TerrainPost> m_postData;                                   // Physics-authoritative coarse terrain posts
    std::vector<BYTE> m_terrainData;                                       // Raw m_height map byte data retained for render mesh rebuilds
    std::vector<CachedQuadData> m_cachedCollisionData;
    int m_mapSize;                                                         // Size of map (pixels length)
    int m_stepSize;                                                        // Steps size between posts
    int m_renderStepSize;                                                  // Render-only raw-pixel step size; physics keeps m_stepSize
    int m_renderPostsPerSide;                                              // Mesh density used for rendering, independent of physics posts.
    int m_textureWrap;                                                     // Number of times to wrap texture over m_terrain
    int m_postsPerSide;                                                    // Terrain postings per side of m_terrain
    int m_terrainSizeWorldCoords;                                          // size per side of m_terrain in world coordinates
    float m_maxTerrainHeight;                                              // Maximum Y height across all posts (computed once at build time)
    float m_minTerrainHeight;                                              // Minimum Y height across all posts (computed once at build time)
    const Basics::EngineConfig* m_config;                                  // Borrowed runtime config for terrain scale/render settings.
    Assets::AssetSystem* m_assets;                                         // Borrowed asset registry for terrain shaders.
    Rendering::IRenderResourceFactory* m_resources;                        // Borrowed active backend resource factory for terrain mesh/shaders.

    // Flat slope mode
    bool m_isFlatSlope;
    float m_slopeBaseY;
    float m_slopeX;
    float m_slopeZ;
    Plane m_flatSlopePlane;
    Math::Vector::Vector3 m_flatSlopeNormal;

    Basics::SbResult LoadTerrainData( const char* sFileName );             // RAW byte load retained for render mesh rebuilds.
    const Basics::EngineConfig& Config() const;                            // Runtime config must be bound before terrain queries or rebuilds.
    void InitialiseTerrainShader();                                        // Lit terrain shader setup for the active backend.
    void ConfigureRenderStepSize();                                        // Chooses a safe render-only terrain step size
    void BuildTerrain();                                                   // Physics-authoritative terrain posts are rebuilt from raw height data.
    void BuildCollisionCache();                                            // Precomputes per-quad triangle planes + normals for physics queries
    int GetQuadCacheIndex( float xPosition,
                           float zPosition,
                           bool& isTriangleA );                            // Maps world X/Z to cached terrain quad and triangle half.
    void QueryCollisionData( float xPosition,
                             float zPosition,
                             float& outHeight,
                             Math::Vector::Vector3* outNormal,
                             Plane* outPlane );
    void QueryCollisionDataUnchecked( float xPosition,
                                      float zPosition,
                                      float& outHeight,
                                      Math::Vector::Vector3* outNormal,
                                      Plane* outPlane );
    float SampleRenderHeightRaw( float rawX, float rawZ ) const;
    Math::Vector::Vector3 SampleRenderNormalRaw( float rawX, float rawZ ) const;
    TerrainPost BuildRenderPost( float rawX, float rawZ ) const;
    void TranslatePostings();                                              // Centers authored posts into world space.
    void GenerateNormals();                                                // Post normals are shared by lighting and terrain contacts.
    void BuildMesh();                                                      // Render-only height samples keep mesh density independent of physics posts.
    void BuildFlatSlopeMesh();                                             // Analytic flat slope scenes bypass RAW height data but still need vertex storage.
    int GetPixelHeightAt( int xCoord, int yCoord );                        // RAW pixel height before terrain post translation.
};
} // namespace Geometry
} // namespace SkullbonezCore

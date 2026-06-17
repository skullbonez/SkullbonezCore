/*
File: SkullbonezSource/SkullbonezTerrain.h
Purpose:
  Stores terrain mesh, height queries, and terrain rendering resources.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezTerrain.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezConfig.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezIMesh.h"
#include "SkullbonezIShader.h"
#include "SkullbonezShadow.h"
#include <vector>


namespace SkullbonezCore
{
namespace Geometry
{
/* -- Terrain ----------------------------------------------------------------------------------------------------------------------------------------------------

    Represents a texturable terrain geometry that must be loaded from a .RAW file.  Also provides information to assist with collision detection.

    Layman physics map:
      Terrain is both render mesh and collision surface. Physics code queries
      height, normal, and plane at an X/Z location, then builds contact rows
      against that surface. The cached collision data exists so the physics loop
      does not rebuild triangle planes every tick.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Terrain
{

  public:
    static constexpr float FLAT_SLOPE_EXTENT = 1000.0f; // XZ extent of the analytic flat slope play area

    Terrain( const char* sFileName, int iMapSize, int iStepSize, int iTextureWrap ); // Overloaded constructor: sFileName is path to .raw file, iMapSize is the size of map (pixels length), iStepSize is steps (pixel steps AND vertex steps), iTextureWrap is number of times to wrap texture
    Terrain( float slopeBaseY, float slopeX, float slopeZ );                         // Flat analytic slope constructor: y = slopeBaseY + slopeX*x + slopeZ*z
    ~Terrain();                                                                      // Default destructor

    void Render( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection, const float* lightPosition, const Basics::CinematicRenderConfig* cinematic = nullptr, const Rendering::ShadowFrameData* shadow = nullptr ); // Renders the terrain with shader
    void RenderShadowDepth( const Math::Transformation::Matrix4& lightView, const Math::Transformation::Matrix4& lightProjection, const Basics::CinematicRenderConfig* cinematic = nullptr );                                                            // Renders terrain into directional shadow depth
    void ResetRenderResources();                                                                                                                                                                                                                         // Rebuild backend-specific mesh/shader resources after a device reset or resize
    Rendering::IMesh* GetMesh() const
    {
        return m_terrainMesh.get();
    } // Returns the internal mesh (for DXR BLAS)
    float GetMaxHeight() const
    {
        return m_maxTerrainHeight;
    } // Returns the maximum Y height across all terrain posts (used for airborne early-out)
    float GetMinHeight() const
    {
        return m_minTerrainHeight;
    } // Returns the minimum Y height across all terrain posts
    XZBounds GetXZBounds();                                                                                                   // Returns the XZ bounds of the terrain
    Triangle LocatePolygon( float xPosition, float zPosition );                                                               // Locates the polygon surrounding the specified X and Z co-ordinates based on an orthagonal XZ projection.  Detailed math reference at http://www.simoneschbach.com/images/FindingArbitraryPolygon.gif
    bool IsInBounds( float xPosition, float zPosition );                                                                      // Returns a flag indicating if specified co-ordinates are inside the bounds of the terrain map
    float GetTerrainHeightAt( float xPosition, float zPosition, bool isFluidMin = false );                                    // Returns the height of the terrain at the specified coordinates
    Math::Vector::Vector3 GetTerrainNormalAt( float xPosition, float zPosition );                                             // Returns the surface normal of the terrain at the specified coordinates
    void GetTerrainHeightAndNormalAt( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3& outNormal ); // Combined lookup — single LocatePolygon call vs two separate calls
    void GetTerrainHeightAndPlaneAt( float xPosition, float zPosition, float& outHeight, Plane& outPlane );                   // Physics fast path — direct cached plane + height lookup

  private:
    struct CachedTriangleData
    {
        Plane m_plane;                  // Plane equation for one terrain triangle.
        Math::Vector::Vector3 m_normal; // Cached upward normal for contact/friction directions.
    };

    struct CachedQuadData
    {
        CachedTriangleData m_triangleA;
        CachedTriangleData m_triangleB;
    };

    UINT displayListReference;                           // Reference to the display list (retained for fallback)
    std::unique_ptr<Rendering::IMesh> m_terrainMesh;     // VBO mesh for m_shader rendering
    std::unique_ptr<Rendering::IShader> m_terrainShader; // Lit+textured m_shader program
    std::unique_ptr<Rendering::IShader> m_shadowDepthShader;
    std::vector<TerrainPost> m_postData; // Physics-authoritative coarse terrain posts
    std::vector<BYTE> m_terrainData;     // Raw m_height map byte data retained for render mesh rebuilds
    std::vector<CachedQuadData> m_cachedCollisionData;
    int m_mapSize;                // Size of map (pixels length)
    int m_stepSize;               // Steps size between posts
    int m_renderStepSize;         // Render-only raw-pixel step size; physics keeps m_stepSize
    int m_renderPostsPerSide;     // Render-only posts per side
    int m_textureWrap;            // Number of times to wrap texture over m_terrain
    int m_postsPerSide;           // Terrain postings per side of m_terrain
    int m_terrainSizeWorldCoords; // size per side of m_terrain in world coordinates
    float m_maxTerrainHeight;     // Maximum Y height across all posts (computed once at build time)
    float m_minTerrainHeight;     // Minimum Y height across all posts (computed once at build time)

    // Flat slope mode
    bool m_isFlatSlope;
    float m_slopeBaseY;
    float m_slopeX;
    float m_slopeZ;
    Plane m_flatSlopePlane;
    Math::Vector::Vector3 m_flatSlopeNormal;

    void LoadTerrainData( const char* sFileName ); // Loads terrain from .RAW file into terrainData member
    void InitialiseTerrainShader();                // Creates and configures lit terrain shader for active backend
    void ConfigureRenderStepSize();                // Chooses a safe render-only terrain step size
    void BuildTerrain();                           // Builds the terrain
    void BuildCollisionCache();                    // Precomputes per-quad triangle planes + normals for physics queries
    int GetQuadCacheIndex( float xPosition, float zPosition, bool& isTriangleA );
    void QueryCollisionData( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal, Plane* outPlane );
    void QueryCollisionDataUnchecked( float xPosition, float zPosition, float& outHeight, Math::Vector::Vector3* outNormal, Plane* outPlane );
    float SampleRenderHeightRaw( float rawX, float rawZ ) const;
    Math::Vector::Vector3 SampleRenderNormalRaw( float rawX, float rawZ ) const;
    TerrainPost BuildRenderPost( float rawX, float rawZ ) const;
    void TranslatePostings();                       // Translates terrain posts
    void GenerateNormals();                         // Generates normals for posts
    void BuildMesh();                               // Builds VBO mesh from render-only height samples
    void BuildFlatSlopeMesh();                      // Builds VBO mesh for analytic flat slope
    int GetPixelHeightAt( int xCoord, int yCoord ); // Returns the .raw height at the specified pixel coordinates
};
} // namespace Geometry
} // namespace SkullbonezCore

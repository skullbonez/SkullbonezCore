/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TERRAIN_H
#define SKULLBONEZ_TERRAIN_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezMesh.h"
#include "SkullbonezShader.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;

namespace SkullbonezCore
{
namespace Geometry
{
/* -- Terrain ----------------------------------------------------------------------------------------------------------------------------------------------------

    Represents a texturable terrain geometry that must be loaded from a .RAW file.  Also provides information to assist with collision detection.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Terrain
{

  public:
    Terrain( const char* sFileName, int iMapSize, int iStepSize, int iTextureWrap ); // Overloaded constructor: sFileName is path to .raw file, iMapSize is the size of map (pixels length), iStepSize is steps (pixel steps AND vertex steps), iTextureWrap is number of times to wrap texture
    ~Terrain( void );                                                                // Default destructor

    void Render( const Matrix4& view, const Matrix4& projection, const float* lightPosition ); // Renders the terrain with shader
    XZBounds GetXZBounds( void );                                                              // Returns the XZ bounds of the terrain
    Triangle LocatePolygon( float xPosition, float zPosition );                                // Locates the polygon surrounding the specified X and Z co-ordinates based on an orthagonal XZ projection.  Detailed math reference at http://www.simoneschbach.com/images/FindingArbitraryPolygon.gif
    bool IsInBounds( float xPosition, float zPosition );                                       // Returns a flag indicating if specified co-ordinates are inside the bounds of the terrain map
    float GetTerrainHeightAt( float xPosition, float zPosition, bool isFluidMin = false );     // Returns the height of the terrain at the specified coordinates
    Vector3 GetTerrainNormalAt( float xPosition, float zPosition );                            // Returns the surface normal of the terrain at the specified coordinates

  private:
    UINT displayListReference;               // Reference to the display list (retained for fallback)
    std::unique_ptr<Mesh> m_terrainMesh;     // VBO mesh for m_shader rendering
    std::unique_ptr<Shader> m_terrainShader; // Lit+textured m_shader program
    std::vector<TerrainPost> m_postData;     // Vertices that make up the m_terrain
    std::vector<BYTE> m_terrainData;         // Raw m_height map byte data (populated during construction, cleared after build)
    int m_mapSize;                           // Size of map (pixels length)
    int m_stepSize;                          // Steps size between posts
    int m_textureWrap;                       // Number of times to wrap texture over m_terrain
    int m_postsPerSide;                      // Terrain postings per side of m_terrain
    int m_terrainSizeWorldCoords;            // size per side of m_terrain in world coordinates

    void LoadTerrainData( const char* sFileName );  // Loads terrain from .RAW file into terrainData member
    void BuildTerrain( void );                      // Builds the terrain
    void TranslatePostings( void );                 // Translates terrain posts
    void GenerateNormals( void );                   // Generates normals for posts
    void BuildMesh( void );                         // Builds VBO mesh from post data
    int GetPixelHeightAt( int xCoord, int yCoord ); // Returns the .raw height at the specified pixel coordinates
};
} // namespace Geometry
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

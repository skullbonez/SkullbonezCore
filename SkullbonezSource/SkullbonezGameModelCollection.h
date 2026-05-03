#pragma once


// --- Includes ---
#include <list>
#include <vector>
#include <algorithm>
#include <memory>
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"
#include "SkullbonezSpatialGrid.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezIShader.h"


// --- Usings ---
using namespace std;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;


namespace SkullbonezCore
{
namespace GameObjects
{
/* -- Game Model Collection --------------------------------------------------------------------------------------------------------------------------------------

    Represents a collection of game models and operations to assist in managing the collection.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModelCollection
{

  private:
    std::vector<GameModel> m_gameModels;               // Collection of game models
    SpatialGrid m_spatialGrid;                         // Broadphase spatial grid for collision culling
    std::vector<std::pair<int, int>> m_candidatePairs; // Retained-capacity pair buffer (avoids per-frame alloc)
    std::unique_ptr<IShader> m_shadowShader;           // Shadow decal ShaderGL (instanced)
    uint32_t m_shadowInstMesh = 0;                     // Instanced MeshGL handle (via Gfx())
    int m_shadowDiscVertexCount = 0;                   // Disc triangle vertex count
    std::vector<float> m_shadowInstanceData;           // Retained-capacity staging buffer (mat4 + alpha per instance)

    void BuildShadowMesh(); // Builds the shadow disc VAO with instanced attributes

  public:
    GameModelCollection(); // Default constructor
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );                                                   // Moves a game model into the collection
    void Clear();                                                                               // Clears all game models (retains GPU resources)
    void RunPhysics( float fChangeInTime );                                                     // Runs the physics for the specified time step
    void RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] );     // Renders the game models
    void RenderShadows( Geometry::Terrain* terrain, const Matrix4& view, const Matrix4& proj ); // Renders ground shadows beneath all models
    void ResetGLResources();                                                                    // Releases GPU resources for GL context reset
    Vector3 GetModelPosition( int index );                                                      // Returns the position of the specified game model
};
} // namespace GameObjects
} // namespace SkullbonezCore

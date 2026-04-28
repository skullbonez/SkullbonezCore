/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_GAME_MODEL_COLLECTION_H
#define SKULLBONEZ_GAME_MODEL_COLLECTION_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
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
#include "SkullbonezMesh.h"
#include "SkullbonezShader.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
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
    std::vector<GameModel> m_gameModels;    // Collection of game models
    SpatialGrid m_spatialGrid;              // Broadphase spatial grid for collision culling
    std::unique_ptr<Mesh> m_shadowMesh;     // Shadow disc mesh (unit m_radius)
    std::unique_ptr<Shader> m_shadowShader; // Shadow disc m_shader

    void BuildShadowMesh(); // Builds the unit-radius shadow disc mesh

  public:
    GameModelCollection(); // Default constructor
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );                                                   // Moves a game model into the collection
    void RunPhysics( float fChangeInTime );                                                     // Runs the physics for the specified time step
    void RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] );     // Renders the game models
    void RenderShadows( Geometry::Terrain* terrain, const Matrix4& view, const Matrix4& proj ); // Renders ground shadows beneath all models
    void ResetGLResources();                                                              // Releases GPU resources for GL context reset
    Vector3 GetModelPosition( int index );                                                      // Returns the position of the specified game model
};
} // namespace GameObjects
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

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
#include "SkullbonezMesh.h"
#include "SkullbonezShader.h"


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
    std::unique_ptr<Mesh> m_shadowMesh;                // Shadow quad mesh (unit size)
    std::unique_ptr<Shader> m_shadowShader;            // Shadow shader (instanced)
    GLuint m_shadowTexture = 0;                        // Soft Gaussian shadow texture
    GLuint m_shadowInstanceVBO = 0;                    // Per-instance VBO (model matrix + alpha)
    int m_shadowInstanceCapacity = 0;                  // Current instance buffer capacity

    void BuildShadowResources(); // Builds shadow quad, shader, texture, and instance buffer

  public:
    GameModelCollection(); // Default constructor
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );                                                                            // Moves a game model into the collection
    void RunPhysics( float fChangeInTime );                                                                              // Runs the physics for the specified time step
    void RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] );                              // Renders the game models
    void RenderShadows( Geometry::Terrain* terrain, const Matrix4& view, const Matrix4& proj, const float lightPos[4] ); // Renders ground shadows beneath all models
    void ResetGLResources();                                                                                             // Releases GPU resources for GL context reset
    Vector3 GetModelPosition( int index );                                                                               // Returns the position of the specified game model
};
} // namespace GameObjects
} // namespace SkullbonezCore

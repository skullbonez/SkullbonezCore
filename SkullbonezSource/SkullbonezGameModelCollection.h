#pragma once


// --- Includes ---
#include <list>
#include <vector>
#include <algorithm>
#include <memory>
#include <cstdint>
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
    std::vector<float> m_timeRemaining;                // Per-model timestep remainder (retained buffer)
    std::vector<uint8_t> m_groundedThisFrame;          // Per-model grounded flag for current frame (0/1)
    std::vector<uint8_t> m_sleepState;                 // Per-model sleep state: 0=awake, 1=sleeping
    std::vector<uint8_t> m_sleepCounter;               // Frames object has been below sleep threshold
    std::unique_ptr<IShader> m_shadowShader;           // Shadow decal shader (instanced)
    uint32_t m_shadowInstMesh = 0;                     // Instanced mesh handle (via Gfx())
    int m_shadowDiscVertexCount = 0;                   // Disc triangle vertex count
    std::vector<float> m_shadowInstanceData;           // Retained-capacity staging buffer (mat4 + alpha per instance)
    bool m_useLegacyPhysics = false;                   // True when legacy sphere-only solver is active
    std::vector<int64_t> m_collisionCellKeys;          // Cells where narrowphase collisions occurred this frame

#ifdef _DEBUG
    char m_physicsLogPath[256] = {}; // Output path for physics state CSV (empty = disabled)
    int m_physicsLogFrame = 0;       // Frame counter reset when path is set
#endif

    void BuildShadowMesh();            // Builds the shadow disc VAO with instanced attributes
    void RunLegacyPhysics( float dt ); // Physics tick: legacy sphere-only solver (boxes skipped)
    void RunSolverPhysics( float dt ); // Physics tick: unified impulse solver (all objects)

  public:
    GameModelCollection(); // Default constructor
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );                                                                                                                                              // Moves a game model into the collection
    void Clear();                                                                                                                                                                          // Clears all game models (retains GPU resources)
    void SetLegacyMode( bool legacy );                                                                                                                                                     // Routes physics and rendering to the legacy sphere-only path when true
    bool GetLegacyMode() const;                                                                                                                                                            // Returns true when the legacy sphere-only solver is active
    void RunPhysics( float fChangeInTime );                                                                                                                                                // Runs the physics for the specified time step
    void RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] );                                                                                                // Renders the game models
    void RenderShadows( Geometry::Terrain* terrain, const Matrix4& view, const Matrix4& proj, float waterSurfaceY );                                                                       // Renders ground shadows beneath all models
    void ResetGLResources();                                                                                                                                                               // Releases GPU resources for GL context reset
    bool SaveSceneSnapshot( const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Vector3& camEye, const Vector3& camView, const Vector3& camUp ); // Saves full scene state to a .scene file; returns true on success
    Vector3 GetModelPosition( int index );                                                                                                                                                 // Returns the position of the specified game model
    int GetModelCount() const;                                                                                                                                                             // Returns the number of game models
    GameModel& GetModelAtIndex( int index );                                                                                                                                               // Returns a reference to the game model at the given index

    // Broadphase visualizer data accessors
    const SpatialGrid& GetSpatialGrid() const
    {
        return m_spatialGrid;
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return m_collisionCellKeys;
    }

#ifdef _DEBUG
    void SetPhysicsLogPath( const char* path ); // Enable per-frame physics state CSV; empty string disables
#endif
};
} // namespace GameObjects
} // namespace SkullbonezCore

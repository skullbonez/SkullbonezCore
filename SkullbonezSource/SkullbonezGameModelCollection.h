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

    // A contact row is one "do not move through each other" rule for two bodies.
    // Catto's paper solves many of these small rules over and over instead of trying
    // to solve the whole stack in one perfect step.  The extra fields store the
    // directions, lever arms, and accumulated push needed for that repeated solve.
    struct PersistentContact
    {
        int bodyA = -1;                 // First body index.  The normal points from A toward B.
        int bodyB = -1;                 // Second body index.
        int64_t key = 0;                // Stable pair id, used to find last frame's remembered impulses.
        Vector3 normal = ZERO_VECTOR;   // Push direction.  Normal impulses separate bodies; they never pull.
        Vector3 tangent1 = ZERO_VECTOR; // First sideways direction at the contact point, used for friction.
        Vector3 tangent2 = ZERO_VECTOR; // Second sideways direction.  3D contacts need two friction axes.
        Vector3 rA = ZERO_VECTOR;       // Vector from body A center to the contact point, for spin/torque.
        Vector3 rB = ZERO_VECTOR;       // Vector from body B center to the contact point, for spin/torque.
        float penetration = 0.0f;       // How far the bodies overlap.  Zero means touching or separated.
        float normalMass = 0.0f;        // "How much velocity changes per unit push" along the normal.
        float tangentMass1 = 0.0f;      // Same effective-mass idea, but for friction tangent 1.
        float tangentMass2 = 0.0f;      // Same effective-mass idea, but for friction tangent 2.
        float bias = 0.0f;              // Small target separation speed used to remove overlap smoothly.
        float frictionLimit = 0.0f;     // Maximum sideways friction push for this contact this frame.
        float accN = 0.0f;              // Total normal push accumulated by the iterative solver.
        float accT1 = 0.0f;             // Total friction push accumulated along tangent 1.
        float accT2 = 0.0f;             // Total friction push accumulated along tangent 2.
    };

    // The previous frame's solution is a very good first guess for this frame.
    // Keeping these impulses is the contact caching step from the paper; it lets a
    // stack remember the support force that was already holding it up.
    struct PersistentContactCacheEntry
    {
        int64_t key = 0;
        float accN = 0.0f;
        float accT1 = 0.0f;
        float accT2 = 0.0f;
    };
    std::vector<PersistentContact> m_persistentContacts;                 // Catto-style contact rows retained across frames
    std::vector<PersistentContactCacheEntry> m_persistentContactCache;   // Previous-frame contact impulses for warm starting
    std::vector<uint16_t> m_persistentContactCounts;                     // Per-body contact count for mc*g friction bounds
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
    void SolvePersistentObjectContacts( float dt ); // PGS contact-force pass for resting/stacked object contacts

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

    void WakeModel( int index ); // Force a model awake (clears sleep state/counter); call before teleporting/firing a recycled model

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

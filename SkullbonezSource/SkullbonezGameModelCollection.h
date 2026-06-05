#pragma once


// --- Includes ---
#include <list>
#include <vector>
#include <algorithm>
#include <memory>
#include <cstdint>
#include <array>
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"
#include "SkullbonezSpatialGrid.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezIShader.h"
#include "SkullbonezPhysicsDebugVisualizer.h"


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
    std::vector<GameModel> m_gameModels;                     // Collection of game models
    SpatialGrid m_spatialGrid;                               // Broadphase spatial grid for collision culling
    std::vector<std::pair<int, int>> m_candidatePairs;       // Retained-capacity pair buffer (avoids per-frame alloc)
    std::vector<float> m_timeRemaining;                      // Per-model timestep remainder (retained buffer)
    std::array<Vector3, MAX_GAME_MODELS> m_soaPositions;     // Hot SoA stream: current model centers
    std::array<float, MAX_GAME_MODELS> m_soaBoundingRadii;   // Hot SoA stream: broadphase/render radii
    std::array<uint8_t, MAX_GAME_MODELS> m_soaIsBox;         // Hot SoA stream: shape class, 0=sphere, 1=box
    std::array<Matrix4, MAX_GAME_MODELS> m_soaModelMatrices; // Per-frame render matrix stream reused by reflection/main passes
    int m_soaActiveCount = 0;
    bool m_soaBodyDataValid = false;
    bool m_soaModelMatricesValid = false;

    // Sleep support is deliberately not the same thing as "grounded" or "has a
    // contact." It means this body has credible support for deactivation this
    // frame: terrain contact that passed the terrain support tests, or a stack
    // chain rooted in such terrain support. Dynamic object contacts alone do not
    // set this flag, which prevents mid-air collisions from becoming sleep seeds.
    std::vector<uint8_t> m_sleepSupportedThisFrame;
    std::vector<uint8_t> m_sleepInhibitedThisFrame; // Per-model flag for contacts that must remain awake this frame
    std::vector<uint8_t> m_sleepState;              // Per-model sleep state: 0=awake, 1=sleeping
    std::vector<uint8_t> m_sleepCounter;            // Frames object has been below sleep threshold
    std::vector<uint8_t> m_collisionVisualContacts; // Per-render-frame collision/contact flags for the collision visualizer
    std::vector<int> m_sleepIslandVisualId;         // Stable visual island id while a body remains asleep
    std::vector<int> m_sleepIslandAssignedVisualId; // Scratch buffer: island root to visual id while transitioning to sleep
    int m_nextSleepIslandVisualId = 1;
    bool m_collisionVisualFrameActive = false;

    // Directed support edges record only the direction of possible vertical
    // support through object contacts. They are resolved after terrain contacts
    // are known, so a box can support a box above it only if the lower box is
    // itself supported by terrain or an already-supported stack chain.
    std::vector<std::pair<int, int>> m_sleepSupportEdges;

    // Island sleep follows the Box2D/Catto-style rule that connected dynamic
    // bodies deactivate together only when the whole contact island is quiet and
    // supported. These retained buffers avoid allocations in the fixed-step hot
    // path while building a small union-find from this frame's persistent contacts.
    std::vector<int> m_sleepIslandParent;
    std::vector<uint8_t> m_sleepIslandRank;
    std::vector<uint8_t> m_sleepIslandHasAwake;
    std::vector<uint8_t> m_sleepIslandEligible;
    std::vector<uint8_t> m_sleepIslandCanSleep;

    // A contact row is one "do not move through each other" rule for two bodies.
    // Catto's paper solves many of these small rules over and over instead of trying
    // to solve the whole stack in one perfect step.  The extra fields store the
    // directions, lever arms, and accumulated push needed for that repeated solve.
    struct PersistentContact
    {
        int bodyA = -1;                 // First body index.  The normal points from A toward B.
        int bodyB = -1;                 // Second body index.
        uint32_t featureId = 0;         // Contact-point identity within this pair.  Bounding-sphere fallback uses feature 0.
        int64_t key = 0;                // Stable pair+feature id, used to find last frame's remembered impulses.
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

    // Compact per-step solver body state.  Contact iteration mutates this array
    // and writes back to GameModel once, matching Catto's sparse body/row shape.
    struct SolverBodyState
    {
        Vector3 linearVelocity = ZERO_VECTOR;
        Vector3 angularVelocity = ZERO_VECTOR;
        Vector3 invInertia = ZERO_VECTOR;
        RotationMatrix orientation;
        float invMass = 0.0f;
        bool useWorldInertia = false;
    };
    std::vector<PersistentContact> m_persistentContacts;               // Catto-style contact rows retained across frames
    std::vector<PersistentContactCacheEntry> m_persistentContactCache; // Previous-frame contact impulses for warm starting
    std::vector<uint16_t> m_persistentContactCounts;                   // Per-body contact count for mc*g friction bounds
    std::vector<SolverBodyState> m_solverBodies;                       // Per-step compact velocity/inertia state for persistent contact solving
    std::vector<Physics::PhysicsDebugContact> m_physicsDebugContacts;  // Last solver contact rows for visual debugging
    std::unique_ptr<IShader> m_shadowShader;                           // Shadow decal shader (instanced)
    uint32_t m_shadowInstMesh = 0;                                     // Instanced mesh handle (via Gfx())
    int m_shadowDiscVertexCount = 0;                                   // Disc triangle vertex count
    std::vector<float> m_shadowInstanceData;                           // Retained-capacity staging buffer (mat4 + alpha per instance)
    bool m_useLegacyPhysics = false;                                   // True when legacy sphere-only solver is active
    std::vector<int64_t> m_collisionCellKeys;                          // Cells where narrowphase collisions occurred this frame

#ifdef _DEBUG
    char m_physicsLogPath[256] = {};         // Output path for physics state CSV (empty = disabled)
    int m_physicsLogFrame = 0;               // Frame counter reset when path is set
    char m_physicsDiagnosticsPath[256] = {}; // Output path for queryable diagnostics trace (empty = disabled)
    int m_physicsDiagnosticsFrame = 0;       // Frame counter reset when path is set
#endif

    void BuildShadowMesh();                         // Builds the shadow disc VAO with instanced attributes
    void RunLegacyPhysics( float dt );              // Physics tick: legacy sphere-only solver (boxes skipped)
    void RunSolverPhysics( float dt );              // Physics tick: unified impulse solver (all objects)
    void SolvePersistentObjectContacts( float dt ); // PGS contact-force pass for resting/stacked object contacts
    void EnsureCollisionVisualBuffers( int modelCount );
    void MarkCollisionVisualContact( int index );
    void InvalidateSoA();
    void RefreshSoABodyData();
    void EnsureSoAModelMatrices();

    // Extends terrain-backed sleep support through vertical object-contact stack
    // chains. This is separate from the solver so sleep policy can stay strict
    // without weakening collision response or adding damping hacks.
    void PropagateSleepSupport();

  public:
    GameModelCollection(); // Default constructor
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );                                                                                                                                              // Moves a game model into the collection
    void Clear();                                                                                                                                                                          // Clears all game models (retains GPU resources)
    void SetLegacyMode( bool legacy );                                                                                                                                                     // Routes physics and rendering to the legacy sphere-only path when true
    bool GetLegacyMode() const;                                                                                                                                                            // Returns true when the legacy sphere-only solver is active
    void RunPhysics( float fChangeInTime );                                                                                                                                                // Runs the physics for the specified time step
    void RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] );                                                                                                // Renders the game models
    void PrepareRenderStreams();                                                                                                                                                           // Builds cached SoA render streams once for the upcoming frame
    void RenderShadows( Geometry::Terrain* terrain, const Matrix4& view, const Matrix4& proj, float waterSurfaceY );                                                                       // Renders ground shadows beneath all models
    void ResetGLResources();                                                                                                                                                               // Releases GPU resources for GL context reset
    bool SaveSceneSnapshot( const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Vector3& camEye, const Vector3& camView, const Vector3& camUp ); // Saves full scene state to a .scene file; returns true on success
    Vector3 GetModelPosition( int index );                                                                                                                                                 // Returns the position of the specified game model
    int GetModelCount() const;                                                                                                                                                             // Returns the number of game models
    GameModel& GetModelAtIndex( int index );                                                                                                                                               // Returns a reference to the game model at the given index

    void WakeModel( int index );      // Force a model awake (clears sleep state/counter); call before teleporting/firing a recycled model
    void BeginCollisionVisualFrame(); // Clears per-render-frame contact flags before one or more physics substeps
    void EndCollisionVisualFrame();   // Ends contact accumulation for standalone physics callers

    // Broadphase visualizer data accessors
    const SpatialGrid& GetSpatialGrid() const
    {
        return m_spatialGrid;
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return m_collisionCellKeys;
    }
    const std::vector<uint8_t>& GetCollisionVisualContacts() const
    {
        return m_collisionVisualContacts;
    }
    const std::vector<uint8_t>& GetSleepStates() const
    {
        return m_sleepState;
    }
    const std::vector<int>& GetSleepIslandVisualIds() const
    {
        return m_sleepIslandVisualId;
    }
    const std::vector<uint8_t>& GetSleepSupportedStates() const
    {
        return m_sleepSupportedThisFrame;
    }
    const std::vector<uint8_t>& GetSleepInhibitedStates() const
    {
        return m_sleepInhibitedThisFrame;
    }
    const std::vector<Physics::PhysicsDebugContact>& GetPhysicsDebugContacts() const
    {
        return m_physicsDebugContacts;
    }

#ifdef _DEBUG
    void SetPhysicsLogPath( const char* path );         // Enable per-frame physics state CSV; empty string disables
    void SetPhysicsDiagnosticsPath( const char* path ); // Enable queryable physics diagnostics trace; empty string disables
#endif
};
} // namespace GameObjects
} // namespace SkullbonezCore

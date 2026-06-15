#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "SkullbonezGameModel.h"
#include "SkullbonezPersistentContactSolver.h"
#include "SkullbonezPhysicsDiagnosticsSink.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "SkullbonezSkullScope.h"
#include "SkullbonezSleepIslandSystem.h"
#include "SkullbonezSpatialGrid.h"
#include "SkullbonezTornadoField.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsWorld
{
  private:
    friend class GameObjects::SkullScope;
    friend class PhysicsDiagnosticsSink;
    friend class PersistentContactSolver;
    friend class SleepIslandSystem;

    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    std::vector<std::pair<int, int>> m_candidatePairs;
    std::vector<float> m_timeRemaining;

    std::vector<uint8_t> m_sleepSupportedThisFrame;
    std::vector<uint8_t> m_sleepInhibitedThisFrame;
    std::vector<uint8_t> m_sleepState;
    std::vector<uint8_t> m_sleepCounter;
    std::vector<uint8_t> m_collisionVisualContacts;
    std::vector<int> m_sleepIslandVisualId;
    std::vector<int> m_sleepIslandAssignedVisualId;
    int m_nextSleepIslandVisualId = 1;
    bool m_sleepEnabled = true;
    bool m_collisionVisualFrameActive = false;

    std::vector<std::pair<int, int>> m_sleepSupportEdges;
    std::vector<int> m_sleepIslandParent;
    std::vector<uint8_t> m_sleepIslandRank;
    std::vector<uint8_t> m_sleepIslandHasAwake;
    std::vector<uint8_t> m_sleepIslandHasSupportAnchor;
    std::vector<uint8_t> m_sleepIslandEligible;
    std::vector<uint8_t> m_sleepIslandCanSleep;

    struct PersistentContact
    {
        int bodyA = -1;
        int bodyB = -1;
        uint32_t featureId = 0;
        int64_t key = 0;
        Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;
        float penetration = 0.0f;
        float normalMass = 0.0f;
        float tangentMass1 = 0.0f;
        float tangentMass2 = 0.0f;
        float bias = 0.0f;
        float frictionLimit = 0.0f;
        float accN = 0.0f;
        float accT1 = 0.0f;
        float accT2 = 0.0f;
        bool warmStarted = false;
        bool isTerrain = false;
        bool supportsRestingPolicy = true;
        bool inhibitsSleep = false;
        uint8_t manifoldPointCount = 1;
        Math::Vector::Vector3 terrainNormal = Math::Vector::ZERO_VECTOR;
        float terrainWarmStart = 0.0f;
    };

    struct PersistentContactCacheEntry
    {
        int64_t key = 0;
        float accN = 0.0f;
        float accT1 = 0.0f;
        float accT2 = 0.0f;
    };

    struct PersistentContactSolverStats
    {
        int rowCount = 0;
        int cachePreviousRows = 0;
        int cacheHits = 0;
        int cacheMisses = 0;
        int warmStartedRows = 0;
        int positionCorrectionRows = 0;
        int solverIterations = 0;
        float positionCorrectionTotal = 0.0f;
        float positionCorrectionMax = 0.0f;
    };

    struct SolverBodyState
    {
        Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 invInertia = Math::Vector::ZERO_VECTOR;
        Math::Transformation::RotationMatrix orientation;
        float invMass = 0.0f;
        bool useWorldInertia = false;
    };

    std::vector<PersistentContact> m_persistentContacts;
    std::vector<PersistentContactCacheEntry> m_persistentContactCache;
    PersistentContactSolverStats m_persistentContactSolverStats;
    std::vector<uint16_t> m_persistentContactCounts;
    std::vector<SolverBodyState> m_solverBodies;
    std::vector<PhysicsDebugContact> m_physicsDebugContacts;
    std::vector<PhysicsPipelineRecord> m_physicsPipelineTrace;
    std::vector<TerrainContactManifold> m_terrainContactManifolds;
    std::vector<int64_t> m_collisionCellKeys;
    std::array<uint8_t, MAX_GAME_MODELS> m_terrainRestApplied = {};
    TornadoField m_tornadoField;
    PersistentContactSolver m_contactSolver;
    SleepIslandSystem m_sleepIslandSystem;
    PhysicsDiagnosticsSink m_diagnostics;

    void RunSolverPhysics( GameObjects::GameModelCollection& collection, float dt );
    void SolvePersistentObjectContacts( GameObjects::GameModelCollection& collection, float dt );
#ifdef _DEBUG
    void EmitPhysicsDiagnosticsFrame( GameObjects::GameModelCollection& collection, float dt );
#endif
    void EmitPhysicsCollisionTime( GameObjects::GameModelCollection& collection, const char* type, int bodyA, int bodyB, float collisionTime, float availableTime );
    void RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record );
    void EnsureCollisionVisualBuffers( int modelCount );
    void MarkCollisionVisualContact( int index );
    void MarkFixedContact( GameObjects::GameModelCollection& collection, int index );
    void ApplyTornadoField( GameObjects::GameModelCollection& collection, float dt );
    void PropagateSleepSupport( GameObjects::GameModelCollection& collection );

  public:
    PhysicsWorld();

    void Clear();
    void RunPhysics( GameObjects::GameModelCollection& collection, float fChangeInTime );
    void WakeModel( GameObjects::GameModelCollection& collection, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    const std::vector<int64_t>& GetCollisionCellKeys() const;
    const std::vector<uint8_t>& GetCollisionVisualContacts() const;
    const std::vector<uint8_t>& GetSleepStates() const;
    const std::vector<int>& GetSleepIslandVisualIds() const;
    const std::vector<uint8_t>& GetSleepSupportedStates() const;
    const std::vector<uint8_t>& GetSleepInhibitedStates() const;
    const std::vector<PhysicsDebugContact>& GetPhysicsDebugContacts() const;
    const std::vector<PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif
};
} // namespace Physics
} // namespace SkullbonezCore

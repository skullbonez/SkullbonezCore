/*
File: SkullbonezSource/SkullbonezGameModelCollection.h
Purpose:
  Owns all scene models and delegates rendering, physics, and snapshots.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SoA (Structure of Arrays): Cache layout that stores each field in its own
  contiguous array for faster iteration.
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezGameModelCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "SkullbonezGameModel.h"
#include "SkullbonezGameModelStreams.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezPhysicsWorld.h"
#include "SkullbonezShadow.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}

namespace GameObjects
{
class GameModelRenderer;
class SceneSnapshotWriter;

/* -- Game Model Collection --------------------------------------------------------------------------------------------------------------------------------------

    Owns the scene's GameModel storage and exposes stable model-facing calls.
    Physics, rendering, hot SoA streams, and scene serialization live behind
    dedicated collaborators so this class stays a container/facade instead of a
    solver, renderer, serializer, and diagnostics hub all at once.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModelCollection
{
  private:
    friend class SkullScope;
    friend class SceneSnapshotWriter;
    friend class Physics::PhysicsDiagnosticsSink;
    friend class Physics::PersistentContactSolver;
    friend class Physics::SleepIslandSystem;

    std::vector<GameModel> m_gameModels;
    GameModelSoACache m_soaCache;
    Physics::PhysicsWorld m_physicsWorld;
    uint32_t m_nextReplayBodyId = 1;

    void InvalidateSoA();

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );
    void Clear();
    void RunPhysics( float fChangeInTime );
    void RenderModels( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const float lightPos[4], const Basics::CinematicRenderConfig* cinematic = nullptr, const Rendering::ShadowFrameData* shadow = nullptr, float materialAlpha = 1.0f );
    void BuildShadowCasterBatches( Rendering::ShadowCasterBatches& outBatches );
    void RenderShadowCasterBatches( const Rendering::ShadowCasterBatches& batches, const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const Basics::CinematicRenderConfig* cinematic = nullptr );
    void RenderShadowCasters( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const Basics::CinematicRenderConfig* cinematic = nullptr );
    void PrepareRenderStreams();
    bool GetObjectShadowBounds( const Math::Vector::Vector3& focus, float maxDistance, Math::Vector::Vector3& outCenter, float& outRadius, float& outHeightRange );
    void ResetRenderResources();
    bool SaveSceneSnapshot( const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Math::Vector::Vector3& camEye, const Math::Vector::Vector3& camView, const Math::Vector::Vector3& camUp, bool editableScene = false, bool fixedStep = false, bool waterHidden = false, bool terrainHidden = false, bool hasFlatSlope = false, float flatBaseY = 0.0f, float flatSlopeX = 0.0f, float flatSlopeZ = 0.0f );
    Math::Vector::Vector3 GetModelPosition( int index );
    int GetModelCount() const;
    const std::vector<GameModel>& Models() const;
    std::vector<GameModel>& PhysicsModels();
    const std::vector<GameModel>& PhysicsModels() const;
    bool TrimModelsForReplayRestore( int modelCount );
    void CaptureReplaySolverWorldSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot ) const;
    bool RestoreReplaySolverWorldSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot );
    GameModelBodyStream GetBodyStream();
    GameModelRenderStream GetRenderStream();
    GameModel& GetModelAtIndex( int index );
    double GetSceneKineticEnergy();
    void InvalidatePhysicsStreams();
    void ReleaseAttachedFixedTreeParts( int sourceIndex, const Math::Vector::Vector3& seedLinearVelocity, const Math::Vector::Vector3& seedAngularVelocity ); // Wakes same-tree parts at or above a released break point.

    void WakeModel( int index );
    void SeedModelAsleep( int index );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame();
    void EndCollisionVisualFrame();
    void SetTornadoFieldConfig( const Physics::TornadoFieldConfig& config );
    const Physics::TornadoFieldConfig& GetTornadoFieldConfig() const
    {
        return m_physicsWorld.GetTornadoFieldConfig();
    }
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const
    {
        return m_physicsWorld.GetSpatialGrid();
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return m_physicsWorld.GetCollisionCellKeys();
    }
    const std::vector<uint8_t>& GetCollisionVisualContacts() const
    {
        return m_physicsWorld.GetCollisionVisualContacts();
    }
    const std::vector<uint8_t>& GetSleepStates() const
    {
        return m_physicsWorld.GetSleepStates();
    }
    const std::vector<int>& GetSleepIslandVisualIds() const
    {
        return m_physicsWorld.GetSleepIslandVisualIds();
    }
    const std::vector<uint8_t>& GetSleepSupportedStates() const
    {
        return m_physicsWorld.GetSleepSupportedStates();
    }
    const std::vector<uint8_t>& GetSleepInhibitedStates() const
    {
        return m_physicsWorld.GetSleepInhibitedStates();
    }
    const std::vector<Physics::PhysicsDebugContact>& GetPhysicsDebugContacts() const
    {
        return m_physicsWorld.GetPhysicsDebugContacts();
    }
    const std::vector<Physics::PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const
    {
        return m_physicsWorld.GetPhysicsPipelineTrace();
    }

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif
};
} // namespace GameObjects
} // namespace SkullbonezCore

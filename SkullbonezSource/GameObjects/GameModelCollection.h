/*
File: SkullbonezSource/GameObjects/GameModelCollection.h
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
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "GameModel.h"
#include "GameModelStreams.h"
#include "../Maths/Matrix4.h"
#include "../Physics/PhysicsScene.h"
#include "../Rendering/Shadow.h"
#include "../Maths/Vector3.h"

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

/* -- Game Model Collection
--------------------------------------------------------------------------------------------------------------------------------------

    Owns the scene's GameModel storage and exposes stable model-facing calls.
    During the store migration this class remains a compatibility facade:
    physics, rendering, hot SoA streams, and scene serialization sit behind
    dedicated collaborators, while older runtime tools still use model-indexed
    accessors until their APIs are moved.
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
    Physics::PhysicsScene m_physicsScene;
    uint32_t m_nextReplayBodyId = 1;

    void InvalidateSoA();

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );
    void Clear();
    void RunPhysics( float fChangeInTime );
    void RenderModels( const Math::Transformation::Matrix4& view,
                       const Math::Transformation::Matrix4& proj,
                       const float lightPos[4],
                       const Basics::CinematicRenderConfig* cinematic = nullptr,
                       const Rendering::ShadowFrameData* shadow = nullptr,
                       float materialAlpha = 1.0f,
                       const std::vector<uint8_t>* modelMask = nullptr,
                       bool drawMaskedModels = true );
    void BuildShadowCasterBatches( Rendering::ShadowCasterBatches& outBatches );
    void RenderShadowCasterBatches( const Rendering::ShadowCasterBatches& batches,
                                    const Math::Transformation::Matrix4& view,
                                    const Math::Transformation::Matrix4& proj,
                                    const Basics::CinematicRenderConfig* cinematic = nullptr );
    void RenderShadowCasters( const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const Basics::CinematicRenderConfig* cinematic = nullptr );
    void PrepareRenderStreams();
    bool GetObjectShadowBounds( const Math::Vector::Vector3& focus,
                                float maxDistance,
                                Math::Vector::Vector3& outCenter,
                                float& outRadius,
                                float& outHeightRange );
    void ResetRenderResources();
    bool SaveSceneSnapshot( const char* path,
                            bool physicsOn,
                            bool textOn,
                            Environment::WorldEnvironment& worldEnv,
                            const Math::Vector::Vector3& camEye,
                            const Math::Vector::Vector3& camView,
                            const Math::Vector::Vector3& camUp,
                            bool editableScene = false,
                            bool fixedStep = false,
                            bool waterHidden = false,
                            bool terrainHidden = false,
                            bool hasFlatSlope = false,
                            float flatBaseY = 0.0f,
                            float flatSlopeX = 0.0f,
                            float flatSlopeZ = 0.0f );
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
    const Physics::PhysicsBodyStore& GetPhysicsBodyStore();
    const Physics::ColliderStore& GetColliderStore();
    const Rendering::RenderInstanceStore& GetRenderInstanceStore();
    GameModel& GetModelAtIndex( int index );
    double GetSceneKineticEnergy();
    void InvalidatePhysicsStreams();
    void ReleaseAttachedFixedTreeParts(
        int sourceIndex,
        const Math::Vector::Vector3& seedLinearVelocity,
        const Math::Vector::Vector3& seedAngularVelocity ); // Wakes same-tree parts at or above a released break point.

    void WakeModel( int index );
    void SeedModelAsleep( int index );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame();
    void EndCollisionVisualFrame();
    void SetTornadoFieldConfig( const Physics::TornadoFieldConfig& config );
    const Physics::TornadoFieldConfig& GetTornadoFieldConfig() const
    {
        return m_physicsScene.GetTornadoFieldConfig();
    }
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const
    {
        return m_physicsScene.GetSpatialGrid();
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return m_physicsScene.GetCollisionCellKeys();
    }
    const std::vector<uint8_t>& GetCollisionVisualContacts() const
    {
        return m_physicsScene.GetCollisionVisualContacts();
    }
    const std::vector<uint8_t>& GetSleepStates() const
    {
        return m_physicsScene.GetSleepStates();
    }
    const std::vector<int>& GetSleepIslandVisualIds() const
    {
        return m_physicsScene.GetSleepIslandVisualIds();
    }
    const std::vector<uint8_t>& GetSleepSupportedStates() const
    {
        return m_physicsScene.GetSleepSupportedStates();
    }
    const std::vector<uint8_t>& GetSleepInhibitedStates() const
    {
        return m_physicsScene.GetSleepInhibitedStates();
    }
    const std::vector<Physics::PhysicsDebugContact>& GetPhysicsDebugContacts() const
    {
        return m_physicsScene.GetPhysicsDebugContacts();
    }
    const std::vector<Physics::PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const
    {
        return m_physicsScene.GetPhysicsPipelineTrace();
    }

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool SetPhysicsDiagnosticsSuppressed( bool suppressed );
#endif
};
} // namespace GameObjects
} // namespace SkullbonezCore

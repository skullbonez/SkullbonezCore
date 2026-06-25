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
#include "../Physics/PhysicsEngine.h"
#include "../Rendering/RenderSceneView.h"
#include "../Rendering/Shadow.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Basics
{
struct MainMemoryGameObjectStats;
}

namespace Environment
{
class WorldEnvironment;
}

namespace GameObjects
{
class GameModelRenderer;

/* -- Game Model Collection
--------------------------------------------------------------------------------------------------------------------------------------

    Owns the scene's GameModel storage and exposes stable model-facing calls.
    During the store migration this class remains a compatibility facade:
    physics, rendering, hot SoA streams, and scene serialization sit behind
    dedicated collaborators, while older runtime tools still use model-indexed
    accessors until their APIs are moved.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModelCollection : public Rendering::IRenderSceneView
{
  private:
    std::vector<GameModel> m_gameModels;
    GameModelSoACache m_soaCache;
    Physics::PhysicsEngine m_physicsEngine;
    uint32_t m_nextReplayBodyId = 1;

    void InvalidateSoA();

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void AddGameModel( GameModel gameModel );
    void Clear();
    void RunPhysics( float fChangeInTime );
    int GetRenderModelCount() const override;
    int CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount ) override;
    void RenderModels( const Math::Transformation::Matrix4& view,
                       const Math::Transformation::Matrix4& proj,
                       const float lightPos[4],
                       const Basics::CinematicRenderConfig* cinematic = nullptr,
                       const Rendering::ShadowFrameData* shadow = nullptr,
                       float materialAlpha = 1.0f,
                       const std::vector<uint8_t>* modelMask = nullptr,
                       bool drawMaskedModels = true ) override;
    void BuildShadowCasterBatches( Rendering::ShadowCasterBatches& outBatches ) override;
    void RenderShadowCasterBatches( const Rendering::ShadowCasterBatches& batches,
                                    const Math::Transformation::Matrix4& view,
                                    const Math::Transformation::Matrix4& proj,
                                    const Basics::CinematicRenderConfig* cinematic = nullptr ) override;
    void RenderShadowCasters( const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const Basics::CinematicRenderConfig* cinematic = nullptr ) override;
    void PrepareRenderStreams();
    bool GetObjectShadowBounds( const Math::Vector::Vector3& focus,
                                float maxDistance,
                                Math::Vector::Vector3& outCenter,
                                float& outRadius,
                                float& outHeightRange ) override;
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
    Basics::MainMemoryGameObjectStats CollectMemoryStats() const;
    std::vector<GameModel>& PhysicsModels();
    const std::vector<GameModel>& PhysicsModels() const;
    bool TrimModelsForReplayRestore( int modelCount );
    void CaptureReplaySolverWorldSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot ) const;
    bool RestoreReplaySolverWorldSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot );
    GameModelBodyStream GetBodyStream();
    GameModelRenderStream GetRenderStream();
    Physics::PhysicsEngine& GetPhysicsEngine();
    const Physics::PhysicsEngine& GetPhysicsEngine() const;
    const Physics::PhysicsBodyStore& GetPhysicsBodyStore();
    const Physics::ColliderStore& GetColliderStore();
    const Rendering::RenderInstanceStore& GetRenderInstanceStore();
    GameModel& GetModelAtIndex( int index );
    double GetSceneKineticEnergy();
    void InvalidatePhysicsStreams();
    void ReleaseAttachedFixedTreeParts(
        int sourceIndex,
        const Math::Vector::Vector3& seedLinearVelocity,
        const Math::Vector::Vector3& seedAngularVelocity ); // Wakes/releases same-tree parts at or above a break point.

    void WakeModel( int index );
    void SeedModelAsleep( int index );
    void SetPhysicsSleepEnabled( bool enabled );
    void ClearPointJointConstraints();
    void AddPointJointConstraint( const Physics::PointJointConstraint& constraint );
    void BeginCollisionVisualFrame();
    void EndCollisionVisualFrame();
    void SetTornadoFieldConfig( const Physics::TornadoFieldConfig& config );
    const Physics::TornadoFieldConfig& GetTornadoFieldConfig() const
    {
        return m_physicsEngine.GetTornadoFieldConfig();
    }
    void SetTornadoSystemConfig( const Physics::TornadoSystemConfig& config );
    const Physics::TornadoSystemConfig& GetTornadoSystemConfig() const
    {
        return m_physicsEngine.GetTornadoSystemConfig();
    }
    float GetTornadoSystemElapsedSeconds() const
    {
        return m_physicsEngine.GetTornadoSystemElapsedSeconds();
    }
    void RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const float lightPos[4],
                                     float alphaOverride ) override;
    void RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                             const Math::Transformation::Matrix4& viewProjection,
                             Geometry::Terrain* terrain ) override;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj ) override;

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const
    {
        return m_physicsEngine.GetSpatialGrid();
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return m_physicsEngine.GetCollisionCellKeys();
    }
    const std::vector<uint8_t>& GetCollisionVisualContacts() const
    {
        return m_physicsEngine.GetCollisionVisualContacts();
    }
    const std::vector<uint8_t>& GetSleepStates() const
    {
        return m_physicsEngine.GetSleepStates();
    }
    const std::vector<int>& GetSleepIslandVisualIds() const
    {
        return m_physicsEngine.GetSleepIslandVisualIds();
    }
    const std::vector<uint8_t>& GetSleepSupportedStates() const
    {
        return m_physicsEngine.GetSleepSupportedStates();
    }
    const std::vector<uint8_t>& GetSleepInhibitedStates() const
    {
        return m_physicsEngine.GetSleepInhibitedStates();
    }
    const std::vector<Physics::PhysicsDebugContact>& GetPhysicsDebugContacts() const
    {
        return m_physicsEngine.GetPhysicsDebugContacts();
    }
    const std::vector<Physics::PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const
    {
        return m_physicsEngine.GetPhysicsPipelineTrace();
    }
    const std::vector<Physics::PointJointConstraint>& GetPointJointConstraints() const
    {
        return m_physicsEngine.GetPointJointConstraints();
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

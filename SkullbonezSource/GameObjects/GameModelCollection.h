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
  Physics material: Per-object friction and drag coefficients cached by the
    collection before models are added or reconfigured.
  Body simulation limit: Scalar cap cached by the collection before models hand
    velocity state to RigidBody integration.
  Contact policy: Terrain and contact thresholds cached by the collection so
    existing and newly added models receive the same physics policy.
  PhysicsModelAccess: Stack-owned facade that forwards allowed physics sync
    commands to this collection without making it inherit physics interfaces.
  Replay body id: Per-collection identity saved in replay samples so restore
    paths can reject stale model slots.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - m_gameModels is the stable scene-order owner; collaborators mirror or view
    that order rather than replacing it.
  - Replay body ids are assigned monotonically per collection so diagnostics can
    identify bodies across frames.

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
#include "../Physics/PhysicsModelAccess.h"
#include "../Rendering/Shadow.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Basics
{
struct CinematicRenderConfig;
class EngineConfig;
struct MainMemoryGameObjectStats;
struct RenderHelperContext;
} // namespace Basics

namespace Assets
{
class AssetSystem;
}

namespace Environment
{
class WorldEnvironment;
}

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class CollisionVisualizer;
class PhysicsDebugVisualizer;
} // namespace Physics

namespace Rendering
{
class IRenderResourceFactory;
}

namespace Threading
{
class WorkerPool;
}

namespace GameObjects
{
class GameModelRenderer;
class GameModelCollectionPhysicsAdapter;

/* -- Game Model Collection
--------------------------------------------------------------------------------------------------------------------------------------

    Owns the scene's GameModel storage and exposes stable model-facing calls.
    Physics, rendering, hot SoA streams, and scene serialization sit behind
    dedicated collaborators. Some runtime tools still use model-indexed calls
    because scene files, replay streams, and editor picks preserve model order.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModelCollection
{
    // Why: the adapter is the named model-order boundary while old callers
    // migrate from model indices to durable physics handles.
    friend class GameModelCollectionPhysicsAdapter;

  private:
    std::vector<GameModel> m_gameModels;
    GameModelSoACache m_soaCache;
    Physics::PhysicsEngine m_physicsEngine;
    // Cached physics policy applied to existing and newly added models whenever
    // runtime config changes.
    Physics::PhysicsMaterial m_physicsMaterial;
    Physics::BodySimulationLimits m_bodySimulationLimits;
    Physics::ContactPolicy m_contactPolicy;
    Threading::WorkerPool* m_workerPool = nullptr; // Borrowed startup worker pool for render/physics parallel helpers.
    bool m_renderCollisionVolumes = false;         // Cached render debug toggle copied from EngineConfig.
    bool m_shadowParallelPrep = false;             // Cached worker-prep toggle copied from EngineConfig.
    uint32_t m_nextReplayBodyId = 1;

    void InvalidateSoA();

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void BindWorkerPool( Threading::WorkerPool& workerPool );
    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    bool ShouldRenderCollisionVolumes() const;
    bool ShouldUseShadowParallelPrep() const;
    Threading::WorkerPool* RenderWorkerPool() const;
    void AddGameModel( GameModel gameModel );
    void Clear();
    void RunPhysics( float fChangeInTime,
                     const Basics::EngineConfig& config,
                     const Physics::PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool );
    int CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount );
    void RenderModels( const Basics::RenderHelperContext& helperContext,
                       const Math::Transformation::Matrix4& view,
                       const Math::Transformation::Matrix4& proj,
                       const float lightPos[4],
                       const Basics::CinematicRenderConfig* cinematic = nullptr,
                       const Rendering::ShadowFrameData* shadow = nullptr,
                       float materialAlpha = 1.0f,
                       const std::vector<uint8_t>* modelMask = nullptr,
                       bool drawMaskedModels = true );
    void BuildShadowCasterBatches( Rendering::ShadowCasterBatches& outBatches );
    void RenderShadowCasterBatches( const Basics::RenderHelperContext& helperContext,
                                    const Rendering::ShadowCasterBatches& batches,
                                    const Math::Transformation::Matrix4& view,
                                    const Math::Transformation::Matrix4& proj,
                                    const Basics::CinematicRenderConfig* cinematic = nullptr );
    void RenderShadowCasters( const Basics::RenderHelperContext& helperContext,
                              const Math::Transformation::Matrix4& view,
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
    int ModelCount() const;
    const std::vector<GameModel>& Models() const;
    // Lifetime: returned model pointers are stable only until collection
    // mutation. Null means the caller held a stale model index.
    const GameModel* TryGetModel( int index ) const;
#ifdef _DEBUG
    bool TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const;
    void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const;
    // Builds one diagnostics record from model-owned display names plus
    // physics-owned body/collider stores, without handing debug sinks a mutable
    // or indexable GameModel range.
    bool TryGetPhysicsDiagnosticsModel( int index,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        Physics::PhysicsDiagnosticsModelRecord& outRecord ) const;
#endif
    // Replays restore saved body state through the collection so cache
    // invalidation and replay-id validation stay with the model owner. The
    // physics values still land in PhysicsBodyStore, not a model reload.
    bool TryRestoreReplayBodyState( int index,
                                    uint32_t replayBodyId,
                                    bool fixed,
                                    const Math::Vector::Vector3& position,
                                    const Math::Orientation::Quaternion& orientation,
                                    const Math::Vector::Vector3& linearVelocity,
                                    const Math::Vector::Vector3& angularVelocity,
                                    float mass,
                                    float inverseMass,
                                    const Math::Vector::Vector3& rotationalInertia,
                                    const Math::Vector::Vector3& inverseRotationalInertia );
    // Replay prediction temporarily simulates from copied body state, then
    // restores the live scene through this owner-checked command.
    bool TryRestoreReplayPredictionBodyState( int index,
                                              uint32_t replayBodyId,
                                              bool fixed,
                                              const Math::Vector::Vector3& position,
                                              const Math::Orientation::Quaternion& orientation,
                                              const Math::Vector::Vector3& linearVelocity,
                                              const Math::Vector::Vector3& angularVelocity,
                                              float mass,
                                              float inverseMass,
                                              const Math::Vector::Vector3& rotationalInertia,
                                              const Math::Vector::Vector3& inverseRotationalInertia,
                                              float fixedContactHighlightSeconds );
    // Replay scrub rendering may override only the draw pose for a frame. The
    // replay id check prevents stale sample indices from moving the wrong body.
    bool TrySetReplayRenderPose( int index,
                                 uint32_t replayBodyId,
                                 const Math::Vector::Vector3& position,
                                 const Math::Orientation::Quaternion& orientation );
    // Mutates angular velocity through the collection so derived body streams
    // cannot keep a stale copy.
    bool TrySetModelAngularVelocity( int index, const Math::Vector::Vector3& angularVelocity );
    Basics::MainMemoryGameObjectStats CollectMemoryStats() const;
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
    GameModelBodyStream GetPhysicsBodyStream();
    void InvalidatePhysicsStreams();
    void WriteBackPhysicsBodies( const Physics::PhysicsBodyStore& bodyStore );
    void WriteBackPhysicsBody( const Physics::PhysicsBodyStore& bodyStore, int modelIndex );
    void ReloadPhysicsBodies( Physics::PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates );
    void RefreshPhysicsBodyFromModel( Physics::PhysicsBodyStore& bodyStore, int modelIndex );
    void RefreshPhysicsColliders( Physics::ColliderStore& colliderStore, const Physics::PhysicsBodyStore& bodyStore );
    void RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore,
                                 const Physics::PhysicsBodyStore& bodyStore,
                                 const Physics::ColliderStore& colliderStore );
    void CommitEditedModelPhysicsState( int modelIndex, bool colliderChanged );
    Physics::PhysicsDiagnosticsView GetPhysicsDiagnosticsView() const;
    void NotifyFixedContact( int modelIndex, float highlightSeconds );
    void TickContactHighlights( int modelCount, float deltaSeconds );
    void NotifyAudioContact( int modelIndex, float highlightSeconds );
    // Legacy model-owned release command retained for non-step callers that do
    // not yet own a current PhysicsBodyStore.
    void ReleaseAttachedFixedTreeParts( const Physics::PhysicsFixedTreeReleaseEvent& event );
    // Uses collection-owned tree grouping metadata, but writes released motion
    // state into PhysicsBodyStore and fills a caller-reused wake list.
    void ReleaseAttachedFixedTreeParts( Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::PhysicsFixedTreeReleaseEvent& event,
                                        std::vector<int>& outReleasedBodyIndices );
    void ReleaseAttachedFixedTreeParts( int sourceIndex,
                                        const Math::Vector::Vector3& seedLinearVelocity,
                                        const Math::Vector::Vector3& seedAngularVelocity );

    void SetPhysicsSleepEnabled( bool enabled );
    void ClearPointJointConstraints();
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
                                     Assets::AssetSystem& assets,
                                     Rendering::IRenderResourceFactory& renderResources,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const float lightPos[4],
                                     float alphaOverride );
    void RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                             const Math::Transformation::Matrix4& viewProjection,
                             Geometry::Terrain* terrain );
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );

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

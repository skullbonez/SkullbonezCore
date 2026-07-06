/*
File: SkullbonezSource/GameObjects/GameModelCollection.h
Purpose:
  Owns all scene models and delegates rendering, physics, and snapshots.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Physics material: Per-object friction and drag coefficients cached by the
    collection before models are added or reconfigured.
  Body simulation limit: Scalar cap cached by the collection before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds cached by the collection so
    existing and newly added models receive the same physics policy.
  Body descriptor: Value packet containing authoring body facts that
    PhysicsScene turns into a live PhysicsBodyStore row.
  Render instance store: Renderer-facing snapshot built once before frame passes
    so draw code can read physics-owned transforms without GameModel pose copies.
  Topology drift: A body/collider/model count mismatch that means stores must
    import explicit construction descriptors before stepping.
  Scene-object group: Cold metadata that maps multi-part authored objects, such
    as ragdolls or releasable trees, back to a root model slot.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsScene turns into a live ColliderStore row.
  Fixed-tree release: Authored scene rule that lets tree parts become dynamic
    when a related fixed part is hit strongly enough.
  Replay render pose override: One-frame presentation pose used when replay
    scrubbing or prediction draws historical/future bodies without mutating physics.
  Replay body id: PhysicsBodyStore row identity saved in replay samples so
    restore paths can reject stale model slots.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - m_gameModels is the stable scene-order owner; collaborators mirror or view
    that order rather than replacing it.
  - m_sceneObjectGroups and m_authoredBodyDescs are same-length sidecars keyed
    by m_gameModels slot. GameModel does not own runtime grouping fields, and
    body-store topology repair reloads from descriptor rows rather than model
    physics fields.
  - Replay identity lives in PhysicsBodyStore rows after creation. Collection
    code receives scene-owned ids at creation and does not allocate them.
  - Collider shape/material data is imported into ColliderStore at create,
    edit, config, or topology-repair boundaries; the collection does not keep a
    second collider-authoring cache.
  - Replay body ids are derived from scene object ids at creation and stored on
    PhysicsBodyStore rows so diagnostics can identify bodies without reopening
    GameModel.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "GameModel.h"
#include "../Maths/Matrix4.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsObjectPolicy.h"
#include "../Rendering/RenderInstanceStore.h"
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
struct PhysicsBodyCreateDesc;
struct PhysicsColliderCreateDesc;
} // namespace Physics

namespace Rendering
{
class IRenderResourceFactory;
} // namespace Rendering

namespace Threading
{
class WorkerPool;
}

namespace GameObjects
{
class GameModelRenderer;

enum class GameModelCollectionKind : uint8_t
{
    None = 0,
    SimpleRagdoll,
    ReleasableTree
};

// Creation-only metadata for multi-part authored objects. Callers that already
// know root/part order pass it once; the collection copies it into the dense
// sidecar instead of deriving grouping from display names.
struct SceneObjectGroupCreateDesc
{
    GameModelCollectionKind kind = GameModelCollectionKind::None;
    int rootModelIndex = -1;
    int partIndex = -1;
};

// Value packet for cold editor/replay body edits. Set only the fields changed by
// the command; unchanged fields are copied from the current PhysicsBodyStore row.
struct PhysicsBodyStateEdit
{
    bool hasPosition = false;
    Math::Vector::Vector3 position;
    bool hasOrientation = false;
    Math::Orientation::Quaternion orientation;
    bool hasLinearVelocity = false;
    Math::Vector::Vector3 linearVelocity;
    bool hasAngularVelocity = false;
    Math::Vector::Vector3 angularVelocity;
};

/* -- Game Model Collection
--------------------------------------------------------------------------------------------------------------------------------------

    Owns the scene's GameModel storage and exposes stable model-facing calls.
    Physics, rendering, and scene serialization sit behind dedicated
    collaborators. Some runtime tools still use model-indexed calls because
    scene files, replay streams, and editor picks preserve model order.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GameModelCollection
{
  private:
    struct SceneObjectGroupRecord
    {
        GameModelCollectionKind kind = GameModelCollectionKind::None;
        int rootModelIndex = -1;
        int partIndex = -1;
    };

    std::vector<GameModel> m_gameModels;
    // Dense sidecar for cold scene-object grouping. Keeping this metadata out
    // of GameModel preserves the model vector for authored presentation data
    // while collection-order systems still get O(1) group lookup by model slot.
    std::vector<SceneObjectGroupRecord> m_sceneObjectGroups;
    // Same-length authoring rows for body-store topology repair. Editor/replay
    // commits update these before refreshing PhysicsBodyStore rows.
    std::vector<Physics::PhysicsBodyCreateDesc> m_authoredBodyDescs;
    Physics::PhysicsEngine m_physicsEngine;
    // Cached physics policy applied to existing and newly added models whenever
    // runtime config changes.
    Physics::PhysicsMaterial m_physicsMaterial;
    Physics::BodySimulationLimits m_bodySimulationLimits;
    Physics::ContactPolicy m_contactPolicy;
    Threading::WorkerPool* m_workerPool = nullptr; // Borrowed startup worker pool for render/physics parallel helpers.
    bool m_renderCollisionVolumes = false;         // Cached render debug toggle copied from EngineConfig.
    bool m_shadowParallelPrep = false;             // Cached worker-prep toggle copied from EngineConfig.
    std::vector<Rendering::RenderInstancePresentationRecord>
        m_renderPresentationRecords;               // Render-facing material/highlight values keyed by model slot.

    void ReserveForActiveGameModelCapacity();
    SceneObjectGroupRecord BuildSceneObjectGroupForAppend( const GameModel& gameModel,
                                                           int newModelIndex,
                                                           SceneObjectGroupCreateDesc groupDesc );
    SceneObjectGroupRecord GroupRecordAt( int modelIndex ) const;
    std::vector<uint32_t> BuildReplayBodyIdsForReload( const Physics::PhysicsBodyStore& bodyStore );
    // Owner boundary: fixed-tree grouping is collection metadata. Body-store
    // import receives only the scalar root, never collection-kind accessors.
    std::vector<int> BuildFixedTreeReleaseRootsForReload() const;
    std::vector<Physics::PhysicsBodyCreateDesc>
    BuildBodyCreateDescsForReload( const Physics::PhysicsBodyStore& bodyStore );
    int FixedTreeReleaseRootForModelIndex( int modelIndex ) const;
    void RefreshRenderInstances();
    Physics::PhysicsBodyHandle AppendGameModelAndPhysicsRows( GameModel gameModel,
                                                              Physics::PhysicsBodyCreateDesc bodyDesc,
                                                              Physics::PhysicsSceneObjectId sceneObjectId,
                                                              Physics::PhysicsColliderCreateDesc colliderDesc,
                                                              SceneObjectGroupCreateDesc groupDesc );

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void BindWorkerPool( Threading::WorkerPool& workerPool );
    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    bool ShouldRenderCollisionVolumes() const;
    bool ShouldUseShadowParallelPrep() const;
    Threading::WorkerPool* RenderWorkerPool() const;
    // Appends model storage while importing caller-owned collider shape/material
    // facts and any explicit scene-object grouping directly into owner stores.
    Physics::PhysicsBodyHandle AddGameModel( GameModel gameModel,
                                             Physics::PhysicsBodyCreateDesc bodyDesc,
                                             Physics::PhysicsColliderCreateDesc colliderDesc,
                                             Physics::PhysicsSceneObjectId sceneObjectId,
                                             SceneObjectGroupCreateDesc groupDesc = {} );
    void Clear();
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
    void PrepareRenderInstances();
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
    // Scene-object grouping belongs to the collection because model order is the
    // scene key shared by editor, replay, snapshots, and physics import.
    GameModelCollectionKind GroupKindAt( int modelIndex ) const;
    int GroupRootModelIndexAt( int modelIndex ) const;
    int GroupPartIndexAt( int modelIndex ) const;
    bool IsSimpleRagdollPart( int modelIndex ) const;
    bool IsSimpleRagdollTorso( int modelIndex ) const;
    int RagdollRootModelIndexForPart( int modelIndex ) const;
    bool TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const;
    int GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const;
#ifdef _DEBUG
    bool TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const;
    void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const;
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
    // Mutates angular velocity through the collection so PhysicsBodyStore is
    // refreshed through the same owner-checked path as replay/editor edits.
    bool TrySetModelAngularVelocity( int index, const Math::Vector::Vector3& angularVelocity );
    Basics::MainMemoryGameObjectStats CollectMemoryStats() const;
    bool TrimModelsForReplayRestore( int modelCount );
    void CaptureReplaySolverWorldSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot ) const;
    bool RestoreReplaySolverWorldSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot );
    Physics::PhysicsEngine& GetPhysicsEngine();
    const Physics::PhysicsEngine& GetPhysicsEngine() const;
    const Physics::PhysicsBodyStore& GetPhysicsBodyStore();
    const Physics::ColliderStore& GetColliderStore();
    // Repairs model/body count drift at the model-owner edge. Same-count body
    // edits remain PhysicsBodyStore authority and must commit through explicit
    // commands instead of reopening a model refresh.
    bool RepairPhysicsBodyTopology();
    // Repairs model/body/collider count drift before tool or picker code asks
    // for body handles and collider bounds.
    bool RepairPhysicsBodyAndColliderTopology();
    // Current prepared collider snapshot. Hot render passes use this after
    // PrepareRenderInstances() instead of invoking topology repair mid-submit.
    const Physics::ColliderStore& Colliders() const;
    // Current prepared render snapshot. Call PrepareRenderInstances() before frame
    // passes; cold callers that need an ensured snapshot use GetRenderInstanceStore().
    const Rendering::RenderInstanceStore& RenderInstances() const;
    const std::vector<Rendering::RenderInstancePresentationRecord>& RenderPresentationRecords() const
    {
        return m_renderPresentationRecords;
    }
    const char* DisplayNameAt( int modelIndex ) const;
    int FindModelIndexByDisplayName( const char* name ) const;
    const Rendering::RenderInstanceStore& GetRenderInstanceStore();
    GameModel& GetModelAtIndex( int index );
    double GetSceneKineticEnergy();
    // Commits a body-only edit described by explicit command data; unchanged
    // fields come from PhysicsBodyStore and the descriptor sidecar is refreshed
    // only after the store commit wins.
    bool ApplyPhysicsBodyEdit( int modelIndex, const PhysicsBodyStateEdit& edit );
    // Commits a shape edit with optional body fields from the same cold command.
    bool ApplyPhysicsBodyColliderEdit( int modelIndex,
                                       const PhysicsBodyStateEdit& edit,
                                       Physics::PhysicsColliderCreateDesc colliderDesc );
    // Owner command for callers whose explicit descriptors are already current.
    void CommitEditedModelBodyState( int modelIndex );
    // Owner command for shape edits that do not also change body fields.
    void CommitEditedModelColliderState( int modelIndex, Physics::PhysicsColliderCreateDesc colliderDesc );
    void NotifyFixedContact( int modelIndex, float highlightSeconds );
    void TickContactHighlights( int modelCount, float deltaSeconds );
    void NotifyAudioContact( int modelIndex, float highlightSeconds );
    // Runtime-tool edge: ray tools release authored fixed tree props through
    // PhysicsBodyStore; presentation reads the store/render snapshot instead of
    // forcing a per-release model-side body projection.
    bool ReleaseAttachedFixedTreeParts( int sourceIndex,
                                        float releaseImpulseStrength,
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
    void UpdateCollisionVisualizer( Physics::CollisionVisualizer& visualizer, float deltaSeconds );
    void UpdatePhysicsDebugVisualizer( Physics::PhysicsDebugVisualizer& visualizer, float deltaSeconds );
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

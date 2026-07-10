/*
File: SkullbonezSource/GameObjects/GameModelCollection.h
Purpose:
  Coordinates transient presentation rows with physics/collider/render stores.

Mental model:
  SceneController owns durable entity metadata. GameModelCollection coordinates
  same-row transient feedback and the currently co-located physics/render
  stores. Creation uses one fail-before-mutation transaction; moving the
  physical physics owner remains the separate A2 boundary.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Physics material: Per-object friction and drag coefficients owned by
    PhysicsScene and copied into authored descriptor rows at cold boundaries.
  Body simulation limit: Scalar cap owned by PhysicsScene before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds owned by PhysicsScene so
    existing and newly added models receive the same physics policy.
  Body descriptor: PhysicsScene-owned authoring value that can rebuild a live
    PhysicsBodyStore row without reading GameModel physics fields.
  Render instance store: Renderer-facing snapshot built once before frame passes
    so draw code can read physics-owned transforms and render-owned presentation
    rows without GameModel pose copies.
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

Invariants:
  - The bound SceneEntityStore is the stable scene-order owner; the collection
    borrows it and must keep every same-row store count aligned.
  - SceneObjectGroupStore is a same-length scene metadata store keyed by
    scene entity slot. GameModel does not own runtime grouping fields;
    PhysicsScene owns body descriptor rows for topology repair.
  - Replay identity lives in PhysicsBodyStore rows after creation. Collection
    code receives scene-owned ids at creation and does not allocate them.
  - Collider shape/material data is imported into ColliderStore at create,
    edit, config, or topology-repair boundaries; the collection does not keep a
    second collider-authoring cache.
  - RenderInstanceStore reads durable material/name values from SceneEntityStore
    and transient contact highlights from GameModel.
  - Replay body ids are derived from scene object ids at creation and stored on
    PhysicsBodyStore rows so diagnostics can identify bodies without reopening
    GameModel.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "GameModel.h"
#include "../Core/SbResult.h"
#include "../Maths/Matrix4.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsEngineStoreQueries.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../Rendering/Shadow.h"
#include "../Runtime/Scene/SceneEntityStore.h"
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
class IRenderCommandContext;
class IRenderDiagnostics;
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

// Concept: creation returns both the recoverable status and the created body
// handle. Capacity, duplicate identity, and malformed grouping fail before any
// owner row changes; callers must check status before using the handle.
struct SceneEntityCreateResult
{
    Basics::SbResult status;
    Physics::PhysicsBodyHandle body;
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

    // Contact highlights are transient same-row render feedback. Durable scene
    // identity and presentation intent live in the bound SceneEntityStore.
    class PresentationStore
    {
      public:
        void Reserve( std::size_t capacity );
        void Clear();
        void Append( GameModel model );
        bool TrimToCount( int count );
        int Count() const;
        std::size_t Capacity() const;
        uint64_t CapacityBytes() const;
        const std::vector<GameModel>& Records() const;
        GameModel& MutableAt( int index );
        const GameModel* TryGet( int index ) const;

      private:
        std::vector<GameModel> m_records;
    };

    // Scene-object groups are cold scene identity metadata, not per-frame model
    // data. Keep their dense storage behind query methods so editor/replay code
    // can ask by scene slot without reopening GameModel fields.
    class SceneObjectGroupStore
    {
      public:
        void Reserve( std::size_t capacity );
        void Clear();
        void Append( SceneObjectGroupRecord record );
        bool TrimToCount( int count );
        int Count() const;
        std::size_t Capacity() const;
        uint64_t CapacityBytes() const;
        SceneObjectGroupRecord RecordAt( int modelIndex ) const;

      private:
        std::vector<SceneObjectGroupRecord> m_records;
    };

    PresentationStore m_presentations;
    SceneObjectGroupStore m_sceneObjectGroupStore;
    Basics::SceneEntityStore* m_sceneEntityStore = nullptr;      // Borrowed scene-lifetime metadata owner.
    Physics::PhysicsEngine m_physicsEngine;
    Rendering::RenderInstanceStore m_renderInstanceStore;        // Render snapshot in scene/model order, owned outside physics.
    Threading::WorkerPool* m_workerPool = nullptr;               // Borrowed startup worker pool for render/physics parallel helpers.
    int m_activeGameModelCapacity = DEFAULT_GAME_MODEL_CAPACITY; // Configured model cap used by append/reserve guards.
    bool m_renderCollisionVolumes = false;                       // Cached render debug toggle copied from EngineConfig.
    bool m_shadowParallelPrep = false;                           // Cached worker-prep toggle copied from EngineConfig.
    void ReserveForActiveGameModelCapacity();
    Basics::SbResult BuildSceneObjectGroupForAppend( int newModelIndex,
                                                     SceneObjectGroupCreateDesc groupDesc,
                                                     SceneObjectGroupRecord& outGroup );
    SceneObjectGroupRecord GroupRecordAt( int modelIndex ) const;
    // Owner boundary: fixed-tree grouping is collection metadata. Body-store
    // import receives only the scalar root, never collection-kind accessors.
    std::vector<Physics::ModelRowHint> BuildFixedTreeReleaseRootsForReload() const;
    std::vector<const char*> BuildDiagnosticNamesForReload() const;
    bool RefreshPhysicsBodyStoreFromAuthoredDescriptors();
    // Private body-only repair is reserved for collection-owned projection
    // phases. Public tool/runtime reads must use an explicit owner boundary
    // before borrowing PhysicsEngine store views.
    bool RepairPhysicsBodyTopology();
    int FixedTreeReleaseRootForModelIndex( int modelIndex ) const;
    void RefreshRenderInstances();
    Basics::SceneEntityStore& SceneEntities();
    const Basics::SceneEntityStore& SceneEntities() const;
    void AssertSceneCreationTopology( int expectedCount ) const;

  public:
    GameModelCollection();
    ~GameModelCollection() = default;

    void BindWorkerPool( Threading::WorkerPool& workerPool );
    void BindSceneEntityStore( Basics::SceneEntityStore& entities );
    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    bool ShouldRenderCollisionVolumes() const;
    bool ShouldUseShadowParallelPrep() const;
    Threading::WorkerPool* RenderWorkerPool() const;
    // One preflighted scene-creation command publishes metadata, physics, and
    // render rows together. Lane R input failures leave every owner unchanged;
    // a mismatched owner count is a fatal topology invariant.
    SceneEntityCreateResult TryCreateSceneEntity( Basics::SceneEntityCreateDesc entity,
                                                  Physics::PhysicsBodyCreateDesc bodyDesc,
                                                  Physics::PhysicsColliderCreateDesc colliderDesc,
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
    // Legacy object-follow cameras can outlive the model slots they track.
    // Returns false only for an absent slot; a present model without a body is
    // store-topology drift and still fails through the fatal invariant lane.
    bool TryGetModelPosition( int index, Math::Vector::Vector3& outPosition ) const;
    // Scene entity count is the stable model-slot count shared by scene files,
    // editor picks, replay streams, and cold owner-repair boundaries.
    int SceneEntityCount() const;
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
    // PhysicsEngine owns body/collider store views. Callers that can observe
    // topology drift must first run the explicit topology-repair command below.
    Physics::PhysicsEngine& GetPhysicsEngine();
    const Physics::PhysicsEngine& GetPhysicsEngine() const;
    // Explicit cold owner boundary before tool or picker code asks for body
    // handles and collider bounds. Read-only store accessors do not repair.
    bool RepairPhysicsBodyAndColliderTopology();
    // Current prepared collider snapshot. Hot render passes use this after
    // PrepareRenderInstances() instead of invoking topology repair mid-submit.
    const Physics::PhysicsBodyStore& BodyStore() const;
    const Physics::ColliderStore& Colliders() const;
    // Current prepared render snapshot. Call PrepareRenderInstances() before frame
    // passes; cold callers that need an ensured snapshot use GetRenderInstanceStore().
    Rendering::RenderInstanceStore& MutableRenderInstances();
    const Rendering::RenderInstanceStore& RenderInstances() const;
    // Replay presentation samples are one-frame render overrides. The collection
    // validates replay body identity before mutating its render snapshot so scrub
    // and prediction code cannot redirect stale model slots.
    bool TryQueueReplayRenderPoseOverride( int modelIndex,
                                           uint32_t replayBodyId,
                                           const Math::Vector::Vector3& position,
                                           const Math::Orientation::Quaternion& orientation );
    const std::vector<Rendering::RenderInstancePresentationRecord>& RenderPresentationRecords() const
    {
        return m_renderInstanceStore.PresentationRecords();
    }
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
    // Packages collision-store views for solid state rendering; caller supplies
    // frame-owned renderer capabilities so debug drawing cannot reopen globals.
    void RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                     Assets::AssetSystem& assets,
                                     Rendering::IRenderResourceFactory& renderResources,
                                     Rendering::IRenderCommandContext& renderCommands,
                                     Rendering::IRenderDiagnostics& renderDiagnostics,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const float lightPos[4],
                                     float alphaOverride );
    // Packages physics store views for debug drawing; caller supplies the
    // frame-owned renderer command context and debug-line capability.
    void RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                             const Math::Transformation::Matrix4& viewProjection,
                             Rendering::IRenderCommandContext& renderCommands,
                             bool supportsDebugLines,
                             Geometry::Terrain* terrain );
    // Borrowed debug/diagnostics views over physics-owned dense rows. Callers
    // must not cache them beyond the frame/tool operation that requested them.
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const
    {
        return Physics::PhysicsEngineStoreQueries::SpatialGrid( m_physicsEngine );
    }
    const std::vector<int64_t>& GetCollisionCellKeys() const
    {
        return Physics::PhysicsEngineStoreQueries::CollisionCellKeys( m_physicsEngine );
    }
    const std::vector<uint8_t>& GetCollisionVisualContacts() const
    {
        return Physics::PhysicsEngineStoreQueries::CollisionVisualContacts( m_physicsEngine );
    }
    const std::vector<uint8_t>& GetSleepStates() const
    {
        return Physics::PhysicsEngineStoreQueries::SleepStates( m_physicsEngine );
    }
    const std::vector<int>& GetSleepIslandVisualIds() const
    {
        return Physics::PhysicsEngineStoreQueries::SleepIslandVisualIds( m_physicsEngine );
    }
    const std::vector<uint8_t>& GetSleepSupportedStates() const
    {
        return Physics::PhysicsEngineStoreQueries::SleepSupportedStates( m_physicsEngine );
    }
    const std::vector<uint8_t>& GetSleepInhibitedStates() const
    {
        return Physics::PhysicsEngineStoreQueries::SleepInhibitedStates( m_physicsEngine );
    }
    const std::vector<Physics::PhysicsDebugContact>& GetPhysicsDebugContacts() const
    {
        return Physics::PhysicsEngineStoreQueries::DebugContacts( m_physicsEngine );
    }
    const std::vector<Physics::PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const
    {
        return Physics::PhysicsEngineStoreQueries::PipelineTrace( m_physicsEngine );
    }
    const std::vector<Physics::PointJointConstraint>& GetPointJointConstraints() const
    {
        return Physics::PhysicsEngineStoreQueries::PointJointConstraints( m_physicsEngine );
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

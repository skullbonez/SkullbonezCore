/*
File: SkullbonezSource/GameObjects/GameModelCollection.cpp
Purpose:
  Coordinates transient presentation rows with physics/collider/render stores.

Mental model:
  SceneController owns durable SceneEntityStore rows. This collection borrows
  that owner while coordinating the currently co-located transient, physics,
  and render stores. Creation preflights every owner before its first mutation;
  A2 separately moves physical physics ownership out of this type.

Glossary:
  Physics material: Per-object friction and drag coefficients owned by
    PhysicsScene and copied into authored descriptor rows at cold boundaries.
  Body simulation limit: Scalar cap owned by PhysicsScene before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds owned by PhysicsScene so
    existing and newly added models receive the same physics policy.
  Body descriptor: PhysicsScene-owned authoring value that can rebuild a live
    PhysicsBodyStore row without reading GameModel physics fields.
  Render instance store: Renderer-facing snapshot built from physics-owned pose
    and render-owned presentation rows before frame passes.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsScene turns into a live ColliderStore row.
  Topology drift: A body/collider/model count mismatch that means stores must
    import explicit construction descriptors before stepping.
  Scene-object group: Cold metadata that maps multi-part authored objects, such
    as ragdolls or releasable trees, to a stable root scene object id.
  Fixed-tree release: Authored scene rule that lets tree parts become dynamic
    when a related fixed part is hit strongly enough.
  Replay body id: PhysicsBodyStore-owned identity saved in replay samples so
    restore paths can reject stale model slots.

Invariants:
  - SceneEntityStore order remains the scene alignment key for physics stores,
    render batches, and scene snapshots. Replay ids live in PhysicsBodyStore
    rows after append.
  - SceneEntityStore owns behavior groups with stable root ids. Dense root rows
    are derived only when physics or a model-index compatibility API requires one.
  - Render prep imports store-backed snapshots once before frame passes; render
    code must not rebuild model-derived pose streams.
  - Owner-side release paths repair topology once before resolving body handles
    from PhysicsBodyStore.
  - RenderInstanceStore imports material/name from SceneEntityStore and contact
    highlight alpha from transient GameModel rows.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelCollection.h"

#include "../Core/FatalError.h"
#include "../Core/MainMemoryStats.h"
#include "../Core/SkullScope.h"
#include "../Runtime/Debug/CollisionVisualizer.h"
#include "../Runtime/Debug/PhysicsDebugVisualizer.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Rendering/GameModelRenderer.h"
#include "../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <utility>
#include <variant>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakeModelRowHint;
using SkullbonezCore::Physics::MakePhysicsAuthoredBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt;
using SkullbonezCore::Physics::MakePhysicsColliderCountFromNonNegativeInt;
using SkullbonezCore::Physics::ModelRowHint;
using SkullbonezCore::Physics::PhysicsAuthoredBodyCount;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRefreshView;
using SkullbonezCore::Physics::PhysicsBodyCount;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCount;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsEngineStoreQueries;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr const char* SCENE_ENTITY_CREATION_OWNER = "Scene/EntityCreation";

template <typename T> uint64_t VectorCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}


void RefreshBodyDescFromStoreBodyState( const PhysicsBodyRecord& record,
                                        const PhysicsBodyStateEdit& edit,
                                        PhysicsBodyCreateDesc& desc,
                                        int fixedTreeReleaseRootIndex )
{
    desc.sceneObjectId = record.sceneObjectId;
    desc.position = edit.hasPosition ? edit.position : record.position;
    desc.orientation = edit.hasOrientation ? edit.orientation : record.orientation;
    desc.linearVelocity = edit.hasLinearVelocity ? edit.linearVelocity : record.linearVelocity;
    desc.angularVelocity = edit.hasAngularVelocity ? edit.angularVelocity : record.angularVelocity;
    desc.rotationalInertia = record.rotationalInertia;
    desc.mass = record.mass;
    desc.motionKind = record.isFixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
    desc.fixedTreeReleaseRootIndex = fixedTreeReleaseRootIndex;
}


} // namespace


GameModelCollection::GameModelCollection()
{
    ReserveForActiveGameModelCapacity();
}


void GameModelCollection::PresentationStore::Reserve( std::size_t capacity )
{
    m_records.reserve( capacity );
}


void GameModelCollection::PresentationStore::Clear()
{
    m_records.clear();
}


void GameModelCollection::PresentationStore::Append( GameModel model )
{
    m_records.push_back( std::move( model ) );
}


bool GameModelCollection::PresentationStore::TrimToCount( int count )
{
    if ( count < 0 || count > static_cast<int>( m_records.size() ) )
    {
        return false;
    }

    const std::size_t targetCount = static_cast<std::size_t>( count );
    if ( targetCount < m_records.size() )
    {
        m_records.erase( m_records.begin() + static_cast<std::ptrdiff_t>( targetCount ), m_records.end() );
    }
    return true;
}


int GameModelCollection::PresentationStore::Count() const
{
    return static_cast<int>( m_records.size() );
}


std::size_t GameModelCollection::PresentationStore::Capacity() const
{
    return m_records.capacity();
}


uint64_t GameModelCollection::PresentationStore::CapacityBytes() const
{
    return VectorCapacityBytes( m_records );
}


const std::vector<GameModel>& GameModelCollection::PresentationStore::Records() const
{
    return m_records;
}


GameModel& GameModelCollection::PresentationStore::MutableAt( int index )
{
    return m_records[static_cast<std::size_t>( index )];
}


const GameModel* GameModelCollection::PresentationStore::TryGet( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_records.size() ) )
    {
        return nullptr;
    }
    return &m_records[static_cast<std::size_t>( index )];
}


void GameModelCollection::ReserveForActiveGameModelCapacity()
{
    // Invariant: model-order storage must be fully sized before steady frames.
    // Config can raise the active model capacity after construction, so each
    // setup/config boundary repeats the reserve instead of letting render-time
    // append paths discover the new capacity by reallocating.
    const std::size_t capacity = static_cast<std::size_t>( m_activeGameModelCapacity );
    m_presentations.Reserve( capacity );
    m_physicsEngine.ReserveAuthoredBodyCapacity( capacity );
    m_renderInstanceStore.ReservePresentationCapacity( capacity );
}


const SceneBehaviorGroup& GameModelCollection::BehaviorGroupAt( int modelIndex ) const
{
    return SceneEntities().At( modelIndex ).behaviorGroup;
}


int GameModelCollection::ResolveBehaviorGroupRootModelIndex( const SceneBehaviorGroup& group ) const
{
    if ( group.kind == SceneBehaviorGroupKind::None )
    {
        return -1;
    }
    // Why: physics descriptors and compatibility APIs still consume dense rows,
    // but the scene owner stores only stable identity. Resolve at this cold
    // boundary instead of caching a movable root row in group metadata.
    const int rootIndex = SceneEntities().FindBySceneObjectId( group.rootObjectId );
    if ( rootIndex < 0 )
    {
        SB_FATAL( "GameObjects/GameModelCollection",
                  "Behavior group root is missing. root_id=%u kind=%u",
                  group.rootObjectId.value,
                  static_cast<unsigned int>( group.kind ) );
    }
    return rootIndex;
}


std::vector<ModelRowHint> GameModelCollection::BuildFixedTreeReleaseRootsForReload() const
{
    std::vector<ModelRowHint> fixedTreeReleaseRoots;
    const int sceneEntityCount = SceneEntityCount();
    fixedTreeReleaseRoots.reserve( static_cast<std::size_t>( sceneEntityCount ) );
    for ( int i = 0; i < sceneEntityCount; ++i )
    {
        fixedTreeReleaseRoots.push_back( MakeModelRowHint( FixedTreeReleaseRootForModelIndex( i ) ) );
    }
    return fixedTreeReleaseRoots;
}


std::vector<const char*> GameModelCollection::BuildDiagnosticNamesForReload() const
{
    std::vector<const char*> diagnosticNames;
    diagnosticNames.reserve( static_cast<std::size_t>( SceneEntities().Count() ) );
    for ( int index = 0; index < SceneEntities().Count(); ++index )
    {
        diagnosticNames.push_back( SceneEntities().At( index ).displayName );
    }
    return diagnosticNames;
}


bool GameModelCollection::RefreshPhysicsBodyStoreFromAuthoredDescriptors()
{
    const std::vector<uint32_t> replayBodyIds =
        PhysicsEngineStoreQueries::BodyStore( m_physicsEngine ).BuildReplayBodyIdsForReload( SceneEntityCount() );
    const std::vector<ModelRowHint> fixedTreeReleaseRoots = BuildFixedTreeReleaseRootsForReload();
    const std::vector<const char*> diagnosticNames = BuildDiagnosticNamesForReload();
    PhysicsAuthoredBodyRefreshView refreshView;
    refreshView.replayBodyIds = replayBodyIds.empty() ? nullptr : replayBodyIds.data();
    refreshView.fixedTreeReleaseRoots = fixedTreeReleaseRoots.empty() ? nullptr : fixedTreeReleaseRoots.data();
    refreshView.diagnosticNames = diagnosticNames.empty() ? nullptr : diagnosticNames.data();
    refreshView.bodyCount = MakePhysicsAuthoredBodyCountFromNonNegativeInt( SceneEntityCount() );
    if ( !m_physicsEngine.RefreshBodyStoreFromAuthoredDescriptors( refreshView ) )
    {
        return false;
    }
    const PhysicsBodyStore& bodyStore = BodyStore();
    for ( int index = 0; index < bodyStore.Count(); ++index )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( index );
        if ( !body )
        {
            SB_FATAL( "GameObjects/GameModelCollection", "Refreshed body row is missing." );
        }
        SceneEntities().UpdateBodyHandleAt( index, body->handle, body->sceneObjectId );
    }
    return true;
}


int GameModelCollection::FixedTreeReleaseRootForModelIndex( int modelIndex ) const
{
    const SceneBehaviorGroup& group = BehaviorGroupAt( modelIndex );
    return group.kind == SceneBehaviorGroupKind::ReleasableTree ? ResolveBehaviorGroupRootModelIndex( group ) : -1;
}


void GameModelCollection::BindWorkerPool( SkullbonezCore::Threading::WorkerPool& workerPool )
{
    m_workerPool = &workerPool;
}


void GameModelCollection::BindSceneEntityStore( SceneEntityStore& entities )
{
    if ( SceneEntityCount() != 0 || entities.Count() != 0 )
    {
        SB_FATAL( "GameObjects/GameModelCollection", "Scene entity owner must bind before model creation." );
    }
    m_sceneEntityStore = &entities;
}


SceneEntityStore& GameModelCollection::SceneEntities()
{
    if ( !m_sceneEntityStore )
    {
        SB_FATAL( "GameObjects/GameModelCollection", "Scene entity owner is not bound." );
    }
    return *m_sceneEntityStore;
}


const SceneEntityStore& GameModelCollection::SceneEntities() const
{
    if ( !m_sceneEntityStore )
    {
        SB_FATAL( "GameObjects/GameModelCollection", "Scene entity owner is not bound." );
    }
    return *m_sceneEntityStore;
}


void GameModelCollection::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_activeGameModelCapacity = ActiveGameModelCapacity( config );
    SceneEntities().ConfigureCapacity( m_activeGameModelCapacity );
    ReserveForActiveGameModelCapacity();
    m_renderCollisionVolumes = config.runtimeRender.renderCollisionVolumes;
    m_shadowParallelPrep = config.shadowParallelPrep;
    m_physicsEngine.ApplyRuntimeConfig( config );
}


bool GameModelCollection::ShouldRenderCollisionVolumes() const
{
    return m_renderCollisionVolumes;
}


bool GameModelCollection::ShouldUseShadowParallelPrep() const
{
    return m_shadowParallelPrep;
}


SkullbonezCore::Threading::WorkerPool* GameModelCollection::RenderWorkerPool() const
{
    return m_workerPool;
}


void GameModelCollection::AssertSceneCreationTopology( int expectedCount ) const
{
    const int descriptorCount = static_cast<int>( m_physicsEngine.AuthoredBodyDescriptorCount().value );
    const int bodyCount = BodyStore().Count();
    const int colliderCount = Colliders().Count();
    const int presentationCount = m_presentations.Count();
    const int renderPresentationCount = m_renderInstanceStore.PresentationCount();
    const int renderCount = m_renderInstanceStore.Count();
    const bool reservationsReady = m_presentations.Capacity() >= static_cast<std::size_t>( m_activeGameModelCapacity );
    if ( expectedCount < 0 || SceneEntities().Count() != expectedCount || presentationCount != expectedCount ||
         descriptorCount != expectedCount || bodyCount != expectedCount || colliderCount != expectedCount ||
         renderPresentationCount != expectedCount || renderCount != expectedCount || !reservationsReady )
    {
        SB_FATAL( "GameObjects/GameModelCollection",
                  "Scene creation topology diverged. expected=%d entities=%d transient=%d descriptors=%d "
                  "bodies=%d colliders=%d render_presentation=%d render=%d reservations_ready=%d",
                  expectedCount,
                  SceneEntities().Count(),
                  presentationCount,
                  descriptorCount,
                  bodyCount,
                  colliderCount,
                  renderPresentationCount,
                  renderCount,
                  reservationsReady ? 1 : 0 );
    }
}


SceneEntityCreateResult GameModelCollection::TryCreateSceneEntity( SceneEntityCreateDesc entity,
                                                                   PhysicsBodyCreateDesc bodyDesc,
                                                                   PhysicsColliderCreateDesc colliderDesc )
{
    const int activeCapacity = m_activeGameModelCapacity;
    const int modelIndex = SceneEntityCount();
    AssertSceneCreationTopology( modelIndex );
    if ( SceneEntityCount() >= activeCapacity )
    {
        return {
            SbResult::Failure( SCENE_ENTITY_CREATION_OWNER,
                               "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." ),
            PhysicsBodyHandle{} };
    }
    const SbResult entityResult = SceneEntities().PreflightAppend( entity );
    if ( !entityResult.ok )
    {
        return { entityResult, PhysicsBodyHandle{} };
    }
    if ( bodyDesc.sceneObjectId.IsValid() && bodyDesc.sceneObjectId.value != entity.sceneObjectId.value )
    {
        return { SbResult::Failure( SCENE_ENTITY_CREATION_OWNER,
                                    "Body scene object id %u does not match entity id %u.",
                                    bodyDesc.sceneObjectId.value,
                                    entity.sceneObjectId.value ),
                 PhysicsBodyHandle{} };
    }
    if ( !m_physicsEngine.CanRegisterAuthoredBody( MakePhysicsAuthoredBodyCountFromNonNegativeInt( modelIndex ) ) )
    {
        SB_FATAL( "GameObjects/GameModelCollection",
                  "Physics creation storage is not preflight-ready. expected=%d descriptors=%u bodies=%d",
                  modelIndex,
                  m_physicsEngine.AuthoredBodyDescriptorCount().value,
                  BodyStore().Count() );
    }
    if ( !m_renderInstanceStore.CanAppendCreationRow( modelIndex ) )
    {
        SB_FATAL( "GameObjects/GameModelCollection",
                  "Render creation storage is not preflight-ready. expected=%d presentation=%d render=%d",
                  modelIndex,
                  m_renderInstanceStore.PresentationCount(),
                  m_renderInstanceStore.Count() );
    }

    Rendering::RenderInstancePresentationRecord renderPresentation;
    renderPresentation.material = entity.renderMaterial;
    strncpy_s( renderPresentation.displayName,
               sizeof( renderPresentation.displayName ),
               entity.displayName,
               _TRUNCATE );
    renderPresentation.simpleRagdollPart = entity.behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;

    // Invariant: every recoverable check is complete before the first mutation.
    // The following owner appends use fixed or pre-reserved storage; any count or
    // identity mismatch is Lane F because partial topology cannot be recovered.
    bodyDesc.sceneObjectId = entity.sceneObjectId;
    bodyDesc.fixedTreeReleaseRootIndex = entity.behaviorGroup.kind != SceneBehaviorGroupKind::ReleasableTree
                                             ? -1
                                             : ( entity.behaviorGroup.rootObjectId.value == entity.sceneObjectId.value
                                                     ? modelIndex
                                                     : ResolveBehaviorGroupRootModelIndex( entity.behaviorGroup ) );
    // Lifetime: authored descriptors outlive this value-local entity packet.
    // Diagnostics receive stable SceneEntityStore names through the explicit
    // refresh/step view, so never retain the packet's displayName pointer.
    bodyDesc.diagnosticName = nullptr;
    m_presentations.Append( GameModel{} );
    const PhysicsBodyHandle bodyHandle = m_physicsEngine.RegisterAuthoredBody( bodyDesc );
    const PhysicsBodyRecord* bodyRecord = BodyStore().RecordForHandle( bodyHandle );
    if ( !bodyRecord )
    {
        // Invariant: RegisterAuthoredBody returns the handle for the row it just
        // appended. A failed immediate lookup means collection/body-store
        // topology diverged after mutation.
        SB_FATAL( "GameObjects/GameModelCollection", "Failed to resolve newly authored physics body record." );
    }
    // Owner: creation callers provide shape facts; PhysicsScene owns live
    // collider rows. Reason: radius/extents/hull values exist before append, so
    // recapturing them from GameModel preserves old ownership and extra work.
    // Transaction API: callers cannot create an entity without supplying the
    // collider descriptor owned by the same preflight.
    PhysicsColliderCreateDesc authoredCollider = std::move( colliderDesc );
    authoredCollider.body = bodyRecord->handle;
    authoredCollider.sceneObjectId = bodyRecord->sceneObjectId;
    m_physicsEngine.ApplyAuthoredColliderPolicy( authoredCollider );
    const auto colliderHandle = m_physicsEngine.RegisterAuthoredCollider( authoredCollider );
    const ColliderRecord* colliderRecord = Colliders().RecordForHandle( colliderHandle );
    if ( !colliderHandle.IsValid() || !colliderRecord )
    {
        // Invariant: collider registration is the physics half of the creation
        // transaction. No collider handle means owner topology diverged after
        // preflight and cannot safely enter physics or render snapshots.
        SB_FATAL( "GameObjects/GameModelCollection", "Failed to register newly authored physics collider record." );
    }
    SceneEntities().CommitAppend( entity, bodyHandle );
    m_renderInstanceStore.CommitCreationRow( renderPresentation, *bodyRecord, *colliderRecord, modelIndex );
    AssertSceneCreationTopology( modelIndex + 1 );
    return { SbResult::Success(), bodyHandle };
}


void GameModelCollection::Clear()
{
    m_presentations.Clear();
    SceneEntities().Clear();
    m_physicsEngine.Clear();
    m_renderInstanceStore.Clear();
    AssertSceneCreationTopology( 0 );
}


void GameModelCollection::PrepareRenderInstances()
{
    // Why: object rendering now reads the render instance store for transforms.
    // Preparing it once here prevents each render pass from re-importing the
    // same physics pose repeatedly.
    RefreshRenderInstances();
}


int GameModelCollection::CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount )
{
    if ( !outMatrixFloats || maxModelCount <= 0 )
    {
        return 0;
    }

    const int sceneEntityCount = SceneEntityCount();
    if ( m_renderInstanceStore.Count() != sceneEntityCount )
    {
        // Hazard: normal render frames call PrepareRenderInstances() first. This
        // cold path keeps standalone DXR callers on the render-instance
        // authority instead of reintroducing model-side pose reconstruction.
        RefreshRenderInstances();
    }

    const std::vector<Rendering::RenderInstanceRecord>& instances = m_renderInstanceStore.Records();
    const int modelCount = (std::min)( static_cast<int>( instances.size() ), maxModelCount );
    for ( int i = 0; i < modelCount; ++i )
    {
        const Matrix4& modelMatrix = instances[static_cast<std::size_t>( i )].modelMatrix;
        memcpy( outMatrixFloats + static_cast<std::size_t>( i ) * 16u, modelMatrix.Data(), 16u * sizeof( float ) );
    }
    return modelCount;
}


void GameModelCollection::RenderModels( const RenderHelperContext& helperContext,
                                        const Matrix4& view,
                                        const Matrix4& proj,
                                        const float lightPos[4],
                                        const CinematicRenderConfig* cinematic,
                                        const ShadowFrameData* shadow,
                                        float materialAlpha,
                                        const std::vector<uint8_t>* modelMask,
                                        bool drawMaskedModels )
{
    GameModelRenderer::RenderModels( helperContext,
                                     RenderInstances(),
                                     Colliders(),
                                     ShouldRenderCollisionVolumes(),
                                     view,
                                     proj,
                                     lightPos,
                                     cinematic,
                                     shadow,
                                     materialAlpha,
                                     modelMask,
                                     drawMaskedModels );
}


void GameModelCollection::BuildShadowCasterBatches( Rendering::ShadowCasterBatches& outBatches )
{
    GameModelRenderer::BuildShadowCasterBatches( RenderInstances(),
                                                 Colliders(),
                                                 RenderWorkerPool(),
                                                 ShouldUseShadowParallelPrep(),
                                                 outBatches );
}


void GameModelCollection::RenderShadowCasterBatches( const RenderHelperContext& helperContext,
                                                     const Rendering::ShadowCasterBatches& batches,
                                                     const Matrix4& view,
                                                     const Matrix4& proj,
                                                     const CinematicRenderConfig* cinematic )
{
    GameModelRenderer::SubmitShadowCasterBatches( helperContext, batches, view, proj, cinematic );
}


void GameModelCollection::RenderShadowCasters( const RenderHelperContext& helperContext,
                                               const Matrix4& view,
                                               const Matrix4& proj,
                                               const CinematicRenderConfig* cinematic )
{
    GameModelRenderer::RenderShadowCasters( helperContext,
                                            RenderInstances(),
                                            Colliders(),
                                            RenderWorkerPool(),
                                            ShouldUseShadowParallelPrep(),
                                            view,
                                            proj,
                                            cinematic );
}


void GameModelCollection::UpdateCollisionVisualizer( Physics::CollisionVisualizer& visualizer, float deltaSeconds )
{
    const Physics::CollisionVisualizerFrameView view{
        BodyStore(),
        Colliders(),
        m_renderInstanceStore,
        GetCollisionVisualContacts(),
        GetSleepStates(),
        GetSleepIslandVisualIds(),
        BodyStore().Count(),
    };
    visualizer.Update( deltaSeconds, view );
}


void GameModelCollection::UpdatePhysicsDebugVisualizer( Physics::PhysicsDebugVisualizer& visualizer,
                                                        float deltaSeconds )
{
    const Physics::PhysicsDebugFrameView view{
        BodyStore(),
        Colliders(),
        GetSleepStates(),
        GetSleepSupportedStates(),
        GetSleepInhibitedStates(),
        GetPhysicsDebugContacts(),
        GetPhysicsPipelineTrace(),
        BodyStore().Count(),
    };
    visualizer.Update( deltaSeconds, view );
}


void GameModelCollection::RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                                      Assets::AssetSystem& assets,
                                                      Rendering::IRenderResourceFactory& renderResources,
                                                      Rendering::IRenderCommandContext& renderCommands,
                                                      Rendering::IRenderDiagnostics& renderDiagnostics,
                                                      const Matrix4& view,
                                                      const Matrix4& proj,
                                                      const float lightPos[4],
                                                      float alphaOverride )
{
    const Physics::CollisionVisualizerFrameView frameView{
        BodyStore(),
        Colliders(),
        m_renderInstanceStore,
        GetCollisionVisualContacts(),
        GetSleepStates(),
        GetSleepIslandVisualIds(),
        BodyStore().Count(),
    };
    visualizer.SetAlphaOverride( alphaOverride );
    visualizer.Render( assets, renderResources, renderCommands, renderDiagnostics, frameView, view, proj, lightPos );
    visualizer.SetAlphaOverride( -1.0f );
}


void GameModelCollection::RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                                              const Matrix4& viewProjection,
                                              Rendering::IRenderCommandContext& renderCommands,
                                              bool supportsDebugLines,
                                              Geometry::Terrain* terrain )
{
    const Physics::PhysicsDebugFrameView frameView{
        BodyStore(),
        Colliders(),
        GetSleepStates(),
        GetSleepSupportedStates(),
        GetSleepInhibitedStates(),
        GetPhysicsDebugContacts(),
        GetPhysicsPipelineTrace(),
        BodyStore().Count(),
    };
    // Caller contract: runtime render passes own renderer readiness for the
    // frame; this collection only packages the physics store view.
    visualizer.Render( frameView, viewProjection, renderCommands, supportsDebugLines, terrain );
}


bool GameModelCollection::GetObjectShadowBounds( const Vector3& focus,
                                                 float maxDistance,
                                                 Vector3& outCenter,
                                                 float& outRadius,
                                                 float& outHeightRange )
{
    return GameModelRenderer::GetObjectShadowBounds( RenderInstances(),
                                                     RenderWorkerPool(),
                                                     ShouldUseShadowParallelPrep(),
                                                     focus,
                                                     maxDistance,
                                                     outCenter,
                                                     outRadius,
                                                     outHeightRange );
}


void GameModelCollection::ResetRenderResources()
{
    GameModelRenderer::ResetRenderResources();
}


bool GameModelCollection::TryGetModelPosition( int index, Vector3& outPosition ) const
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    // Why: object-follow cameras should read the same store-owned pose that
    // physics, diagnostics, replay capture, and render snapshots consume. A
    // missing model slot is a recoverable legacy-camera request, so callers
    // keep their previous target instead of aborting the frame.
    const PhysicsBodyStore& bodyStore = BodyStore();
    const PhysicsBodyRecord* record = bodyStore.RecordForModelIndex( index );
    if ( !record )
    {
        // Invariant: a live scene model must have a physics body row at the
        // same slot. Missing rows are topology drift, not recoverable camera
        // input, because rendering/replay snapshots would read divergent state.
        SB_FATAL( "GameObjects/GameModelCollection",
                  "No physics body exists at the specified index.  (GameModelCollection::TryGetModelPosition)" );
    }
    outPosition = record->position;
    return true;
}


int GameModelCollection::SceneEntityCount() const
{
    return m_presentations.Count();
}


const std::vector<GameModel>& GameModelCollection::Models() const
{
    return m_presentations.Records();
}


const GameModel* GameModelCollection::TryGetModel( int index ) const
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return nullptr;
    }

    return m_presentations.TryGet( index );
}


SceneBehaviorGroupKind GameModelCollection::GroupKindAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).kind;
}


PhysicsSceneObjectId GameModelCollection::GroupRootObjectIdAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).rootObjectId;
}


int GameModelCollection::GroupPartIndexAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).partIndex;
}


bool GameModelCollection::IsSimpleRagdollPart( int modelIndex ) const
{
    return GroupKindAt( modelIndex ) == SceneBehaviorGroupKind::SimpleRagdoll;
}


bool GameModelCollection::IsSimpleRagdollTorso( int modelIndex ) const
{
    return IsSimpleRagdollPart( modelIndex ) && GroupPartIndexAt( modelIndex ) == 0;
}


int GameModelCollection::RagdollRootModelIndexForPart( int modelIndex ) const
{
    const SceneBehaviorGroup& group = BehaviorGroupAt( modelIndex );
    if ( group.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        return modelIndex;
    }

    const int rootIndex = ResolveBehaviorGroupRootModelIndex( group );
    if ( IsSimpleRagdollPart( rootIndex ) )
    {
        return rootIndex;
    }
    return modelIndex;
}


bool GameModelCollection::TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const
{
    outModelIndex = -1;
    if ( !IsSimpleRagdollPart( selectedModelIndex ) )
    {
        return false;
    }

    const PhysicsSceneObjectId rootObjectId = GroupRootObjectIdAt( selectedModelIndex );
    for ( int i = 0; i < SceneEntityCount(); ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );
        if ( group.kind == SceneBehaviorGroupKind::SimpleRagdoll && group.rootObjectId.value == rootObjectId.value &&
             group.partIndex == partIndex )
        {
            outModelIndex = i;
            return true;
        }
    }
    return false;
}


int GameModelCollection::GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const
{
    if ( outIndices && maxIndices > 0 )
    {
        for ( int i = 0; i < maxIndices; ++i )
        {
            outIndices[i] = -1;
        }
    }
    if ( !outIndices || maxIndices <= 0 || selectedModelIndex < 0 || selectedModelIndex >= SceneEntityCount() )
    {
        return 0;
    }

    const SceneBehaviorGroup& selectedGroup = BehaviorGroupAt( selectedModelIndex );
    if ( selectedGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    (void)ResolveBehaviorGroupRootModelIndex( selectedGroup );

    int count = 0;
    for ( int i = 0; i < SceneEntityCount() && count < maxIndices; ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );
        if ( group.kind == selectedGroup.kind && group.rootObjectId.value == selectedGroup.rootObjectId.value )
        {
            outIndices[count] = i;
            ++count;
        }
    }

    if ( count <= 0 )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }
    return count;
}


#ifdef _DEBUG
bool GameModelCollection::TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    outName = SceneEntities().At( index ).displayName;
    return true;
}


void GameModelCollection::FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const
{
    const int clampedBodyCount = (std::max)( 0, bodyCount );
    outNames.assign( static_cast<std::size_t>( clampedBodyCount ), "" );
    const int copyCount = (std::min)( clampedBodyCount, SceneEntityCount() );
    for ( int i = 0; i < copyCount; ++i )
    {
        // Lifetime: these are borrowed display-name pointers for the current
        // Debug diagnostics write; the caller owns only the pointer table.
        outNames[static_cast<std::size_t>( i )] = SceneEntities().At( i ).displayName;
    }
}


#endif


bool GameModelCollection::TryRestoreReplayBodyState( int index,
                                                     uint32_t replayBodyId,
                                                     bool fixed,
                                                     const Vector3& position,
                                                     const Quaternion& orientation,
                                                     const Vector3& linearVelocity,
                                                     const Vector3& angularVelocity,
                                                     float mass,
                                                     float inverseMass,
                                                     const Vector3& rotationalInertia,
                                                     const Vector3& inverseRotationalInertia )
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    // Invariant: model index verifies the presentation slot only. The current
    // body handle and replay id must prove the live physics row before either
    // side of the restore mutates.
    const PhysicsBodyStore& bodyStore = BodyStore();
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( index );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    if ( !bodyRecord || bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    if ( !m_physicsEngine.RestoreReplayBodyState( body,
                                                  replayBodyId,
                                                  fixed,
                                                  position,
                                                  orientation,
                                                  linearVelocity,
                                                  angularVelocity,
                                                  mass,
                                                  inverseMass,
                                                  rotationalInertia,
                                                  inverseRotationalInertia ) )
    {
        return false;
    }

    PhysicsBodyCreateDesc desc;
    const ModelRowHint bodyRow = MakeModelRowHint( index );
    if ( m_physicsEngine.TryGetAuthoredBodyDescriptor( bodyRow, desc ) )
    {
        desc.sceneObjectId = bodyRecord->sceneObjectId;
        desc.position = position;
        desc.orientation = orientation;
        desc.linearVelocity = linearVelocity;
        desc.angularVelocity = angularVelocity;
        desc.rotationalInertia = rotationalInertia;
        desc.mass = mass;
        desc.motionKind = fixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
        desc.diagnosticName = SceneEntities().At( index ).displayName;
        desc.fixedTreeReleaseRootIndex = FixedTreeReleaseRootForModelIndex( index );
        const PhysicsAuthoredBodyCount expectedBodyCount =
            MakePhysicsAuthoredBodyCountFromNonNegativeInt( SceneEntityCount() );
        if ( !m_physicsEngine.UpdateAuthoredBodyDescriptor( bodyRow, desc, expectedBodyCount ) )
        {
            return false;
        }
    }
    return true;
}


bool GameModelCollection::TryRestoreReplayPredictionBodyState( int index,
                                                               uint32_t replayBodyId,
                                                               bool fixed,
                                                               const Vector3& position,
                                                               const Quaternion& orientation,
                                                               const Vector3& linearVelocity,
                                                               const Vector3& angularVelocity,
                                                               float mass,
                                                               float inverseMass,
                                                               const Vector3& rotationalInertia,
                                                               const Vector3& inverseRotationalInertia,
                                                               float fixedContactHighlightSeconds )
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    // Invariant: model index verifies the presentation slot only. The current
    // body handle and replay id must prove the live physics row before either
    // side of the restore mutates.
    const PhysicsBodyStore& bodyStore = BodyStore();
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( index );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    if ( !bodyRecord || bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    // Why: prediction restore swaps live/job state repeatedly. Restore the
    // physics record from the captured backup instead of rebuilding from a
    // presentation row.
    if ( !m_physicsEngine.RestoreReplayBodyState( body,
                                                  replayBodyId,
                                                  fixed,
                                                  position,
                                                  orientation,
                                                  linearVelocity,
                                                  angularVelocity,
                                                  mass,
                                                  inverseMass,
                                                  rotationalInertia,
                                                  inverseRotationalInertia ) )
    {
        return false;
    }

    m_presentations.MutableAt( index ).SetFixedContactHighlightSeconds( fixedContactHighlightSeconds );
    // Why: prediction restore is a scratch/live-state swap used for preview and
    // prediction jobs. Authored descriptors represent editor/replay commits and
    // must not be churned every render frame just to apply a temporary body row.
    return true;
}


bool GameModelCollection::TrySetModelAngularVelocity( int index, const Vector3& angularVelocity )
{
    if ( index < 0 || index >= SceneEntityCount() )
    {
        return false;
    }

    PhysicsBodyStateEdit edit;
    edit.hasAngularVelocity = true;
    edit.angularVelocity = angularVelocity;
    return ApplyPhysicsBodyEdit( index, edit );
}


MainMemoryGameObjectStats GameModelCollection::CollectMemoryStats() const
{
    MainMemoryGameObjectStats stats;
    const Physics::PhysicsBodyStore& bodyStore = BodyStore();
    const Physics::ColliderStore& colliderStore = Colliders();
    const Rendering::RenderInstanceStore& renderStore = m_renderInstanceStore;

    stats.modelCount = static_cast<std::size_t>( m_presentations.Count() );
    stats.modelCapacity = m_presentations.Capacity();
    stats.bodyStoreCapacity = bodyStore.Records().capacity();
    stats.colliderStoreCapacity = colliderStore.Records().capacity();
    stats.renderStoreCapacity = renderStore.Records().capacity();
    stats.modelVectorBytes = m_presentations.CapacityBytes() + SceneEntities().CapacityBytes();
    stats.physicsStoreBytes = VectorCapacityBytes( bodyStore.Records() );
    stats.colliderStoreBytes = VectorCapacityBytes( colliderStore.Records() );
    stats.renderStoreBytes = VectorCapacityBytes( renderStore.Records() );
    stats.physicsWorldBytes = m_physicsEngine.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physicsEngine.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.physicsStoreBytes + stats.colliderStoreBytes +
                       stats.renderStoreBytes + stats.physicsWorldBytes;
    return stats;
}


bool GameModelCollection::TrimPresentationRowsForSceneRestore( int modelCount )
{
    if ( modelCount < 0 || modelCount > SceneEntityCount() )
    {
        return false;
    }
    return m_presentations.TrimToCount( modelCount ) && m_renderInstanceStore.ResizePresentationRecords( modelCount );
}


void GameModelCollection::CaptureReplaySolverWorldSnapshot( ReplaySolverWorldSnapshot& outSnapshot ) const
{
    m_physicsEngine.CaptureReplaySolverSnapshot( outSnapshot,
                                                 MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


bool GameModelCollection::RestoreReplaySolverWorldSnapshot( const ReplaySolverWorldSnapshot& snapshot )
{
    return m_physicsEngine.RestoreReplaySolverSnapshot( snapshot,
                                                        MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


SkullbonezCore::Physics::PhysicsEngine& GameModelCollection::GetPhysicsEngine()
{
    return m_physicsEngine;
}


const SkullbonezCore::Physics::PhysicsEngine& GameModelCollection::GetPhysicsEngine() const
{
    return m_physicsEngine;
}


bool GameModelCollection::RepairPhysicsBodyTopology()
{
    if ( BodyStore().Count() != SceneEntityCount() )
    {
        // Invariant: topology repair imports construction rows only. Same-count
        // state edits are physics-store authority and must not be overwritten by
        // a convenience read that rebuilds descriptor rows.
        (void)RefreshPhysicsBodyStoreFromAuthoredDescriptors();
    }
    return BodyStore().Count() == SceneEntityCount();
}


bool GameModelCollection::RepairPhysicsBodyAndColliderTopology()
{
    const int modelCount = SceneEntityCount();
    const bool bodyTopologyChanged = BodyStore().Count() != modelCount;
    const bool colliderTopologyChanged = Colliders().Count() != modelCount;
    if ( bodyTopologyChanged || colliderTopologyChanged )
    {
        // Why: body rows can be repaired from explicit body descriptors, but
        // collider shape/material rows are store-owned once created. Count drift
        // in ColliderStore is a construction bug, not a reason to rediscover
        // shape facts from GameModel.
        if ( bodyTopologyChanged )
        {
            (void)RefreshPhysicsBodyStoreFromAuthoredDescriptors();
        }
        const bool colliderBindingsReady = m_physicsEngine.RefreshColliderSnapshot();
        return BodyStore().Count() == modelCount && Colliders().Count() == modelCount && colliderBindingsReady;
    }
    return BodyStore().Count() == modelCount && Colliders().Count() == modelCount;
}


const SkullbonezCore::Physics::PhysicsBodyStore& GameModelCollection::BodyStore() const
{
    return PhysicsEngineStoreQueries::BodyStore( m_physicsEngine );
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::Colliders() const
{
    return PhysicsEngineStoreQueries::Colliders( m_physicsEngine );
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::RenderInstances() const
{
    return m_renderInstanceStore;
}


SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::MutableRenderInstances()
{
    return m_renderInstanceStore;
}


bool GameModelCollection::TryQueueReplayRenderPoseOverride( int modelIndex,
                                                            uint32_t replayBodyId,
                                                            const Math::Vector::Vector3& position,
                                                            const Math::Orientation::Quaternion& orientation )
{
    const Physics::PhysicsBodyStore& bodyStore = BodyStore();
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
    const Physics::PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    // Invariant: render-pose overrides are keyed by the physics-owned body id.
    // Model-index hints can lag during scrub/prediction presentation, so they
    // cannot approve which live render instance receives the pose.
    if ( !bodyRecord || bodyStore.ModelIndexForHandle( body ) != modelIndex ||
         bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    return m_renderInstanceStore.OverridePose( modelIndex, replayBodyId, position, orientation, Colliders() );
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::GetRenderInstanceStore()
{
    RefreshRenderInstances();
    return m_renderInstanceStore;
}


GameModel& GameModelCollection::GetModelAtIndex( int index )
{
    return m_presentations.MutableAt( index );
}


double GameModelCollection::GetSceneKineticEnergy()
{
    constexpr double REST_LINEAR_SPEED_SQ = 0.5 * 0.5;
    constexpr double REST_ANGULAR_SPEED_SQ = 0.3 * 0.3;
    double totalEnergy = 0.0;
    const PhysicsBodyStore& bodyStore = BodyStore();
    const auto& bodies = bodyStore.Records();
    for ( const PhysicsBodyRecord& body : bodies )
    {
        if ( body.isFixed )
        {
            continue;
        }

        const Vector3& vel = body.linearVelocity;
        const Vector3& omega = body.angularVelocity;
        const double speedSq = static_cast<double>( vel.x ) * vel.x + static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x + static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        if ( speedSq < REST_LINEAR_SPEED_SQ && omegaSq < REST_ANGULAR_SPEED_SQ )
        {
            continue;
        }

        const Vector3& inertia = body.rotationalInertia;
        const double angularEnergy = 0.5 * ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                             static_cast<double>( inertia.y ) * omega.y * omega.y +
                                             static_cast<double>( inertia.z ) * omega.z * omega.z );
        totalEnergy += 0.5 * static_cast<double>( body.mass ) * speedSq + angularEnergy;
    }
    return totalEnergy;
}


void GameModelCollection::RefreshRenderInstances()
{
    const int modelCount = SceneEntityCount();
    if ( !RepairPhysicsBodyTopology() )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( BodyStore().Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( !m_physicsEngine.RefreshColliderSnapshot() )
    {
        // Hazard: render rows consume collider shape/material data. If topology
        // drift has removed collider rows, do not manufacture a partial render
        // snapshot from stale model-owned shape fields.
        m_renderInstanceStore.Clear();
        return;
    }
    if ( BodyStore().Count() != modelCount || Colliders().Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    if ( !m_renderInstanceStore.ResizePresentationRecords( modelCount ) )
    {
        m_renderInstanceStore.Clear();
        return;
    }
    // Owner boundary: model material and highlight values still live in
    // GameModelCollection, but the render-facing presentation rows belong to
    // RenderInstanceStore before store projection creates draw records.
    for ( int i = 0; i < modelCount; ++i )
    {
        const GameModel& model = m_presentations.Records()[static_cast<std::size_t>( i )];
        Rendering::RenderInstancePresentationRecord* presentation =
            m_renderInstanceStore.MutablePresentationRecordForModelIndex( i );
        if ( !presentation )
        {
            m_renderInstanceStore.Clear();
            return;
        }
        const SceneEntityRecord& entity = SceneEntities().At( i );
        presentation->material = entity.renderMaterial;
        strncpy_s( presentation->displayName, sizeof( presentation->displayName ), entity.displayName, _TRUNCATE );
        presentation->simpleRagdollPart = IsSimpleRagdollPart( i );
        presentation->fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        presentation->audioContactAlpha = model.GetAudioContactHighlightAlpha();
    }
    m_renderInstanceStore.Refresh( BodyStore(), Colliders() );
    if ( m_renderInstanceStore.Count() != modelCount )
    {
        m_renderInstanceStore.Clear();
        return;
    }
#ifdef _DEBUG
    const std::vector<Rendering::RenderInstanceRecord>& instances = m_renderInstanceStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const Rendering::RenderInstanceRecord& instance = instances[index];
        const Rendering::RenderInstanceHandle renderHandle = m_renderInstanceStore.HandleForModelIndex( i );

        assert( renderHandle.IsValid() );
        assert( instance.handle == renderHandle );
        assert( m_renderInstanceStore.ModelIndexForHandle( renderHandle ) == i );
    }
#endif
}


bool GameModelCollection::ApplyPhysicsBodyEdit( int modelIndex, const PhysicsBodyStateEdit& edit )
{
    const int modelCount = SceneEntityCount();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    // Invariant: cold editor/replay commands provide the changed body values.
    // Unchanged fields come from PhysicsBodyStore, not the presentation row.
    const PhysicsAuthoredBodyCount expectedAuthoredBodyCount =
        MakePhysicsAuthoredBodyCountFromNonNegativeInt( modelCount );
    const PhysicsBodyCount expectedBodyCount = MakePhysicsBodyCountFromNonNegativeInt( modelCount );
    const ModelRowHint bodyRow = MakeModelRowHint( modelIndex );
    if ( static_cast<int>( m_physicsEngine.AuthoredBodyDescriptorCount().value ) != modelCount )
    {
        return false;
    }

    if ( BodyStore().Count() != modelCount )
    {
        if ( !RefreshPhysicsBodyStoreFromAuthoredDescriptors() )
        {
            return false;
        }
    }

    const PhysicsBodyRecord* bodyRecord = BodyStore().RecordForModelIndex( modelIndex );
    if ( !bodyRecord )
    {
        return false;
    }

    PhysicsBodyCreateDesc bodyDesc;
    if ( !m_physicsEngine.TryGetAuthoredBodyDescriptor( bodyRow, bodyDesc ) )
    {
        return false;
    }
    RefreshBodyDescFromStoreBodyState( *bodyRecord, edit, bodyDesc, FixedTreeReleaseRootForModelIndex( modelIndex ) );
    if ( !m_physicsEngine.UpdateAuthoredBodyDescriptor( bodyRow, bodyDesc, expectedAuthoredBodyCount ) )
    {
        return false;
    }
    m_physicsEngine.RefreshBodyFromDescriptor( bodyDesc, bodyRow, expectedBodyCount );

    // Why: body edits now stop at PhysicsBodyStore and PhysicsScene authored
    // descriptors. Render, replay, snapshots, and editor wake checks read those
    // stores directly.
    return true;
}


bool GameModelCollection::ApplyPhysicsBodyColliderEdit( int modelIndex,
                                                        const PhysicsBodyStateEdit& edit,
                                                        PhysicsColliderCreateDesc colliderDesc )
{
    const int modelCount = SceneEntityCount();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    // Invariant: editor/replay scale edits are cold authoring events. Commit the
    // body row first, then replace exactly one collider row by stable handle.
    // Count drift still goes through topology repair because missing rows cannot
    // be patched by a single descriptor.
    const PhysicsAuthoredBodyCount expectedAuthoredBodyCount =
        MakePhysicsAuthoredBodyCountFromNonNegativeInt( modelCount );
    const PhysicsBodyCount expectedBodyCount = MakePhysicsBodyCountFromNonNegativeInt( modelCount );
    const ModelRowHint bodyRow = MakeModelRowHint( modelIndex );
    if ( static_cast<int>( m_physicsEngine.AuthoredBodyDescriptorCount().value ) != modelCount )
    {
        return false;
    }

    if ( BodyStore().Count() != modelCount )
    {
        if ( !RefreshPhysicsBodyStoreFromAuthoredDescriptors() )
        {
            return false;
        }
    }

    const PhysicsBodyRecord* existingBody = BodyStore().RecordForModelIndex( modelIndex );
    if ( !existingBody )
    {
        return false;
    }
    PhysicsBodyCreateDesc bodyDesc;
    if ( !m_physicsEngine.TryGetAuthoredBodyDescriptor( bodyRow, bodyDesc ) )
    {
        return false;
    }
    RefreshBodyDescFromStoreBodyState( *existingBody, edit, bodyDesc, FixedTreeReleaseRootForModelIndex( modelIndex ) );
    bodyDesc.shape = colliderDesc.shape;
    bodyDesc.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( bodyDesc.shape );
    bodyDesc.volume = Math::CollisionDetection::GetShapeVolume( bodyDesc.shape );
    bodyDesc.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( bodyDesc.shape );
    bodyDesc.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( bodyDesc.shape );
    bodyDesc.usesWorldInertia = !std::holds_alternative<BoundingSphere>( bodyDesc.shape );
    if ( !m_physicsEngine.UpdateAuthoredBodyDescriptor( bodyRow, bodyDesc, expectedAuthoredBodyCount ) )
    {
        return false;
    }
    m_physicsEngine.RefreshBodyFromDescriptor( bodyDesc, bodyRow, expectedBodyCount );

    const bool colliderBindingsReady = m_physicsEngine.RefreshColliderSnapshot();

    const PhysicsBodyRecord* bodyRecord = BodyStore().RecordForModelIndex( modelIndex );
    if ( !bodyRecord || !colliderBindingsReady || Colliders().Count() != modelCount )
    {
        return false;
    }

    colliderDesc.body = bodyRecord->handle;
    colliderDesc.sceneObjectId = bodyRecord->sceneObjectId;
    m_physicsEngine.ApplyAuthoredColliderPolicy( colliderDesc );
    const PhysicsColliderHandle collider = Colliders().HandleForBodyHandle( bodyRecord->handle );
    const ColliderRecord* existingCollider = Colliders().RecordForHandle( collider );
    if ( colliderDesc.contactMaterialName[0] == '\0' && existingCollider &&
         existingCollider->contactMaterialName[0] != '\0' )
    {
        strncpy_s( colliderDesc.contactMaterialName,
                   sizeof( colliderDesc.contactMaterialName ),
                   existingCollider->contactMaterialName,
                   _TRUNCATE );
    }
    (void)m_physicsEngine.UpdateAuthoredCollider( collider, colliderDesc );

    // Why: shape/body edits now stop at ColliderStore/PhysicsBodyStore plus the
    // PhysicsScene descriptor store. GameModel keeps presentation metadata only.
    return true;
}


void GameModelCollection::CommitEditedModelBodyState( int modelIndex )
{
    (void)ApplyPhysicsBodyEdit( modelIndex, PhysicsBodyStateEdit{} );
}


void GameModelCollection::CommitEditedModelColliderState( int modelIndex, PhysicsColliderCreateDesc colliderDesc )
{
    (void)ApplyPhysicsBodyColliderEdit( modelIndex, PhysicsBodyStateEdit{}, std::move( colliderDesc ) );
}


void GameModelCollection::NotifyFixedContact( int modelIndex, float highlightSeconds )
{
    if ( modelIndex < 0 || modelIndex >= SceneEntityCount() )
    {
        return;
    }

    // Why: fixed-contact events come from the solver. The presentation timer
    // should trust the same dense body row instead of reopening legacy
    // model-side physics state to decide whether a body is fixed.
    const PhysicsBodyRecord* body = BodyStore().RecordForModelIndex( modelIndex );
    if ( body && body->isFixed )
    {
        m_presentations.MutableAt( modelIndex ).NotifyFixedContact( highlightSeconds );
    }
}


void GameModelCollection::TickContactHighlights( int modelCount, float deltaSeconds )
{
    // Why: contact highlights are presentation state on GameModel. Physics owns
    // when contact events happen, but the model collection owns the timers that
    // render/debug/audio views later sample.
    const int tickCount = (std::min)( modelCount, SceneEntityCount() );
    for ( int i = 0; i < tickCount; ++i )
    {
        m_presentations.MutableAt( i ).TickFixedContactHighlight( deltaSeconds );
    }
}


void GameModelCollection::NotifyAudioContact( int modelIndex, float highlightSeconds )
{
    if ( modelIndex < 0 || modelIndex >= SceneEntityCount() )
    {
        return;
    }

    m_presentations.MutableAt( modelIndex ).NotifyAudioContact( highlightSeconds );
}


bool GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                         float releaseImpulseStrength,
                                                         const Vector3& seedLinearVelocity,
                                                         const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= SceneEntityCount() )
    {
        return false;
    }

    // Owner: GameModelCollection runtime-tool edge.
    // Reason: launcher hits still arrive as model indices, but fixed-state,
    // release policy, and same-tree propagation now belong to PhysicsBodyStore.
    // Deletion condition: runtime picking and scene identity use stable entity
    // ids or body handles directly. Checker budget: boundary grep blocks this
    // function from reading GameModel fixed/position/tree body metadata or
    // reintroducing a per-release model writeback.
    if ( !RepairPhysicsBodyAndColliderTopology() )
    {
        return false;
    }

    const PhysicsBodyHandle sourceBody = BodyStore().HandleForModelIndex( sourceIndex );
    if ( !sourceBody.IsValid() )
    {
        return false;
    }

    if ( !m_physicsEngine.ReleaseFixedBodyAndAttachedTreeParts( sourceBody,
                                                                releaseImpulseStrength,
                                                                seedLinearVelocity,
                                                                seedAngularVelocity ) )
    {
        return false;
    }

    return true;
}


void GameModelCollection::SetPhysicsSleepEnabled( bool enabled )
{
    m_physicsEngine.SetSleepEnabled( enabled );
}


void GameModelCollection::ClearPointJointConstraints()
{
    m_physicsEngine.ClearPointJointConstraints();
}


void GameModelCollection::BeginCollisionVisualFrame()
{
    m_physicsEngine.BeginCollisionVisualFrame( MakePhysicsBodyCountFromNonNegativeInt( SceneEntityCount() ) );
}


void GameModelCollection::EndCollisionVisualFrame()
{
    m_physicsEngine.EndCollisionVisualFrame();
}


void GameModelCollection::SetTornadoFieldConfig( const Physics::TornadoFieldConfig& config )
{
    m_physicsEngine.SetTornadoFieldConfig( config );
}


void GameModelCollection::SetTornadoSystemConfig( const Physics::TornadoSystemConfig& config )
{
    m_physicsEngine.SetTornadoSystemConfig( config );
}


#ifdef _DEBUG
void GameModelCollection::SetPhysicsRegressionLogPath( const char* path )
{
    m_physicsEngine.SetPhysicsRegressionLogPath( path );
}


void GameModelCollection::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_physicsEngine.SetPhysicsCollisionTimeLogPath( path );
}


void GameModelCollection::SetPhysicsDiagnosticsPath( const char* path )
{
    m_physicsEngine.SetPhysicsDiagnosticsPath( path );
}


void GameModelCollection::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_physicsEngine.SetPhysicsDiagnosticsRunId( runId );
}


bool GameModelCollection::SetPhysicsDiagnosticsSuppressed( bool suppressed )
{
    return m_physicsEngine.SetDiagnosticsSuppressed( suppressed );
}
#endif

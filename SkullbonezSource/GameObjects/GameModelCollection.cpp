/*
File: SkullbonezSource/GameObjects/GameModelCollection.cpp
Purpose:
  Owns all scene models and delegates rendering, physics, and snapshots.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Physics material: Per-object friction and drag coefficients cached by the
    collection before models are added or reconfigured.
  Body simulation limit: Scalar cap cached by the collection before authored
    descriptors create PhysicsBodyStore rows.
  Contact policy: Terrain and contact thresholds cached by the collection so
    existing and newly added models receive the same physics policy.
  Body descriptor: Value packet containing authoring body facts that
    PhysicsScene turns into a live PhysicsBodyStore row.
  Render instance store: Renderer-facing snapshot built from physics-owned pose
    and model-owned material/presentation state before frame passes.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsScene turns into a live ColliderStore row.
  Topology drift: A body/collider/model count mismatch that means stores must
    import explicit construction descriptors before stepping.
  Scene-object group: Cold metadata that maps multi-part authored objects, such
    as ragdolls or releasable trees, back to a root model slot.
  Fixed-tree release: Authored scene rule that lets tree parts become dynamic
    when a related fixed part is hit strongly enough.
  Replay body id: PhysicsBodyStore-owned identity saved in replay samples so
    restore paths can reject stale model slots.
  Validation gate: Repository script that proves a class of changes before
    commit or PR.

Invariants:
  - Model vector order remains the scene alignment key for physics stores,
    render batches, and scene snapshots. Replay ids live in PhysicsBodyStore
    rows after append.
  - Scene-object group records are a same-length sidecar keyed by model slot;
    GameModel does not carry runtime grouping fields.
  - Render prep imports store-backed snapshots once before frame passes; render
    code must not rebuild model-derived pose streams.
  - Owner-side release paths repair topology once before resolving body handles
    from PhysicsBodyStore.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelCollection.h"

#include "../Core/MainMemoryStats.h"
#include "../Core/SkullScope.h"
#include "../Physics/Debug/CollisionVisualizer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Rendering/GameModelRenderer.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../Scene/SceneSnapshotWriter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BodySimulationLimits;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::ContactPolicy;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsMaterial;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
template <typename T> uint64_t VectorCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}


void ApplyCollectionPhysicsMaterialToColliderDesc( PhysicsColliderCreateDesc& desc, const PhysicsMaterial& material )
{
    desc.friction = material.frictionCoefficient;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( material.sphereDragCoefficient );
        desc.dragCoefficient = material.sphereDragCoefficient;
    }
}

void ApplyCollectionPhysicsPolicyToBodyDesc( PhysicsBodyCreateDesc& desc,
                                             const PhysicsMaterial& material,
                                             const BodySimulationLimits& limits,
                                             const ContactPolicy& policy )
{
    desc.friction = material.frictionCoefficient;
    desc.angularVelocityLimit = limits.angularVelocityLimit;
    desc.contactEpsilon = policy.contactEpsilon;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( material.sphereDragCoefficient );
        desc.dragCoefficient = material.sphereDragCoefficient;
    }
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


void GameModelCollection::ReserveForActiveGameModelCapacity()
{
    // Invariant: model-order storage must be fully sized before steady frames.
    // Config can raise the active model capacity after construction, so each
    // setup/config boundary repeats the reserve instead of letting render-time
    // append paths discover the new capacity by reallocating.
    const std::size_t capacity = static_cast<std::size_t>( ActiveGameModelCapacity() );
    m_gameModels.reserve( capacity );
    m_sceneObjectGroups.reserve( capacity );
    m_authoredBodyDescs.reserve( capacity );
    m_renderPresentationRecords.reserve( capacity );
}


GameModelCollection::SceneObjectGroupRecord
GameModelCollection::BuildSceneObjectGroupForAppend( const GameModel&,
                                                     int newModelIndex,
                                                     SceneObjectGroupCreateDesc groupDesc )
{
    assert( m_sceneObjectGroups.size() == m_gameModels.size() );
    SceneObjectGroupRecord group;

    if ( groupDesc.kind != GameModelCollectionKind::None )
    {
        // Invariant: explicit grouping is creation metadata, not a discovery
        // task for collection append. Bad root/part input means the owner passed
        // an impossible group, so fail closed before rows diverge.
        if ( groupDesc.rootModelIndex < 0 || groupDesc.rootModelIndex > newModelIndex || groupDesc.partIndex < 0 )
        {
            throw std::runtime_error( "Invalid scene-object group descriptor supplied during model append." );
        }

        group.kind = groupDesc.kind;
        group.rootModelIndex = groupDesc.rootModelIndex;
        group.partIndex = groupDesc.partIndex;
        return group;
    }

    return group;
}


GameModelCollection::SceneObjectGroupRecord GameModelCollection::GroupRecordAt( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_sceneObjectGroups.size() ) )
    {
        return SceneObjectGroupRecord{};
    }
    return m_sceneObjectGroups[static_cast<std::size_t>( modelIndex )];
}


static uint32_t NextReplayBodyIdAfter( const PhysicsBodyStore& bodyStore )
{
    uint32_t nextReplayBodyId = 1;
    const uint32_t maxReplayBodyId = ( std::numeric_limits<uint32_t>::max )();
    for ( const PhysicsBodyRecord& body : bodyStore.Records() )
    {
        if ( body.replayBodyId == maxReplayBodyId )
        {
            return maxReplayBodyId;
        }
        if ( body.replayBodyId != 0 )
        {
            nextReplayBodyId = (std::max)( nextReplayBodyId, body.replayBodyId + 1u );
        }
    }
    return nextReplayBodyId;
}


std::vector<uint32_t>
GameModelCollection::BuildReplayBodyIdsForReload( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    std::vector<uint32_t> replayBodyIds;
    replayBodyIds.reserve( m_gameModels.size() );
    uint32_t nextReplayBodyId = NextReplayBodyIdAfter( bodyStore );
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        uint32_t replayBodyId = 0;
        if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i ) )
        {
            replayBodyId = body->replayBodyId;
        }
        // Why: body topology repair is cold. Existing rows preserve their
        // PhysicsBodyStore-owned replay id; only genuinely missing rows allocate
        // a fresh local scratch id rather than keeping collection authority.
        if ( replayBodyId == 0 )
        {
            if ( nextReplayBodyId == ( std::numeric_limits<uint32_t>::max )() )
            {
                throw std::runtime_error( "Replay body id scratch range exhausted." );
            }
            replayBodyId = nextReplayBodyId++;
        }
        replayBodyIds.push_back( replayBodyId );
    }
    return replayBodyIds;
}


std::vector<int> GameModelCollection::BuildFixedTreeReleaseRootsForReload() const
{
    std::vector<int> fixedTreeReleaseRoots;
    fixedTreeReleaseRoots.reserve( m_gameModels.size() );
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        fixedTreeReleaseRoots.push_back( FixedTreeReleaseRootForModelIndex( i ) );
    }
    return fixedTreeReleaseRoots;
}


std::vector<PhysicsBodyCreateDesc>
GameModelCollection::BuildBodyCreateDescsForReload( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    std::vector<uint32_t> replayBodyIds = BuildReplayBodyIdsForReload( bodyStore );
    std::vector<int> fixedTreeReleaseRoots = BuildFixedTreeReleaseRootsForReload();
    assert( replayBodyIds.size() == m_authoredBodyDescs.size() );
    assert( fixedTreeReleaseRoots.size() == m_authoredBodyDescs.size() );

    std::vector<PhysicsBodyCreateDesc> bodyDescs;
    bodyDescs.reserve( m_authoredBodyDescs.size() );
    for ( int i = 0; i < static_cast<int>( m_authoredBodyDescs.size() ); ++i )
    {
        PhysicsBodyCreateDesc desc = m_authoredBodyDescs[static_cast<std::size_t>( i )];
        desc.sceneObjectId = SkullbonezCore::Physics::MakePhysicsSceneObjectIdFromReplayBodyId(
            replayBodyIds[static_cast<std::size_t>( i )] );
        desc.fixedTreeReleaseRootIndex = fixedTreeReleaseRoots[static_cast<std::size_t>( i )];
        if ( i < static_cast<int>( m_gameModels.size() ) )
        {
            desc.diagnosticName = m_gameModels[static_cast<std::size_t>( i )].GetName();
        }
        ApplyCollectionPhysicsPolicyToBodyDesc( desc, m_physicsMaterial, m_bodySimulationLimits, m_contactPolicy );
        bodyDescs.push_back( desc );
    }
    return bodyDescs;
}


int GameModelCollection::FixedTreeReleaseRootForModelIndex( int modelIndex ) const
{
    const SceneObjectGroupRecord group = GroupRecordAt( modelIndex );
    return group.kind == GameModelCollectionKind::ReleasableTree ? group.rootModelIndex : -1;
}


void GameModelCollection::BindWorkerPool( SkullbonezCore::Threading::WorkerPool& workerPool )
{
    m_workerPool = &workerPool;
}


void GameModelCollection::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    ReserveForActiveGameModelCapacity();
    m_physicsMaterial = Physics::PhysicsMaterial::FromConfig( config );
    m_bodySimulationLimits = Physics::BodySimulationLimits::FromConfig( config );
    m_contactPolicy = Physics::ContactPolicy::FromConfig( config );
    m_renderCollisionVolumes = config.runtimeRender.renderCollisionVolumes;
    m_shadowParallelPrep = config.shadowParallelPrep;
    m_physicsEngine.ApplyRuntimeConfig( config );
    const bool colliderRowsReady =
        m_physicsEngine.BodyStore().Count() == ModelCount() && m_physicsEngine.Colliders().Count() == ModelCount();
    for ( int i = 0; i < static_cast<int>( m_authoredBodyDescs.size() ); ++i )
    {
        ApplyCollectionPhysicsPolicyToBodyDesc( m_authoredBodyDescs[static_cast<std::size_t>( i )],
                                                m_physicsMaterial,
                                                m_bodySimulationLimits,
                                                m_contactPolicy );
    }
    if ( colliderRowsReady )
    {
        m_physicsEngine.ApplyColliderMaterial( m_physicsMaterial );
    }
    if ( !colliderRowsReady )
    {
        RepairPhysicsBodyAndColliderTopology();
    }
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


PhysicsBodyHandle GameModelCollection::AddGameModel( GameModel gameModel,
                                                     PhysicsBodyCreateDesc bodyDesc,
                                                     PhysicsColliderCreateDesc colliderDesc,
                                                     PhysicsSceneObjectId sceneObjectId,
                                                     SceneObjectGroupCreateDesc groupDesc )
{
    return AppendGameModelAndPhysicsRows( std::move( gameModel ),
                                          std::move( bodyDesc ),
                                          sceneObjectId,
                                          std::move( colliderDesc ),
                                          groupDesc );
}


PhysicsBodyHandle GameModelCollection::AppendGameModelAndPhysicsRows( GameModel gameModel,
                                                                      PhysicsBodyCreateDesc bodyDesc,
                                                                      PhysicsSceneObjectId sceneObjectId,
                                                                      PhysicsColliderCreateDesc colliderDesc,
                                                                      SceneObjectGroupCreateDesc groupDesc )
{
    const int activeCapacity = ActiveGameModelCapacity();
    assert( static_cast<int>( m_gameModels.size() ) < activeCapacity && "Exceeded active game model capacity" );
    if ( static_cast<int>( m_gameModels.size() ) >= activeCapacity )
    {
        throw std::runtime_error(
            "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." );
    }
    const int modelIndex = static_cast<int>( m_gameModels.size() );
    const SceneObjectGroupRecord groupRecord = BuildSceneObjectGroupForAppend( gameModel, modelIndex, groupDesc );
    if ( !sceneObjectId.IsValid() )
    {
        throw std::runtime_error( "Cannot append model without a scene object id." );
    }
    // Invariant: creation identity is scene-owned. Collection appends the model
    // row and forwards the id once; body/collider/render stores then carry it
    // through their dense rows without a collection-side allocator or sidecar.
    // Hazard: append-time body/collider registration assumes existing rows are
    // aligned. Missing collider rows mean a caller skipped the creation command
    // that owns shape data; do not recapture old GameModel fields to paper over
    // the topology bug.
    if ( !RepairPhysicsBodyAndColliderTopology() )
    {
        throw std::runtime_error( "Cannot append model while physics collider rows are missing." );
    }
    bodyDesc.sceneObjectId = sceneObjectId;
    bodyDesc.fixedTreeReleaseRootIndex =
        groupRecord.kind == GameModelCollectionKind::ReleasableTree ? groupRecord.rootModelIndex : -1;
    bodyDesc.diagnosticName = gameModel.GetName();
    ApplyCollectionPhysicsPolicyToBodyDesc( bodyDesc, m_physicsMaterial, m_bodySimulationLimits, m_contactPolicy );
    m_gameModels.push_back( std::move( gameModel ) );
    m_sceneObjectGroups.push_back( groupRecord );
    m_authoredBodyDescs.push_back( bodyDesc );
    const PhysicsBodyHandle bodyHandle = m_physicsEngine.RegisterAuthoredBody( bodyDesc );
    const PhysicsBodyRecord* bodyRecord = m_physicsEngine.BodyStore().RecordForHandle( bodyHandle );
    assert( bodyRecord != nullptr );
    if ( !bodyRecord )
    {
        throw std::runtime_error( "Failed to resolve newly authored physics body record." );
    }
    // Owner: creation callers provide shape facts; PhysicsScene owns live
    // collider rows. Reason: radius/extents/hull values exist before append, so
    // recapturing them from GameModel preserves old ownership and extra work.
    // Checker: runtime boundaries reject bare AddGameModel calls that omit a
    // collider descriptor.
    PhysicsColliderCreateDesc authoredCollider = std::move( colliderDesc );
    authoredCollider.body = bodyRecord->handle;
    authoredCollider.sceneObjectId = bodyRecord->sceneObjectId;
    ApplyCollectionPhysicsMaterialToColliderDesc( authoredCollider, m_physicsMaterial );
    const auto colliderHandle = m_physicsEngine.RegisterAuthoredCollider( authoredCollider );
    assert( colliderHandle.IsValid() );
    if ( !colliderHandle.IsValid() )
    {
        throw std::runtime_error( "Failed to register newly authored physics collider record." );
    }
    return bodyHandle;
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_sceneObjectGroups.clear();
    m_authoredBodyDescs.clear();
    m_renderPresentationRecords.clear();
    m_physicsEngine.Clear();
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

    const int collectionModelCount = GetModelCount();
    if ( m_physicsEngine.RenderInstances().Count() != collectionModelCount )
    {
        // Hazard: normal render frames call PrepareRenderInstances() first. This
        // cold path keeps standalone DXR callers on the render-instance
        // authority instead of reintroducing model-side pose reconstruction.
        RefreshRenderInstances();
    }

    const std::vector<Rendering::RenderInstanceRecord>& instances = m_physicsEngine.RenderInstances().Records();
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
        m_physicsEngine.BodyStore(),
        m_physicsEngine.Colliders(),
        m_physicsEngine.RenderInstances(),
        m_physicsEngine.GetCollisionVisualContacts(),
        m_physicsEngine.GetSleepStates(),
        m_physicsEngine.GetSleepIslandVisualIds(),
        GetModelCount(),
    };
    visualizer.Update( deltaSeconds, view );
}


void GameModelCollection::UpdatePhysicsDebugVisualizer( Physics::PhysicsDebugVisualizer& visualizer,
                                                        float deltaSeconds )
{
    const Physics::PhysicsDebugFrameView view{
        m_physicsEngine.BodyStore(),
        m_physicsEngine.Colliders(),
        m_physicsEngine.GetSleepStates(),
        m_physicsEngine.GetSleepSupportedStates(),
        m_physicsEngine.GetSleepInhibitedStates(),
        m_physicsEngine.GetPhysicsDebugContacts(),
        m_physicsEngine.GetPhysicsPipelineTrace(),
        GetModelCount(),
    };
    visualizer.Update( deltaSeconds, view );
}


void GameModelCollection::RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                                      Assets::AssetSystem& assets,
                                                      Rendering::IRenderResourceFactory& renderResources,
                                                      const Matrix4& view,
                                                      const Matrix4& proj,
                                                      const float lightPos[4],
                                                      float alphaOverride )
{
    const Physics::CollisionVisualizerFrameView frameView{
        m_physicsEngine.BodyStore(),
        m_physicsEngine.Colliders(),
        m_physicsEngine.RenderInstances(),
        m_physicsEngine.GetCollisionVisualContacts(),
        m_physicsEngine.GetSleepStates(),
        m_physicsEngine.GetSleepIslandVisualIds(),
        GetModelCount(),
    };
    visualizer.SetAlphaOverride( alphaOverride );
    visualizer.Render( assets, renderResources, frameView, view, proj, lightPos );
    visualizer.SetAlphaOverride( -1.0f );
}


void GameModelCollection::RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                                              const Matrix4& viewProjection,
                                              Geometry::Terrain* terrain )
{
    const Physics::PhysicsDebugFrameView frameView{
        m_physicsEngine.BodyStore(),
        m_physicsEngine.Colliders(),
        m_physicsEngine.GetSleepStates(),
        m_physicsEngine.GetSleepSupportedStates(),
        m_physicsEngine.GetSleepInhibitedStates(),
        m_physicsEngine.GetPhysicsDebugContacts(),
        m_physicsEngine.GetPhysicsPipelineTrace(),
        GetModelCount(),
    };
    visualizer.Render( frameView, viewProjection, terrain );
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


bool GameModelCollection::SaveSceneSnapshot( const char* path,
                                             bool physicsOn,
                                             bool textOn,
                                             Environment::WorldEnvironment& worldEnv,
                                             const Vector3& camEye,
                                             const Vector3& camView,
                                             const Vector3& camUp,
                                             bool editableScene,
                                             bool fixedStep,
                                             bool waterHidden,
                                             bool terrainHidden,
                                             bool hasFlatSlope,
                                             float flatBaseY,
                                             float flatSlopeX,
                                             float flatSlopeZ )
{
    return SceneSnapshotWriter::Save( *this,
                                      path,
                                      physicsOn,
                                      textOn,
                                      worldEnv,
                                      camEye,
                                      camView,
                                      camUp,
                                      editableScene,
                                      fixedStep,
                                      waterHidden,
                                      terrainHidden,
                                      hasFlatSlope,
                                      flatBaseY,
                                      flatSlopeX,
                                      flatSlopeZ );
}


Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        throw std::runtime_error(
            "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    // Why: object-follow cameras should read the same store-owned pose that
    // physics, diagnostics, replay capture, and render snapshots consume. A
    // topology mismatch is repaired by GetPhysicsBodyStore(); same-count edits
    // must already have entered the store through explicit command/commit paths.
    const PhysicsBodyStore& bodyStore = GetPhysicsBodyStore();
    const PhysicsBodyRecord* record = bodyStore.RecordForModelIndex( index );
    if ( !record )
    {
        throw std::runtime_error(
            "No physics body exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }
    return record->position;
}


int GameModelCollection::GetModelCount() const
{
    return static_cast<int>( m_gameModels.size() );
}


int GameModelCollection::ModelCount() const
{
    return GetModelCount();
}


const std::vector<GameModel>& GameModelCollection::Models() const
{
    return m_gameModels;
}


const GameModel* GameModelCollection::TryGetModel( int index ) const
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return nullptr;
    }

    return &m_gameModels[static_cast<std::size_t>( index )];
}


GameModelCollectionKind GameModelCollection::GroupKindAt( int modelIndex ) const
{
    return GroupRecordAt( modelIndex ).kind;
}


int GameModelCollection::GroupRootModelIndexAt( int modelIndex ) const
{
    return GroupRecordAt( modelIndex ).rootModelIndex;
}


int GameModelCollection::GroupPartIndexAt( int modelIndex ) const
{
    return GroupRecordAt( modelIndex ).partIndex;
}


bool GameModelCollection::IsSimpleRagdollPart( int modelIndex ) const
{
    return GroupKindAt( modelIndex ) == GameModelCollectionKind::SimpleRagdoll;
}


bool GameModelCollection::IsSimpleRagdollTorso( int modelIndex ) const
{
    return IsSimpleRagdollPart( modelIndex ) && GroupPartIndexAt( modelIndex ) == 0;
}


int GameModelCollection::RagdollRootModelIndexForPart( int modelIndex ) const
{
    const SceneObjectGroupRecord group = GroupRecordAt( modelIndex );
    if ( group.kind != GameModelCollectionKind::SimpleRagdoll )
    {
        return modelIndex;
    }

    if ( group.rootModelIndex >= 0 && group.rootModelIndex < GetModelCount() &&
         IsSimpleRagdollPart( group.rootModelIndex ) )
    {
        return group.rootModelIndex;
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

    const int rootModelIndex = GroupRootModelIndexAt( selectedModelIndex );
    for ( int i = 0; i < GetModelCount(); ++i )
    {
        const SceneObjectGroupRecord group = GroupRecordAt( i );
        if ( group.kind == GameModelCollectionKind::SimpleRagdoll && group.rootModelIndex == rootModelIndex &&
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
    if ( !outIndices || maxIndices <= 0 || selectedModelIndex < 0 || selectedModelIndex >= GetModelCount() )
    {
        return 0;
    }

    const SceneObjectGroupRecord selectedGroup = GroupRecordAt( selectedModelIndex );
    if ( selectedGroup.kind != GameModelCollectionKind::SimpleRagdoll )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    const int selectedRootIndex = selectedGroup.rootModelIndex;
    if ( selectedRootIndex < 0 || selectedRootIndex >= GetModelCount() )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    int count = 0;
    for ( int i = 0; i < GetModelCount() && count < maxIndices; ++i )
    {
        const SceneObjectGroupRecord group = GroupRecordAt( i );
        if ( group.kind == selectedGroup.kind && group.rootModelIndex == selectedRootIndex )
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
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    const GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    outName = model.GetName();
    return true;
}


void GameModelCollection::FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const
{
    const int clampedBodyCount = (std::max)( 0, bodyCount );
    outNames.assign( static_cast<std::size_t>( clampedBodyCount ), "" );
    const int copyCount = (std::min)( clampedBodyCount, GetModelCount() );
    for ( int i = 0; i < copyCount; ++i )
    {
        // Lifetime: these are borrowed display-name pointers for the current
        // Debug diagnostics write; the caller owns only the pointer table.
        outNames[static_cast<std::size_t>( i )] = m_gameModels[static_cast<std::size_t>( i )].GetName();
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
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    // Invariant: model index verifies the presentation slot only. The current
    // body handle and replay id must prove the live physics row before either
    // side of the restore mutates.
    const PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
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

    if ( index < static_cast<int>( m_authoredBodyDescs.size() ) )
    {
        PhysicsBodyCreateDesc& desc = m_authoredBodyDescs[static_cast<std::size_t>( index )];
        desc.sceneObjectId = bodyRecord->sceneObjectId;
        desc.position = position;
        desc.orientation = orientation;
        desc.linearVelocity = linearVelocity;
        desc.angularVelocity = angularVelocity;
        desc.rotationalInertia = rotationalInertia;
        desc.mass = mass;
        desc.motionKind = fixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
        desc.diagnosticName = m_gameModels[static_cast<std::size_t>( index )].GetName();
        desc.fixedTreeReleaseRootIndex = FixedTreeReleaseRootForModelIndex( index );
        ApplyCollectionPhysicsPolicyToBodyDesc( desc, m_physicsMaterial, m_bodySimulationLimits, m_contactPolicy );
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
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    // Invariant: model index verifies the presentation slot only. The current
    // body handle and replay id must prove the live physics row before either
    // side of the restore mutates.
    const PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
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

    m_gameModels[static_cast<std::size_t>( index )].SetFixedContactHighlightSeconds( fixedContactHighlightSeconds );
    // Why: prediction restore is a scratch/live-state swap used for preview and
    // prediction jobs. Authored descriptors represent editor/replay commits and
    // must not be churned every render frame just to apply a temporary body row.
    return true;
}


bool GameModelCollection::TrySetModelAngularVelocity( int index, const Vector3& angularVelocity )
{
    if ( index < 0 || index >= GetModelCount() )
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
    const Physics::PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
    const Physics::ColliderStore& colliderStore = m_physicsEngine.Colliders();
    const Rendering::RenderInstanceStore& renderStore = m_physicsEngine.RenderInstances();

    stats.modelCount = m_gameModels.size();
    stats.modelCapacity = m_gameModels.capacity();
    stats.bodyStoreCapacity = bodyStore.Records().capacity();
    stats.colliderStoreCapacity = colliderStore.Records().capacity();
    stats.renderStoreCapacity = renderStore.Records().capacity();
    stats.modelVectorBytes = VectorCapacityBytes( m_gameModels );
    stats.physicsStoreBytes = VectorCapacityBytes( bodyStore.Records() );
    stats.colliderStoreBytes = VectorCapacityBytes( colliderStore.Records() );
    stats.renderStoreBytes = VectorCapacityBytes( renderStore.Records() );
    stats.physicsWorldBytes = m_physicsEngine.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physicsEngine.CollectDebugAndBroadphaseMemoryBytes();
    const uint64_t sceneObjectGroupBytes = VectorCapacityBytes( m_sceneObjectGroups );
    stats.totalBytes = stats.modelVectorBytes + sceneObjectGroupBytes + stats.physicsStoreBytes +
                       stats.colliderStoreBytes + stats.renderStoreBytes + stats.physicsWorldBytes;
    return stats;
}


bool GameModelCollection::TrimModelsForReplayRestore( int modelCount )
{
    if ( modelCount < 0 || modelCount > static_cast<int>( m_gameModels.size() ) )
    {
        return false;
    }

    const std::size_t targetCount = static_cast<std::size_t>( modelCount );
    if ( !m_physicsEngine.TrimBodyStoreToCount( modelCount ) )
    {
        return false;
    }
    if ( m_physicsEngine.Colliders().Count() > modelCount && !m_physicsEngine.TrimColliderStoreToCount( modelCount ) )
    {
        return false;
    }
    if ( targetCount < m_gameModels.size() )
    {
        m_gameModels.erase( m_gameModels.begin() + static_cast<std::ptrdiff_t>( targetCount ), m_gameModels.end() );
        m_sceneObjectGroups.erase( m_sceneObjectGroups.begin() + static_cast<std::ptrdiff_t>( targetCount ),
                                   m_sceneObjectGroups.end() );
        m_authoredBodyDescs.erase( m_authoredBodyDescs.begin() + static_cast<std::ptrdiff_t>( targetCount ),
                                   m_authoredBodyDescs.end() );
    }
    return true;
}


void GameModelCollection::CaptureReplaySolverWorldSnapshot( ReplaySolverWorldSnapshot& outSnapshot ) const
{
    m_physicsEngine.CaptureReplaySolverSnapshot( outSnapshot, static_cast<int>( m_gameModels.size() ) );
}


bool GameModelCollection::RestoreReplaySolverWorldSnapshot( const ReplaySolverWorldSnapshot& snapshot )
{
    return m_physicsEngine.RestoreReplaySolverSnapshot( snapshot, static_cast<int>( m_gameModels.size() ) );
}


SkullbonezCore::Physics::PhysicsEngine& GameModelCollection::GetPhysicsEngine()
{
    return m_physicsEngine;
}


const SkullbonezCore::Physics::PhysicsEngine& GameModelCollection::GetPhysicsEngine() const
{
    return m_physicsEngine;
}


const SkullbonezCore::Physics::PhysicsBodyStore& GameModelCollection::GetPhysicsBodyStore()
{
    RepairPhysicsBodyTopology();
    return m_physicsEngine.BodyStore();
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
{
    // Invariant: convenience reads repair topology only. Shape/material edits
    // commit through CommitEditedModelColliderState() so picks, saves, and
    // queries do not rebuild collider metadata just to inspect it.
    const bool repaired = RepairPhysicsBodyAndColliderTopology();
    assert( repaired );
    (void)repaired;
    return m_physicsEngine.Colliders();
}


bool GameModelCollection::RepairPhysicsBodyTopology()
{
    if ( m_physicsEngine.BodyStore().Count() != ModelCount() )
    {
        // Invariant: topology repair imports construction rows only. Same-count
        // state edits are physics-store authority and must not be overwritten by
        // a convenience read that rebuilds descriptor rows.
        std::vector<PhysicsBodyCreateDesc> bodyDescs = BuildBodyCreateDescsForReload( m_physicsEngine.BodyStore() );
        m_physicsEngine.RefreshBodyStore( bodyDescs );
    }
    return m_physicsEngine.BodyStore().Count() == ModelCount();
}


bool GameModelCollection::RepairPhysicsBodyAndColliderTopology()
{
    const int modelCount = ModelCount();
    const bool bodyTopologyChanged = m_physicsEngine.BodyStore().Count() != modelCount;
    const bool colliderTopologyChanged = m_physicsEngine.Colliders().Count() != modelCount;
    if ( bodyTopologyChanged || colliderTopologyChanged )
    {
        // Why: body rows can be repaired from explicit body descriptors, but
        // collider shape/material rows are store-owned once created. Count drift
        // in ColliderStore is a construction bug, not a reason to rediscover
        // shape facts from GameModel.
        if ( bodyTopologyChanged )
        {
            std::vector<PhysicsBodyCreateDesc> bodyDescs = BuildBodyCreateDescsForReload( m_physicsEngine.BodyStore() );
            m_physicsEngine.RefreshBodyStore( bodyDescs );
        }
        const bool colliderBindingsReady = m_physicsEngine.RefreshColliderSnapshot();
        return m_physicsEngine.BodyStore().Count() == modelCount && m_physicsEngine.Colliders().Count() == modelCount &&
               colliderBindingsReady;
    }
    return m_physicsEngine.BodyStore().Count() == modelCount && m_physicsEngine.Colliders().Count() == modelCount;
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::Colliders() const
{
    return m_physicsEngine.Colliders();
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::RenderInstances() const
{
    return m_physicsEngine.RenderInstances();
}


const char* GameModelCollection::DisplayNameAt( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= GetModelCount() )
    {
        return "";
    }
    return m_gameModels[static_cast<std::size_t>( modelIndex )].GetName();
}


int GameModelCollection::FindModelIndexByDisplayName( const char* name ) const
{
    if ( !name || name[0] == '\0' )
    {
        return -1;
    }
    for ( int modelIndex = 0; modelIndex < GetModelCount(); ++modelIndex )
    {
        if ( strcmp( DisplayNameAt( modelIndex ), name ) == 0 )
        {
            return modelIndex;
        }
    }
    return -1;
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::GetRenderInstanceStore()
{
    RefreshRenderInstances();
    return m_physicsEngine.RenderInstances();
}


GameModel& GameModelCollection::GetModelAtIndex( int index )
{
    return m_gameModels[index];
}


double GameModelCollection::GetSceneKineticEnergy()
{
    constexpr double REST_LINEAR_SPEED_SQ = 0.5 * 0.5;
    constexpr double REST_ANGULAR_SPEED_SQ = 0.3 * 0.3;
    double totalEnergy = 0.0;
    const PhysicsBodyStore& bodyStore = GetPhysicsBodyStore();
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
    const int modelCount = ModelCount();
    if ( !RepairPhysicsBodyTopology() || !m_physicsEngine.PrepareRenderStoreRefresh( modelCount ) )
    {
        return;
    }
    // Owner boundary: model material and presentation highlight values still
    // live in GameModelCollection. Physics prepares body/collider rows; this
    // cold projection edge packages the remaining render-facing facts once.
    Rendering::RenderInstanceStore& renderInstanceStore = m_physicsEngine.MutableRenderInstances();
    const PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
    const ColliderStore& colliderStore = m_physicsEngine.Colliders();
    m_renderPresentationRecords.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        const GameModel& model = m_gameModels[static_cast<std::size_t>( i )];
        Rendering::RenderInstancePresentationRecord& presentation =
            m_renderPresentationRecords[static_cast<std::size_t>( i )];
        presentation.material = model.GetRenderMaterial();
        strncpy_s( presentation.displayName, sizeof( presentation.displayName ), model.GetName(), _TRUNCATE );
        presentation.simpleRagdollPart = IsSimpleRagdollPart( i );
        presentation.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        presentation.audioContactAlpha = model.GetAudioContactHighlightAlpha();
    }
    renderInstanceStore.Refresh( m_renderPresentationRecords, bodyStore, colliderStore );
#ifdef _DEBUG
    m_physicsEngine.ValidateRenderStore( modelCount );
#endif
}


bool GameModelCollection::ApplyPhysicsBodyEdit( int modelIndex, const PhysicsBodyStateEdit& edit )
{
    const int modelCount = GetModelCount();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    // Invariant: cold editor/replay commands provide the changed body values.
    // Unchanged fields come from PhysicsBodyStore, not the presentation row.
    if ( static_cast<int>( m_authoredBodyDescs.size() ) != modelCount )
    {
        return false;
    }

    if ( m_physicsEngine.BodyStore().Count() != modelCount )
    {
        std::vector<PhysicsBodyCreateDesc> bodyDescs = BuildBodyCreateDescsForReload( m_physicsEngine.BodyStore() );
        m_physicsEngine.RefreshBodyStore( bodyDescs );
    }

    const PhysicsBodyRecord* bodyRecord = m_physicsEngine.BodyStore().RecordForModelIndex( modelIndex );
    if ( !bodyRecord )
    {
        return false;
    }

    PhysicsBodyCreateDesc bodyDesc = m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )];
    RefreshBodyDescFromStoreBodyState( *bodyRecord, edit, bodyDesc, FixedTreeReleaseRootForModelIndex( modelIndex ) );
    ApplyCollectionPhysicsPolicyToBodyDesc( bodyDesc, m_physicsMaterial, m_bodySimulationLimits, m_contactPolicy );
    m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )] = bodyDesc;
    m_physicsEngine.RefreshBodyFromDescriptor( bodyDesc, modelIndex, modelCount );

    // Why: body edits now stop at PhysicsBodyStore/m_authoredBodyDescs. Render,
    // replay, snapshots, and editor wake checks read those stores directly.
    return true;
}


bool GameModelCollection::ApplyPhysicsBodyColliderEdit( int modelIndex,
                                                        const PhysicsBodyStateEdit& edit,
                                                        PhysicsColliderCreateDesc colliderDesc )
{
    const int modelCount = GetModelCount();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    // Invariant: editor/replay scale edits are cold authoring events. Commit the
    // body row first, then replace exactly one collider row by stable handle.
    // Count drift still goes through topology repair because missing rows cannot
    // be patched by a single descriptor.
    if ( static_cast<int>( m_authoredBodyDescs.size() ) != modelCount )
    {
        return false;
    }

    if ( m_physicsEngine.BodyStore().Count() != modelCount )
    {
        std::vector<PhysicsBodyCreateDesc> bodyDescs = BuildBodyCreateDescsForReload( m_physicsEngine.BodyStore() );
        m_physicsEngine.RefreshBodyStore( bodyDescs );
    }

    const PhysicsBodyRecord* existingBody = m_physicsEngine.BodyStore().RecordForModelIndex( modelIndex );
    if ( !existingBody )
    {
        return false;
    }
    PhysicsBodyCreateDesc bodyDesc = m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )];
    RefreshBodyDescFromStoreBodyState( *existingBody, edit, bodyDesc, FixedTreeReleaseRootForModelIndex( modelIndex ) );
    bodyDesc.shape = colliderDesc.shape;
    bodyDesc.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( bodyDesc.shape );
    bodyDesc.volume = Math::CollisionDetection::GetShapeVolume( bodyDesc.shape );
    bodyDesc.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( bodyDesc.shape );
    bodyDesc.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( bodyDesc.shape );
    bodyDesc.usesWorldInertia = !std::holds_alternative<BoundingSphere>( bodyDesc.shape );
    ApplyCollectionPhysicsPolicyToBodyDesc( bodyDesc, m_physicsMaterial, m_bodySimulationLimits, m_contactPolicy );
    m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )] = bodyDesc;
    m_physicsEngine.RefreshBodyFromDescriptor( bodyDesc, modelIndex, modelCount );

    const bool colliderBindingsReady = m_physicsEngine.RefreshColliderSnapshot();

    const PhysicsBodyRecord* bodyRecord = m_physicsEngine.BodyStore().RecordForModelIndex( modelIndex );
    if ( !bodyRecord || !colliderBindingsReady || m_physicsEngine.Colliders().Count() != modelCount )
    {
        return false;
    }

    colliderDesc.body = bodyRecord->handle;
    colliderDesc.sceneObjectId = bodyRecord->sceneObjectId;
    ApplyCollectionPhysicsMaterialToColliderDesc( colliderDesc, m_physicsMaterial );
    const PhysicsColliderHandle collider = m_physicsEngine.Colliders().HandleForModelIndex( modelIndex );
    const ColliderRecord* existingCollider = m_physicsEngine.Colliders().RecordForHandle( collider );
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
    // descriptor sidecar. GameModel keeps presentation metadata only.
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
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }

    // Why: fixed-contact events come from the solver. The presentation timer
    // should trust the same dense body row instead of reopening legacy
    // model-side physics state to decide whether a body is fixed.
    const PhysicsBodyRecord* body = m_physicsEngine.BodyStore().RecordForModelIndex( modelIndex );
    if ( body && body->isFixed )
    {
        m_gameModels[static_cast<size_t>( modelIndex )].NotifyFixedContact( highlightSeconds );
    }
}


void GameModelCollection::TickContactHighlights( int modelCount, float deltaSeconds )
{
    // Why: contact highlights are presentation state on GameModel. Physics owns
    // when contact events happen, but the model collection owns the timers that
    // render/debug/audio views later sample.
    const int tickCount = (std::min)( modelCount, static_cast<int>( m_gameModels.size() ) );
    for ( int i = 0; i < tickCount; ++i )
    {
        m_gameModels[static_cast<size_t>( i )].TickFixedContactHighlight( deltaSeconds );
    }
}


void GameModelCollection::NotifyAudioContact( int modelIndex, float highlightSeconds )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }

    m_gameModels[static_cast<size_t>( modelIndex )].NotifyAudioContact( highlightSeconds );
}


bool GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                         float releaseImpulseStrength,
                                                         const Vector3& seedLinearVelocity,
                                                         const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= static_cast<int>( m_gameModels.size() ) )
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

    const PhysicsBodyHandle sourceBody = m_physicsEngine.BodyStore().HandleForModelIndex( sourceIndex );
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
    m_physicsEngine.BeginCollisionVisualFrame( static_cast<int>( m_gameModels.size() ) );
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


void GameModelCollection::RenderTornadoFieldVectors( const Matrix4& viewProj,
                                                     Rendering::IRenderCommandContext& renderCommands,
                                                     bool supportsDebugLines )
{
    m_physicsEngine.RenderTornadoFieldVectors( viewProj, renderCommands, supportsDebugLines );
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

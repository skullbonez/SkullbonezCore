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
  Body simulation limit: Scalar cap cached by the collection before models hand
    velocity state to RigidBody integration.
  Contact policy: Terrain and contact thresholds cached by the collection so
    existing and newly added models receive the same physics policy.
  PhysicsModelAccess: Stack-owned refresh facade that lets physics stores import
    model-owned authoring without making this collection inherit physics
    interfaces.
  Render instance store: Renderer-facing snapshot built from physics-owned pose
    and model-owned material/presentation state before frame passes.
  Collider descriptor: Value packet containing shape/material facts that
    PhysicsScene turns into a live ColliderStore row.
  Topology drift: A body/collider/model count mismatch that means compatibility
    stores must import model-owned construction data before stepping.
  Fixed-tree release: Compatibility rule that lets authored tree parts become
    dynamic when a related fixed part is hit strongly enough.
  Replay body id: Per-collection identity saved in replay samples so restore
    paths can reject stale model slots.
  Validation gate: Repository script that proves a class of changes before
    commit or PR.

Invariants:
  - Model vector order is stable subsystem identity for physics stores, render
    batches, replay ids, and scene snapshots; m_replayBodyIds is lockstep with
    m_gameModels and is the replay-id owner during this compatibility phase.
  - Render prep imports store-backed snapshots once before frame passes; render
    code must not rebuild GameModel-derived pose streams.
  - Owner-side compatibility release paths repair topology once before resolving
    body handles from PhysicsBodyStore.

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
#include <stdexcept>
#include <utility>
#include <variant>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeBodyRecordFromAuthoredModel;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsMaterial;
using SkullbonezCore::Physics::PhysicsModelAccess;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace SkullbonezCore
{
namespace Physics
{
PhysicsModelAccess::PhysicsModelAccess( GameObjects::GameModelCollection& collection ) : m_collection( collection )
{
}


int PhysicsModelAccess::ModelCount() const
{
    return m_collection.ModelCount();
}


void PhysicsModelAccess::ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates )
{
    m_collection.ReloadPhysicsBodies( bodyStore, sleepStates );
}


void PhysicsModelAccess::RefreshPhysicsBodyFromModel( PhysicsBodyStore& bodyStore, int modelIndex )
{
    m_collection.RefreshPhysicsBodyFromModel( bodyStore, modelIndex );
}


void PhysicsModelAccess::RefreshPhysicsColliders( ColliderStore& colliderStore, const PhysicsBodyStore& bodyStore )
{
    m_collection.RefreshPhysicsColliders( colliderStore, bodyStore );
}


void PhysicsModelAccess::RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore,
                                                 const PhysicsBodyStore& bodyStore,
                                                 const ColliderStore& colliderStore )
{
    m_collection.RefreshRenderInstances( renderInstanceStore, bodyStore, colliderStore );
}
} // namespace Physics
} // namespace SkullbonezCore

namespace
{
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}


// Why: this is the remaining cold collider-authoring import while scene and
// editor code still mutate GameModel fields. It returns a descriptor, not a
// live ColliderStore row, so physics remains the owner of row layout and handle
// identity. Deletion: scene/entity metadata writes PhysicsColliderCreateDesc
// directly. Checker: runtime boundaries reject GameModelCollection building
// ColliderRecord values from GameModel.
PhysicsColliderCreateDesc CaptureAuthoredColliderDesc( GameModel& model, const PhysicsBodyRecord& body )
{
    PhysicsColliderCreateDesc desc;
    desc.body = body.handle;
    desc.sceneObjectId = body.sceneObjectId;
    desc.shape = model.GetCollisionShape();
    desc.boundingRadius = model.GetBoundingRadius();
    desc.restitution = model.GetCoefficientRestitution();
    desc.friction = model.GetFrictionCoefficient();
    desc.contactMaterialId = model.GetContactMaterialId();
    desc.projectedSurfaceArea = model.GetProjectedSurfaceArea();
    desc.dragCoefficient = model.GetDragCoefficient();
    return desc;
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


bool IsDecimalDigit( char c )
{
    return c >= '0' && c <= '9';
}


bool IsReleasableEditorTreePartSuffix( const char* suffix )
{
    if ( !suffix || suffix[0] == '\0' )
    {
        return false;
    }
    return strcmp( suffix, "trunk" ) == 0 || strcmp( suffix, "low" ) == 0 || strcmp( suffix, "mid" ) == 0 ||
           strcmp( suffix, "top" ) == 0 || strncmp( suffix, "needle_", 7 ) == 0;
}


bool TryGetEditorTreeInstancePrefixLength( const char* name, size_t& outPrefixLength )
{
    outPrefixLength = 0;
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    if ( strstr( name, "_tree_" ) == nullptr && strncmp( name, "tree_", 5 ) != 0 )
    {
        return false;
    }

    const size_t nameLength = strlen( name );
    size_t marker = nameLength;
    for ( size_t i = 0; i + 5 < nameLength; ++i )
    {
        if ( name[i] == '_' && IsDecimalDigit( name[i + 1] ) && IsDecimalDigit( name[i + 2] ) &&
             IsDecimalDigit( name[i + 3] ) && name[i + 4] == '_' )
        {
            marker = i;
        }
    }

    if ( marker != nameLength )
    {
        const size_t prefixLength = marker + 5;
        if ( !IsReleasableEditorTreePartSuffix( name + prefixLength ) )
        {
            return false;
        }

        outPrefixLength = prefixLength;
        return true;
    }

    for ( size_t i = 0; i + 1 < nameLength; ++i )
    {
        if ( name[i] == '_' && IsReleasableEditorTreePartSuffix( name + i + 1 ) )
        {
            outPrefixLength = i + 1;
            return true;
        }
    }

    return false;
}


// Why: older saved scenes only preserve simple-ragdoll membership in part names.
// Convert that string compatibility into integer collection metadata once, at
// append/load time, so per-frame editor and replay code can stay off name scans.
// Owner: GameModelCollection construction metadata import. Deletion condition:
// scene/entity metadata serializes and loads collection groups directly. Checker
// budget: runtime boundaries block name/suffix parsing in editor transform code.
bool TryGetSimpleRagdollInstancePrefixLength( const char* name, size_t& outPrefixLength, int& outPartIndex )
{
    static constexpr const char* SIMPLE_RAGDOLL_SUFFIXES[] = { "torso",
                                                               "head",
                                                               "upper_arm_l",
                                                               "lower_arm_l",
                                                               "upper_arm_r",
                                                               "lower_arm_r",
                                                               "upper_leg_l",
                                                               "lower_leg_l",
                                                               "upper_leg_r",
                                                               "lower_leg_r" };

    outPrefixLength = 0;
    outPartIndex = -1;
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    const size_t nameLength = strlen( name );
    for ( int i = 0; i < static_cast<int>( sizeof( SIMPLE_RAGDOLL_SUFFIXES ) / sizeof( SIMPLE_RAGDOLL_SUFFIXES[0] ) );
          ++i )
    {
        const char* suffix = SIMPLE_RAGDOLL_SUFFIXES[static_cast<size_t>( i )];
        const size_t suffixLength = strlen( suffix );
        if ( nameLength <= suffixLength + 1 )
        {
            continue;
        }

        const size_t suffixStart = nameLength - suffixLength;
        if ( name[suffixStart - 1] != '_' || strncmp( name + suffixStart, suffix, suffixLength ) != 0 )
        {
            continue;
        }

        outPrefixLength = suffixStart - 1;
        outPartIndex = i;
        return outPrefixLength > 0;
    }
    return false;
}


bool SimpleRagdollPrefixMatches( const char* a, size_t aLength, const char* b, size_t bLength )
{
    return aLength == bLength && strncmp( a, b, aLength ) == 0;
}


void AssignRuntimeCollectionFromConstructionName( GameModel& gameModel,
                                                  std::vector<GameModel>& existingModels,
                                                  int newModelIndex )
{
    if ( gameModel.GetRuntimeCollectionKind() != GameModelCollectionKind::None )
    {
        return;
    }

    const char* sourceName = gameModel.GetName();
    size_t sourcePrefixLength = 0;
    int sourcePartIndex = -1;
    if ( TryGetSimpleRagdollInstancePrefixLength( sourceName, sourcePrefixLength, sourcePartIndex ) )
    {
        int rootModelIndex = sourcePartIndex == 0 ? newModelIndex : -1;
        for ( int i = 0; i < static_cast<int>( existingModels.size() ); ++i )
        {
            GameModel& existing = existingModels[static_cast<size_t>( i )];
            if ( existing.GetRuntimeCollectionKind() != GameModelCollectionKind::SimpleRagdoll )
            {
                continue;
            }

            size_t existingPrefixLength = 0;
            int existingPartIndex = -1;
            const char* existingName = existing.GetName();
            if ( !TryGetSimpleRagdollInstancePrefixLength( existingName, existingPrefixLength, existingPartIndex ) ||
                 !SimpleRagdollPrefixMatches( sourceName, sourcePrefixLength, existingName, existingPrefixLength ) )
            {
                continue;
            }

            int existingRoot = existing.GetRuntimeCollectionRootModelIndex();
            if ( existingRoot < 0 || existingRoot >= newModelIndex )
            {
                existingRoot = i;
            }
            if ( rootModelIndex < 0 || existingPartIndex == 0 )
            {
                rootModelIndex = existingRoot;
            }
            if ( sourcePartIndex == 0 )
            {
                // Why: legacy scenes can stream parts in any model order. When the
                // torso arrives late, move earlier siblings to the torso root so the
                // editor sees one collection instead of one group per early limb.
                existing.SetRuntimeCollection( GameModelCollectionKind::SimpleRagdoll,
                                               newModelIndex,
                                               existing.GetRuntimeCollectionPartIndex() );
            }
            if ( existingPartIndex == 0 )
            {
                break;
            }
        }
        if ( rootModelIndex < 0 )
        {
            rootModelIndex = newModelIndex;
        }
        gameModel.SetRuntimeCollection( GameModelCollectionKind::SimpleRagdoll, rootModelIndex, sourcePartIndex );
        return;
    }

    if ( !TryGetEditorTreeInstancePrefixLength( sourceName, sourcePrefixLength ) )
    {
        return;
    }

    int rootModelIndex = newModelIndex;
    int partIndex = 0;
    for ( int i = 0; i < static_cast<int>( existingModels.size() ); ++i )
    {
        const GameModel& existing = existingModels[static_cast<size_t>( i )];
        if ( existing.GetRuntimeCollectionKind() != GameModelCollectionKind::ReleasableTree )
        {
            continue;
        }

        size_t existingPrefixLength = 0;
        const char* existingName = existing.GetName();
        if ( !TryGetEditorTreeInstancePrefixLength( existingName, existingPrefixLength ) ||
             existingPrefixLength != sourcePrefixLength ||
             strncmp( existingName, sourceName, sourcePrefixLength ) != 0 )
        {
            continue;
        }

        rootModelIndex = existing.GetRuntimeCollectionRootModelIndex();
        if ( rootModelIndex < 0 || rootModelIndex >= newModelIndex )
        {
            rootModelIndex = i;
        }
        partIndex = (std::max)( partIndex, existing.GetRuntimeCollectionPartIndex() + 1 );
    }

    gameModel.SetRuntimeCollection( GameModelCollectionKind::ReleasableTree, rootModelIndex, partIndex );
}


} // namespace


GameModelCollection::GameModelCollection()
{
    // The collection is the stable owner of model order. Physics arrays,
    // render batches, debug overlays, and scene snapshots all index into this
    // vector, so preserving deterministic order matters even when the work is
    // delegated to renderer/physics helper classes.
    m_gameModels.reserve( ActiveGameModelCapacity() );
    m_replayBodyIds.reserve( ActiveGameModelCapacity() );
}


bool GameModelCollection::UpdateColliderStoreFromModel( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= GetModelCount() )
    {
        return false;
    }
    const PhysicsBodyRecord* bodyRecord = m_physicsEngine.BodyStore().RecordForModelIndex( modelIndex );
    assert( bodyRecord != nullptr );
    if ( !bodyRecord )
    {
        return false;
    }

    // Owner: GameModelCollection still owns the compatibility import because
    // scene/editor construction mutates GameModel collider fields today.
    // Reason: replace one dense ColliderStore row at explicit edit/config
    // boundaries instead of keeping a second authoring array or rescanning every
    // model on steady frames. Deletion: scene/entity creation writes collider
    // descriptors directly. Checker: runtime boundaries block ColliderStore
    // from accepting GameModel and block the deleted authoring sidecar names.
    const PhysicsColliderHandle collider = m_physicsEngine.Colliders().HandleForModelIndex( modelIndex );
    return m_physicsEngine.UpdateAuthoredCollider(
        collider,
        CaptureAuthoredColliderDesc( m_gameModels[static_cast<std::size_t>( modelIndex )], *bodyRecord ) );
}


void GameModelCollection::BindWorkerPool( SkullbonezCore::Threading::WorkerPool& workerPool )
{
    m_workerPool = &workerPool;
}


void GameModelCollection::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_physicsMaterial = Physics::PhysicsMaterial::FromConfig( config );
    m_bodySimulationLimits = Physics::BodySimulationLimits::FromConfig( config );
    m_contactPolicy = Physics::ContactPolicy::FromConfig( config );
    m_renderCollisionVolumes = config.runtimeRender.renderCollisionVolumes;
    m_shadowParallelPrep = config.shadowParallelPrep;
    m_physicsEngine.ApplyRuntimeConfig( config );
    const bool colliderRowsReady =
        m_physicsEngine.BodyStore().Count() == ModelCount() && m_physicsEngine.Colliders().Count() == ModelCount();
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        GameModel& model = m_gameModels[static_cast<std::size_t>( i )];
        model.ApplyPhysicsMaterial( m_physicsMaterial );
        model.ApplyBodySimulationLimits( m_bodySimulationLimits );
        model.ApplyContactPolicy( m_contactPolicy );
        if ( colliderRowsReady )
        {
            (void)UpdateColliderStoreFromModel( i );
        }
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


PhysicsBodyHandle GameModelCollection::AddGameModel( GameModel gameModel, uint32_t replayBodyId )
{
    return AppendGameModelAndPhysicsRows( std::move( gameModel ), replayBodyId, nullptr );
}


PhysicsBodyHandle GameModelCollection::AddGameModel( GameModel gameModel,
                                                     const PhysicsColliderCreateDesc& colliderDesc,
                                                     uint32_t replayBodyId )
{
    return AppendGameModelAndPhysicsRows( std::move( gameModel ), replayBodyId, &colliderDesc );
}


PhysicsBodyHandle GameModelCollection::AppendGameModelAndPhysicsRows( GameModel gameModel,
                                                                      uint32_t replayBodyId,
                                                                      const PhysicsColliderCreateDesc* colliderDesc )
{
    const int activeCapacity = ActiveGameModelCapacity();
    assert( static_cast<int>( m_gameModels.size() ) < activeCapacity && "Exceeded active game model capacity" );
    if ( static_cast<int>( m_gameModels.size() ) >= activeCapacity )
    {
        throw std::runtime_error(
            "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." );
    }
    AssignRuntimeCollectionFromConstructionName( gameModel, m_gameModels, static_cast<int>( m_gameModels.size() ) );
    assert( m_replayBodyIds.size() == m_gameModels.size() );
    // Invariant: replay identity is collection metadata, not a GameModel field.
    // Body/collider/render stores import explicit values at owner boundaries
    // while GameModel remains only the temporary source for unmigrated authoring.
    if ( replayBodyId == 0 )
    {
        replayBodyId = m_nextReplayBodyId++;
    }
    else
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, replayBodyId + 1u );
    }
    gameModel.ApplyPhysicsMaterial( m_physicsMaterial );
    gameModel.ApplyBodySimulationLimits( m_bodySimulationLimits );
    gameModel.ApplyContactPolicy( m_contactPolicy );
    // Hazard: append-time body/collider registration assumes existing rows are
    // aligned. Repair only pre-existing count drift; the newly pushed model
    // then appends one body row and one collider row instead of reloading all
    // rows through the compatibility model view.
    RepairPhysicsBodyAndColliderTopology();
    m_gameModels.push_back( std::move( gameModel ) );
    m_replayBodyIds.push_back( replayBodyId );
    const PhysicsBodyHandle bodyHandle =
        m_physicsEngine.RegisterAuthoredBody( MakeBodyRecordFromAuthoredModel( m_gameModels.back(), replayBodyId ) );
    const PhysicsBodyRecord* bodyRecord = m_physicsEngine.BodyStore().RecordForHandle( bodyHandle );
    assert( bodyRecord != nullptr );
    if ( !bodyRecord )
    {
        throw std::runtime_error( "Failed to resolve newly authored physics body record." );
    }
    // Owner: scene/editor creation provides shape facts; PhysicsScene owns live
    // collider rows. Reason: scene setup already has the radius/extents/hull
    // values, so recapturing them from GameModel only preserves migration debt.
    // Deletion: when all creation callers pass descriptors, remove the fallback
    // CaptureAuthoredColliderDesc path. Checker: scene setup guardrails reject
    // primitive/hull AddGameModel calls that omit a collider descriptor.
    PhysicsColliderCreateDesc authoredCollider =
        colliderDesc ? *colliderDesc : CaptureAuthoredColliderDesc( m_gameModels.back(), *bodyRecord );
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
    m_replayBodyIds.clear();
    m_physicsEngine.Clear();
    m_replayRenderPoseOverrides.clear();
    m_nextReplayBodyId = 1;
}


void GameModelCollection::ApplyReplayRenderPoseOverrides( Rendering::RenderInstanceStore& renderInstanceStore,
                                                          const Physics::ColliderStore& colliderStore )
{
    if ( m_replayRenderPoseOverrides.empty() )
    {
        return;
    }

    for ( const ReplayRenderPoseOverride& overridePose : m_replayRenderPoseOverrides )
    {
        const int modelIndex = overridePose.modelIndex;
        if ( modelIndex < 0 || modelIndex >= GetModelCount() )
        {
            continue;
        }

        // Why: replay scrub and prediction poses are presentation-only. Physics
        // and GameModel body mirrors stay live; only the CPU render snapshot
        // receives the historical/future pose after normal refresh completes.
        renderInstanceStore.OverridePose( modelIndex,
                                          overridePose.replayBodyId,
                                          overridePose.position,
                                          overridePose.orientation,
                                          colliderStore );
    }
    m_replayRenderPoseOverrides.clear();
}


void GameModelCollection::PrepareRenderInstances()
{
    // Why: object rendering now reads the render instance store for transforms.
    // Preparing it once here prevents each render pass from re-importing the
    // same physics pose repeatedly.
    PhysicsModelAccess modelAccess( *this );
    m_physicsEngine.RefreshRenderStore( modelAccess );
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
        // authority instead of falling back to GameModel pose recomputation.
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshRenderStore( modelAccess );
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
                                     *this,
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
    GameModelRenderer::BuildShadowCasterBatches( *this, outBatches );
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
    GameModelRenderer::RenderShadowCasters( helperContext, *this, view, proj, cinematic );
}


void GameModelCollection::RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                                      Assets::AssetSystem& assets,
                                                      Rendering::IRenderResourceFactory& renderResources,
                                                      const Matrix4& view,
                                                      const Matrix4& proj,
                                                      const float lightPos[4],
                                                      float alphaOverride )
{
    visualizer.SetAlphaOverride( alphaOverride );
    visualizer.Render( assets, renderResources, *this, view, proj, lightPos );
    visualizer.SetAlphaOverride( -1.0f );
}


void GameModelCollection::RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                                              const Matrix4& viewProjection,
                                              Geometry::Terrain* terrain )
{
    visualizer.Render( *this, viewProjection, terrain );
}


bool GameModelCollection::GetObjectShadowBounds( const Vector3& focus,
                                                 float maxDistance,
                                                 Vector3& outCenter,
                                                 float& outRadius,
                                                 float& outHeightRange )
{
    return GameModelRenderer::GetObjectShadowBounds( *this, focus, maxDistance, outCenter, outRadius, outHeightRange );
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
    // side of the compatibility restore mutates.
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

    GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    // Why: legacy render/editor readers still observe the GameModel body mirror
    // after replay restore. Keep this projection after the store-owned restore
    // succeeds so the mirror cannot decide which body is restored.
    model.SetFixed( fixed );
    model.SetPosition( position );
    model.SetOrientation( orientation );
    model.SetLinearVelocity( linearVelocity );
    model.SetAngularVelocity( angularVelocity );
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
    // side of the compatibility restore mutates.
    const PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( index );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    if ( !bodyRecord || bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    // Why: prediction restore swaps live/job state repeatedly. Restore the
    // physics record from the captured backup instead of recapturing GameModel.
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

    GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    // Why: prediction preview restore still mirrors state for legacy
    // presentation readers, but live replay identity is proved by
    // PhysicsBodyStore before any GameModel field is touched.
    model.SetFixed( fixed );
    model.SetPosition( position );
    model.SetOrientation( orientation );
    model.SetLinearVelocity( linearVelocity );
    model.SetAngularVelocity( angularVelocity );
    model.SetFixedContactHighlightSeconds( fixedContactHighlightSeconds );
    return true;
}


bool GameModelCollection::TryQueueReplayRenderPoseOverride( int index,
                                                            uint32_t replayBodyId,
                                                            const Vector3& position,
                                                            const Quaternion& orientation )
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = m_physicsEngine.BodyStore();
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( index );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    // Why: replay render overrides are keyed by the physics-owned body id. The
    // GameModel mirror may lag during scrub/prediction presentation, so it is
    // not allowed to approve which live body receives the override.
    if ( !bodyRecord || bodyStore.ModelIndexForHandle( body ) != index || bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    for ( ReplayRenderPoseOverride& overridePose : m_replayRenderPoseOverrides )
    {
        if ( overridePose.modelIndex == index )
        {
            overridePose.replayBodyId = replayBodyId;
            overridePose.position = position;
            overridePose.orientation = orientation;
            return true;
        }
    }

    ReplayRenderPoseOverride overridePose;
    overridePose.modelIndex = index;
    overridePose.replayBodyId = replayBodyId;
    overridePose.position = position;
    overridePose.orientation = orientation;
    m_replayRenderPoseOverrides.push_back( overridePose );
    // Why: replay render poses are one-frame presentation overrides. They are
    // queued for RenderInstanceStore so neither PhysicsBodyStore nor GameModel
    // receives historical/future scrub poses.
    return true;
}


void GameModelCollection::ClearReplayRenderPoseOverrides()
{
    m_replayRenderPoseOverrides.clear();
}


bool GameModelCollection::TrySetModelAngularVelocity( int index, const Vector3& angularVelocity )
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    m_gameModels[static_cast<std::size_t>( index )].SetAngularVelocity( angularVelocity );
    CommitEditedModelPhysicsState( index, false );
    return true;
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
    stats.modelReplayIdBytes = VectorCapacityBytes( m_replayBodyIds );
    stats.physicsStoreBytes = VectorCapacityBytes( bodyStore.Records() );
    stats.colliderStoreBytes = VectorCapacityBytes( colliderStore.Records() );
    stats.renderStoreBytes = VectorCapacityBytes( renderStore.Records() );
    stats.physicsWorldBytes = m_physicsEngine.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physicsEngine.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.modelReplayIdBytes + stats.physicsStoreBytes +
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
    assert( m_replayBodyIds.size() == m_gameModels.size() );
    if ( m_replayBodyIds.size() != m_gameModels.size() )
    {
        return false;
    }
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
        m_replayBodyIds.erase( m_replayBodyIds.begin() + static_cast<std::ptrdiff_t>( targetCount ),
                               m_replayBodyIds.end() );
    }
    m_nextReplayBodyId = 1;
    for ( const uint32_t replayBodyId : m_replayBodyIds )
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, replayBodyId + 1u );
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
    // commit through CommitEditedModelPhysicsState(..., true) so picks, saves,
    // and queries do not rebuild collider metadata just to inspect it.
    RepairPhysicsBodyAndColliderTopology();
    return m_physicsEngine.Colliders();
}


bool GameModelCollection::RepairPhysicsBodyTopology()
{
    if ( m_physicsEngine.BodyStore().Count() != ModelCount() )
    {
        // Invariant: topology repair imports construction rows only. Same-count
        // state edits are physics-store authority and must not be overwritten by
        // a convenience read that reloads the GameModel compatibility mirror.
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshBodyStore( modelAccess );
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
        // Why: one owner-side facade handles construction drift for both stores.
        // Equal-count body and collider state stays store-owned on steady frames.
        PhysicsModelAccess modelAccess( *this );
        if ( bodyTopologyChanged )
        {
            m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        if ( colliderTopologyChanged )
        {
            m_physicsEngine.RefreshColliderSnapshot( modelAccess );
        }
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


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::GetRenderInstanceStore()
{
    PhysicsModelAccess modelAccess( *this );
    m_physicsEngine.RefreshRenderStore( modelAccess );
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
    const std::vector<PhysicsBodyRecord>& bodies = bodyStore.Records();
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


void GameModelCollection::ReloadPhysicsBodies( SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                               const std::vector<uint8_t>& sleepStates )
{
    assert( m_replayBodyIds.size() == m_gameModels.size() );
    // Invariant: the replay-id sidecar and model rows advance together. A body
    // reload may import mutable model authoring data, but stable identity stays
    // in the sidecar and PhysicsBodyStore handle maps.
    bodyStore.LoadFromModels( m_gameModels, m_replayBodyIds, sleepStates );
}


void GameModelCollection::RefreshPhysicsBodyFromModel( SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                       int modelIndex )
{
    bodyStore.CaptureMutableStateFromModelAt( m_gameModels, modelIndex );
}


void GameModelCollection::RefreshPhysicsColliders( SkullbonezCore::Physics::ColliderStore& colliderStore,
                                                   const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    const int modelCount = ModelCount();
    assert( bodyStore.Count() == modelCount );
    const bool colliderTopologyChanged = colliderStore.Count() != modelCount;
    // Invariant: same-count refresh is identity repair only. Shape/material
    // data is rebuilt from GameModel below only when topology count drift means
    // some ColliderStore rows do not exist yet.
    colliderStore.RefreshBodyBindings( bodyStore );
    if ( !colliderTopologyChanged || bodyStore.Count() != modelCount )
    {
        return;
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        const bool updated = UpdateColliderStoreFromModel( i );
        assert( updated );
        (void)updated;
    }
}


void GameModelCollection::RefreshRenderInstances( SkullbonezCore::Rendering::RenderInstanceStore& renderInstanceStore,
                                                  const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                  const SkullbonezCore::Physics::ColliderStore& colliderStore )
{
    renderInstanceStore.Refresh( m_gameModels, bodyStore, colliderStore );
    ApplyReplayRenderPoseOverrides( renderInstanceStore, colliderStore );
}


void GameModelCollection::CommitEditedModelPhysicsState( int modelIndex, bool colliderChanged )
{
    // Owner: GameModelCollection still owns editor/replay model mutations.
    // Reason: render records now read physics stores for pose/shape, so direct
    // edits must commit changed body data before the next draw snapshot.
    // Deletion: remove this when editor/replay writes PhysicsBodyHandle-backed
    // body and collider commands directly instead of mutating GameModel first.
    // Checker: tools/check_runtime_boundaries.py blocks the old render refresh
    // path from depending on GameModel::GetModelMatrix again.
    if ( modelIndex < 0 || modelIndex >= GetModelCount() )
    {
        return;
    }

    PhysicsModelAccess modelAccess( *this );
    if ( colliderChanged )
    {
        if ( m_physicsEngine.BodyStore().Count() != ModelCount() )
        {
            m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        if ( m_physicsEngine.Colliders().Count() != ModelCount() )
        {
            m_physicsEngine.RefreshColliderSnapshot( modelAccess );
        }
        else
        {
            // Why: same-count collider edits replace exactly one dense store
            // row. That avoids a full model-order scan and keeps the existing
            // collider handle stable for editor, render, and query callers.
            (void)UpdateColliderStoreFromModel( modelIndex );
        }
    }
    else
    {
        m_physicsEngine.RefreshBodyFromModel( modelAccess, modelIndex );
    }
}


void GameModelCollection::NotifyFixedContact( int modelIndex, float highlightSeconds )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }

    // Why: fixed-contact events come from the solver. The presentation timer
    // should trust the same dense body row instead of reopening the GameModel
    // physics mirror to decide whether a body is fixed.
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

    // Compatibility owner: GameModelCollection runtime-tool edge.
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


void GameModelCollection::RenderTornadoFieldVectors( const Matrix4& viewProj )
{
    m_physicsEngine.RenderTornadoFieldVectors( viewProj );
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

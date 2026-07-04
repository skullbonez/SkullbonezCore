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
    batches, replay ids, and scene snapshots.
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

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeBodyRecordFromAuthoredModel;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
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

void AssignRuntimeCollectionFromConstructionName( GameModel& gameModel,
                                                  const std::vector<GameModel>& existingModels,
                                                  int newModelIndex )
{
    if ( gameModel.GetRuntimeCollectionKind() != GameModelCollectionKind::None )
    {
        return;
    }

    const char* sourceName = gameModel.GetName();
    size_t sourcePrefixLength = 0;
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
    for ( GameModel& model : m_gameModels )
    {
        model.ApplyPhysicsMaterial( m_physicsMaterial );
        model.ApplyBodySimulationLimits( m_bodySimulationLimits );
        model.ApplyContactPolicy( m_contactPolicy );
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


PhysicsBodyHandle GameModelCollection::AddGameModel( GameModel gameModel )
{
    const int activeCapacity = ActiveGameModelCapacity();
    assert( static_cast<int>( m_gameModels.size() ) < activeCapacity && "Exceeded active game model capacity" );
    if ( static_cast<int>( m_gameModels.size() ) >= activeCapacity )
    {
        throw std::runtime_error(
            "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." );
    }
    AssignRuntimeCollectionFromConstructionName( gameModel, m_gameModels, static_cast<int>( m_gameModels.size() ) );
    if ( gameModel.GetReplayBodyId() == 0 )
    {
        gameModel.SetReplayBodyId( m_nextReplayBodyId++ );
    }
    else
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, gameModel.GetReplayBodyId() + 1u );
    }
    gameModel.ApplyPhysicsMaterial( m_physicsMaterial );
    gameModel.ApplyBodySimulationLimits( m_bodySimulationLimits );
    gameModel.ApplyContactPolicy( m_contactPolicy );
    if ( m_physicsEngine.BodyStore().Count() != static_cast<int>( m_gameModels.size() ) )
    {
        // Hazard: append-time body registration assumes the existing model/body
        // rows are aligned. Repair only pre-existing count drift; the newly
        // pushed model then appends one body record instead of reloading all rows.
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshBodyStore( modelAccess );
    }
    m_gameModels.push_back( std::move( gameModel ) );
    return m_physicsEngine.RegisterAuthoredBody( MakeBodyRecordFromAuthoredModel( m_gameModels.back() ) );
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_physicsEngine.Clear();
    m_nextReplayBodyId = 1;
}


void GameModelCollection::RememberReplayRenderPoseOverride( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= GetModelCount() )
    {
        return;
    }
    if ( std::find( m_replayRenderPoseOverrideIndices.begin(), m_replayRenderPoseOverrideIndices.end(), modelIndex ) !=
         m_replayRenderPoseOverrideIndices.end() )
    {
        return;
    }
    m_replayRenderPoseOverrideIndices.push_back( modelIndex );
}


void GameModelCollection::ApplyReplayRenderPoseOverrides( Rendering::RenderInstanceStore& renderInstanceStore )
{
    if ( m_replayRenderPoseOverrideIndices.empty() )
    {
        return;
    }

    for ( int modelIndex : m_replayRenderPoseOverrideIndices )
    {
        if ( modelIndex < 0 || modelIndex >= GetModelCount() )
        {
            continue;
        }

        // Why: replay scrub and prediction poses are presentation-only. Physics
        // remains store-owned; the override is applied only to the CPU render
        // snapshot after the normal physics-backed refresh has completed.
        renderInstanceStore.OverridePoseFromModel( modelIndex, m_gameModels[static_cast<std::size_t>( modelIndex )] );
    }
    m_replayRenderPoseOverrideIndices.clear();
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

    GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    if ( model.GetReplayBodyId() != replayBodyId )
    {
        return false;
    }

    model.SetFixed( fixed );
    model.SetPosition( position );
    model.SetOrientation( orientation );
    model.SetLinearVelocity( linearVelocity );
    model.SetAngularVelocity( angularVelocity );
    return m_physicsEngine.RestoreReplayBodyState( index,
                                                   replayBodyId,
                                                   fixed,
                                                   position,
                                                   orientation,
                                                   linearVelocity,
                                                   angularVelocity,
                                                   mass,
                                                   inverseMass,
                                                   rotationalInertia,
                                                   inverseRotationalInertia );
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

    GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    if ( model.GetReplayBodyId() != replayBodyId )
    {
        return false;
    }

    model.SetFixed( fixed );
    model.SetPosition( position );
    model.SetOrientation( orientation );
    model.SetLinearVelocity( linearVelocity );
    model.SetAngularVelocity( angularVelocity );
    model.SetFixedContactHighlightSeconds( fixedContactHighlightSeconds );
    // Why: prediction restore swaps live/job state repeatedly. Restore the
    // physics record from the captured backup instead of recapturing GameModel.
    return m_physicsEngine.RestoreReplayBodyState( index,
                                                   replayBodyId,
                                                   fixed,
                                                   position,
                                                   orientation,
                                                   linearVelocity,
                                                   angularVelocity,
                                                   mass,
                                                   inverseMass,
                                                   rotationalInertia,
                                                   inverseRotationalInertia );
}


bool GameModelCollection::TrySetReplayRenderPose( int index,
                                                  uint32_t replayBodyId,
                                                  const Vector3& position,
                                                  const Quaternion& orientation )
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    if ( model.GetReplayBodyId() != replayBodyId )
    {
        return false;
    }

    model.SetPosition( position );
    model.SetOrientation( orientation );
    RememberReplayRenderPoseOverride( index );
    // Why: replay render poses are one-frame presentation overrides. Physics
    // body state must stay owned by explicit restore/prediction commands.
    return true;
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
    stats.physicsStoreBytes = VectorCapacityBytes( bodyStore.Records() );
    stats.colliderStoreBytes = VectorCapacityBytes( colliderStore.Records() );
    stats.renderStoreBytes = VectorCapacityBytes( renderStore.Records() );
    stats.physicsWorldBytes = m_physicsEngine.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physicsEngine.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.physicsStoreBytes + stats.colliderStoreBytes +
                       stats.renderStoreBytes + stats.physicsWorldBytes;
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
    if ( targetCount < m_gameModels.size() )
    {
        m_gameModels.erase( m_gameModels.begin() + static_cast<std::ptrdiff_t>( targetCount ), m_gameModels.end() );
    }
    m_nextReplayBodyId = 1;
    for ( const GameModel& model : m_gameModels )
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, model.GetReplayBodyId() + 1u );
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
    if ( m_physicsEngine.BodyStore().Count() != ModelCount() )
    {
        // Invariant: this accessor repairs topology only. Same-count body edits
        // are physics-store authority and must not be overwritten by a
        // convenience read that reloads the GameModel compatibility mirror.
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshBodyStore( modelAccess );
    }
    return m_physicsEngine.BodyStore();
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
{
    // Invariant: shape/material snapshots may refresh from GameModel, but body
    // rows keep PhysicsBodyStore authority unless topology count drift needs repair.
    GetPhysicsBodyStore();
    PhysicsModelAccess modelAccess( *this );
    m_physicsEngine.RefreshColliderSnapshot( modelAccess );
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


void GameModelCollection::WriteBackPhysicsBodies( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    bodyStore.WriteBackToModels( m_gameModels );
}


void GameModelCollection::WriteBackPhysicsBody( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                int modelIndex )
{
    bodyStore.WriteBackToModelAt( m_gameModels, modelIndex );
}


void GameModelCollection::ReloadPhysicsBodies( SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                               const std::vector<uint8_t>& sleepStates )
{
    bodyStore.LoadFromModels( m_gameModels, sleepStates );
}


void GameModelCollection::RefreshPhysicsBodyFromModel( SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                       int modelIndex )
{
    bodyStore.CaptureMutableStateFromModelAt( m_gameModels, modelIndex );
}


void GameModelCollection::RefreshPhysicsColliders( SkullbonezCore::Physics::ColliderStore& colliderStore,
                                                   const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    colliderStore.Refresh( m_gameModels, bodyStore );
}


void GameModelCollection::RefreshRenderInstances( SkullbonezCore::Rendering::RenderInstanceStore& renderInstanceStore,
                                                  const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                  const SkullbonezCore::Physics::ColliderStore& colliderStore )
{
    renderInstanceStore.Refresh( m_gameModels, bodyStore, colliderStore );
    ApplyReplayRenderPoseOverrides( renderInstanceStore );
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
        m_physicsEngine.RefreshColliderStore( modelAccess );
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

    GameModel& model = m_gameModels[static_cast<size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        model.NotifyFixedContact( highlightSeconds );
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


void GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                         const Vector3& seedLinearVelocity,
                                                         const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }

    const int sourceRootModelIndex =
        m_gameModels[static_cast<size_t>( sourceIndex )].GetRuntimeCollectionRootModelIndex();
    const float sourceY = m_gameModels[static_cast<size_t>( sourceIndex )].GetPosition().y;
    if ( m_gameModels[static_cast<size_t>( sourceIndex )].GetRuntimeCollectionKind() !=
             GameModelCollectionKind::ReleasableTree ||
         sourceRootModelIndex < 0 )
    {
        return;
    }

    // Why: runtime ray tools edit this GameModel-owned compatibility edge
    // directly. Repair topology once, then resolve body handles from the store
    // instead of bouncing back through the legacy model-index adapter per part.
    const int modelCount = static_cast<int>( m_gameModels.size() );
    const bool bodyTopologyChanged = m_physicsEngine.BodyStore().Count() != modelCount;
    const bool colliderTopologyChanged = m_physicsEngine.Colliders().Count() != modelCount;
    if ( bodyTopologyChanged || colliderTopologyChanged )
    {
        PhysicsModelAccess modelAccess( *this );
        if ( bodyTopologyChanged )
        {
            m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        if ( colliderTopologyChanged )
        {
            m_physicsEngine.RefreshColliderStore( modelAccess );
        }
    }
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        if ( i == sourceIndex )
        {
            continue;
        }

        GameModel& model = m_gameModels[static_cast<size_t>( i )];
        if ( model.GetRuntimeCollectionKind() != GameModelCollectionKind::ReleasableTree ||
             model.GetRuntimeCollectionRootModelIndex() != sourceRootModelIndex )
        {
            continue;
        }
        if ( model.GetPosition().y + 0.05f < sourceY )
        {
            continue;
        }

        if ( model.IsFixed() )
        {
            if ( !model.ReleasesFromFixedOnContact() )
            {
                continue;
            }
            model.SetFixed( false );
            model.SetLinearVelocity( seedLinearVelocity );
            model.SetAngularVelocity( seedAngularVelocity );
        }
        const PhysicsBodyHandle body = m_physicsEngine.BodyStore().HandleForModelIndex( i );
        if ( body.IsValid() )
        {
            m_physicsEngine.WakeBody( body );
        }
    }
}


void GameModelCollection::RunPhysics( float fChangeInTime,
                                      const Basics::EngineConfig& config,
                                      const Physics::PhysicsWorldForces& worldForces,
                                      Threading::WorkerPool& workerPool )
{
    const int modelCount = ModelCount();

    // Invariant: PhysicsBodyStore is the per-tick body authority. GameModel is
    // imported only when model/body topology changes; same-count editor or replay
    // mutations must use explicit commit paths before the step reads the store.
    const bool bodyTopologyChanged = m_physicsEngine.BodyStore().Count() != modelCount;
    // Why: collider metadata is construction/authoring state, not per-tick solver
    // state. The step repairs count drift without borrowing the GameModel owner
    // during steady-state frames.
    const bool colliderTopologyChanged = m_physicsEngine.Colliders().Count() != modelCount;
    if ( bodyTopologyChanged || colliderTopologyChanged )
    {
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

    TickContactHighlights( modelCount, fChangeInTime );

    const char* const* diagnosticNames = nullptr;
    int diagnosticNameCount = 0;
#ifdef _DEBUG
    std::vector<const char*> physicsDiagnosticsModelNames;
    if ( m_physicsEngine.ShouldEmitStepDiagnostics() || m_physicsEngine.ShouldEmitCollisionTimeDiagnostics() )
    {
        // Why: Debug rows still need presentation names, but the physics step
        // receives them as a cold pointer table instead of borrowing the model owner.
        FillPhysicsDiagnosticsNames( m_physicsEngine.BodyStore().Count(), physicsDiagnosticsModelNames );
        diagnosticNames = physicsDiagnosticsModelNames.empty() ? nullptr : physicsDiagnosticsModelNames.data();
        diagnosticNameCount = static_cast<int>( physicsDiagnosticsModelNames.size() );
    }
#endif

    m_physicsEngine.Step( fChangeInTime, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );

    // Why: fixed-contact highlights are GameModel presentation feedback. The
    // solver records compact body indices; the collection applies them before
    // bulk writeback changes fixed flags in the compatibility model mirror.
    for ( int index : m_physicsEngine.GetFixedContactHighlightBodies() )
    {
        NotifyFixedContact( index, 0.5f );
    }

    // Compatibility owner: GameModelCollection step boundary.
    // Reason: editor and replay compatibility consumers still read GameModel
    // pose/state after the store-owned solver has finished.
    // Deletion condition: those consumers read PhysicsBodyStore or
    // handle-addressed physics commands directly. Checker budget: boundary grep
    // keeps bulk solver writeback out of PhysicsWorld and PhysicsScene stepping.
    WriteBackPhysicsBodies( m_physicsEngine.BodyStore() );
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

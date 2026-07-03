/*
File: SkullbonezSource/GameObjects/GameModelCollection.cpp
Purpose:
  Owns all scene models and delegates rendering, physics, and snapshots.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SoA (Structure of Arrays): Data layout that stores each field in a separate
  contiguous array for cache-friendly iteration.
  Physics material: Per-object friction and drag coefficients cached by the
    collection before models are added or reconfigured.
  Body simulation limit: Scalar cap cached by the collection before models hand
    velocity state to RigidBody integration.
  Contact policy: Terrain and contact thresholds cached by the collection so
    existing and newly added models receive the same physics policy.
  Replay body id: Per-collection identity saved in replay samples so restore
    paths can reject stale model slots.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Model vector order is stable subsystem identity for physics stores, render
    batches, replay ids, and scene snapshots.
  - Any mutation that changes model state visible to hot streams must invalidate
    or refresh the SoA cache before clients read it.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelCollection.h"

#include "GameModelCollectionPhysicsAdapter.h"
#include "../Core/MainMemoryStats.h"
#include "../Core/SkullScope.h"
#include "../Physics/Debug/CollisionVisualizer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#ifdef _DEBUG
#include "../Physics/PhysicsDiagnosticsModel.h"
#endif
#include "../Rendering/GameModelRenderer.h"
#include "../Scene/SceneSnapshotWriter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <stdexcept>
#ifdef _DEBUG
#include <type_traits>
#endif
#include <utility>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::ShadowFrameData;

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


void GameModelCollection::AddGameModel( GameModel gameModel )
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
    m_gameModels.push_back( std::move( gameModel ) );
    InvalidateSoA();
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_soaCache.Clear();
    m_physicsEngine.Clear();
    m_nextReplayBodyId = 1;
}


void GameModelCollection::InvalidateSoA()
{
    // SoA caches are derived data. Any direct access to a GameModel can mutate
    // body/render state, so mark the cache dirty and let the next hot-path user
    // rebuild it from the authoritative vector.
    m_soaCache.Invalidate();
}


void GameModelCollection::PrepareRenderStreams()
{
    GameModelStreamProvider::PrepareRenderStreams( m_soaCache, m_gameModels );
}


int GameModelCollection::GetRenderModelCount() const
{
    return GetModelCount();
}


int GameModelCollection::CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount )
{
    if ( !outMatrixFloats || maxModelCount <= 0 )
    {
        return 0;
    }

    const int modelCount = (std::min)( GetModelCount(), maxModelCount );
    for ( int i = 0; i < modelCount; ++i )
    {
        const Matrix4 modelMatrix = m_gameModels[static_cast<std::size_t>( i )].GetModelMatrix();
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

    return m_gameModels[index].GetPosition();
}


int GameModelCollection::GetModelCount() const
{
    return static_cast<int>( m_gameModels.size() );
}


int GameModelCollection::ModelCount() const
{
    return GetModelCount();
}


GameModel* GameModelCollection::MutableModelData()
{
    return m_gameModels.data();
}


const GameModel* GameModelCollection::ModelData() const
{
    return m_gameModels.data();
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
bool GameModelCollection::TryGetPhysicsDiagnosticsModel( int index,
                                                         Physics::PhysicsDiagnosticsModelRecord& outRecord ) const
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    const GameModel& model = m_gameModels[static_cast<std::size_t>( index )];
    outRecord = Physics::PhysicsDiagnosticsModelRecord{};
    outRecord.name = model.GetName();
    outRecord.shapeName = model.GetShapeName();
    outRecord.position = model.GetPosition();
    outRecord.velocity = model.GetVelocity();
    outRecord.angularVelocity = model.GetAngularVelocity();
    outRecord.rotationalInertia = model.GetRotationalInertia();
    model.GetOrientation().GetComponents( outRecord.qx, outRecord.qy, outRecord.qz, outRecord.qw );
    outRecord.mass = model.GetMass();
    outRecord.inverseMass = model.GetInvertedMass();

    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, Math::CollisionDetection::BoundingSphere> )
            {
                outRecord.radius = shape.GetRadius();
            }
            else if constexpr ( std::is_same_v<ShapeT, Math::CollisionDetection::BoundingBox> )
            {
                outRecord.halfExtents = shape.GetHalfExtents();
            }
            else
            {
                outRecord.radius = shape.GetBoundingRadius();
                outRecord.hullName = shape.GetName();
                outRecord.hullVertices = shape.GetVertexCount();
                outRecord.hullFaces = shape.GetFaceCount();
                outRecord.hullEdges = shape.GetEdgeCount();
            }
        },
        model.GetCollisionShape() );
    return true;
}
#endif


bool GameModelCollection::TryRestoreReplayBodyState( int index,
                                                     uint32_t replayBodyId,
                                                     bool fixed,
                                                     const Vector3& position,
                                                     const Quaternion& orientation,
                                                     const Vector3& linearVelocity,
                                                     const Vector3& angularVelocity )
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
    model.ClearImpulseForce();
    InvalidateSoA();
    return true;
}


bool GameModelCollection::TryRestoreReplayPredictionBodyState( int index,
                                                               uint32_t replayBodyId,
                                                               bool fixed,
                                                               const Vector3& position,
                                                               const Quaternion& orientation,
                                                               const Vector3& linearVelocity,
                                                               const Vector3& angularVelocity,
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
    InvalidateSoA();
    return true;
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
    InvalidateSoA();
    return true;
}


bool GameModelCollection::TrySetModelAngularVelocity( int index, const Vector3& angularVelocity )
{
    if ( index < 0 || index >= GetModelCount() )
    {
        return false;
    }

    m_gameModels[static_cast<std::size_t>( index )].SetAngularVelocity( angularVelocity );
    InvalidateSoA();
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
    stats.soaCacheBytes = static_cast<uint64_t>( sizeof( m_soaCache ) );
    stats.physicsStoreBytes = VectorCapacityBytes( bodyStore.Records() );
    stats.colliderStoreBytes = VectorCapacityBytes( colliderStore.Records() );
    stats.renderStoreBytes = VectorCapacityBytes( renderStore.Records() );
    stats.physicsWorldBytes = m_physicsEngine.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = m_physicsEngine.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.soaCacheBytes + stats.physicsStoreBytes +
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
    if ( targetCount < m_gameModels.size() )
    {
        m_gameModels.erase( m_gameModels.begin() + static_cast<std::ptrdiff_t>( targetCount ), m_gameModels.end() );
    }
    m_nextReplayBodyId = 1;
    for ( const GameModel& model : m_gameModels )
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, model.GetReplayBodyId() + 1u );
    }
    InvalidateSoA();
    return true;
}


void GameModelCollection::CaptureReplaySolverWorldSnapshot( ReplaySolverWorldSnapshot& outSnapshot ) const
{
    m_physicsEngine.CaptureReplaySolverSnapshot( outSnapshot, static_cast<int>( m_gameModels.size() ) );
}


bool GameModelCollection::RestoreReplaySolverWorldSnapshot( const ReplaySolverWorldSnapshot& snapshot )
{
    const bool restored =
        m_physicsEngine.RestoreReplaySolverSnapshot( snapshot, static_cast<int>( m_gameModels.size() ) );
    if ( restored )
    {
        InvalidateSoA();
    }
    return restored;
}


GameModelBodyStream GameModelCollection::GetBodyStream()
{
    return GameModelStreamProvider::GetBodyStream( m_soaCache, m_gameModels );
}


GameModelBodyStream GameModelCollection::GetPhysicsBodyStream()
{
    return GetBodyStream();
}


GameModelRenderStream GameModelCollection::GetRenderStream()
{
    return GameModelStreamProvider::GetRenderStream( m_soaCache, m_gameModels );
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
    m_physicsEngine.RefreshBodyStore( *this );
    return m_physicsEngine.BodyStore();
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
{
    m_physicsEngine.RefreshColliderStore( *this );
    return m_physicsEngine.Colliders();
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::GetRenderInstanceStore()
{
    m_physicsEngine.RefreshRenderStore( *this );
    return m_physicsEngine.RenderInstances();
}


GameModel& GameModelCollection::GetModelAtIndex( int index )
{
    InvalidateSoA();
    return m_gameModels[index];
}


double GameModelCollection::GetSceneKineticEnergy()
{
    constexpr double REST_LINEAR_SPEED_SQ = 0.5 * 0.5;
    constexpr double REST_ANGULAR_SPEED_SQ = 0.3 * 0.3;
    double totalEnergy = 0.0;
    for ( GameModel& model : m_gameModels )
    {
        if ( model.IsFixed() )
        {
            continue;
        }

        const Vector3& vel = model.GetVelocity();
        const Vector3& omega = model.GetAngularVelocity();
        const double speedSq = static_cast<double>( vel.x ) * vel.x + static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x + static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        if ( speedSq < REST_LINEAR_SPEED_SQ && omegaSq < REST_ANGULAR_SPEED_SQ )
        {
            continue;
        }

        const Vector3& inertia = model.GetRotationalInertia();
        const double angularEnergy = 0.5 * ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                             static_cast<double>( inertia.y ) * omega.y * omega.y +
                                             static_cast<double>( inertia.z ) * omega.z * omega.z );
        totalEnergy += 0.5 * static_cast<double>( model.GetMass() ) * speedSq + angularEnergy;
    }
    return totalEnergy;
}


void GameModelCollection::InvalidatePhysicsStreams()
{
    InvalidateSoA();
}


SkullbonezCore::Physics::PhysicsBodyEventSink& GameModelCollection::BodyEvents()
{
    return *this;
}


SkullbonezCore::Physics::PhysicsDiagnosticsView GameModelCollection::GetPhysicsDiagnosticsView() const
{
    return m_physicsEngine.GetDiagnosticsView();
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


void GameModelCollection::ReleaseAttachedFixedTreeParts(
    const SkullbonezCore::Physics::PhysicsFixedTreeReleaseEvent& event )
{
    ReleaseAttachedFixedTreeParts( event.sourceIndex, event.seedLinearVelocity, event.seedAngularVelocity );
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
        WakeModel( i );
    }
}


void GameModelCollection::RunPhysics( float fChangeInTime,
                                      const Basics::EngineConfig& config,
                                      const Physics::PhysicsWorldForces& worldForces,
                                      Threading::WorkerPool& workerPool )
{
    m_physicsEngine.Step( *this, fChangeInTime, config, worldForces, workerPool );
}


void GameModelCollection::WakeModel( int index )
{
    // Why: these legacy entry points still speak model indices, but physics
    // commands should cross the boundary as validated body handles.
    GameModelCollectionPhysicsAdapter( *this ).WakeBodyForModelIndex( index );
}


void GameModelCollection::SeedModelAsleep( int index )
{
    GameModelCollectionPhysicsAdapter( *this ).SeedBodyAsleepForModelIndex( index );
}


void GameModelCollection::ApplyBodyImpulse( int index, const Vector3& impulse, const Vector3& localApplicationPoint )
{
    GameModelCollectionPhysicsAdapter( *this ).ApplyBodyImpulseForModelIndex( index, impulse, localApplicationPoint );
}


void GameModelCollection::SetPendingBodyImpulse( int index,
                                                 const Vector3& impulse,
                                                 const Vector3& localApplicationPoint )
{
    GameModelCollectionPhysicsAdapter( *this ).SetPendingBodyImpulseForModelIndex( index,
                                                                                   impulse,
                                                                                   localApplicationPoint );
}


void GameModelCollection::SetPhysicsSleepEnabled( bool enabled )
{
    m_physicsEngine.SetSleepEnabled( enabled );
}


void GameModelCollection::ClearPointJointConstraints()
{
    m_physicsEngine.ClearPointJointConstraints();
}


void GameModelCollection::AddPointJointConstraint( const Physics::PointJointConstraint& constraint )
{
    m_physicsEngine.AddPointJointConstraint( constraint );
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

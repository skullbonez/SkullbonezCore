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
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelCollection.h"

#include "../Physics/Debug/CollisionVisualizer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../Rendering/GameModelRenderer.h"
#include "../Scene/SceneSnapshotWriter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <utility>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
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

    if ( marker == nameLength )
    {
        return false;
    }

    const size_t prefixLength = marker + 5;
    if ( !IsReleasableEditorTreePartSuffix( name + prefixLength ) )
    {
        return false;
    }

    outPrefixLength = prefixLength;
    return true;
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


void GameModelCollection::AddGameModel( GameModel gameModel )
{
    const int activeCapacity = ActiveGameModelCapacity();
    assert( static_cast<int>( m_gameModels.size() ) < activeCapacity && "Exceeded active game model capacity" );
    if ( static_cast<int>( m_gameModels.size() ) >= activeCapacity )
    {
        throw std::runtime_error(
            "Exceeded active game model capacity; raise --model-capacity or game_model_capacity." );
    }
    if ( gameModel.GetReplayBodyId() == 0 )
    {
        gameModel.SetReplayBodyId( m_nextReplayBodyId++ );
    }
    else
    {
        m_nextReplayBodyId = (std::max)( m_nextReplayBodyId, gameModel.GetReplayBodyId() + 1u );
    }
    m_gameModels.push_back( std::move( gameModel ) );
    InvalidateSoA();
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_soaCache.Clear();
    m_physicsScene.Clear();
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


void GameModelCollection::RenderModels( const Matrix4& view,
                                        const Matrix4& proj,
                                        const float lightPos[4],
                                        const CinematicRenderConfig* cinematic,
                                        const ShadowFrameData* shadow,
                                        float materialAlpha,
                                        const std::vector<uint8_t>* modelMask,
                                        bool drawMaskedModels )
{
    GameModelRenderer::RenderModels( *this,
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


void GameModelCollection::RenderShadowCasterBatches( const Rendering::ShadowCasterBatches& batches,
                                                     const Matrix4& view,
                                                     const Matrix4& proj,
                                                     const CinematicRenderConfig* cinematic )
{
    GameModelRenderer::SubmitShadowCasterBatches( batches, view, proj, cinematic );
}


void GameModelCollection::RenderShadowCasters( const Matrix4& view,
                                               const Matrix4& proj,
                                               const CinematicRenderConfig* cinematic )
{
    GameModelRenderer::RenderShadowCasters( *this, view, proj, cinematic );
}


void GameModelCollection::RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                                      const Matrix4& view,
                                                      const Matrix4& proj,
                                                      const float lightPos[4],
                                                      float alphaOverride )
{
    visualizer.SetAlphaOverride( alphaOverride );
    visualizer.Render( *this, view, proj, lightPos );
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


const std::vector<GameModel>& GameModelCollection::Models() const
{
    return m_gameModels;
}


std::vector<GameModel>& GameModelCollection::PhysicsModels()
{
    return m_gameModels;
}


const std::vector<GameModel>& GameModelCollection::PhysicsModels() const
{
    return m_gameModels;
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
    m_physicsScene.CaptureReplaySolverSnapshot( outSnapshot, static_cast<int>( m_gameModels.size() ) );
}


bool GameModelCollection::RestoreReplaySolverWorldSnapshot( const ReplaySolverWorldSnapshot& snapshot )
{
    const bool restored =
        m_physicsScene.RestoreReplaySolverSnapshot( snapshot, static_cast<int>( m_gameModels.size() ) );
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


GameModelRenderStream GameModelCollection::GetRenderStream()
{
    return GameModelStreamProvider::GetRenderStream( m_soaCache, m_gameModels );
}


const SkullbonezCore::Physics::PhysicsBodyStore& GameModelCollection::GetPhysicsBodyStore()
{
    m_physicsScene.RefreshBodyStore( *this );
    return m_physicsScene.BodyStore();
}


const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
{
    m_physicsScene.RefreshColliderStore( *this );
    return m_physicsScene.Colliders();
}


const SkullbonezCore::Rendering::RenderInstanceStore& GameModelCollection::GetRenderInstanceStore()
{
    m_physicsScene.RefreshRenderStore( *this );
    return m_physicsScene.RenderInstances();
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


void GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                         const Vector3& seedLinearVelocity,
                                                         const Vector3& seedAngularVelocity )
{
    if ( sourceIndex < 0 || sourceIndex >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }

    const char* sourceName = m_gameModels[static_cast<size_t>( sourceIndex )].GetName();
    const float sourceY = m_gameModels[static_cast<size_t>( sourceIndex )].GetPosition().y;
    size_t sourcePrefixLength = 0;
    if ( !TryGetEditorTreeInstancePrefixLength( sourceName, sourcePrefixLength ) )
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
        const char* modelName = model.GetName();
        size_t modelPrefixLength = 0;
        if ( !TryGetEditorTreeInstancePrefixLength( modelName, modelPrefixLength ) ||
             modelPrefixLength != sourcePrefixLength || strncmp( modelName, sourceName, sourcePrefixLength ) != 0 )
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


void GameModelCollection::RunPhysics( float fChangeInTime )
{
    m_physicsScene.RunPhysics( *this, fChangeInTime );
}


void GameModelCollection::WakeModel( int index )
{
    m_physicsScene.WakeModel( *this, index );
}


void GameModelCollection::SeedModelAsleep( int index )
{
    m_physicsScene.SeedModelAsleep( *this, index );
}


void GameModelCollection::SetPhysicsSleepEnabled( bool enabled )
{
    m_physicsScene.SetPhysicsSleepEnabled( enabled );
}


void GameModelCollection::ClearPointJointConstraints()
{
    m_physicsScene.ClearPointJointConstraints();
}


void GameModelCollection::AddPointJointConstraint( const Physics::PointJointConstraint& constraint )
{
    m_physicsScene.AddPointJointConstraint( constraint );
}


void GameModelCollection::BeginCollisionVisualFrame()
{
    m_physicsScene.BeginCollisionVisualFrame( static_cast<int>( m_gameModels.size() ) );
}


void GameModelCollection::EndCollisionVisualFrame()
{
    m_physicsScene.EndCollisionVisualFrame();
}


void GameModelCollection::SetTornadoFieldConfig( const Physics::TornadoFieldConfig& config )
{
    m_physicsScene.SetTornadoFieldConfig( config );
}


void GameModelCollection::RenderTornadoFieldVectors( const Matrix4& viewProj )
{
    m_physicsScene.RenderTornadoFieldVectors( viewProj );
}


#ifdef _DEBUG
void GameModelCollection::SetPhysicsRegressionLogPath( const char* path )
{
    m_physicsScene.SetPhysicsRegressionLogPath( path );
}


void GameModelCollection::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_physicsScene.SetPhysicsCollisionTimeLogPath( path );
}


void GameModelCollection::SetPhysicsDiagnosticsPath( const char* path )
{
    m_physicsScene.SetPhysicsDiagnosticsPath( path );
}


void GameModelCollection::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_physicsScene.SetPhysicsDiagnosticsRunId( runId );
}


bool GameModelCollection::SetPhysicsDiagnosticsSuppressed( bool suppressed )
{
    return m_physicsScene.SetDiagnosticsSuppressed( suppressed );
}
#endif

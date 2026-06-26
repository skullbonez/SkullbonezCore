/*
File: SkullbonezSource/Physics/PhysicsScene.cpp
Purpose:
  Coordinates PhysicsWorld with deterministic body/collider/render stores.

Mental model:
  PhysicsWorld still owns the solver. PhysicsScene is the coordination boundary
  that refreshes body, collider, and render snapshots around that solver without
  changing the live GameModelCollection order.

Glossary:
  Solver: Physics step that integrates motion and applies collision/contact
    impulses.
  Store: Ordered snapshot of one ownership concern such as bodies, colliders,
    or render instances.
  Determinism: Same inputs produce byte-exact validation artifacts.

Invariants:
  - Store refresh order must preserve GameModelCollection physics model order.
  - RunPhysics delegates to PhysicsWorld without changing floating-point order.

Related:
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#include "PhysicsScene.h"
#include "PhysicsApi.h"

#include <cassert>
#include <cstddef>

#include "../GameObjects/GameModelCollection.h"

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsScene;


void PhysicsScene::Clear()
{
    m_world.Clear();
    m_bodyStore.Clear();
    m_colliderStore.Clear();
    m_renderInstanceStore.Clear();
}


void PhysicsScene::RefreshStores( GameModelCollection& collection )
{
    RefreshPhysicsStores( collection );
    RefreshRenderStore( collection );
}


void PhysicsScene::RefreshPhysicsStores( GameModelCollection& collection )
{
    RefreshBodyStore( collection );
    RefreshColliderStore( collection );
#ifdef _DEBUG
    ValidatePhysicsStoreMappings( static_cast<int>( collection.PhysicsModels().size() ) );
#endif
}


void PhysicsScene::RefreshBodyStore( GameModelCollection& collection )
{
    std::vector<SkullbonezCore::GameObjects::GameModel>& models = collection.PhysicsModels();
    m_bodyStore.LoadFromModels( models, m_world.GetSleepStates() );
}


void PhysicsScene::ClearPendingBodyImpulses()
{
    m_bodyStore.ClearPendingImpulses();
}


void PhysicsScene::RefreshColliderStore( GameModelCollection& collection )
{
    std::vector<SkullbonezCore::GameObjects::GameModel>& models = collection.PhysicsModels();
    m_colliderStore.Refresh( models );
}


void PhysicsScene::RefreshRenderStore( GameModelCollection& collection )
{
    std::vector<SkullbonezCore::GameObjects::GameModel>& models = collection.PhysicsModels();
    m_renderInstanceStore.Refresh( models );
#ifdef _DEBUG
    ValidateRenderStoreMappings( static_cast<int>( models.size() ) );
#endif
}


#ifdef _DEBUG
void PhysicsScene::ValidatePhysicsStoreMappings( int modelCount ) const
{
    assert( m_bodyStore.Count() == modelCount );
    assert( m_colliderStore.Count() == modelCount );

    const std::vector<PhysicsBodyRecord>& bodies = m_bodyStore.Records();
    const std::vector<ColliderRecord>& colliders = m_colliderStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodies[index];
        const ColliderRecord& collider = colliders[index];
        const PhysicsBodyHandle bodyHandle = m_bodyStore.HandleForModelIndex( i );
        const PhysicsColliderHandle colliderHandle = m_colliderStore.HandleForModelIndex( i );

        assert( bodyHandle.IsValid() );
        assert( colliderHandle.IsValid() );
        assert( body.handle == bodyHandle );
        assert( collider.handle == colliderHandle );
        assert( collider.body == bodyHandle );
        assert( m_bodyStore.ModelIndexForHandle( bodyHandle ) == i );
        assert( m_colliderStore.ModelIndexForHandle( colliderHandle ) == i );
        assert( body.legacyModelIndex == i );
        assert( collider.legacyModelIndex == i );
        assert( body.replayBodyId == collider.replayBodyId );
        assert( body.sceneObjectId == collider.sceneObjectId );
    }
}


void PhysicsScene::ValidateRenderStoreMappings( int modelCount ) const
{
    assert( m_renderInstanceStore.Count() == modelCount );

    const std::vector<SkullbonezCore::Rendering::RenderInstanceRecord>& instances = m_renderInstanceStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const SkullbonezCore::Rendering::RenderInstanceRecord& instance = instances[index];
        const SkullbonezCore::Rendering::RenderInstanceHandle renderHandle =
            m_renderInstanceStore.HandleForModelIndex( i );

        assert( renderHandle.IsValid() );
        assert( instance.handle == renderHandle );
        assert( m_renderInstanceStore.ModelIndexForHandle( renderHandle ) == i );
        assert( instance.legacyModelIndex == i );
    }
}
#endif


void PhysicsScene::RunPhysics( GameModelCollection& collection, float fChangeInTime )
{
    std::vector<SkullbonezCore::GameObjects::GameModel>& models = collection.PhysicsModels();
    m_bodyStore.LoadFromModels( models, m_world.GetSleepStates() );
    m_world.RunPhysics( collection, m_bodyStore, fChangeInTime );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    m_bodyStore.WriteBackToModels( models );
}


void PhysicsScene::WakeModel( GameModelCollection& collection, int index )
{
    RefreshBodyStore( collection );
    m_bodyStore.WakeBody( index );
    m_world.WakeModel( collection, index );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    m_bodyStore.WriteBackToModelAt( collection.PhysicsModels(), index );
}


void PhysicsScene::SeedModelAsleep( GameModelCollection& collection, int index )
{
    RefreshBodyStore( collection );
    m_bodyStore.SeedBodyAsleep( index );
    m_world.SeedModelAsleep( collection, index );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    m_bodyStore.WriteBackToModelAt( collection.PhysicsModels(), index );
}


void PhysicsScene::ApplyBodyImpulse( GameModelCollection& collection,
                                     int bodyIndex,
                                     const Math::Vector::Vector3& impulse,
                                     const Math::Vector::Vector3& localApplicationPoint )
{
    SetPendingBodyImpulse( collection, bodyIndex, impulse, localApplicationPoint );
    WakeModel( collection, bodyIndex );
}


void PhysicsScene::SetPendingBodyImpulse( GameModelCollection& collection,
                                          int bodyIndex,
                                          const Math::Vector::Vector3& impulse,
                                          const Math::Vector::Vector3& localApplicationPoint )
{
    RefreshBodyStore( collection );
    if ( m_bodyStore.SetPendingBodyImpulse( bodyIndex, impulse, localApplicationPoint ) )
    {
        m_bodyStore.WriteBackToModelAt( collection.PhysicsModels(), bodyIndex );
        collection.InvalidatePhysicsStreams();
    }
}


void PhysicsScene::SetPhysicsSleepEnabled( bool enabled )
{
    m_world.SetPhysicsSleepEnabled( enabled );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
}


void PhysicsScene::BeginCollisionVisualFrame( int modelCount )
{
    m_world.BeginCollisionVisualFrame( modelCount );
}


void PhysicsScene::EndCollisionVisualFrame()
{
    m_world.EndCollisionVisualFrame();
}


void PhysicsScene::ClearPointJointConstraints()
{
    m_world.ClearPointJointConstraints();
}


void PhysicsScene::AddPointJointConstraint( const PointJointConstraint& constraint )
{
    m_world.AddPointJointConstraint( constraint );
}


void PhysicsScene::SetTornadoFieldConfig( const TornadoFieldConfig& config )
{
    m_world.SetTornadoFieldConfig( config );
}


const SkullbonezCore::Physics::TornadoFieldConfig& PhysicsScene::GetTornadoFieldConfig() const
{
    return m_world.GetTornadoFieldConfig();
}


void PhysicsScene::SetTornadoSystemConfig( const TornadoSystemConfig& config )
{
    m_world.SetTornadoSystemConfig( config );
}


const SkullbonezCore::Physics::TornadoSystemConfig& PhysicsScene::GetTornadoSystemConfig() const
{
    return m_world.GetTornadoSystemConfig();
}


float PhysicsScene::GetTornadoSystemElapsedSeconds() const
{
    return m_world.GetTornadoSystemElapsedSeconds();
}


void PhysicsScene::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj )
{
    m_world.RenderTornadoFieldVectors( viewProj );
}


void PhysicsScene::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const
{
    m_world.CaptureReplaySolverSnapshot( outSnapshot, modelCount );
}


bool PhysicsScene::RestoreReplaySolverSnapshot( const ReplaySolverWorldSnapshot& snapshot, int modelCount )
{
    const bool restored = m_world.RestoreReplaySolverSnapshot( snapshot, modelCount );
    if ( restored )
    {
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    return restored;
}


SkullbonezCore::Physics::PhysicsWorld::DiagnosticsView PhysicsScene::GetDiagnosticsView() const
{
    return m_world.GetDiagnosticsView();
}

uint64_t PhysicsScene::CollectPhysicsWorldMemoryBytes() const
{
    return m_world.CollectMemoryBytes();
}


uint64_t PhysicsScene::CollectDebugAndBroadphaseMemoryBytes() const
{
    return m_world.CollectDebugAndBroadphaseMemoryBytes();
}


const PhysicsBodyStore& PhysicsScene::BodyStore() const
{
    return m_bodyStore;
}


const ColliderStore& PhysicsScene::Colliders() const
{
    return m_colliderStore;
}


const SkullbonezCore::Rendering::RenderInstanceStore& PhysicsScene::RenderInstances() const
{
    return m_renderInstanceStore;
}


const SkullbonezCore::Math::CollisionDetection::SpatialGrid& PhysicsScene::GetSpatialGrid() const
{
    return m_world.GetSpatialGrid();
}


const std::vector<int64_t>& PhysicsScene::GetCollisionCellKeys() const
{
    return m_world.GetCollisionCellKeys();
}


const std::vector<uint8_t>& PhysicsScene::GetCollisionVisualContacts() const
{
    return m_world.GetCollisionVisualContacts();
}


const std::vector<uint8_t>& PhysicsScene::GetSleepStates() const
{
    return m_world.GetSleepStates();
}


const std::vector<int>& PhysicsScene::GetSleepIslandVisualIds() const
{
    return m_world.GetSleepIslandVisualIds();
}


const std::vector<uint8_t>& PhysicsScene::GetSleepSupportedStates() const
{
    return m_world.GetSleepSupportedStates();
}


const std::vector<uint8_t>& PhysicsScene::GetSleepInhibitedStates() const
{
    return m_world.GetSleepInhibitedStates();
}


const std::vector<SkullbonezCore::Physics::PhysicsDebugContact>& PhysicsScene::GetPhysicsDebugContacts() const
{
    return m_world.GetPhysicsDebugContacts();
}


const std::vector<SkullbonezCore::Physics::PhysicsPipelineRecord>& PhysicsScene::GetPhysicsPipelineTrace() const
{
    return m_world.GetPhysicsPipelineTrace();
}


const std::vector<SkullbonezCore::Physics::PointJointConstraint>& PhysicsScene::GetPointJointConstraints() const
{
    return m_world.GetPointJointConstraints();
}


#ifdef _DEBUG
void PhysicsScene::SetPhysicsRegressionLogPath( const char* path )
{
    m_world.SetPhysicsRegressionLogPath( path );
}


void PhysicsScene::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_world.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsScene::SetPhysicsDiagnosticsPath( const char* path )
{
    m_world.SetPhysicsDiagnosticsPath( path );
}


void PhysicsScene::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_world.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsScene::SetDiagnosticsSuppressed( bool suppressed )
{
    return m_world.SetDiagnosticsSuppressed( suppressed );
}
#endif

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

#include "../GameObjects/GameModelCollection.h"

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyStore;
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
}


void PhysicsScene::RefreshBodyStore( GameModelCollection& collection )
{
    std::vector<SkullbonezCore::GameObjects::GameModel>& models = collection.PhysicsModels();
    m_bodyStore.Refresh( models, m_world.GetSleepStates() );
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
}


void PhysicsScene::RunPhysics( GameModelCollection& collection, float fChangeInTime )
{
    m_world.RunPhysics( collection, fChangeInTime );
}


void PhysicsScene::WakeModel( GameModelCollection& collection, int index )
{
    m_world.WakeModel( collection, index );
}


void PhysicsScene::SeedModelAsleep( GameModelCollection& collection, int index )
{
    m_world.SeedModelAsleep( collection, index );
}


void PhysicsScene::SetPhysicsSleepEnabled( bool enabled )
{
    m_world.SetPhysicsSleepEnabled( enabled );
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
    return m_world.RestoreReplaySolverSnapshot( snapshot, modelCount );
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


SkullbonezCore::Physics::PhysicsWorld& PhysicsScene::DiagnosticsWorldForSkullScope()
{
    return m_world;
}


const SkullbonezCore::Physics::PhysicsWorld& PhysicsScene::DiagnosticsWorldForSkullScope() const
{
    return m_world;
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

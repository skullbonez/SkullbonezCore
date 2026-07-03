/*
File: SkullbonezSource/Physics/PhysicsEngine.cpp
Purpose:
  Forwards the public PhysicsEngine facade to the existing deterministic PhysicsScene.

Mental model:
  This file is intentionally boring migration glue. It gives runtime code one
  physics owner without changing PhysicsScene or PhysicsWorld execution order.

Glossary:
  Facade: Narrow public boundary that forwards commands while hiding solver
  internals.
  Store refresh: Deterministic copy between compatibility model-view state and
  physics-owned body/collider/render stores.
  Replay restore: Replacement of live solver state from a saved replay sample.

Invariants:
  - Step and replay restore forward directly to PhysicsScene.
  - Store refreshes keep their existing body/collider/render ordering.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezSource/Physics/PhysicsScene.cpp
*/
#include "PhysicsEngine.h"

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsModelAccess;


void PhysicsEngine::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_scene.ApplyRuntimeConfig( config );
}


void PhysicsEngine::Clear()
{
    m_scene.Clear();
}


void PhysicsEngine::RefreshBodyStore( PhysicsModelAccess& modelAccess )
{
    m_scene.RefreshBodyStore( modelAccess );
}


void PhysicsEngine::ClearPendingBodyImpulses()
{
    m_scene.ClearPendingBodyImpulses();
}


void PhysicsEngine::RefreshColliderStore( PhysicsModelAccess& modelAccess )
{
    m_scene.RefreshColliderStore( modelAccess );
}


void PhysicsEngine::RefreshRenderStore( PhysicsModelAccess& modelAccess )
{
    m_scene.RefreshRenderStore( modelAccess );
}


void PhysicsEngine::Step( PhysicsModelAccess& modelAccess,
                          float deltaSeconds,
                          const Basics::EngineConfig& config,
                          const PhysicsWorldForces& worldForces,
                          Threading::WorkerPool& workerPool )
{
    m_scene.RunPhysics( modelAccess, deltaSeconds, config, worldForces, workerPool );
}


void PhysicsEngine::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
{
    m_scene.WakeBody( modelAccess, body );
}


void PhysicsEngine::SeedBodyAsleep( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
{
    m_scene.SeedBodyAsleep( modelAccess, body );
}


void PhysicsEngine::ApplyBodyImpulse( PhysicsModelAccess& modelAccess,
                                      PhysicsBodyHandle body,
                                      const Math::Vector::Vector3& impulse,
                                      const Math::Vector::Vector3& localApplicationPoint )
{
    m_scene.ApplyBodyImpulse( modelAccess, body, impulse, localApplicationPoint );
}


void PhysicsEngine::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess,
                                           PhysicsBodyHandle body,
                                           const Math::Vector::Vector3& impulse,
                                           const Math::Vector::Vector3& localApplicationPoint )
{
    m_scene.SetPendingBodyImpulse( modelAccess, body, impulse, localApplicationPoint );
}


void PhysicsEngine::SetSleepEnabled( bool enabled )
{
    m_scene.SetPhysicsSleepEnabled( enabled );
}


void PhysicsEngine::BeginCollisionVisualFrame( int modelCount )
{
    m_scene.BeginCollisionVisualFrame( modelCount );
}


void PhysicsEngine::EndCollisionVisualFrame()
{
    m_scene.EndCollisionVisualFrame();
}


void PhysicsEngine::ClearPointJointConstraints()
{
    m_scene.ClearPointJointConstraints();
}


void PhysicsEngine::AddPointJointConstraint( const PointJointConstraint& constraint )
{
    m_scene.AddPointJointConstraint( constraint );
}


void PhysicsEngine::SetTornadoFieldConfig( const TornadoFieldConfig& config )
{
    m_scene.SetTornadoFieldConfig( config );
}


const SkullbonezCore::Physics::TornadoFieldConfig& PhysicsEngine::GetTornadoFieldConfig() const
{
    return m_scene.GetTornadoFieldConfig();
}


void PhysicsEngine::SetTornadoSystemConfig( const TornadoSystemConfig& config )
{
    m_scene.SetTornadoSystemConfig( config );
}


const SkullbonezCore::Physics::TornadoSystemConfig& PhysicsEngine::GetTornadoSystemConfig() const
{
    return m_scene.GetTornadoSystemConfig();
}


float PhysicsEngine::GetTornadoSystemElapsedSeconds() const
{
    return m_scene.GetTornadoSystemElapsedSeconds();
}


void PhysicsEngine::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj )
{
    m_scene.RenderTornadoFieldVectors( viewProj );
}


void PhysicsEngine::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const
{
    m_scene.CaptureReplaySolverSnapshot( outSnapshot, modelCount );
}


bool PhysicsEngine::RestoreReplaySolverSnapshot( const ReplaySolverWorldSnapshot& snapshot, int modelCount )
{
    return m_scene.RestoreReplaySolverSnapshot( snapshot, modelCount );
}


SkullbonezCore::Physics::PhysicsDiagnosticsView PhysicsEngine::GetDiagnosticsView() const
{
    return m_scene.GetDiagnosticsView();
}

uint64_t PhysicsEngine::CollectPhysicsWorldMemoryBytes() const
{
    return m_scene.CollectPhysicsWorldMemoryBytes();
}


uint64_t PhysicsEngine::CollectDebugAndBroadphaseMemoryBytes() const
{
    return m_scene.CollectDebugAndBroadphaseMemoryBytes();
}


const PhysicsBodyStore& PhysicsEngine::BodyStore() const
{
    return m_scene.BodyStore();
}


const ColliderStore& PhysicsEngine::Colliders() const
{
    return m_scene.Colliders();
}


const SkullbonezCore::Rendering::RenderInstanceStore& PhysicsEngine::RenderInstances() const
{
    return m_scene.RenderInstances();
}


const SkullbonezCore::Math::CollisionDetection::SpatialGrid& PhysicsEngine::GetSpatialGrid() const
{
    return m_scene.GetSpatialGrid();
}


const std::vector<int64_t>& PhysicsEngine::GetCollisionCellKeys() const
{
    return m_scene.GetCollisionCellKeys();
}


const std::vector<uint8_t>& PhysicsEngine::GetCollisionVisualContacts() const
{
    return m_scene.GetCollisionVisualContacts();
}


const std::vector<uint8_t>& PhysicsEngine::GetSleepStates() const
{
    return m_scene.GetSleepStates();
}


const std::vector<int>& PhysicsEngine::GetSleepIslandVisualIds() const
{
    return m_scene.GetSleepIslandVisualIds();
}


const std::vector<uint8_t>& PhysicsEngine::GetSleepSupportedStates() const
{
    return m_scene.GetSleepSupportedStates();
}


const std::vector<uint8_t>& PhysicsEngine::GetSleepInhibitedStates() const
{
    return m_scene.GetSleepInhibitedStates();
}


const std::vector<SkullbonezCore::Physics::PhysicsDebugContact>& PhysicsEngine::GetPhysicsDebugContacts() const
{
    return m_scene.GetPhysicsDebugContacts();
}


const std::vector<SkullbonezCore::Physics::PhysicsPipelineRecord>& PhysicsEngine::GetPhysicsPipelineTrace() const
{
    return m_scene.GetPhysicsPipelineTrace();
}


const std::vector<SkullbonezCore::Physics::PointJointConstraint>& PhysicsEngine::GetPointJointConstraints() const
{
    return m_scene.GetPointJointConstraints();
}


#ifdef _DEBUG
void PhysicsEngine::SetPhysicsRegressionLogPath( const char* path )
{
    m_scene.SetPhysicsRegressionLogPath( path );
}


void PhysicsEngine::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_scene.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsEngine::SetPhysicsDiagnosticsPath( const char* path )
{
    m_scene.SetPhysicsDiagnosticsPath( path );
}


void PhysicsEngine::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_scene.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsEngine::SetDiagnosticsSuppressed( bool suppressed )
{
    return m_scene.SetDiagnosticsSuppressed( suppressed );
}
#endif

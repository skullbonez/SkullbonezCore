/*
File: SkullbonezSource/Physics/PhysicsEngine.cpp
Purpose:
  Forwards the public PhysicsEngine facade to the existing deterministic PhysicsScene.

Mental model:
  This file is intentionally thin facade code. Runtime callers enter one physics
  owner, while PhysicsScene keeps store coordination and PhysicsWorld keeps
  solver execution order.

Glossary:
  Facade: Narrow public boundary that forwards commands while hiding solver
  internals.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Physics material: Runtime policy for collider friction and sphere drag.
  Descriptor refresh: Deterministic copy from cold authoring values into
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
#include "PhysicsApi.h"

#include <utility>

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Physics::PhysicsAuthoredBodyCount;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRefreshView;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration;
using SkullbonezCore::Physics::PhysicsBodyCount;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsColliderCount;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsEngine;
void PhysicsEngine::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_scene.ApplyRuntimeConfig( config );
}


void PhysicsEngine::ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const
{
    m_scene.ApplyAuthoredBodyPolicy( desc );
}


void PhysicsEngine::ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const
{
    m_scene.ApplyAuthoredColliderPolicy( desc );
}


void PhysicsEngine::ReserveAuthoredBodyCapacity( std::size_t capacity )
{
    m_scene.ReserveAuthoredBodyCapacity( capacity );
}


PhysicsAuthoredBodyCount PhysicsEngine::AuthoredBodyDescriptorCount() const
{
    return m_scene.AuthoredBodyDescriptorCount();
}

bool PhysicsEngine::CanRegisterAuthoredBody( PhysicsAuthoredBodyCount expectedBodyCount ) const
{
    return m_scene.CanRegisterAuthoredBody( expectedBodyCount );
}


bool PhysicsEngine::TrimAuthoredBodyDescriptorsToCount( PhysicsAuthoredBodyCount bodyCount )
{
    return m_scene.TrimAuthoredBodyDescriptorsToCount( bodyCount );
}


void PhysicsEngine::Clear()
{
    m_scene.Clear();
}


bool PhysicsEngine::RefreshBodyStoreFromAuthoredDescriptors( const PhysicsAuthoredBodyRefreshView& refreshView )
{
    return m_scene.RefreshBodyStoreFromAuthoredDescriptors( refreshView );
}


PhysicsAuthoredBodyRegistration PhysicsEngine::RegisterAuthoredBody( const PhysicsBodyCreateDesc& body,
                                                                     PhysicsColliderCreateDesc collider )
{
    return m_scene.RegisterAuthoredBody( body, std::move( collider ) );
}


bool PhysicsEngine::DestroyAuthoredBody( PhysicsBodyHandle body )
{
    return m_scene.DestroyAuthoredBody( body );
}


bool PhysicsEngine::UpdateAuthoredBody( const PhysicsBodyUpdateDesc& update )
{
    return m_scene.UpdateAuthoredBody( update );
}


bool PhysicsEngine::UpdateAuthoredBodyAndCollider( const PhysicsBodyUpdateDesc& update,
                                                   PhysicsColliderCreateDesc collider )
{
    return m_scene.UpdateAuthoredBodyAndCollider( update, std::move( collider ) );
}


void PhysicsEngine::ClearPendingBodyImpulses()
{
    m_scene.ClearPendingBodyImpulses();
}


bool PhysicsEngine::TrimBodiesToCount( PhysicsBodyCount bodyCount )
{
    return m_scene.TrimBodiesToCount( bodyCount );
}


bool PhysicsEngine::TrimCollidersToCount( PhysicsColliderCount colliderCount )
{
    return m_scene.TrimCollidersToCount( colliderCount );
}


bool PhysicsEngine::RestoreReplayBodyState( PhysicsBodyHandle body,
                                            uint32_t replayBodyId,
                                            bool fixed,
                                            const Math::Vector::Vector3& position,
                                            const Math::Orientation::Quaternion& orientation,
                                            const Math::Vector::Vector3& linearVelocity,
                                            const Math::Vector::Vector3& angularVelocity,
                                            float mass,
                                            float inverseMass,
                                            const Math::Vector::Vector3& rotationalInertia,
                                            const Math::Vector::Vector3& inverseRotationalInertia )
{
    return m_scene.RestoreReplayBodyState( body,
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


bool PhysicsEngine::RefreshColliderSnapshot()
{
    return m_scene.RefreshColliderSnapshot();
}


void PhysicsEngine::Step( float deltaSeconds,
                          const Basics::EngineConfig& config,
                          const PhysicsWorldForces& worldForces,
                          Threading::WorkerPool& workerPool,
                          const char* const* diagnosticNames,
                          int diagnosticNameCount,
                          const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
    m_scene.RunPhysics( deltaSeconds,
                        config,
                        worldForces,
                        workerPool,
                        diagnosticNames,
                        diagnosticNameCount,
                        diagnosticsCsvWriter );
}


void PhysicsEngine::WakeBody( PhysicsBodyHandle body )
{
    m_scene.WakeBody( body );
}


bool PhysicsEngine::ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                                          float releaseImpulseStrength,
                                                          const Math::Vector::Vector3& seedLinearVelocity,
                                                          const Math::Vector::Vector3& seedAngularVelocity )
{
    return m_scene.ReleaseFixedBodyAndAttachedTreeParts( sourceBody,
                                                         releaseImpulseStrength,
                                                         seedLinearVelocity,
                                                         seedAngularVelocity );
}


bool PhysicsEngine::SetBodyVelocity( PhysicsBodyHandle body,
                                     const Math::Vector::Vector3& linearVelocity,
                                     const Math::Vector::Vector3& angularVelocity,
                                     bool wakeIfMoving )
{
    return m_scene.SetBodyVelocity( body, linearVelocity, angularVelocity, wakeIfMoving );
}


void PhysicsEngine::SeedBodyAsleep( PhysicsBodyHandle body )
{
    m_scene.SeedBodyAsleep( body );
}


void PhysicsEngine::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                           const Math::Vector::Vector3& impulse,
                                           const Math::Vector::Vector3& localApplicationPoint )
{
    m_scene.SetPendingBodyImpulse( body, impulse, localApplicationPoint );
}


void PhysicsEngine::ApplyBodyImpulse( PhysicsBodyHandle body,
                                      const Math::Vector::Vector3& impulse,
                                      const Math::Vector::Vector3& localApplicationPoint )
{
    m_scene.ApplyBodyImpulse( body, impulse, localApplicationPoint );
}


void PhysicsEngine::SetSleepEnabled( bool enabled )
{
    m_scene.SetPhysicsSleepEnabled( enabled );
}


bool PhysicsEngine::IsSleepEnabled() const
{
    return m_scene.IsPhysicsSleepEnabled();
}


void PhysicsEngine::BeginCollisionVisualFrame( PhysicsBodyCount bodyCount )
{
    m_scene.BeginCollisionVisualFrame( bodyCount );
}


void PhysicsEngine::EndCollisionVisualFrame()
{
    m_scene.EndCollisionVisualFrame();
}


void PhysicsEngine::ClearPointJointConstraints()
{
    m_scene.ClearPointJointConstraints();
}


PhysicsConstraintHandle PhysicsEngine::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    return m_scene.CreatePointJoint( desc );
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


void PhysicsEngine::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot,
                                                 PhysicsBodyCount bodyCount ) const
{
    m_scene.CaptureReplaySolverSnapshot( outSnapshot, bodyCount );
}


bool PhysicsEngine::RestoreReplaySolverSnapshot( const ReplaySolverWorldSnapshot& snapshot, PhysicsBodyCount bodyCount )
{
    return m_scene.RestoreReplaySolverSnapshot( snapshot, bodyCount );
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


bool PhysicsEngine::ShouldEmitStepDiagnostics() const
{
    return m_scene.ShouldEmitStepDiagnostics();
}


bool PhysicsEngine::ShouldEmitCollisionTimeDiagnostics() const
{
    return m_scene.ShouldEmitCollisionTimeDiagnostics();
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

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

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
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


void PhysicsEngine::Clear()
{
    m_scene.Clear();
}


void PhysicsEngine::RefreshBodyStore( const std::vector<PhysicsBodyCreateDesc>& bodyDescs )
{
    m_scene.RefreshBodyStore( bodyDescs );
}


void PhysicsEngine::RefreshBodyFromDescriptor( const PhysicsBodyCreateDesc& desc,
                                               int modelIndex,
                                               int expectedModelCount )
{
    m_scene.RefreshBodyFromDescriptor( desc, modelIndex, expectedModelCount );
}


PhysicsBodyHandle PhysicsEngine::RegisterAuthoredBody( const PhysicsBodyCreateDesc& desc )
{
    return m_scene.RegisterAuthoredBody( desc );
}


PhysicsColliderHandle PhysicsEngine::RegisterAuthoredCollider( const PhysicsColliderCreateDesc& desc )
{
    return m_scene.RegisterAuthoredCollider( desc );
}


bool PhysicsEngine::UpdateAuthoredCollider( PhysicsColliderHandle collider, const PhysicsColliderCreateDesc& desc )
{
    return m_scene.UpdateAuthoredCollider( collider, desc );
}


void PhysicsEngine::ClearPendingBodyImpulses()
{
    m_scene.ClearPendingBodyImpulses();
}


bool PhysicsEngine::TrimBodyStoreToCount( int bodyCount )
{
    return m_scene.TrimBodyStoreToCount( bodyCount );
}


bool PhysicsEngine::TrimColliderStoreToCount( int colliderCount )
{
    return m_scene.TrimColliderStoreToCount( colliderCount );
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


bool PhysicsEngine::PrepareRenderStoreRefresh( int expectedModelCount )
{
    return m_scene.PrepareRenderStoreRefresh( expectedModelCount );
}


bool PhysicsEngine::OverrideRenderInstancePose( int modelIndex,
                                                uint32_t replayBodyId,
                                                const Math::Vector::Vector3& position,
                                                const Math::Orientation::Quaternion& orientation )
{
    const PhysicsBodyStore& bodyStore = m_scene.BodyStore();
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    // Invariant: render-pose overrides are keyed by the physics-owned body id.
    // The model-index hint can lag during scrub/prediction presentation, so it
    // is not allowed to approve which live render instance receives the pose.
    if ( !bodyRecord || bodyStore.ModelIndexForHandle( body ) != modelIndex ||
         bodyRecord->replayBodyId != replayBodyId )
    {
        return false;
    }

    return m_scene.MutableRenderInstances().OverridePose( modelIndex,
                                                          replayBodyId,
                                                          position,
                                                          orientation,
                                                          m_scene.Colliders() );
}


SkullbonezCore::Rendering::RenderInstanceStore& PhysicsEngine::MutableRenderInstances()
{
    return m_scene.MutableRenderInstances();
}


void PhysicsEngine::Step( float deltaSeconds,
                          const Basics::EngineConfig& config,
                          const PhysicsWorldForces& worldForces,
                          Threading::WorkerPool& workerPool,
                          const char* const* diagnosticNames,
                          int diagnosticNameCount )
{
    m_scene.RunPhysics( deltaSeconds, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );
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


void PhysicsEngine::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj,
                                               Rendering::IRenderCommandContext& renderCommands,
                                               bool supportsDebugLines )
{
    m_scene.RenderTornadoFieldVectors( viewProj, renderCommands, supportsDebugLines );
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


bool PhysicsEngine::ShouldEmitStepDiagnostics() const
{
    return m_scene.ShouldEmitStepDiagnostics();
}


bool PhysicsEngine::ShouldEmitCollisionTimeDiagnostics() const
{
    return m_scene.ShouldEmitCollisionTimeDiagnostics();
}


const std::vector<int>& PhysicsEngine::GetFixedContactHighlightBodies() const
{
    return m_scene.GetFixedContactHighlightBodies();
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


#ifdef _DEBUG
void PhysicsEngine::ValidateRenderStore( int expectedModelCount ) const
{
    m_scene.ValidateRenderStore( expectedModelCount );
}
#endif


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

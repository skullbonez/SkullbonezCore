/*
File: SkullbonezSource/Physics/PhysicsScene.cpp
Purpose:
  Coordinates PhysicsWorld with deterministic body/collider/render stores.

Mental model:
  PhysicsWorld still owns the solver. PhysicsScene is the coordination boundary
  that refreshes body, collider, and render snapshots around that solver without
  changing the live model order.

Glossary:
  Solver: Physics step that integrates motion and applies collision/contact
    impulses.
  Store: Ordered snapshot of one ownership concern such as bodies, colliders,
    or render instances.
  Determinism: Same inputs produce byte-exact validation artifacts.

Invariants:
  - Store refresh order must preserve deterministic model-view order.
  - RunPhysics delegates to PhysicsWorld without changing floating-point order.

Related:
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#include "PhysicsScene.h"
#include "PhysicsApi.h"

#include <cassert>
#include <cstddef>

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsModelAccess;
using SkullbonezCore::Physics::PhysicsScene;


void PhysicsScene::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_world.ApplyRuntimeConfig( config );
}


void PhysicsScene::Clear()
{
    m_world.Clear();
    m_bodyStore.Clear();
    m_colliderStore.Clear();
    m_renderInstanceStore.Clear();
}


void PhysicsScene::RefreshBodyStore( PhysicsModelAccess& modelAccess )
{
    modelAccess.ReloadPhysicsBodies( m_bodyStore, m_world.GetSleepStates() );
}


void PhysicsScene::RefreshBodyFromModel( PhysicsModelAccess& modelAccess, int modelIndex )
{
    const int modelCount = modelAccess.ModelCount();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return;
    }
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
        return;
    }

    modelAccess.RefreshPhysicsBodyFromModel( m_bodyStore, modelIndex );
}


void PhysicsScene::ClearPendingBodyImpulses()
{
    m_bodyStore.ClearPendingImpulses();
}


bool PhysicsScene::TrimBodyStoreToCount( int bodyCount )
{
    return m_bodyStore.TrimToCount( bodyCount );
}


bool PhysicsScene::RestoreReplayBodyState( int modelIndex,
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
    return m_bodyStore.RestoreReplayBodyState( modelIndex,
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


void PhysicsScene::RefreshColliderStore( PhysicsModelAccess& modelAccess )
{
    RefreshBodyStore( modelAccess );
    modelAccess.RefreshPhysicsColliders( m_colliderStore, m_bodyStore );
}


void PhysicsScene::RefreshRenderStore( PhysicsModelAccess& modelAccess )
{
    const int modelCount = modelAccess.ModelCount();
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
    }
    if ( m_colliderStore.Count() != modelCount )
    {
        modelAccess.RefreshPhysicsColliders( m_colliderStore, m_bodyStore );
    }
    modelAccess.RefreshRenderInstances( m_renderInstanceStore, m_bodyStore, m_colliderStore );
#ifdef _DEBUG
    ValidatePhysicsStoreMappings( modelCount );
    ValidateRenderStoreMappings( modelCount );
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
    }
}
#endif


void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess,
                               float fChangeInTime,
                               const Basics::EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               Threading::WorkerPool& workerPool )
{
    const int modelCount = modelAccess.ModelCount();

    // Invariant: PhysicsBodyStore is the per-tick body authority. GameModel is
    // imported only when model/body topology changes; same-count editor or replay
    // mutations must use explicit commit paths before the step reads the store.
    if ( m_bodyStore.Count() != modelCount )
    {
        modelAccess.ReloadPhysicsBodies( m_bodyStore, m_world.GetSleepStates() );
    }
    // Why: collider metadata is construction/authoring state, not per-tick
    // solver state. Scene setup and explicit refresh calls rebuild the snapshot;
    // the hot step only needs to catch topology changes.
    if ( m_colliderStore.Count() != modelCount )
    {
        modelAccess.RefreshPhysicsColliders( m_colliderStore, m_bodyStore );
    }
    // Why: contact highlight timers are model-owned presentation state, not
    // solver input. Tick them at the compatibility edge so PhysicsWorld keeps
    // its fixed step closer to store-owned data.
    modelAccess.TickContactHighlights( modelCount, fChangeInTime );
    m_lastWorldForces = worldForces;
    m_hasLastWorldForces = true;
    m_world.RunPhysics( modelAccess, m_bodyStore, m_colliderStore, fChangeInTime, config, worldForces, workerPool );

    // Why: fixed-contact highlights are GameModel presentation feedback. The
    // solver records compact body indices; PhysicsScene applies them at the
    // compatibility edge so PhysicsWorld does not mutate presentation state.
    for ( int index : m_world.GetFixedContactHighlightBodies() )
    {
        modelAccess.NotifyFixedContact( index, 0.5f );
    }
    ApplyFixedTreeReleaseEvents( modelAccess, worldForces );

    m_world.EmitStepDiagnostics( modelAccess, m_bodyStore, m_colliderStore, fChangeInTime );

    // Compatibility owner: PhysicsScene step boundary.
    // Reason: editor and replay compatibility consumers still read GameModel
    // pose/state after the store-owned solver has finished.
    // Deletion condition: those consumers read PhysicsBodyStore or
    // handle-addressed physics commands directly. Checker budget: boundary grep
    // keeps bulk solver writeback out of PhysicsWorld::RunPhysics.
    modelAccess.WriteBackPhysicsBodies( m_bodyStore );

    // Why: model stream caches belong to the compatibility model view. The
    // world step may mirror body state back to GameModel, but PhysicsScene owns
    // the boundary where those cached SoA streams become stale.
    modelAccess.InvalidatePhysicsStreams();
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
}


void PhysicsScene::ApplyFixedTreeReleaseEvents( PhysicsModelAccess& modelAccess, const PhysicsWorldForces& worldForces )
{
    const std::vector<PhysicsFixedTreeReleaseEvent>& releaseEvents = m_world.GetFixedTreeReleaseEvents();
    if ( releaseEvents.empty() )
    {
        return;
    }

    // Why: fixed-tree release changes live simulation state, then wake
    // propagation may touch neighbouring bodies. Keep both operations on the
    // body store before Debug diagnostics or compatibility writeback sample it.
    m_fixedTreeReleaseWakeBodies.reserve( static_cast<std::size_t>( m_bodyStore.Count() ) );
    for ( const PhysicsFixedTreeReleaseEvent& event : releaseEvents )
    {
        modelAccess.ReleaseAttachedFixedTreeParts( m_bodyStore, event, m_fixedTreeReleaseWakeBodies );
        for ( int index : m_fixedTreeReleaseWakeBodies )
        {
            m_world.WakeModel( modelAccess, m_bodyStore, m_colliderStore, worldForces, index );
        }
    }
}


// Invariant: steady-state commands mutate PhysicsBodyStore records selected by
// handles. GameModel body data is imported here only when topology/count changed.
void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
{
    const int modelCount = modelAccess.ModelCount();
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
    }
    if ( m_colliderStore.Count() != modelCount )
    {
        RefreshColliderStore( modelAccess );
    }
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 )
    {
        return;
    }
    if ( m_hasLastWorldForces )
    {
        m_world.WakeModel( modelAccess, m_bodyStore, m_colliderStore, m_lastWorldForces, index );
    }
    else
    {
        m_world.WakeModel( modelAccess, m_bodyStore, index );
    }
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
}


bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess,
                                    PhysicsBodyHandle body,
                                    const Math::Vector::Vector3& linearVelocity,
                                    const Math::Vector::Vector3& angularVelocity,
                                    bool wakeIfMoving )
{
    const int modelCount = modelAccess.ModelCount();
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
    }
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 || !m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity ) )
    {
        return false;
    }

    const bool shouldWake = wakeIfMoving && ( !linearVelocity.IsCloseToZero() || !angularVelocity.IsCloseToZero() );
    if ( shouldWake )
    {
        if ( m_colliderStore.Count() != modelCount )
        {
            RefreshColliderStore( modelAccess );
        }
        if ( m_hasLastWorldForces )
        {
            m_world.WakeModel( modelAccess, m_bodyStore, m_colliderStore, m_lastWorldForces, index );
        }
        else
        {
            m_world.WakeModel( modelAccess, m_bodyStore, index );
        }
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }

    // Why: replay velocity edit is a handle-keyed body-store command. The
    // model writeback below is only the remaining presentation projection.
    modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
    modelAccess.InvalidatePhysicsStreams();
    return true;
}


void PhysicsScene::SeedBodyAsleep( PhysicsBodyHandle body )
{
    if ( !m_world.IsPhysicsSleepEnabled() )
    {
        return;
    }

    // Why: scene setup already owns the freshly created GameModel. The initial
    // sleep seed belongs in PhysicsBodyStore and will be copied into PhysicsWorld
    // when the first step begins, so no model-access writeback is needed here.
    m_bodyStore.SeedBodyAsleep( body );
}


void PhysicsScene::SeedBodyAsleep( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
{
    const int modelCount = modelAccess.ModelCount();
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
    }
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 )
    {
        return;
    }
    m_world.SeedModelAsleep( m_bodyStore, index );
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    // Compatibility owner: PhysicsScene explicit command edge.
    // Reason: seeding sleep mutates PhysicsWorld and PhysicsBodyStore state;
    // GameModel is updated only as the remaining presentation mirror.
    // Deletion condition: editor/ragdoll callers consume store views directly.
    // Checker budget: store-owned PhysicsWorld seed must not rebuild model
    // streams or invalidate model caches itself.
    modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
    modelAccess.InvalidatePhysicsStreams();
}


void PhysicsScene::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                          const Math::Vector::Vector3& impulse,
                                          const Math::Vector::Vector3& localApplicationPoint )
{
    // Why: initial authored/generated impulses are one-shot physics state.
    // Writing them into the body store avoids routing setup through the
    // GameModelCollection model-index command wrappers.
    m_bodyStore.SetPendingBodyImpulse( body, impulse, localApplicationPoint );
}


void PhysicsScene::ApplyBodyImpulse( PhysicsModelAccess& modelAccess,
                                     PhysicsBodyHandle body,
                                     const Math::Vector::Vector3& impulse,
                                     const Math::Vector::Vector3& localApplicationPoint )
{
    SetPendingBodyImpulse( modelAccess, body, impulse, localApplicationPoint );
    WakeBody( modelAccess, body );
}


void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess,
                                          PhysicsBodyHandle body,
                                          const Math::Vector::Vector3& impulse,
                                          const Math::Vector::Vector3& localApplicationPoint )
{
    const int modelCount = modelAccess.ModelCount();
    if ( m_bodyStore.Count() != modelCount )
    {
        RefreshBodyStore( modelAccess );
    }
    if ( !m_bodyStore.SetPendingBodyImpulse( body, impulse, localApplicationPoint ) )
    {
        return;
    }

    // Why: the mutation above is handle-keyed store authority; this model index
    // is only the remaining legacy projection for model-backed presentation.
    const int bodyIndex = m_bodyStore.ModelIndexForHandle( body );
    if ( bodyIndex >= 0 )
    {
        modelAccess.WriteBackPhysicsBody( m_bodyStore, bodyIndex );
        modelAccess.InvalidatePhysicsStreams();
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


PhysicsConstraintHandle PhysicsScene::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    // Why: stale body handles should fail at the scene/store boundary before
    // the solver receives an append-only point-joint row.
    if ( !m_bodyStore.Contains( desc.bodyA ) || !m_bodyStore.Contains( desc.bodyB ) || desc.bodyA == desc.bodyB )
    {
        return PhysicsConstraintHandle{};
    }

    return m_world.CreatePointJoint( desc );
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


SkullbonezCore::Physics::PhysicsDiagnosticsView PhysicsScene::GetDiagnosticsView() const
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

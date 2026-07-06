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
  Pending impulse: One-shot velocity edit queued on a body record and consumed
    by the next solver step.
  Physics material: Runtime policy for collider friction and sphere drag.
  Velocity edit: Replay-authored command that changes live body velocity before
    prediction or the next step samples the body store.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Sleep: Solver optimization that stops integrating stable bodies until an
    explicit wake or contact event reactivates them.
  Determinism: Same inputs produce byte-exact validation artifacts.

Invariants:
  - Store refresh order must preserve deterministic model-view order.
  - RunPhysics delegates to PhysicsWorld without changing floating-point order.
  - Pending impulses stay store-owned until consumed; render projection refresh
    is separate from solver sleep/island mutation.
  - Velocity edits stay store-owned until the normal step boundary projects
    body state for presentation.
  - Wake commands update solver sleep/island state without rebuilding render
    projection records.

Related:
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#include "PhysicsScene.h"
#include "PhysicsApi.h"

#include "../Core/Common.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <variant>

using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Physics::BodySimulationLimits;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::ContactPolicy;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsMaterial;
using SkullbonezCore::Physics::PhysicsScene;


namespace
{
ColliderShapeKind ShapeKindForColliderDesc( const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape )
{
    if ( std::holds_alternative<BoundingBox>( shape ) )
    {
        return ColliderShapeKind::Box;
    }
    if ( std::holds_alternative<ConvexHullShape>( shape ) )
    {
        return ColliderShapeKind::ConvexHull;
    }
    return ColliderShapeKind::Sphere;
}


// Why: descriptor import is a cold authoring boundary. The dense row shape and
// cheap collider discriminator are derived inside PhysicsScene so collection
// code cannot become a second ColliderStore layout owner.
ColliderRecord MakeColliderRecordFromDesc( const PhysicsColliderCreateDesc& desc, const PhysicsBodyRecord& body )
{
    ColliderRecord record;
    record.body = body.handle;
    record.sceneObjectId = desc.sceneObjectId.IsValid() ? desc.sceneObjectId : body.sceneObjectId;
    record.replayBodyId = body.replayBodyId;
    record.shape = desc.shape;
    record.shapeKind = ShapeKindForColliderDesc( desc.shape );
    record.boundingRadius = desc.boundingRadius;
    record.restitution = desc.restitution;
    record.friction = desc.friction;
    record.contactMaterialId = desc.contactMaterialId;
    strncpy_s( record.contactMaterialName, sizeof( record.contactMaterialName ), desc.contactMaterialName, _TRUNCATE );
    record.projectedSurfaceArea = desc.projectedSurfaceArea;
    record.dragCoefficient = desc.dragCoefficient;
    return record;
}
} // namespace


PhysicsScene::PhysicsScene()
{
}


void PhysicsScene::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    m_physicsMaterial = PhysicsMaterial::FromConfig( config );
    m_bodySimulationLimits = BodySimulationLimits::FromConfig( config );
    m_contactPolicy = ContactPolicy::FromConfig( config );
    m_world.ApplyRuntimeConfig( config );
    m_colliderStore.ApplyPhysicsMaterial( m_physicsMaterial );
    for ( PhysicsBodyCreateDesc& desc : m_authoredBodyDescs )
    {
        ApplyAuthoredBodyPolicy( desc );
    }
}


void PhysicsScene::ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const
{
    desc.friction = m_physicsMaterial.frictionCoefficient;
    desc.angularVelocityLimit = m_bodySimulationLimits.angularVelocityLimit;
    desc.contactEpsilon = m_contactPolicy.contactEpsilon;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( m_physicsMaterial.sphereDragCoefficient );
        desc.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
    }
}


void PhysicsScene::ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const
{
    desc.friction = m_physicsMaterial.frictionCoefficient;
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &desc.shape ) )
    {
        sphere->SetDragCoefficient( m_physicsMaterial.sphereDragCoefficient );
        desc.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
    }
}


void PhysicsScene::ReserveAuthoredBodyCapacity( std::size_t capacity )
{
    m_authoredBodyDescs.reserve( capacity );
}


int PhysicsScene::AuthoredBodyDescriptorCount() const
{
    return static_cast<int>( m_authoredBodyDescs.size() );
}


bool PhysicsScene::TryGetAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& outDesc ) const
{
    if ( modelIndex < 0 || modelIndex >= AuthoredBodyDescriptorCount() )
    {
        return false;
    }
    outDesc = m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )];
    return true;
}


bool PhysicsScene::UpdateAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& desc, int expectedModelCount )
{
    if ( modelIndex < 0 || modelIndex >= expectedModelCount || expectedModelCount != AuthoredBodyDescriptorCount() )
    {
        return false;
    }
    ApplyAuthoredBodyPolicy( desc );
    m_authoredBodyDescs[static_cast<std::size_t>( modelIndex )] = desc;
    return true;
}


bool PhysicsScene::TrimAuthoredBodyDescriptorsToCount( int bodyCount )
{
    if ( bodyCount < 0 )
    {
        return false;
    }
    const std::size_t targetCount = static_cast<std::size_t>( bodyCount );
    if ( targetCount > m_authoredBodyDescs.size() )
    {
        return false;
    }
    m_authoredBodyDescs.erase( m_authoredBodyDescs.begin() + static_cast<std::ptrdiff_t>( targetCount ),
                               m_authoredBodyDescs.end() );
    return AuthoredBodyDescriptorCount() == bodyCount;
}


void PhysicsScene::Clear()
{
    m_world.Clear();
    m_authoredBodyDescs.clear();
    m_bodyStore.Clear();
    m_colliderStore.Clear();
    m_renderInstanceStore.Clear();
}


bool PhysicsScene::RefreshBodyStoreFromAuthoredDescriptors( const std::vector<uint32_t>& replayBodyIds,
                                                            const std::vector<int>& fixedTreeReleaseRoots,
                                                            const std::vector<const char*>& diagnosticNames )
{
    const std::size_t descriptorCount = m_authoredBodyDescs.size();
    if ( replayBodyIds.size() != descriptorCount || fixedTreeReleaseRoots.size() != descriptorCount ||
         diagnosticNames.size() != descriptorCount )
    {
        return false;
    }

    std::vector<PhysicsBodyCreateDesc> bodyDescs;
    bodyDescs.reserve( descriptorCount );

    for ( int i = 0; i < static_cast<int>( descriptorCount ); ++i )
    {
        PhysicsBodyCreateDesc desc = m_authoredBodyDescs[static_cast<std::size_t>( i )];
        desc.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( replayBodyIds[static_cast<std::size_t>( i )] );
        desc.fixedTreeReleaseRootIndex = fixedTreeReleaseRoots[static_cast<std::size_t>( i )];
        desc.diagnosticName = diagnosticNames[static_cast<std::size_t>( i )];
        ApplyAuthoredBodyPolicy( desc );
        bodyDescs.push_back( desc );
    }

    RefreshBodyStore( bodyDescs );
    return m_bodyStore.Count() == AuthoredBodyDescriptorCount();
}


void PhysicsScene::RefreshBodyStore( const std::vector<PhysicsBodyCreateDesc>& bodyDescs )
{
    m_bodyStore.LoadFromDescriptors( bodyDescs, m_world.GetSleepStates() );
}


void PhysicsScene::RefreshBodyFromDescriptor( const PhysicsBodyCreateDesc& desc,
                                              int modelIndex,
                                              int expectedModelCount )
{
    if ( modelIndex < 0 || modelIndex >= expectedModelCount )
    {
        return;
    }
    if ( m_bodyStore.Count() != expectedModelCount )
    {
        return;
    }

    m_bodyStore.RefreshRecordFromDescriptorAt( desc, modelIndex );
}


PhysicsBodyHandle PhysicsScene::RegisterAuthoredBody( const PhysicsBodyCreateDesc& desc )
{
    PhysicsBodyCreateDesc authoredDesc = desc;
    ApplyAuthoredBodyPolicy( authoredDesc );
    m_authoredBodyDescs.push_back( authoredDesc );
    return m_bodyStore.CreateBodyRecord( authoredDesc, m_world.IsPhysicsSleepEnabled() );
}


PhysicsColliderHandle PhysicsScene::RegisterAuthoredCollider( const PhysicsColliderCreateDesc& desc )
{
    const PhysicsBodyRecord* body = m_bodyStore.RecordForHandle( desc.body );
    assert( body != nullptr );
    return body ? m_colliderStore.CreateColliderRecord( MakeColliderRecordFromDesc( desc, *body ) )
                : PhysicsColliderHandle{};
}


bool PhysicsScene::UpdateAuthoredCollider( PhysicsColliderHandle collider, const PhysicsColliderCreateDesc& desc )
{
    const PhysicsBodyRecord* body = m_bodyStore.RecordForHandle( desc.body );
    return body && m_colliderStore.UpdateRecordForHandle( collider, MakeColliderRecordFromDesc( desc, *body ) );
}


void PhysicsScene::ClearPendingBodyImpulses()
{
    m_bodyStore.ClearPendingImpulses();
}


bool PhysicsScene::TrimBodyStoreToCount( int bodyCount )
{
    return m_bodyStore.TrimToCount( bodyCount );
}


bool PhysicsScene::TrimColliderStoreToCount( int colliderCount )
{
    return m_colliderStore.TrimToCount( colliderCount );
}


bool PhysicsScene::RestoreReplayBodyState( PhysicsBodyHandle body,
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
    return m_bodyStore.RestoreReplayBodyState( body,
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


bool PhysicsScene::RefreshColliderSnapshot()
{
    return m_colliderStore.RefreshBodyBindings( m_bodyStore );
}


bool PhysicsScene::PrepareRenderStoreRefresh( int expectedModelCount )
{
    if ( m_bodyStore.Count() != expectedModelCount )
    {
        m_renderInstanceStore.Clear();
        return false;
    }
    if ( !RefreshColliderSnapshot() )
    {
        // Hazard: render rows consume collider shape/material data. If topology
        // drift has removed collider rows, do not manufacture a partial render
        // snapshot from stale model-owned shape fields.
        m_renderInstanceStore.Clear();
        return false;
    }
    if ( m_bodyStore.Count() != expectedModelCount || m_colliderStore.Count() != expectedModelCount )
    {
        m_renderInstanceStore.Clear();
        return false;
    }
#ifdef _DEBUG
    ValidatePhysicsStoreMappings( expectedModelCount );
#endif
    return true;
}


void PhysicsScene::ReserveRenderPresentationCapacity( std::size_t capacity )
{
    m_renderInstanceStore.ReservePresentationCapacity( capacity );
}


bool PhysicsScene::ResizeRenderPresentationRecords( int presentationCount )
{
    return m_renderInstanceStore.ResizePresentationRecords( presentationCount );
}


SkullbonezCore::Rendering::RenderInstancePresentationRecord*
PhysicsScene::MutableRenderPresentationRecordForModelIndex( int modelIndex )
{
    return m_renderInstanceStore.MutablePresentationRecordForModelIndex( modelIndex );
}


const std::vector<SkullbonezCore::Rendering::RenderInstancePresentationRecord>&
PhysicsScene::RenderPresentationRecords() const
{
    return m_renderInstanceStore.PresentationRecords();
}


bool PhysicsScene::RefreshRenderInstancesFromPresentation()
{
    m_renderInstanceStore.Refresh( m_bodyStore, m_colliderStore );
    return m_renderInstanceStore.Count() == m_bodyStore.Count();
}


SkullbonezCore::Rendering::RenderInstanceStore& PhysicsScene::MutableRenderInstances()
{
    return m_renderInstanceStore;
}


#ifdef _DEBUG
void PhysicsScene::ValidateRenderStore( int expectedModelCount ) const
{
    ValidateRenderStoreMappings( expectedModelCount );
}


void PhysicsScene::ValidatePhysicsStoreMappings( int modelCount ) const
{
    assert( m_bodyStore.Count() == modelCount );
    assert( m_colliderStore.Count() == modelCount );

    const auto& bodies = m_bodyStore.Records();
    const auto& colliders = m_colliderStore.Records();
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodies[index];
        const ColliderRecord& collider = colliders[index];
        const PhysicsBodyHandle bodyHandle = m_bodyStore.HandleForModelIndex( i );
        const PhysicsColliderHandle colliderHandle = m_colliderStore.HandleForBodyHandle( bodyHandle );

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


void PhysicsScene::RunPhysics( float fChangeInTime,
                               const Basics::EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               Threading::WorkerPool& workerPool,
                               const char* const* diagnosticNames,
                               int diagnosticNameCount )
{
    m_lastWorldForces = worldForces;
    m_hasLastWorldForces = true;

    m_world.RunPhysics( m_bodyStore,
                        m_colliderStore,
                        fChangeInTime,
                        config,
                        worldForces,
                        workerPool,
                        diagnosticNames,
                        diagnosticNameCount );

    ApplyFixedTreeReleaseEvents( worldForces );

    m_world.EmitStepDiagnostics( m_bodyStore, m_colliderStore, fChangeInTime, diagnosticNames, diagnosticNameCount );

    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
}


void PhysicsScene::ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces )
{
    const std::vector<PhysicsFixedTreeReleaseEvent>& releaseEvents = m_world.GetFixedTreeReleaseEvents();
    if ( releaseEvents.empty() )
    {
        return;
    }

    // Why: fixed-tree release changes live simulation state, then wake
    // propagation may touch neighbouring bodies. Keep both operations on the
    // body store before Debug diagnostics or presentation sampling reads it.
    m_fixedTreeReleaseWakeBodies.reserve( static_cast<std::size_t>( m_bodyStore.Count() ) );
    for ( const PhysicsFixedTreeReleaseEvent& event : releaseEvents )
    {
        m_bodyStore.ReleaseAttachedFixedTreeParts( event, m_fixedTreeReleaseWakeBodies );
        for ( int index : m_fixedTreeReleaseWakeBodies )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, worldForces, index );
        }
    }
}


bool PhysicsScene::ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                                         float releaseImpulseStrength,
                                                         const Math::Vector::Vector3& seedLinearVelocity,
                                                         const Math::Vector::Vector3& seedAngularVelocity )
{
    const int sourceIndex = m_bodyStore.ModelIndexForHandle( sourceBody );
    PhysicsBodyRecord* sourceRecord = m_bodyStore.MutableRecordForHandle( sourceBody );
    if ( sourceIndex < 0 || !sourceRecord )
    {
        return false;
    }

    const std::size_t bodyCapacity = static_cast<std::size_t>( m_bodyStore.Count() );
    m_fixedTreeReleaseWakeBodies.reserve( bodyCapacity );

    bool sourceReleased = false;
    if ( sourceRecord->isFixed )
    {
        // Hazard: authored fixed props only become dynamic when their store
        // policy accepts the tool impulse. The source body receives the actual
        // launcher impulse separately, so its release preserves current velocity
        // while attached parts inherit the seeded breakaway velocity.
        if ( !sourceRecord->releasesFromFixedOnContact ||
             releaseImpulseStrength < sourceRecord->contactReleaseImpulseThreshold )
        {
            return false;
        }
        const Math::Vector::Vector3 sourceLinearVelocity = sourceRecord->linearVelocity;
        const Math::Vector::Vector3 sourceAngularVelocity = sourceRecord->angularVelocity;
        PhysicsBodyStore::ReleaseFixedRecord( *sourceRecord, sourceLinearVelocity, sourceAngularVelocity );
        sourceReleased = true;
    }

    const PhysicsFixedTreeReleaseEvent event = { sourceIndex, seedLinearVelocity, seedAngularVelocity };
    m_bodyStore.ReleaseAttachedFixedTreeParts( event, m_fixedTreeReleaseWakeBodies );

    const auto wakeReleasedIndex = [&]( int index )
    {
        if ( m_hasLastWorldForces )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
        }
        else
        {
            m_world.WakeModel( m_bodyStore, index );
        }
    };

    if ( sourceReleased )
    {
        wakeReleasedIndex( sourceIndex );
    }
    for ( int index : m_fixedTreeReleaseWakeBodies )
    {
        wakeReleasedIndex( index );
    }
    if ( sourceReleased || !m_fixedTreeReleaseWakeBodies.empty() )
    {
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    return true;
}


// Invariant: steady-state wake commands mutate PhysicsBodyStore records
// selected by handles. Legacy model-index callers must refresh topology before
// they enter this handle-owned command.
void PhysicsScene::WakeBody( PhysicsBodyHandle body )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 )
    {
        return;
    }
    if ( m_hasLastWorldForces )
    {
        m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
    }
    else
    {
        m_world.WakeModel( m_bodyStore, index );
    }
    m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    // Why: wake is solver sleep/island state. Rebuilding render projection here
    // would add work without changing pose; the normal step boundary owns later
    // presentation updates that actually change body state.
}


bool PhysicsScene::SetBodyVelocity( PhysicsBodyHandle body,
                                    const Math::Vector::Vector3& linearVelocity,
                                    const Math::Vector::Vector3& angularVelocity,
                                    bool wakeIfMoving )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 || !m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity ) )
    {
        return false;
    }

    const bool shouldWake = wakeIfMoving && ( !linearVelocity.IsCloseToZero() || !angularVelocity.IsCloseToZero() );
    if ( shouldWake )
    {
        if ( m_hasLastWorldForces )
        {
            m_world.WakeModel( m_bodyStore, m_colliderStore, m_lastWorldForces, index );
        }
        else
        {
            m_world.WakeModel( m_bodyStore, index );
        }
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }

    // Invariant: callers that start from model indices perform any count-gated
    // topology refresh before resolving the handle. This command does not borrow
    // authoring state or reload same-count body rows on the edit path.
    return true;
}


void PhysicsScene::SeedBodyAsleep( PhysicsBodyHandle body )
{
    const int index = m_bodyStore.ModelIndexForHandle( body );
    if ( index < 0 || !m_world.IsPhysicsSleepEnabled() )
    {
        return;
    }

    // Why: sleep seeding is solver state, not presentation. Seed both the
    // dense body store and PhysicsWorld's sleep counters, then leave
    // presentation projection to the next normal step boundary.
    if ( m_bodyStore.SeedBodyAsleep( body ) )
    {
        m_world.SeedModelAsleep( m_bodyStore, index );
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
}


void PhysicsScene::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                          const Math::Vector::Vector3& impulse,
                                          const Math::Vector::Vector3& localApplicationPoint )
{
    // Why: initial authored/generated impulses are one-shot physics state.
    // Writing them into the body store avoids routing setup through the
    // collection-owned model-index command wrappers.
    m_bodyStore.SetPendingBodyImpulse( body, impulse, localApplicationPoint );
}


void PhysicsScene::ApplyBodyImpulse( PhysicsBodyHandle body,
                                     const Math::Vector::Vector3& impulse,
                                     const Math::Vector::Vector3& localApplicationPoint )
{
    SetPendingBodyImpulse( body, impulse, localApplicationPoint );
    WakeBody( body );
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


void PhysicsScene::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj,
                                              Rendering::IRenderCommandContext& renderCommands,
                                              bool supportsDebugLines )
{
    m_world.RenderTornadoFieldVectors( viewProj, renderCommands, supportsDebugLines );
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


bool PhysicsScene::ShouldEmitStepDiagnostics() const
{
    return m_world.ShouldEmitStepDiagnostics();
}


bool PhysicsScene::ShouldEmitCollisionTimeDiagnostics() const
{
    return m_world.ShouldEmitCollisionTimeDiagnostics();
}


const std::vector<int>& PhysicsScene::GetFixedContactHighlightBodies() const
{
    return m_world.GetFixedContactHighlightBodies();
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

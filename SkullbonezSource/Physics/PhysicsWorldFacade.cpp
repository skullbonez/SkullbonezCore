/*
File: SkullbonezSource/Physics/PhysicsWorldFacade.cpp
Purpose:
  Implements the public physics facade controls, diagnostics views, and memory reporting.

Summary:
  These methods translate public world requests into typed stage-owner calls.
  The split keeps the solver sequencer readable; it does not create a second
  owner or move authority back from the concrete stages.

Glossary:
  Facade: Stable public entry point that delegates to the domain owner.
  Diagnostics view: Immutable references used for reporting after a step.
  Stay-behind state: Cross-stage sequencing state intentionally retained by
    PhysicsWorld and documented in the ownership map.

Invariants:
  - Facade methods do not retain pointers or references into a stage owner.
  - Constraint handles describe dense point-joint rows owned by PhysicsWorld.
  - Diagnostic access is read-only and does not mutate solver ordering.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#include "PhysicsWorld.h"
#include "PhysicsApi.h"

using namespace SkullbonezCore::Physics;
namespace Math = SkullbonezCore::Math;

namespace
{
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

void PhysicsWorld::BeginCollisionVisualFrame( int modelCount )
{
    m_stepDiagnostics.BeginCollisionVisualFrame( modelCount );
    m_sleepController.EnsureVisualIdSize( modelCount );
}


void PhysicsWorld::EndCollisionVisualFrame()
{
    m_stepDiagnostics.EndCollisionVisualFrame();
}


void PhysicsWorld::ClearPointJointConstraints()
{
    m_pointJointConstraints.clear();
}


void PhysicsWorld::DestroyPointJointsForBody( PhysicsBodyHandle body )
{
    // Invariant: remove every joint that names the retiring handle before the
    // body slot can be reused. Runtime joint rows are dense and are not retained
    // as stable identity outside the physics owner.
    for ( std::size_t index = 0; index < m_pointJointConstraints.size(); )
    {
        const PointJointConstraint& constraint = m_pointJointConstraints[index];
        if ( constraint.bodyA != body && constraint.bodyB != body )
        {
            ++index;
            continue;
        }

        if ( index + 1u != m_pointJointConstraints.size() )
        {
            m_pointJointConstraints[index] = m_pointJointConstraints.back();
        }
        m_pointJointConstraints.pop_back();
    }
}


PhysicsConstraintHandle PhysicsWorld::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    if ( !desc.bodyA.IsValid() || !desc.bodyB.IsValid() || desc.bodyA == desc.bodyB )
    {
        return PhysicsConstraintHandle{};
    }

    // Why: callers create constraints with handle-keyed descriptors, while the
    // solver still iterates dense PointJointConstraint rows without indirection.
    PointJointConstraint constraint;
    constraint.SetBodies( desc.bodyA, desc.bodyB );
    constraint.localAnchorA = desc.localAnchorA;
    constraint.localAnchorB = desc.localAnchorB;
    constraint.slack = desc.slack;
    constraint.stiffness = desc.stiffness;
    constraint.damping = desc.damping;
    constraint.groupId = desc.groupId;
    constraint.flags = desc.flags;

    PhysicsConstraintHandle handle;
    handle.index = static_cast<uint32_t>( m_pointJointConstraints.size() );
    handle.generation = PHYSICS_HANDLE_INITIAL_GENERATION;
    m_pointJointConstraints.push_back( constraint );
    return handle;
}


const std::vector<PointJointConstraint>& PhysicsWorld::GetPointJointConstraints() const
{
    return m_pointJointConstraints;
}


bool PhysicsWorld::ShouldEmitStepDiagnostics() const
{
#ifdef _DEBUG
    return m_stepDiagnostics.ShouldEmitStepDiagnostics( m_diagnosticsSuppressed );
#else
    return false;
#endif
}


bool PhysicsWorld::ShouldEmitCollisionTimeDiagnostics() const
{
#ifdef _DEBUG
    return m_stepDiagnostics.ShouldEmitCollisionTimeDiagnostics( m_diagnosticsSuppressed );
#else
    return false;
#endif
}


void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        float fChangeInTime,
                                        const char* const* diagnosticNames,
                                        int diagnosticNameCount,
                                        const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
#ifdef _DEBUG
    const PhysicsDiagnosticsView diagnosticsView = GetDiagnosticsView();
    m_stepDiagnostics.EmitStepDiagnostics( m_diagnosticsSuppressed,
                                           diagnosticsView,
                                           bodyStore,
                                           colliderStore,
                                           fChangeInTime,
                                           diagnosticNames,
                                           diagnosticNameCount,
                                           diagnosticsCsvWriter );
#else
    (void)bodyStore;
    (void)colliderStore;
    (void)fChangeInTime;
    (void)diagnosticNames;
    (void)diagnosticNameCount;
    (void)diagnosticsCsvWriter;
#endif
}


#ifdef _DEBUG
void PhysicsWorld::SetPhysicsRegressionLogPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsRegressionLogPath( path );
}


void PhysicsWorld::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsDiagnosticsPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_stepDiagnostics.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsWorld::SetDiagnosticsSuppressed( bool suppressed )
{
    const bool previous = m_diagnosticsSuppressed;
    m_diagnosticsSuppressed = suppressed;
    return previous;
}


#endif


PhysicsDiagnosticsView PhysicsWorld::GetDiagnosticsView() const
{
    return PhysicsDiagnosticsView{ m_contactSolverStage.GetPersistentContacts(),
                                   m_contactSolverStage.GetStats(),
                                   m_sleepController.GetSleepIslandParents(),
                                   m_sleepController.GetSleepSupportedVector(),
                                   m_sleepController.GetSleepInhibitedVector(),
                                   m_sleepController.GetSleepStateVector(),
                                   m_sleepController.GetSleepCounters(),
                                   m_sleepController.GetSleepIslandEligible(),
                                   m_sleepController.GetSleepIslandCanSleep(),
                                   m_pointJointConstraints,
                                   m_broadphase.GetSpatialGrid(),
                                   m_broadphase.GetCandidatePairs(),
                                   m_broadphase.GetCollisionCellKeys(),
                                   m_sleepController.GetSleepSupportEdgeVector(),
                                   m_sleepController.GetSleepIslandVisualIdVector(),
                                   m_stepDiagnostics.GetPipelineTrace(),
                                   m_terrain.GetContactManifolds() };
}

uint64_t PhysicsWorld::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += m_forceStage.CollectDynamicMemoryBytes();
    bytes += m_broadphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_timeRemaining );
    bytes += m_sleepController.CollectDynamicMemoryBytes();
    bytes += m_stepDiagnostics.CollectDynamicMemoryBytes();
    bytes += m_contactSolverStage.CollectDynamicMemoryBytes();
    bytes += m_terrain.CollectDynamicMemoryBytes();
    bytes += m_narrowphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_pointJointConstraints );
    bytes += m_tornadoGameplay.CollectMemoryBytes();
    return bytes;
}

uint64_t PhysicsWorld::CollectDebugAndBroadphaseMemoryBytes() const
{
    uint64_t bytes = m_broadphase.CollectDebugAndBroadphaseMemoryBytes();
    bytes += m_stepDiagnostics.CollectDebugMemoryBytes();
    bytes += VectorCapacityBytes( m_sleepController.GetSleepIslandVisualIdVector() );
    bytes += m_tornadoGameplay.CollectDebugMemoryBytes();
    return bytes;
}


const Math::CollisionDetection::SpatialGrid& PhysicsWorld::GetSpatialGrid() const
{
    return m_broadphase.GetSpatialGrid();
}


const std::vector<int64_t>& PhysicsWorld::GetCollisionCellKeys() const
{
    return m_broadphase.GetCollisionCellKeys();
}


const std::vector<uint8_t>& PhysicsWorld::GetCollisionVisualContacts() const
{
    return m_stepDiagnostics.GetCollisionVisualContacts();
}


std::span<const int> PhysicsWorld::GetFixedContactHighlightBodies() const
{
    return m_contactSolverStage.GetSideEffects().fixedContactBodies;
}


std::span<const PhysicsFixedTreeReleaseEvent> PhysicsWorld::GetFixedTreeReleaseEvents() const
{
    return m_contactSolverStage.GetSideEffects().fixedTreeReleases;
}


std::span<const uint8_t> PhysicsWorld::GetSleepStates() const
{
    return m_sleepController.GetSleepStates();
}


std::span<const int> PhysicsWorld::GetSleepIslandVisualIds() const
{
    return m_sleepController.GetSleepIslandVisualIds();
}


std::span<const uint8_t> PhysicsWorld::GetSleepSupportedStates() const
{
    return m_sleepController.GetSleepSupportedStates();
}


std::span<const uint8_t> PhysicsWorld::GetSleepInhibitedStates() const
{
    return m_sleepController.GetSleepInhibitedStates();
}


const std::vector<PhysicsDebugContact>& PhysicsWorld::GetPhysicsDebugContacts() const
{
    return m_stepDiagnostics.GetDebugContacts();
}


const std::vector<PhysicsPipelineRecord>& PhysicsWorld::GetPhysicsPipelineTrace() const
{
    return m_stepDiagnostics.GetPipelineTrace();
}

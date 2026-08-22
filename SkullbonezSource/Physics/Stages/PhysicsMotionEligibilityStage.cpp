/*
File: SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.cpp
Purpose:
  Implements deterministic squared-threshold motion classification.

Summary:
  The stage reads force-resolved velocities and cold collider geometry facts in
  dense model order. It writes disjoint fixed-capacity rows, retaining only the
  previous classification bits required for hysteresis and replay.

Invariants:
  - Squared comparisons avoid per-body square roots.
  - Non-finite or invalid geometry promotes conservatively.
  - Timing observes the pass but never changes classification or ordering.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.h
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "PhysicsMotionEligibilityStage.h"

#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace SkullbonezCore::Physics
{
namespace
{
template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

bool ResolveEligibility( bool wasEligible, float travelSquared, float thickness )
{
    if ( !std::isfinite( travelSquared ) || !std::isfinite( thickness ) || thickness <= 0.0f )
    {
        return true;
    }

    const float alpha = wasEligible ? PHYSICS_MOTION_ALPHA_DEMOTE : PHYSICS_MOTION_ALPHA_PROMOTE;
    const float threshold = alpha * thickness;
    const float thresholdSquared = threshold * threshold;

    // Equality is deliberately asymmetric: a cold row promotes at equality,
    // while a hot row demotes at the lower equality boundary.
    return wasEligible ? travelSquared > thresholdSquared : travelSquared >= thresholdSquared;
}
} // namespace

void PhysicsMotionEligibilityStage::ReserveBodyCapacity( std::size_t bodyCapacity )
{
    m_state.Reserve( bodyCapacity );
    m_linearTravelSquared.Reserve( bodyCapacity );
    m_angularTravelSquared.Reserve( bodyCapacity );
    m_angularBroadphaseExpansion.Reserve( bodyCapacity );
}

void PhysicsMotionEligibilityStage::Clear()
{
    m_state.clear();
    m_linearTravelSquared.clear();
    m_angularTravelSquared.clear();
    m_angularBroadphaseExpansion.clear();
    m_stats = {};
    m_topologyInvalidated = true;
}

void PhysicsMotionEligibilityStage::InvalidateBodyTopology()
{
    m_topologyInvalidated = true;
}

void PhysicsMotionEligibilityStage::CommitReplayRestoreState( bool hasVersionedState )
{
    // Replay v4 owns exact hysteresis bytes. Body-row restoration may have
    // invalidated topology immediately before this final solver commit, so the
    // versioned owner explicitly makes those bytes authoritative. Legacy
    // snapshots remain cold and rebuild classification on their next step.
    m_linearTravelSquared.assign( m_state.size(), 0.0f );
    m_angularTravelSquared.assign( m_state.size(), 0.0f );
    m_angularBroadphaseExpansion.assign( m_state.size(), 0.0f );
    m_stats = {};
    m_topologyInvalidated = !hasVersionedState;
}

void PhysicsMotionEligibilityStage::Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         std::span<const uint8_t> sleepState, float dt )
{
    const auto begin = std::chrono::steady_clock::now();
    const int modelCount = (std::min)( { bodyStore.Count(), colliderStore.Count(), static_cast<int>( sleepState.size() ) } );
    const std::size_t rowCount = static_cast<std::size_t>( (std::max)( 0, modelCount ) );

    if ( m_topologyInvalidated || m_state.size() != rowCount )
    {
        m_state.assign( rowCount, 0u );
        m_topologyInvalidated = false;
    }

    // Capacity is committed at scene load. Resizing only on row-count changes
    // leaves the measured hot path as one contiguous classification walk.
    if ( m_linearTravelSquared.size() != rowCount )
    {
        m_linearTravelSquared.resize( rowCount );
        m_angularTravelSquared.resize( rowCount );
        m_angularBroadphaseExpansion.resize( rowCount );
    }

    m_stats = {};

    const PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliders = colliderStore.Records();
    const float dtSquared = dt * dt;

    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        const std::size_t row = static_cast<std::size_t>( bodyIndex );
        m_linearTravelSquared[row] = 0.0f;
        m_angularTravelSquared[row] = 0.0f;
        m_angularBroadphaseExpansion[row] = 0.0f;

        if ( hot.fixed[row] != 0u || sleepState[row] != 0u )
        {
            m_state[row] = 0u;
            continue;
        }

        ++m_stats.evaluatedBodies;
        const ColliderRecord& collider = colliders[row];
        const Math::Vector::Vector3 linear = PhysicsBodyLinearVelocity( hot, row );
        const Math::Vector::Vector3 angular = PhysicsBodyAngularVelocity( hot, row );
        const float linearTravelSquared = Math::Vector::VectorMagSquared( linear ) * dtSquared;
        const float angularTravelSquared = Math::Vector::VectorMagSquared( angular ) * collider.maximumCenterOfMassRadius *
                                           collider.maximumCenterOfMassRadius * dtSquared;
        m_linearTravelSquared[row] = linearTravelSquared;
        m_angularTravelSquared[row] = angularTravelSquared;

        const uint8_t previous = m_state[row];
        uint8_t resolved = 0u;

        if ( ResolveEligibility( ( previous & PhysicsMotionEligibilityLinearPromoted ) != 0u, linearTravelSquared,
                                 collider.minimumCollisionThickness ) )
        {
            resolved |= PhysicsMotionEligibilityLinearPromoted;
            ++m_stats.promotedBodies;
        }
        else
        {
            ++m_stats.discreteBodies;
        }

        if ( ResolveEligibility( ( previous & PhysicsMotionEligibilityAngularExpanded ) != 0u, angularTravelSquared,
                                 collider.minimumCollisionThickness ) )
        {
            resolved |= PhysicsMotionEligibilityAngularExpanded;
            ++m_stats.angularExpandedBodies;

            // Why: the sum of absolute angular components is a square-root-free
            // upper bound on angular speed. Multiplying by the farthest-point
            // radius yields a conservative broadphase tip-distance envelope.
            m_angularBroadphaseExpansion[row] = ( std::fabs( angular.x ) + std::fabs( angular.y ) +
                                                  std::fabs( angular.z ) ) *
                                                collider.maximumCenterOfMassRadius * dt;
        }

        m_state[row] = resolved;
    }

    m_stats.passDurationNanoseconds = static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now() - begin ).count() );
}

std::span<const uint8_t> PhysicsMotionEligibilityStage::State() const
{
    return m_state;
}
std::span<const float> PhysicsMotionEligibilityStage::LinearTravelSquared() const
{
    return m_linearTravelSquared;
}
std::span<const float> PhysicsMotionEligibilityStage::AngularTravelSquared() const
{
    return m_angularTravelSquared;
}
std::span<const float> PhysicsMotionEligibilityStage::AngularBroadphaseExpansion() const
{
    return m_angularBroadphaseExpansion;
}
const PhysicsMotionEligibilityStats& PhysicsMotionEligibilityStage::Stats() const
{
    return m_stats;
}
PhysicsBodyRowList<uint8_t>& PhysicsMotionEligibilityStage::StateForReplay()
{
    return m_state;
}
std::span<const uint8_t> PhysicsMotionEligibilityStage::StateForReplay() const
{
    return m_state;
}
std::size_t PhysicsMotionEligibilityStage::StateCapacityForReplay() const noexcept
{
    return m_state.capacity();
}

uint64_t PhysicsMotionEligibilityStage::CollectDynamicMemoryBytes() const
{
    return ListCapacityBytes( m_state ) + ListCapacityBytes( m_linearTravelSquared ) +
           ListCapacityBytes( m_angularTravelSquared ) + ListCapacityBytes( m_angularBroadphaseExpansion );
}
} // namespace SkullbonezCore::Physics

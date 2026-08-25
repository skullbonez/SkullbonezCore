/*
File: SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.cpp
Purpose:
  Implements deterministic squared-threshold motion classification.

Summary:
  The stage reads force-resolved velocities and collider geometry in dense model
  order. It can run the shipping absolute threshold or FP4's direction-valid
  radius-scaled trial without changing iteration order or replay-owned state bytes.

Invariants:
  - Squared comparisons avoid per-body square roots.
  - Non-finite predicted travel promotes conservatively.
  - The absolute control remains independent of collider thickness.
  - Linear promotion uses the exact radius along tick travel: sphere radius,
    oriented-box support, or bounded convex-hull vertex support.
  - Angular expansion remains conservative and uses half the minimum thickness.
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
#include <limits>
#include <type_traits>

namespace SkullbonezCore::Physics
{
namespace
{
template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

bool ResolveScalarEligibility( bool wasEligible, float travelSquared, float minimumCollisionThickness,
                               bool radiusScaledPolicy )
{
    if ( !std::isfinite( travelSquared ) )
    {
        return true;
    }

    if ( radiusScaledPolicy )
    {
        const float finiteThickness = std::isfinite( minimumCollisionThickness )
                                          ? (std::max)( 0.0f, minimumCollisionThickness )
                                          : 0.0f;
        const float threshold = finiteThickness * PHYSICS_RADIUS_SCALED_THRESHOLD_THICKNESS_FACTOR;
        const float thresholdSquared = threshold * threshold;

        // Invariant: the hot decision is one squared radius comparison. Travel
        // above promotes, travel below demotes, and exact equality preserves the
        // previous state so the boundary cannot chatter on identical inputs.
        return travelSquared == thresholdSquared ? wasEligible : travelSquared > thresholdSquared;
    }

    const float threshold = wasEligible ? PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK : PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK;
    const float thresholdSquared = threshold * threshold;

    // Invariant: equality is deliberately asymmetric. A Discrete row promotes
    // at the upper boundary, while a promoted row demotes at the lower boundary.
    return wasEligible ? travelSquared > thresholdSquared : travelSquared >= thresholdSquared;
}

float DirectionalBoundary( const Math::CollisionDetection::BoundingSphere& sphere, const Math::Vector::Vector3&, float )
{
    const float radius = (std::max)( 0.0f, sphere.GetRadius() );
    return radius * radius;
}

float DirectionalBoundary( const Math::CollisionDetection::BoundingBox& box, const Math::Vector::Vector3& localTravel,
                           float )
{
    const Math::Vector::Vector3 half = box.GetHalfExtents();

    // Concept: for non-zero travel L, this value is L times the OBB support
    // radius in the travel direction. Comparing L^2 against it is exactly
    // equivalent to L > radius, but needs neither normalization nor sqrt.
    return std::fabs( localTravel.x ) * std::fabs( half.x ) + std::fabs( localTravel.y ) * std::fabs( half.y ) +
           std::fabs( localTravel.z ) * std::fabs( half.z );
}

float DirectionalBoundary( const Math::CollisionDetection::ConvexHullShape& hull, const Math::Vector::Vector3& localTravel,
                           float )
{
    if ( hull.GetVertexCount() == 0u )
    {
        return 0.0f;
    }

    float minimumProjection = ( std::numeric_limits<float>::max )();
    float maximumProjection = ( std::numeric_limits<float>::lowest )();

    // Why: an enclosing AABB can overstate hull thickness and delay promotion.
    // The fixed-capacity hull has at most 64 vertices, so an allocation-free
    // support scan is the exact direction-valid test and has a bounded cost.
    for ( uint16_t vertexIndex = 0; vertexIndex < hull.GetVertexCount(); ++vertexIndex )
    {
        const float projection = Math::Vector::Dot( localTravel, hull.GetVertex( vertexIndex ) );
        minimumProjection = (std::min)( minimumProjection, projection );
        maximumProjection = (std::max)( maximumProjection, projection );
    }

    return 0.5f * ( maximumProjection - minimumProjection );
}

float ComputeDirectionalBoundary( const ColliderRecord& collider, const Math::Orientation::Quaternion& orientation,
                                  const Math::Vector::Vector3& worldTravel, float travelSquared )
{
    return Math::CollisionDetection::
        VisitCollisionShape( collider.shape,
                             [&]( const auto& shape )
                             {
                                 using Shape = std::decay_t<decltype( shape )>;

                                 if constexpr ( std::is_same_v<Shape, Math::CollisionDetection::BoundingSphere> )
                                 {
                                     return DirectionalBoundary( shape, worldTravel, travelSquared );
                                 }
                                 else
                                 {
                                     const Math::Vector::Vector3 localTravel = orientation.GetOrientationMatrix()
                                                                                   .TransposeMultiply( worldTravel );
                                     return DirectionalBoundary( shape, localTravel, travelSquared );
                                 }
                             } );
}

bool ResolveDirectionalEligibility( bool wasEligible, float travelSquared, float directionalBoundary )
{
    if ( !std::isfinite( travelSquared ) || !std::isfinite( directionalBoundary ) )
    {
        return true;
    }

    if ( travelSquared <= 0.0f )
    {
        return false;
    }

    const float finiteBoundary = (std::max)( 0.0f, directionalBoundary );

    // Invariant: above promotes, below demotes, and exact non-zero equality
    // retains prior state. Stationary rows demote because multiplying the
    // directional radius by zero otherwise collapses both sides to equality.
    return travelSquared == finiteBoundary ? wasEligible : travelSquared > finiteBoundary;
}
} // namespace

void PhysicsMotionEligibilityStage::ReserveBodyCapacity( std::size_t bodyCapacity )
{
    m_state.Reserve( bodyCapacity );
    m_linearTravelSquared.Reserve( bodyCapacity );
    m_linearDirectionalBoundary.Reserve( bodyCapacity );
    m_angularTravelSquared.Reserve( bodyCapacity );
    m_angularBroadphaseExpansion.Reserve( bodyCapacity );
}

void PhysicsMotionEligibilityStage::Clear()
{
    m_state.clear();
    m_linearTravelSquared.clear();
    m_linearDirectionalBoundary.clear();
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
    m_linearDirectionalBoundary.assign( m_state.size(), 0.0f );
    m_angularTravelSquared.assign( m_state.size(), 0.0f );
    m_angularBroadphaseExpansion.assign( m_state.size(), 0.0f );
    m_stats = {};
    m_topologyInvalidated = !hasVersionedState;
}

void PhysicsMotionEligibilityStage::Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         std::span<const uint8_t> sleepState, float dt, bool radiusScaledPolicy )
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
        m_linearDirectionalBoundary.resize( rowCount );
        m_angularTravelSquared.resize( rowCount );
        m_angularBroadphaseExpansion.resize( rowCount );
    }

    m_stats = {};
    m_stats.policyVersion = radiusScaledPolicy ? PHYSICS_RADIUS_SCALED_MOTION_ELIGIBILITY_POLICY_VERSION
                                               : PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION;

    const PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliders = colliderStore.Records();
    const float dtSquared = dt * dt;

    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        const std::size_t row = static_cast<std::size_t>( bodyIndex );
        const uint8_t previous = m_state[row];
        m_linearTravelSquared[row] = 0.0f;
        m_linearDirectionalBoundary[row] = 0.0f;
        m_angularTravelSquared[row] = 0.0f;
        m_angularBroadphaseExpansion[row] = 0.0f;

        if ( hot.fixed[row] != 0u || sleepState[row] != 0u )
        {
            if ( ( previous & PhysicsMotionEligibilityLinearPromoted ) != 0u )
            {
                ++m_stats.demotionsThisStep;
            }

            m_state[row] = 0u;
            continue;
        }

        ++m_stats.evaluatedBodies;
        const ColliderRecord& collider = colliders[row];
        const Math::Vector::Vector3 linear = PhysicsBodyLinearVelocity( hot, row );
        const Math::Vector::Vector3 angular = PhysicsBodyAngularVelocity( hot, row );
        const Math::Vector::Vector3 linearTravel = linear * dt;
        const float linearTravelSquared = Math::Vector::VectorMagSquared( linear ) * dtSquared;
        const float angularTravelSquared = Math::Vector::VectorMagSquared( angular ) * collider.maximumCenterOfMassRadius *
                                           collider.maximumCenterOfMassRadius * dtSquared;
        m_linearTravelSquared[row] = linearTravelSquared;
        m_angularTravelSquared[row] = angularTravelSquared;

        uint8_t resolved = 0u;

        const float directionalBoundary = radiusScaledPolicy
                                              ? ComputeDirectionalBoundary( collider, PhysicsBodyOrientation( hot, row ),
                                                                            linearTravel, linearTravelSquared )
                                              : 0.0f;
        m_linearDirectionalBoundary[row] = directionalBoundary;
        const bool linearEligible = radiusScaledPolicy
                                        ? ResolveDirectionalEligibility( ( previous &
                                                                           PhysicsMotionEligibilityLinearPromoted ) != 0u,
                                                                         linearTravelSquared, directionalBoundary )
                                        : ResolveScalarEligibility( ( previous & PhysicsMotionEligibilityLinearPromoted ) !=
                                                                        0u,
                                                                    linearTravelSquared, collider.minimumCollisionThickness,
                                                                    false );

        if ( linearEligible )
        {
            resolved |= PhysicsMotionEligibilityLinearPromoted;
            ++m_stats.promotedBodies;
        }
        else
        {
            ++m_stats.discreteBodies;
        }

        const bool wasPromoted = ( previous & PhysicsMotionEligibilityLinearPromoted ) != 0u;
        const bool isPromoted = ( resolved & PhysicsMotionEligibilityLinearPromoted ) != 0u;

        if ( !wasPromoted && isPromoted )
        {
            ++m_stats.promotionsThisStep;
        }
        else if ( wasPromoted && !isPromoted )
        {
            ++m_stats.demotionsThisStep;
        }

        if ( ResolveScalarEligibility( ( previous & PhysicsMotionEligibilityAngularExpanded ) != 0u, angularTravelSquared,
                                       collider.minimumCollisionThickness, radiusScaledPolicy ) )
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

std::span<const float> PhysicsMotionEligibilityStage::LinearDirectionalBoundary() const
{
    return m_linearDirectionalBoundary;
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
           ListCapacityBytes( m_linearDirectionalBoundary ) + ListCapacityBytes( m_angularTravelSquared ) +
           ListCapacityBytes( m_angularBroadphaseExpansion );
}
} // namespace SkullbonezCore::Physics

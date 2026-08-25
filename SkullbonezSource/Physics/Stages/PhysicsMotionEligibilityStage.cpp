/*
File: SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.cpp
Purpose:
  Implements deterministic squared-threshold motion classification.

Summary:
  The stage reads force-resolved velocities and collider geometry in dense model
  order and applies the direction-valid radius policy without changing iteration
  order or replay-owned state bytes.

Invariants:
  - Squared comparisons avoid per-body square roots.
  - Non-finite predicted travel promotes conservatively.
  - Linear promotion uses direct squared comparisons against sphere radius,
    box half-extents, or the cached hull difference-body SAT axes.
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

bool ResolveAngularEligibility( bool wasEligible, float travelSquared, float minimumCollisionThickness )
{
    if ( !std::isfinite( travelSquared ) )
    {
        return true;
    }

    const float finiteThickness = std::isfinite( minimumCollisionThickness ) ? (std::max)( 0.0f, minimumCollisionThickness )
                                                                             : 0.0f;
    const float threshold = finiteThickness * PHYSICS_ANGULAR_EXPANSION_THRESHOLD_THICKNESS_FACTOR;
    const float thresholdSquared = threshold * threshold;

    // Invariant: travel above expands, travel below contracts, and exact
    // equality preserves the previous state so identical inputs cannot chatter.
    return travelSquared == thresholdSquared ? wasEligible : travelSquared > thresholdSquared;
}

struct DirectionalEligibilityFacts
{
    float boundarySquared = std::numeric_limits<float>::quiet_NaN();
    bool above = false;
    bool equal = false;
    bool finite = true;
};

void IncludeDirectionalAxis( DirectionalEligibilityFacts& facts, float projectedTravelSquared, float axisBoundarySquared,
                             float travelSquared )
{
    if ( !std::isfinite( projectedTravelSquared ) || !std::isfinite( axisBoundarySquared ) )
    {
        facts.finite = false;
        return;
    }

    if ( projectedTravelSquared <= 0.0f )
    {
        return;
    }

    facts.above = facts.above || projectedTravelSquared > axisBoundarySquared;
    facts.equal = facts.equal || projectedTravelSquared == axisBoundarySquared;

    const float rayBoundarySquared = travelSquared * axisBoundarySquared / projectedTravelSquared;
    facts.boundarySquared = std::isfinite( facts.boundarySquared ) ? (std::min)( facts.boundarySquared, rayBoundarySquared )
                                                                   : rayBoundarySquared;
}

DirectionalEligibilityFacts DirectionalFacts( const Math::CollisionDetection::BoundingSphere& sphere,
                                              const Math::Vector::Vector3&, float travelSquared )
{
    const float radius = (std::max)( 0.0f, sphere.GetRadius() );
    DirectionalEligibilityFacts facts;
    IncludeDirectionalAxis( facts, travelSquared, radius * radius, travelSquared );
    return facts;
}

DirectionalEligibilityFacts LocalAxisFacts( const Math::Vector::Vector3& halfExtents,
                                            const Math::Vector::Vector3& localTravel, float travelSquared )
{
    DirectionalEligibilityFacts facts;
    const auto includeAxis = [&]( float travel, float halfExtent )
    {
        const float axisTravelSquared = travel * travel;
        const float finiteHalfExtent = (std::max)( 0.0f, std::fabs( halfExtent ) );
        IncludeDirectionalAxis( facts, axisTravelSquared, finiteHalfExtent * finiteHalfExtent, travelSquared );
    };

    includeAxis( localTravel.x, halfExtents.x );
    includeAxis( localTravel.y, halfExtents.y );
    includeAxis( localTravel.z, halfExtents.z );

    return facts;
}

DirectionalEligibilityFacts DirectionalFacts( const Math::CollisionDetection::BoundingBox& box,
                                              const Math::Vector::Vector3& localTravel, float travelSquared )
{
    return LocalAxisFacts( box.GetHalfExtents(), localTravel, travelSquared );
}

DirectionalEligibilityFacts DirectionalFacts( const Math::CollisionDetection::ConvexHullShape& hull,
                                              const Math::Vector::Vector3& localTravel, float travelSquared )
{
    DirectionalEligibilityFacts facts;

    for ( const Math::CollisionDetection::ConvexHullMotionAxis& axis : hull.GetMotionAxes() )
    {
        const float projectedTravel = Math::Vector::Dot( localTravel, axis.normalLocal );
        const float projectedTravelSquared = projectedTravel * projectedTravel;
        IncludeDirectionalAxis( facts, projectedTravelSquared, axis.halfWidthSquared, travelSquared );
    }

    return facts;
}

DirectionalEligibilityFacts ComputeDirectionalFacts( const ColliderRecord& collider,
                                                     const Math::Orientation::Quaternion& orientation,
                                                     const Math::Vector::Vector3& worldTravel, float travelSquared )
{
    return Math::CollisionDetection::
        VisitCollisionShape( collider.shape,
                             [&]( const auto& shape )
                             {
                                 using Shape = std::decay_t<decltype( shape )>;

                                 if constexpr ( std::is_same_v<Shape, Math::CollisionDetection::BoundingSphere> )
                                 {
                                     return DirectionalFacts( shape, worldTravel, travelSquared );
                                 }
                                 else
                                 {
                                     const Math::Vector::Vector3 localTravel = orientation.GetOrientationMatrix()
                                                                                   .TransposeMultiply( worldTravel );
                                     return DirectionalFacts( shape, localTravel, travelSquared );
                                 }
                             } );
}

bool ResolveDirectionalEligibility( bool wasEligible, float travelSquared, const DirectionalEligibilityFacts& facts )
{
    if ( !std::isfinite( travelSquared ) || !facts.finite )
    {
        return true;
    }

    if ( travelSquared <= 0.0f )
    {
        return false;
    }

    // Invariant: classify on the original per-axis squares. The separately
    // reconstructed ray boundary is diagnostics only; using it here would let
    // multiply/divide rounding break exact mixed-axis equality hysteresis.
    return facts.above ? true : ( facts.equal ? wasEligible : false );
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
    m_linearDirectionalBoundary.assign( m_state.size(), -1.0f );
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
        m_linearDirectionalBoundary.resize( rowCount );
        m_angularTravelSquared.resize( rowCount );
        m_angularBroadphaseExpansion.resize( rowCount );
    }

    m_stats = {};
    m_stats.policyVersion = PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION;

    const PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliders = colliderStore.Records();
    const float dtSquared = dt * dt;

    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        const std::size_t row = static_cast<std::size_t>( bodyIndex );
        const uint8_t previous = m_state[row];
        m_linearTravelSquared[row] = 0.0f;

        // SkullScope maps negative boundaries to -1 (unavailable). Fixed and
        // sleeping rows are not evaluated and must not publish a fake 0 m
        // promote/demote distance. The finite sentinel also remains byte-exact
        // under the determinism lane's retained diagnostics comparison.
        m_linearDirectionalBoundary[row] = -1.0f;
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

        const DirectionalEligibilityFacts directionalFacts = ComputeDirectionalFacts( collider,
                                                                                      PhysicsBodyOrientation( hot, row ),
                                                                                      linearTravel, linearTravelSquared );
        m_linearDirectionalBoundary[row] = directionalFacts.boundarySquared;
        const bool linearEligible = ResolveDirectionalEligibility( ( previous & PhysicsMotionEligibilityLinearPromoted ) !=
                                                                       0u,
                                                                   linearTravelSquared, directionalFacts );

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

        if ( ResolveAngularEligibility( ( previous & PhysicsMotionEligibilityAngularExpanded ) != 0u, angularTravelSquared,
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

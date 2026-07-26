/*
File: SkullbonezSource/Physics/BuoyancySystem.cpp
Purpose:
  Implements dense fluid-fact lifecycle and deterministic submersion queries.

Summary:
  Descriptor facts enter one dense fixed-capacity store at cold topology
  boundaries. The main force integrator borrows those rows, while underwater
  sleep locking refreshes only the candidate row's transient submersion value.

Glossary:
  Sphere cap: Portion of a sphere below the fluid surface; its analytic volume
    gives a deterministic submerged fraction without sampling.
  Buoyancy row: Physics-owned dense fluid facts aligned with one body row.
  Fluid surface: World-space Y plane where the fluid medium begins.

Invariants:
  - Row mutations mirror body/collider dense compaction exactly.
  - Non-sphere or invalid colliders leave the refreshed submersion fraction at
    zero and report no usable buoyancy snapshot.
  - Full-submerged classification uses a near-one threshold so tiny float drift
    near the analytic cap limit does not churn sleep-lock decisions.

Related:
  - SkullbonezSource/Physics/BuoyancySystem.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/physics-overview.md
*/
#include "BuoyancySystem.h"

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorldForces.h"
#include "../Maths/MathsCommon.h"

#include <algorithm>
#include <variant>

namespace SkullbonezCore
{
namespace Physics
{
namespace
{
constexpr float UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT = 0.999f;

void ApplyDescriptorFacts( const PhysicsBodyCreateDesc& desc, BuoyancyBodyFacts& facts )
{

    // Invariant: assignment order matches the retired PhysicsBodyRecord
    // stamping path so topology refresh cannot change fixed-step inputs.
    facts.volume = desc.volume;
    facts.projectedSurfaceArea = desc.projectedSurfaceArea;
    facts.dragCoefficient = desc.dragCoefficient;
    facts.submergedVolumePercent = 0.0f;
    facts.contactEpsilon = desc.contactEpsilon;
}
} // namespace


void BuoyancySystem::ReserveCapacity( std::size_t capacity )
{
    m_bodyFacts.Reserve( capacity );
}


bool BuoyancySystem::AppendBodyFacts( const PhysicsBodyCreateDesc& desc )
{

    if ( m_bodyFacts.size() >= m_bodyFacts.capacity() )
    {
        return false;
    }

    m_bodyFacts.push_back( {} );
    ApplyDescriptorFacts( desc, m_bodyFacts.back() );
    return true;
}


bool BuoyancySystem::RefreshBodyFacts( int index, const PhysicsBodyCreateDesc& desc )
{

    if ( index < 0 || index >= Count() )
    {
        return false;
    }

    ApplyDescriptorFacts( desc, m_bodyFacts[static_cast<std::size_t>( index )] );
    return true;
}


bool BuoyancySystem::EraseBodyFactsSwapLast( int index )
{

    if ( index < 0 || index >= Count() )
    {
        return false;
    }

    const std::size_t row = static_cast<std::size_t>( index );

    if ( row + 1u != m_bodyFacts.size() )
    {
        m_bodyFacts[row] = m_bodyFacts.back();
    }

    m_bodyFacts.pop_back();
    return true;
}


bool BuoyancySystem::TrimToCount( int count )
{

    if ( count < 0 || count > Count() )
    {
        return false;
    }

    m_bodyFacts.resize( static_cast<std::size_t>( count ) );
    return true;
}


void BuoyancySystem::Clear()
{
    m_bodyFacts.clear();
}


int BuoyancySystem::Count() const
{
    return static_cast<int>( m_bodyFacts.size() );
}


std::size_t BuoyancySystem::RecordCapacity() const
{
    return m_bodyFacts.capacity();
}


std::span<const BuoyancyBodyFacts> BuoyancySystem::Facts() const
{
    return { m_bodyFacts.data(), m_bodyFacts.size() };
}


std::span<BuoyancyBodyFacts> BuoyancySystem::MutableFacts()
{
    return { m_bodyFacts.data(), m_bodyFacts.size() };
}


bool BuoyancySystem::IsFullySubmergedBall( const BuoyancyBodyFacts& facts, bool fixed, const ColliderStore& colliderStore,
                                           int index )
{
    const auto colliders = colliderStore.Records();

    if ( index < 0 || index >= static_cast<int>( colliders.size() ) || fixed ||
         colliders[static_cast<std::size_t>( index )].shapeKind != ColliderShapeKind::Sphere )
    {
        return false;
    }

    return facts.submergedVolumePercent >= UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT;
}


bool BuoyancySystem::RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces,
                                                         const PhysicsBodyStore& bodyStore,
                                                         const ColliderStore& colliderStore, BuoyancyBodyFacts& facts,
                                                         int index )
{

    if ( index < 0 || index >= bodyStore.Count() )
    {
        return false;
    }

    facts.submergedVolumePercent = 0.0f;
    const auto colliders = colliderStore.Records();

    if ( index < 0 || index >= static_cast<int>( colliders.size() ) )
    {
        return false;
    }

    const ColliderRecord& collider = colliders[static_cast<std::size_t>( index )];

    if ( collider.shapeKind != ColliderShapeKind::Sphere )
    {
        return false;
    }

    const auto* sphere = Math::CollisionDetection::GetShapeIf<Math::CollisionDetection::BoundingSphere>( &collider.shape );

    if ( !sphere )
    {
        return false;
    }

    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::size_t bodyIndex = static_cast<std::size_t>( index );
    const Math::Transformation::RotationMatrix rotation = PhysicsBodyOrientation( hotFields, bodyIndex )
                                                              .GetOrientationMatrix();

    const Math::Vector::Vector3 center = PhysicsBodyPosition( hotFields, bodyIndex ) + ( rotation * sphere->GetPosition() );

    const float radius = sphere->GetRadius();

    if ( radius <= TOLERANCE )
    {
        return false;
    }

    const float fluidHeightRelativeToCenter = worldForces.fluidSurfaceHeight - center.y;

    if ( fluidHeightRelativeToCenter <= -radius )
    {
        return true;
    }

    if ( fluidHeightRelativeToCenter >= radius )
    {
        facts.submergedVolumePercent = 1.0f;
        return true;
    }

    // Concept: sphere-cap volume gives the submerged fraction for the exact
    // sphere pose. The sleep policy reads this value later; no force is applied
    // here.
    const float yValue = fluidHeightRelativeToCenter + radius;
    facts.submergedVolumePercent = std::clamp( ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) /
                                                   sphere->GetVolume(),
                                               0.0f, 1.0f );

    return true;
}
} // namespace Physics
} // namespace SkullbonezCore

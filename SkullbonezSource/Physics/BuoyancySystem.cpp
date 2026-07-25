/*
File: SkullbonezSource/Physics/BuoyancySystem.cpp
Purpose:
  Implements deterministic sphere-cap buoyancy snapshots for sleep policy.

Summary:
  The main force integrator already computes broad fluid forces for all bodies.
  Underwater sleep locking needs one narrow question: is this ball fully
  submerged now? This file answers that question and writes only the body row's
  transient submersion snapshot.

Glossary:
  Sphere cap: Portion of a sphere below the fluid surface; its analytic volume
    gives a deterministic submerged fraction without sampling.
  Body row: Physics-owned dense record mutated during a fixed tick.
  Fluid surface: World-space Y plane where the fluid medium begins.

Invariants:
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
}


bool BuoyancySystem::IsFullySubmergedBall( const PhysicsBodyRecord& bodyRecord,
                                           bool fixed,
                                           const ColliderStore& colliderStore,
                                           int index )
{
    const auto colliders = colliderStore.Records();
    if ( index < 0 || index >= static_cast<int>( colliders.size() ) || fixed ||
         colliders[static_cast<std::size_t>( index )].shapeKind != ColliderShapeKind::Sphere )
    {
        return false;
    }

    return bodyRecord.submergedVolumePercent >= UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT;
}


bool BuoyancySystem::RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces,
                                                         PhysicsBodyStore& bodyStore,
                                                         const ColliderStore& colliderStore,
                                                         int index )
{
    PhysicsBodyRecord* bodyRecord = bodyStore.MutableRecordForModelIndex( index );
    if ( !bodyRecord )
    {
        return false;
    }

    bodyRecord->submergedVolumePercent = 0.0f;
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

    const auto* sphere = std::get_if<Math::CollisionDetection::BoundingSphere>( &collider.shape );
    if ( !sphere )
    {
        return false;
    }

    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::size_t bodyIndex = static_cast<std::size_t>( index );
    const Math::Transformation::RotationMatrix rotation = PhysicsBodyOrientation( hotFields, bodyIndex )
                                                              .GetOrientationMatrix();

    const Math::Vector::Vector3 center = PhysicsBodyPosition( hotFields, bodyIndex ) +
                                         ( rotation * sphere->GetPosition() );

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
        bodyRecord->submergedVolumePercent = 1.0f;
        return true;
    }

    // Concept: sphere-cap volume gives the submerged fraction for the exact
    // sphere pose. The sleep policy reads this value later; no force is applied
    // here.
    const float yValue = fluidHeightRelativeToCenter + radius;
    bodyRecord->submergedVolumePercent = std::clamp(
        ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) / sphere->GetVolume(),
        0.0f,
        1.0f );

    return true;
}
} // namespace Physics
} // namespace SkullbonezCore

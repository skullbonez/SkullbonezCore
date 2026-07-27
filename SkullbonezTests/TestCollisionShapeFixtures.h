//
// File: SkullbonezTests/TestCollisionShapeFixtures.h
// Purpose:
//   Share value-only collision-shape constructors across subsystem test files.
//
// Invariants:
//   - Shapes are centered at the local origin with no skin inflation.
//   - The helpers own no mutable state and do not weaken subsystem assertions.
//

#pragma once

#include "../SkullbonezSource/Physics/CollisionShape.h"

namespace SkullbonezTests::CollisionShapeFixtures
{
inline SkullbonezCore::Math::CollisionDetection::CollisionShape SphereShape( float radius )
{
    using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
    using SkullbonezCore::Math::CollisionDetection::CollisionShape;
    using SkullbonezCore::Math::Vector::Vector3;
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

inline SkullbonezCore::Math::CollisionDetection::CollisionShape
BoxShape( const SkullbonezCore::Math::Vector::Vector3& halfExtents )
{
    using SkullbonezCore::Math::CollisionDetection::BoundingBox;
    using SkullbonezCore::Math::CollisionDetection::CollisionShape;
    using SkullbonezCore::Math::Vector::Vector3;
    return CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
}
} // namespace SkullbonezTests::CollisionShapeFixtures

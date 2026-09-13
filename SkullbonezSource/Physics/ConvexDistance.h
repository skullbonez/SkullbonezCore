#pragma once

#include "ObjectContactManifold.h"

namespace SkullbonezCore::Physics
{
// Witnesses lie on the two uninflated shapes. Positive separation means a gap;
// negative separation is available for sphere margins, not general penetration.
// Overlapping polytope cores use the existing contact manifold for penetration.
struct ConvexDistanceResult
{
    Math::Vector::Vector3 pointA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pointB = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    float separation = 0.0f;
    uint32_t featureId = 0;
    int iterations = 0;
    bool converged = false;
};

// Bounded, allocation-free GJK distance. Stable support ties and simplex order
// make identical shape/pose inputs produce identical witnesses and feature IDs.
ConvexDistanceResult ComputeConvexDistance( const ObjectContactBodyView& a,
                                            const Math::CollisionDetection::CollisionShapeReference& shapeA,
                                            const ObjectContactBodyView& b,
                                            const Math::CollisionDetection::CollisionShapeReference& shapeB );
} // namespace SkullbonezCore::Physics

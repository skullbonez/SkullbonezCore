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

struct ConvexCastResult
{
    bool hit = false;
    bool converged = false;
    float collisionTime = 0.0f;
    int iterations = 0;
};

// Constant-orientation translation only. Each advance uses a separating support
// plane; only the complete discrete manifold can authorize a hit. Failure leaves
// both input poses untouched. At most 64 distance queries, each bounded by 32
// GJK iterations, run without allocation.
ConvexCastResult CastConvexContact( const ObjectContactBodyView& a,
                                    const Math::CollisionDetection::CollisionShapeReference& shapeA,
                                    const Math::Vector::Vector3& velocityA,
                                    const ObjectContactBodyView& b,
                                    const Math::CollisionDetection::CollisionShapeReference& shapeB,
                                    const Math::Vector::Vector3& velocityB,
                                    float availableTime,
                                    float contactSkin );
} // namespace SkullbonezCore::Physics

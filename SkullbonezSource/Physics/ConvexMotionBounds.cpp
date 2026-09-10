#include "ConvexMotionBounds.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace SkullbonezCore::Physics
{
using Math::Vector::Dot;
using Math::Vector::Vector3;
using Math::Vector::ZERO_VECTOR;
namespace Vector = Math::Vector;

float MaximumRotatedProjection( const Math::Orientation::Quaternion& orientation,
                                const Math::CollisionDetection::CollisionShapeReference& shape,
                                const Vector3& angularVelocity, const Vector3& normal, float stepDuration )
{
    const auto rotation = orientation.GetOrientationMatrix();
    Vector3 integratedOmega = angularVelocity;
    // Match PhysicsPoseIntegration's component simplification before deriving
    // the axis; otherwise a tiny discarded component can tilt the orbit bound.
    integratedOmega.Simplify();
    const float speed = Vector::VectorMag( integratedOmega );
    const Vector3 axis = speed > 0.0f ? integratedOmega / speed : ZERO_VECTOR;
    const float angle = speed * stepDuration;
    auto project = [&]( const Vector3& localPoint )
    {
        const Vector3 point = rotation * localPoint;
        const float initial = Dot( normal, point );
        if ( speed == 0.0f )
        {
            return initial;
        }
        // Concept: an ideal axis rotation projects as c + a*cos(t) + b*sin(t).
        // Its second derivative has magnitude at most sqrt(a*a+b*b). The
        // convex Taylor upper bound therefore peaks at one of the arc ends.
        // Intersect it with the full-orbit bound; neither needs transcendental
        // functions or a numerically ambiguous stationary-angle comparison.
        const float constant = Dot( normal, axis ) * Dot( point, axis );
        const float a = initial - constant;
        const float b = Dot( normal, Vector::CrossProduct( axis, point ) );
        const float amplitude = std::sqrt( a * a + b * b );
        const float orbitMaximum = constant + amplitude;
        const float arcMaximum = initial + (std::max)( 0.0f, b * angle + 0.5f * amplitude * angle * angle );
        // Hazard: PhysicsPoseIntegration constructs a normalized delta with
        // deterministic HALF-angle math. TestDeterministicMath certifies
        // 0.00171 radians of angular error, so the final rotation may differ
        // by twice that. 0.0035 bounds its projection displacement per radius;
        // the separate roundoff term covers axis, dot and quaternion arithmetic.
        constexpr float integrationAngleAllowance = 0.0035f;
        const float radius = Vector::VectorMag( point );
        const float roundoff = 64.0f * std::numeric_limits<float>::epsilon() * ( 1.0f + radius );
        return (std::min)( orbitMaximum, arcMaximum + integrationAngleAllowance * radius ) + roundoff;
    };
    return Math::CollisionDetection::
        VisitCollisionShape( shape,
                             [&]( const auto& value )
                             {
                                 using Shape = std::decay_t<decltype( value )>;
                                 if constexpr ( std::is_same_v<Shape, Math::CollisionDetection::BoundingSphere> )
                                 {
                                     return project( value.GetPosition() ) + value.GetRadius();
                                 }
                                 else
                                 {
                                     float maximum = -std::numeric_limits<float>::max();
                                     if constexpr ( std::is_same_v<Shape, Math::CollisionDetection::BoundingBox> )
                                     {
                                         const Vector3 half = value.GetHalfExtents();
                                         for ( unsigned index = 0; index < 8u; ++index )
                                         {
                                             const Vector3 vertex( index & 1u ? half.x : -half.x,
                                                                   index & 2u ? half.y : -half.y,
                                                                   index & 4u ? half.z : -half.z );
                                             maximum = (std::max)( maximum, project( value.GetPosition() + vertex ) );
                                         }
                                     }
                                     else
                                     {
                                         for ( uint16_t index = 0; index < value.GetVertexCount(); ++index )
                                         {
                                             maximum = (std::max)( maximum, project( value.GetPosition() +
                                                                                     value.GetVertex( index ) ) );
                                         }
                                     }
                                     return maximum;
                                 }
                             } );
}

} // namespace SkullbonezCore::Physics

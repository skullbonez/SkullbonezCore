#include "SpeculativeContacts.h"
#include "ConvexDistance.h"
#include "ConvexMotionBounds.h"
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "../Core/FatalError.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace SkullbonezCore::Physics
{
using Math::Vector::Dot;
using Math::Vector::Vector3;
using Math::Vector::ZERO_VECTOR;
namespace Vector = Math::Vector;
namespace
{
float AngularReach( const ColliderRecord& collider, const Vector3& angularVelocity )
{
    const auto* sphere = Math::CollisionDetection::GetShapeIf<Math::CollisionDetection::BoundingSphere>( &collider.shape );
    // A centered sphere's spin changes neither its occupied volume nor its
    // swept path. Only an offset sphere center needs rotational expansion.
    const float radius = sphere ? Vector::VectorMag( sphere->GetPosition() ) : collider.maximumCenterOfMassRadius;
    return Vector::VectorMag( angularVelocity ) * radius;
}


bool WitnessIsOnShape( const Vector3& witness, const ObjectContactBodyView& body,
                       const Math::CollisionDetection::CollisionShapeReference& shape )
{
    const Math::CollisionDetection::CollisionShape point = Math::CollisionDetection::BoundingSphere( 0.0f, ZERO_VECTOR );
    ObjectContactBodyView pointBody;
    pointBody.position = witness;
    const auto distance = ComputeConvexDistance( pointBody, point, body, shape );
    // World witnesses have already rounded to body-coordinate precision.
    const float tolerance = 8.0f * std::numeric_limits<float>::epsilon() *
                            (std::max)( 1.0f, Vector::VectorMag( body.position ) );
    return distance.converged && distance.separation <= tolerance;
}

bool BuildAngularContactPatch( const PhysicsBodyStore& bodies, const ColliderStore& colliders, float stepDuration,
                               float searchDistance, int bodyA, int bodyB, ObjectContactManifold& manifold )
{
    const auto hot = bodies.HotFields();
    const ObjectContactBodyView a { PhysicsBodyPosition( hot, bodyA ), PhysicsBodyOrientation( hot, bodyA ) };
    const ObjectContactBodyView b { PhysicsBodyPosition( hot, bodyB ), PhysicsBodyOrientation( hot, bodyB ) };
    const auto& shapeA = colliders.Records()[bodyA].shape;
    const auto& shapeB = colliders.Records()[bodyB].shape;
    if ( !BuildObjectContactManifold( a, shapeA, b, shapeB, bodyA, bodyB, searchDistance, manifold ) )
    {
        return false;
    }
    const Vector3 linearA = hot.fixed[bodyA] ? ZERO_VECTOR : PhysicsBodyLinearVelocity( hot, bodyA );
    const Vector3 linearB = hot.fixed[bodyB] ? ZERO_VECTOR : PhysicsBodyLinearVelocity( hot, bodyB );
    const Vector3 angularA = hot.fixed[bodyA] ? ZERO_VECTOR : PhysicsBodyAngularVelocity( hot, bodyA );
    const Vector3 angularB = hot.fixed[bodyB] ? ZERO_VECTOR : PhysicsBodyAngularVelocity( hot, bodyB );
    uint8_t admitted = 0;
    for ( uint8_t index = 0; index < manifold.pointCount; ++index )
    {
        ObjectContactPoint point = manifold.points[index];
        const float gap = point.signedSeparation;
        if ( gap <= 0.0f )
        {
            continue;
        }
        const Vector3 witnessA = point.point - manifold.normal * ( gap * 0.5f );
        const Vector3 witnessB = point.point + manifold.normal * ( gap * 0.5f );
        // Hazard: normal-range clipping may extend a face's side planes. Reject
        // any resulting point outside either uninflated collider so a nearby
        // corner miss cannot manufacture a contact patch in empty space.
        if ( !WitnessIsOnShape( witnessA, a, shapeA ) || !WitnessIsOnShape( witnessB, b, shapeB ) )
        {
            continue;
        }
        point.rA = witnessA - a.position;
        point.rB = witnessB - b.position;
        const Vector3 velocityA = linearA + Vector::CrossProduct( angularA, point.rA );
        const Vector3 velocityB = linearB + Vector::CrossProduct( angularB, point.rB );
        const float closing = Dot( velocityB - velocityA, manifold.normal );
        if ( closing >= 0.0f || gap + closing * stepDuration >= 0.0f )
        {
            continue;
        }
        point.penetration = -gap;
        manifold.points[admitted++] = point;
    }
    manifold.pointCount = admitted;
    return admitted != 0u;
}
} // namespace

bool BuildArticulatedContactManifold( const PhysicsBodyStore& bodies, const ColliderStore& colliders, float stepDuration,
                                      float contactEpsilon, int bodyA, int bodyB, ObjectContactManifold& manifold )
{
    const auto hot = bodies.HotFields();
    const ObjectContactBodyView a { PhysicsBodyPosition( hot, bodyA ), PhysicsBodyOrientation( hot, bodyA ) };
    const ObjectContactBodyView b { PhysicsBodyPosition( hot, bodyB ), PhysicsBodyOrientation( hot, bodyB ) };
    const ColliderRecord& colliderA = colliders.Records()[bodyA];
    const ColliderRecord& colliderB = colliders.Records()[bodyB];
    if ( BuildObjectContactManifold( a, colliderA.shape, b, colliderB.shape, bodyA, bodyB, 0.0f, manifold ) )
    {
        // Actual contact keeps the proven manifold footprint. Distance GJK is
        // for separated shapes; it is not a penetration-depth algorithm.
        return BuildObjectContactManifold( a, colliderA.shape, b, colliderB.shape, bodyA, bodyB, contactEpsilon, manifold );
    }
    const auto distance = ComputeConvexDistance( a, colliderA.shape, b, colliderB.shape );
    if ( !distance.converged )
    {
        // Fatal invariant: an unresolved distance cannot silently discard a
        // fast articulated candidate and turn numerical failure into tunnelling.
        SB_FATAL( "Physics/ConvexDistance", "Distance did not converge: bodyA=%d bodyB=%d separation=%g iterations=%d.",
                  bodyA, bodyB, distance.separation, distance.iterations );
    }
    if ( distance.separation <= 0.0f )
    {
        return BuildObjectContactManifold( a, colliderA.shape, b, colliderB.shape, bodyA, bodyB, contactEpsilon, manifold );
    }
    const Vector3 linearA = hot.fixed[bodyA] ? ZERO_VECTOR : PhysicsBodyLinearVelocity( hot, bodyA );
    const Vector3 linearB = hot.fixed[bodyB] ? ZERO_VECTOR : PhysicsBodyLinearVelocity( hot, bodyB );
    const Vector3 angularA = hot.fixed[bodyA] ? ZERO_VECTOR : PhysicsBodyAngularVelocity( hot, bodyA );
    const Vector3 angularB = hot.fixed[bodyB] ? ZERO_VECTOR : PhysicsBodyAngularVelocity( hot, bodyB );
    const Vector3 armA = distance.pointA - a.position;
    const Vector3 armB = distance.pointB - b.position;
    const Vector3 velocityA = linearA + Vector::CrossProduct( angularA, armA );
    const Vector3 velocityB = linearB + Vector::CrossProduct( angularB, armB );
    const float closingVelocity = Dot( velocityB - velocityA, distance.normal );
    const float angularBound = AngularReach( colliderA, angularA ) + AngularReach( colliderB, angularB );
    const float conservativeClosing = (std::min)( closingVelocity,
                                                  Dot( linearB - linearA, distance.normal ) - angularBound );
    if ( conservativeClosing >= 0.0f || distance.separation + conservativeClosing * stepDuration >= 0.0f )
    {
        return false;
    }
    if ( angularBound > 0.0f )
    {
        const float reachA = MaximumRotatedProjection( a.orientation, colliderA.shape, angularA, distance.normal,
                                                       stepDuration );
        const float reachB = MaximumRotatedProjection( b.orientation, colliderB.shape, angularB, -distance.normal,
                                                       stepDuration );
        const float relativeTravel = Dot( linearB - linearA, distance.normal ) * stepDuration;
        const float separationBound = Dot( b.position - a.position, distance.normal ) - reachA - reachB +
                                      (std::min)( 0.0f, relativeTravel );
        const float rounding = 32.0f * std::numeric_limits<float>::epsilon() *
                               ( 1.0f + colliderA.maximumCenterOfMassRadius + colliderB.maximumCenterOfMassRadius +
                                 Vector::VectorMag( b.position - a.position ) + std::abs( relativeTravel ) );
        // A separating plane throughout both arcs proves a miss even when
        // instantaneous corner velocities would cross that plane linearly.
        if ( separationBound > rounding )
        {
            return false;
        }
    }
    if ( angularBound > 0.0f && BuildAngularContactPatch( bodies, colliders, stepDuration,
                                                          -conservativeClosing * stepDuration, bodyA, bodyB, manifold ) )
    {
        return true;
    }
    // Why: a closing tangent plane alone may brake a near miss. For translating
    // shapes the existing swept geometry rejects that false plane crossing.
    // Rotation uses conservative reach; it does not claim exact rotational TOI.
    if ( angularBound == 0.0f &&
         !SweepObjectContact( a, colliderA.shape, linearA, b, colliderB.shape, linearB, stepDuration ).hit )
    {
        return false;
    }
    manifold.bodyA = bodyA;
    manifold.bodyB = bodyB;
    manifold.normal = distance.normal;
    manifold.pointCount = 1u;
    manifold.points[0].point = ( distance.pointA + distance.pointB ) * 0.5f;
    manifold.points[0].rA = armA;
    manifold.points[0].rB = armB;
    manifold.points[0].penetration = -distance.separation;
    manifold.points[0].signedSeparation = distance.separation;
    manifold.points[0].featureId = distance.featureId;
    return true;
}

} // namespace SkullbonezCore::Physics

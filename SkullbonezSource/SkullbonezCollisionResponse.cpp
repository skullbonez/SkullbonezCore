// --- Includes ---
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezVector3.h"
#include "SkullbonezCollisionShape.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::CollisionDetection;


// --- Physics logging state ---
static FILE* s_physicsLog = nullptr;
static int s_physicsFrame = 0;


void CollisionResponse::SetPhysicsLog( FILE* file )
{
    s_physicsLog = file;
}


void CollisionResponse::SetPhysicsFrame( int frame )
{
    s_physicsFrame = frame;
}


void CollisionResponse::RespondCollisionTerrain( GameModel& gameModel, float changeInTime )
{
    std::visit( [&]( const auto& shape )
                {
        using ShapeT = std::decay_t<decltype( shape )>;

        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            Vector3 normal = gameModel.m_responseInformation.collidedPlane.m_normal;
            float radius = shape.GetRadius();
            float mass = gameModel.m_physicsInfo.GetMass();
            float invMass = gameModel.m_physicsInfo.GetInvertedMass();
            Vector3 inertia = gameModel.m_physicsInfo.GetRotationalInertia();
            Vector3 velocity = gameModel.m_physicsInfo.GetVelocity();
            Vector3 omega = gameModel.m_physicsInfo.GetAngularVelocity();

            // Log pre-collision state
            Vector3 velBefore = velocity;
            Vector3 omegaBefore = omega;

            // Contact point: center to bottom of sphere along surface normal
            Vector3 rContact = normal * ( -radius );

            // Velocity at the contact point: v_contact = v + omega x r
            Vector3 vContact = velocity + Vector::CrossProduct( omega, rContact );

            // Normal component of contact velocity (negative = moving into surface)
            float vn = vContact * normal;

            // Only resolve if moving into the surface
            if ( vn >= 0.0f )
            {
                return;
            }

            // --- Normal impulse with velocity-dependent restitution ---
            float e = gameModel.m_physicsInfo.GetCoefficientRestitution();
            if ( fabsf( vn ) < Cfg().contactRestitutionThreshold )
            {
                e = 0.0f;
            }

            // Effective mass for normal impulse at the contact point:
            // 1/m_eff = 1/m + n . ((r x n) / I) x r
            Vector3 rCrossN = Vector::CrossProduct( rContact, normal );
            Vector3 iInvRCrossN = rCrossN / inertia;
            Vector3 iInvRCrossNCrossR = Vector::CrossProduct( iInvRCrossN, rContact );
            float kNormal = invMass + ( normal * iInvRCrossNCrossR );

            float jn = -( 1.0f + e ) * vn / kNormal;

            // Apply normal impulse to linear and angular velocity
            velocity += normal * ( jn * invMass );
            omega += Vector::CrossProduct( rContact, normal * jn ) / inertia;

            // --- Friction impulse (Coulomb model) ---
            // Recompute contact velocity after normal impulse
            vContact = velocity + Vector::CrossProduct( omega, rContact );
            Vector3 vTangent = vContact - normal * ( vContact * normal );
            float vTangentMag = Vector::VectorMag( vTangent );

            if ( vTangentMag > TOLERANCE )
            {
                Vector3 tangentDir = vTangent / vTangentMag;

                // Effective mass for friction direction at contact point
                Vector3 rCrossT = Vector::CrossProduct( rContact, tangentDir );
                Vector3 iInvRCrossT = rCrossT / inertia;
                Vector3 iInvRCrossTCrossR = Vector::CrossProduct( iInvRCrossT, rContact );
                float kTangent = invMass + ( tangentDir * iInvRCrossTCrossR );

                // Impulse needed to stop tangential sliding
                float jt = vTangentMag / kTangent;

                // Clamp to Coulomb friction cone
                float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();
                float maxFriction = mu * jn;
                if ( jt > maxFriction )
                {
                    jt = maxFriction;
                }

                // Apply friction impulse (opposes tangential sliding)
                Vector3 frictionImpulse = tangentDir * ( -jt );
                velocity += frictionImpulse * invMass;
                omega += Vector::CrossProduct( rContact, frictionImpulse ) / inertia;
            }

            // --- Spin damping: damp world-Y (top-spin) angular velocity ---
            // Coulomb friction is blind to omega.y because (omega×rContact).y == 0
            // for flat terrain. The previous normal-projected approach also removed
            // omega.z on steep slopes, which negated rolling omega.
            // Damping only omega.y, weighted by normal.y², leaves rolling intact
            // and fades naturally to zero on steep contacts.
            if ( fabsf( omega.y ) > TOLERANCE )
            {
                float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();
                float maxYDelta = mu * jn * radius * normal.y * normal.y / inertia.y;
                float yCorrection = fabsf( omega.y ) > maxYDelta
                                        ? maxYDelta * ( omega.y > 0.0f ? 1.0f : -1.0f )
                                        : omega.y;
                omega.y -= yCorrection;
            }

            // --- Rolling alignment: snap omega to pure-rolling on ground contact ---
            // Pure rolling for velocity v on surface with normal n:
            //   omega = (n × v) / r
            // This forces the spin axis to match the travel direction immediately,
            // eliminating sideways spin artifacts from bounces/sphere-sphere.
            float tangentSpeed = Vector::VectorMag( velocity - normal * ( velocity * normal ) );
            if ( tangentSpeed > TOLERANCE )
            {
                Vector3 omegaTarget = Vector::CrossProduct( normal, velocity ) / radius;
                omega = omegaTarget;
            }

            // --- Rolling friction (small constant torque opposing spin) ---
            float normalForce = mass * fabsf( Cfg().gravity ) * normal.y;
            float rollingDecel = Cfg().rollingFrictionCoeff * normalForce / mass;
            float speed = Vector::VectorMag( velocity - normal * ( velocity * normal ) );
            if ( speed > TOLERANCE )
            {
                Vector3 moveDir = velocity - normal * ( velocity * normal );
                moveDir.Normalise();
                float deltaV = rollingDecel * changeInTime;
                if ( deltaV > speed )
                {
                    deltaV = speed;
                }
                velocity -= moveDir * deltaV;
            }

            gameModel.m_physicsInfo.SetLinearVelocity( velocity );
            gameModel.m_physicsInfo.SetAngularVelocity( omega );

            // Log post-collision state
            if ( s_physicsLog )
            {
                Vector3 pos = gameModel.m_physicsInfo.GetPosition();
                fprintf( s_physicsLog, "terrain,%d,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", s_physicsFrame, pos.x, pos.y, pos.z, velBefore.x, velBefore.y, velBefore.z, omegaBefore.x, omegaBefore.y, omegaBefore.z, velocity.x, velocity.y, velocity.z, omega.x, omega.y, omega.z );
            }
        } },
                gameModel.m_boundingVolume );
}


void CollisionResponse::RespondCollisionGameModels( GameModel& gameModel1,
                                                    GameModel& gameModel2 )
{
    std::visit( [&]( const auto& shape1, const auto& shape2 )
                {
        using Shape1T = std::decay_t<decltype( shape1 )>;
        using Shape2T = std::decay_t<decltype( shape2 )>;

        if constexpr ( std::is_same_v<Shape1T, BoundingSphere> && std::is_same_v<Shape2T, BoundingSphere> )
        {
            // calculate the collision m_normal once and once only
            Vector3 collisionNormal =
                CollisionResponse::GetCollisionNormalSphereVsSphere( gameModel1,
                                                                     gameModel2 );

            // apply the response forces to the models - angular first
            CollisionResponse::SphereVsSphereAngular( gameModel1, gameModel2, collisionNormal );

            // then linear
            CollisionResponse::SphereVsSphereLinear( gameModel1, gameModel2, collisionNormal );

            // apply the change in angular velocities now the linear reactions
            // have taken place
            gameModel1.m_physicsInfo.ApplyChangeInAngularVelocity();
            gameModel2.m_physicsInfo.ApplyChangeInAngularVelocity();

            // positional correction: push overlapping spheres apart to prevent penetration accumulation
            Vector3 pos1 = gameModel1.m_physicsInfo.GetPosition();
            Vector3 pos2 = gameModel2.m_physicsInfo.GetPosition();
            float r1 = shape1.GetRadius();
            float r2 = shape2.GetRadius();
            Vector3 delta = pos2 - pos1;
            float dist = Vector::VectorMag( delta );
            float overlap = ( r1 + r2 ) - dist;
            if ( overlap > 0.0f && dist > 0.0f )
            {
                Vector3 axis = delta / dist;
                float halfOverlap = overlap * 0.5f;
                gameModel1.m_physicsInfo.SetPosition( pos1 - axis * halfOverlap );
                gameModel2.m_physicsInfo.SetPosition( pos2 + axis * halfOverlap );
            }
        } },
                gameModel1.m_boundingVolume,
                gameModel2.m_boundingVolume );
}


void CollisionResponse::SphereVsSphereLinear( GameModel& gameModel1,
                                              GameModel& gameModel2,
                                              const Vector3& collisionNormal )
{
    // calculate the total velocity of the gameModel1
    Vector3 gameModel1TotalVelocity = gameModel1.m_physicsInfo.GetVelocity();

    // calculate the total velocity of the gameModel2
    Vector3 gameModel2TotalVelocity = gameModel2.m_physicsInfo.GetVelocity();

    /*
        ------------------------------------------------------
        FINAL VELOCITY EQN:
        ------------------------------------------------------

                    (m1-em2)v1i + (1+e)m2v2i
        v1f   =   -----------------------------
                             m1 + m2

                    (m2-em1)v2i + (1+e)m1v1i
        v2f   =   -----------------------------
                             m1 + m2

        ------------------------------------------------------
        WHERE:
        ------------------------------------------------------
        m1 and m2 are the masses of the objects
        v1i and v2i are the initial velocities of the objects
        v1f and v2f are the final velocities of the objects
        e is the average coefficient of restitution
    */

    // compute the projection of the velocities in the direction perpendicular to the collision
    float gameModel1ProjectedVelocity = gameModel1TotalVelocity * collisionNormal;
    float gameModel2ProjectedVelocity = gameModel2TotalVelocity * collisionNormal;

    // find the average coefficient of restitution
    float averageBounciness = ( gameModel1.m_physicsInfo.GetCoefficientRestitution() + gameModel2.m_physicsInfo.GetCoefficientRestitution() ) / 2;

    // calculate the final velocity of the gameModel1
    float gameModel1FinalVelocity = ( ( ( gameModel1.m_physicsInfo.GetMass() - ( averageBounciness * gameModel2.m_physicsInfo.GetMass() ) ) * gameModel1ProjectedVelocity ) +
                                      ( ( 1 + averageBounciness ) * gameModel2.m_physicsInfo.GetMass() * gameModel2ProjectedVelocity ) ) /
                                    ( gameModel1.m_physicsInfo.GetMass() + gameModel2.m_physicsInfo.GetMass() );

    // calculate the final velocity of the gameModel2
    float gameModel2FinalVelocity = ( ( ( gameModel2.m_physicsInfo.GetMass() - ( averageBounciness * gameModel1.m_physicsInfo.GetMass() ) ) * gameModel2ProjectedVelocity ) +
                                      ( ( 1 + averageBounciness ) * gameModel1.m_physicsInfo.GetMass() * gameModel1ProjectedVelocity ) ) /
                                    ( gameModel1.m_physicsInfo.GetMass() + gameModel2.m_physicsInfo.GetMass() );

    // update the gameModel1 velocity
    gameModel1.m_physicsInfo.SetLinearVelocity( ( gameModel1FinalVelocity - gameModel1ProjectedVelocity ) * collisionNormal + gameModel1.m_physicsInfo.GetVelocity() );

    // update the gameModel2 velocity
    gameModel2.m_physicsInfo.SetLinearVelocity( ( gameModel2FinalVelocity - gameModel2ProjectedVelocity ) * collisionNormal + gameModel2.m_physicsInfo.GetVelocity() );
}


void CollisionResponse::SphereVsSphereAngular( GameModel& gameModel1,
                                               GameModel& gameModel2,
                                               const Vector3& collisionNormal )
{
    // compute the object space points of collision
    Vector3 objectSpaceCollisionPoint1 = collisionNormal * GetShapeBoundingRadius( gameModel1.m_boundingVolume );
    Vector3 objectSpaceCollisionPoint2 = -collisionNormal * GetShapeBoundingRadius( gameModel2.m_boundingVolume );

    // compute the linear velocities of the game models
    Vector3 linearVelocity1 = gameModel1.m_physicsInfo.GetVelocity();
    Vector3 linearVelocity2 = gameModel2.m_physicsInfo.GetVelocity();

    // compute the individual velocity sums for the game models - remember the cross product here gives the 'wrench' that is perpendicular to the angular velocity
    // and the point of the collision
    Vector3 velocitySum1 = linearVelocity1 + ( Vector::CrossProduct( gameModel1.m_physicsInfo.GetAngularVelocity(), objectSpaceCollisionPoint1 ) );
    Vector3 velocitySum2 = linearVelocity2 + ( Vector::CrossProduct( gameModel2.m_physicsInfo.GetAngularVelocity(), objectSpaceCollisionPoint2 ) );

    // compute the relative velocity between the two objects
    Vector3 relativeVelocity = velocitySum2 - velocitySum1;

    // get the scalar magnitude of the relative velocity projected along the m_normal to the collision
    float velocitySumProjectedAlongCollisionNormal = relativeVelocity * collisionNormal;

    // compute the relative linear velocity
    Vector3 relativeLinearVelocity = linearVelocity2 - linearVelocity1;

    // normalise the relative linear velocity
    relativeLinearVelocity.Normalise();

    /*
        Set the points of collision to the normalised relative linear velocity vector - remember - we are dealing with spheres - we must avoid the object space collision
        points from being parallel with the collision m_normal - this will always happen with spheres, so use the normalised relative linear velocity vector as the collision
        point instead for the remainder of the calculations.  This is a little concoction I thought of after comparing the sphere vs sphere case with the sphere vs plane
        case - note how with the sphere vs plane case from [CollisionResponse::SphereVsPlaneAngular], the collision point is based entirely on the objects ray of movement.

        Now, I don't know why this works so well, nor am I going to pretend to, but let me tell you this:  implementing rotational dynamics has been really difficult -
        texts by Conger and Zerbst explain techniques - both containing code samples, both which flat out don't work.  This technique works really well for my spheres,
        it is based on the angular response I have come to understand with my sphere vs plane routine, I am quite happy to use it here.  [SE: 16-08-2005 06:15]
    */
    objectSpaceCollisionPoint1 = objectSpaceCollisionPoint2 = relativeLinearVelocity;

    /*
                                                            Rigid body response formula:

                                                                    -vr(e+1)
                                            Fi = ---------------------------------------------------
                                                   1/m1+1/m2+n.[((r1*n)/I1)*r1]+n.[((r2*n)/I2)*r2]
    */

    // calculate the average coefficient of restitution
    float averageCoefficientRestitution = ( gameModel1.m_physicsInfo.GetCoefficientRestitution() + gameModel2.m_physicsInfo.GetCoefficientRestitution() ) * 0.5f;

    // calculate the numerator just as the formula above states
    float numerator = -velocitySumProjectedAlongCollisionNormal * ( 1.0f + averageCoefficientRestitution );

    // get the perpendicular of the collision point with the m_normal to the collision
    Vector3 collisionPointCrossPlaneNormal1 = Vector::CrossProduct( objectSpaceCollisionPoint1, collisionNormal );
    Vector3 collisionPointCrossPlaneNormal2 = Vector::CrossProduct( objectSpaceCollisionPoint2, collisionNormal );

    // multiply the inertia tensor from the game model with the perpendicular of the collsiion point and collision m_normal
    Vector3 scaledRotationalInertia1 = Vector::VectorMultiply( gameModel1.m_physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal1 );
    Vector3 scaledRotationalInertia2 = Vector::VectorMultiply( gameModel2.m_physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal2 );

    // take the perpendicular to the scaled inertia tensor and the point of collision
    Vector3 scaledRotationalInertiaCrossCollisionPoint1 = Vector::CrossProduct( scaledRotationalInertia1, objectSpaceCollisionPoint1 );
    Vector3 scaledRotationalInertiaCrossCollisionPoint2 = Vector::CrossProduct( scaledRotationalInertia2, objectSpaceCollisionPoint2 );

    // project the collsiion m_normal along the inertia tensor/collision point perpendicular
    float collisionNormalDotInertiaCollisionPointPerp1 = collisionNormal * scaledRotationalInertiaCrossCollisionPoint1;
    float collisionNormalDotInertiaCollisionPointPerp2 = collisionNormal * scaledRotationalInertiaCrossCollisionPoint2;

    // finally compute the denominator to the equation
    float denominator = gameModel1.m_physicsInfo.GetInvertedMass() +
                        gameModel2.m_physicsInfo.GetInvertedMass() +
                        collisionNormalDotInertiaCollisionPointPerp1 +
                        collisionNormalDotInertiaCollisionPointPerp2;

    // compute the equation result
    float equationResult = numerator / denominator;

    // the force will be in the direction of the collision m_normal (respective to each obect), we now scale it to the magnitude of the equation result
    Vector3 impulseForce = collisionNormal * equationResult;

    // convert the impulse force to a change in angular velocity via taking the perpendicular with the point of collision - keep in mind rotational inertia
    // has already been taken into account here (so no need to compute angular acceleration via m_torque/inertiaTensor eqn)
    Vector3 changeInAngularVelocity1 = Vector::CrossProduct( objectSpaceCollisionPoint1, impulseForce );
    Vector3 changeInAngularVelocity2 = Vector::CrossProduct( objectSpaceCollisionPoint2, -impulseForce );

    // Negate: this function was calibrated for the old visual convention where
    // positive omega displayed as forward spin.  The engine now uses physical
    // convention (omega negated at display time), so we flip the sign here to
    // keep sphere-sphere angular response consistent with the terrain solver.
    gameModel1.m_physicsInfo.SetChangeInAngularVelocity( -changeInAngularVelocity1 );
    gameModel2.m_physicsInfo.SetChangeInAngularVelocity( -changeInAngularVelocity2 );

    // Log sphere-sphere angular changes
    if ( s_physicsLog )
    {
        Vector3 pos1 = gameModel1.m_physicsInfo.GetPosition();
        Vector3 pos2 = gameModel2.m_physicsInfo.GetPosition();
        fprintf( s_physicsLog, "sphere,%d,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,0,0,0\n", s_physicsFrame, pos1.x, pos1.y, pos1.z, -changeInAngularVelocity1.x, -changeInAngularVelocity1.y, -changeInAngularVelocity1.z, pos2.x, pos2.y, pos2.z, -changeInAngularVelocity2.x, -changeInAngularVelocity2.y, -changeInAngularVelocity2.z );
    }
}


Ray CollisionResponse::CalculateRay( GameModel& gameModel,
                                     float changeInTime )
{
    // return the ray of the gameModel
    return Ray( gameModel.m_physicsInfo.GetPosition(), gameModel.m_physicsInfo.GetVelocity() * changeInTime );
}


Vector3 CollisionResponse::GetCollidedObjectWorldPosition( GameModel& gameModel )
{
    return gameModel.m_physicsInfo.GetPosition() + ( gameModel.m_physicsInfo.GetOrientationMatrix() * GetShapePosition( gameModel.m_boundingVolume ) );
}


Vector3 CollisionResponse::GetCollisionNormalSphereVsSphere( GameModel& gameModel1,
                                                             GameModel& gameModel2 )
{
    // compute the world coordinates of the collided dynamics objects we are dealing with
    Vector3 boundingVolumeWorldPosition1 = CollisionResponse::GetCollidedObjectWorldPosition( gameModel1 );
    Vector3 boundingVolumeWorldPosition2 = CollisionResponse::GetCollidedObjectWorldPosition( gameModel2 );

    // take the vector between the point of collision
    Vector3 collisionNormal = boundingVolumeWorldPosition2 - boundingVolumeWorldPosition1;

    // normalise this vector
    collisionNormal.Normalise();

    // return the result
    return collisionNormal;
}

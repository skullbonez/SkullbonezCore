// =============================================================================
// COLLISION RESPONSE (SkullbonezCollisionResponse.cpp)
// =============================================================================
//
// PURPOSE: Calculate and apply physics impulses when objects collide.
// Handles sphere-vs-terrain and sphere-vs-sphere collisions.
//
// --- What is an Impulse? ---
//
//  An impulse is an instantaneous change in momentum (mass × velocity).
//  Unlike continuous forces (gravity), impulses happen in a single instant
//  at the moment of collision. They change velocity directly:
//
//  Δv = impulse / mass
//
// --- Collision Response Pipeline ---
//
//  1. DETECT collision (handled elsewhere — bounding sphere overlap)
//  2. COMPUTE collision normal (direction of impact)
//  3. COMPUTE normal impulse (how hard to push objects apart)
//  4. COMPUTE friction impulse (how much to slow sliding/spinning)
//  5. APPLY velocity changes to both objects
//
// --- Key Physics Concepts ---
//
//  Coefficient of Restitution (e):
//  - e = 1.0 → perfectly elastic (no energy lost, ball bounces to same height)
//  - e = 0.0 → perfectly inelastic (all kinetic energy absorbed, ball doesn't bounce)
//  - Typical: 0.5-0.9 for bouncy balls
//
//  Coulomb Friction Model:
//  - Friction force ≤ μ × normal force (friction coefficient × how hard surfaces press)
//  - Below threshold: static friction (objects stick)
//  - Above threshold: dynamic friction (objects slide)
//
//  Contact Velocity:
//  - v_contact = v_linear + (ω × r)
//  - The velocity at the contact point includes both translation AND rotation
//  - A spinning ball touching a surface has velocity at the contact even if its
//    center isn't moving!
//
// =============================================================================


// --- Includes ---
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezVector3.h"
#include "SkullbonezCollisionShape.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::CollisionDetection;


// =============================================================================
// SPHERE-TERRAIN COLLISION RESPONSE
// =============================================================================
//
// Called when a sphere is detected overlapping with the terrain surface.
// This is the most complex collision handler because it implements:
//
//  1. Normal impulse (bounce off surface)
//  2. Friction impulse (Coulomb model — resist sliding)
//  3. Spin damping (drill friction — resist spinning like a top)
//  4. Rolling friction (small resistance to rolling motion)
//  5. No-slip constraint (enforce v = ω × r at contact)
//  6. Pole alignment (stabilize visual orientation)
//
// --- Normal Impulse Formula ---
//
//  The impulse magnitude along the collision normal:
//
//           -(1 + e) × v_n
//  j_n = ───────────────────────
//         1/m + n · ((r×n)/I) × r
//
//  Where:
//    e = restitution (bounciness)
//    v_n = velocity at contact projected onto normal (how fast approaching)
//    m = mass
//    r = vector from center to contact point
//    I = rotational inertia
//    n = collision normal
//
//  The denominator accounts for the object's resistance to both linear
//  AND angular acceleration at the contact point.
//
// =============================================================================
void CollisionResponse::RespondCollisionTerrain( GameModel& gameModel, float changeInTime )
{
    std::visit( [&]( const auto& shape )
                {
        using ShapeT = std::decay_t<decltype( shape )>;

        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            Vector3 normal = gameModel.m_responseInformation.collidedPlane.m_normal;
            float radius = shape.GetRadius();
            float mass = gameModel.GetMass();
            float invMass = gameModel.GetInvertedMass();
            Vector3 inertia = gameModel.GetRotationalInertia();
            Vector3 invInertia = gameModel.GetInvertedRotationalInertia();
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
            Vector3 iInvRCrossN = Vector::VectorMultiply( invInertia, rCrossN );
            Vector3 iInvRCrossNCrossR = Vector::CrossProduct( iInvRCrossN, rContact );
            float kNormal = invMass + ( normal * iInvRCrossNCrossR );

            float jn = -( 1.0f + e ) * vn / kNormal;

            // Apply normal impulse to linear and angular velocity
            velocity += normal * ( jn * invMass );
            omega += Vector::VectorMultiply( invInertia, Vector::CrossProduct( rContact, normal * jn ) );

            // --- Friction impulse (Coulomb model) ---
            // Recompute contact velocity after normal impulse
            vContact = velocity + Vector::CrossProduct( omega, rContact );
            float vContactNormal = vContact * normal;
            Vector3 vTangent = vContact - normal * vContactNormal;
            float vTangentSq = vTangent * vTangent;

            if ( vTangentSq > TOLERANCE * TOLERANCE )
            {
                float vTangentMag = sqrtf( vTangentSq );
                Vector3 tangentDir = vTangent / vTangentMag;

                // Effective mass for friction direction at contact point
                Vector3 rCrossT = Vector::CrossProduct( rContact, tangentDir );
                Vector3 iInvRCrossT = Vector::VectorMultiply( invInertia, rCrossT );
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
                omega += Vector::VectorMultiply( invInertia, Vector::CrossProduct( rContact, frictionImpulse ) );
            }

            // --- Spin damping (drill friction): damp spin about contact normal ---
            // On curved terrain, true rolling can require a non-zero world Y component.
            // Damping world omega.y directly destroys that coupling and causes visual
            // orientation drift.  Instead, damp only the scalar spin around the contact
            // normal; keep no-slip rolling in the tangent plane untouched.
            float normalForce = mass * fabsf( Cfg().gravity ) * fabsf( normal.y );
            float inertiaNormal =
                inertia.x * normal.x * normal.x +
                inertia.y * normal.y * normal.y +
                inertia.z * normal.z * normal.z;
            if ( inertiaNormal < TOLERANCE )
            {
                inertiaNormal = inertia.y;
            }
            float spinOmega = omega * normal;
            float spinDecel = Cfg().spinFrictionCoeff * normalForce * radius / inertiaNormal;
            float maxSpinDelta = spinDecel * changeInTime;
            if ( fabsf( spinOmega ) <= maxSpinDelta )
            {
                spinOmega = 0.0f;
            }
            else
            {
                spinOmega -= maxSpinDelta * ( spinOmega > 0.0f ? 1.0f : -1.0f );
            }

            // --- Rolling friction (small constant torque opposing spin) ---
            float rollingDecel = Cfg().rollingFrictionCoeff * normalForce / mass;
            float velocityAlongNormal = velocity * normal;
            Vector3 rollingTangentVelocity = velocity - normal * velocityAlongNormal;
            float rollingTangentSpeedSq = rollingTangentVelocity * rollingTangentVelocity;
            if ( rollingTangentSpeedSq > TOLERANCE * TOLERANCE )
            {
                float rollingTangentSpeed = sqrtf( rollingTangentSpeedSq );
                Vector3 moveDir = rollingTangentVelocity / rollingTangentSpeed;
                float deltaV = rollingDecel * changeInTime;
                if ( deltaV > rollingTangentSpeed )
                {
                    deltaV = rollingTangentSpeed;
                }
                velocity -= moveDir * deltaV;
            }

            // Enforce full no-slip rolling on the tangent plane and preserve only
            // the (damped) drill spin around the contact normal.
            Vector3 rollingOmega = Vector::CrossProduct( velocity, normal ) / radius;
            rollingOmega = rollingOmega * -1.0f;
            omega = rollingOmega + normal * spinOmega;

            // After slip settles, the Pole Vector should rotate about the Roll Axis.
            // Gradually remove any Pole component along omega so Pole -> perpendicular
            // to the red axis while preserving smooth motion.
            // Skip when the ball is in water: it is floating, not rolling on terrain,
            // so orientation correction is meaningless and causes visible snaps.
            bool ballInWater = ( gameModel.m_physicsInfo.GetPosition().y - radius ) < Cfg().fluidHeight;
            float omegaMagSq = omega.x * omega.x +
                               omega.y * omega.y +
                               omega.z * omega.z;
            float velocityAlongNormalPostFriction = velocity * normal;
            Vector3 alignTangentVelocity = velocity - normal * velocityAlongNormalPostFriction;
            float tangentSpeedSq = alignTangentVelocity * alignTangentVelocity;
            float minSpeedSq = Cfg().rollAlignMinSpeed * Cfg().rollAlignMinSpeed;
            float minOmegaSq = Cfg().rollAlignMinOmega * Cfg().rollAlignMinOmega;
            static uint32_t s_rollAlignCounter = 0;
            ++s_rollAlignCounter;
            int alignInterval = Cfg().rollAlignInterval;
            if ( alignInterval < 1 )
            {
                alignInterval = 1;
            }

            bool shouldAlign = Cfg().rollAlignEnabled &&
                               !ballInWater &&
                               omegaMagSq > TOLERANCE * TOLERANCE &&
                               ( tangentSpeedSq > minSpeedSq || omegaMagSq > minOmegaSq ) &&
                               ( alignInterval <= 1 || ( s_rollAlignCounter % static_cast<uint32_t>( alignInterval ) ) == 0 );

            if ( shouldAlign )
            {
                float omegaMag = sqrtf( omegaMagSq );
                Vector3 omegaDir = omega / omegaMag;

                Vector3 pole = gameModel.GetOrientationUp();
                float poleMag = Vector::VectorMag( pole );
                if ( poleMag > TOLERANCE )
                {
                    pole /= poleMag;
                    float poleAlongOmega = pole * omegaDir;

                    Vector3 targetPole = pole - omegaDir * poleAlongOmega;
                    float targetMag = Vector::VectorMag( targetPole );
                    if ( targetMag <= TOLERANCE )
                    {
                        // Degenerate case: pick a stable perpendicular from velocity.
                        targetPole = velocity - omegaDir * ( velocity * omegaDir );
                        targetMag = Vector::VectorMag( targetPole );
                    }

                    if ( targetMag > TOLERANCE )
                    {
                        targetPole /= targetMag;
                        float dotPole = pole * targetPole;
                        if ( dotPole > 1.0f )
                        {
                            dotPole = 1.0f;
                        }
                        else if ( dotPole < -1.0f )
                        {
                            dotPole = -1.0f;
                        }

                        float correctionAngle = acosf( dotPole );
                        float correctionTolerance = Cfg().rollAlignPerpToleranceDeg * _PI / 180.0f;
                        float maxCorrectionAngle = Cfg().rollAlignMaxCorrectionDeg * _PI / 180.0f;

                        if ( correctionAngle > correctionTolerance )
                        {
                            if ( maxCorrectionAngle > 0.0f && correctionAngle > maxCorrectionAngle )
                            {
                                correctionAngle = maxCorrectionAngle;
                            }
                            Vector3 correctionAxis = Vector::CrossProduct( pole, targetPole );
                            float correctionAxisMag = Vector::VectorMag( correctionAxis );
                            if ( correctionAxisMag > TOLERANCE )
                            {
                                correctionAxis /= correctionAxisMag;
                                Quaternion q = gameModel.m_physicsInfo.GetOrientation();
                                q.RotateAboutAxis( correctionAxis, correctionAngle );
                                gameModel.m_physicsInfo.SetOrientation( q );
                            }
                        }
                    }
                }
            }

            gameModel.m_physicsInfo.SetLinearVelocity( velocity );
            gameModel.m_physicsInfo.SetAngularVelocity( omega );
        } },
                gameModel.m_boundingVolume );
}


// =============================================================================
// SPHERE-SPHERE COLLISION RESPONSE
// =============================================================================
//
// Called when two spheres are detected overlapping. Handles both the angular
// (spin transfer) and linear (velocity exchange) components of the collision.
//
// --- Collision Pipeline ---
//
//  1. Compute collision normal (direction from center1 to center2)
//  2. Apply angular response (spin transfer based on contact geometry)
//  3. Apply linear response (velocity exchange using momentum conservation)
//  4. Positional correction (push overlapping spheres apart)
//
// --- Positional Correction ---
//
//  After velocity is resolved, spheres may still be overlapping (penetrating).
//  We push them apart along the collision normal by half the overlap distance
//  each. This prevents penetration from accumulating over multiple frames.
//
// =============================================================================
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


// =============================================================================
// SPHERE-SPHERE LINEAR VELOCITY EXCHANGE
// =============================================================================
//
// Uses the 1D elastic collision formula projected along the collision normal.
// This is conservation of momentum + conservation of kinetic energy combined:
//
// --- The Formula ---
//
//         (m1 - e·m2)·v1i + (1+e)·m2·v2i
//  v1f = ──────────────────────────────────
//                   m1 + m2
//
//         (m2 - e·m1)·v2i + (1+e)·m1·v1i
//  v2f = ──────────────────────────────────
//                   m1 + m2
//
// Where:
//  m1, m2 = masses of the two objects
//  v1i, v2i = initial velocities (projected along collision normal)
//  v1f, v2f = final velocities
//  e = average coefficient of restitution (bounciness)
//
// Special cases:
//  - Equal masses, e=1: objects exchange velocities (billiard balls)
//  - One mass infinite, e=1: ball bounces off wall at same speed
//  - e=0: objects stick together (perfectly inelastic)
//
// The formula is applied only to the NORMAL component of velocity.
// The TANGENTIAL component (perpendicular to collision) is unchanged.
//
// =============================================================================
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
    float gameModel1Mass = gameModel1.GetMass();
    float gameModel2Mass = gameModel2.GetMass();
    float totalMass = gameModel1Mass + gameModel2Mass;

    // calculate the final velocity of the gameModel1
    float gameModel1FinalVelocity = ( ( ( gameModel1Mass - ( averageBounciness * gameModel2Mass ) ) * gameModel1ProjectedVelocity ) +
                                      ( ( 1 + averageBounciness ) * gameModel2Mass * gameModel2ProjectedVelocity ) ) /
                                    totalMass;

    // calculate the final velocity of the gameModel2
    float gameModel2FinalVelocity = ( ( ( gameModel2Mass - ( averageBounciness * gameModel1Mass ) ) * gameModel2ProjectedVelocity ) +
                                      ( ( 1 + averageBounciness ) * gameModel1Mass * gameModel1ProjectedVelocity ) ) /
                                    totalMass;

    // update the gameModel1 velocity
    gameModel1.m_physicsInfo.SetLinearVelocity( ( gameModel1FinalVelocity - gameModel1ProjectedVelocity ) * collisionNormal + gameModel1.m_physicsInfo.GetVelocity() );

    // update the gameModel2 velocity
    gameModel2.m_physicsInfo.SetLinearVelocity( ( gameModel2FinalVelocity - gameModel2ProjectedVelocity ) * collisionNormal + gameModel2.m_physicsInfo.GetVelocity() );
}


// =============================================================================
// SPHERE-SPHERE ANGULAR IMPULSE (Spin Transfer)
// =============================================================================
//
// Computes how much each sphere should start spinning after the collision.
// Uses the rigid body angular impulse formula:
//
//                       -v_r × (e+1)
//  F_impulse = ─────────────────────────────────────────────────
//               1/m1 + 1/m2 + n·((r1×n)/I1)×r1 + n·((r2×n)/I2)×r2
//
// Where:
//  v_r = relative velocity at contact (includes both linear and angular contributions)
//  e = average restitution
//  m1, m2 = masses
//  r1, r2 = contact points (object space)
//  I1, I2 = rotational inertias
//  n = collision normal
//
// The denominator represents the EFFECTIVE MASS at the contact point,
// accounting for both objects' resistance to linear AND rotational acceleration.
//
// The resulting impulse is then converted to angular velocity change via:
//  Δω = (r × F_impulse) / I
//
// =============================================================================
void CollisionResponse::SphereVsSphereAngular( GameModel& gameModel1,
                                               GameModel& gameModel2,
                                               const Vector3& collisionNormal )
{
    // compute the object space points of collision
    Vector3 objectSpaceCollisionPoint1 = collisionNormal * gameModel1.GetBoundingRadius();
    Vector3 objectSpaceCollisionPoint2 = -collisionNormal * gameModel2.GetBoundingRadius();

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
    Vector3 scaledRotationalInertia1 = Vector::VectorMultiply( gameModel1.GetRotationalInertia(), collisionPointCrossPlaneNormal1 );
    Vector3 scaledRotationalInertia2 = Vector::VectorMultiply( gameModel2.GetRotationalInertia(), collisionPointCrossPlaneNormal2 );

    // take the perpendicular to the scaled inertia tensor and the point of collision
    Vector3 scaledRotationalInertiaCrossCollisionPoint1 = Vector::CrossProduct( scaledRotationalInertia1, objectSpaceCollisionPoint1 );
    Vector3 scaledRotationalInertiaCrossCollisionPoint2 = Vector::CrossProduct( scaledRotationalInertia2, objectSpaceCollisionPoint2 );

    // project the collsiion m_normal along the inertia tensor/collision point perpendicular
    float collisionNormalDotInertiaCollisionPointPerp1 = collisionNormal * scaledRotationalInertiaCrossCollisionPoint1;
    float collisionNormalDotInertiaCollisionPointPerp2 = collisionNormal * scaledRotationalInertiaCrossCollisionPoint2;

    // finally compute the denominator to the equation
    float denominator = gameModel1.GetInvertedMass() +
                        gameModel2.GetInvertedMass() +
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

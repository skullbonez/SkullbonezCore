// =============================================================================
// IMPULSE SOLVER (SkullbonezImpulseSolver.cpp)
// =============================================================================
//
// PURPOSE: Unified sequential impulse solver for terrain and object-object
//          collisions.  Handles both spheres and oriented boxes.
//
// --- Algorithm (Erin Catto / Box2D / Bullet) ---
//
//  1. Build contact manifold:
//       Sphere  → 1 contact at the bottom pole (center - radius * normal)
//       Box     → up to 8 vertex contacts, filtered by proximity to deepest vertex
//  2. Pre-compute per-contact effective mass (K = 1/m + n·((I⁻¹(r×n))×r))
//  3. Compute Baumgarte bias for resting contacts (penetration correction)
//  4. Iteratively solve normal + two friction constraints (20 iterations)
//       Normal:  accumulated impulse clamped ≥ 0 (push only, not pull)
//       Friction: Coulomb cone |λ_t| ≤ μ·λ_n with gravity-floor warm-start
//  5. Direct position correction (project out remaining penetration × 0.4)
//  6. Gravitational tipping torque for boxes on edge/vertex contacts
//  7. Rolling friction torque (opposes ω, decays to rest)
//  8. Sleep (zero velocity when both linear and angular fall below threshold)
//  9. Visual pole alignment (spheres only, cosmetic)
//
// --- World-Space Inertia for Boxes ---
//
//  I_world_inv * v = R · (I_body_inv ∘ (Rᵀ · v))
//
//  Where R is the current orientation matrix.  Spheres use body-space (isotropic).
//
// --- References ---
//  Erin Catto, "Iterative Dynamics with Temporal Coherence", GDC 2005
//    https://box2d.org/files/ErinCatto_IterativeDynamics_GDC2005.pdf
//  Bullet Physics — btSequentialImpulseConstraintSolver
//  Box2D — b2ContactSolver
//
// =============================================================================


// --- Includes ---
#include "SkullbonezImpulseSolver.h"
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezVector3.h"
#include "SkullbonezCollisionShape.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::CollisionDetection;


// --- Static flag ---
bool ImpulseSolver::s_legacyPhysics = false;


// =============================================================================
// TERRAIN COLLISION RESPONSE — Unified Sequential Impulse Solver
// =============================================================================
void ImpulseSolver::RespondCollisionTerrain( GameModel& gameModel, float changeInTime )
{
    // --- Common state ---
    float invMass = gameModel.GetInvertedMass();
    Vector3 invInertia = gameModel.GetInvertedRotationalInertia();
    Vector3 inertia = gameModel.GetRotationalInertia();
    Vector3 velocity = gameModel.m_physicsInfo.GetVelocity();
    Vector3 omega = gameModel.m_physicsInfo.GetAngularVelocity();
    float e = gameModel.m_physicsInfo.GetCoefficientRestitution();
    float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();
    Vector3 position = gameModel.m_physicsInfo.GetPosition();
    float mass = gameModel.GetMass();

    // --- World-space inverse inertia ---
    // For spheres (isotropic inertia), body-space = world-space.
    // For boxes, we must transform: I_world_inv * v = R * (I_body_inv ∘ (R^T * v))
    // where R is the orientation matrix and ∘ is component-wise multiply.
    Transformation::RotationMatrix orientMat = gameModel.m_physicsInfo.GetOrientationMatrix();
    bool useWorldInertia = std::visit( []( const auto& s ) -> bool
    {
        using S = std::decay_t<decltype( s )>;
        return std::is_same_v<S, BoundingBox>;
    }, gameModel.m_boundingVolume );

    // Lambda: compute I_world_inv * v
    auto applyInvInertia = [&]( const Vector3& v ) -> Vector3
    {
        if ( !useWorldInertia )
        {
            return Vector::VectorMultiply( invInertia, v );
        }
        // Rotate v into body space, scale by body-space invI, rotate back
        Vector3 bodyV = orientMat.TransposeMultiply( v );
        Vector3 scaled = Vector::VectorMultiply( invInertia, bodyV );
        return orientMat * scaled;
    };

    // Collision plane from the detection pass - guarantees consistency
    // between "did we collide?" and "where are the contacts?"
    Geometry::Plane colPlane = gameModel.m_responseInformation.collidedPlane;
    Vector3 planeNormal = colPlane.m_normal;

    // --- Contact manifold ---
    struct Contact
    {
        Vector3 r;
        Vector3 normal;
        float   penetration;
        float   normalMass;
        float   tangentMass1;
        float   tangentMass2;
        Vector3 tangent1;
        Vector3 tangent2;
        float   bias;
        float   accN;
        float   accT1;
        float   accT2;
    };

    Contact contacts[8];
    int contactCount = 0;

    // --- Build contacts based on shape type ---
    std::visit( [&]( const auto& shape )
    {
        using ShapeT = std::decay_t<decltype( shape )>;

        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            // Sphere: single contact at the bottom pole
            float radius = shape.GetRadius();
            Vector3 contactWorldPos = position - planeNormal * radius;

            // Signed distance from contact point to plane (negative = penetrating)
            // Plane convention: dot(point, normal) = m_distance for points ON the plane
            float signedDist = ( contactWorldPos * planeNormal ) - colPlane.m_distance;
            float penetration = -signedDist;

            Contact& c = contacts[0];
            c.r = contactWorldPos - position;
            c.normal = planeNormal;
            c.penetration = penetration;
            c.accN = 0.0f;
            c.accT1 = 0.0f;
            c.accT2 = 0.0f;
            contactCount = 1;
        }
        else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
        {
            // Box: check all 8 vertices against the collision plane.
            // Use two-pass approach: first find the minimum signed distance,
            // then include all vertices within a relative threshold of the min.
            // This ensures at least 1 contact even on slopes where absolute
            // distances vary widely across vertices.
            const Vector3& he = shape.GetHalfExtents();
            Transformation::RotationMatrix rotMat = gameModel.m_physicsInfo.GetOrientationMatrix();

            Vector3 worldVerts[8];
            float signedDists[8];
            float minSignedDist = 1e10f;

            for ( int v = 0; v < 8; ++v )
            {
                Vector3 local(
                    ( v & 1 ) ? he.x : -he.x,
                    ( v & 2 ) ? he.y : -he.y,
                    ( v & 4 ) ? he.z : -he.z );
                worldVerts[v] = position + ( rotMat * local );
                signedDists[v] = ( worldVerts[v] * planeNormal ) - colPlane.m_distance;
                if ( signedDists[v] < minSignedDist )
                {
                    minSignedDist = signedDists[v];
                }
            }

            // Include all vertices within contactThreshold of the deepest penetration.
            // A small threshold gives edge (2 verts) or vertex (1) contacts for
            // tilted boxes, and face (4) contacts for flush boxes.
            constexpr float contactThreshold = 0.15f;
            float cutoff = minSignedDist + contactThreshold;

            for ( int v = 0; v < 8; ++v )
            {
                if ( signedDists[v] > cutoff )
                {
                    continue;
                }

                float penetration = -signedDists[v];
                Contact& c = contacts[contactCount];
                c.r = worldVerts[v] - position;
                c.normal = planeNormal;
                c.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
                c.accN = 0.0f;
                c.accT1 = 0.0f;
                c.accT2 = 0.0f;
                ++contactCount;
            }
        }
    }, gameModel.m_boundingVolume );

    // Safety fallback: cancel normal velocity if no contacts found
    if ( contactCount == 0 )
    {
        float vn = velocity * planeNormal;
        if ( vn < 0.0f )
        {
            velocity -= planeNormal * vn;
        }
        gameModel.m_physicsInfo.SetLinearVelocity( velocity );
        return;
    }

    // --- Collapse manifold to centroid for high-velocity impacts ---
    // With multiple contacts, per-contact impulses create angular feedback
    // that can amplify beyond physical bounds. Using a single centroid contact
    // for impacts avoids this; full manifold used for resting/low-velocity.
    float preVn = velocity * planeNormal;
    if ( preVn < -Cfg().contactRestitutionThreshold && contactCount > 1 )
    {
        Vector3 centroidR = Math::Vector::ZERO_VECTOR;
        float avgPen = 0.0f;
        for ( int i = 0; i < contactCount; ++i )
        {
            centroidR += contacts[i].r;
            avgPen += contacts[i].penetration;
        }
        float invN = 1.0f / static_cast<float>( contactCount );
        contacts[0].r = centroidR * invN;
        contacts[0].penetration = avgPen * invN;
        contactCount = 1;
    }

    // --- Gravity-based friction floor ---
    // For resting contacts sliding tangentially, the solver's accN can be
    // near-zero. But static friction still acts proportional to the normal
    // reaction force (mg·cos(θ)). We compute the per-contact share of the
    // expected gravity load and use it as a minimum friction budget.
    float gravityNormalImpulse = mass * fabsf( Cfg().gravity ) *
                                  fabsf( planeNormal.y ) * changeInTime;
    float warmStartPerContact = gravityNormalImpulse /
                                 static_cast<float>( contactCount );

    // --- Pre-compute per-contact effective mass and bias ---
    // Baumgarte stabilization is used ONLY for resting contacts (low v_n) with
    // a hard cap on maximum bias to prevent launching objects on impacts.
    constexpr float penetrationSlop = 0.005f;
    constexpr float baumgarteBeta = 0.3f;
    constexpr float maxBaumgarteBias = 2.0f;
    float invDt = ( changeInTime > TOLERANCE ) ? ( 1.0f / changeInTime ) : 120.0f;

    for ( int i = 0; i < contactCount; ++i )
    {
        Contact& c = contacts[i];

        // Build orthonormal tangent frame for friction (Gram-Schmidt)
        if ( fabsf( c.normal.x ) > 0.9f )
        {
            c.tangent1 = Vector3( 0.0f, 0.0f, 1.0f );
        }
        else
        {
            c.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        }
        float d = c.tangent1 * c.normal;
        c.tangent1 = c.tangent1 - c.normal * d;
        float t1Mag = VectorMag( c.tangent1 );
        if ( t1Mag > TOLERANCE )
        {
            c.tangent1 = c.tangent1 / t1Mag;
        }
        c.tangent2 = Vector::CrossProduct( c.normal, c.tangent1 );

        // Normal effective mass: K = 1/m + n . ((I^-1 * (r x n)) x r)
        Vector3 rCrossN = Vector::CrossProduct( c.r, c.normal );
        Vector3 iRCrossN = applyInvInertia( rCrossN );
        float kN = invMass + ( c.normal * Vector::CrossProduct( iRCrossN, c.r ) );
        c.normalMass = ( kN > TOLERANCE ) ? ( 1.0f / kN ) : 0.0f;

        // Tangent effective masses
        Vector3 rCrossT1 = Vector::CrossProduct( c.r, c.tangent1 );
        Vector3 iRCrossT1 = applyInvInertia( rCrossT1 );
        float kT1 = invMass + ( c.tangent1 * Vector::CrossProduct( iRCrossT1, c.r ) );
        c.tangentMass1 = ( kT1 > TOLERANCE ) ? ( 1.0f / kT1 ) : 0.0f;

        Vector3 rCrossT2 = Vector::CrossProduct( c.r, c.tangent2 );
        Vector3 iRCrossT2 = applyInvInertia( rCrossT2 );
        float kT2 = invMass + ( c.tangent2 * Vector::CrossProduct( iRCrossT2, c.r ) );
        c.tangentMass2 = ( kT2 > TOLERANCE ) ? ( 1.0f / kT2 ) : 0.0f;

        // Bias: Baumgarte only for resting contacts (|v_n| below restitution threshold)
        // with a hard cap to prevent launching on slopes where vertex pen can be large.
        Vector3 vAtContact = velocity + Vector::CrossProduct( omega, c.r );
        float vnContact = vAtContact * c.normal;

        c.bias = 0.0f;
        if ( fabsf( vnContact ) < Cfg().contactRestitutionThreshold )
        {
            // Resting contact: gentle Baumgarte to maintain surface contact
            float corrPen = c.penetration - penetrationSlop;
            if ( corrPen < 0.0f ) corrPen = 0.0f;
            float baumBias = baumgarteBeta * corrPen * invDt;
            if ( baumBias > maxBaumgarteBias ) baumBias = maxBaumgarteBias;
            c.bias = baumBias;
        }
        else if ( vnContact < -Cfg().contactRestitutionThreshold )
        {
            // Impact: restitution only, no Baumgarte (position correction handles pen)
            c.bias = ( -e * vnContact ) / static_cast<float>( contactCount );
        }
    }

    // --- Sequential Impulse Solver (20 iterations) ---
    // Warm-start accN with expected gravitational load per contact.
    // This provides a friction budget floor for resting contacts but does NOT
    // pre-apply velocity changes (doing so creates separation that the solver
    // then cancels, wasting iterations).
    constexpr int solverIterations = 20;

    for ( int i = 0; i < contactCount; ++i )
    {
        contacts[i].accN = warmStartPerContact;
    }

    for ( int iter = 0; iter < solverIterations; ++iter )
    {
        for ( int i = 0; i < contactCount; ++i )
        {
            Contact& c = contacts[i];

            // Velocity at contact point: v + omega x r
            Vector3 vAtContact = velocity + Vector::CrossProduct( omega, c.r );

            // --- Normal constraint (non-penetration) ---
            float vn = vAtContact * c.normal;
            float lambdaN = c.normalMass * ( -vn + c.bias );

            // Accumulated impulse clamping: only push, never pull
            float oldAccN = c.accN;
            c.accN = oldAccN + lambdaN;
            if ( c.accN < 0.0f ) c.accN = 0.0f;
            float deltaN = c.accN - oldAccN;

            Vector3 impulseN = c.normal * deltaN;
            velocity += impulseN * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseN ) );

            // --- Friction constraints (Coulomb cone) ---
            // Use max(accN, gravity load) as friction budget. For resting
            // contacts the solver's accN can be near-zero (tangential sliding)
            // but static friction still acts proportional to normal force.
            vAtContact = velocity + Vector::CrossProduct( omega, c.r );

            // Tangent axis 1
            float vt1 = vAtContact * c.tangent1;
            float lambdaT1 = c.tangentMass1 * ( -vt1 );

            float frictionBudget = ( c.accN > warmStartPerContact )
                                     ? c.accN : warmStartPerContact;
            float maxFriction = mu * frictionBudget;
            float oldAccT1 = c.accT1;
            c.accT1 = oldAccT1 + lambdaT1;
            if ( c.accT1 > maxFriction ) c.accT1 = maxFriction;
            if ( c.accT1 < -maxFriction ) c.accT1 = -maxFriction;
            float deltaT1 = c.accT1 - oldAccT1;

            Vector3 impulseT1 = c.tangent1 * deltaT1;
            velocity += impulseT1 * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseT1 ) );

            // Tangent axis 2
            vAtContact = velocity + Vector::CrossProduct( omega, c.r );
            float vt2 = vAtContact * c.tangent2;
            float lambdaT2 = c.tangentMass2 * ( -vt2 );

            float oldAccT2 = c.accT2;
            c.accT2 = oldAccT2 + lambdaT2;
            if ( c.accT2 > maxFriction ) c.accT2 = maxFriction;
            if ( c.accT2 < -maxFriction ) c.accT2 = -maxFriction;
            float deltaT2 = c.accT2 - oldAccT2;

            Vector3 impulseT2 = c.tangent2 * deltaT2;
            velocity += impulseT2 * invMass;
            omega += applyInvInertia( Vector::CrossProduct( c.r, impulseT2 ) );
        }
    }

    // --- Position correction (direct projection) ---
    float maxPen = 0.0f;
    for ( int i = 0; i < contactCount; ++i )
    {
        float corrPen = contacts[i].penetration - penetrationSlop;
        if ( corrPen > maxPen )
        {
            maxPen = corrPen;
        }
    }
    if ( maxPen > penetrationSlop )
    {
        position += planeNormal * ( ( maxPen - penetrationSlop ) * 0.4f );
    }

    // --- Gravitational tipping (boxes only, edge/vertex contacts) ---
    // When a box rests on fewer than 4 contacts (edge or vertex), it may be
    // in an unstable configuration. Apply the gravitational torque about the
    // contact centroid to drive it toward its nearest stable face. This
    // is physically correct: gravity at the CM creates a moment about the
    // support that the solver's normal impulses don't fully capture.
    bool isBox = std::visit( []( const auto& s ) -> bool
    {
        using S = std::decay_t<decltype( s )>;
        return std::is_same_v<S, BoundingBox>;
    }, gameModel.m_boundingVolume );

    if ( isBox && contactCount > 0 && contactCount < 4 )
    {
        // Only apply when nearly at rest — during impacts the solver handles it
        float speedSqChk = velocity * velocity;
        float omegaSqChk = omega * omega;
        constexpr float tippingSpeedThreshold = 1.0f;
        constexpr float tippingOmegaThreshold = 0.5f;

        if ( speedSqChk < tippingSpeedThreshold * tippingSpeedThreshold &&
             omegaSqChk < tippingOmegaThreshold * tippingOmegaThreshold )
        {
            // Compute centroid of contacts (vector from CM to avg contact point)
            Vector3 contactCentroid = Math::Vector::ZERO_VECTOR;
            for ( int i = 0; i < contactCount; ++i )
            {
                contactCentroid += contacts[i].r;
            }
            contactCentroid = contactCentroid * ( 1.0f / static_cast<float>( contactCount ) );

            // Gravitational torque about contact centroid:
            // T = (CM - contact_centroid) x F_gravity
            // Since contacts[i].r is from CM to contact, (CM - contact) = -r
            Vector3 leverArm = contactCentroid * ( -1.0f );  // from contact to CM
            Vector3 gravForce( 0.0f, mass * Cfg().gravity, 0.0f );
            Vector3 gravTorque = Vector::CrossProduct( leverArm, gravForce );

            // Scale down to 50% to avoid over-correction / oscillation
            constexpr float tippingScale = 0.5f;
            Vector3 angAccel = applyInvInertia( gravTorque ) * tippingScale;
            omega += angAccel * changeInTime;
        }
    }

    // --- Rolling friction ---
    // Small torque opposing angular velocity to bring objects to rest.
    float normalForce = mass * fabsf( Cfg().gravity ) * fabsf( planeNormal.y );
    float omegaMagSq = omega * omega;
    if ( omegaMagSq > TOLERANCE * TOLERANCE )
    {
        float omegaMag = sqrtf( omegaMagSq );

        float rEff = std::visit( []( const auto& s ) -> float
        {
            using S = std::decay_t<decltype( s )>;
            if constexpr ( std::is_same_v<S, BoundingSphere> )
            {
                return s.GetRadius();
            }
            else
            {
                const Vector3& he = s.GetHalfExtents();
                return ( he.x + he.y + he.z ) / 3.0f;
            }
        }, gameModel.m_boundingVolume );

        constexpr float muRolling = 0.02f;
        float rollingTorqueMag = muRolling * normalForce * rEff;

        float avgInertia = ( inertia.x + inertia.y + inertia.z ) / 3.0f;
        if ( avgInertia < TOLERANCE ) avgInertia = 1.0f;
        float deltaOmega = ( rollingTorqueMag / avgInertia ) * changeInTime;

        if ( deltaOmega >= omegaMag )
        {
            omega = Math::Vector::ZERO_VECTOR;
        }
        else
        {
            Vector3 omegaDir = omega / omegaMag;
            omega -= omegaDir * deltaOmega;
        }
    }

    // --- Sleep detection ---
    // Angular threshold must be very low to allow gravity-driven tipping
    // to bring tilted boxes flush against the surface before sleeping.
    float speedSq = velocity * velocity;
    omegaMagSq = omega * omega;
    constexpr float sleepLinear = 0.05f;
    constexpr float sleepAngular = 0.02f;
    if ( speedSq < sleepLinear * sleepLinear &&
         omegaMagSq < sleepAngular * sleepAngular )
    {
        velocity = Math::Vector::ZERO_VECTOR;
        omega = Math::Vector::ZERO_VECTOR;
    }

    // --- Write back ---
    gameModel.m_physicsInfo.SetPosition( position );
    gameModel.m_physicsInfo.SetLinearVelocity( velocity );
    gameModel.m_physicsInfo.SetAngularVelocity( omega );

    // --- Visual pole alignment (sphere only, cosmetic) ---
    std::visit( [&]( const auto& shape )
    {
        using ShapeT = std::decay_t<decltype( shape )>;
        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            float omMagSq = omega * omega;
            if ( omMagSq < TOLERANCE * TOLERANCE ) return;
            if ( !Cfg().rollAlignEnabled ) return;

            float radius = shape.GetRadius();
            bool inWater = ( position.y - radius ) < Cfg().fluidHeight;
            if ( inWater ) return;

            float omMag = sqrtf( omMagSq );
            Vector3 omDir = omega / omMag;

            Vector3 pole = gameModel.GetOrientationUp();
            float poleMag = VectorMag( pole );
            if ( poleMag < TOLERANCE ) return;
            pole = pole / poleMag;

            float poleAlongOm = pole * omDir;
            Vector3 targetPole = pole - omDir * poleAlongOm;
            float targetMag = VectorMag( targetPole );

            if ( targetMag < TOLERANCE )
            {
                targetPole = velocity - omDir * ( velocity * omDir );
                targetMag = VectorMag( targetPole );
            }
            if ( targetMag < TOLERANCE ) return;

            targetPole = targetPole / targetMag;

            float dotPole = pole * targetPole;
            if ( dotPole > 1.0f ) dotPole = 1.0f;
            if ( dotPole < -1.0f ) dotPole = -1.0f;

            float angle = acosf( dotPole );
            float maxAngle = Cfg().rollAlignMaxCorrectionDeg * _PI / 180.0f;
            if ( angle > maxAngle && maxAngle > 0.0f )
            {
                angle = maxAngle;
            }

            if ( angle > Cfg().rollAlignPerpToleranceDeg * _PI / 180.0f )
            {
                Vector3 axis = Vector::CrossProduct( pole, targetPole );
                float axisMag = VectorMag( axis );
                if ( axisMag > TOLERANCE )
                {
                    axis = axis / axisMag;
                    Quaternion q = gameModel.m_physicsInfo.GetOrientation();
                    q.RotateAboutAxis( axis, angle );
                    gameModel.m_physicsInfo.SetOrientation( q );
                }
            }
        }
    }, gameModel.m_boundingVolume );
}


// =============================================================================
// SPHERE-SPHERE / MIXED GAME MODEL COLLISION RESPONSE
// =============================================================================
//
// Sphere-sphere: uses proper Coulomb friction-based spin transfer.
// Mixed (sphere-box, box-box): center-to-center impulse with mass-weighted
// positional correction.
//
// =============================================================================
void ImpulseSolver::RespondCollisionGameModels( GameModel& gameModel1,
                                                GameModel& gameModel2 )
{
    std::visit( [&]( const auto& shape1, const auto& shape2 )
                {
        using Shape1T = std::decay_t<decltype( shape1 )>;
        using Shape2T = std::decay_t<decltype( shape2 )>;

        if constexpr ( std::is_same_v<Shape1T, BoundingSphere> && std::is_same_v<Shape2T, BoundingSphere> )
        {
            Vector3 collisionNormal =
                ImpulseSolver::GetCollisionNormalSphereVsSphere( gameModel1, gameModel2 );

            ImpulseSolver::SphereVsSphereAngular( gameModel1, gameModel2, collisionNormal );
            ImpulseSolver::SphereVsSphereLinear( gameModel1, gameModel2, collisionNormal );

            gameModel1.m_physicsInfo.ApplyChangeInAngularVelocity();
            gameModel2.m_physicsInfo.ApplyChangeInAngularVelocity();

            // Positional correction: push overlapping spheres apart
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
        }
        else
        {
            // Box-sphere, sphere-box, or box-box: simple center-based impulse response.
            Vector3 pos1 = gameModel1.m_physicsInfo.GetPosition();
            Vector3 pos2 = gameModel2.m_physicsInfo.GetPosition();
            Vector3 delta = pos2 - pos1;
            float dist = Vector::VectorMag( delta );
            if ( dist < TOLERANCE )
            {
                return;
            }
            Vector3 normal = delta / dist;

            float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
            float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
            float e = sqrtf( e1 * e2 );

            Vector3 vel1 = gameModel1.m_physicsInfo.GetVelocity();
            Vector3 vel2 = gameModel2.m_physicsInfo.GetVelocity();
            Vector3 relVel = vel1 - vel2;
            float relNormalVel = relVel * normal;

            if ( relNormalVel <= 0.0f )
            {
                return;
            }

            float invMass1 = gameModel1.GetInvertedMass();
            float invMass2 = gameModel2.GetInvertedMass();
            float j = -( 1.0f + e ) * relNormalVel / ( invMass1 + invMass2 );

            gameModel1.m_physicsInfo.SetLinearVelocity( vel1 + normal * ( j * invMass1 ) );
            gameModel2.m_physicsInfo.SetLinearVelocity( vel2 - normal * ( j * invMass2 ) );

            float br1 = GetShapeBoundingRadius( gameModel1.m_boundingVolume );
            float br2 = GetShapeBoundingRadius( gameModel2.m_boundingVolume );
            float overlap = ( br1 + br2 ) - dist;
            if ( overlap > 0.0f )
            {
                float totalInvMass = invMass1 + invMass2;
                gameModel1.m_physicsInfo.SetPosition( pos1 - normal * ( overlap * invMass1 / totalInvMass ) );
                gameModel2.m_physicsInfo.SetPosition( pos2 + normal * ( overlap * invMass2 / totalInvMass ) );
            }
        } },
                gameModel1.m_boundingVolume,
                gameModel2.m_boundingVolume );
}


// =============================================================================
// SPHERE-SPHERE LINEAR VELOCITY EXCHANGE
// =============================================================================
//
// Uses the 1D elastic collision formula projected along the collision normal:
//
//         (m1 - e·m2)·v1i + (1+e)·m2·v2i
//  v1f = ──────────────────────────────────
//                   m1 + m2
//
// Restitution combined via geometric mean (e = sqrt(e1 * e2)).
// A perfectly inelastic material (e=0) dominates regardless of the other body.
//
// =============================================================================
void ImpulseSolver::SphereVsSphereLinear( GameModel& gameModel1,
                                          GameModel& gameModel2,
                                          const Vector3& collisionNormal )
{
    Vector3 v1 = gameModel1.m_physicsInfo.GetVelocity();
    Vector3 v2 = gameModel2.m_physicsInfo.GetVelocity();

    float v1Proj = v1 * collisionNormal;
    float v2Proj = v2 * collisionNormal;

    float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
    float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
    float e = sqrtf( e1 * e2 );

    float m1 = gameModel1.GetMass();
    float m2 = gameModel2.GetMass();
    float totalMass = m1 + m2;

    float v1f = ( ( m1 - e * m2 ) * v1Proj + ( 1.0f + e ) * m2 * v2Proj ) / totalMass;
    float v2f = ( ( m2 - e * m1 ) * v2Proj + ( 1.0f + e ) * m1 * v1Proj ) / totalMass;

    gameModel1.m_physicsInfo.SetLinearVelocity( v1 + collisionNormal * ( v1f - v1Proj ) );
    gameModel2.m_physicsInfo.SetLinearVelocity( v2 + collisionNormal * ( v2f - v2Proj ) );
}


// =============================================================================
// SPHERE-SPHERE ANGULAR IMPULSE (Friction-Based Spin Transfer)
// =============================================================================
//
// For sphere-sphere collisions, the contact point lies along the collision
// normal (r = R*n), making r × n = 0.  This means the NORMAL impulse cannot
// produce angular velocity change — all spin transfer must come from TANGENTIAL
// friction at the contact point.
//
//  1. Compute the tangential (sliding) velocity at the contact point
//  2. Apply a friction impulse opposing this sliding, clamped to the Coulomb
//     friction cone (μ × normal impulse magnitude)
//  3. Δω = I⁻¹(r × J)
//
// =============================================================================
void ImpulseSolver::SphereVsSphereAngular( GameModel& gameModel1,
                                           GameModel& gameModel2,
                                           const Vector3& collisionNormal )
{
    float r1 = GetShapeBoundingRadius( gameModel1.m_boundingVolume );
    float r2 = GetShapeBoundingRadius( gameModel2.m_boundingVolume );

    Vector3 rContact1 = collisionNormal * r1;
    Vector3 rContact2 = collisionNormal * ( -r2 );

    Vector3 vContact1 = gameModel1.m_physicsInfo.GetVelocity() +
                        Vector::CrossProduct( gameModel1.m_physicsInfo.GetAngularVelocity(), rContact1 );
    Vector3 vContact2 = gameModel2.m_physicsInfo.GetVelocity() +
                        Vector::CrossProduct( gameModel2.m_physicsInfo.GetAngularVelocity(), rContact2 );

    Vector3 vRel = vContact2 - vContact1;
    float vRelNormal = vRel * collisionNormal;
    Vector3 vTangent = vRel - collisionNormal * vRelNormal;
    float vTangentMagSq = vTangent * vTangent;

    if ( vTangentMagSq < TOLERANCE * TOLERANCE )
    {
        gameModel1.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        gameModel2.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        return;
    }

    float vTangentMag = sqrtf( vTangentMagSq );
    Vector3 tangentDir = vTangent / vTangentMag;

    float e1 = gameModel1.m_physicsInfo.GetCoefficientRestitution();
    float e2 = gameModel2.m_physicsInfo.GetCoefficientRestitution();
    float e = sqrtf( e1 * e2 );

    float invMass1 = gameModel1.GetInvertedMass();
    float invMass2 = gameModel2.GetInvertedMass();
    Vector3 invInertia1 = gameModel1.GetInvertedRotationalInertia();
    Vector3 invInertia2 = gameModel2.GetInvertedRotationalInertia();

    float jnMag = fabsf( -( 1.0f + e ) * vRelNormal / ( invMass1 + invMass2 ) );

    Vector3 r1CrossT = Vector::CrossProduct( rContact1, tangentDir );
    Vector3 iInv1_r1CrossT = Vector::VectorMultiply( invInertia1, r1CrossT );
    Vector3 term1 = Vector::CrossProduct( iInv1_r1CrossT, rContact1 );

    Vector3 r2CrossT = Vector::CrossProduct( rContact2, tangentDir );
    Vector3 iInv2_r2CrossT = Vector::VectorMultiply( invInertia2, r2CrossT );
    Vector3 term2 = Vector::CrossProduct( iInv2_r2CrossT, rContact2 );

    float kTangent = invMass1 + invMass2 + ( tangentDir * term1 ) + ( tangentDir * term2 );
    if ( kTangent < TOLERANCE )
    {
        gameModel1.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        gameModel2.m_physicsInfo.SetChangeInAngularVelocity( Math::Vector::ZERO_VECTOR );
        return;
    }

    float jt = vTangentMag / kTangent;

    float mu = ( gameModel1.m_physicsInfo.GetFrictionCoefficient() +
                 gameModel2.m_physicsInfo.GetFrictionCoefficient() ) * 0.5f;
    float maxFriction = mu * jnMag;
    if ( jt > maxFriction )
    {
        jt = maxFriction;
    }

    Vector3 frictionImpulse = tangentDir * ( -jt );

    Vector3 deltaOmega1 = Vector::VectorMultiply( invInertia1, Vector::CrossProduct( rContact1, frictionImpulse * ( -1.0f ) ) );
    Vector3 deltaOmega2 = Vector::VectorMultiply( invInertia2, Vector::CrossProduct( rContact2, frictionImpulse ) );

    gameModel1.m_physicsInfo.SetChangeInAngularVelocity( deltaOmega1 );
    gameModel2.m_physicsInfo.SetChangeInAngularVelocity( deltaOmega2 );
}


Vector3 ImpulseSolver::GetCollidedObjectWorldPosition( GameModel& gameModel )
{
    return gameModel.m_physicsInfo.GetPosition() +
           ( gameModel.m_physicsInfo.GetOrientationMatrix() * GetShapePosition( gameModel.m_boundingVolume ) );
}


Vector3 ImpulseSolver::GetCollisionNormalSphereVsSphere( GameModel& gameModel1,
                                                         GameModel& gameModel2 )
{
    Vector3 pos1 = ImpulseSolver::GetCollidedObjectWorldPosition( gameModel1 );
    Vector3 pos2 = ImpulseSolver::GetCollidedObjectWorldPosition( gameModel2 );
    Vector3 normal = pos2 - pos1;
    normal.Normalise();
    return normal;
}

// --- Includes ---
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezVector3.h"
#include "SkullbonezCollisionShape.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::CollisionDetection;


void CollisionResponse::RespondCollisionTerrain( GameModel& gameModel )
{
    // Reverse v so it points away from the contact plane, then project onto n:
    //   v_⊥ = (-v) · n
    Vector3 totalVelocity = gameModel.m_physicsInfo.GetVelocity() * -1;
    float projectedVelocity = totalVelocity * gameModel.m_responseInformation.collidedPlane.m_normal;

    // Resting contact threshold — too slow to bounce, treat as grounded
    if ( projectedVelocity < 1.0f )
    {
        gameModel.SetIsGrounded( true );
    }

    std::visit( [&]( const auto& shape )
                {
        using ShapeT = std::decay_t<decltype( shape )>;

        if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
        {
            if ( !gameModel.IsGrounded() )
            {
                // apply the response forces to the model - angular impulse first
                CollisionResponse::SphereVsPlaneAngularImpulse( gameModel );

                // then linear impulse
                CollisionResponse::SphereVsPlaneLinearImpulse( gameModel, totalVelocity, projectedVelocity );

                // apply the change in angular velocity now the linear reaction has taken place
                gameModel.m_physicsInfo.ApplyChangeInAngularVelocity();
            }
            else
            {
                // perform the rolling physics
                CollisionResponse::SphereVsPlaneRollResponse( gameModel );
            }
        } },
                gameModel.m_boundingVolume );
}


void CollisionResponse::RespondCollisionGameModels( GameModel& gameModel1,
                                                    GameModel& gameModel2 )
{
    // Wake both balls — a ball-to-ball hit must break grounded roll mode
    gameModel1.SetIsGrounded( false );
    gameModel2.SetIsGrounded( false );

    std::visit( [&]( const auto& shape1, const auto& shape2 )
                {
        using Shape1T = std::decay_t<decltype( shape1 )>;
        using Shape2T = std::decay_t<decltype( shape2 )>;

        if constexpr ( std::is_same_v<Shape1T, BoundingSphere> && std::is_same_v<Shape2T, BoundingSphere> )
        {
            const Vector3 n = GetCollisionNormalSphereVsSphere( gameModel1, gameModel2 );

            SphereVsSphereAngular( gameModel1, gameModel2, n );
            SphereVsSphereLinear( gameModel1, gameModel2, n );

            gameModel1.m_physicsInfo.ApplyChangeInAngularVelocity();
            gameModel2.m_physicsInfo.ApplyChangeInAngularVelocity();

            // Positional correction: split the overlap evenly along n to prevent
            // penetration accumulation.
            //   overlap = (r1 + r2) - |Δp|
            //   p1 ← p1 - ½·overlap·n̂,   p2 ← p2 + ½·overlap·n̂
            const Vector3 pos1 = gameModel1.m_physicsInfo.GetPosition();
            const Vector3 pos2 = gameModel2.m_physicsInfo.GetPosition();
            const Vector3 delta = pos2 - pos1;
            const float dist = Vector::VectorMag( delta );
            const float overlap = ( shape1.GetRadius() + shape2.GetRadius() ) - dist;
            if ( overlap > 0.0f && dist > 0.0f )
            {
                const Vector3 push = ( delta / dist ) * ( overlap * 0.5f );
                gameModel1.m_physicsInfo.SetPosition( pos1 - push );
                gameModel2.m_physicsInfo.SetPosition( pos2 + push );
            }
        } },
                gameModel1.m_boundingVolume,
                gameModel2.m_boundingVolume );
}


void CollisionResponse::SphereVsPlaneRollResponse( GameModel& gameModel )
{
    // Rolling-contact damping:  v ← 0.9·v,  ω ← 0.9·ω
    gameModel.m_physicsInfo.SetLinearVelocity( gameModel.m_physicsInfo.GetVelocity() * 0.9f );
    gameModel.m_physicsInfo.SetAngularVelocity( gameModel.m_physicsInfo.GetAngularVelocity() * 0.9f );
}


void CollisionResponse::SphereVsPlaneLinearImpulse( GameModel& gameModel, Vector3 totalVelocity, float projectedVelocity )
{
    const Vector3 n = gameModel.m_responseInformation.collidedPlane.m_normal;
    const float e = gameModel.m_physicsInfo.GetCoefficientRestitution();
    const float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();

    // Bounce direction:  d = reflect(v̂_in, n)
    Vector3 incidentVector = totalVelocity;
    incidentVector.Normalise();
    const Vector3 d = Vector::VectorReflect( incidentVector, n );

    //   v_bounce = d · (v_⊥ · e)
    const Vector3 v_bounce = d * ( projectedVelocity * e );

    // Surface velocity from spin at the contact point:
    //   v_spin = ω × r_contact   with   r_contact = -r·n
    const float r = GetShapeBoundingRadius( gameModel.m_boundingVolume );
    const Vector3 v_spin = Vector::CrossProduct( gameModel.m_physicsInfo.GetAngularVelocity(), n * ( -r ) );

    // Strip the normal component so only tangential slide is converted by friction:
    //   v_spin_t = v_spin - (v_spin · n)·n
    //   v_grip   = -mu · v_spin_t       (friction opposes sliding)
    const Vector3 v_spin_t = v_spin - n * ( v_spin * n );
    const Vector3 v_grip = v_spin_t * ( -mu );

    //   v' = v_bounce + v_grip
    gameModel.m_physicsInfo.SetLinearVelocity( v_bounce + v_grip );

    // Spin energy was transferred to translation — bleed it off
    gameModel.m_physicsInfo.DampenAngularVelocity();
}


void CollisionResponse::SphereVsPlaneAngularImpulse( GameModel& gameModel )
{
    const Vector3 n = gameModel.m_responseInformation.collidedPlane.m_normal;
    const float e = gameModel.m_physicsInfo.GetCoefficientRestitution();
    const float mu = gameModel.m_physicsInfo.GetFrictionCoefficient();

    // Object-space contact point:  r = p_contact - p_center
    Vector3 r = GeometricMath::ComputeIntersectionPoint( gameModel.m_responseInformation.collidedRay, gameModel.m_responseInformation.collisionTime );
    r -= gameModel.m_physicsInfo.GetPosition();

    // Velocity of the contact point:  v_p = v + ω × r
    // and its component along the normal (signed approach speed):
    const Vector3 v_p = gameModel.m_physicsInfo.GetVelocity() + Vector::CrossProduct( gameModel.m_physicsInfo.GetAngularVelocity(), r );
    const float v_n = n * v_p;

    // Rigid-body collision impulse against a fixed plane (m₂ → ∞):
    //
    //              -(1+e)·(v_p · n)
    //   J = ───────────────────────────────
    //          1/m  +  n · ((I·(r×n)) × r)
    //
    // (Note: this engine multiplies by I element-wise rather than I⁻¹ — see report.)
    const float numerator = -v_n * ( 1.0f + e );

    const Vector3 r_x_n = Vector::CrossProduct( r, n );
    const Vector3 I_r_x_n = Vector::VectorMultiply( gameModel.m_physicsInfo.GetRotationalInertia(), r_x_n );
    const float angularDenom = n * Vector::CrossProduct( I_r_x_n, r );
    const float denominator = gameModel.m_physicsInfo.GetInvertedMass() + angularDenom;

    const float J = numerator / denominator;

    // Δω from the impulse (I⁻¹ already folded into the I·(r×n) term above):
    //   delta_omega = (r × J·n) · 10·mu
    Vector3 deltaOmega = Vector::CrossProduct( r, n * J ) * ( mu * 10.0f );

    // Tangential friction — drive ω toward the no-slip condition at the contact point.
    //   v_t       = v_p - (v_p · n)·n
    //   delta_omega_noslip = (r × -v_t) / |r|^2
    //   delta_omega += grip · delta_omega_noslip,   grip = clamp(10·mu, 0, 1)
    const Vector3 v_t = v_p - n * v_n;
    if ( Vector::VectorMag( v_t ) > TOLERANCE )
    {
        const float r_sq = Vector::VectorMagSquared( r );
        if ( r_sq > TOLERANCE )
        {
            const Vector3 noSlipDelta = Vector::CrossProduct( r, v_t * ( -1.0f ) ) / r_sq;
            const float grip = ( mu * 10.0f > 1.0f ) ? 1.0f : mu * 10.0f;
            deltaOmega += noSlipDelta * grip;
        }
    }

    gameModel.m_physicsInfo.SetChangeInAngularVelocity( deltaOmega );
}


void CollisionResponse::SphereVsSphereLinear( GameModel& gameModel1,
                                              GameModel& gameModel2,
                                              const Vector3& n )
{
    // 1-D inelastic collision along the contact normal (Hibbeler/Hecht):
    //
    //          (m₁ - e·m₂)·u₁ + (1 + e)·m₂·u₂                  (m₂ - e·m₁)·u₂ + (1 + e)·m₁·u₁
    //   v₁ = ─────────────────────────────────── ,      v₂ = ───────────────────────────────────
    //                    m₁ + m₂                                          m₁ + m₂
    //
    // where u_i = v_i · n (incoming projection on n) and e = average restitution.
    // The tangential component is preserved:  v'_i = v_i + (v'ₙ - uᵢ)·n
    const Vector3 v1 = gameModel1.m_physicsInfo.GetVelocity();
    const Vector3 v2 = gameModel2.m_physicsInfo.GetVelocity();
    const float u1 = v1 * n;
    const float u2 = v2 * n;

    const float m1 = gameModel1.m_physicsInfo.GetMass();
    const float m2 = gameModel2.m_physicsInfo.GetMass();
    const float e = ( gameModel1.m_physicsInfo.GetCoefficientRestitution() +
                      gameModel2.m_physicsInfo.GetCoefficientRestitution() ) * 0.5f;
    const float invSum = 1.0f / ( m1 + m2 );

    const float v1n = ( ( m1 - e * m2 ) * u1 + ( 1.0f + e ) * m2 * u2 ) * invSum;
    const float v2n = ( ( m2 - e * m1 ) * u2 + ( 1.0f + e ) * m1 * u1 ) * invSum;

    gameModel1.m_physicsInfo.SetLinearVelocity( v1 + n * ( v1n - u1 ) );
    gameModel2.m_physicsInfo.SetLinearVelocity( v2 + n * ( v2n - u2 ) );
}


void CollisionResponse::SphereVsSphereAngular( GameModel& gameModel1,
                                               GameModel& gameModel2,
                                               const Vector3& n )
{
    // Object-space contact points:  r₁ = +R₁·n,  r₂ = -R₂·n
    Vector3 r1 = n * GetShapeBoundingRadius( gameModel1.m_boundingVolume );
    Vector3 r2 = -n * GetShapeBoundingRadius( gameModel2.m_boundingVolume );

    // Velocity at each contact point:  v_pᵢ = vᵢ + ωᵢ × rᵢ
    const Vector3 v1 = gameModel1.m_physicsInfo.GetVelocity();
    const Vector3 v2 = gameModel2.m_physicsInfo.GetVelocity();
    const Vector3 v_p1 = v1 + Vector::CrossProduct( gameModel1.m_physicsInfo.GetAngularVelocity(), r1 );
    const Vector3 v_p2 = v2 + Vector::CrossProduct( gameModel2.m_physicsInfo.GetAngularVelocity(), r2 );

    // Approach speed along n:  v_n = (v_p2 - v_p1) · n
    const float v_n = ( v_p2 - v_p1 ) * n;

    // Replace rᵢ with the normalised relative-linear-velocity direction.
    // For spheres rᵢ ∥ n, so r×n = 0 and the rotational denominator term vanishes.
    // Using v̂_rel as the moment arm restores a non-trivial angular response.
    // [Original comment ~SE 2005: empirical, modelled on the sphere/plane routine.]
    Vector3 vRelDir = v2 - v1;
    vRelDir.Normalise();
    r1 = r2 = vRelDir;

    // Two-body rigid impulse along n:
    //
    //                       -(1+e)·v_n
    //   J = ──────────────────────────────────────────────────────────────
    //         1/m₁ + 1/m₂ + n·((I₁·(r₁×n))×r₁) + n·((I₂·(r₂×n))×r₂)
    //
    // (Same I-vs-I⁻¹ note as the plane case applies — see report.)
    const float e = ( gameModel1.m_physicsInfo.GetCoefficientRestitution() +
                      gameModel2.m_physicsInfo.GetCoefficientRestitution() ) * 0.5f;
    const float numerator = -v_n * ( 1.0f + e );

    auto angularTerm = [&]( const Vector3& r, const Vector3& I ) {
        return n * Vector::CrossProduct( Vector::VectorMultiply( I, Vector::CrossProduct( r, n ) ), r );
    };

    const float denominator = gameModel1.m_physicsInfo.GetInvertedMass() +
                              gameModel2.m_physicsInfo.GetInvertedMass() +
                              angularTerm( r1, gameModel1.m_physicsInfo.GetRotationalInertia() ) +
                              angularTerm( r2, gameModel2.m_physicsInfo.GetRotationalInertia() );

    const Vector3 J = n * ( numerator / denominator );

    // Δω = r × ±J  (sign flipped between bodies — equal & opposite impulses)
    gameModel1.m_physicsInfo.SetChangeInAngularVelocity( Vector::CrossProduct( r1,  J ) );
    gameModel2.m_physicsInfo.SetChangeInAngularVelocity( Vector::CrossProduct( r2, -J ) );
}


Ray CollisionResponse::CalculateRay( GameModel& gameModel,
                                     float changeInTime )
{
    // Sweep ray over Δt:  origin = p,  direction = v·Δt
    return Ray( gameModel.m_physicsInfo.GetPosition(), gameModel.m_physicsInfo.GetVelocity() * changeInTime );
}


Vector3 CollisionResponse::GetCollidedObjectWorldPosition( GameModel& gameModel )
{
    // World pos of the bounding shape:  p + R · p_local
    return gameModel.m_physicsInfo.GetPosition() + ( gameModel.m_physicsInfo.GetOrientationMatrix() * GetShapePosition( gameModel.m_boundingVolume ) );
}


Vector3 CollisionResponse::GetCollisionNormalSphereVsSphere( GameModel& gameModel1,
                                                             GameModel& gameModel2 )
{
    // n̂ = normalize(p₂ - p₁)
    Vector3 n = GetCollidedObjectWorldPosition( gameModel2 ) - GetCollidedObjectWorldPosition( gameModel1 );
    n.Normalise();
    return n;
}

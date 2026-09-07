#include "PointJointBlock.h"
#include "PhysicsBodyStore.h"
#include "../Core/FatalError.h"
#include "../Maths/ScalarMath.h"

using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

void PointJointBlock::Prepare( const PointJointConstraint& constraint, const PhysicsBodyStore& bodyStore,
                               std::span<const SolverBodyState> bodies, float dt, std::size_t sourceIndex )
{
    *this = PointJointBlock();
    m_sourceIndex = sourceIndex;
    m_handle = constraint.handle;
    m_impulse = constraint.accumulatedImpulse;
    m_bodyA = constraint.BodyAIndex( bodyStore );
    m_bodyB = constraint.BodyBIndex( bodyStore );
    if ( m_bodyA < 0 || m_bodyB < 0 || m_bodyA == m_bodyB || static_cast<std::size_t>( m_bodyA ) >= bodies.size() ||
         static_cast<std::size_t>( m_bodyB ) >= bodies.size() )
    {
        m_impulse = ZERO_VECTOR;
        return;
    }
    const SolverBodyState& bodyA = bodies[static_cast<std::size_t>( m_bodyA )];
    const SolverBodyState& bodyB = bodies[static_cast<std::size_t>( m_bodyB )];
    if ( bodyA.invMass + bodyB.invMass <= TOLERANCE )
    {
        return; // Sleeping joints retain their cache without applying it.
    }
    const auto hot = bodyStore.HotFields();
    auto rotationA = PhysicsBodyOrientation( hot, static_cast<std::size_t>( m_bodyA ) );
    auto rotationB = PhysicsBodyOrientation( hot, static_cast<std::size_t>( m_bodyB ) );
    m_rA = rotationA.GetOrientationMatrix() * constraint.localAnchorA;
    m_rB = rotationB.GetOrientationMatrix() * constraint.localAnchorB;
    m_error = PhysicsBodyPosition( hot, static_cast<std::size_t>( m_bodyB ) ) + m_rB -
              ( PhysicsBodyPosition( hot, static_cast<std::size_t>( m_bodyA ) ) + m_rA );
    m_softness = PointJointSoftness( constraint.frequencyHz, constraint.dampingRatio, dt );
    m_effectiveMass = PointJointMath::PointJointEffectiveMass( bodyA, m_rA, bodyB, m_rB );
    if ( !m_softness.IsEnabled() || !m_effectiveMass.IsValid() )
    {
        m_impulse = ZERO_VECTOR;
        return;
    }
    const float distance = VectorMag( m_error );
    const Vector3 axis = distance > 0.0f ? m_error / distance : ZERO_VECTOR;
    const float distanceError = (std::max)( 0.0f, distance - constraint.slack );
    // Units: positional bias is m/s; the recovery cap does not clamp body motion.
    m_bias = axis * std::clamp( distanceError * m_softness.BiasRate(), 0.0f, 28.0f );
    m_active = true;
}

void PointJointBlock::ApplyImpulse( std::span<SolverBodyState> bodies, const Vector3& impulse ) const
{
    SolverBodyState& bodyA = bodies[static_cast<std::size_t>( m_bodyA )];
    SolverBodyState& bodyB = bodies[static_cast<std::size_t>( m_bodyB )];
    bodyA.linearVelocity += impulse * bodyA.invMass;
    bodyA.angularVelocity += bodyA.ApplyInverseInertia( CrossProduct( m_rA, impulse ) );
    bodyB.linearVelocity -= impulse * bodyB.invMass;
    bodyB.angularVelocity -= bodyB.ApplyInverseInertia( CrossProduct( m_rB, impulse ) );
}

void PointJointBlock::WarmStart( std::span<SolverBodyState> bodies ) const
{
    if ( m_active )
    {
        ApplyImpulse( bodies, m_impulse );
    }
}

float PointJointBlock::Solve( std::span<SolverBodyState> bodies, PointJointIterationSample* sample )
{
    if ( !m_active )
    {
        return 0.0f;
    }
    const SolverBodyState& bodyA = bodies[static_cast<std::size_t>( m_bodyA )];
    const SolverBodyState& bodyB = bodies[static_cast<std::size_t>( m_bodyB )];
    const Vector3 before = bodyB.linearVelocity + CrossProduct( bodyB.angularVelocity, m_rB ) -
                           ( bodyA.linearVelocity + CrossProduct( bodyA.angularVelocity, m_rA ) );
    Vector3 delta = ZERO_VECTOR;
    if ( m_effectiveMass.Solve( before + m_bias, delta ) )
    {
        delta = delta * m_softness.MassScale() - m_impulse * m_softness.ImpulseScale();
        m_impulse += delta;
        ApplyImpulse( bodies, delta );
    }
    if ( sample )
    {
        sample->constraint = m_handle;
        sample->anchorErrorBeforeCorrection = m_error;
        sample->accumulatedImpulse = m_impulse;
        sample->relativeAnchorVelocity = bodyB.linearVelocity + CrossProduct( bodyB.angularVelocity, m_rB ) -
                                         bodyA.linearVelocity - CrossProduct( bodyA.angularVelocity, m_rA );
        sample->minimumScaledPivot = m_effectiveMass.MinimumScaledPivot();
        sample->impulseWorkJoules = -0.5f * Dot( delta, before + sample->relativeAnchorVelocity );
        sample->biasRatePerSecond = m_softness.BiasRate();
        sample->complianceScale = m_softness.ImpulseScale() / m_softness.MassScale();
        const Vector3 responseA = m_impulse * bodyA.invMass +
                                  CrossProduct( bodyA.ApplyInverseInertia( CrossProduct( m_rA, m_impulse ) ), m_rA );
        const Vector3 responseB = m_impulse * bodyB.invMass +
                                  CrossProduct( bodyB.ApplyInverseInertia( CrossProduct( m_rB, m_impulse ) ), m_rB );
        sample->constraintResidualVelocity = sample->relativeAnchorVelocity + m_bias -
                                             ( responseA + responseB ) * sample->complianceScale;
        sample->impulseDeltaSq = VectorMagSquared( delta );
    }
    return VectorMagSquared( delta );
}

void PointJointBlock::StoreImpulse( std::span<PointJointConstraint> constraints ) const
{
    // Invariant: the synchronous solve cannot compact or replace constraint rows.
    // Check identity so a misuse cannot publish a warm impulse into another joint.
    if ( m_sourceIndex >= constraints.size() || constraints[m_sourceIndex].handle != m_handle )
    {
        SB_FATAL( "Physics/PointJointBlock", "Constraint identity changed during a synchronous solve." );
    }
    constraints[m_sourceIndex].accumulatedImpulse = m_impulse;
}

int PointJointConstraint::BodyAIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyA );
}


int PointJointConstraint::BodyBIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyB );
}

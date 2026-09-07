// A three-dimensional implicit joint block prepared once for a fixed step.
// It applies impulses only to the transaction's shared solver-body scratch;
// the transaction owns warm-start order, sweeps, islands and cache publication.
#pragma once
#include "PointJointConstraint.h"
#include "PersistentContactSolver.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace SkullbonezCore::Physics
{
namespace PointJointMath
{
using Math::Vector::CrossProduct;
using Math::Vector::Vector3;
class PointJointEffectiveMass
{
    float m_scale = 0.0f;
    float m_l00 = 0.0f, m_l10 = 0.0f, m_l20 = 0.0f;
    float m_l11 = 0.0f, m_l21 = 0.0f, m_l22 = 0.0f;
    float m_minimumScaledPivot = 0.0f;

  public:
    PointJointEffectiveMass() = default;
    PointJointEffectiveMass( const SolverBodyState& bodyA, const Vector3& rA, const SolverBodyState& bodyB,
                             const Vector3& rB )
    {
        const auto response = [&]( const Vector3& axis )
        {
            const Vector3 responseA = axis * bodyA.invMass +
                                      CrossProduct( bodyA.ApplyInverseInertia( CrossProduct( rA, axis ) ), rA );
            const Vector3 responseB = axis * bodyB.invMass +
                                      CrossProduct( bodyB.ApplyInverseInertia( CrossProduct( rB, axis ) ), rB );
            return responseA + responseB;
        };
        const Vector3 x = response( Vector3( 1.0f, 0.0f, 0.0f ) );
        const Vector3 y = response( Vector3( 0.0f, 1.0f, 0.0f ) );
        const Vector3 z = response( Vector3( 0.0f, 0.0f, 1.0f ) );
        const float scale = (std::max)( x.x, (std::max)( y.y, z.z ) );
        if ( !std::isfinite( scale ) || scale <= 0.0f )
        {
            return;
        }
        constexpr float minimumPivot = 1.0e-8f;
        const float xx = x.x / scale;
        const float yx = ( x.y * 0.5f + y.x * 0.5f ) / scale;
        const float zx = ( x.z * 0.5f + z.x * 0.5f ) / scale;
        const float yy = y.y / scale;
        const float zy = ( y.z * 0.5f + z.y * 0.5f ) / scale;
        const float zz = z.z / scale;
        if ( !std::isfinite( xx ) || xx <= minimumPivot )
        {
            return;
        }
        m_l00 = sqrtf( xx );
        m_l10 = yx / m_l00;
        m_l20 = zx / m_l00;
        const float pivot1 = yy - m_l10 * m_l10;
        if ( !std::isfinite( pivot1 ) || pivot1 <= minimumPivot )
        {
            return;
        }
        m_l11 = sqrtf( pivot1 );
        m_l21 = ( zy - m_l20 * m_l10 ) / m_l11;
        const float pivot2 = zz - m_l20 * m_l20 - m_l21 * m_l21;
        if ( !std::isfinite( pivot2 ) || pivot2 <= minimumPivot )
        {
            return;
        }
        m_l22 = sqrtf( pivot2 );
        m_scale = scale;
        m_minimumScaledPivot = (std::min)( xx, (std::min)( pivot1, pivot2 ) );
    }
    bool IsValid() const
    {
        return m_scale > 0.0f;
    }
    float MinimumScaledPivot() const
    {
        return m_minimumScaledPivot;
    }
    bool Solve( const Vector3& velocityTarget, Vector3& outImpulse ) const
    {
        if ( !IsValid() )
        {
            return false;
        }
        const Vector3 rhs = velocityTarget / m_scale;
        const float a = rhs.x / m_l00;
        const float b = ( rhs.y - m_l10 * a ) / m_l11;
        const float c = ( rhs.z - m_l20 * a - m_l21 * b ) / m_l22;
        const float iz = c / m_l22;
        const float iy = ( b - m_l21 * iz ) / m_l11;
        const float ix = ( a - m_l10 * iy - m_l20 * iz ) / m_l00;
        if ( !std::isfinite( ix ) || !std::isfinite( iy ) || !std::isfinite( iz ) )
        {
            return false;
        }
        outImpulse = Vector3( ix, iy, iz );
        return true;
    }
};
} // namespace PointJointMath

class PointJointBlock
{
    PointJointMath::PointJointEffectiveMass m_effectiveMass;
    PointJointSoftness m_softness { 0.0f, 0.0f, 1.0f };
    Math::Vector::Vector3 m_rA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_rB = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_error = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_bias = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_impulse = Math::Vector::ZERO_VECTOR;
    PhysicsConstraintHandle m_handle;
    std::size_t m_sourceIndex = 0;
    int m_bodyA = -1;
    int m_bodyB = -1;
    bool m_active = false;

    void ApplyImpulse( std::span<SolverBodyState> bodies, const Math::Vector::Vector3& impulse ) const;

  public:
    void Prepare( const PointJointConstraint& constraint, const PhysicsBodyStore& bodyStore,
                  std::span<const SolverBodyState> bodies, float dt, std::size_t sourceIndex );
    void WarmStart( std::span<SolverBodyState> bodies ) const;
    float Solve( std::span<SolverBodyState> bodies, PointJointIterationSample* sample = nullptr );
    void StoreImpulse( std::span<PointJointConstraint> constraints ) const;
    void ClearImpulse()
    {
        m_impulse = Math::Vector::ZERO_VECTOR;
    }
    const Math::Vector::Vector3& AccumulatedImpulse() const
    {
        return m_impulse;
    }
    bool Active() const
    {
        return m_active;
    }
    int BodyA() const
    {
        return m_bodyA;
    }
    int BodyB() const
    {
        return m_bodyB;
    }
    PhysicsConstraintHandle Handle() const
    {
        return m_handle;
    }
    std::size_t SourceIndex() const
    {
        return m_sourceIndex;
    }
};
} // namespace SkullbonezCore::Physics

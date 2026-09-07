#pragma once

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Physics
{
inline constexpr float POINT_JOINT_DEFAULT_FREQUENCY_HZ = 40.0f;
inline constexpr float POINT_JOINT_DEFAULT_DAMPING_RATIO = 1.0f;
inline constexpr int POINT_JOINT_SOLVER_ITERATIONS = 8;

// Compatibility: the old controls had no physical units. This explicit tuning
// migration maps their defaults to 40 Hz / critical damping and preserves their
// relative ordering. It does not promise the historical solver's trajectories.
inline float PointJointFrequencyFromLegacy( float stiffness )
{
    return ( std::clamp( stiffness, 0.0f, 1.0f ) / 0.22f ) * POINT_JOINT_DEFAULT_FREQUENCY_HZ;
}

inline float PointJointDampingFromLegacy( float damping )
{
    return std::clamp( damping, 0.0f, 1.0f ) / 0.35f;
}

// Converts a mass-normalized implicit spring to the coefficients used by a
// three-dimensional block solve. K has units 1/kg; compliance is gamma*K,
// gamma = 1/[dt*omega*(2*zeta+dt*omega)]. Thus (K+gamma*K)^-1 can use the
// same factorization as K, scaled by massScale. biasRate has units 1/s.
class PointJointSoftness
{
    float m_massScale = 0.0f;
    float m_impulseScale = 1.0f;
    float m_biasRate = 0.0f;

  public:
    PointJointSoftness( float frequencyHz, float dampingRatio, float dt )
    {
        if ( !std::isfinite( frequencyHz ) || frequencyHz <= 0.0f || !std::isfinite( dampingRatio ) || dampingRatio < 0.0f ||
             !std::isfinite( dt ) || dt <= 0.0f )
        {
            return;
        }

        // Double intermediates avoid overflow for finite authored floats. The
        // retained solver coefficients and all body arithmetic remain float.
        const double omega = 6.28318530717958647692 * frequencyHz;
        const double denominator = 2.0 * dampingRatio + dt * omega;
        const double response = dt * omega * denominator;
        m_massScale = static_cast<float>( response / ( 1.0 + response ) );
        m_impulseScale = static_cast<float>( 1.0 / ( 1.0 + response ) );
        m_biasRate = static_cast<float>( omega / denominator );
    }

    // Zero frequency disables the joint and discards its warm start. As
    // frequency grows, massScale -> 1, impulseScale -> 0, biasRate -> 1/dt.
    bool IsEnabled() const
    {
        return m_massScale > 0.0f;
    }
    float MassScale() const
    {
        return m_massScale;
    }
    float ImpulseScale() const
    {
        return m_impulseScale;
    }
    float BiasRate() const
    {
        return m_biasRate;
    }
};
} // namespace SkullbonezCore::Physics

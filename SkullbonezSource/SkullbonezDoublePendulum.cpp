/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           |   .--. .--.   |
                                                                           | )/   | |   \( |
                                                                           |/ \__/   \__/ \|
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             |\         /|
                                                                             |\'"VUUUV"'/|
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* -- INCLUDES ------------------------------------------------------------------------------------------*/
#include "SkullbonezDoublePendulum.h"
#include <cmath>

namespace SkullbonezCore
{
namespace Physics
{
DoublePendulum::DoublePendulum( float length1, float length2, float mass1, float mass2, float gravity )
    : m_length1( length1 ), m_length2( length2 ), m_mass1( mass1 ), m_mass2( mass2 ), m_gravity( gravity ),
      m_angle1( 0.5f ), m_angle2( 1.0f ), m_velocity1( 0.0f ), m_velocity2( 0.0f )
{
}

DoublePendulum::~DoublePendulum()
{
}

void DoublePendulum::SetInitialAngles( float angle1, float angle2 )
{
    m_angle1 = angle1;
    m_angle2 = angle2;
}

void DoublePendulum::SetInitialVelocities( float velocity1, float velocity2 )
{
    m_velocity1 = velocity1;
    m_velocity2 = velocity2;
}

float DoublePendulum::GetPosition1X( float originX ) const
{
    return originX + m_length1 * std::sin( m_angle1 );
}

float DoublePendulum::GetPosition1Y( float originY ) const
{
    return originY + m_length1 * std::cos( m_angle1 );
}

float DoublePendulum::GetPosition2X( float originX ) const
{
    return GetPosition1X( originX ) + m_length2 * std::sin( m_angle2 );
}

float DoublePendulum::GetPosition2Y( float originY ) const
{
    return GetPosition1Y( originY ) + m_length2 * std::cos( m_angle2 );
}

void DoublePendulum::ComputeAccelerations( float& accel1, float& accel2 ) const
{
    float m1 = m_mass1;
    float m2 = m_mass2;
    float l1 = m_length1;
    float l2 = m_length2;
    float g = m_gravity;
    float a1 = m_angle1;
    float a2 = m_angle2;
    float w1 = m_velocity1;
    float w2 = m_velocity2;

    // Standard double pendulum Lagrangian (angles from vertical, 0 = hanging down)
    // D = 2m1 + m2 - m2*cos(2*(a1-a2))  — always > 0 for m1 > 0
    float delta = a1 - a2;
    float cd = std::cos( delta );
    float sd = std::sin( delta );
    float D = 2.0f * m1 + m2 - m2 * std::cos( 2.0f * delta );

    accel1 = ( -g * ( 2.0f * m1 + m2 ) * std::sin( a1 ) - m2 * g * std::sin( a1 - 2.0f * a2 ) - 2.0f * sd * m2 * ( w2 * w2 * l2 + w1 * w1 * l1 * cd ) ) / ( l1 * D );

    accel2 = ( 2.0f * sd * ( w1 * w1 * l1 * ( m1 + m2 ) + g * ( m1 + m2 ) * std::cos( a1 ) + w2 * w2 * l2 * m2 * cd ) ) / ( l2 * D );
}

void DoublePendulum::Update( float deltaTime )
{
    // RK4 integration over state (a1, a2, w1, w2)
    auto deriv = [&]( float a1, float a2, float w1, float w2, float& da1, float& da2, float& dw1, float& dw2 )
    {
        da1 = w1;
        da2 = w2;

        float m1 = m_mass1, m2 = m_mass2;
        float l1 = m_length1, l2 = m_length2;
        float g = m_gravity;
        float delta = a1 - a2;
        float cd = std::cos( delta );
        float sd = std::sin( delta );
        float D = 2.0f * m1 + m2 - m2 * std::cos( 2.0f * delta );

        dw1 = ( -g * ( 2.0f * m1 + m2 ) * std::sin( a1 ) - m2 * g * std::sin( a1 - 2.0f * a2 ) - 2.0f * sd * m2 * ( w2 * w2 * l2 + w1 * w1 * l1 * cd ) ) / ( l1 * D );

        dw2 = ( 2.0f * sd * ( w1 * w1 * l1 * ( m1 + m2 ) + g * ( m1 + m2 ) * std::cos( a1 ) + w2 * w2 * l2 * m2 * cd ) ) / ( l2 * D );
    };

    float dt = deltaTime;
    float a1 = m_angle1, a2 = m_angle2, w1 = m_velocity1, w2 = m_velocity2;

    float k1_a1, k1_a2, k1_w1, k1_w2;
    deriv( a1, a2, w1, w2, k1_a1, k1_a2, k1_w1, k1_w2 );

    float k2_a1, k2_a2, k2_w1, k2_w2;
    deriv( a1 + 0.5f * dt * k1_a1, a2 + 0.5f * dt * k1_a2, w1 + 0.5f * dt * k1_w1, w2 + 0.5f * dt * k1_w2, k2_a1, k2_a2, k2_w1, k2_w2 );

    float k3_a1, k3_a2, k3_w1, k3_w2;
    deriv( a1 + 0.5f * dt * k2_a1, a2 + 0.5f * dt * k2_a2, w1 + 0.5f * dt * k2_w1, w2 + 0.5f * dt * k2_w2, k3_a1, k3_a2, k3_w1, k3_w2 );

    float k4_a1, k4_a2, k4_w1, k4_w2;
    deriv( a1 + dt * k3_a1, a2 + dt * k3_a2, w1 + dt * k3_w1, w2 + dt * k3_w2, k4_a1, k4_a2, k4_w1, k4_w2 );

    m_angle1 = a1 + ( dt / 6.0f ) * ( k1_a1 + 2.0f * k2_a1 + 2.0f * k3_a1 + k4_a1 );
    m_angle2 = a2 + ( dt / 6.0f ) * ( k1_a2 + 2.0f * k2_a2 + 2.0f * k3_a2 + k4_a2 );
    m_velocity1 = w1 + ( dt / 6.0f ) * ( k1_w1 + 2.0f * k2_w1 + 2.0f * k3_w1 + k4_w1 );
    m_velocity2 = w2 + ( dt / 6.0f ) * ( k1_w2 + 2.0f * k2_w2 + 2.0f * k3_w2 + k4_w2 );
}

} // namespace Physics
} // namespace SkullbonezCore

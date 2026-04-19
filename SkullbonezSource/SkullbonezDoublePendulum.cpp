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
DoublePendulum::DoublePendulum(float length1, float length2, float mass1, float mass2, float gravity)
    : m_length1(length1), m_length2(length2), m_mass1(mass1), m_mass2(mass2), m_gravity(gravity),
      m_angle1(0.5f), m_angle2(1.0f), m_velocity1(0.0f), m_velocity2(0.0f)
{
}

DoublePendulum::~DoublePendulum()
{
}

void DoublePendulum::SetInitialAngles(float angle1, float angle2)
{
  m_angle1 = angle1;
  m_angle2 = angle2;
}

void DoublePendulum::SetInitialVelocities(float velocity1, float velocity2)
{
  m_velocity1 = velocity1;
  m_velocity2 = velocity2;
}

float DoublePendulum::GetPosition1X(float originX) const
{
  return originX + m_length1 * std::sin(m_angle1);
}

float DoublePendulum::GetPosition1Y(float originY) const
{
  return originY + m_length1 * std::cos(m_angle1);
}

float DoublePendulum::GetPosition2X(float originX) const
{
  return GetPosition1X(originX) + m_length2 * std::sin(m_angle2);
}

float DoublePendulum::GetPosition2Y(float originY) const
{
  return GetPosition1Y(originY) + m_length2 * std::cos(m_angle2);
}

void DoublePendulum::ComputeAccelerations(float& accel1, float& accel2) const
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

  float dtheta = a2 - a1;
  float cdtheta = std::cos(dtheta);
  float sdtheta = std::sin(dtheta);

  float denom1 = (m1 + m2) * l1 - m2 * l1 * cdtheta * cdtheta;

  accel1 = (m2 * l1 * w1 * w1 * sdtheta * cdtheta + m2 * g * std::sin(a2) * cdtheta +
            m2 * l2 * w2 * w2 * sdtheta - (m1 + m2) * g * std::sin(a1)) /
           denom1;

  float denom2 = (m2 * l2 - m1 * l2 * cdtheta * cdtheta) / l1;
  accel2 = (-m2 * l2 * w2 * w2 * sdtheta * cdtheta - (m1 + m2) * g * std::sin(a1) * cdtheta +
            (m1 + m2) * l1 * w1 * w1 * sdtheta) /
           denom2;
}

void DoublePendulum::Update(float deltaTime)
{
  float accel1, accel2;
  ComputeAccelerations(accel1, accel2);

  m_velocity1 += accel1 * deltaTime;
  m_velocity2 += accel2 * deltaTime;

  m_angle1 += m_velocity1 * deltaTime;
  m_angle2 += m_velocity2 * deltaTime;
}

} // namespace Physics
} // namespace SkullbonezCore

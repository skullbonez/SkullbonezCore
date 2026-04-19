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

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_DOUBLE_PENDULUM_H
#define SKULLBONEZ_DOUBLE_PENDULUM_H

/* -- INCLUDES ------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Physics
{
/* -- Skullbonez Double Pendulum -----------------------------------------------------------------------

    Physics simulation for a double pendulum system.
    Uses Lagrangian mechanics to compute angular accelerations.

-------------------------------------------------------------------------------------------------*/
class DoublePendulum
{
private:
  float m_length1;      // Length of first rod
  float m_length2;      // Length of second rod
  float m_mass1;        // Mass of first bob
  float m_mass2;        // Mass of second bob
  float m_angle1;       // Angle of first rod (radians)
  float m_angle2;       // Angle of second rod (radians)
  float m_velocity1;    // Angular velocity of first rod
  float m_velocity2;    // Angular velocity of second rod
  float m_gravity;      // Gravity constant

public:
  DoublePendulum(float length1 = 1.0f, float length2 = 1.0f, float mass1 = 1.0f, float mass2 = 1.0f,
                 float gravity = 9.81f);
  ~DoublePendulum();

  void SetInitialAngles(float angle1, float angle2);
  void SetInitialVelocities(float velocity1, float velocity2);

  void Update(float deltaTime);

  float GetAngle1() const { return m_angle1; }
  float GetAngle2() const { return m_angle2; }
  float GetVelocity1() const { return m_velocity1; }
  float GetVelocity2() const { return m_velocity2; }

  float GetPosition1X(float originX) const;
  float GetPosition1Y(float originY) const;
  float GetPosition2X(float originX) const;
  float GetPosition2Y(float originY) const;

private:
  void ComputeAccelerations(float& accel1, float& accel2) const;
};
} // namespace Physics
} // namespace SkullbonezCore

#endif // SKULLBONEZ_DOUBLE_PENDULUM_H

#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezQuaternion.h"
#include "SkullbonezRotationMatrix.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;


namespace SkullbonezCore
{
namespace Physics
{
/* -- Rigid Body -------------------------------------------------------------------------------------------------------------------------------------------------

    A representation for a physical objects velocity, acceleration and position acted upon by an externally applied force.
    Takes orientation, angular velocity, angular acceleration, rotational intertia and torque into account.

    Layman map:
      - Position and linear velocity say where the body is and how fast it is
        sliding through the world.
      - Orientation and angular velocity say how it is rotated and how fast it
        is spinning.
      - Mass resists sliding changes; rotational inertia resists spin changes.
      - Forces are accumulated by the world/collision code, then integrated into
        velocity and position by the fixed-step physics loop.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RigidBody
{

  private:
    bool m_isForceApplied;           // Keeps track of whether an impulse force has been applied or not
    float m_mass;                    // Scalar representative of object m_mass									[Units: kg]
    float m_invertedMass;            // 1.0f / m_mass															[Units: kg]
    float m_coefficientRestitution;  // Scalar representative of the coefficient of restitution (bounciness) [Units: scalar]
    float m_frictionCoefficient;     // Scalar representative of the coefficient of friction (grip)			[Units: scalar]
    float m_volume;                  // Scalar representative of the m_volume of the body						[Units: m^3]
    Vector3 m_position;              // Vector representative of object m_position								[Units: point]
    Vector3 m_linearVelocity;        // Vector representative of object velocity								[Units: m/s]
    Vector3 m_linearAcceleration;    // Vector representative of object acceleration							[Units: m/s^2]
    Vector3 m_appliedForce;          // Vector representative of the sum of all forces						[Units: N]
    Vector3 m_worldForce;            // Linear forces acted upon the body by the world						[Units: N]
    Vector3 m_worldTorque;           // Angular forces acted upon the body by the world						[Units: Nm]
    Vector3 m_forceApplicationPoint; // Vector representative of the location of the force applied			[Units: point]
    Vector3 m_angularVelocity;       // Vector representative of the bodies angular velocity					[Units: radians/s]
    Vector3 m_angularAcceleration;   // Vector representative of the bodies angular acceleration				[Units: radians/s^2]
    Vector3 m_rotationalInertia;     // Diagonal of the 3×3 inertia tensor (off-diagonal terms are zero for symmetric bodies)  [Units: kg·m²]
    Vector3 m_torque;                // Vector representative of the bodies m_torque							[Units: Nm]
    /* m_changeInAngularVelocity / m_changeInLinearVelocity are DEFERRED IMPULSE BUFFERS.
       During collision resolution, both objects' velocity changes are computed first and
       stored here, then applied simultaneously via ApplyChange*Velocity(). This prevents
       the second object's response from being affected by the first's already-updated velocity,
       giving order-independent (symmetric) results. */
    Vector3 m_changeInAngularVelocity; // Buffered angular-velocity delta — staged here, applied via ApplyChangeInAngularVelocity() [Units: rad/s]
    Vector3 m_changeInLinearVelocity;  // Buffered linear-velocity delta  — staged here, applied via ApplyChangeInLinearVelocity()  [Units: m/s]
    Quaternion m_orientation;          // Quaternion representative of the m_orientation of the rigid body		[Units: Qrtn]

    void ApplyWorldForce(); // Applies continuous world forces (gravity) each frame: a = F/m, v += a
    /* NOTE: Despite being named "Force", both of the following apply ONE-SHOT IMPULSES
       (instantaneous velocity changes) rather than continuous forces. The impulse is
       consumed on the first call and ignored on subsequent calls (m_isForceApplied flag). */
    void ApplyLinearForce();   // Applies the buffered linear impulse: a = F/m, v += a
    void ApplyAngularForce();  // Applies the buffered angular impulse: τ = r×F, α = τ/I, ω += α
    Vector3 GetRollVelocity(); // Gets the linear velocity derived from rolling angular velocity (ω × ground normal)

  public:
    RigidBody();                                                                            // Default constructor
    ~RigidBody();                                                                           // Default destructor
    void ApplyForces();                                                                     // Update the rigid body's velocity based on its current state
    void UpdatePosition( float changeInTime );                                              // Update the rigid body's position based on its current state
    void ApplyImpulseForce();                                                               // Apply the impulse force to the body
    void ZeroForce();                                                                       // Zero the force vectors
    const Quaternion& GetOrientation() const;                                               // Returns the orientation quaternion
    void SetMass( float fMass );                                                            // Set the mass of the rigid body
    void SetFrictionCoefficient( float fFriction );                                         // Set the friction coefficient of the body
    void SetVolume( float fVolume );                                                        // Sets the volume member
    void SetCoefficientRestitution( float fCoefficientRestitution );                        // Set the coefficient of restitution (bounciness)
    void SetPosition( const Vector3& vPosition );                                           // Set the position of the rigid body
    void SetRotationalInertia( const Vector3& vRotationalInertia );                         // Sets the rotational inertia for the obect
    void SetChangeInAngularVelocity( const Vector3& vAngularVelocity );                     // Sets the change in angular velocity
    void SetChangeInLinearVelocity( const Vector3& vLinearVelocity );                       // Sets the change in linear velocity
    void ApplyChangeInAngularVelocity();                                                    // Applies the change in angular velocity
    void ThrottleAngularVelocity();                                                         // Slows angular velocity to ensure it does not reach astronomical speeds
    void ApplyChangeInLinearVelocity();                                                     // Applies the change in linear velocity
    float GetCoefficientRestitution();                                                      // Get the coefficient of restitution (bounciness)
    float GetFrictionCoefficient();                                                         // Get the friction coefficient of the body
    float GetMass();                                                                        // Returns the mass of the rigid body
    float GetInvertedMass();                                                                // Returns the inverted mass of the rigid body
    float GetVolume();                                                                      // Returns the volume of the rigid body
    const Vector3& GetVelocity();                                                           // Returns a const reference to the velocity of the rigid body
    const Vector3& GetPosition();                                                           // Returns a const reference to the position of the rigid body
    const Vector3& GetPosition() const;                                                     // Const center read for Catto-style contact-arm setup
    const Vector3& GetAngularVelocity();                                                    // Returns a const reference to the angular velocity of the rigid body
    const Vector3& GetRotationalInertia();                                                  // Returns a const reference to the rotational inertia of the rigid body
    float GetDensity();                                                                     // Calculates and returns the density of the body
    void SetLinearVelocity( const Vector3& vLinear );                                       // Set the linear velocity of the rigid body
    void SetAngularVelocity( const Vector3& vAngular );                                     // Set the angular velocity of the rigid body
    void SetOrientation( const Quaternion& q );                                             // Set the initial orientation quaternion directly
    void SetImpulseForce( const Vector3& vImpulseForce, const Vector3& vApplicationPoint ); // Set an impulse force to the rigid body
    void SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque );          // Sets the forces being acted upon the object by the world environment
    void UpdateRollPosition( float changeInTime, float circumference );                     // Update the rigid body's position when rolling (supply circumference of the body)
    RotationMatrix GetOrientationMatrix( float fTime = 0.0f );                              // Gets the rotation matrix representing the bodies orientation at the specified time (0.0f returns CURRENT orientation matrix)
};
} // namespace Physics
} // namespace SkullbonezCore

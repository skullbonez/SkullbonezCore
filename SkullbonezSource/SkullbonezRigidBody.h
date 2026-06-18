/*
File: SkullbonezSource/SkullbonezRigidBody.h
Purpose:
  Stores physical body state and integrates forces, impulses, velocity, and sleep hints.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezRigidBody.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezQuaternion.h"
#include "SkullbonezRotationMatrix.h"


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
    bool m_isForceApplied;                         // Keeps track of whether an impulse force has been applied or not
    float m_mass;                                  // Scalar representative of object m_mass									[Units: kg]
    float m_invertedMass;                          // 1.0f / m_mass															[Units: kg]
    float m_coefficientRestitution;                // Scalar representative of the coefficient of restitution (bounciness) [Units: scalar]
    float m_frictionCoefficient;                   // Scalar representative of the coefficient of friction (grip)			[Units: scalar]
    float m_volume;                                // Scalar representative of the m_volume of the body						[Units: m^3]
    Math::Vector::Vector3 m_position;              // Vector representative of object m_position								[Units: point]
    Math::Vector::Vector3 m_linearVelocity;        // Vector representative of object velocity								[Units: m/s]
    Math::Vector::Vector3 m_linearAcceleration;    // Vector representative of object acceleration							[Units: m/s^2]
    Math::Vector::Vector3 m_appliedForce;          // Vector representative of the sum of all forces						[Units: N]
    Math::Vector::Vector3 m_worldForce;            // Linear forces acted upon the body by the world						[Units: N]
    Math::Vector::Vector3 m_worldTorque;           // Angular forces acted upon the body by the world						[Units: Nm]
    Math::Vector::Vector3 m_forceApplicationPoint; // Vector representative of the location of the force applied			[Units: point]
    Math::Vector::Vector3 m_angularVelocity;       // Vector representative of the bodies angular velocity					[Units: radians/s]
    Math::Vector::Vector3 m_angularAcceleration;   // Vector representative of the bodies angular acceleration				[Units: radians/s^2]
    Math::Vector::Vector3 m_rotationalInertia;     // Diagonal of the 3x3 inertia tensor (off-diagonal terms are zero for symmetric bodies) [Units: kg*m^2]
    Math::Vector::Vector3 m_torque;                // Vector representative of the bodies m_torque							[Units: Nm]
    /* m_changeInAngularVelocity / m_changeInLinearVelocity are DEFERRED IMPULSE BUFFERS.
       During collision resolution, both objects' velocity changes are computed first and
       stored here, then applied simultaneously via ApplyChange*Velocity(). This prevents
       the second object's response from being affected by the first's already-updated velocity,
       giving order-independent (symmetric) results. */
    Math::Vector::Vector3 m_changeInAngularVelocity; // Solver-staged angular-velocity delta consumed after pair rows finish [Units: rad/s]
    Math::Vector::Vector3 m_changeInLinearVelocity;  // Solver-staged linear-velocity delta consumed after pair rows finish [Units: m/s]
    Math::Orientation::Quaternion m_orientation;     // Quaternion representative of the m_orientation of the rigid body		[Units: Qrtn]

    void ApplyWorldForce(); // Continuous world forces, such as gravity, update velocity through a = F/m.
    /* NOTE: Despite being named "Force", both of the following apply ONE-SHOT IMPULSES
       (instantaneous velocity changes) rather than continuous forces. The impulse is
       consumed on the first call and ignored on subsequent calls (m_isForceApplied flag). */
    void ApplyLinearForce();                 // Consumes the one-shot linear impulse as an immediate velocity delta.
    void ApplyAngularForce();                // Converts the one-shot force at its application point into angular velocity.
    Math::Vector::Vector3 GetRollVelocity(); // Rolling contribution derived from angular velocity around the contact normal.

  public:
    RigidBody();
    ~RigidBody();
    void ApplyForces();                        // Integrates accumulated world forces and one-shot impulses for one physics tick.
    void UpdatePosition( float changeInTime ); // changeInTime is seconds; advances pose from the current velocities.
    void ApplyImpulseForce();
    void ZeroForce(); // Clears accumulated force/torque after the tick consumes them.
    const Math::Orientation::Quaternion& GetOrientation() const;
    void SetMass( float fMass );
    void SetFrictionCoefficient( float fFriction );
    void SetVolume( float fVolume );
    void SetCoefficientRestitution( float fCoefficientRestitution );
    void SetPosition( const Math::Vector::Vector3& vPosition );
    void SetRotationalInertia( const Math::Vector::Vector3& vRotationalInertia );
    void SetChangeInAngularVelocity( const Math::Vector::Vector3& vAngularVelocity ); // Stages solver angular delta until simultaneous pair response is ready.
    void SetChangeInLinearVelocity( const Math::Vector::Vector3& vLinearVelocity );   // Stages solver linear delta until simultaneous pair response is ready.
    void ApplyChangeInAngularVelocity();                                              // Consumes the staged angular delta after all pair rows solve.
    void ThrottleAngularVelocity();                                                   // Caps spin to avoid destabilizing collision rows.
    void ApplyChangeInLinearVelocity();                                               // Consumes the staged linear delta after all pair rows solve.
    float GetCoefficientRestitution();
    float GetFrictionCoefficient();
    float GetMass();
    float GetInvertedMass();
    float GetVolume();
    const Math::Vector::Vector3& GetVelocity();
    const Math::Vector::Vector3& GetPosition();
    const Math::Vector::Vector3& GetPosition() const; // Const center read for Catto-style contact-arm setup
    const Math::Vector::Vector3& GetAngularVelocity();
    const Math::Vector::Vector3& GetRotationalInertia();
    float GetDensity(); // Density assumes mass and volume caches are already current.
    void SetLinearVelocity( const Math::Vector::Vector3& vLinear );
    void SetAngularVelocity( const Math::Vector::Vector3& vAngular );
    void SetOrientation( const Math::Orientation::Quaternion& q );
    void SetImpulseForce( const Math::Vector::Vector3& vImpulseForce, const Math::Vector::Vector3& vApplicationPoint );
    void SetWorldForce( const Math::Vector::Vector3& vWorldForce, const Math::Vector::Vector3& vWorldTorque ); // Continuous environment force/torque consumed by ApplyWorldForce().
    Math::Transformation::RotationMatrix GetOrientationMatrix( float fTime = 0.0f );                           // fTime=0 reads current pose; nonzero extrapolates from angular velocity.
};
} // namespace Physics
} // namespace SkullbonezCore

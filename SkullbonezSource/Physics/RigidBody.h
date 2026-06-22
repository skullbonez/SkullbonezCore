/*
File: SkullbonezSource/Physics/RigidBody.h
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
  Impulse: Instant velocity change used by collisions and one-shot forces.
  Restitution: Bounce coefficient used by the solver normal row.
  Friction: Tangential resistance coefficient used by contact rows.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/RigidBody.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Quaternion.h"
#include "../Maths/RotationMatrix.h"


namespace SkullbonezCore
{
namespace Physics
{
/* -- Rigid Body
-------------------------------------------------------------------------------------------------------------------------------------------------

    A representation for a physical objects velocity, acceleration and position acted upon by an externally applied
force. Takes orientation, angular velocity, angular acceleration, rotational intertia and torque into account.

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
    bool m_isForceApplied;                            // True once the one-shot impulse has been consumed.
    float m_mass;                                     // Units: kg.
    float m_invertedMass;                             // 1 / mass; zero for immovable bodies.
    float m_coefficientRestitution;                   // Bounce response; solver reads it during contact setup.
    float m_frictionCoefficient;                      // Tangential contact resistance shared by terrain/object contacts.
    float m_volume;                                   // Units: m^3.

    Math::Vector::Vector3 m_position;                 // Units: world point.
    Math::Vector::Vector3 m_linearVelocity;           // Units: m/s.
    Math::Vector::Vector3 m_linearAcceleration;       // Units: m/s^2.
    Math::Vector::Vector3 m_appliedForce;             // Units: N.
    Math::Vector::Vector3 m_worldForce;               // Units: N.
    Math::Vector::Vector3 m_worldTorque;              // Units: Nm.
    Math::Vector::Vector3 m_forceApplicationPoint;    // Units: world point.
    Math::Vector::Vector3 m_angularVelocity;          // Units: radians/s.
    Math::Vector::Vector3 m_angularAcceleration;      // Units: radians/s^2.
    Math::Vector::Vector3 m_rotationalInertia;        // Diagonal inertia tensor, units: kg*m^2.
    Math::Vector::Vector3 m_torque;                   // Units: Nm.
    /* m_changeInAngularVelocity / m_changeInLinearVelocity are DEFERRED IMPULSE BUFFERS.
       During collision resolution, both objects' velocity changes are computed first and
       stored here, then applied simultaneously via ApplyChange*Velocity(). This prevents
       the second object's response from being affected by the first's already-updated velocity,
       giving order-independent (symmetric) results. */
    // Solver-staged angular delta consumed after all pair rows finish.
    Math::Vector::Vector3 m_changeInAngularVelocity;
    // Solver-staged linear delta consumed after all pair rows finish.
    Math::Vector::Vector3 m_changeInLinearVelocity;
    Math::Orientation::Quaternion m_orientation;

    void ApplyWorldForce();                           // Continuous world forces, such as gravity, update velocity through a = F/m.
    /* NOTE: Despite being named "Force", both of the following apply ONE-SHOT IMPULSES
       (instantaneous velocity changes) rather than continuous forces. The impulse is
       consumed on the first call and ignored on subsequent calls (m_isForceApplied flag). */
    void ApplyLinearForce();                          // Consumes the one-shot linear impulse as an immediate velocity delta.
    void ApplyAngularForce();                         // One-shot off-center impulse changes angular velocity through its torque arm.
    // Rolling contribution derived from angular velocity around the contact normal.
    Math::Vector::Vector3 GetRollVelocity();

  public:
    RigidBody();
    ~RigidBody();
    void ApplyForces();                               // Integrates accumulated world forces and one-shot impulses for one physics tick.
    void UpdatePosition( float changeInTime );        // changeInTime is seconds; advances pose from the current velocities.
    void ApplyImpulseForce();
    void ZeroForce();                                 // End-of-tick cleanup after world forces and solver impulses are consumed.
    const Math::Orientation::Quaternion& GetOrientation() const;
    void SetMass( float fMass );
    void SetFrictionCoefficient( float fFriction );
    void SetVolume( float fVolume );
    void SetCoefficientRestitution( float fCoefficientRestitution );
    void SetPosition( const Math::Vector::Vector3& vPosition );
    void SetRotationalInertia( const Math::Vector::Vector3& vRotationalInertia );
    // Stages solver angular delta until simultaneous pair response is ready.
    void SetChangeInAngularVelocity( const Math::Vector::Vector3& vAngularVelocity );
    // Stages solver linear delta until simultaneous pair response is ready.
    void SetChangeInLinearVelocity( const Math::Vector::Vector3& vLinearVelocity );
    void ApplyChangeInAngularVelocity();              // Consumes the staged angular delta after all pair rows solve.
    void ThrottleAngularVelocity();                   // Caps spin to avoid destabilizing collision rows.
    void ApplyChangeInLinearVelocity();               // Consumes the staged linear delta after all pair rows solve.
    float GetCoefficientRestitution();
    float GetFrictionCoefficient();
    float GetMass();
    float GetInvertedMass();
    float GetVolume();
    const Math::Vector::Vector3& GetVelocity();
    const Math::Vector::Vector3& GetVelocity() const;
    const Math::Vector::Vector3& GetPosition();
    const Math::Vector::Vector3& GetPosition() const; // Const center read for Catto-style contact-arm setup
    const Math::Vector::Vector3& GetAngularVelocity();
    const Math::Vector::Vector3& GetAngularVelocity() const;
    const Math::Vector::Vector3& GetRotationalInertia();
    float GetDensity();                               // Density assumes mass and volume caches are already current.
    void SetLinearVelocity( const Math::Vector::Vector3& vLinear );
    void SetAngularVelocity( const Math::Vector::Vector3& vAngular );
    void SetOrientation( const Math::Orientation::Quaternion& q );
    void SetImpulseForce( const Math::Vector::Vector3& vImpulseForce, const Math::Vector::Vector3& vApplicationPoint );
    void ClearImpulseForce();                         // Clears any pending one-shot impulse and marks it consumed.
    // Continuous environment force/torque consumed by ApplyWorldForce().
    void SetWorldForce( const Math::Vector::Vector3& vWorldForce, const Math::Vector::Vector3& vWorldTorque );
    // fTime=0 reads current pose; nonzero extrapolates from angular velocity.
    Math::Transformation::RotationMatrix GetOrientationMatrix( float fTime = 0.0f );
};
} // namespace Physics
} // namespace SkullbonezCore

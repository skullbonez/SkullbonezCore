/*
File: SkullbonezSource/Physics/RigidBody.cpp
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
  - SkullbonezSource/Physics/RigidBody.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// RIGID BODY PHYSICS (RigidBody.cpp)
// =============================================================================
//
// PURPOSE: Simulates the physical motion of solid objects (spheres) including
// both LINEAR motion (sliding, falling) and ANGULAR motion (spinning, rolling).
//
// --- Physics Simulation Overview ---
//
//  Every frame, each rigid body goes through these steps:
//
//  1. ACCUMULATE FORCES: Gravity (world force) + collision impulses + friction
//  2. COMPUTE ACCELERATION: Newton's 2nd Law (F = ma → a = F/m)
//  3. UPDATE VELOCITY: v += a (velocity changes by acceleration)
//  4. UPDATE POSITION: x += v * dt (position changes by velocity over time)
//  5. UPDATE ORIENTATION: quaternion rotation about angular velocity axis
//
// --- Linear vs Angular ---
//
//  Linear (translation):           Angular (rotation):
//  - Force (N)                     - Torque (N·m)
//  - Mass (kg)                     - Rotational Inertia (kg·m²)
//  - Acceleration (m/s²)           - Angular Acceleration (rad/s²)
//  - Velocity (m/s)                - Angular Velocity (rad/s)
//  - Position (m)                  - Orientation (quaternion)
//
//  The SAME equations apply to both:
//  F = m·a  ←→  T = I·α  (torque = inertia × angular acceleration)
//
// --- Euler Integration ---
//
//  This engine uses SEMI-IMPLICIT EULER integration:
//  1. velocity += acceleration
//  2. position += velocity * dt
//
//  This is simple and stable enough for game physics (not scientific simulation).
//  More accurate methods (Runge-Kutta 4, Verlet) exist but add complexity.
//
// --- Quaternion Orientation ---
//
//  Instead of storing orientation as 3 Euler angles (which suffer from
//  "gimbal lock" — losing a degree of freedom), we use a QUATERNION.
//  Quaternions smoothly interpolate rotations without singularities.
//
//  Each frame: q_new = q_rotation(axis, angle) × q_old
//  Where axis = normalized angular velocity, angle = |omega| * dt
//
// =============================================================================


#include "RigidBody.h"


using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;


RigidBody::RigidBody()
{
    m_frictionCoefficient = 0.5f;
    m_invertedMass = 0.1f;
    m_coefficientRestitution = 0.9f;
    m_mass = 1.0f;
    m_volume = 1.0f;
    m_position = Vector::ZERO_VECTOR;
    m_linearVelocity = Vector::ZERO_VECTOR;
    m_angularVelocity = Vector::ZERO_VECTOR;
    m_changeInAngularVelocity = Vector::ZERO_VECTOR;
    m_rotationalInertia = Vector3( 1.0f, 1.0f, 1.0f );
    m_angularVelocityLimit = 5.0f;
    m_orientation.Identity();
}


RigidBody::~RigidBody()
{
}


void RigidBody::SetChangeInAngularVelocity( const Vector3& vAngularVelocity )
{
    m_changeInAngularVelocity = vAngularVelocity;
}


// Flush the deferred angular-velocity buffer into the live angular velocity,
// then clamp to prevent numerical explosion. The deferred buffer pattern ensures
// that when two objects collide simultaneously, both responses are computed from
// the pre-collision state before either body's ω is modified.
void RigidBody::ApplyChangeInAngularVelocity()
{
    m_angularVelocity += m_changeInAngularVelocity;
    m_changeInAngularVelocity.Zero();
    ThrottleAngularVelocity();
}


// Clamp angular velocity magnitude to prevent numerical explosion.
// Without this limit, a series of rapid collisions could accumulate
// unrealistically fast spin that destabilizes the simulation.
// Uses magnitude-based clamping so the limit is isotropic (axis-independent).
void RigidBody::ThrottleAngularVelocity()
{
    float magSq = m_angularVelocity.x * m_angularVelocity.x + m_angularVelocity.y * m_angularVelocity.y +
                  m_angularVelocity.z * m_angularVelocity.z;
    float limitSq = m_angularVelocityLimit * m_angularVelocityLimit;
    if ( magSq > limitSq )
    {
        float scale = m_angularVelocityLimit / sqrtf( magSq );
        m_angularVelocity.x *= scale;
        m_angularVelocity.y *= scale;
        m_angularVelocity.z *= scale;
    }
}


void RigidBody::SetChangeInLinearVelocity( const Vector3& vLinearVelocity )
{
    m_changeInLinearVelocity = vLinearVelocity;
}


// Flush the deferred linear-velocity buffer into the live linear velocity.
// See ApplyChangeInAngularVelocity() for why changes are buffered and applied
// in a separate step rather than directly during collision resolution.
void RigidBody::ApplyChangeInLinearVelocity()
{
    m_linearVelocity += m_changeInLinearVelocity;
    m_changeInLinearVelocity.Zero();
}


const Quaternion& RigidBody::GetOrientation() const
{
    return m_orientation;
}


// Convert angular velocity to translational (rolling) velocity.
// For a ball rolling on a flat surface:
//
//  Spin about Z axis → moves along X  (rollVelocity.x = omega.z)
//  Spin about X axis → moves along -Z (rollVelocity.z = -omega.x)
//  Spin about Y axis → no linear motion (spinning in place like a top)
//
//  Think of it like a car wheel:
//  - Wheel rotates about Z → car moves forward along X
//  - The cross product ω × r gives the contact velocity
//
Vector3 RigidBody::GetRollVelocity()
{
    // local for calculation
    Vector3 rollVelocity;

    // x == z
    rollVelocity.x = m_angularVelocity.z;

    // y == 0
    rollVelocity.y = 0.0f;

    // z == -x
    rollVelocity.z = -m_angularVelocity.x;

    return rollVelocity;
}


// Update position and orientation for FREE-FLIGHT (non-rolling) motion.
// Used when the ball is airborne (not in contact with terrain).
//
// Semi-implicit Euler integration:
//   position += linearVelocity × dt
//   orientation.rotate(axis=ω/|ω|, angle=|ω|×dt)
//
// Simplify() removes floating-point noise (tiny values ≈ 0) to prevent
// energy from accumulating in numerical artifacts.
void RigidBody::UpdatePosition( float changeInTime )
{
    // Zero out insignificant float noise
    m_linearVelocity.Simplify();
    m_angularVelocity.Simplify();

    // Linear: position += velocity × dt (basic kinematics)
    m_position += m_linearVelocity * changeInTime;

    // Angular: rotate quaternion about the omega axis by |omega|*dt radians.
    // Using axis-angle avoids gimbal lock that would occur with Euler angle decomposition.
    Vector3 omega = m_angularVelocity;
    float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    if ( omegaMag > 0.0001f )
    {
        Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
        m_orientation.RotateAboutAxis( axis, omegaMag * changeInTime );
    }
}


// Compute the orientation matrix at a future time (for rendering interpolation).
// If fTime=0, returns current orientation. Otherwise, predicts where the body
// will be oriented after fTime seconds of rotation at current angular velocity.
RotationMatrix RigidBody::GetOrientationMatrix( float fTime )
{
    if ( !fTime )
    {
        return m_orientation.GetOrientationMatrix();
    }
    else
    {
        Quaternion initialOrientation = m_orientation;
        Vector3 omega = m_angularVelocity;
        float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
        if ( omegaMag > 0.0001f )
        {
            Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
            initialOrientation.RotateAboutAxis( axis, omegaMag * fTime );
        }
        return initialOrientation.GetOrientationMatrix();
    }
}


const Vector3& RigidBody::GetRotationalInertia()
{
    return m_rotationalInertia;
}


void RigidBody::SetRotationalInertia( const Vector3& vRotationalInertia )
{
    // Each diagonal component I_x, I_y, I_z is used as a divisor when computing
    // angular acceleration: α = τ / I. A zero component would cause division by
    // zero in every subsequent physics step — guard against it here.
    if ( !vRotationalInertia.x || !vRotationalInertia.y || !vRotationalInertia.z )
    {
        throw std::runtime_error(
            "Rotational inertia cannot contain any components equal to zero!  (RigidBody::SetRotationalInertia)" );
    }

    m_rotationalInertia = vRotationalInertia;
}


const Vector3& RigidBody::GetAngularVelocity()
{
    return m_angularVelocity;
}


const Vector3& RigidBody::GetAngularVelocity() const
{
    return m_angularVelocity;
}


void RigidBody::SetMass( float fMass )
{
    if ( fMass <= 0.0f )
    {
        throw std::runtime_error( "Mass must be greater than zero!  (RigidBody::SetMass)" );
    }

    m_mass = fMass;
    m_invertedMass = 1.0f / m_mass;
}


float RigidBody::GetInvertedMass()
{
    return m_invertedMass;
}


void RigidBody::SetPosition( const Vector3& vPosition )
{
    m_position = vPosition;
}


void RigidBody::SetCoefficientRestitution( float fCoefficientRestitution )
{
    m_coefficientRestitution = fCoefficientRestitution;
}


float RigidBody::GetCoefficientRestitution() const
{
    return m_coefficientRestitution;
}


float RigidBody::GetMass()
{
    return m_mass;
}


const Vector3& RigidBody::GetPosition()
{
    return m_position;
}


const Vector3& RigidBody::GetPosition() const
{
    // ENGINE-SPECIFIC:
    //   Const position access supports read-only narrowphase manifold building.
    //   Catto-style contact rows need body centers to compute rA/rB, but the
    //   geometry pass must not mutate the rigid body while doing that setup.
    return m_position;
}


const Vector3& RigidBody::GetVelocity()
{
    return m_linearVelocity;
}


const Vector3& RigidBody::GetVelocity() const
{
    return m_linearVelocity;
}


void RigidBody::SetLinearVelocity( const Vector3& vLinear )
{
    m_linearVelocity = vLinear;
}


void RigidBody::SetAngularVelocity( const Vector3& vAngular )
{
    m_angularVelocity = vAngular;
}


void RigidBody::SetAngularVelocityLimit( float velocityLimit )
{
    m_angularVelocityLimit = (std::max)( 0.0f, velocityLimit );
}


void RigidBody::SetOrientation( const Quaternion& q )
{
    m_orientation = q;
}


void RigidBody::SetVolume( float fVolume )
{
    if ( fVolume <= 0.0f )
    {
        throw std::runtime_error( "Volume must be greater than zero!  (RigidBody::SetVolume)" );
    }

    m_volume = fVolume;
}


// Density = mass / volume (kg/m³). Used for buoyancy calculations —
// objects denser than water sink, lighter objects float.
float RigidBody::GetDensity()
{
    return m_mass / m_volume;
}


float RigidBody::GetVolume()
{
    return m_volume;
}


float RigidBody::GetFrictionCoefficient() const
{
    return m_frictionCoefficient;
}


// Friction coefficient: 1.0 = maximum grip (rubber on asphalt),
// 0.0 = no friction at all (ice). Controls how quickly tangential
// sliding velocity is converted to rolling velocity on contact.
void RigidBody::SetFrictionCoefficient( float fFriction )
{
    m_frictionCoefficient = fFriction;
}

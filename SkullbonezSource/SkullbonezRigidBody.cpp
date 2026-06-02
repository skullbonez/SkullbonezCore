// =============================================================================
// RIGID BODY PHYSICS (SkullbonezRigidBody.cpp)
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


// --- Includes ---
#include "SkullbonezRigidBody.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;


RigidBody::RigidBody()
{
    // set all members to default values
    m_frictionCoefficient = 0.5f;
    m_invertedMass = 0.1f;
    m_coefficientRestitution = 0.9f;
    m_mass = 1.0f;
    m_volume = 1.0f;
    m_isForceApplied = false;
    m_position = Vector::ZERO_VECTOR;
    m_linearVelocity = Vector::ZERO_VECTOR;
    m_linearAcceleration = Vector::ZERO_VECTOR;
    m_appliedForce = Vector::ZERO_VECTOR;
    m_forceApplicationPoint = Vector::ZERO_VECTOR;
    m_angularVelocity = Vector::ZERO_VECTOR;
    m_angularAcceleration = Vector::ZERO_VECTOR;
    m_torque = Vector::ZERO_VECTOR;
    m_worldForce = Vector::ZERO_VECTOR;
    m_worldTorque = Vector::ZERO_VECTOR;
    m_changeInAngularVelocity = Vector::ZERO_VECTOR;
    m_rotationalInertia = Vector3( 1.0f, 1.0f, 1.0f );
    m_orientation.Identity();
}


RigidBody::~RigidBody()
{
}


// Apply persistent world-space forces (e.g., gravity) to the body.
// These forces act continuously every frame (unlike impulse forces which are one-shot).
//
// Newton's 2nd Law: F = m·a  →  a = F/m
// After computing acceleration, we add it directly to velocity (semi-implicit Euler).
// The same applies to angular: torque/inertia = angular acceleration.
void RigidBody::ApplyWorldForce()
{
    // Linear: a = F/m, then v += a
    Vector3 worldLinearAcceleration = m_worldForce / m_mass;
    m_linearVelocity += worldLinearAcceleration;

    // Angular: α = τ/I, then ω += α
    // (component-wise division because we store inertia as a diagonal vector)
    Vector3 worldAngularAcceleration = m_worldTorque /
                                       m_rotationalInertia;
    m_angularVelocity += worldAngularAcceleration;
}


// Apply a one-shot linear impulse force to the body.
// Used for collision responses — the force is applied once and then consumed.
//
// --- Newton's 2nd Law ---
//
//  force = mass × acceleration
//  1 Newton = force needed to accelerate 1 kg at 1 m/s²
//
//  Rearranging: acceleration = force / mass
//
//  Example: A 2 kg ball hit with 10 N force → accelerates at 5 m/s²
//
void RigidBody::ApplyLinearForce()
{
    m_linearAcceleration = m_appliedForce / m_mass;
    m_linearVelocity += m_linearAcceleration;
}


// Apply a one-shot angular impulse (torque) to the body.
//
// --- How Torque Works ---
//
//  Torque is the ROTATIONAL equivalent of force. It's computed as:
//  τ = r × F  (cross product of application point and force)
//
//  The cross product gives a vector where:
//  - DIRECTION = the axis the body will rotate around
//  - MAGNITUDE = the rotational strength
//
//  Example: pushing a door at the handle (far from hinge) creates more torque
//  than pushing near the hinge, even with the same force.
//
//  ASCII diagram — torque from off-center force:
//
//     Force →→→→ applied here (off-center)
//         ↗
//        ⊙────────── center of mass
//        ↑
//    r (lever arm)
//
//  τ = r × F → body spins about the axis perpendicular to both r and F
//
//  Then angular acceleration: α = τ / I  (Newton's 2nd law, rotational form)
//
void RigidBody::ApplyAngularForce()
{
    // Torque = cross product of lever arm × force.
    // Direction = rotation axis; magnitude = rotational strength.
    m_torque = Vector::CrossProduct( m_forceApplicationPoint,
                                     m_appliedForce );

    // Angular acceleration = torque / inertia (rotational Newton's 2nd law: T = I·α → α = T/I)
    m_angularAcceleration = m_torque / m_rotationalInertia;

    // Accumulate angular velocity from this impulse
    m_angularVelocity += m_angularAcceleration;
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
    float magSq = m_angularVelocity.x * m_angularVelocity.x +
                  m_angularVelocity.y * m_angularVelocity.y +
                  m_angularVelocity.z * m_angularVelocity.z;
    float limitSq = Cfg().velocityLimit * Cfg().velocityLimit;
    if ( magSq > limitSq )
    {
        float scale = Cfg().velocityLimit / sqrtf( magSq );
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


void RigidBody::ApplyForces()
{
    // apply the world force
    ApplyWorldForce();

    // apply the impulse force
    ApplyImpulseForce();
}


void RigidBody::ApplyImpulseForce()
{
    // only apply an inpulse force once
    if ( m_isForceApplied )
    {
        return;
    }
    else
    {
        m_isForceApplied = true;
    }

    // apply linear impulse
    ApplyLinearForce();

    // apply angular impulse
    ApplyAngularForce();
}


const Quaternion& RigidBody::GetOrientation() const
{
    return m_orientation;
}


// Update position using "rolling" mechanics: the ball's circumference determines
// how far it moves per revolution. Also updates quaternion orientation.
//
// --- Rolling Without Slipping ---
//
//  A ball that rolls without slipping moves forward by one circumference (2πr)
//  per full revolution. So: displacement = (ω / 2π) × circumference × dt
//
//  This creates physically correct "treadmill" motion where spin directly
//  drives translation (used for ground contact).
//
void RigidBody::UpdateRollPosition( float changeInTime, float circumference )
{
    // Convert angular velocity from rad/s to revolutions/s (divide by 2π)
    Vector3 rollRevolutions = GetRollVelocity() / _2PI;

    // Displacement = revolutions × time × distance_per_revolution
    Vector3 positionUpdate = rollRevolutions * changeInTime * circumference;
    m_position += positionUpdate;

    // Update orientation quaternion: rotate about the angular velocity axis
    // by (|ω| × dt) radians. This avoids gimbal lock that Euler angles cause.
    Vector3 omega = m_angularVelocity;
    float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    if ( omegaMag > 0.0001f )
    {
        Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
        m_orientation.RotateAboutAxis( axis, omegaMag * changeInTime );
    }
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

    // return the result
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


void RigidBody::ZeroForce()
{
    m_appliedForce.Zero();
    m_forceApplicationPoint.Zero();
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
    if ( !vRotationalInertia.x ||
         !vRotationalInertia.y ||
         !vRotationalInertia.z )
    {
        throw std::runtime_error( "Rotational inertia cannot contain any components equal to zero!  (RigidBody::SetRotationalInertia)" );
    }

    m_rotationalInertia = vRotationalInertia;
}


void RigidBody::SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque )
{
    m_worldForce = vWorldForce;
    m_worldTorque = vWorldTorque;
}


void RigidBody::SetImpulseForce( const Vector3& vImpulseForce,
                                 const Vector3& vApplicationPoint )
{
    m_appliedForce = vImpulseForce;
    m_forceApplicationPoint = vApplicationPoint;
    m_isForceApplied = false;
}


const Vector3& RigidBody::GetAngularVelocity()
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


float RigidBody::GetCoefficientRestitution()
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


void RigidBody::SetLinearVelocity( const Vector3& vLinear )
{
    m_linearVelocity = vLinear;
}


void RigidBody::SetAngularVelocity( const Vector3& vAngular )
{
    m_angularVelocity = vAngular;
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


float RigidBody::GetFrictionCoefficient()
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

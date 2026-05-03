// --- Includes ---
#include "SkullbonezRigidBody.h"


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;


RigidBody::RigidBody()
{
    // set all members to default values
    m_frictionCoefficient = 0.1f;
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


void RigidBody::ApplyWorldForce()
{
    // World force/torque are pre-scaled by Δt in WorldEnvironment::AddWorldForces,
    // so dividing by m / I yields a velocity delta directly:
    //   Δv = (F·Δt) / m
    //   Δω = (τ·Δt) / I
    m_linearVelocity += m_worldForce / m_mass;
    m_angularVelocity += m_worldTorque / m_rotationalInertia;
}


void RigidBody::ApplyLinearForce()
{
    // Newton's 2nd law in impulse form (m_appliedForce is treated as an impulse J):
    //   Δv = J / m
    m_linearAcceleration = m_appliedForce / m_mass;
    m_linearVelocity += m_linearAcceleration;
}


void RigidBody::ApplyAngularForce()
{
    // Rotational analogue of F = ma (applied force is again treated as an impulse):
    //   τ  = r × F
    //   Δω = τ / I
    m_torque = Vector::CrossProduct( m_forceApplicationPoint, m_appliedForce );
    m_angularAcceleration = m_torque / m_rotationalInertia;
    m_angularVelocity += m_angularAcceleration;
}


void RigidBody::SetChangeInAngularVelocity( const Vector3& vAngularVelocity )
{
    m_changeInAngularVelocity = vAngularVelocity;
}


void RigidBody::ApplyChangeInAngularVelocity()
{
    m_angularVelocity += m_changeInAngularVelocity;
    m_changeInAngularVelocity.Zero();
    ThrottleAngularVelocity();
}


void RigidBody::ThrottleAngularVelocity()
{
    // Component-wise clamp:  ω_i ← clamp(ω_i, -L, +L)
    const float L = Cfg().velocityLimit;
    auto clamp = []( float v, float lim ) { return v > lim ? lim : ( v < -lim ? -lim : v ); };
    m_angularVelocity.x = clamp( m_angularVelocity.x, L );
    m_angularVelocity.y = clamp( m_angularVelocity.y, L );
    m_angularVelocity.z = clamp( m_angularVelocity.z, L );
}


void RigidBody::SetChangeInLinearVelocity( const Vector3& vLinearVelocity )
{
    m_changeInLinearVelocity = vLinearVelocity;
}


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


void RigidBody::UpdateRollPosition( float changeInTime, float circumference )
{
    // No-slip rolling on the XZ plane:
    //   Δx = (ω̃ / 2π) · C · Δt        where C = 2πr  →   Δx = ω̃·r·Δt
    // (ω̃ = GetRollVelocity() — ω with X/Z components remapped to ground motion.)
    m_position += GetRollVelocity() * ( changeInTime * circumference / _2PI );
    m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
}


Vector3 RigidBody::GetRollVelocity()
{
    // Map ω → ground-plane direction (Y up; rolling produces no Y component):
    //   ω̃ = ( ω.z, 0, -ω.x )
    return Vector3( m_angularVelocity.z, 0.0f, -m_angularVelocity.x );
}


void RigidBody::UpdatePosition( float changeInTime )
{
    m_linearVelocity.Simplify();
    m_angularVelocity.Simplify();

    // Forward-Euler integration:
    //   x ← x + v·Δt
    //   q ← q · Δq(ω·Δt)
    m_position += m_linearVelocity * changeInTime;
    m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
}


void RigidBody::ZeroForce()
{
    m_appliedForce.Zero();
    m_forceApplicationPoint.Zero();
}


RotationMatrix RigidBody::GetOrientationMatrix( float fTime )
{
    if ( !fTime )
    {
        return m_orientation.GetOrientationMatrix();
    }
    else
    {
        Quaternion initialOrientation = m_orientation;
        initialOrientation.RotateAboutXYZ( m_angularVelocity * fTime );
        return initialOrientation.GetOrientationMatrix();
    }
}


const Vector3& RigidBody::GetRotationalInertia()
{
    return m_rotationalInertia;
}


void RigidBody::SetRotationalInertia( const Vector3& vRotationalInertia )
{
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


void RigidBody::SetVolume( float fVolume )
{
    if ( fVolume <= 0.0f )
    {
        throw std::runtime_error( "Volume must be greater than zero!  (RigidBody::SetVolume)" );
    }

    m_volume = fVolume;
}


float RigidBody::GetDensity()
{
    // ρ = m / V
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


void RigidBody::SetFrictionCoefficient( float fFriction )
{
    // 1 is grippy, 0.0f is no grip
    m_frictionCoefficient = fFriction;
}


void RigidBody::DampenAngularVelocity()
{
    // ω ← ω · (1 - 5μ)
    m_angularVelocity *= 1.0f - ( m_frictionCoefficient * 5.0f );
}

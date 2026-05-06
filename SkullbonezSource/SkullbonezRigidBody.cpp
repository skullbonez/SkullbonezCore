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
    // find acceleration (a = F/m)
    Vector3 worldLinearAcceleration = m_worldForce / m_mass;

    // add to the linear velocity
    m_linearVelocity += worldLinearAcceleration;

    // find acceleration (a = m_torque/m_rotationalInertia)
    Vector3 worldAngularAcceleration = m_worldTorque /
                                       m_rotationalInertia;

    // add to the angular velocity
    m_angularVelocity += worldAngularAcceleration;
}


void RigidBody::ApplyLinearForce()
{
    /*
        Calculate linear dynamics...

        calculate acceleration:
        force = m_mass * acceleration
        where m_mass is measured in kg,
        acceleration in m/s^2, and force in newtons

        eg: 1N = 1kg * 1m/s^2
        (one newton is the force required to accelerate
        one kilogram one metre per second squared)

        based on rearranging the above: acceleration = force / m_mass
    */
    m_linearAcceleration = m_appliedForce / m_mass;

    // calculate velocity
    m_linearVelocity += m_linearAcceleration;
}


void RigidBody::ApplyAngularForce()
{
    /*
        Translation from linear to rotational terms:

        Force			--> Torque (T)
        Mass			--> Rotational Inertia (I)
        Acceleration	--> Rotational Acceleration (b)

        Torque is found by taking the perpendicular between the application point
        and the force applied.  This returns a vector where the direction is the
        axis the body will rotate on, and the magnitude is the strength of the force.

        The angular acceleration is found based on Newton's 2nd law:

        F = ma   -->  T = Ib
        a = F/m  -->  b = T/I
    */
    m_torque = Vector::CrossProduct( m_forceApplicationPoint,
                                     m_appliedForce );

    // find acceleration (a = m_torque/m_rotationalInertia)
    m_angularAcceleration = m_torque / m_rotationalInertia;

    // now add the angular velocity accumulated from this impulse
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
    if ( m_angularVelocity.x > Cfg().velocityLimit )
    {
        m_angularVelocity.x = Cfg().velocityLimit;
    }
    else if ( m_angularVelocity.x < -Cfg().velocityLimit )
    {
        m_angularVelocity.x = -Cfg().velocityLimit;
    }

    if ( m_angularVelocity.y > Cfg().velocityLimit )
    {
        m_angularVelocity.y = Cfg().velocityLimit;
    }
    else if ( m_angularVelocity.y < -Cfg().velocityLimit )
    {
        m_angularVelocity.y = -Cfg().velocityLimit;
    }

    if ( m_angularVelocity.z > Cfg().velocityLimit )
    {
        m_angularVelocity.z = Cfg().velocityLimit;
    }
    else if ( m_angularVelocity.z < -Cfg().velocityLimit )
    {
        m_angularVelocity.z = -Cfg().velocityLimit;
    }
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
    // get roll velocity in terms of full revolutions (radians/2PI)
    Vector3 rollRevolutions = GetRollVelocity() / _2PI;

    // scale the roll velocity to a displacement based on change in time
    // and circumference
    Vector3 positionUpdate = rollRevolutions * changeInTime * circumference;

    // update the m_position
    m_position += positionUpdate;

    Vector3 omega = m_angularVelocity;
    float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    if ( omegaMag > 0.0001f )
    {
        Vector3 axis( omega.x / omegaMag, omega.y / omegaMag, omega.z / omegaMag );
        m_orientation.RotateAboutAxis( axis, omegaMag * changeInTime );
    }
}


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


void RigidBody::UpdatePosition( float changeInTime )
{
    // get rid of tiny float values
    m_linearVelocity.Simplify();
    m_angularVelocity.Simplify();

    // calculate location based on current linear velocity
    m_position += m_linearVelocity * changeInTime;

    // World-frame integration: single rotation about the omega axis.
    // Avoids Euler decomposition (RotateAboutXYZ) which causes gimbal-lock
    // artifacts when omega has multiple non-zero components.
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


float RigidBody::GetDensity()
{
    // calculate the density
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

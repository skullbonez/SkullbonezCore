/*-----------------------------------------------------------------------------------
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
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezRigidBody.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
RigidBody::RigidBody( void )
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

/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
RigidBody::~RigidBody()
{
}

/* -- APPLY WORLD FORCES ----------------------------------------------------------*/
void RigidBody::ApplyWorldForce( void )
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

/* -- APPLY LINEAR FORCE ----------------------------------------------------------*/
void RigidBody::ApplyLinearForce( void )
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

/* -- APPLY ANGULAR FORCE ---------------------------------------------------------*/
void RigidBody::ApplyAngularForce( void )
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

/* -- SET CHANGE IN ANGULAR VELOCITY ----------------------------------------------*/
void RigidBody::SetChangeInAngularVelocity( const Vector3& vAngularVelocity )
{
    m_changeInAngularVelocity = vAngularVelocity;
}

/* -- APPLY CHANGE IN ANGULAR VELOCITY --------------------------------------------*/
void RigidBody::ApplyChangeInAngularVelocity( void )
{
    m_angularVelocity += m_changeInAngularVelocity;
    m_changeInAngularVelocity.Zero();
    this->ThrottleAngularVelocity();
}

/* -- THROTTLE ANGULAR VELOCITY ---------------------------------------------------*/
void RigidBody::ThrottleAngularVelocity( void )
{
    if ( m_angularVelocity.x > VELOCITY_LIMIT )
    {
        m_angularVelocity.x = VELOCITY_LIMIT;
    }
    else if ( m_angularVelocity.x < -VELOCITY_LIMIT )
    {
        m_angularVelocity.x = -VELOCITY_LIMIT;
    }

    if ( m_angularVelocity.y > VELOCITY_LIMIT )
    {
        m_angularVelocity.y = VELOCITY_LIMIT;
    }
    else if ( m_angularVelocity.y < -VELOCITY_LIMIT )
    {
        m_angularVelocity.y = -VELOCITY_LIMIT;
    }

    if ( m_angularVelocity.z > VELOCITY_LIMIT )
    {
        m_angularVelocity.z = VELOCITY_LIMIT;
    }
    else if ( m_angularVelocity.z < -VELOCITY_LIMIT )
    {
        m_angularVelocity.z = -VELOCITY_LIMIT;
    }
}

/* -- SET CHANGE IN LINEAR VELOCITY -----------------------------------------------*/
void RigidBody::SetChangeInLinearVelocity( const Vector3& vLinearVelocity )
{
    m_changeInLinearVelocity = vLinearVelocity;
}

/* -- APPLY CHANGE IN LINEAR VELOCITY ---------------------------------------------*/
void RigidBody::ApplyChangeInLinearVelocity( void )
{
    m_linearVelocity += m_changeInLinearVelocity;
    m_changeInLinearVelocity.Zero();
}

/* -- UPDATE VELOCITY -------------------------------------------------------------*/
void RigidBody::ApplyForces( void )
{
    // apply the world force
    this->ApplyWorldForce();

    // apply the impulse force
    this->ApplyImpulseForce();
}

/* -- APPLY IMPULSE FORCE ---------------------------------------------------------*/
void RigidBody::ApplyImpulseForce( void )
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
    this->ApplyLinearForce();

    // apply angular impulse
    this->ApplyAngularForce();
}

/* -- GET ORIENTATION -------------------------------------------------------------*/
const Quaternion& RigidBody::GetOrientation( void ) const
{
    return m_orientation;
}

/* -- UPDATE ROLL POSITION --------------------------------------------------------*/
void RigidBody::UpdateRollPosition( float changeInTime, float circumference )
{
    // get roll velocity in terms of full revolutions (radians/2PI)
    Vector3 rollRevolutions = this->GetRollVelocity() / _2PI;

    // scale the roll velocity to a displacement based on change in time
    // and circumference
    Vector3 positionUpdate = rollRevolutions * changeInTime * circumference;

    // update the m_position
    m_position += positionUpdate;

    // update the m_orientation based on current angular velocity
    m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
}

/* -- GET ROLL VELOCITY -----------------------------------------------------------*/
Vector3 RigidBody::GetRollVelocity( void )
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

/* -- UPDATE POSITION -------------------------------------------------------------*/
void RigidBody::UpdatePosition( float changeInTime )
{
    // get rid of tiny float values
    m_linearVelocity.Simplify();
    m_angularVelocity.Simplify();

    // calculate location based on current linear velocity
    m_position += m_linearVelocity * changeInTime;

    // update the m_orientation based on current angular velocity
    m_orientation.RotateAboutXYZ( m_angularVelocity * changeInTime );
}

/* -- ZERO FORCE ------------------------------------------------------------------*/
void RigidBody::ZeroForce( void )
{
    m_appliedForce.Zero();
    m_forceApplicationPoint.Zero();
}

/* -- GET ORIENTATION -------------------------------------------------------------*/
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

/* -- GET ROTATIONAL INERTIA ------------------------------------------------------*/
const Vector3& RigidBody::GetRotationalInertia( void )
{
    return m_rotationalInertia;
}

/* -- SET ROTATIONAL INERTIA ------------------------------------------------------*/
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

/* -- SET WORLD FORCE --------------------------------------------------------------*/
void RigidBody::SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque )
{
    m_worldForce = vWorldForce;
    m_worldTorque = vWorldTorque;
}

/* -- APPLY FORCE ------------------------------------------------------------------*/
void RigidBody::SetImpulseForce( const Vector3& vImpulseForce,
                                 const Vector3& vApplicationPoint )
{
    m_appliedForce = vImpulseForce;
    m_forceApplicationPoint = vApplicationPoint;
    m_isForceApplied = false;
}

/* -- GET ANGULAR VELOCITY ---------------------------------------------------------*/
const Vector3& RigidBody::GetAngularVelocity( void )
{
    return m_angularVelocity;
}

/* -- SET MASS ---------------------------------------------------------------------*/
void RigidBody::SetMass( float fMass )
{
    if ( fMass <= 0.0f )
    {
        throw std::runtime_error( "Mass must be greater than zero!  (RigidBody::SetMass)" );
    }

    m_mass = fMass;
    m_invertedMass = 1.0f / m_mass;
}

/* -- GET INVERTED MASS -------------------------------------------------------------*/
float RigidBody::GetInvertedMass( void )
{
    return m_invertedMass;
}

/* -- SET POSITION ------------------------------------------------------------------*/
void RigidBody::SetPosition( const Vector3& vPosition )
{
    m_position = vPosition;
}

/* -- SET COEFFICIENT OF RESTITUTION ------------------------------------------------*/
void RigidBody::SetCoefficientRestitution( float fCoefficientRestitution )
{
    m_coefficientRestitution = fCoefficientRestitution;
}

/* -- GET COEFFICIENT OF RESTITUTION ------------------------------------------------*/
float RigidBody::GetCoefficientRestitution( void )
{
    return m_coefficientRestitution;
}

/* -- GET MASS ----------------------------------------------------------------------*/
float RigidBody::GetMass( void )
{
    return m_mass;
}

/* -- GET POSITION ------------------------------------------------------------------*/
const Vector3& RigidBody::GetPosition( void )
{
    return m_position;
}

/* -- GET VELOCITY ------------------------------------------------------------------*/
const Vector3& RigidBody::GetVelocity( void )
{
    return m_linearVelocity;
}

/* -- SET LINEAR VELOCITY -----------------------------------------------------------*/
void RigidBody::SetLinearVelocity( const Vector3& vLinear )
{
    m_linearVelocity = vLinear;
}

/* -- SET ANGULAR VELOCITY ----------------------------------------------------------*/
void RigidBody::SetAngularVelocity( const Vector3& vAngular )
{
    m_angularVelocity = vAngular;
}

/* -- SET VOLUME --------------------------------------------------------------------*/
void RigidBody::SetVolume( float fVolume )
{
    if ( fVolume <= 0.0f )
    {
        throw std::runtime_error( "Volume must be greater than zero!  (RigidBody::SetVolume)" );
    }

    m_volume = fVolume;
}

/* -- GET DENSITY -------------------------------------------------------------------*/
float RigidBody::GetDensity( void )
{
    // calculate the density
    return m_mass / m_volume;
}

/* -- GET VOLUME --------------------------------------------------------------------*/
float RigidBody::GetVolume( void )
{
    return m_volume;
}

/* -- GET FRICTION COEFFICIENT ------------------------------------------------------*/
float RigidBody::GetFrictionCoefficient( void )
{
    return m_frictionCoefficient;
}

/* -- SET FRICTION COEFFICIENT ------------------------------------------------------*/
void RigidBody::SetFrictionCoefficient( float fFriction )
{
    // 1 is grippy, 0.0f is no grip
    m_frictionCoefficient = fFriction;
}

/* -- DAMPEN ANGULAR VELOCITY -------------------------------------------------------*/
void RigidBody::DampenAngularVelocity( void )
{
    m_angularVelocity *= 1.0f - ( m_frictionCoefficient * 5.0f );
}
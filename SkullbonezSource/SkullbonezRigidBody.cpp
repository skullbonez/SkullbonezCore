/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ¦   .--. .--.   ¦
								   ¦ )/   ¦ ¦   \( ¦
								   ¦/ \__/   \__/ \¦
								   /      /^\      \
								   \__    '='    __/
								   	 ¦\         /¦
									 ¦\'"VUUUV"'/¦
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
RigidBody::RigidBody(void)
{
	// set all members to default values
	this->frictionCoefficient		= 0.1f;
	this->invertedMass				= 0.1f;
	this->coefficientRestitution	= 0.9f;
	this->mass						= 1.0f;
	this->volume					= 1.0f;
	this->isForceApplied			= false;
	this->position					= Vector::ZERO_VECTOR;
	this->linearVelocity			= Vector::ZERO_VECTOR;
	this->linearAcceleration		= Vector::ZERO_VECTOR;
	this->appliedForce				= Vector::ZERO_VECTOR;
	this->forceApplicationPoint		= Vector::ZERO_VECTOR;
	this->angularVelocity			= Vector::ZERO_VECTOR;
	this->angularAcceleration		= Vector::ZERO_VECTOR;
	this->torque					= Vector::ZERO_VECTOR;
	this->worldForce				= Vector::ZERO_VECTOR;
	this->worldTorque				= Vector::ZERO_VECTOR;
	this->changeInAngularVelocity	= Vector::ZERO_VECTOR;
	this->rotationalInertia			= Vector3(1.0f, 1.0f, 1.0f);
	this->orientation.Identity();
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
RigidBody::~RigidBody() {}



/* -- APPLY WORLD FORCES ----------------------------------------------------------*/
void RigidBody::ApplyWorldForce(void)
{
	// find acceleration (a = F/m)
	Vector3 worldLinearAcceleration = this->worldForce / this->mass;

	// add to the linear velocity
	this->linearVelocity += worldLinearAcceleration;

	// find acceleration (a = torque/rotationalInertia)
	Vector3 worldAngularAcceleration = this->worldTorque / 
									   this->rotationalInertia;

	// add to the angular velocity
	this->angularVelocity += worldAngularAcceleration;
}



/* -- APPLY LINEAR FORCE ----------------------------------------------------------*/
void RigidBody::ApplyLinearForce(void)
{
	/*
		Calculate linear dynamics...

		calculate acceleration:
		force = mass * acceleration
		where mass is measured in kg,
		acceleration in m/s^2, and force in newtons
		
		eg: 1N = 1kg * 1m/s^2
		(one newton is the force required to accelerate
		one kilogram one metre per second squared)

		based on rearranging the above: acceleration = force / mass
	*/
	this->linearAcceleration = this->appliedForce / this->mass;

	// calculate velocity
	this->linearVelocity += this->linearAcceleration;
}



/* -- APPLY ANGULAR FORCE ---------------------------------------------------------*/
void RigidBody::ApplyAngularForce(void)
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
	this->torque = Vector::CrossProduct(this->forceApplicationPoint, 
										this->appliedForce);

	// find acceleration (a = torque/rotationalInertia)
	this->angularAcceleration = this->torque / this->rotationalInertia;

	// now add the angular velocity accumulated from this impulse
	this->angularVelocity += this->angularAcceleration;
}



/* -- SET CHANGE IN ANGULAR VELOCITY ----------------------------------------------*/
void RigidBody::SetChangeInAngularVelocity(const Vector3& vAngularVelocity)
{
	this->changeInAngularVelocity = vAngularVelocity;
}



/* -- APPLY CHANGE IN ANGULAR VELOCITY --------------------------------------------*/
void RigidBody::ApplyChangeInAngularVelocity(void)
{
	this->angularVelocity += this->changeInAngularVelocity;
	this->changeInAngularVelocity.Zero();
	this->ThrottleAngularVelocity();
}



/* -- THROTTLE ANGULAR VELOCITY ---------------------------------------------------*/
void RigidBody::ThrottleAngularVelocity(void)
{
	if(this->angularVelocity.x > VELOCITY_LIMIT) 
		this->angularVelocity.x = VELOCITY_LIMIT;
	else if(this->angularVelocity.x < -VELOCITY_LIMIT)
		this->angularVelocity.x = -VELOCITY_LIMIT;

	if(this->angularVelocity.y > VELOCITY_LIMIT) 
		this->angularVelocity.y = VELOCITY_LIMIT;
	else if(this->angularVelocity.y < -VELOCITY_LIMIT)
		this->angularVelocity.y = -VELOCITY_LIMIT;

	if(this->angularVelocity.z > VELOCITY_LIMIT) 
		this->angularVelocity.z = VELOCITY_LIMIT;
	else if(this->angularVelocity.z < -VELOCITY_LIMIT)
		this->angularVelocity.z = -VELOCITY_LIMIT;
}



/* -- SET CHANGE IN LINEAR VELOCITY -----------------------------------------------*/
void RigidBody::SetChangeInLinearVelocity(const Vector3& vLinearVelocity)
{
	this->changeInLinearVelocity = vLinearVelocity;
}



/* -- APPLY CHANGE IN LINEAR VELOCITY ---------------------------------------------*/
void RigidBody::ApplyChangeInLinearVelocity(void)
{
	this->linearVelocity += this->changeInLinearVelocity;
	this->changeInLinearVelocity.Zero();
}



/* -- UPDATE VELOCITY -------------------------------------------------------------*/
void RigidBody::ApplyForces(void)
{
	// apply the world force
	this->ApplyWorldForce();

	// apply the impulse force
	this->ApplyImpulseForce();
}



/* -- APPLY IMPULSE FORCE ---------------------------------------------------------*/
void RigidBody::ApplyImpulseForce(void)
{
	// only apply an inpulse force once
	if(this->isForceApplied)	return;
	else this->isForceApplied = true;

	// apply linear impulse
	this->ApplyLinearForce();

	// apply angular impulse
	this->ApplyAngularForce();
}



/* -- ROTATE BODY TO ORIENTATION --------------------------------------------------*/
void RigidBody::RotateBodyToOrientation(void)
{
	this->orientation.GlRotateToOrientation();
}



/* -- UPDATE ROLL POSITION --------------------------------------------------------*/
void RigidBody::UpdateRollPosition(float changeInTime, float circumference)
{
	// get roll velocity in terms of full revolutions (radians/2PI)
	Vector3 rollRevolutions = this->GetRollVelocity() / _2PI;

	// scale the roll velocity to a displacement based on change in time 
	// and circumference
	Vector3 positionUpdate = rollRevolutions * changeInTime * circumference;

	// update the position
	this->position += positionUpdate;

	// update the orientation based on current angular velocity
	this->orientation.RotateAboutXYZ(this->angularVelocity * changeInTime);
}



/* -- GET ROLL VELOCITY -----------------------------------------------------------*/
Vector3 RigidBody::GetRollVelocity(void)
{
	// local for calculation
	Vector3 rollVelocity;

	// x == z
	rollVelocity.x = this->angularVelocity.z;

	// y == 0
	rollVelocity.y = 0.0f;

	// z == -x
	rollVelocity.z = -this->angularVelocity.x;

	// return the result
	return rollVelocity;
}



/* -- UPDATE POSITION -------------------------------------------------------------*/
void RigidBody::UpdatePosition(float changeInTime)
{
	// get rid of tiny float values
	this->linearVelocity.Simplify();
	this->angularVelocity.Simplify();

	// calculate location based on current linear velocity
	this->position += this->linearVelocity * changeInTime;

	// update the orientation based on current angular velocity
	this->orientation.RotateAboutXYZ(this->angularVelocity * changeInTime);
}



/* -- ZERO FORCE ------------------------------------------------------------------*/
void RigidBody::ZeroForce(void)
{
	this->appliedForce.Zero();
	this->forceApplicationPoint.Zero();
}



/* -- GET ORIENTATION -------------------------------------------------------------*/
RotationMatrix RigidBody::GetOrientationMatrix(float fTime)
{
	if(!fTime)
	{
		return this->orientation.GetOrientationMatrix();
	}
	else
	{
		Quaternion initialOrientation = this->orientation;
		initialOrientation.RotateAboutXYZ(this->angularVelocity * fTime);
		return initialOrientation.GetOrientationMatrix();
	}
}



/* -- GET ROTATIONAL INERTIA ------------------------------------------------------*/
const Vector3& RigidBody::GetRotationalInertia(void)
{
	return this->rotationalInertia;
}



/* -- SET ROTATIONAL INERTIA ------------------------------------------------------*/
void RigidBody::SetRotationalInertia(const Vector3& vRotationalInertia)
{
	if(!vRotationalInertia.x || 
	   !vRotationalInertia.y || 
	   !vRotationalInertia.z)
	{
		throw "Rotational inertia cannot contain any components equal to zero!  "
			  "(RigidBody::SetRotationalInertia)";
	}

	this->rotationalInertia = vRotationalInertia;
}



/* -- SET WORLD FORCE --------------------------------------------------------------*/
void RigidBody::SetWorldForce(const Vector3& vWorldForce, const Vector3& vWorldTorque)
{
	this->worldForce = vWorldForce;
	this->worldTorque = vWorldTorque;
}



/* -- APPLY FORCE ------------------------------------------------------------------*/
void RigidBody::SetImpulseForce(const Vector3& vImpulseForce, 
								const Vector3& vApplicationPoint)
{
	this->appliedForce			= vImpulseForce;
	this->forceApplicationPoint = vApplicationPoint;
	this->isForceApplied		= false;
}



/* -- GET ANGULAR VELOCITY ---------------------------------------------------------*/
const Vector3& RigidBody::GetAngularVelocity(void)
{
	return this->angularVelocity;
}



/* -- SET MASS ---------------------------------------------------------------------*/
void RigidBody::SetMass(float fMass)
{
	if(fMass <= 0.0f) 
	{
		throw "Mass must be greater than zero!  (RigidBody::SetMass)";
	}

	this->mass = fMass;
	this->invertedMass = 1.0f / this->mass;
}



/* -- GET INVERTED MASS -------------------------------------------------------------*/
float RigidBody::GetInvertedMass(void)
{
	return this->invertedMass;
}



/* -- SET POSITION ------------------------------------------------------------------*/
void RigidBody::SetPosition(const Vector3& vPosition)
{
	this->position = vPosition;
}



/* -- SET COEFFICIENT OF RESTITUTION ------------------------------------------------*/
void RigidBody::SetCoefficientRestitution(float fCoefficientRestitution)
{
	this->coefficientRestitution = fCoefficientRestitution;
}



/* -- GET COEFFICIENT OF RESTITUTION ------------------------------------------------*/
float RigidBody::GetCoefficientRestitution(void)
{
	return this->coefficientRestitution;
}



/* -- GET MASS ----------------------------------------------------------------------*/
float RigidBody::GetMass(void)
{
	return this->mass;
}



/* -- GET POSITION ------------------------------------------------------------------*/
const Vector3& RigidBody::GetPosition(void)
{
	return this->position;
}



/* -- GET VELOCITY ------------------------------------------------------------------*/
const Vector3& RigidBody::GetVelocity(void)
{
	return this->linearVelocity;
}



/* -- SET LINEAR VELOCITY -----------------------------------------------------------*/
void RigidBody::SetLinearVelocity(const Vector3& vLinear)
{
	this->linearVelocity = vLinear;
}



/* -- SET ANGULAR VELOCITY ----------------------------------------------------------*/
void RigidBody::SetAngularVelocity(const Vector3& vAngular)
{
	this->angularVelocity = vAngular;
}



/* -- SET VOLUME --------------------------------------------------------------------*/
void RigidBody::SetVolume(float fVolume)
{
	if(fVolume <= 0.0f)
	{
		throw "Volume must be greater than zero!  (RigidBody::SetVolume)";
	}

	this->volume = fVolume;
}



/* -- GET DENSITY -------------------------------------------------------------------*/
float RigidBody::GetDensity(void)
{
	// calculate the density
	return this->mass / this->volume;
}



/* -- GET VOLUME --------------------------------------------------------------------*/
float RigidBody::GetVolume(void)
{
	return this->volume;
}



/* -- GET FRICTION COEFFICIENT ------------------------------------------------------*/
float RigidBody::GetFrictionCoefficient(void)
{
	return this->frictionCoefficient;
}



/* -- SET FRICTION COEFFICIENT ------------------------------------------------------*/
void RigidBody::SetFrictionCoefficient(float fFriction)
{
	if(fFriction > 1.0f || fFriction < 0.0f)
	{
		throw "Friction must be between 0 and 1!  (RigidBody::SetFrictionCoefficient)";
	}

	// 0.1 is grippy, 0.0f is no grip
	this->frictionCoefficient = fFriction / 10.0f;
}



/* -- DAMPEN ANGULAR VELOCITY -------------------------------------------------------*/
void RigidBody::DampenAngularVelocity(void)
{
	this->angularVelocity *= 1.0f - (this->frictionCoefficient * 5.0f);
}
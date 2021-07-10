/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_RIGID_BODY_H
#define SKULLBONEZ_RIGID_BODY_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezQuaternion.h"
#include "SkullbonezRotationMatrix.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
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
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class RigidBody
		{

		private:

			bool		isForceApplied;							// Keeps track of whether an impulse force has been applied or not
			float		mass;									// Scalar representative of object mass									[Units: kg]
			float		invertedMass;							// 1.0f / mass															[Units: kg]
			float		coefficientRestitution;					// Scalar representative of the coefficient of restitution (bounciness) [Units: scalar]
			float		frictionCoefficient;					// Scalar representative of the coefficient of friction (grip)			[Units: scalar]
			float		volume;									// Scalar representative of the volume of the body						[Units: m^3]
			Vector3		position;								// Vector representative of object position								[Units: point]
			Vector3		linearVelocity;							// Vector representative of object velocity								[Units: m/s]
			Vector3		linearAcceleration;						// Vector representative of object acceleration							[Units: m/s^2]
			Vector3		appliedForce;							// Vector representative of the sum of all forces						[Units: N]
			Vector3		worldForce;								// Linear forces acted upon the body by the world						[Units: N]
			Vector3		worldTorque;							// Angular forces acted upon the body by the world						[Units: Nm]
			Vector3		forceApplicationPoint;					// Vector representative of the location of the force applied			[Units: point]
			Vector3		angularVelocity;						// Vector representative of the bodies angular velocity					[Units: radians/s]
			Vector3		angularAcceleration;					// Vector representative of the bodies angular acceleration				[Units: radians/s^2]
			Vector3		rotationalInertia;						// Vector representative of the bodies rotational inertia				[Units: Inertia Tensor]
			Vector3		torque;									// Vector representative of the bodies torque							[Units: Nm]
			Vector3		changeInAngularVelocity;				// A buffer to store an expected change in angular velocity				[Units: m/s]
			Vector3		changeInLinearVelocity;					// A buffer to store an expected change in linear velocity				[Units: m/s]
			Quaternion	orientation;							// Quaternion representative of the orientation of the rigid body		[Units: Qrtn]



			void				ApplyWorldForce					(void);									// Applies the world force acting on the body
			void				ApplyLinearForce				(void);									// Applies the linear force acting on the body
			void				ApplyAngularForce				(void);									// Applies the angular force acting on the body
			Vector3				GetRollVelocity					(void);									// Gets the linear velocity based on rolling angular velocity


		public:

								RigidBody						(void);									// Default constructor
								~RigidBody						(void);									// Default destructor
			void				ApplyForces						(void);									// Update the rigid body's velocity based on its current state
			void				UpdatePosition					(float changeInTime);					// Update the rigid body's position based on its current state
			void				ApplyImpulseForce				(void);									// Apply the impulse force to the body
			void				ZeroForce						(void);									// Zero the force vectors
			void				RotateBodyToOrientation			(void);									// Rotates the orientation of the body into the world matrix
			void				SetMass							(float fMass);							// Set the mass of the rigid body
			void				SetFrictionCoefficient			(float fFriction);						// Set the friction coefficient of the body
			void				SetVolume						(float fVolume);						// Sets the volume member
			void				SetCoefficientRestitution		(float fCoefficientRestitution);		// Set the coefficient of restitution (bounciness)
			void				SetPosition						(const Vector3& vPosition);				// Set the position of the rigid body
			void				SetRotationalInertia			(const Vector3& vRotationalInertia);	// Sets the rotational inertia for the obect
			void				SetChangeInAngularVelocity		(const Vector3& vAngularVelocity);		// Sets the change in angular velocity
			void				SetChangeInLinearVelocity		(const Vector3& vLinearVelocity);		// Sets the change in linear velocity
			void				DampenAngularVelocity			(void);									// Dampens the angular velocity based on the friction coefficient
			void				ApplyChangeInAngularVelocity	(void);									// Applies the change in angular velocity
			void				ThrottleAngularVelocity			(void);									// Slows angular velocity to ensure it does not reach astronomical speeds
			void				ApplyChangeInLinearVelocity		(void);									// Applies the change in linear velocity
			float				GetCoefficientRestitution		(void);									// Get the coefficient of restitution (bounciness)
			float				GetFrictionCoefficient			(void);									// Get the friction coefficient of the body
			float				GetMass							(void);									// Returns the mass of the rigid body
			float				GetInvertedMass					(void);									// Returns the inverted mass of the rigid body
			float				GetVolume						(void);									// Returns the volume of the rigid body
			const Vector3&		GetVelocity						(void);									// Returns a const reference to the velocity of the rigid body
			const Vector3&		GetPosition						(void);									// Returns a const reference to the position of the rigid body
			const Vector3&		GetAngularVelocity				(void);									// Returns a const reference to the angular velocity of the rigid body
			const Vector3&		GetRotationalInertia			(void);									// Returns a const reference to the rotational inertia of the rigid body
			float				GetDensity						(void);									// Calculates and returns the density of the body
			void				SetLinearVelocity				(const Vector3& vLinear);				// Set the linear velocity of the rigid body
			void				SetAngularVelocity				(const Vector3& vAngular);				// Set the angular velocity of the rigid body
			void				SetImpulseForce					(const Vector3& vImpulseForce,
																 const Vector3& vApplicationPoint);		// Set an impulse force to the rigid body
			void				SetWorldForce					(const Vector3& vWorldForce,
																 const Vector3& vWorldTorque);			// Sets the forces being acted upon the object by the world environment
			void				UpdateRollPosition				(float changeInTime, 
																 float circumference);					// Update the rigid body's position when rolling (supply circumference of the body)
			RotationMatrix		GetOrientationMatrix			(float fTime = 0.0f);					// Gets the rotation matrix representing the bodies orientation at the specified time (0.0f returns CURRENT orientation matrix)
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
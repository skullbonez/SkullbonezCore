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
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezVector3.h"
#include "SkullbonezBoundingSphere.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::CollisionDetection;



/* -- RESPOND COLLISION TERRAIN -----------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::RespondCollisionTerrain(GameModel& gameModel)
{
	// get the total velocity (reverse it so the vector points away from the plane normal)
	Vector3 totalVelocity = gameModel.physicsInfo.GetVelocity() * -1;

	// get the scalar magnitude of the velocity projected along the normal of the collision
	float projectedVelocity = totalVelocity * gameModel.responseInformation.collidedPlane.normal;

	// if the projected velocity is really small
	if(projectedVelocity < 1.0f)
	{
		gameModel.SetIsGrounded(true);
	}

	if(typeid(*gameModel.boundingVolume) == typeid(BoundingSphere))
	{
		if(!gameModel.IsGrounded())
		{
			// apply the response forces to the model - angular impulse first
			CollisionResponse::SphereVsPlaneAngularImpulse(gameModel);

			// then linear impulse
			CollisionResponse::SphereVsPlaneLinearImpulse(gameModel, totalVelocity, projectedVelocity);

			// apply the change in angular velocity now the linear reaction has taken place
			gameModel.physicsInfo.ApplyChangeInAngularVelocity();
		}
		else
		{
			// perform the rolling physics
			CollisionResponse::SphereVsPlaneRollResponse(gameModel);
		}

		return;
	}

	throw "A supplied dynamics object is of unrecognised type!  "
		  "(CollisionResponse::RespondCollisionTerrain)";
}



/* -- RESPOND COLLISION GAME MODELS -------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::RespondCollisionGameModels(GameModel& gameModel1, 
												   GameModel& gameModel2)
{
	if(typeid(*gameModel1.boundingVolume) == typeid(BoundingSphere) &&
	   typeid(*gameModel2.boundingVolume) == typeid(BoundingSphere))
	{
		// calculate the collision normal once and once only
		Vector3 collisionNormal = 
			CollisionResponse::GetCollisionNormalSphereVsSphere(gameModel1, 
																gameModel2);

		// apply the response forces to the models - angular first
		CollisionResponse::SphereVsSphereAngular(gameModel1, gameModel2, collisionNormal);

		// then linear
		CollisionResponse::SphereVsSphereLinear (gameModel1, gameModel2, collisionNormal);

		// apply the change in angular velocities now the linear reactions
		// have taken place
		gameModel1.physicsInfo.ApplyChangeInAngularVelocity();
		gameModel2.physicsInfo.ApplyChangeInAngularVelocity();

		return;
	}

	throw "A supplied dynamics object is of unrecognised type!  "
		  "(CollisionResponse::RespondCollisionGameModels)";
}



/* -- SPHERE VS PLANE ROLL RESPONSE -------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::SphereVsPlaneRollResponse(GameModel& gameModel)
{
	// kill linear
	Vector3 linearVelocity = gameModel.physicsInfo.GetVelocity() * 0.9f;
	gameModel.physicsInfo.SetLinearVelocity(linearVelocity);

	// kill angular
	Vector3 angularVelocity = gameModel.physicsInfo.GetAngularVelocity() * 0.9f;
	gameModel.physicsInfo.SetAngularVelocity(angularVelocity);
}



/* -- SPHERE VS PLANE LINEAR IMPULSE ------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::SphereVsPlaneLinearImpulse(GameModel& gameModel, Vector3 totalVelocity, float projectedVelocity)
{
	// variable to calculate the incident velocity vector (normalised)
	Vector3 incidentVector = totalVelocity;

	// perform the normalisation
	incidentVector.Normalise();

	// calculate the direction of reflection
	Vector3 direction = Vector::VectorReflect(incidentVector, gameModel.responseInformation.collidedPlane.normal);

	// calculare the spin and grip
	float xSpin = gameModel.physicsInfo.GetAngularVelocity().z;
	float zSpin = gameModel.physicsInfo.GetAngularVelocity().x;
	float xGrip = xSpin * gameModel.physicsInfo.GetFrictionCoefficient();
	float zGrip = zSpin * gameModel.physicsInfo.GetFrictionCoefficient();

	// factor the spin and grip into the direction
	direction.x += xGrip;
	direction.z += zGrip;
	direction.Normalise();

	// dampen the balls spin as some of its momentum has been converted to linear velocity
	gameModel.physicsInfo.DampenAngularVelocity();

	// compute and update the gameModel velocity (compute scalar force magnitude, multiply by incident vector reflected about collision normal)
	gameModel.physicsInfo.SetLinearVelocity((projectedVelocity * gameModel.physicsInfo.GetCoefficientRestitution()) * direction);
}



/* -- SPHERE VS PLANE ANGULAR IMPULSE -----------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::SphereVsPlaneAngularImpulse(GameModel& gameModel)
{
	// compute the world space coordinate where the collision has occured
	Vector3 collisionPoint = GeometricMath::ComputeIntersectionPoint(gameModel.responseInformation.collidedRay, gameModel.responseInformation.collisionTime);

	// convert collisionPoint to object space, take into consideration the orientation of the object and the object space position of the particular dynamics 
	// object we are dealing with
	collisionPoint -= gameModel.physicsInfo.GetPosition();

	// sum the velocities of the game model [linear + (angular cross applicationPoint)] - remember the cross product here gives the 'wrench' that is perpendicular to the
	// angular velocity and the point of the collision
	Vector3 velocitySum = gameModel.physicsInfo.GetVelocity() + (Vector::CrossProduct(gameModel.physicsInfo.GetAngularVelocity(), collisionPoint));

	// get the scalar magnitude of the velocity projected along the normal to the collision
	float velocitySumProjectedAlongCollisionNormal = gameModel.responseInformation.collidedPlane.normal * velocitySum;

	/*
																	-vr(e+1)
											Fi = ---------------------------------------------------
												   1/m1+1/m2+n.[((r1*n)/I1)*r1]+n.[((r2*n)/I2)*r2]

	   This is the rigid body response formula used all over the shop.  Keep in mind the rigid body in this case is responding from a collision with a plane - the 
	   plane has infinite mass and will not move, so this code is simplified based on these assumptions.
	*/

	// calculate the numerator just as the formula above states
	float numerator										= -velocitySumProjectedAlongCollisionNormal * (1.0f + gameModel.physicsInfo.GetCoefficientRestitution());

	// get the perpendicular of the collision point with the normal to the collision
	Vector3 collisionPointCrossPlaneNormal				= Vector::CrossProduct(collisionPoint, gameModel.responseInformation.collidedPlane.normal);

	// multiply the inertia tensor from the game model with the perpendicular of the collsiion point and collision normal
	Vector3 scaledRotationalInertia						= Vector::VectorMultiply(gameModel.physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal);

	// take the perpendicular to the scaled inertia tensor and the point of collision
	Vector3 scaledRotationalInertiaCrossCollisionPoint	= Vector::CrossProduct(scaledRotationalInertia, collisionPoint);

	// project the collsiion normal along the inertia tensor/collision point perpendicular
	float collisionNormalDotInertiaCollisionPointPerp   = gameModel.responseInformation.collidedPlane.normal * scaledRotationalInertiaCrossCollisionPoint;

	// finally compute the denominator to the equation
	float denominator									= gameModel.physicsInfo.GetInvertedMass() + collisionNormalDotInertiaCollisionPointPerp;

	// compute the equation result
	float equationResult								= numerator / denominator;

	// the force will be in the direction of the collision normal, we now scale it to the magnitude of the equation result
	Vector3 impulseForce								= gameModel.responseInformation.collidedPlane.normal * equationResult;

	// convert the impulse force to a change in angular velocity via taking the perpendicular with the point of collision - keep in mind rotational inertia 
	// has already been taken into account here (so no need to compute angular acceleration via torque/inertiaTensor eqn)
	Vector3 changeInAngularVelocity						= Vector::CrossProduct(collisionPoint, impulseForce);

	// factor in the grippiness of the contact surface
	changeInAngularVelocity								*=  gameModel.physicsInfo.GetFrictionCoefficient() * 10;
	
	// finally, set the change in the angular velocity of the game model
	gameModel.physicsInfo.SetChangeInAngularVelocity(changeInAngularVelocity);
}



/* -- SPHERE VS SPHERE LINEAR -------------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::SphereVsSphereLinear(GameModel& gameModel1, 
											 GameModel& gameModel2,
											 const Vector3& collisionNormal)
{
	// calculate the total velocity of the gameModel1
	Vector3 gameModel1TotalVelocity = gameModel1.physicsInfo.GetVelocity();

	// calculate the total velocity of the gameModel2
	Vector3 gameModel2TotalVelocity = gameModel2.physicsInfo.GetVelocity();


	/*
		------------------------------------------------------
		FINAL VELOCITY EQN:
		------------------------------------------------------

					(m1-em2)v1i + (1+e)m2v2i
		v1f   =   -----------------------------
							 m1 + m2

					(m2-em1)v2i + (1+e)m1v1i
		v2f   =   -----------------------------
							 m1 + m2

		------------------------------------------------------
		WHERE:
		------------------------------------------------------
		m1 and m2 are the masses of the objects
		v1i and v2i are the initial velocities of the objects
		v1f and v2f are the final velocities of the objects
		e is the average coefficient of restitution
	*/

	// compute the projection of the velocities in the direction perpendicular to the collision
	float gameModel1ProjectedVelocity = gameModel1TotalVelocity * collisionNormal;
	float gameModel2ProjectedVelocity = gameModel2TotalVelocity * collisionNormal;

	// find the average coefficient of restitution
	float averageBounciness = (gameModel1.physicsInfo.GetCoefficientRestitution() + gameModel2.physicsInfo.GetCoefficientRestitution()) / 2;

	// calculate the final velocity of the gameModel1
	float gameModel1FinalVelocity = (((gameModel1.physicsInfo.GetMass() - (averageBounciness * gameModel2.physicsInfo.GetMass())) * gameModel1ProjectedVelocity) + 
	((1 + averageBounciness) * gameModel2.physicsInfo.GetMass() * gameModel2ProjectedVelocity)) / (gameModel1.physicsInfo.GetMass() + gameModel2.physicsInfo.GetMass());

	// calculate the final velocity of the gameModel2
	float gameModel2FinalVelocity = (((gameModel2.physicsInfo.GetMass() - (averageBounciness * gameModel1.physicsInfo.GetMass())) * gameModel2ProjectedVelocity) + 
	((1 + averageBounciness) * gameModel1.physicsInfo.GetMass() * gameModel1ProjectedVelocity)) / (gameModel1.physicsInfo.GetMass() + gameModel2.physicsInfo.GetMass());

	// update the gameModel1 velocity
	gameModel1.physicsInfo.SetLinearVelocity((gameModel1FinalVelocity - gameModel1ProjectedVelocity) * collisionNormal + gameModel1.physicsInfo.GetVelocity());

	// update the gameModel2 velocity
	gameModel2.physicsInfo.SetLinearVelocity((gameModel2FinalVelocity - gameModel2ProjectedVelocity) * collisionNormal + gameModel2.physicsInfo.GetVelocity());
}



/* -- SPHERE VS SPHERE ANGULAR ------------------------------------------------------------------------------------------------------------------------------------------*/
void CollisionResponse::SphereVsSphereAngular(GameModel& gameModel1, 
											  GameModel& gameModel2,
											  const Vector3& collisionNormal)
{
	// compute the object space points of collision
	Vector3 objectSpaceCollisionPoint1 =  collisionNormal * dynamic_cast<BoundingSphere*>(gameModel1.boundingVolume)->GetRadius();
	Vector3 objectSpaceCollisionPoint2 = -collisionNormal * dynamic_cast<BoundingSphere*>(gameModel2.boundingVolume)->GetRadius();

	// compute the linear velocities of the game models
	Vector3 linearVelocity1 = gameModel1.physicsInfo.GetVelocity();
	Vector3 linearVelocity2 = gameModel2.physicsInfo.GetVelocity();

	// compute the individual velocity sums for the game models - remember the cross product here gives the 'wrench' that is perpendicular to the angular velocity 
	// and the point of the collision
	Vector3 velocitySum1 = linearVelocity1 + (Vector::CrossProduct(gameModel1.physicsInfo.GetAngularVelocity(), objectSpaceCollisionPoint1));
	Vector3 velocitySum2 = linearVelocity2 + (Vector::CrossProduct(gameModel2.physicsInfo.GetAngularVelocity(), objectSpaceCollisionPoint2));

	// compute the relative velocity between the two objects
	Vector3 relativeVelocity = velocitySum2 - velocitySum1;

	// get the scalar magnitude of the relative velocity projected along the normal to the collision
	float velocitySumProjectedAlongCollisionNormal = relativeVelocity * collisionNormal;

	// compute the relative linear velocity
	Vector3 relativeLinearVelocity = linearVelocity2 - linearVelocity1;

	// normalise the relative linear velocity
	relativeLinearVelocity.Normalise();

	/* 
		Set the points of collision to the normalised relative linear velocity vector - remember - we are dealing with spheres - we must avoid the object space collision 
	    points from being parallel with the collision normal - this will always happen with spheres, so use the normalised relative linear velocity vector as the collision 
		point instead for the remainder of the calculations.  This is a little concoction I thought of after comparing the sphere vs sphere case with the sphere vs plane 
		case - note how with the sphere vs plane case from [CollisionResponse::SphereVsPlaneAngular], the collision point is based entirely on the objects ray of movement.

		Now, I don't know why this works so well, nor am I going to pretend to, but let me tell you this:  implementing rotational dynamics has been really difficult -
		texts by Conger and Zerbst explain techniques - both containing code samples, both which flat out don't work.  This technique works really well for my spheres,
		it is based on the angular response I have come to understand with my sphere vs plane routine, I am quite happy to use it here.  [SE: 16-08-2005 06:15]
	*/
	objectSpaceCollisionPoint1 = objectSpaceCollisionPoint2 = relativeLinearVelocity;

	/*
															Rigid body response formula:

																	-vr(e+1)
											Fi = ---------------------------------------------------
												   1/m1+1/m2+n.[((r1*n)/I1)*r1]+n.[((r2*n)/I2)*r2]
	*/

	// calculate the average coefficient of restitution
	float averageCoefficientRestitution					= (gameModel1.physicsInfo.GetCoefficientRestitution() + gameModel2.physicsInfo.GetCoefficientRestitution()) * 0.5f;

	// calculate the numerator just as the formula above states
	float numerator										= -velocitySumProjectedAlongCollisionNormal * (1.0f + averageCoefficientRestitution);

	// get the perpendicular of the collision point with the normal to the collision
	Vector3 collisionPointCrossPlaneNormal1				= Vector::CrossProduct(objectSpaceCollisionPoint1, collisionNormal);
	Vector3 collisionPointCrossPlaneNormal2				= Vector::CrossProduct(objectSpaceCollisionPoint2, collisionNormal);

	// multiply the inertia tensor from the game model with the perpendicular of the collsiion point and collision normal
	Vector3 scaledRotationalInertia1					= Vector::VectorMultiply(gameModel1.physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal1);
	Vector3 scaledRotationalInertia2					= Vector::VectorMultiply(gameModel2.physicsInfo.GetRotationalInertia(), collisionPointCrossPlaneNormal2);

	// take the perpendicular to the scaled inertia tensor and the point of collision
	Vector3 scaledRotationalInertiaCrossCollisionPoint1	= Vector::CrossProduct(scaledRotationalInertia1, objectSpaceCollisionPoint1);
	Vector3 scaledRotationalInertiaCrossCollisionPoint2	= Vector::CrossProduct(scaledRotationalInertia2, objectSpaceCollisionPoint2);

	// project the collsiion normal along the inertia tensor/collision point perpendicular
	float collisionNormalDotInertiaCollisionPointPerp1  = collisionNormal * scaledRotationalInertiaCrossCollisionPoint1;
	float collisionNormalDotInertiaCollisionPointPerp2  = collisionNormal * scaledRotationalInertiaCrossCollisionPoint2;

	// finally compute the denominator to the equation
	float denominator									= gameModel1.physicsInfo.GetInvertedMass() + 
														  gameModel2.physicsInfo.GetInvertedMass() + 
														  collisionNormalDotInertiaCollisionPointPerp1 + 
														  collisionNormalDotInertiaCollisionPointPerp2;

	// compute the equation result
	float equationResult								= numerator / denominator;

	// the force will be in the direction of the collision normal (respective to each obect), we now scale it to the magnitude of the equation result
	Vector3 impulseForce								= collisionNormal * equationResult;

	// convert the impulse force to a change in angular velocity via taking the perpendicular with the point of collision - keep in mind rotational inertia 
	// has already been taken into account here (so no need to compute angular acceleration via torque/inertiaTensor eqn)
	Vector3 changeInAngularVelocity1					= Vector::CrossProduct(objectSpaceCollisionPoint1,  impulseForce);
	Vector3 changeInAngularVelocity2					= Vector::CrossProduct(objectSpaceCollisionPoint2, -impulseForce);

	// finally, set the change in the angular velocity of the game models
	gameModel1.physicsInfo.SetChangeInAngularVelocity(changeInAngularVelocity1);
	gameModel2.physicsInfo.SetChangeInAngularVelocity(changeInAngularVelocity2);
}



/* -- CALCULATE RAY -----------------------------------------------------------------------------------------------------------------------------------------------------*/
Ray CollisionResponse::CalculateRay(GameModel& gameModel,
									float	   changeInTime)
{
	// return the ray of the gameModel
	return Ray(gameModel.physicsInfo.GetPosition(), gameModel.physicsInfo.GetVelocity() * changeInTime);
}



/* -- GET COLLIDED OBJECT WORLD POSITION --------------------------------------------------------------------------------------------------------------------------------*/
Vector3 CollisionResponse::GetCollidedObjectWorldPosition(GameModel& gameModel)
{
	return gameModel.physicsInfo.GetPosition() + (gameModel.physicsInfo.GetOrientationMatrix() * gameModel.boundingVolume->GetPosition());
}



/* -- GET COLLISION NORMAL SPHERE VS SPERE ------------------------------------------------------------------------------------------------------------------------------*/
Vector3 CollisionResponse::GetCollisionNormalSphereVsSphere(GameModel& gameModel1,
															GameModel& gameModel2)
{
	// compute the world coordinates of the collided dynamics objects we are dealing with
	Vector3 boundingVolumeWorldPosition1 = CollisionResponse::GetCollidedObjectWorldPosition(gameModel1);
	Vector3 boundingVolumeWorldPosition2 = CollisionResponse::GetCollidedObjectWorldPosition(gameModel2);

	// take the vector between the point of collision
	Vector3 collisionNormal = boundingVolumeWorldPosition2 - boundingVolumeWorldPosition1;

	// normalise this vector
	collisionNormal.Normalise();

	// return the result
	return collisionNormal;
}
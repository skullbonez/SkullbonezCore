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
#include "SkullbonezGameModel.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezCollisionResponse.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Environment;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
GameModel::GameModel(WorldEnvironment* pWorldEnv, 
					 const Vector3& vPosition, 
					 const Vector3& vRotationalInertia, 
					 float fMass)
{
	// check for valid world environment pointer
	if(!pWorldEnv) 
	{
		throw "Invalid world environment pointer supplied.  (GameModel::GameModel)";
	}

	// set the important members
	this->worldEnvironment = pWorldEnv;
	this->physicsInfo.SetPosition(vPosition);
	this->physicsInfo.SetRotationalInertia(vRotationalInertia);
	this->physicsInfo.SetMass(fMass);

	// initialise pointers
	this->boundingVolume				= 0;
	this->terrain						= 0;

	// initialise other members
	this->projectedSurfaceArea			= 0.0f;
	this->dragCoefficient				= 0.0f;
	this->isGrounded					= false;
	this->isResponseRequired			= false;
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
GameModel::~GameModel()
{
	if(this->boundingVolume) delete this->boundingVolume;
}



/* -- APPLY FORCE -----------------------------------------------------------------*/
void GameModel::SetImpulseForce(const Vector3& vForce,
								const Vector3& vApplicationPoint)
{
	this->physicsInfo.SetImpulseForce(vForce, vApplicationPoint);
}



/* -- SET WORLD FORCE -------------------------------------------------------------*/
void GameModel::SetWorldForce(const Vector3& vWorldForce, const Vector3& vWorldTorque)
{
	this->physicsInfo.SetWorldForce(vWorldForce, vWorldTorque);
}



/* -- SET COEFFICIENT RESTITUTION -------------------------------------------------*/
void GameModel::SetCoefficientRestitution(float fCoefficientRestitution)
{
	this->physicsInfo.SetCoefficientRestitution(fCoefficientRestitution);
}



/* -- ADD BOUNDING SPHERE ---------------------------------------------------------*/
void GameModel::AddBoundingSphere(float fRadius)
{
	// clear the previous bounding sphere if there was one
	if(this->boundingVolume) delete this->boundingVolume;

	// add the new bounding sphere
	this->boundingVolume = new BoundingSphere(fRadius, Vector::ZERO_VECTOR);

	// recalculate model info now the model has changed
	this->UpdateModelInfo();
}



/* -- GET DRAG COEFFICIENT --------------------------------------------------------*/
float GameModel::GetDragCoefficient(void)
{
	return this->dragCoefficient;
}



/* -- GET PROJECTED SURFACE AREA --------------------------------------------------*/
float GameModel::GetProjectedSurfaceArea(void)
{
	return this->projectedSurfaceArea;
}



/* -- GET VELOCITY ----------------------------------------------------------------*/
const Vector3& GameModel::GetVelocity(void)
{
	return this->physicsInfo.GetVelocity();
}



/* -- UPDATE MODEL INFO -----------------------------------------------------------*/
void GameModel::UpdateModelInfo(void)
{
	this->CalculateVolume();
	this->CalculateDragCoefficient();
	this->CalculateProjectedSurfaceArea();
}



/* -- GET MODEL COLLISION TIME ----------------------------------------------------*/
float GameModel::GetModelCollisionTime(GameModel& collisionTarget, 
									   float	  changeInTime)
{
	// calculate the ray of the target
	Ray targetRay = CollisionResponse::CalculateRay(collisionTarget, changeInTime);

	// calculate the ray of the focus
	Ray focusRay = CollisionResponse::CalculateRay(*this, changeInTime);

	// perform the collision test
	return boundingVolume->TestCollision(*collisionTarget.boundingVolume, 
										 targetRay, 
										 focusRay);
}



/* -- COLLISION RESPONSE GAME MODEL -----------------------------------------------*/
void GameModel::CollisionResponseGameModel(GameModel&  responseTarget,
										   float	   remainingTimeStep)
{
	// if there has been no collision, throw an exception!
	if(!responseTarget.isResponseRequired || !this->isResponseRequired)
	{
		throw "Cannot perform collision response when no collision has occured!  "
			  "(GameModel::CollisionResponseGameModel)";
	}

	// respond to the collision...
	CollisionResponse::RespondCollisionGameModels(*this, responseTarget);

	// update the positions based on remaining time step
	this->UpdatePosition(remainingTimeStep);
	responseTarget.UpdatePosition(remainingTimeStep);

	// set the collided collision object to null now the reaction has taken place
	this->isResponseRequired		  = false;
	responseTarget.isResponseRequired = false;
}



/* -- COLLISION RESPONSE TERRAIN --------------------------------------------------*/
void GameModel::CollisionResponseTerrain(float remainingTimeStep)
{
	// if there has been no collision, throw an exception!
	if(!this->isResponseRequired)
	{
		throw "Cannot perform collision response when no collision has occured!  "
			  "(GameModel::CollisionResponseTerrain)";
	}

	// respond to the collision...
	CollisionResponse::RespondCollisionTerrain(*this);

	// update the position based on remaining time step
	this->UpdatePosition(remainingTimeStep);

	// set the collided collision object to null now the reaction has taken place
	this->isResponseRequired = false;
}



/* -- IS RESPONSE REQUIRED --------------------------------------------------------*/
bool GameModel::IsResponseRequired(void)
{
	return this->isResponseRequired;
}



/* -- ORIENT MESH ---------------------------------------------------------------*/
void GameModel::OrientMesh(void)
{
	// translate to object space
	Vector3 position = this->physicsInfo.GetPosition();
	glTranslatef(position.x, position.y, position.z);

	// orient object space
	this->physicsInfo.RotateBodyToOrientation();

	// translate back to world space (keeping the orientation)
	glTranslatef(-position.x, -position.y, -position.z);
}



/* -- RENDER MODEL ----------------------------------------------------------------*/
void GameModel::RenderModel(void)
{
	// save the matrix state if we are going to be performing
	// rotations on the world matrix
	glPushMatrix();

	// orient the mesh for rendering
	this->OrientMesh();

	// ******************** render here ********************

	// restore the original world matrix state now we are done rendering
	glPopMatrix();
}



/* -- RENDER COLLISION BOUNDS -----------------------------------------------------*/
void GameModel::RenderCollisionBounds(void)
{
	// save the matrix state if we are going to be performing
	// rotations on the world matrix
	glPushMatrix();

	// orient the bounding volume for rendering
	this->OrientMesh();

	// render
	this->boundingVolume->DEBUG_RenderCollisionVolume(this->physicsInfo.GetPosition());

	// restore the original world matrix state now we are done rendering
	glPopMatrix();
}



/* -- UPDATE VELOCITY -------------------------------------------------------------*/
void GameModel::ApplyForces(float changeInTime)
{
	// throttle the angular velocity
	this->physicsInfo.ThrottleAngularVelocity();

	// apply the world forces
	this->ApplyWorldForces(changeInTime);

	// apply the forces to the model
	this->physicsInfo.ApplyForces();
}



/* -- APPLY WORLD FORCES ----------------------------------------------------------*/
void GameModel::ApplyWorldForces(float changeInTime)
{
	// apply the world forces now we know the pointer is valid
	this->worldEnvironment->AddWorldForces(*this, changeInTime);
}



/* -- CALCULATE PROJECTED SURFACE AREA --------------------------------------------*/
void GameModel::CalculateProjectedSurfaceArea(void)
{
	// return the average submerged percentage
	this->projectedSurfaceArea = this->boundingVolume->GetProjectedSurfaceArea();
}



/* -- CALCULATE DRAG COEFFICIENT --------------------------------------------------*/
void GameModel::CalculateDragCoefficient(void)
{
	// return the average submerged percentage
	this->dragCoefficient = this->boundingVolume->GetDragCoefficient();
}



/* -- GET VOLUME ------------------------------------------------------------------*/
float GameModel::GetVolume(void)
{
	return this->physicsInfo.GetVolume();
}



/* -- UPDATE POSITION -------------------------------------------------------------*/
void GameModel::UpdatePosition(float changeInTime)
{
	// update position based on airbourne model
	this->physicsInfo.UpdatePosition(changeInTime);

	// slam the ball to the terrain height if it has fallen below
	this->DEBUG_SetSphereToTerrain();
}



/* -- UPDATE POSITION -------------------------------------------------------------*/
void GameModel::CalculateVolume(void)
{
	this->physicsInfo.SetVolume(this->boundingVolume->GetVolume());
}



/* -- GET MASS --------------------------------------------------------------------*/
float GameModel::GetMass(void)
{
	return this->physicsInfo.GetMass();
}



/* -- GET ANGULAR VELOCITY --------------------------------------------------------*/
const Vector3& GameModel::GetAngularVelocity(void)
{
	return this->physicsInfo.GetAngularVelocity();
}



/* -- SET TERRAIN -----------------------------------------------------------------*/
void GameModel::SetTerrain(Terrain* pTerrain)
{
	this->terrain = pTerrain;
}



/* -- IS GROUNDED -----------------------------------------------------------------*/
bool GameModel::IsGrounded(void)
{
	return this->isGrounded;
}



/* -- SET IS GROUNDED -------------------------------------------------------------*/
void GameModel::SetIsGrounded(bool fIsGrounded)
{
	this->isGrounded = fIsGrounded;
}



/* -- GET TERRAIN COLLISION TIME ---------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::GetTerrainCollisionTime(float changeInTime)
{
	// calculate the ray for the current dynamics object
	this->responseInformation.testingRay = CollisionResponse::CalculateRay(*this, changeInTime);

	// if the dynamics object is stationary, no collision will occur
	if(this->responseInformation.testingRay.vector3.IsCloseToZero())
	{
		return NO_COLLISION;
	}

	// the origin will be in a different position based on the geometrical shape of the object
	if(typeid(*this->boundingVolume) == typeid(BoundingSphere))
	{
		// offset the origin by the radius of the sphere
		this->responseInformation.testingRay.origin.y -= dynamic_cast<BoundingSphere*>(this->boundingVolume)->GetRadius();
	}
	else
	{
		// if we cant recognise the bounding object, something is wrong...
		throw "A supplied dynamics object is of unrecognised type!  (GameModel::GetTerrainCollisionTime)";
	}

	// if out of bounds, no collision has occured
	if(!this->terrain->IsInBounds(this->responseInformation.testingRay.origin.x, this->responseInformation.testingRay.origin.z))
	{
		return NO_COLLISION;
	}

	// store the plane vertically aligned with the object...
	this->responseInformation.testingPlane = GeometricMath::ComputePlane(this->terrain->LocatePolygon(this->responseInformation.testingRay.origin.x, 
																									  this->responseInformation.testingRay.origin.z));

	// if the ball is grounded then it has already hit the ground
	if(this->isGrounded)
	{
		this->responseInformation.collisionTime = 0.0f;
		return 0.0f;
	}

	// save the collision time
	this->responseInformation.collisionTime = GeometricMath::CalculateIntersectionTime(this->responseInformation.testingPlane, this->responseInformation.testingRay);

	// return the point in time where the collision has occured (yes, we do save this to a member but it is more intuitive just to return the value as well)
	return this->responseInformation.collisionTime;
}



/* -- COLLISION DETECT TERRAIN -----------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::CollisionDetectTerrain(float changeInTime)
{
	// ensure terrain pointer is valid
	if(!this->terrain)
	{
		throw "Terrain pointer not valid!  (GameModel::CollisionDetectTerrain)";
	}

	// check to ensure pending responses have been responded to
	if(this->isResponseRequired)
	{
		throw "Cannot detect collision when a response is required first!  "
			  "(GameModel::CollisionDetectTerrain)";
	}

	float collisionTime = this->GetTerrainCollisionTime(changeInTime);

	// if no collision in this time frame
	if(collisionTime > 1.0f || collisionTime < ZERO_TAKE_TOLERANCE)
	{
		// allow full time to be applied as no collision will occur
		collisionTime = changeInTime;
	}
	else
	{
		// perform the cap - cap time to be applied by converting collision from time ratio to actual seconds
		collisionTime *= changeInTime;

		// set response required flag to true
		this->isResponseRequired = true;

		// store the response information
		this->responseInformation.collidedPlane = this->responseInformation.testingPlane;
		this->responseInformation.collidedRay	= this->responseInformation.testingRay;
	}

	// return when the collision will occur
	return collisionTime;
}



/* -- COLLISION DETECT GAME MODEL --------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::CollisionDetectGameModel(GameModel& collisionTarget,
										  float		 changeInTime)
{
	// if there is a collision pending to be responded to between one of the two models
	if(this->isResponseRequired || collisionTarget.isResponseRequired)
	{
		// throw an exception!
		throw "Cannot detect collision when a response is required first!  (GameModel::CollisionDetectGameModel)";
	}

	// get the time of collision
	float collisionTime = this->GetModelCollisionTime(collisionTarget, changeInTime);

	// if no collision in this time frame
	if(collisionTime > 1.0f || collisionTime < ZERO_TAKE_TOLERANCE)
	{
		// allow full time to be applied as no collision will occur
		collisionTime = changeInTime;
	}
	else
	{
		// perform the cap - cap time to be applied by converting collision from time ratio to actual seconds
		collisionTime *= changeInTime;
		this->isResponseRequired		    = true;
		collisionTarget.isResponseRequired = true;
	}

	// return when the collision will occur
	return collisionTime;
}



/* -- GET POSITION -----------------------------------------------------------------------------------------------------------------------------------------------------*/
const Vector3& GameModel::GetPosition(void)
{
	return this->physicsInfo.GetPosition();
}



/* -- DEBUG_SET SPHERE TO TERRAIN --------------------------------------------------------------------------------------------------------------------------------------*/
void GameModel::DEBUG_SetSphereToTerrain(void)
{
	// if we are not in bounds then exit now!
	if(!this->terrain->IsInBounds(this->physicsInfo.GetPosition().x, this->physicsInfo.GetPosition().z))
	{
		return;
	}

	// get the total radius
	float rad = dynamic_cast<BoundingSphere*>(this->boundingVolume)->GetRadius();

	// get the height of the terrain at the current XZ position
	float height = this->terrain->GetTerrainHeightAt(this->physicsInfo.GetPosition().x, this->physicsInfo.GetPosition().z);

	// if we are lower than the terrain
	if(this->physicsInfo.GetPosition().y - rad < height)
	{
		// work out the position we will update the model to
		Vector3 updatePos(this->physicsInfo.GetPosition().x, 
						  height + rad,
						  this->physicsInfo.GetPosition().z);

		// update the position
		this->physicsInfo.SetPosition(updatePos);
	}
}



/* -- GET SUBMERGED VOLUME PERCENT --------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::GetSubmergedVolumePercent(void)
{
	float fluidSurfaceHeight	= this->worldEnvironment->GetFluidSurfaceHeight();
	float totalPercentage		= this->boundingVolume->GetSubmergedVolumePercent(fluidSurfaceHeight - this->physicsInfo.GetPosition().y);

	// return the submerged percentage
	return totalPercentage;
}
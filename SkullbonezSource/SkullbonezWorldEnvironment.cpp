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
#include "SkullbonezWorldEnvironment.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;



/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
WorldEnvironment::WorldEnvironment(float fFluidSurfaceHeight,
								   float fFluidDensity,
								   float fGasDensity,
								   float fGravity) : fluidSurfaceHeight(fFluidSurfaceHeight),
													 fluidDensity(fFluidDensity),
													 gasDensity(fGasDensity),
													 gravity(fGravity) {}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
WorldEnvironment::~WorldEnvironment() {}



/* -- RENDER FLUID ----------------------------------------------------------------*/
void WorldEnvironment::RenderFluid(void)
{
	// disable lighting
	glDisable(GL_LIGHTING);

	// turn on blending
	glEnable(GL_BLEND);

	glBegin(GL_QUADS);

		// Corner 1
		glNormal3i(0, 1, 0);
		glTexCoord2i(0, 0);
		glVertex3f(-FRUSTUM_CLIP_FAR_QTY, 
				   this->fluidSurfaceHeight, 
				   -FRUSTUM_CLIP_FAR_QTY);

		// Corner 2
		glNormal3i(0, 1, 0);
		glTexCoord2i(0, 1);
		glVertex3f(-FRUSTUM_CLIP_FAR_QTY, 
				   this->fluidSurfaceHeight,  
				   FRUSTUM_CLIP_FAR_QTY);

		// Corner 3
		glNormal3i(0, 1, 0);
		glTexCoord2i(1, 1);
		glVertex3f(FRUSTUM_CLIP_FAR_QTY, 
				   this->fluidSurfaceHeight,  
				   FRUSTUM_CLIP_FAR_QTY);
		
		// Corner 4
		glNormal3i(0, 1, 0);
		glTexCoord2i(1, 0);
		glVertex3f(FRUSTUM_CLIP_FAR_QTY, 
				   this->fluidSurfaceHeight, 
				   -FRUSTUM_CLIP_FAR_QTY);

	glEnd();

	// turn off blending
	glDisable(GL_BLEND);

	// enable lighting
	glEnable(GL_LIGHTING);
}



/* -- GET FLUID SURFACE HEIGHT ----------------------------------------------------*/
float WorldEnvironment::GetFluidSurfaceHeight(void)
{
	return this->fluidSurfaceHeight;
}



/* -- ADD WORLD FORCES ------------------------------------------------------------*/
void WorldEnvironment::AddWorldForces(GameModel& target, float changeInTime)
{
	// initialise the world force vector so we can add to it
	Vector3 worldForce				= Math::Vector::ZERO_VECTOR;
	Vector3 worldTorque				= Math::Vector::ZERO_VECTOR;

	// get the total volume of the target
	float totalVolume				= target.GetVolume();

	// get the submerged percentage of the volume of the target
	float submergedVolumePercent	= target.GetSubmergedVolumePercent();

	// get the drag coefficient of the target
	float dragCoefficient			= target.GetDragCoefficient();

	// get the projected surface area of the target
	float projectedSurfaceArea		= target.GetProjectedSurfaceArea();

	// add the force of gravity to the world force
	worldForce.y += this->CalculateGravity(target.GetMass());

	// add the force of buoyancy to the world force
	worldForce.y += this->CalculateBuoyancy(totalVolume * submergedVolumePercent);

	// add the linear viscous drag to the world force
	worldForce	 += this->CalculateViscousDrag(target.GetVelocity(),
											   submergedVolumePercent,
											   dragCoefficient,
											   projectedSurfaceArea);

	// add the angular viscous drag to the world force
	worldTorque	 += this->CalculateViscousDrag(target.GetAngularVelocity(),
											   submergedVolumePercent,
											   dragCoefficient,
											   projectedSurfaceArea);

	// scale and then set the world force and torque
	target.SetWorldForce(worldForce * changeInTime, worldTorque * changeInTime);
}



/* -- CALCULATE GRAVITY -----------------------------------------------------------*/
float WorldEnvironment::CalculateGravity(float objectMass)
{
	// Fg = ma
	return objectMass * this->gravity;
}



/* -- CALCULATE BUOYANCY ----------------------------------------------------------*/
float WorldEnvironment::CalculateBuoyancy(float submergedObjectVolume)
{
	// Fb = -gravity * mass of displaced fluid
	return this->gravity * this->fluidDensity * submergedObjectVolume * -1.0f;
}



/* -- CALCULATE VISCOUS DRAG ------------------------------------------------------*/
Vector3 WorldEnvironment::CalculateViscousDrag(Vector3 velocityVector,
											   float   submergedVolumePercent,
											   float   dragCoefficient,
											   float   projectedSurfaceArea)
{
	// if there is no velocity, there will be no viscous drag
	if(velocityVector.IsCloseToZero())
	{
		return Math::Vector::ZERO_VECTOR;
	}

	// calculate the squared magnitude of the velocity vector
	float distanceSquared = Math::Vector::VectorMagSquared(velocityVector);

	// normalise the velocity vector
	velocityVector.Normalise();

	// negate the velocity vector
	velocityVector *= -1;

	/* 
		Formula for viscous drag:

		Viscous Drag (N) = vDir * 
						   0.5 * 
						   density * 
						   velocity * velocity * 
						   dragCoefficient *
						   surfaceArea

		NOTE: Density is averaged based on the amount
		the body is submerged...
	*/
	return  velocityVector *
		    0.5f *
		  ((this->gasDensity * (1.0f - submergedVolumePercent)) +
		   (this->fluidDensity * submergedVolumePercent)) *
		    distanceSquared *
		    dragCoefficient *
		    projectedSurfaceArea;
}
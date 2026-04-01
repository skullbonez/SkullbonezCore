/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   �   .--. .--.   �
								   � )/   � �   \( �
								   �/ \__/   \__/ \�
								   /      /^\      \
								   \__    '='    __/
								   	 �\         /�
									 �\'"VUUUV"'/�
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezWorldEnvironment.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
WorldEnvironment::WorldEnvironment(void) : fluidSurfaceHeight(0.0f),
										   fluidDensity(0.0f),
										   gasDensity(0.0f),
										   gravity(0.0f) {}



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
void WorldEnvironment::RenderFluid(const Matrix4& proj)
{
	if (!this->fluidMesh)
		this->BuildFluidMesh();

	glEnable(GL_BLEND);

	this->fluidShader->Use();

	// Read current FFP modelview as model transform (caller pushes translate)
	float mv[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, mv);
	Matrix4 modelView(mv);
	Matrix4 identity;

	this->fluidShader->SetMat4("uModel", identity);
	this->fluidShader->SetMat4("uView", modelView);
	this->fluidShader->SetMat4("uProjection", proj);
	this->fluidShader->SetVec4("uColorTint", 1.0f, 1.0f, 1.0f, 0.5f);

	this->fluidMesh->Draw();
	glUseProgram(0);

	glDisable(GL_BLEND);
}



/* -- BUILD FLUID MESH ------------------------------------------------------------*/
void WorldEnvironment::BuildFluidMesh(void)
{
	float h = this->fluidSurfaceHeight;
	float f = FRUSTUM_CLIP_FAR_QTY;

	// 2 triangles = 6 vertices, 5 floats each (pos3 + tex2)
	float data[] = {
		-f, h, -f,  0, 0,
		-f, h,  f,  0, 1,
		 f, h,  f,  1, 1,
		-f, h, -f,  0, 0,
		 f, h,  f,  1, 1,
		 f, h, -f,  1, 0,
	};

	this->fluidMesh = std::make_unique<Mesh>(data, 6, false, true);
	this->fluidShader = std::make_unique<Shader>(
		"SkullbonezData/shaders/unlit_textured.vert",
		"SkullbonezData/shaders/unlit_textured.frag");
}



/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void WorldEnvironment::ResetGLResources(void)
{
	this->fluidMesh.reset();
	this->fluidShader.reset();
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
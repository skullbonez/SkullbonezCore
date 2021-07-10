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
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezHelper.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Basics;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
BoundingSphere::BoundingSphere(void) {}



/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
BoundingSphere::BoundingSphere(float			fRadius,
							   const Vector3&	vPosition) : radius(fRadius)
{
	this->position = vPosition;
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
BoundingSphere::~BoundingSphere() {}



/* -- COLLISION DETECT ------------------------------------------------------------*/
float BoundingSphere::CollisionDetect(const BoundingSphere& target,
									  const Ray&			targetRay,
									  const Ray&			focusRay)
{
	// calculate the total movement
	Vector3 totalMovement	= targetRay.vector3 - focusRay.vector3;

	// if the total movement is insignificant, exit early
	if(totalMovement.IsCloseToZero()) return NO_COLLISION;

	// calculate the total displacement of the total movement
	float displacement		 = Vector::VectorMag(totalMovement);

	// calculate the difference vector between both bounding spheres
	Vector3 difference		 = focusRay.origin - targetRay.origin;

	// dot product the difference vector
	float diffDotDiff		 = difference * difference;

	// sum the radii of the bounding spheres
	float radiusSum			 = target.radius + this->radius;

	// square the sums of the radii
	float radiusSumSq		 = radiusSum * radiusSum;

	// normalise the total movement vector to dispose its magnitude
	totalMovement.Normalise();

	// dot the difference vector with the normalised total movement vector
	float diffDotMoveDir	 = difference * totalMovement;

	// square the result from above
	float diffDotMoveDirSq	 = diffDotMoveDir * diffDotMoveDir;

	// store part of the quadratic theorem result into a temp variable
	float tmp = diffDotMoveDirSq + radiusSumSq - diffDotDiff;

	// ensure no square root of a negative will be
	// performed - if this was going to be attempted, there
	// will be no collision in ANY given time frame
	if(tmp < 0.0f)			return NO_COLLISION;

	// calculate the negative root via quadratic formula
	float collisionStartDist = diffDotMoveDir - sqrtf(tmp);

	// return the proportionate time in which the collision started
	return collisionStartDist / displacement;
}



/* -- TEST COLLISION --------------------------------------------------------------*/
float BoundingSphere::TestCollision(const DynamicsObject&  target,
									const Ray&			   targetRay,
									const Ray&			   focusRay)
{
	if(typeid(target) == typeid(BoundingSphere))
	{
		return(this->CollisionDetect((BoundingSphere&)target, targetRay, focusRay));
	}

	throw "Target dynamics object is of unrecognised type!  "
		  "(BoundingSphere::TestCollision)";
}



/* -- GET RADIUS ------------------------------------------------------------------*/
float BoundingSphere::GetRadius(void)
{
	return this->radius;
}



/* -- DEBUG RENDER COLLISION VOLUME -----------------------------------------------*/
void BoundingSphere::DEBUG_RenderCollisionVolume(const Vector3& worldSpaceCoords)
{
	glPushMatrix();

		glTranslatef(worldSpaceCoords.x + this->position.x, 
					 worldSpaceCoords.y + this->position.y, 
					 worldSpaceCoords.z + this->position.z);

		SkullbonezHelper::DrawSphere(this->radius, RENDER_COL_VOL_TRANS);

	glPopMatrix();
}



/* -- GET VOLUME ------------------------------------------------------------------*/
float BoundingSphere::GetVolume(void)
{
	// volume of sphere = 4/3 * PI * radius^3
	return FOUR_OVER_THREE * _PI * this->radius * this->radius * this->radius;
}



/* -- GET SUBMERGED VOLUME PERCENT ------------------------------------------------*/
float BoundingSphere::GetSubmergedVolumePercent(float fluidSurfaceHeight)
{
	if(this->position.y - this->radius >= fluidSurfaceHeight)
	{
		// not touching fluid
		return 0.0f;
	}
	else if(this->position.y + this->radius <= fluidSurfaceHeight)
	{
		// totally submerged in fluid
		return 1.0f;
	}
	else
	{
		/* 
			Partially submerged in fluid, return submerged proportion
			
			Volume for partially submerged sphere:
			v = 1/3 * PI * (3r-y)y^2

			Formula from: http://vps.arachnoid.com/calculus/volume1.html
		*/
		float yValue = fluidSurfaceHeight - (this->position.y - this->radius);
		return  (((ONE_OVER_THREE * _PI * 
				 ((3.0f * this->radius) - yValue) * 
				 yValue * yValue)) / this->GetVolume());
	}
}



/* -- GET DRAG COEFFICIENT --------------------------------------------------------*/
float BoundingSphere::GetDragCoefficient(void)
{
	return SPHERE_DRAG_COEFFICIENT;
}



/* -- GET PROJECTED SURFACE AREA --------------------------------------------------*/
float BoundingSphere::GetProjectedSurfaceArea(void)
{
	// Area of circle = PI * r^2
	return _PI * this->radius * this->radius;
}
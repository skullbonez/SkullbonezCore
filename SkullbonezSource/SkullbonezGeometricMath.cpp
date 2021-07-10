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
#include "SkullbonezGeometricMath.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math;



/* -- COMPUTE TRIANGLE NORMAL -----------------------------------------------------*/
Vector3	GeometricMath::ComputeTriangleNormal(const Triangle& triangle)
{
	// get the vectors representing the edges of the triangle 
	// (counter-clockwise rotation)
	Vector3 edge1 = triangle.v2 - triangle.v1;
	Vector3 edge2 = triangle.v3 - triangle.v2;

	// compute the normal to the triangle
	Vector3 normal = Vector::CrossProduct(edge1, edge2);

	// normalise and return
	normal.Normalise();
	return normal;
}



/* -- COMPUTE PLANE ---------------------------------------------------------------*/
Plane GeometricMath::ComputePlane(const Triangle& triangle)
{
	Plane plane;
	ZeroMemory(&plane, sizeof(plane));

	// compute the normal of the plane Triangle 'triangle' is sitting on
	plane.normal = GeometricMath::ComputeTriangleNormal(triangle);

	// compute the distance of the plane from the origin by taking the
	// dot product of the plane normal and one of the points lying on the plane
	plane.distance = triangle.v1 * plane.normal;

	// return computed plane
	return plane;
}



/* -- DETERMINE POINT DIST FROM PLANE ---------------------------------------------*/
float GeometricMath::DeterminePointDistFromPlane(const Plane&   plane,
												 const Vector3& point)
{
	// dot the normal and point then subtract the planes scalar distance from 
	// the origin
	return (plane.normal * point - plane.distance);
}



/* -- CLASSIFY POINT AGAINST PLANE ------------------------------------------------*/
GeometricMath::PointPlaneClassification 
GeometricMath::ClassifyPointAgainstPlane(const Plane&   plane, 
										 const Vector3& point)
{
	// determine the distance the point is to the plane
	float result = GeometricMath::DeterminePointDistFromPlane(plane, point);

	// if the distance is positive the point is on the front side of the plane
	if(result > 0.0f) return PointPlaneClassification::FrontSideOfPlane;

	// if the distance is negative the point is on the back side of the plane
	if(result < 0.0f) return PointPlaneClassification::BackSideOfPlane;

	// if the distance is 0, the point coincides with the plane
	return PointPlaneClassification::CoincideWithPlane;
}



/* -- GET HEIGHT FROM PLANE -------------------------------------------------------*/
float GeometricMath::GetHeightFromPlane(const Triangle& triangle,
										float		xCoord,
										float		zCoord)
{
	// place the point on the XZ plane at Y = 0
	Vector3 point		= Vector3(xCoord, 0.0f, zCoord);

	// compute the traiangle plane
	Plane trianglePlane = GeometricMath::ComputePlane(triangle);

	// compute the distance of the plane to the point along the plane normal
	float normalDist	= GeometricMath::DeterminePointDistFromPlane(trianglePlane, 
																	 point);

	// use rearranged dot product formula to compute the angle between
	// the triangle plane normal and vertically upwards (0, 1, 0)
	// remember (let '*' be dot product): (x, y, z)*(0, 1, 0) = y
	float theta			= _HALF_PI - acosf(trianglePlane.normal.y);

	// use law of sines to compute result (see math reference)
	return				  -(normalDist / sinf(theta));
}



/* -- CALCULATE INTERSECTION TIME ---------------------------------------------------*/
float GeometricMath::CalculateIntersectionTime(const Plane&	 plane,
											   const Ray&	 ray)
{
	// ensure data is valid
	if(plane.normal == ZERO_VECTOR) 
		throw "Division by zero!  (GeometricMath::CalculateIntersectionTime)";

	// if the ray doesnt go anywhere then no collision will occur
	if(ray.vector3.IsCloseToZero()) return NO_COLLISION;

	// check the normal and ray aren't perpendicular to each other
	float denominator = plane.normal * ray.vector3;
	if(!denominator) return NO_COLLISION;

	// compute the scalar representing the magnitude of the ray that needs to
	// be translated upon from vBegin UNTIL the intersection with the plane
	// takes place.  this magnitude is computed by taking the dot product of
	// of the plane normal and vBegin plus the planes distance from the origin,
	// divided by the dot product of the planes normal and the ray
	return -(((plane.normal * ray.origin) - plane.distance) / denominator);
}



/* -- CALCULATE INTERSECTION TIME ---------------------------------------------------*/
float GeometricMath::CalculateIntersectionTime(const Triangle& triangle,
											   const Ray&	   ray)
{
	return GeometricMath::CalculateIntersectionTime(
											GeometricMath::ComputePlane(triangle),
											ray);
}



/* -- COMPUTE INTERSECTION POINT -----------------------------------------------------*/
Vector3	GeometricMath::ComputeIntersectionPoint(const Plane&   plane,
												const Ray&	   ray)
{
	// get the time of intersection
	float collisionTime = GeometricMath::CalculateIntersectionTime(plane, ray);

	// ensure the ray intersects with the plane
	if(collisionTime > 1.0f || collisionTime < 0.0f)
	{
		throw "Supplied ray will not intersect with this plane!  "
			  "(GeometricMath::ComputeIntersectionPoint)";
	}

	// translate from the origin of the ray along the ray until the collision
	// occurs, and return this vector
	return GeometricMath::ComputeIntersectionPoint(ray, collisionTime);
}



/* -- COMPUTE INTERSECTION POINT -----------------------------------------------------*/
Vector3	GeometricMath::ComputeIntersectionPoint(const Ray&	   ray,
												float fCollisionTime)
{
	// translate from the origin of the ray along the ray until the collision
	// occurs, and return this vector
	return ray.origin + (ray.vector3 * fCollisionTime);
}



/* -- IS POINT INSIDE TRIANGLE -------------------------------------------------------*/
bool GeometricMath::IsPointInsideTriangle(const Triangle& triangle,
										  const Vector3&  point)
{
	// compute the barycentric coordinates for the point on the triangle plane
	Vector3 barycentricCoords = 
		GeometricMath::ComputeBarycentricCoordinates(triangle, point);

	// if the point lies outside of the triangle, there will be at least one
	// negative barycentric coordinate.  returning the result of this test will
	// determine whether the point lies inside or outside of the triangle
	return (barycentricCoords.x >= 0 && 
			barycentricCoords.y >= 0 &&
			barycentricCoords.z >= 0);
}



/* -- COMPUTE BARYCENTRIC COORDINATES ------------------------------------------------*/
Vector3	GeometricMath::ComputeBarycentricCoordinates(const Triangle& triangle,
													 const Vector3&  point)
{
	// compute the normal of the triangle
	Vector3 normal = GeometricMath::ComputeTriangleNormal(triangle);
	
	// convert the normal to an absolute representation
	normal.Absolute();

	/*
		In order to get the most accurate calculation,  it is optimal to
		project the triangle onto the plane that will give the projected 
		triangle the largest possible area.  this is done by taking the
		largest absolute component of the normal, and discarding this
		component from the supplied point and triangle

		Triangle after projection (assume XY projection):
				v1
				  /	\
				 /	 \					Assume:  Positive Y is upwards
				/	  \						     Positive X is to the right
			   /	   \
			  /			\
			 /			 \
			/			  \
		   /	. <- point \
          /					\
		 /					 \
		/					  \
	 v0 ----------------------- v2

		Assume the triangle has been projected onto the appropriate plane.
		Let p = point, and assume the v0, v1 and v2 are the coordinates that make
		up the Triangle 'triangle'

		Eight 2-Dimensional vectors now need to be calculated.  This calculation 
		will assist in determining the relative weights of the vertices to formulate
		the point 'p' through the barycentric computations

		Assume we are projected onto the XY plane.

		We need to find: 
			(a) the vector represented by v2->v0 for X (edge 1)
			(b) the vector represented by v2->v0 for Y (edge 1)
			(c) the vector represented by v2->v1 for X (edge 2)
			(d) the vector represented by v2->v1 for Y (edge 2)
			(e) the vector represented by v0->p  for X (inner edge 1)
			(f) the vector represented by v0->p  for Y (inner edge 1)
			(g) the vector represented by v2->p  for X (inner edge 2)
			(h) the vector represented by v2->p  for Y (inner edge 2)

		This can be expressed (irrespective of projection) by:
			float v2_v0_axis1,  // edge 1
				  v2_v0_axis2,  // edge 1
				  v2_v1_axis1,  // edge 2
				  v2_v1_axis2,  // edge 2
				  v0_p_axis1,   // inner edge 1
				  v0_p_axis2,   // inner edge 1
				  v2_p_axis1,   // inner edge 2
				  v2_p_axis2;   // inner edge 2

	     Now, back to our assumed XY projection:
		    v2_v0_axis1 = v0.x - v2.x;	// edge 1
			v2_v0_axis2 = v0.y - v2.y;	// edge 1
			v2_v1_axis1 = v1.x - v2.x;	// edge 2
			v2_v1_axis2 = v1.y - v2.y;	// edge 2
			v0_p_axis1	= p.x  - v0.x;	// inner edge 1
			v0_p_axis2	= p.y  - v0.y;	// inner edge 1
			v2_p_axis1	= p.x  - v2.x;	// inner edge 2
			v2_p_axis2	= p.y  - v2.y;	// inner edge 2

		 And the same goes for the other projections.
	*/

	float v2_v0_axis1,  // edge 1
		  v2_v0_axis2,  // edge 1
		  v2_v1_axis1,  // edge 2
		  v2_v1_axis2,  // edge 2
		  v0_p_axis1,   // inner edge 1
		  v0_p_axis2,   // inner edge 1
		  v2_p_axis1,   // inner edge 2
		  v2_p_axis2;   // inner edge 2

	if(normal.x >= normal.y && normal.x >= normal.z)
	{
		// discard 'x' component, project onto yz plane
		v2_v0_axis1 = triangle.v1.y - triangle.v3.y; // edge 1
		v2_v0_axis2 = triangle.v1.z - triangle.v3.z; // edge 1
		v2_v1_axis1 = triangle.v2.y - triangle.v3.y; // edge 2
		v2_v1_axis2 = triangle.v2.z - triangle.v3.z; // edge 2
		v0_p_axis1	= point.y - triangle.v1.y;		 // inner edge 1
		v0_p_axis2	= point.z - triangle.v1.z;		 // inner edge 1
		v2_p_axis1	= point.y - triangle.v3.y;		 // inner edge 2
		v2_p_axis2	= point.z - triangle.v3.z;		 // inner edge 2		
	}
	else if(normal.y >= normal.z)
	{
		// discard 'y' component, project onto xz plane
		v2_v0_axis1 = triangle.v1.z - triangle.v3.z; // edge 1
		v2_v0_axis2 = triangle.v1.x - triangle.v3.x; // edge 1
		v2_v1_axis1 = triangle.v2.z - triangle.v3.z; // edge 2
		v2_v1_axis2 = triangle.v2.x - triangle.v3.x; // edge 2
		v0_p_axis1	= point.z - triangle.v1.z;		 // inner edge 1
		v0_p_axis2	= point.x - triangle.v1.x;		 // inner edge 1
		v2_p_axis1	= point.z - triangle.v3.z;		 // inner edge 2
		v2_p_axis2	= point.x - triangle.v3.x;		 // inner edge 2
	}
	else
	{
		// discard 'z' component, project onto xy plane
		v2_v0_axis1 = triangle.v1.x - triangle.v3.x; // edge 1
		v2_v0_axis2 = triangle.v1.y - triangle.v3.y; // edge 1
		v2_v1_axis1 = triangle.v2.x - triangle.v3.x; // edge 2
		v2_v1_axis2 = triangle.v2.y - triangle.v3.y; // edge 2
		v0_p_axis1	= point.x - triangle.v1.x;		 // inner edge 1
		v0_p_axis2	= point.y - triangle.v1.y;		 // inner edge 1
		v2_p_axis1	= point.x - triangle.v3.x;		 // inner edge 2
		v2_p_axis2	= point.y - triangle.v3.y;		 // inner edge 2
	}

	/*
		Now the computation is complete, it is time to compute the
		barycentric coordinates.

		Computing barycentric coordinates for a 2d point is defined 
		as follows (following on from above comments)

				v2_p_axis2  * v2_v1_axis1 - v2_v1_axis2 * v2_p_axis1
		b1 =	-----------------------------------------------------
				v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1

				v0_p_axis2  * v2_v0_axis1 - v2_v0_axis2 * v0_p_axis1
		b2 =	-----------------------------------------------------
				v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1

		b3 also contains its own equation, BUT since b3 can be derrived from
		b1 and b2 which will already be calculated, it is not necessary to 
		compute it
	*/

	// pre-calculate the denominator
	float denominator = v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1;

	// check for a division by zero (this would imply the supplied triangle
	// was co-linear)
	if(!denominator)
	{
		throw "Division by zero due to co-linear triangle.  "
			  "GeometricMath::ComputeBarycentricCoordinates)";
	}

	// compute the X and Y barycentric coordinates
	Vector3 barycentricResult =	Vector3((v2_p_axis2  * v2_v1_axis1 - 
										 v2_v1_axis2 * v2_p_axis1) / denominator,
										(v0_p_axis2  * v2_v0_axis1 - 
										 v2_v0_axis2 * v0_p_axis1) / -denominator,
										 0.0f);

	// derrive the Z component
	barycentricResult.z = 1.0f - barycentricResult.x - barycentricResult.y;

	return barycentricResult;
}
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
#ifndef SKULLBONEZ_DYNAMICS_OBJECT_H
#define SKULLBONEZ_DYNAMICS_OBJECT_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Math
	{
		namespace CollisionDetection
		{
			/* -- Dynamics Object ----------------------------------------------------------------------------------------------------------------------------------------
			
				An abstract base for all collision objects to enable polymorphism.
			-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
			class DynamicsObject
			{

			protected:

				Vector3				position;		// Position of centre of object



			public:

				virtual void		DEBUG_RenderCollisionVolume		(const Vector3& worldSpaceCoords)		= 0;  	// Debug routine to render a representation of the collision volume
				virtual float		GetSubmergedVolumePercent		(float fluidSurfaceHeight)				= 0;	// Returns the percent of the object submerged in fluid
				virtual float		GetVolume						(void)									= 0;	// Returns the volume of the object
				virtual float		GetDragCoefficient				(void)									= 0;	// Returns the drag coefficient for the object
				virtual float		GetProjectedSurfaceArea			(void)									= 0;	// Returns the surface area of the objects 2d projection based on its current orientation and velocity
				virtual float		TestCollision					(const DynamicsObject& target,
																	 const Ray&			   targetRay,
																	 const Ray&			   focusRay)		= 0;	// Sweep test to be performed by all children, for all children (all children should be testable against each other)
				
				const Vector3&		GetPosition				(void)			   { return this->position; }			// Get reference to position of centre of object
			};
		}
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
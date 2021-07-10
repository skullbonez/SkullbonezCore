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
#ifndef SKULLBONEZ_GAME_MODEL_H
#define SKULLBONEZ_GAME_MODEL_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezCommon.h"
#include "SkullbonezRigidBody.h"
#include "SkullbonezDynamicsObject.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezResponseInformation.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::CollisionDetection;



namespace SkullbonezCore
{
	namespace Environment { class WorldEnvironment; }		// Forward declaration
	namespace Physics	  { class CollisionResponse; }		// Forward declaration

	namespace GameObjects
	{
		/* -- Game Model -------------------------------------------------------------------------------------------------------------------------------------------------

			Represents the highest level object in the library - a renderable mesh 
			with collision bounds and physics information.

			TODO: 3DS MODEL MESH BELONGS HERE!
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class GameModel
		{
			friend class									CollisionResponse;													// Declare class Collision Response as a friend of class Game Model

		private:

			DynamicsObject*									boundingVolume;														// Pointer to the bounding volume
			RigidBody										physicsInfo;														// Physics information for the game object
			Environment::WorldEnvironment*					worldEnvironment;													// Pointer to the world environment settings
			Geometry::Terrain*								terrain;															// Pointer to the world terrain
			Physics::ResponseInformation					responseInformation;												// Information regarding a collision response that needs to be reacted to
			float											projectedSurfaceArea;												// 2d surface area approximation based on dynamics object list
			float											dragCoefficient;													// Calculated based on the average drag coefficient of all dynamics objects
			bool											isGrounded;															// Flag to indicate whether the model is on the ground or not
			bool											isResponseRequired;													// Indicates whether a response is required or not

			void			OrientMesh						(void);																// Orients the mesh based on its orientation
			void			CalculateVolume					(void);																// Calculates the volume of the model
			void			ApplyWorldForces				(float changeInTime);												// Apply forces on the body from the world environment
			void			UpdateModelInfo					(void);																// Perform this operation every time the model has objects added or removed from its object list
			float			GetTerrainCollisionTime			(float changeInTime);												// Gets the time of collision between the current GameModel instance and the terrain
			float			GetModelCollisionTime			(GameModel& collisionTarget, float changeInTime);					// Gets the time of collision between the current GameModel instance and collisionTarget
			void			DEBUG_SetSphereToTerrain		(void);																// Debug routine - ensure sphere does not go through terrain


		public:
							GameModel						(Environment::WorldEnvironment* pWorldEnv,
															 const Vector3& vPosition,
															 const Vector3& vRotationalInertia,
															 float fMass);														// Overloaded constructor
							~GameModel						(void);																// Default destructor

			void			RenderModel						(void);																// Renders the entire game model
			void			RenderCollisionBounds			(void);																// Renders the collision bounds
			bool			IsResponseRequired				(void);																// Indicates whether a collision response is required
			float			GetSubmergedVolumePercent		(void);																// Returns the percentage of the game model submerged in fluid
			float			GetMass							(void);																// Returns the mass of the game model
			float			GetVolume						(void);																// Returns the volume of the game model
			void			CalculateProjectedSurfaceArea	(void);																// Calculates the sum of the surface area of the game model
			bool			IsGrounded						(void);																// Gets the isGrounded flag
			void			SetIsGrounded					(bool fIsGrounded);													// Sets the isGrounded flag
			void			CalculateDragCoefficient		(void);																// Calculates the drag coefficient of the model
			float			GetProjectedSurfaceArea			(void);																// Returns the projected surface area of the model
			float			GetDragCoefficient				(void);																// Returns the drag coefficient of the model
			const Vector3&	GetPosition						(void);																// Returns the position of the game model
			const Vector3&	GetVelocity						(void);																// Returns the velocity of the model
			const Vector3&  GetAngularVelocity				(void);																// Returns the angular velocity of the model
			void			ApplyForces						(float changeInTime);												// Update the models velocity based on its current physicsInfo
			void			UpdatePosition					(float changeInTime);												// Update the models position based on its current physicsInfo
			void			SetTerrain						(Geometry::Terrain* pTerrain);										// Sets the terrain pointer
			float			CollisionDetectTerrain			(float changeInTime);												// Collision detect model against terrain
			void			CollisionResponseTerrain		(float changeInTime);												// Collision response model against terrain
			void			SetImpulseForce					(const Vector3& vForce, const Vector3& vApplicationPoint);			// Sets an impulse force for the model
			void			SetCoefficientRestitution		(float fCoefficientRestitution);									// Sets the coefficient of restitution for the game model
			void			SetWorldForce					(const Vector3& vWorldForce, const Vector3& vWorldTorque);			// Sets the worlds forces acting on the model
			void			AddBoundingSphere				(float fRadius);													// Add a bounding sphere to the game model
			float			CollisionDetectGameModel		(GameModel&	collisionTarget, float changeInTime);					// Collision detect model against model
			void			CollisionResponseGameModel		(GameModel&	responseTarget, float remainingTimeStep);				// Collision response model against model
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
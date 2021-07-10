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
#ifndef SKULLBONEZ_CAMERA_H
#define SKULLBONEZ_CAMERA_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Environment
	{
		class CameraCollection;			// Forward declaration



		/* -- Camera ------------------------------------------------------------------------------------------------------------------------------------------------------

			The camera class provides the base functionality for a manouverable camera in 3d space.  Uses matrix rotations, therefore Gimbel lock safe.
			This class has no public interface, but defines CameraCollection as a Friend.
		*/
		class Camera
		{
			friend CameraCollection;	// Friend declaration


		public:

			enum TravelDirection	{ Forward, Left, Right, Backward };									// Helps with moving the camera around the scene


		private:

			Vector3		Position;								// Position
			Vector3		View;									// View
			Vector3		UpVector;								// Up vector
			Vector3		MovementBuffer;							// Temporary storage to test a translation before applying it
			float		ViewMagnitude;							// Keeps track of the magnitude of the view vector
			bool		IsFinishedTranslationRecursed;			// Makes sure finished translation method is only recursed once (used for view vector magnitude preservation)
			bool		DoCalculateViewMagnitude;				// Specifies whether the view magnitude should be calculated or not (used for view vector magnitude preservation)
			bool		DoPreserveViewMagnitude;				// Specified whether view magnitude preservation should be enforced
			bool		IsLockedMode;							// Locks the view vector from being moved
			XZBounds	Boundary;								// Boundary the camera must not translate beyond
			XZCoords	XZStore;								// Stores XZ coordinates for bounds checking


							Camera								(void);									// Default constructor
							~Camera								(void);									// Default destructor
			void			PrepareTranslation					(void);									// Assists in keeping translations within bounds, should be called before all translations
			void			FinishTranslation					(void);									// Assists in keeping translations within bounds, should be called after all translations
			void			ApplyMovementBuffer					(void);									// Applies a camera translation
			void			ZeroCamera							(void);									// Sets all vector members to zero vector
			Vector3			GetViewVectorNormalised				(void);									// Returns the normalised view vector
			Vector3			GetViewVectorRaw					(void);									// Returns the non-normalised view vector
			Vector3			GetRightVector						(void);									// Returns the normalised right vector
			float			UpVectorViewVectorRotationCap		(float requestRadians);					// Returns a capped value in radians of what is safe to rotate before the view vector hits the up vector
			void			RecoverViewMagnitude				(bool isOnBoundX,
																 bool isOnBoundZ);						// Recovers view magnitude if under quota, indirectly recurses FinishTranslation function
			void			SetAll								(const Vector3& vPosition,
																 const Vector3& vView,
																 const Vector3& vUpVector);				// Set all by vectors
			void			MoveCamera							(const TravelDirection enumDir,
																 float				   fQuantity);		// Move the camera specified amount in specified direction
			void			AddCamera							(const Camera& updateCamera);			// Adds a camera to the current instance
			void			RotateCamera						(float xMove,
																 float yMove);							// Offers an arbitrary rotation suitable for mouse input


			Camera&			operator=							(const Camera& target);					// Equality operator overload
			Camera&			operator+=							(const Camera& target);					// += Overload			
			Camera			operator-							(const Camera& target);					// Binary subtraction operator overload
			Camera			operator*							(float f);								// Multiplication by scalar overload
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
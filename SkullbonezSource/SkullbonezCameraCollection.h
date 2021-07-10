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
#ifndef SKULLBONEZ_CAMERA_COLLECTION_H
#define SKULLBONEZ_CAMERA_COLLECTION_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezCamera.h"
#include "SkullbonezTerrain.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Environment
	{
		/* -- Camera Collection ------------------------------------------------------------------------------------------------------------------------------------------

			A singleton class that holds a collection of Camera objects and performs operations on these multiple cameras such as tweens, camera changes etc.
			This class is a friend of the Camera class.  The camera class has no public interface - cameras must be used through the CameraCollection class.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class CameraCollection
		{

		private:

			static CameraCollection*	pInstance;				// Singleton instance pointer
			Camera*						cameraArray;			// Holds the cameras
			Camera						primaryStore;			// Holds initial position of primary camera after selection as primary (helps calculate how much to update the other cameras when the current primary loses its focus)
			Camera						tweenPath;				// Holds the current tweening path
			Camera						tweenCamera;			// Holds the state of the current tween
			Camera						tweenStart;				// Holds the tweens starting camera
			char**						cameraNames;			// Holds the camera names
			int							maxCameraCount;			// Maximum number of cameras
			int							arrayPosition;			// Current array position
			int							selectedCamera;			// Current selected camera			
			float						tweenSpeed;				// Camera tweening speed
			bool						isTweening;				// Flag indicating whether the camera is in the middle of a tween or not
			float						tweenProgress;			// Stores the state of the current tween			
			Terrain*					terrain;				// Stores a pointer to the terrain for tweening collision purposes

	
										CameraCollection		(int iCameraCount);			// Constructor
										~CameraCollection		(void);						// Default destructor
			void						SetGluLookAt			(Camera& cCameraData);		// Sets glu look at with the supplied camera data
			int							FindIndex				(const char* sCameraName);	// Returns the index of the specified camera
			Camera						GetUpdateCamera			(void);						// Returns the current update camera for relative updates
			void						UpdateTweenPath			(void);						// Alters the tween path member to end at the required destination (it is important to call this during tweens as the destination can move about the scene during the tween)
			void						SetTweenPath			(int fromIndex, 
																 int toIndex);				// Sets the tween path member to the required tween path (specify fromIndex as -1 to use the tween camera as the from camera)



		public:



			static CameraCollection*	Instance						(int iCameraCount);					// Returns a pointer to the sole class instance
			static void					Destroy							(void);								// Deletes the sole class instance
			const Vector3&				GetCameraView					(void);								// Returns the current view of the primary camera
			const Vector3&				GetCameraTranslation			(void);								// Returns the current translation of the primary camera
			const Vector3&				GetCameraTranslation			(const char* sCameraName);			// Returns the current translation of the specified camera
			void						SetViewCoordinates				(const Vector3& vView);				// Sets the primary camera view position (helpful for keeping focused on an object)
			void						SetTweenSpeed					(float fTweenSpeed);				// Sets the tween transition speed
			void						SetCamera						(void);								// Takes care of setting the camera in the specified position.  Should be called once per frame when the camera has been updated
			bool						IsPrimaryLocked					(void);								// Returns whether the primary camera is in locked mode or not
			void						SetLockedMode					(bool fIsLocked);					// Sets the camera to or from locked mode
			void						TranslateToView					(void);								// Translates to the primary view position
			void						TranslateToView					(const char* sCameraName);			// Translates to the specified view position
			void						AmmendPrimaryY					(float yCoordinate);				// Translates the primary cameras Y position to the specified world coordinate
			void						TranslateToPosition				(void);								// Translates to the primary cameras translation
			void						TranslateToPosition				(const char* sCameraName);			// Translates to the specified cameras translation
			void						TranslateToPosition				(float yValue);						// Translates to the primary cameras XZ translation with supplied Y translation
			void						SetCameraXZBounds				(const XZBounds	bounds);			// Set a camera boundary for all cameras
			void						ResetRelativity					(void);								// Resets the difference camera to the current camera (call this after all camera updates have been made)
			bool						IsCameraTweening				(void);								// Returns a flag indicating if the camera is currently tweening or not
			void						DEBUG_PlotViewVectors			(void);								// Plots the view vectors using gl line primitves (debug routine)
			float						DEBUG_GetViewMag				(void);								// Gets the magnitude of the primary view vector (debug routine)			
			float						DEBUG_GetViewMagTarget			(void);								// Returns the target magnitude of the primary view vector (debug routine)
			char*						GetSelectedCameraName			(void);								// Returns the name of the selected camera
			bool						IsCameraSelected				(const char* sCameraName);			// Returns a flag indicating if the specified camera is selected
			void						ApplyPrimaryMovementBuffer		(void);								// Applies the movement buffer to the primary camera
			const Vector3&				GetPrimaryMovementBuffer		(void);								// Returns the primary cameras movement buffer
			void						SetTerrain						(Terrain* cTerrain);				// Sets the terrain pointer
			void						RotatePrimary					(float xMove, float yMove);			// Rotates the primary camera

			void						SetCameraXZBounds				(const char* sCameraName, const XZBounds bounds);					// Set a camera boundary for a specific camera
			void						RelativeUpdate					(const char* sCameraName, float yMin, float yMax);					// Updates specified camera relative to primary, limit to specified y coordinate (minimum)
			void						MovePrimary						(Camera::TravelDirection enumDir, float fQuantity);					// Moves the primary camera in the specified ditection
			void						SelectCamera					(const char* sCameraName, bool fTween);								// Selects a camera as primary

			void						AddCamera						(const Vector3& vPosition, const Vector3& vView, const Vector3& vUp, const char* sCameraName);			// Add a camera to the camera collection
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
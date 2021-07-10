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
#ifndef SKULLBONEZ_RUN_H
#define SKULLBONEZ_RUN_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezCameraCollection.h"
#include "SkullbonezTimer.h"
#include "SkullbonezInput.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezWindow.h"
#include "SkullbonezText.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezSkyBox.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"


/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::GameObjects;



namespace SkullbonezCore
{
	namespace Basics
	{
		/* -- Skullbonez Run ---------------------------------------------------------------------------------------------------------------------------------------------

			Harness for the Skullbonez Core graphics library.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class SkullbonezRun
		{

		private:
			
			int						selectedCamera;				// Keeps track of which camera is selected
			int						modelCount;					// Number of models in the scene
			float					physicsTime, r_physicsTime;	// Physics time
			float					renderTime, r_renderTime;	// Render time
			float					r_fpsTime;					// FPS time
			float					timeSinceLastRender;		// Render helper
			float					cameraTime;					// Camera helper
			CameraCollection*		cCameras;					// SkullbonezCore::Environment::CameraCollection class
			Timer					cFrameTimer;				// SkullbonezCore::Environment::Timer class
			Timer					cWorkTimer;					// SkullbonezCore::Environment::Timer class
			Timer					cUpdateTimer;				// SkullbonezCore::Environment::Timer class
			Timer					cCameraTimer;				// SkullbonezCore::Environment::Timer class
			Timer					cSimulationTimer;			// SkullbonezCore::Environment::Timer class
			TextureCollection*		cTextures;					// SkullbonezCore::Textures::TextureCollection class
			SkullbonezWindow*		cWindow;					// SkullbonezCore::Basics::SkullbonezWindow class
			Terrain*				cTerrain;					// SkullbonezCore::Geometry::Terrain class
			SkyBox*					cSkyBox;					// SkullbonezCore::Geometry::SkyBox class
			WorldEnvironment*		cWorldEnvironment;			// SkullbonezCore::Environment::WorldEnvironment class
			GameModelCollection*	cGameModelCollection;		// SkullbonezCore::GameObjects::GameModelCollection class
			


			void					Render					(void);								// Main render method
			void					RelativeUpdateCamera	(const char* cameraName);			// Relative update specified camera
			void					UpdateLogic				(float fSecondsPerFrame);			// Update world logic
			void					TakeInput				(void);								// Take user input
			void					SetUpCameras			(void);								// Camera init
			void					SetUpGameModels			(void);								// Game model init
			void					DrawPrimitives			(void);								// Draw OpenGL primitives here
			void					SetInitialOpenGlState	(void);								// Sets the initial state of the OpenGL evironment
			void					SetViewingOrientation	(void);								// Renders camera views etc			
			void					DrawWindowText			(const double dSecondsPerFrame);	// Renders text to the window
			void					MoveCamera				(float keyMovementQty, 
															 float mouseMovemementQty);			// Moves the camera


		public:

									SkullbonezRun			(void);			// Default constructor
									~SkullbonezRun			(void);			// Default destructor
			void					Initialise				(void);			// Initialises the SkullbonezRun class
			bool					Run						(void);			// Runs the application after initialisation - main message loop
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
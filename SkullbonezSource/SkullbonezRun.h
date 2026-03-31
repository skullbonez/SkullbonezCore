/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_RUN_H
#define SKULLBONEZ_RUN_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include <memory>
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
#include "SkullbonezTestScene.h"


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
			
			static int				sGlResetPass;				// GL reset test pass counter (persists across instances)
			static int				sPerfPass;					// Perf test pass counter (persists across instances)
			bool					isSceneMode;				// Scene file mode (deterministic, data-driven)
			bool					isScenePhysics;				// Physics enabled in scene mode
			bool					isSceneText;				// Text overlay enabled in scene mode
			bool					isGlResetTest;				// Test GL context recreation
			bool					isPerfTest;					// Performance logging mode
			bool					isScreenshotSaved;			// Screenshot already written this run
			int						targetFrameCount;			// Frames to render before holding (-1 = unlimited)
			int						currentFrame;				// Current frame counter for scene mode
			int						screenshotFrame;			// Save screenshot at this frame (-1 = unused)
			int						screenshotMs;				// Save screenshot at this elapsed ms (-1 = unused)
			char					scenePath[256];				// Path to scene file (empty = legacy mode)
			char					screenshotPath[256];		// Output path for screenshot (empty = none)
			char					perfLogPath[256];			// Output path for perf CSV (empty = none)
			FILE*					perfLogFile;				// Open handle for perf CSV
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
			std::unique_ptr<Terrain>	cTerrain;				// SkullbonezCore::Geometry::Terrain class
			SkyBox*					cSkyBox;					// SkullbonezCore::Geometry::SkyBox class
			WorldEnvironment		cWorldEnvironment;			// SkullbonezCore::Environment::WorldEnvironment class
			GameModelCollection		cGameModelCollection;		// SkullbonezCore::GameObjects::GameModelCollection class
			


			void					Render					(void);								// Main render method
			void					RelativeUpdateCamera	(uint32_t hash);					// Relative update specified camera
			void					UpdateLogic				(float fSecondsPerFrame);			// Update world logic
			void					TakeInput				(void);								// Take user input
			void					SetUpCameras			(void);								// Camera init (legacy mode)
			void					SetUpCamerasFromScene	(const TestScene& scene);			// Camera init from scene file
			void					SetUpGameModels			(int count = 300);				// Game model init (random legacy mode)
			void					SetUpGameModelsFromScene(const TestScene& scene);			// Game model init from scene file
			void					DrawPrimitives			(void);								// Draw OpenGL primitives here
			void					SetInitialOpenGlState	(void);								// Sets the initial state of the OpenGL evironment
			void					SetViewingOrientation	(void);								// Renders camera views etc			
			void					DrawWindowText			(const double dSecondsPerFrame);	// Renders text to the window
			void					SaveScreenshot			(const char* path);					// Saves framebuffer to BMP file via glReadPixels
			void					LogPerfMemory			(const char* checkpoint);			// Log memory usage to perf CSV
			void					MoveCamera				(float keyMovementQty, 
															 float mouseMovemementQty);			// Moves the camera


		public:

									SkullbonezRun			(const char* pScenePath = nullptr);	// Constructor (nullptr = legacy mode)
									~SkullbonezRun			(void);								// Default destructor
			void					Initialise				(void);								// Initialises the SkullbonezRun class
			bool					Run						(void);								// Runs the application after initialisation - main message loop
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
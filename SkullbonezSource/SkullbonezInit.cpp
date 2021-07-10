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
#include "SkullbonezCommon.h"
#include "SkullbonezRun.h"
#include "SkullbonezWindow.h"
#include "SkullbonezTimer.h"
#include <float.h>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;



/* -- WIN MAIN --------------------------------------------------------------------*/
// "Windows Main" - this function is the execution entry point for the application
int WINAPI WinMain(HINSTANCE hInstance,		// Holds info on instance of app
				   HINSTANCE hPrevInstance, // Some useless Win32 junk
				   PSTR szCmdLine,			// Params passed from command line
				   int iCmdShow)			// Window state (maximised, hide etc)
{
	// Heap debug code - breaks program at specified allocation
	// _CrtSetBreakAlloc(89);

	// floating point check routine
	// _controlfp(0, _MCW_EM ^ _EM_INEXACT);

	// Supress benign (level 4) warnings
	hPrevInstance; szCmdLine; iCmdShow;

	// Create an instance of our window class
	// (holds everything associated with our window)
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	// Create the application window
	cWindow->CreateAppWindow(hInstance, FULLSCREEN_MODE);

	// Get the device context for our window
	cWindow->sDevice = GetDC(cWindow->sWindow);

	for(;;)
	{
		// Init OpenGL
		cWindow->InitialiseOpenGL();

		// Create the Skullbonez Core instance
		SkullbonezRun *cRun = new SkullbonezRun();

		try
		{
			// Attempt to initialise the Skullbonez Core
			cRun->Initialise();

			// Attempt to run the Skullbonez Core
			if(!cRun->Run()) throw "Thanks for using the Skullbonez Core!";

			// Attempt to delete the Skullbonez Core
			if(cRun)
			{
				delete cRun;
				cRun = 0;
			}

			// Cleanup rendering context
			if(cWindow->sRenderContext)
			{
				// Free rendering memory, rollback all changes
				wglMakeCurrent(NULL, NULL);

				// Delete the rendering context
				wglDeleteContext(cWindow->sRenderContext);
			}
		}
		catch (char* Exception)  // Catch all exceptions thrown by the Skullbonez Core
		{
			// Display the nasty exception details
			cWindow->MsgBox(Exception, "Alert!", MB_OK);

			// Delete the Skullbonez Core if we were interupted earlier
			if(cRun) delete cRun;

			// exit the loop now
			break;
		}
	}

	// Cleanup rendering context
	if(cWindow->sRenderContext)
	{
		// Free rendering memory, rollback all changes
		wglMakeCurrent(NULL, NULL);

		// Delete the rendering context
		wglDeleteContext(cWindow->sRenderContext);
	}

	// Cleanup device context (Free device context associated with our window)
	if(cWindow->sDevice) ReleaseDC(cWindow->sWindow,
								   cWindow->sDevice);

	// Restore desktop settings
	if(cWindow->fIsFullScreenMode)
	{
		ChangeDisplaySettings(NULL, 0);	// Switch back to desktop mode
		ShowCursor(true);				// Bring mouse pointer back
	}

	// Free up memory associated with the window class
	UnregisterClass(WINDOW_NAME, hInstance);

	// Delete our window class
	cWindow->Destroy();

	// Write memory leaks to output window
	// _CrtDumpMemoryLeaks();

	// Return wParam of our msg struct
	return 0;
}
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
#include "SkullbonezCommon.h"
#include "SkullbonezRun.h"
#include "SkullbonezWindow.h"
#include "SkullbonezTimer.h"
#include <float.h>
#include <cstring>



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
	hPrevInstance; iCmdShow;

	// Parse command line for --scene <path>
	const char* scenePath = nullptr;
	if (szCmdLine && szCmdLine[0] != '\0')
	{
		const char* sceneArg = strstr(szCmdLine, "--scene");
		if (sceneArg)
		{
			sceneArg += 7; // skip "--scene"
			while (*sceneArg == ' ') ++sceneArg;
			if (*sceneArg != '\0') scenePath = sceneArg;
		}
	}

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

		bool shouldRestart = false;
		{
			// Create the Skullbonez Core instance (scoped so destructor runs
			// BEFORE GL context deletion — ensures GL cleanup calls work)
			SkullbonezRun cRun(scenePath);

			try
			{
				// Attempt to initialise the Skullbonez Core
				cRun.Initialise();

				// Attempt to run the Skullbonez Core
				if(!cRun.Run())
				{
					if (scenePath)
						break; // clean exit for test harness — no MessageBox
					else
						throw std::runtime_error("Thanks for using the Skullbonez Core!");
				}

				shouldRestart = true;
			}
			catch (const std::exception& e)  // Catch all exceptions thrown by the Skullbonez Core
			{
				if (!scenePath)
					cWindow->MsgBox(e.what(), "Alert!", MB_OK);

				// exit the loop now
				break;
			}
			// cRun destroyed here — GL context still alive for proper cleanup
		}

		// Cleanup rendering context AFTER cRun is destroyed
		if (shouldRestart && cWindow->sRenderContext)
		{
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(cWindow->sRenderContext);
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
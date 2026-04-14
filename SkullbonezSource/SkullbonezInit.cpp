/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\"VUUUV"/?
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
#include <vector>
#include <string>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;



/* ================================================================================
   SHARED: command-line argument parsing
   Populates sceneList; returns isSuiteOrSceneMode.
   On Windows szCmdLine is a single string; on Linux we have argc/argv.
   ================================================================================ */
static bool ParseArgs(std::vector<std::string>& sceneList,
					  const char*               suiteArg,
					  const char*               sceneArg)
{
	bool isSuiteOrSceneMode = false;

	if (suiteArg && suiteArg[0] != '\0')
	{
		FILE* f = nullptr;
		if (fopen_s(&f, suiteArg, "r") == 0 && f)
		{
			char line[512];
			while (fgets(line, sizeof(line), f))
			{
				size_t len = strlen(line);
				while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' '))
					line[--len] = '\0';
				if (len > 0 && line[0] != '#')
					sceneList.push_back(line);
			}
			fclose(f);
		}
		isSuiteOrSceneMode = true;
	}
	else if (sceneArg && sceneArg[0] != '\0')
	{
		sceneList.push_back(sceneArg);
		isSuiteOrSceneMode = true;
	}

	if (sceneList.empty())
		sceneList.push_back("");  // legacy mode

	return isSuiteOrSceneMode;
}



/* ================================================================================
   SHARED: run the scene list (called by both WinMain and main)
   ================================================================================ */
static int RunSceneList(std::vector<std::string>& sceneList,
						bool isSuiteOrSceneMode)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	bool abortAll = false;
	for (size_t sceneIdx = 0; sceneIdx < sceneList.size() && !abortAll; ++sceneIdx)
	{
		const char* currentScene = sceneList[sceneIdx].empty() ? nullptr : sceneList[sceneIdx].c_str();

		for(;;)
		{
			// Init (or re-init) OpenGL context
			cWindow->InitialiseOpenGL();

			bool shouldRestart = false;
			{
				SkullbonezRun cRun(currentScene);
				try
				{
					cRun.Initialise();

					if(!cRun.Run())
					{
						if (currentScene)
						{
							break;
						}
						else
							throw std::runtime_error("Thanks for using the Skullbonez Core!");
					}
					shouldRestart = true;
				}
				catch (const std::exception& e)
				{
					if (!isSuiteOrSceneMode)
						cWindow->MsgBox(e.what(), "Alert!", 0 /*MB_OK*/);
					abortAll = true;
					break;
				}
			}

#ifdef _WIN32
			// Windows: destroy / recreate GL context between passes
			if (shouldRestart && cWindow->sRenderContext)
			{
				wglMakeCurrent(NULL, NULL);
				wglDeleteContext(cWindow->sRenderContext);
			}
			else
			{
				break;
			}
#else
			// Linux: GLFW context stays alive; just re-run InitialiseOpenGL next pass
			if (!shouldRestart) break;
#endif
		}

#ifdef _WIN32
		// Between scenes: destroy GL context so the next scene gets a fresh one
		if (!abortAll && sceneIdx + 1 < sceneList.size() && cWindow->sRenderContext)
		{
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(cWindow->sRenderContext);
		}
#endif
	}

	return 0;
}



/* ================================================================================
   WINDOWS ENTRY POINT
   ================================================================================ */
#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInstance,
				   HINSTANCE hPrevInstance,
				   PSTR szCmdLine,
				   int iCmdShow)
{
	// Suppress benign (level 4) warnings
	hPrevInstance; iCmdShow;

	// Parse Windows single-string command line
	std::vector<std::string> sceneList;
	const char* suiteArgVal = nullptr;
	const char* sceneArgVal = nullptr;
	char suiteArgBuf[512] = {};
	char sceneArgBuf[512] = {};

	if (szCmdLine && szCmdLine[0] != '\0')
	{
		const char* sa = strstr(szCmdLine, "--suite");
		const char* sc = strstr(szCmdLine, "--scene");
		if (sa)
		{
			sa += 7;
			while (*sa == ' ') ++sa;
			strcpy_s(suiteArgBuf, sizeof(suiteArgBuf), sa);
			suiteArgVal = suiteArgBuf;
		}
		else if (sc)
		{
			sc += 7;
			while (*sc == ' ') ++sc;
			strcpy_s(sceneArgBuf, sizeof(sceneArgBuf), sc);
			sceneArgVal = sceneArgBuf;
		}
	}

	bool isSuiteOrSceneMode = ParseArgs(sceneList, suiteArgVal, sceneArgVal);

	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	cWindow->CreateAppWindow(hInstance, FULLSCREEN_MODE);
	cWindow->sDevice = GetDC(cWindow->sWindow);

	RunSceneList(sceneList, isSuiteOrSceneMode);

	// Cleanup GL context
	if(cWindow->sRenderContext)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(cWindow->sRenderContext);
	}

	// Cleanup device context
	if(cWindow->sDevice)
		ReleaseDC(cWindow->sWindow, cWindow->sDevice);

	// Restore desktop settings
	if(cWindow->fIsFullScreenMode)
	{
		ChangeDisplaySettings(NULL, 0);
		ShowCursor(true);
	}

	UnregisterClass(WINDOW_NAME, hInstance);
	cWindow->Destroy();

	return 0;
}

#else
/* ================================================================================
   LINUX ENTRY POINT
   ================================================================================ */

int main(int argc, char* argv[])
{
	// Parse argc/argv
	std::vector<std::string> sceneList;
	const char* suiteArgVal = nullptr;
	const char* sceneArgVal = nullptr;

	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc)
			suiteArgVal = argv[++i];
		else if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
			sceneArgVal = argv[++i];
	}

	bool isSuiteOrSceneMode = ParseArgs(sceneList, suiteArgVal, sceneArgVal);

	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	cWindow->CreateAppWindow(FULLSCREEN_MODE);

	int ret = RunSceneList(sceneList, isSuiteOrSceneMode);

	// GLFW cleanup
	if (cWindow->sWindow)
		glfwDestroyWindow(cWindow->sWindow);
	glfwTerminate();

	cWindow->Destroy();
	return ret;
}

#endif // _WIN32

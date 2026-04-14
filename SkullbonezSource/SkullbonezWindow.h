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
#ifndef SKULLBONEZ_WINDOW_H
#define SKULLBONEZ_WINDOW_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"



namespace SkullbonezCore
{
	namespace Basics
	{
		/* -- Skullbonez Window ------------------------------------------------------------------------------------------------------------------------------------------

			A singleton class representing a Windows OS application window.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class SkullbonezWindow
		{

		private:

			static	SkullbonezWindow			*pInstance;						// Pointer to singleton instance

			SkullbonezWindow					(void);							// Default constructor
			~SkullbonezWindow					(void);							// Default destructor



		public:

#ifdef _WIN32
			HWND	sWindow;			// Handle to window
			HDC		sDevice;			// Handle to device context
			HGLRC	sRenderContext;		// Handle to rendering context
#else
			GLFWwindow* sWindow;		// GLFW window handle (Linux)
			void*       sDevice;		// Unused on Linux (kept for source compat)
			void*       sRenderContext;	// Unused on Linux (GLFW manages context)
#endif
			POINT	sWindowDimensions;	// Window width and height
			bool	fIsFullScreenMode;	// Flag for fullscreen mode


			static SkullbonezWindow*	Instance					(void);						// Call to request a pointer to the singleton instance
			static void					Destroy						(void);						// Call to destroy the singleton instance
			void						HandleScreenResize			(void);						// Reset OpenGL drawing boundaries and aspect ratio when the screen is resized
#ifdef _WIN32
			bool						SetupPixelFormat			(void);						// Prepares pixel format of back and front buffer
#endif
			void						InitialiseOpenGL			(void);						// For all OpenGL API initialisation code (after the window has been created)
			void						SwitchToOrthoMode			(void);						// Switches mode to glOrtho (saves matrix state and restores it when SwitchOutOfOrthoMode is called)
			void						SwitchOutOfOrthoMode		(void);						// switches out of ortho mode and restores matrix prior to SwitchToOrthoMode call
			void						SetTitleText				(const char* cText);		// Draws text to title bar of window
#ifdef _WIN32
			void						SetWindowDimensions			(const RECT dimensions);	// Sets window dimensions by RECT struct
#endif
			void						SetWindowDimensions			(int width,
																	 int height);				// Sets window dimensions by integer values
#ifdef _WIN32
			void						CreateAppWindow				(HINSTANCE hInstance,
																	 bool isFullScreenMode);	// Creates our application window, returns a handle to it
			void						ChangeToFullScreen			(int xResolution,
																	 int yResolution);			// Changes screeen to full screen mode
#else
			void						CreateAppWindow				(bool isFullScreenMode);	// Creates GLFW window (Linux)
#endif
			int							MsgBox						(const char* cMsgBoxText,
																	 const char* cMsgBoxTitle,
																	 const UINT  iMsgBoxType);	// Draws a message box to the screen
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
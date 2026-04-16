/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



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
#include "SkullbonezWindow.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;



/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
SkullbonezWindow* SkullbonezWindow::pInstance = 0;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
SkullbonezWindow::SkullbonezWindow() 
{
	this->sWindow			= 0;
	this->sDevice			= 0;
	this->sRenderContext	= 0;
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkullbonezWindow::~SkullbonezWindow() {}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkullbonezWindow* SkullbonezWindow::Instance(void)
{
	if(!SkullbonezWindow::pInstance)
	{
		static SkullbonezWindow instance;
		SkullbonezWindow::pInstance = &instance;
	}
	return SkullbonezWindow::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkullbonezWindow::Destroy(void)
{
	SkullbonezWindow::pInstance = 0;
}



/* -- SET WINDOW DIMENSIONS -------------------------------------------------------*/
void SkullbonezWindow::SetWindowDimensions(int width, int height)
{
	this->sWindowDimensions.x = width;
	this->sWindowDimensions.y = height;
}



/* -- SET WINDOW DIMENSIONS -------------------------------------------------------*/
void SkullbonezWindow::SetWindowDimensions(const RECT dimensions)
{
	this->sWindowDimensions.x = dimensions.right;
	this->sWindowDimensions.y = dimensions.bottom;
}



/* -- HANDLE SCREEN RESIZE --------------------------------------------------------*/
void SkullbonezWindow::HandleScreenResize(void)
{
	// Guard: skip GL calls if GLAD hasn't loaded function pointers yet.
	// WM_SIZE fires during CreateAppWindow before InitialiseOpenGL.
	if (!glViewport) return;

	// Create an instance of our window class
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	// Set width and height to local variables
	int width = cWindow->sWindowDimensions.x;
	int height = cWindow->sWindowDimensions.y;

	if (!height) height = 1;			// Avoid division by zero
	glViewport(0, 0, width, height);	// Set viewport (screen boundaries)

	// Build and store the perspective projection matrix
	float aspect = (float)width / (float)height;
	cWindow->projectionMatrix = Math::Transformation::Matrix4::Perspective(
		45.0f, aspect, FRUSTUM_CLIP_SHORT_QTY, FRUSTUM_CLIP_FAR_QTY);
}



/* -- CHANGE TO FULLSCREEN --------------------------------------------------------*/
void SkullbonezWindow::ChangeToFullScreen(int xResolution, int yResolution)
{
	/*
		Changes screeen to full screen mode...
		Notes for this method:
		---------------------------------------------------------------------------
		*** This can be quite helpful in some scenarios: ***

		dmSettings.dmBitsPerPel = BITS_PER_PIXEL;		// Set bits per pixel
		dmSettings.dmDisplayFrequency = REFRESH_RATE;	// Set refresh rate
		...
		DM_BITSPERPEL							// We changed bits per pixel
		DM_DISPLAYFREQUENCY;					// We changed display frequency
		---------------------------------------------------------------------------
		*** This code is redundant as DEVMODE struct was set to {0}: ***

		memset(&dmSettings,		 // Beginning at the memory location of dmSettings
			0,					 // set to zero
			sizeof(dmSettings)); // the entire DEVMODE struct
		---------------------------------------------------------------------------
	*/

	DEVMODE dmSettings = {0};	// Device mode variable - required to change modes

	if(!EnumDisplaySettings(NULL,					// Get current screen settings
							ENUM_CURRENT_SETTINGS,
							&dmSettings))
	{
		this->MsgBox("Could Not Enumerate Display Settings", "Error", MB_OK);
		PostQuitMessage(0);
	}

	dmSettings.dmPelsWidth =  xResolution;			// Set new width
	dmSettings.dmPelsHeight = yResolution;			// Set new height

	// Specifiy what we have changed
	dmSettings.dmFields = DM_PELSWIDTH |			// We changed width
						  DM_PELSHEIGHT;			// We changed height

	// Save result of our change
	int result = ChangeDisplaySettings(&dmSettings,		// Change to this struct
									   CDS_FULLSCREEN);	// Remove start bar

	// If we failed, quit
	if(result != DISP_CHANGE_SUCCESSFUL)
	{
		this->MsgBox("Display Mode Not Compatible", "Error", MB_OK);
		PostQuitMessage(0);
	}
}



/* -- SETUP PIXEL FORMAT ----------------------------------------------------------*/
bool SkullbonezWindow::SetupPixelFormat(void)
{
	// Create an instance of our window class
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	PIXELFORMATDESCRIPTOR pfd = {0}; // Declare struct to describe out pixel format
	int pixelFormat = NULL;			 // To hold our bits per pixel information

	// We now fill the PIXELFORMATDESCRIPTOR struct with what we want
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);		// Set size of struct
	pfd.nVersion = 1;								// Always should be 1
	pfd.dwFlags =	PFD_DRAW_TO_WINDOW |			// Set flags we want
					PFD_SUPPORT_OPENGL |			// being window drawing, openGL
					PFD_DOUBLEBUFFER;				// support + two buffers
	pfd.dwLayerMask = PFD_MAIN_PLANE;				// Standard mask
	pfd.iPixelType = PFD_TYPE_RGBA;					// Red Green Blue Alpha pixels
	pfd.cColorBits = BITS_PER_PIXEL;				// BITS_PER_PIXEL is #defined
	pfd.cDepthBits = BITS_PER_PIXEL;				// BITS_PER_PIXEL is #defined
	pfd.cAccumBits = 0;								// No accumulation bits
	pfd.cStencilBits = 0;							// No stencil bits

	// Gets a pixel format that best matches what we have requested
	pixelFormat = ChoosePixelFormat(cWindow->sDevice, &pfd);

	// If pixel format was not set from the previous line, fail
	if(!pixelFormat)
	{
		this->MsgBox("ChoosePixelFormat failed", "Error", MB_OK);
		return false;
	}

	// If SetPixelFormat fails, we fail too
	if(!SetPixelFormat(cWindow->sDevice, pixelFormat, &pfd))
	{
		this->MsgBox("SetPixelFormat failed", "Error", MB_OK);
		return false;
	}

	return true;	// If we have made it down to here we have succeeded
}



/* -- WndProc FUNCTION ------------------------------------------------------------*/
// "Windows Procedure" - this function handles messages for our window
LRESULT CALLBACK WndProc (HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	// Create an instance of our window class
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	PAINTSTRUCT ps = {0};		// Assists with repainting the client area

	try
	{
		// Which message do we have to deal with today...?
		switch (iMsg)
		{
			// WM_CREATE fired on window creation
			case WM_CREATE:
				break;

			// WM_SIZE fired on a resize
			case WM_SIZE:
				// LoWord = width, HiWord = height
				cWindow->SetWindowDimensions(LOWORD(lParam), HIWORD(lParam));
				// Adjust OpenGL viewport matrix
				cWindow->HandleScreenResize();
				break;

			// WM_PAINT fired when client area is invalidated
			case WM_PAINT:
				BeginPaint(hWnd, &ps);	// Init paint struct
				EndPaint(hWnd, &ps);	// End painting
				break;

			case WM_KEYDOWN:
				// Quit if ESCAPE pressed
				if(wParam == VK_ESCAPE) PostQuitMessage(0);
				break;

			// WM_DESTROY is fired when the window is closed
			case WM_DESTROY:
				// Close the window
				PostQuitMessage(0);
				break;
		}
	}
	catch (const std::exception& e)	// Catch all exceptions thrown by the Skullbonez Core
	{
		cWindow->MsgBox(e.what(), "FATAL ERROR", MB_OK);
	}

	// Now we have done whatever we wanted to do, let windows do anything else it
	// needs to do based on the message fired...
	return DefWindowProc (hWnd, iMsg, wParam, lParam);
}



/* -- GLAD LOADER CALLBACK --------------------------------------------------------*/
// GLAD needs a loader that returns GL function pointers.  For core GL 1.1
// functions Windows exposes them directly from opengl32.dll; everything
// else comes from wglGetProcAddress.
static GLADapiproc GladLoadFunc(const char* name)
{
	GLADapiproc p = reinterpret_cast<GLADapiproc>(wglGetProcAddress(name));
	if (p == 0 || p == reinterpret_cast<GLADapiproc>(0x1) ||
		p == reinterpret_cast<GLADapiproc>(0x2) ||
		p == reinterpret_cast<GLADapiproc>(0x3) ||
		p == reinterpret_cast<GLADapiproc>(-1))
	{
		static HMODULE gl = GetModuleHandleA("opengl32.dll");
		p = reinterpret_cast<GLADapiproc>(GetProcAddress(gl, name));
	}
	return p;
}



/* -- INITIALISE OPEN GL ----------------------------------------------------------*/
void SkullbonezWindow::InitialiseOpenGL(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	if(!SetupPixelFormat())
		PostQuitMessage(0);

	// Bootstrap: create a temporary legacy context so we can load
	// wglCreateContextAttribsARB, then replace it with a 3.3 context.
	HGLRC tempContext = wglCreateContext(cWindow->sDevice);
	wglMakeCurrent(cWindow->sDevice, tempContext);

	// Resolve the modern context creation function
	typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
	auto wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
		wglGetProcAddress("wglCreateContextAttribsARB"));

	if (wglCreateContextAttribsARB)
	{
		// Request OpenGL 3.3 Compatibility Profile (allows FFP during migration)
		const int attribs[] = {
			0x2091, 3,  // WGL_CONTEXT_MAJOR_VERSION_ARB
			0x2092, 3,  // WGL_CONTEXT_MINOR_VERSION_ARB
			0x9126, 0x2, // WGL_CONTEXT_PROFILE_MASK_ARB = COMPATIBILITY (0x2)
			0           // terminator
		};

		HGLRC modernContext = wglCreateContextAttribsARB(cWindow->sDevice, 0, attribs);
		if (modernContext)
		{
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(tempContext);
			wglMakeCurrent(cWindow->sDevice, modernContext);
			cWindow->sRenderContext = modernContext;
		}
		else
		{
			// Fall back to legacy context if 3.3 is not available
			cWindow->sRenderContext = tempContext;
		}
	}
	else
	{
		cWindow->sRenderContext = tempContext;
	}

	// Load all GL function pointers via GLAD
	int gladVersion = gladLoadGL(GladLoadFunc);
	if (!gladVersion)
	{
		this->MsgBox("gladLoadGL returned 0 - GL function loading failed", "GLAD Error", MB_OK);
		PostQuitMessage(0);
		return;
	}

	// Set window dimensions
	RECT windowDimensions;
	GetClientRect(cWindow->sWindow, &windowDimensions);
	cWindow->SetWindowDimensions(windowDimensions);

	HandleScreenResize();
}



/* -- CREATE APP WINDOW -----------------------------------------------------------*/
void SkullbonezWindow::CreateAppWindow(HINSTANCE hInstance, bool isFullScreenMode)
{
    HWND			hWnd		= NULL;					// Handle to our window
	WNDCLASS		wndclass	= {0};					// Window class struct
	DWORD			dwStyle		= NULL;					// Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW;			// Vert and Horiz redraw
	wndclass.lpfnWndProc = WndProc;						// Assign callback function
    wndclass.hInstance = hInstance;						// Assign hInstance
    wndclass.hIcon = LoadIcon (NULL, IDI_WINLOGO);		// Default icon
    wndclass.hCursor = LoadCursor (NULL, IDC_ARROW);	// Arrow cursor
    wndclass.hbrBackground = 
		(HBRUSH)GetStockObject(WHITE_BRUSH);			// White client background
	wndclass.lpszClassName = WINDOW_NAME;				// Assign class name

	RegisterClass(&wndclass);							// Register class with OS

	// Set full screen mode flag member
	this->fIsFullScreenMode = isFullScreenMode;

	if(this->fIsFullScreenMode)
	{
		// Set window properties for full screen mode
		dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

		// Changes to full screen mode
		this->ChangeToFullScreen(SCREEN_X, SCREEN_Y);

		// Hide the mouse cursor
		ShowCursor(false);
	}
	else
	{
		// Set window properties for non full screen mode
		dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

    hWnd = CreateWindow(WINDOW_NAME,				// Window class name
						TITLE_TEXT,					// Window title text
						dwStyle,					// Set defined style
						0,							// Window xPos
						0,							// Window yPos
						SCREEN_X,					// Screen width
						SCREEN_Y,					// Screen height
						NULL,						// Parent window handle
						NULL,						// Window menu handle
						hInstance,					// Application instance
						NULL);						// Data to pass to WndProc

	if(!hWnd) throw std::runtime_error("Window creation failed");		// Throw exception on failure
	ShowWindow(hWnd, SW_SHOWNORMAL);				// Show window
    UpdateWindow(hWnd);								// Draw window
	SetFocus(hWnd);									// Set keyboard focus
													// to our window

	this->sWindow = hWnd;
}



/* -- SET TITLE TEXT --------------------------------------------------------------*/
void SkullbonezWindow::SetTitleText(const char* cText)
{
	// set the window title text
	SetWindowText(this->sWindow, cText);
}



/* -- MSG BOX ---------------------------------------------------------------------*/
int SkullbonezWindow::MsgBox(const char* cMsgBoxText, 
							 const char* cMsgBoxTitle, 
							 const UINT iMsgBoxType)
{
	return(MessageBox(this->sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType));
}
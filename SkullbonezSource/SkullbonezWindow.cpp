/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



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



#ifdef _WIN32
/* -- SET WINDOW DIMENSIONS (RECT overload) ---------------------------------------*/
void SkullbonezWindow::SetWindowDimensions(const RECT dimensions)
{
	this->sWindowDimensions.x = dimensions.right;
	this->sWindowDimensions.y = dimensions.bottom;
}
#endif



/* -- HANDLE SCREEN RESIZE --------------------------------------------------------*/
void SkullbonezWindow::HandleScreenResize(void)
{
	// Guard: skip GL calls if GLAD hasn't loaded function pointers yet.
	if (!glViewport) return;

	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	int width  = cWindow->sWindowDimensions.x;
	int height = cWindow->sWindowDimensions.y;

	if (!height) height = 1;
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	gluPerspective(45.0f,
				   (float)width/(float)height,
				   FRUSTUM_CLIP_SHORT_QTY,
				   FRUSTUM_CLIP_FAR_QTY);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}



/* -- SWITCH TO ORTHO MODE --------------------------------------------------------*/
void SkullbonezWindow::SwitchToOrthoMode(void)
{
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,
			this->sWindowDimensions.x,
			0,
			this->sWindowDimensions.y,
			-1,
			1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}



/* -- SWITCH OUT OF ORTHO MODE ----------------------------------------------------*/
void SkullbonezWindow::SwitchOutOfOrthoMode(void)
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}



/* ================================================================================
   PLATFORM-SPECIFIC IMPLEMENTATIONS
   ================================================================================ */

#ifdef _WIN32
/* ---- WINDOWS IMPLEMENTATION --------------------------------------------------- */

/* -- WndProc FUNCTION ------------------------------------------------------------*/
LRESULT CALLBACK WndProc (HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	PAINTSTRUCT ps = {0};

	try
	{
		switch (iMsg)
		{
			case WM_CREATE:
				break;

			case WM_SIZE:
				cWindow->SetWindowDimensions(LOWORD(lParam), HIWORD(lParam));
				cWindow->HandleScreenResize();
				break;

			case WM_PAINT:
				BeginPaint(hWnd, &ps);
				EndPaint(hWnd, &ps);
				break;

			case WM_KEYDOWN:
				if(wParam == VK_ESCAPE) PostQuitMessage(0);
				break;

			case WM_DESTROY:
				PostQuitMessage(0);
				break;
		}
	}
	catch (const std::exception& e)
	{
		cWindow->MsgBox(e.what(), "FATAL ERROR", MB_OK);
	}

	return DefWindowProc (hWnd, iMsg, wParam, lParam);
}



/* -- GLAD LOADER CALLBACK (Windows/WGL) ------------------------------------------*/
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



/* -- SETUP PIXEL FORMAT ----------------------------------------------------------*/
bool SkullbonezWindow::SetupPixelFormat(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	PIXELFORMATDESCRIPTOR pfd = {0};
	int pixelFormat = NULL;

	pfd.nSize		= sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion	= 1;
	pfd.dwFlags		= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.dwLayerMask = PFD_MAIN_PLANE;
	pfd.iPixelType  = PFD_TYPE_RGBA;
	pfd.cColorBits  = BITS_PER_PIXEL;
	pfd.cDepthBits  = BITS_PER_PIXEL;
	pfd.cAccumBits  = 0;
	pfd.cStencilBits= 0;

	pixelFormat = ChoosePixelFormat(cWindow->sDevice, &pfd);
	if(!pixelFormat)
	{
		this->MsgBox("ChoosePixelFormat failed", "Error", MB_OK);
		return false;
	}
	if(!SetPixelFormat(cWindow->sDevice, pixelFormat, &pfd))
	{
		this->MsgBox("SetPixelFormat failed", "Error", MB_OK);
		return false;
	}
	return true;
}



/* -- INITIALISE OPEN GL (Windows) ------------------------------------------------*/
void SkullbonezWindow::InitialiseOpenGL(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();

	if(!SetupPixelFormat())
		PostQuitMessage(0);

	HGLRC tempContext = wglCreateContext(cWindow->sDevice);
	wglMakeCurrent(cWindow->sDevice, tempContext);

	typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
	auto wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
		wglGetProcAddress("wglCreateContextAttribsARB"));

	if (wglCreateContextAttribsARB)
	{
		const int attribs[] = {
			0x2091, 3,   // WGL_CONTEXT_MAJOR_VERSION_ARB
			0x2092, 3,   // WGL_CONTEXT_MINOR_VERSION_ARB
			0x9126, 0x2, // WGL_CONTEXT_PROFILE_MASK_ARB = COMPATIBILITY
			0
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
			cWindow->sRenderContext = tempContext;
		}
	}
	else
	{
		cWindow->sRenderContext = tempContext;
	}

	int gladVersion = gladLoadGL(GladLoadFunc);
	if (!gladVersion)
	{
		this->MsgBox("gladLoadGL returned 0 - GL function loading failed", "GLAD Error", MB_OK);
		PostQuitMessage(0);
		return;
	}

	RECT windowDimensions;
	GetClientRect(cWindow->sWindow, &windowDimensions);
	cWindow->SetWindowDimensions(windowDimensions);

	HandleScreenResize();
}



/* -- CHANGE TO FULLSCREEN --------------------------------------------------------*/
void SkullbonezWindow::ChangeToFullScreen(int xResolution, int yResolution)
{
	DEVMODE dmSettings = {0};

	if(!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dmSettings))
	{
		this->MsgBox("Could Not Enumerate Display Settings", "Error", MB_OK);
		PostQuitMessage(0);
	}

	dmSettings.dmPelsWidth  = xResolution;
	dmSettings.dmPelsHeight = yResolution;
	dmSettings.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

	int result = ChangeDisplaySettings(&dmSettings, CDS_FULLSCREEN);
	if(result != DISP_CHANGE_SUCCESSFUL)
	{
		this->MsgBox("Display Mode Not Compatible", "Error", MB_OK);
		PostQuitMessage(0);
	}
}



/* -- CREATE APP WINDOW (Windows) -------------------------------------------------*/
void SkullbonezWindow::CreateAppWindow(HINSTANCE hInstance, bool isFullScreenMode)
{
	HWND     hWnd     = NULL;
	WNDCLASS wndclass = {0};
	DWORD    dwStyle  = NULL;

	wndclass.style         = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc   = WndProc;
	wndclass.hInstance     = hInstance;
	wndclass.hIcon         = LoadIcon(NULL, IDI_WINLOGO);
	wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = WINDOW_NAME;

	RegisterClass(&wndclass);

	this->fIsFullScreenMode = isFullScreenMode;

	if(this->fIsFullScreenMode)
	{
		dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
		this->ChangeToFullScreen(SCREEN_X, SCREEN_Y);
		ShowCursor(false);
	}
	else
	{
		dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	hWnd = CreateWindow(WINDOW_NAME,
						TITLE_TEXT,
						dwStyle,
						0, 0,
						SCREEN_X, SCREEN_Y,
						NULL, NULL, hInstance, NULL);

	if(!hWnd) throw std::runtime_error("Window creation failed");
	ShowWindow(hWnd, SW_SHOWNORMAL);
	UpdateWindow(hWnd);
	SetFocus(hWnd);

	this->sWindow = hWnd;
}



/* -- SET TITLE TEXT (Windows) ----------------------------------------------------*/
void SkullbonezWindow::SetTitleText(const char* cText)
{
	SetWindowText(this->sWindow, cText);
}



/* -- MSG BOX (Windows) -----------------------------------------------------------*/
int SkullbonezWindow::MsgBox(const char* cMsgBoxText,
							  const char* cMsgBoxTitle,
							  const UINT iMsgBoxType)
{
	return(MessageBox(this->sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType));
}


#else
/* ---- LINUX / GLFW IMPLEMENTATION ---------------------------------------------- */

/* -- GLFW resize callback --------------------------------------------------------*/
static void GlfwFramebufferSizeCallback(GLFWwindow* /*window*/, int width, int height)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	cWindow->SetWindowDimensions(width, height);
	cWindow->HandleScreenResize();
}

/* -- GLFW key callback (escape + toggle tracking) --------------------------------*/
// Toggle-state table: flips on each GLFW_PRESS event
static bool g_keyToggle[GLFW_KEY_LAST + 1] = {};

static void GlfwKeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_ESCAPE)
			glfwSetWindowShouldClose(window, GLFW_TRUE);

		if (key >= 0 && key <= GLFW_KEY_LAST)
			g_keyToggle[key] = !g_keyToggle[key];
	}
}

bool* SkullbonezWindow_GetKeyToggleTable() { return g_keyToggle; }



/* -- GLAD LOADER CALLBACK (Linux/GLFW) -------------------------------------------*/
static GLADapiproc GladLoadFunc(const char* name)
{
	return reinterpret_cast<GLADapiproc>(glfwGetProcAddress(name));
}



/* -- INITIALISE OPEN GL (Linux) --------------------------------------------------*/
void SkullbonezWindow::InitialiseOpenGL(void)
{
	// GLFW context is already current (created in CreateAppWindow / re-made in Init loop).
	// Just load GLAD function pointers.
	int gladVersion = gladLoadGL(GladLoadFunc);
	if (!gladVersion)
		throw std::runtime_error("gladLoadGL returned 0 - GL function loading failed");

	int width = 0, height = 0;
	glfwGetFramebufferSize(this->sWindow, &width, &height);
	this->SetWindowDimensions(width, height);
	HandleScreenResize();
}



/* -- CREATE APP WINDOW (Linux) ---------------------------------------------------*/
void SkullbonezWindow::CreateAppWindow(bool isFullScreenMode)
{
	if (!glfwInit())
		throw std::runtime_error("glfwInit failed");

	// Request OpenGL 3.3 Compatibility Profile (same as Windows path)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

	GLFWmonitor* monitor = isFullScreenMode ? glfwGetPrimaryMonitor() : nullptr;
	GLFWwindow* win = glfwCreateWindow(SCREEN_X, SCREEN_Y, TITLE_TEXT, monitor, nullptr);
	if (!win)
	{
		glfwTerminate();
		throw std::runtime_error("GLFW window creation failed");
	}

	glfwMakeContextCurrent(win);
	glfwSwapInterval(0);  // disable vsync for raw perf measurements

	glfwSetFramebufferSizeCallback(win, GlfwFramebufferSizeCallback);
	glfwSetKeyCallback(win, GlfwKeyCallback);

	this->sWindow          = win;
	this->sDevice          = nullptr;
	this->sRenderContext   = nullptr;
	this->fIsFullScreenMode = isFullScreenMode;
}



/* -- SET TITLE TEXT (Linux) ------------------------------------------------------*/
void SkullbonezWindow::SetTitleText(const char* cText)
{
	if (this->sWindow)
		glfwSetWindowTitle(this->sWindow, cText);
}



/* -- MSG BOX (Linux) ------------------------------------------------------------*/
int SkullbonezWindow::MsgBox(const char* cMsgBoxText,
							  const char* /*cMsgBoxTitle*/,
							  const UINT  /*iMsgBoxType*/)
{
	// No message-box API on Linux — print to stderr and return OK
	fprintf(stderr, "[SkullbonezCore] %s\n", cMsgBoxText);
	return 1; // IDOK
}

#endif // _WIN32

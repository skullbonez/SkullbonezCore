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
#include "SkullbonezText.h"
#include "SkullbonezWindow.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Text;



/* -- INITIALISE STATICS ----------------------------------------------------------*/
UINT Text2d::displayList = 0;



/* ================================================================================
   WINDOWS IMPLEMENTATION — GDI font → wglUseFontOutlines display list
   ================================================================================ */
#ifdef _WIN32

/* -- RENDER 2D TEXT --------------------------------------------------------------*/
void Text2d::Render2dText(float xPosition,
						   float yPosition,
						   float fSize,
						   const char* cRawText,
						   ...)
{
	if (!cRawText) return;

	Text2d::PrepareEnvironment(xPosition, yPosition, fSize);

	std::unique_ptr<char[]> symbolSafeText(new char[strlen(cRawText) + 100]);

	va_list arguments;
	va_start(arguments, cRawText);
	vsprintf_s(symbolSafeText.get(), strlen(cRawText) + 100, cRawText, arguments);
	va_end(arguments);

	glPushAttrib(GL_LIST_BIT);
	glListBase(Text2d::displayList - 32);
	glCallLists((GLsizei)strlen(symbolSafeText.get()),
				GL_UNSIGNED_BYTE,
				symbolSafeText.get());
	glPopAttrib();

	Text2d::RestoreEnvironment();
}



/* -- RESTORE ENVIRONMENT ---------------------------------------------------------*/
void Text2d::RestoreEnvironment(void)
{
	glEnable(GL_LIGHTING);
	glEnable(GL_TEXTURE_2D);
	glPopMatrix();
}



/* -- PREPARE ENVIRONMENT ---------------------------------------------------------*/
void Text2d::PrepareEnvironment(float xPosition,
								 float yPosition,
								 float fSize)
{
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(xPosition, yPosition, -FRUSTUM_CLIP_SHORT_QTY);
	glScalef(fSize, fSize, fSize);
}



/* -- DELETE FONT -----------------------------------------------------------------*/
void Text2d::DeleteFont(void)
{
	glDeleteLists(Text2d::displayList, 96);
}



/* -- BUILD FONT ------------------------------------------------------------------*/
void Text2d::BuildFont(HDC hDC, const char* cFontName)
{
	HFONT font;
	HFONT oldfont;

	Text2d::displayList = glGenLists(96);

	font = CreateFont(0, 0, 0, 0,
					  FW_NORMAL,
					  FALSE, FALSE, FALSE,
					  ANSI_CHARSET,
					  OUT_TT_PRECIS,
					  CLIP_DEFAULT_PRECIS,
					  ANTIALIASED_QUALITY,
					  FF_DONTCARE|DEFAULT_PITCH,
					  cFontName);

	if(!font) throw std::runtime_error("Font creation failed. (Text2d::BuildFont)");

	oldfont = (HFONT)SelectObject(hDC, font);

	GLYPHMETRICSFLOAT agmf[96];
	wglUseFontOutlines(hDC, 32, 96, Text2d::displayList,
					   0.0, 0.0, WGL_FONT_POLYGONS, agmf);

	SelectObject(hDC, oldfont);
	DeleteObject(font);
}


#else
/* ================================================================================
   LINUX STUB — text rendering via wglUseFontOutlines is Windows-only.
   BuildFont creates empty display lists; Render2dText is a no-op.
   Phase 7 (p7-text-render) will replace this with a proper shader quad path.
   ================================================================================ */

/* -- RENDER 2D TEXT (stub) -------------------------------------------------------*/
void Text2d::Render2dText(float /*xPosition*/,
						   float /*yPosition*/,
						   float /*fSize*/,
						   const char* /*cRawText*/,
						   ...)
{
	// No-op on Linux until Phase 7 text rendering is implemented.
}



/* -- RESTORE ENVIRONMENT (stub) --------------------------------------------------*/
void Text2d::RestoreEnvironment(void)
{
}



/* -- PREPARE ENVIRONMENT (stub) --------------------------------------------------*/
void Text2d::PrepareEnvironment(float /*xPosition*/,
								 float /*yPosition*/,
								 float /*fSize*/)
{
}



/* -- DELETE FONT (stub) ----------------------------------------------------------*/
void Text2d::DeleteFont(void)
{
	if (Text2d::displayList)
		glDeleteLists(Text2d::displayList, 96);
}



/* -- BUILD FONT (stub) -----------------------------------------------------------*/
void Text2d::BuildFont(HDC /*hDC*/, const char* /*cFontName*/)
{
	// Allocate 96 empty display lists so glCallLists doesn't fault.
	Text2d::displayList = glGenLists(96);
	// Leave them empty — Render2dText is a no-op on Linux.
}

#endif // _WIN32

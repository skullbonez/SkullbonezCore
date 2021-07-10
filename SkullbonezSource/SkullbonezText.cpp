/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



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
#include "SkullbonezText.h"
#include "SkullbonezWindow.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Text;



/* -- INITIALISE STATICS ----------------------------------------------------------*/
UINT Text2d::displayList = 0;



/* -- RENDER 2D TEXT --------------------------------------------------------------*/
void Text2d::Render2dText(float xPosition,
						  float yPosition,
						  float fSize,
						  const char* cRawText,
						  ...)
{
	// return if no text to to print
	if (!cRawText) return;

	// prepare environment
	Text2d::PrepareEnvironment(xPosition, yPosition, fSize);

	// prepare space for the symbol conversion, add 100 chars for
	// additional arguments and null termination
	char* symbolSafeText = new char[strlen(cRawText) + 100];

	// pointer to list of arguments
	va_list	arguments;

	// parses the string for variables
	va_start(arguments, cRawText);

	// converts symbols to actual numbers
	vsprintf(symbolSafeText, cRawText, arguments);

	// results are stored in text
	va_end(arguments);

	// pushes the display list bits
	glPushAttrib(GL_LIST_BIT);

	// sets the base character to 32
	glListBase(Text2d::displayList-32);

	// draws the display list text
	glCallLists((GLsizei)strlen(symbolSafeText), // cull null space
				GL_UNSIGNED_BYTE,				 // display list element type
				symbolSafeText);				 // string to rasterise

	// pops the display list bits
	glPopAttrib();

	// cleanup allocated memory
	if(symbolSafeText) delete [] symbolSafeText;

	// restore environment
	Text2d::RestoreEnvironment();
}



/* -- RESTORE ENVIRONMENT ---------------------------------------------------------*/
void Text2d::RestoreEnvironment(void)
{
	// restore lighting
	glEnable(GL_LIGHTING);

	// restore texture mapping
	glEnable(GL_TEXTURE_2D);

	// restrore matrix state
	glPopMatrix();
}



/* -- PREPARE ENVIRONMENT ---------------------------------------------------------*/
void Text2d::PrepareEnvironment(float xPosition,
								float yPosition,
								float fSize)
{
	// disable lighting
	glDisable(GL_LIGHTING);

	// disable texture mapping
	glDisable(GL_TEXTURE_2D);

	// save current matrix state
	glPushMatrix();

	// reset matrix
	glLoadIdentity();

	// move into screen (no closer than the clipping volume)
	glTranslatef(xPosition, yPosition, -FRUSTUM_CLIP_SHORT_QTY);

	// scale to requested size
	glScalef(fSize, fSize, fSize);
}



/* -- DELETE FONT -----------------------------------------------------------------*/
void Text2d::DeleteFont(void)
{
	// delete all 96 characters
	glDeleteLists(Text2d::displayList, 96);
}



/* -- BUILD FONT ------------------------------------------------------------------*/
void Text2d::BuildFont(const HDC hDC, const char* cFontName)
{
	HFONT	font;			// windows font id
	HFONT	oldfont;		// used for house keeping

	// get storage for 96 characters
	Text2d::displayList = glGenLists(96);

	font = CreateFont(0,							// font height		   (null)
					  0,							// font width		   (null)
					  0,							// angle of escapement (null)
					  0,							// orintation angle	   (null)
					  FW_NORMAL,					// font weight
					  FALSE,						// italic flag
					  FALSE,						// underline flag
					  FALSE,						// strikeout flag
					  ANSI_CHARSET,					// character set identifier
					  OUT_TT_PRECIS,				// output precision
					  CLIP_DEFAULT_PRECIS,			// clipping precision
					  ANTIALIASED_QUALITY,			// output quality
					  FF_DONTCARE|DEFAULT_PITCH,	// family and pitch
					  cFontName);					// font name

	// check for success
	if(!font) throw "Font creation failed. (Text2d::BuildFont)";

	// selects the font we want
	oldfont = (HFONT)SelectObject(hDC, font);

	// allocate structure for character metrics
	GLYPHMETRICSFLOAT agmf[96];

	// builds 96 characters starting at character 32
	wglUseFontOutlines(hDC,						// device
					   32,						// starting ascii char
					   96,						// num ascii chars
					   Text2d::displayList,	// display list id to use
					   0.0,						// deviation
					   0.0,						// extrusion
					   WGL_FONT_POLYGONS,		// filled polies
					   agmf);					// storage stuct

	// selects the font we want
	SelectObject(hDC, oldfont);

	// deletes the font
	DeleteObject(font);
}
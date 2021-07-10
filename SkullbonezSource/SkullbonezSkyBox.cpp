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
#include "SkullbonezSkyBox.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;



/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
SkyBox* SkyBox::pInstance = 0;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
SkyBox::SkyBox(int xMin, 
			   int xMax,
			   int yMin,
			   int yMax,
			   int zMin,
			   int zMax)
{
	// set boundaries
	this->boundaries.xMin = xMin;
	this->boundaries.xMax = xMax;
	this->boundaries.yMin = yMin;
	this->boundaries.yMax = yMax;
	this->boundaries.zMin = zMin;
	this->boundaries.zMax = zMax;

	// request singleton instance
	this->textures = TextureCollection::Instance(TOTAL_TEXTURE_COUNT);

	// load textures (with no linear mipmap interpolation to 
	// avoid lines on skybox edges)
	this->textures->CreateJpegTexture(SKY_LEFT_PATH,	"SkyLeft");
	this->textures->CreateJpegTexture(SKY_RIGHT_PATH,	"SkyRight");
	this->textures->CreateJpegTexture(SKY_FRONT_PATH,	"SkyFront");
	this->textures->CreateJpegTexture(SKY_BACK_PATH,	"SkyBack");
	this->textures->CreateJpegTexture(SKY_UP_PATH,		"SkyUp");
	this->textures->CreateJpegTexture(SKY_DOWN_PATH,	"SkyDown");
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkyBox::~SkyBox(void)
{
	this->textures  = 0;
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkyBox* SkyBox::Instance(int xMin, 
						 int xMax,
						 int yMin,
						 int yMax,
						 int zMin,
						 int zMax)
{
	// if no skybox exists, create one
	if(!SkyBox::pInstance)
		SkyBox::pInstance = new SkyBox(xMin, xMax, yMin, yMax, zMin, zMax);

	// return the instance pointer
	return SkyBox::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkyBox::Destroy(void)
{
	if(SkyBox::pInstance) delete SkyBox::pInstance;
	SkyBox::pInstance = 0;
}



/* -- RENDER ----------------------------------------------------------------------*/
void SkyBox::Render(void)
{
	// turn off lighting
	glDisable(GL_LIGHTING);

	// draw roof
	this->textures->SelectTexture("SkyUp");

	glBegin(GL_QUADS);
		
		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW, 
				   this->boundaries.yMax, 
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);
		
		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW, 
				   this->boundaries.yMax, 
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW, 
				   this->boundaries.yMax, 
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW, 
				   this->boundaries.yMax, 
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

	glEnd();


	// draw bottom
	this->textures->SelectTexture("SkyDown");

	glBegin(GL_QUADS);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW, 
				   this->boundaries.yMin, 
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW, 
				   this->boundaries.yMin, 
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);
		
		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW, 
				   this->boundaries.yMin, 
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW, 
				   this->boundaries.yMin, 
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

	glEnd();


	// draw right
	this->textures->SelectTexture("SkyRight");

	glBegin(GL_QUADS);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMin,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMin,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMin,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMin,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

	glEnd();


	// draw left
	this->textures->SelectTexture("SkyLeft");

	glBegin(GL_QUADS);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMax,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMax,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMax,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMax+SKY_BOX_OVERFLOW);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMax,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMin-SKY_BOX_OVERFLOW);

	glEnd();


	// draw front
	this->textures->SelectTexture("SkyFront");

	glBegin(GL_QUADS);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMax);

		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMax);

		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMax);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMax);

	glEnd();


	// draw back
	this->textures->SelectTexture("SkyBack");

	glBegin(GL_QUADS);

		glTexCoord2i(1, 1);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMin);

		glTexCoord2i(0, 1);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW,
				   this->boundaries.yMin-SKY_BOX_OVERFLOW,
				   this->boundaries.zMin);

		glTexCoord2i(0, 0);
		glVertex3i(this->boundaries.xMax+SKY_BOX_OVERFLOW,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMin);

		glTexCoord2i(1, 0);
		glVertex3i(this->boundaries.xMin-SKY_BOX_OVERFLOW,
				   this->boundaries.yMax+SKY_BOX_OVERFLOW,
				   this->boundaries.zMin);

	glEnd();


	// enable lighting again
	glEnable(GL_LIGHTING);
}
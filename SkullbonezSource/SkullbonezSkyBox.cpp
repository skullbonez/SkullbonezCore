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
	this->textures = TextureCollection::Instance();

	// load textures (with no linear mipmap interpolation to 
	// avoid lines on skybox edges)
	this->textures->CreateJpegTexture(SKY_LEFT_PATH,	TEXTURE_SKY_LEFT);
	this->textures->CreateJpegTexture(SKY_RIGHT_PATH,	TEXTURE_SKY_RIGHT);
	this->textures->CreateJpegTexture(SKY_FRONT_PATH,	TEXTURE_SKY_FRONT);
	this->textures->CreateJpegTexture(SKY_BACK_PATH,	TEXTURE_SKY_BACK);
	this->textures->CreateJpegTexture(SKY_UP_PATH,		TEXTURE_SKY_UP);
	this->textures->CreateJpegTexture(SKY_DOWN_PATH,	TEXTURE_SKY_DOWN);
}



/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkyBox::~SkyBox(void)
{
	this->textures  = 0;
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkyBox* SkyBox::Instance(int xMin, int xMax, int yMin, int yMax, int zMin, int zMax)
{
	if(!SkyBox::pInstance)
	{
		static SkyBox instance(xMin, xMax, yMin, yMax, zMin, zMax);
		SkyBox::pInstance = &instance;
	}
	return SkyBox::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkyBox::Destroy(void)
{
	SkyBox::pInstance = 0;
}



/* -- RENDER ----------------------------------------------------------------------*/
void SkyBox::Render(void)
{
	// turn off lighting
	glDisable(GL_LIGHTING);

	// draw roof
	this->textures->SelectTexture(TEXTURE_SKY_UP);

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
	this->textures->SelectTexture(TEXTURE_SKY_DOWN);

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
	this->textures->SelectTexture(TEXTURE_SKY_RIGHT);

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
	this->textures->SelectTexture(TEXTURE_SKY_LEFT);

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
	this->textures->SelectTexture(TEXTURE_SKY_FRONT);

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
	this->textures->SelectTexture(TEXTURE_SKY_BACK);

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

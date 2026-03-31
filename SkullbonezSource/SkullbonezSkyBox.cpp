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
#include <vector>



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
	this->boundaries.xMin = xMin;
	this->boundaries.xMax = xMax;
	this->boundaries.yMin = yMin;
	this->boundaries.yMax = yMax;
	this->boundaries.zMin = zMin;
	this->boundaries.zMax = zMax;
	this->textures = 0;
}



/* -- LOAD TEXTURES ---------------------------------------------------------------*/
void SkyBox::LoadTextures(void)
{
	this->textures = TextureCollection::Instance();
	this->textures->CreateJpegTexture(SKY_LEFT_PATH,	TEXTURE_SKY_LEFT);
	this->textures->CreateJpegTexture(SKY_RIGHT_PATH,	TEXTURE_SKY_RIGHT);
	this->textures->CreateJpegTexture(SKY_FRONT_PATH,	TEXTURE_SKY_FRONT);
	this->textures->CreateJpegTexture(SKY_BACK_PATH,	TEXTURE_SKY_BACK);
	this->textures->CreateJpegTexture(SKY_UP_PATH,		TEXTURE_SKY_UP);
	this->textures->CreateJpegTexture(SKY_DOWN_PATH,	TEXTURE_SKY_DOWN);
}



/* -- BUILD MESHES ----------------------------------------------------------------*/
void SkyBox::BuildMeshes(void)
{
	// Shorthand for boundary values with overflow
	float xn = (float)(this->boundaries.xMin - SKY_BOX_OVERFLOW);
	float xp = (float)(this->boundaries.xMax + SKY_BOX_OVERFLOW);
	float yn = (float)(this->boundaries.yMin - SKY_BOX_OVERFLOW);
	float yp = (float)(this->boundaries.yMax + SKY_BOX_OVERFLOW);
	float zn = (float)(this->boundaries.zMin - SKY_BOX_OVERFLOW);
	float zp = (float)(this->boundaries.zMax + SKY_BOX_OVERFLOW);
	float yMinF = (float)this->boundaries.yMin;
	float yMaxF = (float)this->boundaries.yMax;
	float xMinF = (float)this->boundaries.xMin;
	float xMaxF = (float)this->boundaries.xMax;
	float zMinF = (float)this->boundaries.zMin;
	float zMaxF = (float)this->boundaries.zMax;

	// Each face: 2 triangles = 6 vertices, 5 floats each (pos3 + tex2)
	// Face order: up, down, right(west), left(east), front, back

	// UP face (y = yMax)
	float up[] = {
		xn, yMaxF, zp,   0,1,    xn, yMaxF, zn,   0,0,    xp, yMaxF, zn,   1,0,
		xn, yMaxF, zp,   0,1,    xp, yMaxF, zn,   1,0,    xp, yMaxF, zp,   1,1,
	};

	// DOWN face (y = yMin)
	float down[] = {
		xp, yMinF, zp,   1,0,    xp, yMinF, zn,   1,1,    xn, yMinF, zn,   0,1,
		xp, yMinF, zp,   1,0,    xn, yMinF, zn,   0,1,    xn, yMinF, zp,   0,0,
	};

	// RIGHT/WEST face (x = xMin)
	float right[] = {
		xMinF, yn, zp,   1,1,    xMinF, yn, zn,   0,1,    xMinF, yp, zn,   0,0,
		xMinF, yn, zp,   1,1,    xMinF, yp, zn,   0,0,    xMinF, yp, zp,   1,0,
	};

	// LEFT/EAST face (x = xMax)
	float left[] = {
		xMaxF, yn, zn,   1,1,    xMaxF, yn, zp,   0,1,    xMaxF, yp, zp,   0,0,
		xMaxF, yn, zn,   1,1,    xMaxF, yp, zp,   0,0,    xMaxF, yp, zn,   1,0,
	};

	// FRONT face (z = zMax)
	float front[] = {
		xp, yn, zMaxF,   1,1,    xn, yn, zMaxF,   0,1,    xn, yp, zMaxF,   0,0,
		xp, yn, zMaxF,   1,1,    xn, yp, zMaxF,   0,0,    xp, yp, zMaxF,   1,0,
	};

	// BACK face (z = zMin)
	float back[] = {
		xn, yn, zMinF,   1,1,    xp, yn, zMinF,   0,1,    xp, yp, zMinF,   0,0,
		xn, yn, zMinF,   1,1,    xp, yp, zMinF,   0,0,    xn, yp, zMinF,   1,0,
	};

	float* faceData[] = { up, down, right, left, front, back };
	this->faceTextures = { TEXTURE_SKY_UP, TEXTURE_SKY_DOWN, TEXTURE_SKY_RIGHT,
						   TEXTURE_SKY_LEFT, TEXTURE_SKY_FRONT, TEXTURE_SKY_BACK };

	for (int i = 0; i < 6; ++i)
		this->faceMeshes[i] = std::make_unique<Mesh>(faceData[i], 6, false, true);

	// Load shader
	this->shader = std::make_unique<Shader>(
		"SkullbonezData/shaders/unlit_textured.vert",
		"SkullbonezData/shaders/unlit_textured.frag");
}



/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkyBox* SkyBox::Instance(int xMin, int xMax, int yMin, int yMax, int zMin, int zMax)
{
	if(!SkyBox::pInstance)
	{
		static SkyBox instance(xMin, xMax, yMin, yMax, zMin, zMax);
		instance.LoadTextures();
		instance.BuildMeshes();
		SkyBox::pInstance = &instance;
	}
	return SkyBox::pInstance;
}



/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkyBox::Destroy(void)
{
	SkyBox::pInstance = 0;
}



/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void SkyBox::ResetGLResources(void)
{
	for (int i = 0; i < 6; ++i)
		this->faceMeshes[i].reset();
	this->shader.reset();
	this->BuildMeshes();
}



/* -- RENDER ----------------------------------------------------------------------*/
void SkyBox::Render(const Matrix4& view, const Matrix4& proj)
{
	// Identity model matrix (transform baked into view via FFP matrix stack)
	Matrix4 identity;

	this->shader->Use();
	this->shader->SetMat4("uModel", identity);
	this->shader->SetMat4("uView", view);
	this->shader->SetMat4("uProjection", proj);
	this->shader->SetVec4("uColorTint", 1.0f, 1.0f, 1.0f, 1.0f);

	for (int i = 0; i < 6; ++i)
	{
		this->textures->SelectTexture(this->faceTextures[i]);
		this->faceMeshes[i]->Draw();
	}

	glUseProgram(0);
}

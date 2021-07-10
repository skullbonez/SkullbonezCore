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
#include "SkullbonezHelper.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;



/* -- DRAW SPHERE -----------------------------------------------------------------*/
void SkullbonezHelper::DrawSphere(float radius, const bool isTransparent)
{
	if(isTransparent)
	{
		// turn on blending
		glEnable(GL_BLEND);
	}

	// save world matrix
	glPushMatrix();

	// rotate to get the textures right
	glRotatef(90.0f, 1.0f, 0.0f, 0.0f);

	// quadric required to build sphere
	GLUquadricObj *quadric = gluNewQuadric();

	// generate smooth normals for the sphere
	gluQuadricNormals(quadric, GL_SMOOTH);

	// generate texture coords for the sphere
	gluQuadricTexture(quadric, true);

	// draw sphere
	gluSphere(quadric,			// quadric
			  radius,			// radius
			  25,				// segments horiz
			  25);				// segments vert

	// delete the quadric
	gluDeleteQuadric(quadric);

	// restore world matrix
	glPopMatrix();

	if(isTransparent)
	{
		// turn off blending
		glDisable(GL_BLEND);
	}
}




/* -- STATE SETUP -----------------------------------------------------------------*/
void SkullbonezHelper::StateSetup(void)
{
/*
	// specular light setting
	float specularLight[] = {1.0f,	// red
							 1.0f,	// green
							 1.0f,	// blue
							 0.1f};	// alpha

	// set specular properties for light 0 and 1
	glLightfv(GL_LIGHT0, GL_SPECULAR, specularLight);

	// enable colour tracking (glColor specifies colour of material)
	glEnable(GL_COLOR_MATERIAL);

	// specify additional colour tracking params
	glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

	// specify that all materials should have specular reflectivity
	glMaterialfv(GL_FRONT, GL_SPECULAR, specularLight);

	// set the specular exponent (the size of specular highlights)
	glMateriali(GL_FRONT, GL_SHININESS, 128);

	// enable specularity on textures (opengl 1.2 only)
	glLightModel(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
*/

	glClearColor(0.0f,	// red
				 0.0f,	// green
				 0.0f,	// blue
				 0.0f);	// alpha

	glClearDepth(1.0f);			// clear all every frame

	glEnable(GL_DEPTH_TEST);	// enable depth testing
	glDepthFunc(GL_LEQUAL);		// less than or equal to depth testing type
	glEnable(GL_CULL_FACE);		// enable back face culling
	glFrontFace(GL_CCW);		// specify cull winding order

	// enable smooth shading (interpolation between vertex normals)
	glShadeModel(GL_SMOOTH);

	// set up alpha blending
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	// best perspective calculations
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	// enable lighting
	glEnable(GL_LIGHTING);

	// ambient light setting
	float ambientLight[] = {1.0f,	// red
							0.5f,	// green
							0.5f,	// blue
							1.0f};	// alpha

	// diffuse light setting
	float diffuseLight[] = {1.0f,	// red
							0.5f,	// green
							0.5f,	// blue
							1.0f};	// alpha

	// enable light 0
	glEnable(GL_LIGHT0);

	// set ambient properties for light 0
	glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);

	// set diffuse properties for light 0
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);

	// normalise normals when calls to glScale are made
	glEnable(GL_NORMALIZE);

	// enable texture mapping
	glEnable(GL_TEXTURE_2D);

	// ensure textures are repeated and not clamped
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}
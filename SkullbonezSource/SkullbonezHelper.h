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
#ifndef SKULLBONEZ_HELPER_H
#define	SKULLBONEZ_HELPER_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezShader.h"
#include "SkullbonezMesh.h"
#include "SkullbonezMatrix4.h"
#include <memory>



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;



namespace SkullbonezCore
{
	namespace Basics
	{
		/* -- Skullbonez Helper ------------------------------------------------------------------------------------------------------------------------------------------

			Static helper to assist in OpenGL state setup.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class SkullbonezHelper
		{

		private:

			static std::unique_ptr<Mesh>	sphereMesh;				// Shared unit sphere VBO
			static std::unique_ptr<Shader>	sphereShader;			// Shared lit_textured shader

			static void		BuildSphereMesh		(int slices, int stacks);	// Generate UV sphere mesh


		public:

			static void		StateSetup			(void);								// Assists in setting up initial open gl state
			static void		DrawSphere			(float radius,
												 const Matrix4& proj,
												 const float lightPos[4],
												 bool  isTransparent = false);		// Draws a sphere at current GL matrix position
			static void		ResetGLResources	(void);								// Call after GL context recreated to invalidate cached GL objects
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
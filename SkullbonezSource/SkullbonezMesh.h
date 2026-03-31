/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
																		 ?\'"VUUUV"'/?
																		 \ `"""""""` /
																		  `-._____.-'

																	 www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_MESH_H
#define SKULLBONEZ_MESH_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"



namespace SkullbonezCore
{
	namespace Rendering
	{
		/* -- Mesh ------------------------------------------------------------------------------------------------------------------------------------------------------

			VAO/VBO wrapper for interleaved vertex data. Supports flexible vertex formats:
			  - Position only (3 floats)
			  - Position + Normal (6 floats)
			  - Position + TexCoord (5 floats)
			  - Position + Normal + TexCoord (8 floats)

			Vertex attribute layout:
			  location 0 = aPosition (vec3)
			  location 1 = aNormal   (vec3)  [if hasNormals]
			  location 2 = aTexCoord (vec2)  [if hasTexCoords]
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class Mesh
		{

		private:

			GLuint					vao;									// Vertex Array Object
			GLuint					vbo;									// Vertex Buffer Object
			int						vertexCount;							// Number of vertices
			GLenum					drawMode;								// GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.


		public:

									Mesh				(const float* data,
														 int vertexCount,
														 bool hasNormals,
														 bool hasTexCoords,
														 GLenum drawMode = GL_TRIANGLES);	// Upload interleaved vertex data
									~Mesh				(void);								// Destructor: delete VAO/VBO

			void					Draw				(void) const;						// Bind VAO and draw
			int						GetVertexCount		(void) const;						// Get vertex count
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

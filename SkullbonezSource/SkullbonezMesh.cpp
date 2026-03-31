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
									 ?\'"VUUUV"'/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezMesh.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Rendering;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Mesh::Mesh(const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, GLenum drawMode)
{
	this->vertexCount = vertexCount;
	this->drawMode    = drawMode;

	// Calculate stride: position(3) + optional normal(3) + optional texcoord(2)
	int floatsPerVertex = 3;
	if (hasNormals)   floatsPerVertex += 3;
	if (hasTexCoords) floatsPerVertex += 2;
	int stride = floatsPerVertex * static_cast<int>(sizeof(float));

	// Create VAO
	glGenVertexArrays(1, &this->vao);
	glBindVertexArray(this->vao);

	// Create VBO and upload data
	glGenBuffers(1, &this->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, this->vbo);
	glBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(vertexCount) * stride,
		data, GL_STATIC_DRAW);

	// Configure vertex attributes
	int offset = 0;

	// location 0 = aPosition (vec3)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
		reinterpret_cast<void*>(static_cast<intptr_t>(offset)));
	offset += 3 * static_cast<int>(sizeof(float));

	// location 1 = aNormal (vec3)
	if (hasNormals)
	{
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<void*>(static_cast<intptr_t>(offset)));
		offset += 3 * static_cast<int>(sizeof(float));
	}

	// location 2 = aTexCoord (vec2)
	if (hasTexCoords)
	{
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<void*>(static_cast<intptr_t>(offset)));
	}

	// Unbind
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}



/* -- DESTRUCTOR ------------------------------------------------------------------*/
Mesh::~Mesh(void)
{
	if (this->vbo) glDeleteBuffers(1, &this->vbo);
	if (this->vao) glDeleteVertexArrays(1, &this->vao);
}



/* -- DRAW ------------------------------------------------------------------------*/
void Mesh::Draw(void) const
{
	glBindVertexArray(this->vao);
	glDrawArrays(this->drawMode, 0, this->vertexCount);
	glBindVertexArray(0);
}



/* -- GET VERTEX COUNT ------------------------------------------------------------*/
int Mesh::GetVertexCount(void) const
{
	return this->vertexCount;
}

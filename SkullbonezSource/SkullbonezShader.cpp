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
#include "SkullbonezShader.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Rendering;



/* -- LOAD SHADER SOURCE ----------------------------------------------------------*/
char* Shader::LoadShaderSource(const char* path)
{
	FILE* file = nullptr;
	errno_t err = fopen_s(&file, path, "rb");
	if (err != 0 || !file)
	{
		char msg[512];
		sprintf_s(msg, sizeof(msg), "Failed to open shader file: %s  (Shader::LoadShaderSource)", path);
		throw std::runtime_error(msg);
	}

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* source = new char[length + 1];
	fread(source, 1, static_cast<size_t>(length), file);
	source[length] = '\0';
	fclose(file);

	return source;
}



/* -- COMPILE SHADER --------------------------------------------------------------*/
GLuint Shader::CompileShader(const char* path, GLenum type)
{
	char* source = LoadShaderSource(path);

	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	delete[] source;

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char infoLog[1024];
		glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
		glDeleteShader(shader);

		char msg[1536];
		sprintf_s(msg, sizeof(msg), "Shader compilation failed (%s):\n%s  (Shader::CompileShader)", path, infoLog);
		throw std::runtime_error(msg);
	}

	return shader;
}



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Shader::Shader(const char* vertPath, const char* fragPath)
{
	GLuint vertShader = CompileShader(vertPath, GL_VERTEX_SHADER);
	GLuint fragShader = CompileShader(fragPath, GL_FRAGMENT_SHADER);

	this->programID = glCreateProgram();
	glAttachShader(this->programID, vertShader);
	glAttachShader(this->programID, fragShader);
	glLinkProgram(this->programID);

	GLint success = 0;
	glGetProgramiv(this->programID, GL_LINK_STATUS, &success);
	if (!success)
	{
		char infoLog[1024];
		glGetProgramInfoLog(this->programID, sizeof(infoLog), NULL, infoLog);
		glDeleteShader(vertShader);
		glDeleteShader(fragShader);
		glDeleteProgram(this->programID);

		char msg[1536];
		sprintf_s(msg, sizeof(msg), "Shader program link failed (%s + %s):\n%s  (Shader::Shader)", vertPath, fragPath, infoLog);
		throw std::runtime_error(msg);
	}

	// Shaders are linked into the program — delete the intermediate objects
	glDeleteShader(vertShader);
	glDeleteShader(fragShader);
}



/* -- DESTRUCTOR ------------------------------------------------------------------*/
Shader::~Shader(void)
{
	if (this->programID)
		glDeleteProgram(this->programID);
}



/* -- USE -------------------------------------------------------------------------*/
void Shader::Use(void) const
{
	glUseProgram(this->programID);
}



/* -- GET PROGRAM ID --------------------------------------------------------------*/
GLuint Shader::GetProgramID(void) const
{
	return this->programID;
}



/* -- SET INT ---------------------------------------------------------------------*/
void Shader::SetInt(const char* name, int value) const
{
	glUniform1i(glGetUniformLocation(this->programID, name), value);
}



/* -- SET FLOAT -------------------------------------------------------------------*/
void Shader::SetFloat(const char* name, float value) const
{
	glUniform1f(glGetUniformLocation(this->programID, name), value);
}



/* -- SET VEC3 (Vector3) ----------------------------------------------------------*/
void Shader::SetVec3(const char* name, const Vector3& v) const
{
	glUniform3f(glGetUniformLocation(this->programID, name), v.x, v.y, v.z);
}



/* -- SET VEC3 (components) -------------------------------------------------------*/
void Shader::SetVec3(const char* name, float x, float y, float z) const
{
	glUniform3f(glGetUniformLocation(this->programID, name), x, y, z);
}



/* -- SET VEC4 --------------------------------------------------------------------*/
void Shader::SetVec4(const char* name, float x, float y, float z, float w) const
{
	glUniform4f(glGetUniformLocation(this->programID, name), x, y, z, w);
}



/* -- SET MAT4 --------------------------------------------------------------------*/
void Shader::SetMat4(const char* name, const Matrix4& mat) const
{
	glUniformMatrix4fv(glGetUniformLocation(this->programID, name), 1, GL_FALSE, mat.Data());
}

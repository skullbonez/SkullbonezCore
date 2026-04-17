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
char* Shader::LoadShaderSource( const char* path )
{
    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "rb" );
    if ( err != 0 || !file )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open m_shader file: %s  (Shader::LoadShaderSource)", path );
        throw std::runtime_error( msg );
    }

    fseek( file, 0, SEEK_END );
    long length = ftell( file );
    fseek( file, 0, SEEK_SET );

    char* source = new char[length + 1];
    fread( source, 1, static_cast<size_t>( length ), file );
    source[length] = '\0';
    fclose( file );

    return source;
}


/* -- COMPILE SHADER --------------------------------------------------------------*/
GLuint Shader::CompileShader( const char* path, GLenum type )
{
    char* source = LoadShaderSource( path );

    GLuint m_shader = glCreateShader( type );
    glShaderSource( m_shader, 1, &source, NULL );
    glCompileShader( m_shader );

    delete[] source;

    GLint success = 0;
    glGetShaderiv( m_shader, GL_COMPILE_STATUS, &success );
    if ( !success )
    {
        char infoLog[1024];
        glGetShaderInfoLog( m_shader, sizeof( infoLog ), NULL, infoLog );
        glDeleteShader( m_shader );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "Shader compilation failed (%s):\n%s  (Shader::CompileShader)", path, infoLog );
        throw std::runtime_error( msg );
    }

    return m_shader;
}


/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Shader::Shader( const char* vertPath, const char* fragPath )
{
    GLuint vertShader = CompileShader( vertPath, GL_VERTEX_SHADER );
    GLuint fragShader = CompileShader( fragPath, GL_FRAGMENT_SHADER );

    m_programID = glCreateProgram();
    glAttachShader( m_programID, vertShader );
    glAttachShader( m_programID, fragShader );
    glLinkProgram( m_programID );

    GLint success = 0;
    glGetProgramiv( m_programID, GL_LINK_STATUS, &success );
    if ( !success )
    {
        char infoLog[1024];
        glGetProgramInfoLog( m_programID, sizeof( infoLog ), NULL, infoLog );
        glDeleteShader( vertShader );
        glDeleteShader( fragShader );
        glDeleteProgram( m_programID );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "Shader program link failed (%s + %s):\n%s  (Shader::Shader)", vertPath, fragPath, infoLog );
        throw std::runtime_error( msg );
    }

    // Shaders are linked into the program — delete the intermediate objects
    glDeleteShader( vertShader );
    glDeleteShader( fragShader );
}


/* -- DESTRUCTOR ------------------------------------------------------------------*/
Shader::~Shader( void )
{
    if ( m_programID )
    {
        glDeleteProgram( m_programID );
    }
}


/* -- USE -------------------------------------------------------------------------*/
void Shader::Use( void ) const
{
    glUseProgram( m_programID );
}


/* -- GET PROGRAM ID --------------------------------------------------------------*/
GLuint Shader::GetProgramID( void ) const
{
    return m_programID;
}


/* -- SET INT ---------------------------------------------------------------------*/
void Shader::SetInt( const char* name, int value ) const
{
    glUniform1i( glGetUniformLocation( m_programID, name ), value );
}


/* -- SET FLOAT -------------------------------------------------------------------*/
void Shader::SetFloat( const char* name, float value ) const
{
    glUniform1f( glGetUniformLocation( m_programID, name ), value );
}


/* -- SET VEC3 (Vector3) ----------------------------------------------------------*/
void Shader::SetVec3( const char* name, const Vector3& v ) const
{
    glUniform3f( glGetUniformLocation( m_programID, name ), v.x, v.y, v.z );
}


/* -- SET VEC3 (components) -------------------------------------------------------*/
void Shader::SetVec3( const char* name, float x, float y, float z ) const
{
    glUniform3f( glGetUniformLocation( m_programID, name ), x, y, z );
}


/* -- SET VEC4 --------------------------------------------------------------------*/
void Shader::SetVec4( const char* name, float x, float y, float z, float w ) const
{
    glUniform4f( glGetUniformLocation( m_programID, name ), x, y, z, w );
}


/* -- SET MAT4 --------------------------------------------------------------------*/
void Shader::SetMat4( const char* name, const Matrix4& mat ) const
{
    glUniformMatrix4fv( glGetUniformLocation( m_programID, name ), 1, GL_FALSE, mat.Data() );
}

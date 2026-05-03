// --- Includes ---
#include "SkullbonezShaderGL.h"


// --- Usings ---
using namespace SkullbonezCore::Rendering;


char* ShaderGL::LoadShaderSource( const char* path )
{
    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "rb" );
    if ( err != 0 || !file )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open m_shader file: %s  (ShaderGL::LoadShaderSource)", path );
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


GLuint ShaderGL::CompileShader( const char* path, GLenum type )
{
    char* source = LoadShaderSource( path );

    GLuint m_shader = glCreateShader( type );
    glShaderSource( m_shader, 1, &source, nullptr );
    glCompileShader( m_shader );

    delete[] source;

    GLint success = 0;
    glGetShaderiv( m_shader, GL_COMPILE_STATUS, &success );
    if ( !success )
    {
        char infoLog[1024];
        glGetShaderInfoLog( m_shader, sizeof( infoLog ), nullptr, infoLog );
        glDeleteShader( m_shader );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "ShaderGL compilation failed (%s):\n%s  (ShaderGL::CompileShader)", path, infoLog );
        throw std::runtime_error( msg );
    }

    return m_shader;
}


ShaderGL::ShaderGL( const char* vertPath, const char* fragPath )
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
        glGetProgramInfoLog( m_programID, sizeof( infoLog ), nullptr, infoLog );
        glDeleteShader( vertShader );
        glDeleteShader( fragShader );
        glDeleteProgram( m_programID );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "ShaderGL program link failed (%s + %s):\n%s  (ShaderGL::ShaderGL)", vertPath, fragPath, infoLog );
        throw std::runtime_error( msg );
    }

    // Shaders are linked into the program — delete the intermediate objects
    glDeleteShader( vertShader );
    glDeleteShader( fragShader );
}


ShaderGL::~ShaderGL()
{
    if ( m_programID )
    {
        glDeleteProgram( m_programID );
    }
}


void ShaderGL::Use() const
{
    glUseProgram( m_programID );
}


GLuint ShaderGL::GetProgramID() const
{
    return m_programID;
}


void ShaderGL::SetInt( const char* name, int value ) const
{
    glUniform1i( glGetUniformLocation( m_programID, name ), value );
}


void ShaderGL::SetFloat( const char* name, float value ) const
{
    glUniform1f( glGetUniformLocation( m_programID, name ), value );
}


void ShaderGL::SetVec3( const char* name, const Vector3& v ) const
{
    glUniform3f( glGetUniformLocation( m_programID, name ), v.x, v.y, v.z );
}


void ShaderGL::SetVec3( const char* name, float x, float y, float z ) const
{
    glUniform3f( glGetUniformLocation( m_programID, name ), x, y, z );
}


void ShaderGL::SetVec4( const char* name, float x, float y, float z, float w ) const
{
    glUniform4f( glGetUniformLocation( m_programID, name ), x, y, z, w );
}


void ShaderGL::SetMat4( const char* name, const Matrix4& mat ) const
{
    glUniformMatrix4fv( glGetUniformLocation( m_programID, name ), 1, GL_FALSE, mat.Data() );
}

// --- Includes ---
#include "SkullbonezShaderGL.h"

#include <memory>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


namespace
{
struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;
} // namespace


std::string ShaderGL::LoadShaderSource( const char* path )
{
    FILE* rawFile = nullptr;
    errno_t err = fopen_s( &rawFile, path, "rb" );
    if ( err != 0 || !rawFile )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open m_shader file: %s  (ShaderGL::LoadShaderSource)", path );
        throw std::runtime_error( msg );
    }
    FileHandle file( rawFile );

    if ( fseek( file.get(), 0, SEEK_END ) != 0 )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to seek m_shader file: %s  (ShaderGL::LoadShaderSource)", path );
        throw std::runtime_error( msg );
    }
    long length = ftell( file.get() );
    if ( length < 0 )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to measure m_shader file: %s  (ShaderGL::LoadShaderSource)", path );
        throw std::runtime_error( msg );
    }
    fseek( file.get(), 0, SEEK_SET );

    std::string source( static_cast<size_t>( length ), '\0' );
    if ( !source.empty() )
    {
        const size_t bytesRead = fread( source.data(), 1, source.size(), file.get() );
        if ( bytesRead != source.size() )
        {
            char msg[512];
            sprintf_s( msg, sizeof( msg ), "Failed to read m_shader file: %s  (ShaderGL::LoadShaderSource)", path );
            throw std::runtime_error( msg );
        }
    }

    return source;
}


GLuint ShaderGL::CompileShader( const char* path, GLenum type )
{
    // --- Shader Compilation Concept ---
    // Shaders are small programs written in GLSL that run on the GPU. Unlike C++ which is
    // compiled once at build time, GLSL shaders are compiled at runtime by the GPU driver
    // because different GPUs have different instruction sets.
    //
    // The process is: load text file → give source to GPU driver → driver compiles it →
    // we check for errors → return a handle to the compiled shader.

    std::string source = LoadShaderSource( path );
    const char* sourceText = source.c_str();

    // Create a shader object on the GPU. GL_VERTEX_SHADER or GL_FRAGMENT_SHADER tells the
    // driver what stage of the pipeline this shader is for.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCreateShader.xhtml
    GLuint m_shader = glCreateShader( type );

    // Upload the GLSL source code text to the shader object.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glShaderSource.xhtml
    glShaderSource( m_shader, 1, &sourceText, nullptr );

    // Ask the GPU driver to compile the source code into GPU machine code.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCompileShader.xhtml
    glCompileShader( m_shader );

    GLint success = 0;
    // Check if compilation succeeded. If there are syntax errors in the GLSL code,
    // the driver reports them via the info log.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetShader.xhtml
    glGetShaderiv( m_shader, GL_COMPILE_STATUS, &success );
    if ( !success )
    {
        char infoLog[1024];
        // Retrieve the compiler error messages (similar to GCC/MSVC error output).
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetShaderInfoLog.xhtml
        glGetShaderInfoLog( m_shader, sizeof( infoLog ), nullptr, infoLog );
        // Clean up the failed shader object to avoid GPU resource leaks.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteShader.xhtml
        glDeleteShader( m_shader );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "ShaderGL compilation failed (%s):\n%s  (ShaderGL::CompileShader)", path, infoLog );
        throw std::runtime_error( msg );
    }

    return m_shader;
}


ShaderGL::ShaderGL( const char* vertPath, const char* fragPath )
{
    // --- Shader Program Linking Concept ---
    // A complete GPU "program" needs at minimum a vertex shader (positions vertices) and a
    // fragment shader (colors pixels). These are compiled separately, then "linked" together
    // into a program object — similar to how .obj files are linked into an .exe.
    //
    //  Vertex Shader (.vert)     Fragment Shader (.frag)
    //       |                         |
    //       v                         v
    //  [glCompileShader]         [glCompileShader]
    //       |                         |
    //       +--- [glLinkProgram] ----+
    //                   |
    //                   v
    //         Linked Program (ready to use)

    GLuint vertShader = CompileShader( vertPath, GL_VERTEX_SHADER );
    GLuint fragShader = CompileShader( fragPath, GL_FRAGMENT_SHADER );

    // Create an empty program object that will hold the linked shaders.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCreateProgram.xhtml
    m_programID = glCreateProgram();

    // Attach compiled shaders to the program (like adding .obj files to a linker command).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glAttachShader.xhtml
    glAttachShader( m_programID, vertShader );
    glAttachShader( m_programID, fragShader );

    // Bind a clean dummy VAO before linking to ensure the driver's JIT compiler captures a
    // deterministic vertex attribute state. Without this, NVIDIA drivers emit a MEDIUM perf
    // warning ("vertex shader is being recompiled based on GL state") when the VAO state at
    // first draw differs from the state at link time.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindVertexArray.xhtml
    GLint prevVAO = 0;
    glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &prevVAO );
    GLuint dummyVAO = 0;
    glGenVertexArrays( 1, &dummyVAO );
    glBindVertexArray( dummyVAO );

    // Link the program — resolves connections between vertex shader outputs and fragment
    // shader inputs (varyings), validates attribute bindings, and produces final GPU code.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glLinkProgram.xhtml
    glLinkProgram( m_programID );

    // Prime the driver's internal shader cache by activating the program once immediately
    // after linking. This forces eager compilation against the clean VAO state and prevents
    // a deferred recompile at first draw time.
    glUseProgram( m_programID );
    glUseProgram( 0 );

    // Restore previous VAO and delete the temporary one.
    glBindVertexArray( static_cast<GLuint>( prevVAO ) );
    glDeleteVertexArrays( 1, &dummyVAO );

    GLint success = 0;
    // Check if linking succeeded (can fail if outputs/inputs don't match, etc.).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetProgram.xhtml
    glGetProgramiv( m_programID, GL_LINK_STATUS, &success );
    if ( !success )
    {
        char infoLog[1024];
        // Retrieve linker error messages.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetProgramInfoLog.xhtml
        glGetProgramInfoLog( m_programID, sizeof( infoLog ), nullptr, infoLog );
        glDeleteShader( vertShader );
        glDeleteShader( fragShader );
        glDeleteProgram( m_programID );

        char msg[1536];
        sprintf_s( msg, sizeof( msg ), "ShaderGL program link failed (%s + %s):\n%s  (ShaderGL::ShaderGL)", vertPath, fragPath, infoLog );
        throw std::runtime_error( msg );
    }

    // Shaders are linked into the program — delete the intermediate compiled objects.
    // The program retains its own copy of the machine code; these are no longer needed.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteShader.xhtml
    glDeleteShader( vertShader );
    glDeleteShader( fragShader );
}


ShaderGL::~ShaderGL()
{
    if ( m_programID )
    {
        // Delete the linked shader program and free its GPU resources.
        // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDeleteProgram.xhtml
        glDeleteProgram( m_programID );
    }
}


void ShaderGL::Use() const
{
    // Activate this shader program — all subsequent draw calls will use these shaders
    // until a different program is activated. Only one program can be active at a time.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUseProgram.xhtml
    glUseProgram( m_programID );
}


GLuint ShaderGL::GetProgramID() const
{
    return m_programID;
}


GLint ShaderGL::GetUniformLocation( const char* name ) const
{
    auto it = m_uniformCache.find( name );
    if ( it != m_uniformCache.end() )
    {
        return it->second;
    }
    // Query the GPU for the location (index) of a uniform variable in the shader program.
    // Uniforms are "global settings" you pass from C++ to GLSL — things like the camera
    // matrix, light position, or texture unit number. The location is a small integer that
    // identifies which uniform slot to write to.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetUniformLocation.xhtml
    GLint loc = glGetUniformLocation( m_programID, name );
    m_uniformCache[name] = loc;
    return loc;
}


void ShaderGL::SetInt( const char* name, int value ) const
{
    // Upload a single integer to a uniform variable in the active shader.
    // Commonly used to tell a shader which texture slot to sample from (e.g. "use texture 0").
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
    glUniform1i( GetUniformLocation( name ), value );
}


void ShaderGL::SetFloat( const char* name, float value ) const
{
    // Upload a single float to a uniform. Used for time, opacity, animation parameters, etc.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
    glUniform1f( GetUniformLocation( name ), value );
}


void ShaderGL::SetVec3( const char* name, const Vector3& v ) const
{
    // Upload a 3-component vector (x,y,z) to a uniform. Used for positions, directions,
    // colors (RGB), and other 3D quantities.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
    glUniform3f( GetUniformLocation( name ), v.x, v.y, v.z );
}


void ShaderGL::SetVec3( const char* name, float x, float y, float z ) const
{
    glUniform3f( GetUniformLocation( name ), x, y, z );
}


void ShaderGL::SetVec4( const char* name, float x, float y, float z, float w ) const
{
    // Upload a 4-component vector. Used for RGBA colors, homogeneous positions (with w),
    // clip plane equations, and light/material properties.
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
    glUniform4f( GetUniformLocation( name ), x, y, z, w );
}


void ShaderGL::SetMat4( const char* name, const Matrix4& mat ) const
{
    // Upload a 4×4 matrix to a uniform. This is how we send transformation matrices
    // (model, view, projection) to the vertex shader so it can position vertices correctly.
    // GL_FALSE = don't transpose (our matrices are already in column-major order, which
    // is what OpenGL expects).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
    glUniformMatrix4fv( GetUniformLocation( name ), 1, GL_FALSE, mat.Data() );
}

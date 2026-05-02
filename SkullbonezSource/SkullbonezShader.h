#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIShader.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;


namespace SkullbonezCore
{
namespace Rendering
{
/* -- Shader ----------------------------------------------------------------------------------------------------------------------------------------------------

    OpenGL 3.3 implementation of IShader. Loads, compiles, and links a GLSL shader program
    from vertex and fragment shader source files.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Shader : public IShader
{

  private:
    GLuint m_programID; // OpenGL shader program handle

    static GLuint CompileShader( const char* path, GLenum type ); // Compile a single shader stage from file
    static char* LoadShaderSource( const char* path );            // Read shader source from file

  public:
    Shader( const char* vertPath, const char* fragPath ); // Constructor: compile and link from files
    ~Shader() override;                                   // Destructor: delete program

    void Use() const override;   // Bind this shader program
    GLuint GetProgramID() const; // Get the OpenGL program handle

    void SetInt( const char* name, int value ) const override;                           // Set int uniform
    void SetFloat( const char* name, float value ) const override;                       // Set float uniform
    void SetVec3( const char* name, const Vector3& v ) const override;                   // Set vec3 uniform
    void SetVec3( const char* name, float x, float y, float z ) const override;          // Set vec3 uniform (components)
    void SetVec4( const char* name, float x, float y, float z, float w ) const override; // Set vec4 uniform
    void SetMat4( const char* name, const Matrix4& mat ) const override;                 // Set mat4 uniform
};
} // namespace Rendering
} // namespace SkullbonezCore

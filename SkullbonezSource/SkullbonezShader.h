#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;


namespace SkullbonezCore
{
namespace Rendering
{
/* -- Shader ----------------------------------------------------------------------------------------------------------------------------------------------------

    Loads, compiles, and links a GLSL shader program from vertex and fragment shader source files.
    Provides uniform setters for common types used in the rendering pipeline.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Shader
{

  private:
    GLuint m_programID; // OpenGL shader program handle

    static GLuint CompileShader( const char* path, GLenum type ); // Compile a single shader stage from file
    static char* LoadShaderSource( const char* path );            // Read shader source from file

  public:
    Shader( const char* vertPath, const char* fragPath ); // Constructor: compile and link from files
    ~Shader();                                            // Destructor: delete program

    void Use() const;            // Bind this shader program
    GLuint GetProgramID() const; // Get the OpenGL program handle

    void SetInt( const char* name, int value ) const;                           // Set int uniform
    void SetFloat( const char* name, float value ) const;                       // Set float uniform
    void SetVec3( const char* name, const Vector3& v ) const;                   // Set vec3 uniform
    void SetVec3( const char* name, float x, float y, float z ) const;          // Set vec3 uniform (components)
    void SetVec4( const char* name, float x, float y, float z, float w ) const; // Set vec4 uniform
    void SetMat4( const char* name, const Matrix4& mat ) const;                 // Set mat4 uniform
};
} // namespace Rendering
} // namespace SkullbonezCore

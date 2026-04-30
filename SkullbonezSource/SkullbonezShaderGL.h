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
class Shader
{
  private:
    GLuint m_programID;

    static GLuint CompileShader( const char* path, GLenum type );
    static char* LoadShaderSource( const char* path );

  public:
    Shader( const char* vertPath, const char* fragPath );
    ~Shader();

    void Use() const;
    GLuint GetProgramID() const;

    void SetInt( const char* name, int value ) const;
    void SetFloat( const char* name, float value ) const;
    void SetVec3( const char* name, const Vector3& v ) const;
    void SetVec3( const char* name, float x, float y, float z ) const;
    void SetVec4( const char* name, float x, float y, float z, float w ) const;
    void SetMat4( const char* name, const Matrix4& mat ) const;
};
} // namespace Rendering
} // namespace SkullbonezCore

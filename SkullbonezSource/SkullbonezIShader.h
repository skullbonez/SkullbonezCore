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
/* -- IShader ----------------------------------------------------------------------------------------------------------------------------------------------------

    Abstract shader interface. Concrete implementations handle GLSL (OpenGL) or HLSL (DirectX).
    Uniform setters write shader constants — the backend handles how they're uploaded.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class IShader
{

  public:
    virtual ~IShader() = default;

    virtual void Use() const = 0;
    virtual void SetInt( const char* name, int value ) const = 0;
    virtual void SetFloat( const char* name, float value ) const = 0;
    virtual void SetVec3( const char* name, const Vector3& v ) const = 0;
    virtual void SetVec3( const char* name, float x, float y, float z ) const = 0;
    virtual void SetVec4( const char* name, float x, float y, float z, float w ) const = 0;
    virtual void SetMat4( const char* name, const Matrix4& mat ) const = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore

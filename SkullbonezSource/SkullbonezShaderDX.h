#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezMatrix4.h"
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;


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
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;

    static HRESULT CompileShader( const char* hlslPath, const char* entryPoint, const char* target, ID3DBlob** blob );
    static char* LoadShaderSource( const char* path );

  public:
    Shader( const char* hlslPath );
    ~Shader() = default;

    void Use() const;

    void SetInt( const char* name, int value ) const;
    void SetFloat( const char* name, float value ) const;
    void SetVec3( const char* name, const Vector3& v ) const;
    void SetVec3( const char* name, float x, float y, float z ) const;
    void SetVec4( const char* name, float x, float y, float z, float w ) const;
    void SetMat4( const char* name, const Matrix4& mat ) const;

    ID3D11InputLayout* GetInputLayout() const { return m_inputLayout.Get(); }
};
} // namespace Rendering
} // namespace SkullbonezCore

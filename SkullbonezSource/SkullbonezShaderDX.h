#pragma once


// --- Includes ---
#include "SkullbonezIShader.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <vector>
#include <unordered_map>
#include <string>


namespace SkullbonezCore
{
namespace Rendering
{
/* -- ShaderDX --------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the IShader interface.
    Compiles HLSL from combined VS+PS files. Uses D3D11 reflection for constant buffer layout.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShaderDX : public IShader
{

  private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11VertexShader* m_vs;
    ID3D11PixelShader* m_ps;
    ID3D11Buffer* m_cbuffer;
    uint32_t m_cbSize;

    std::vector<uint8_t> m_vsBytecode;
    mutable std::vector<uint8_t> m_cbData;
    mutable bool m_cbDirty;

    struct UniformInfo
    {
        uint32_t offset;
        uint32_t size;
    };
    std::unordered_map<std::string, UniformInfo> m_uniformMap;

  public:
    ShaderDX( ID3D11Device* device, ID3D11DeviceContext* context );
    ~ShaderDX() override;

    bool Compile( const char* hlslPath );

    void Use() const override;
    void SetInt( const char* name, int value ) const override;
    void SetFloat( const char* name, float value ) const override;
    void SetVec3( const char* name, const Vector3& v ) const override;
    void SetVec3( const char* name, float x, float y, float z ) const override;
    void SetVec4( const char* name, float x, float y, float z, float w ) const override;
    void SetMat4( const char* name, const Math::Transformation::Matrix4& mat ) const override;

    void FlushCB() const;
    const void* GetVSBytecode() const;
    size_t GetVSBytecodeSize() const;
};
} // namespace Rendering
} // namespace SkullbonezCore
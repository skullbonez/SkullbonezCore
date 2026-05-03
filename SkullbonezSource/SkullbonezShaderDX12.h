#pragma once


// --- Includes ---
#include "SkullbonezIShader.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX12;


/* -- ShaderDX12 -------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 ShaderGL implementation. Compiles combined VS+PS from a single HLSL file.
    Uses D3DReflect for cbuffer layout. Stores VS/PS bytecode for PSO creation.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShaderDX12 : public IShader
{

  private:
    ID3DBlob* m_vsBlob;
    ID3DBlob* m_psBlob;

    // Uniform reflection
    struct UniformInfo
    {
        UINT offset;
        UINT size;
    };
    std::unordered_map<std::string, UniformInfo> m_uniformMap;
    UINT m_cbSize;
    mutable std::vector<uint8_t> m_cbData;
    mutable bool m_cbDirty;

    void ReflectCB( ID3DBlob* blob );

  public:
    ShaderDX12();
    ~ShaderDX12() override;

    bool Compile( const char* hlslPath );

    void Use() const override;
    void SetInt( const char* name, int value ) const override;
    void SetFloat( const char* name, float value ) const override;
    void SetVec3( const char* name, float x, float y, float z ) const override;
    void SetVec3( const char* name, const Math::Vector::Vector3& v ) const override;
    void SetVec4( const char* name, float x, float y, float z, float w ) const override;
    void SetMat4( const char* name, const Math::Transformation::Matrix4& m ) const override;

    // DX12-specific: flush CB to upload buffer, return GPU VA
    D3D12_GPU_VIRTUAL_ADDRESS FlushCB() const;

    const void* GetVSBytecode() const;
    SIZE_T GetVSBytecodeSize() const;
    const void* GetPSBytecode() const;
    SIZE_T GetPSBytecodeSize() const;
};
} // namespace Rendering
} // namespace SkullbonezCore

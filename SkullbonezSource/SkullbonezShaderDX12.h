/*
File: SkullbonezSource/SkullbonezShaderDX12.h
Purpose:
  Compiles and binds shaders/root signatures for the DX12 renderer.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/SkullbonezShaderDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezIShader.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX12;
struct ShaderProgramDesc;


/* -- ShaderDX12 -------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 shader wrapper.

    A single HLSL file provides the vertex shader (VS) and pixel shader (PS).
    The wrapper compiles both, reflects the constant-buffer layout, stores a
    CPU-side copy of uniform bytes, and exposes bytecode for DX12 PSO creation.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShaderDX12 : public IShader
{

  private:
    Microsoft::WRL::ComPtr<ID3DBlob> m_vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> m_psBlob;

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
    const ShaderProgramDesc* m_contract;
#ifdef _DEBUG
    struct ResourceInfo
    {
        UINT bindPoint;
        D3D_SHADER_INPUT_TYPE type;
    };
    std::unordered_map<std::string, ResourceInfo> m_resourceMap;
    mutable std::vector<uint8_t> m_contractUniformsSet;
    mutable std::vector<uint8_t> m_contractMissingRequiredLogged;
    mutable std::vector<std::string> m_missingUniformWarnings;
    mutable std::vector<std::string> m_typeMismatchWarnings;
#endif

    void ReflectCB( ID3DBlob* blob );
#ifdef _DEBUG
    void ResetContractActivation() const;
    void MarkContractUniformSet( const char* name, const char* setterName ) const;
    void ReportMissingRequiredContractUniforms() const;
    void ReportContractReflectionMismatch() const;
    void ReportUniformNotReflected( const char* name, const char* setterName ) const;
#endif

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
    bool SetConstantBufferBytes( const void* data, size_t size, const char* debugName ) const override;

    // Flush the dirty constant-buffer bytes into the backend upload arena and
    // return the GPU virtual address used by the root CBV binding.
    D3D12_GPU_VIRTUAL_ADDRESS FlushCB() const;

    const void* GetVSBytecode() const;
    SIZE_T GetVSBytecodeSize() const;
    const void* GetPSBytecode() const;
    SIZE_T GetPSBytecodeSize() const;
};
} // namespace Rendering
} // namespace SkullbonezCore

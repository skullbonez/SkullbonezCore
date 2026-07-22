/*
File: SkullbonezSource/Rendering/DX12/ShaderDX12.h
Purpose:
  Declares the baked-shader wrapper used by the DX12 renderer.

Summary:
  A shader owns verified vertex/pixel bytecode plus a reflected constant layout.
  Draw code writes named values into its CPU byte copy, then flushes that copy
  through the frame upload owner before binding the pipeline.

Glossary:
  CBV (Constant Buffer View): Descriptor or root binding that lets shaders read
  a packed block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
    must stay explicit.
  - Device, pipeline, shader-development, and upload-owner references are stable
    for the shader's lifetime; the shader never retains the aggregate backend.

Related:
  - SkullbonezSource/Rendering/DX12/ShaderDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../../Core/ByteView.h"
#include "../../Core/Common.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class Dx12RenderDevice;
class Dx12PipelineOwner;
class Dx12ShaderDevelopment;
class Dx12UploadReservations;
struct ShaderProgramDesc;

struct ShaderDX12ReloadPayload
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
    size_t vertexHash = 0;
    size_t pixelHash = 0;
};


/* -- ShaderDX12
-------------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 shader wrapper.

    A single HLSL file provides the vertex shader (VS) and pixel shader (PS).
    The wrapper loads both baked stages, reflects the constant-buffer layout,
    stores a CPU-side copy of uniform bytes, and exposes bytecode for DX12 PSO
    creation. Manual dev reload adopts another verified baked pair without
    changing the live object or its constant values until validation succeeds.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ShaderDX12
{
    friend class Dx12PipelineOwner;

  private:
    Dx12RenderDevice& m_device;
    Dx12PipelineOwner& m_pipeline;
    Dx12ShaderDevelopment& m_shaderDevelopment;
    Dx12UploadReservations& m_uploadReservations;
    Microsoft::WRL::ComPtr<ID3DBlob> m_vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> m_psBlob;

    // Uniform reflection
    struct UniformInfo
    {
        UINT offset;
        UINT size;
    };
    std::unordered_map<std::string, UniformInfo> m_uniformMap;
    UINT m_cbReflectedSize;
    UINT m_cbSize;
    mutable std::vector<uint8_t> m_cbData;
    mutable bool m_cbDirty;
    size_t m_vsBytecodeHash;
    size_t m_psBytecodeHash;
    std::string m_sourcePath;
    const ShaderProgramDesc* m_contract;
    bool m_registeredForDevelopment = false;
    struct ResourceInfo
    {
        UINT bindPoint;
        UINT space;
        D3D_SHADER_INPUT_TYPE type;
        D3D_SRV_DIMENSION dimension;
    };
    std::unordered_map<std::string, ResourceInfo> m_resourceMap;
#ifdef _DEBUG
    mutable std::vector<uint8_t> m_contractUniformsSet;
    mutable std::vector<uint8_t> m_contractMissingRequiredLogged;
    mutable std::vector<std::string> m_missingUniformWarnings;
    mutable std::vector<std::string> m_typeMismatchWarnings;
#endif

    bool ReflectCB( ID3DBlob* blob, const char* hlslPath, const char* stageName );
    bool ValidateReflectedContract( std::string& outError ) const;
    const UniformInfo* FindUniformInfo( const char* name ) const;
#ifdef _DEBUG
    void ResetContractActivation() const;
    void MarkContractUniformSet( const char* name, const char* setterName ) const;
    void ReportMissingRequiredContractUniforms() const;
    void ReportContractReflectionMismatch() const;
    void ReportUniformNotReflected( const char* name, const char* setterName ) const;
#endif

  public:
    ShaderDX12( Dx12RenderDevice& device,
                Dx12PipelineOwner& pipeline,
                Dx12ShaderDevelopment& shaderDevelopment,
                Dx12UploadReservations& uploadReservations,
                bool registerForDevelopment = true );
    ~ShaderDX12();

    bool Compile( const char* hlslPath );
    bool CanAdoptReload( const ShaderDX12& candidate ) const;
    bool PrepareReload( ShaderDX12ReloadPayload& payload ) const;
    void AdoptReload( ShaderDX12ReloadPayload& payload );
    const char* SourcePath() const;

    void Use() const;
    void SetInt( const char* name, int value ) const;
    void SetFloat( const char* name, float value ) const;
    void SetVec3( const char* name, float x, float y, float z ) const;
    void SetVec3( const char* name, const Math::Vector::Vector3& v ) const;
    void SetVec4( const char* name, float x, float y, float z, float w ) const;
    void SetMat4( const char* name, const Math::Transformation::Matrix4& m ) const;
    bool SetConstantBufferBytes( SkullbonezCore::Core::ByteView bytes, const char* debugName ) const;

    // Flush the dirty constant-buffer bytes into the current frame upload arena and
    // return the GPU virtual address used by the root CBV binding.
    D3D12_GPU_VIRTUAL_ADDRESS FlushCB() const;
    UINT ConstantBufferUploadSize() const
    {
        return m_cbSize;
    }

    const uint8_t* GetVSBytecode() const;
    SIZE_T GetVSBytecodeSize() const;
    size_t GetVSBytecodeHash() const;
    const uint8_t* GetPSBytecode() const;
    SIZE_T GetPSBytecodeSize() const;
    size_t GetPSBytecodeHash() const;
    bool ValidateInputLayout( const D3D12_INPUT_ELEMENT_DESC* elements, UINT count, const char*& outError ) const;
};
} // namespace Rendering
} // namespace SkullbonezCore

/*
File: SkullbonezSource/SkullbonezShaderDX12.cpp
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
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/SkullbonezShaderDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezShaderDX12.h"
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderContracts.h"
#include <d3d11shader.h>
#include <stdexcept>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>


using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using Microsoft::WRL::ComPtr;


ShaderDX12::ShaderDX12()
    : m_cbSize( 0 ), m_cbDirty( false ), m_contract( nullptr )
{
}


ShaderDX12::~ShaderDX12() = default;


bool ShaderDX12::Compile( const char* hlslPath )
{
    m_contract = FindShaderProgramDesc( hlslPath );

    // Read file
    std::ifstream file( hlslPath, std::ios::binary );
    if ( !file.is_open() )
    {
        throw std::runtime_error( std::string( "Cannot open HLSL: " ) + hlslPath );
    }
    std::string source( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    // Compile the vertex shader from HLSL source code. D3DCompile takes human-readable HLSL text
    // and converts it into GPU bytecode. The "vs_5_0" target means Vertex Shader Model 5.0.
    // D3D_COMPILE_STANDARD_FILE_INCLUDE enables #include directives in the shader source.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile( source.c_str(), source.size(), hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_vs", "vs_5_0", flags, 0, m_vsBlob.ReleaseAndGetAddressOf(), errors.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        std::string msg = "VS compile failed: ";
        if ( errors )
        {
            msg += static_cast<const char*>( errors->GetBufferPointer() );
        }
        throw std::runtime_error( msg );
    }
    errors.Reset();

    // Compile the pixel shader from the same HLSL file. The "ps_5_0" target means Pixel Shader
    // Model 5.0. Both VS and PS live in the same .hlsl file with different entry points.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
    hr = D3DCompile( source.c_str(), source.size(), hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_ps", "ps_5_0", flags, 0, m_psBlob.ReleaseAndGetAddressOf(), errors.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        std::string msg = "PS compile failed: ";
        if ( errors )
        {
            msg += static_cast<const char*>( errors->GetBufferPointer() );
        }
        throw std::runtime_error( msg );
    }
    errors.Reset();

    // Reflect both stages so PS-only post/sky uniforms are visible to SetFloat/SetVec*.
    ReflectCB( m_vsBlob.Get() );
    ReflectCB( m_psBlob.Get() );
#ifdef _DEBUG
    if ( m_contract )
    {
        m_contractUniformsSet.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
        m_contractMissingRequiredLogged.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
        ReportContractReflectionMismatch();
    }
#endif

    return true;
}


void ShaderDX12::ReflectCB( ID3DBlob* blob )
{
    // Use D3DReflect to inspect the compiled shader bytecode and discover constant buffer layouts.
    // Reflection tells us the name, offset, and size of each variable in the shader's cbuffer,
    // so we can write data at the correct byte offsets when setting uniforms from C++.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
    ComPtr<ID3D11ShaderReflection> reflect;
    HRESULT hr = D3DReflect( blob->GetBufferPointer(), blob->GetBufferSize(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>( reflect.GetAddressOf() ) );
    if ( FAILED( hr ) || !reflect )
    {
        throw std::runtime_error( "D3DReflect failed for DX12 shader." );
    }

    D3D11_SHADER_DESC shaderDesc = {};
    reflect->GetDesc( &shaderDesc );

    for ( UINT i = 0; i < shaderDesc.ConstantBuffers; ++i )
    {
        ID3D11ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex( i );
        D3D11_SHADER_BUFFER_DESC bufDesc = {};
        cb->GetDesc( &bufDesc );

        if ( bufDesc.Size > m_cbSize )
        {
            m_cbSize = bufDesc.Size;
        }

        for ( UINT v = 0; v < bufDesc.Variables; ++v )
        {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex( v );
            D3D11_SHADER_VARIABLE_DESC varDesc = {};
            var->GetDesc( &varDesc );
            m_uniformMap[varDesc.Name] = { varDesc.StartOffset, varDesc.Size };
        }
    }
    // Align CB size to 256 bytes (DX12 requirement)
    m_cbSize = ( m_cbSize + 255 ) & ~255u;
    m_cbData.resize( m_cbSize, 0 );
}


void ShaderDX12::Use() const
{
    auto* backend = RenderBackendDX12::Get();
    if ( backend )
    {
        backend->SetActiveShader( const_cast<ShaderDX12*>( this ) );
    }
#ifdef _DEBUG
    ResetContractActivation();
#endif
}


#ifdef _DEBUG
namespace
{
bool ContainsWarningKey( const std::vector<std::string>& warnings, const std::string& key )
{
    for ( const std::string& warning : warnings )
    {
        if ( warning == key )
        {
            return true;
        }
    }
    return false;
}

ShaderValueType ShaderValueTypeForSetter( const char* setterName )
{
    if ( std::strcmp( setterName, "SetInt" ) == 0 )
    {
        return ShaderValueType::Int;
    }
    if ( std::strcmp( setterName, "SetFloat" ) == 0 )
    {
        return ShaderValueType::Float;
    }
    if ( std::strcmp( setterName, "SetVec3" ) == 0 )
    {
        return ShaderValueType::Vec3;
    }
    if ( std::strcmp( setterName, "SetVec4" ) == 0 )
    {
        return ShaderValueType::Vec4;
    }
    return ShaderValueType::Mat4;
}
} // namespace


void ShaderDX12::ResetContractActivation() const
{
    if ( !m_contract )
    {
        return;
    }
    if ( m_contractUniformsSet.size() != m_contract->uniformCount )
    {
        m_contractUniformsSet.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
    }
    else
    {
        std::fill( m_contractUniformsSet.begin(), m_contractUniformsSet.end(), static_cast<uint8_t>( 0 ) );
    }
}


void ShaderDX12::MarkContractUniformSet( const char* name, const char* setterName ) const
{
    if ( !m_contract || !name )
    {
        return;
    }

    size_t uniformIndex = static_cast<size_t>( -1 );
    const ShaderUniformDecl* uniform = FindShaderUniformDecl( *m_contract, name, &uniformIndex );
    if ( !uniform )
    {
        for ( size_t i = 0; i < m_contract->resourceCount; ++i )
        {
            const ShaderResourceDecl& resource = m_contract->resources[i];
            if ( ShaderContractNameEquals( resource.name, name ) )
            {
                const std::string key = std::string( name ) + ":" + setterName + ":resource_via_uniform_api";
                if ( !ContainsWarningKey( m_missingUniformWarnings, key ) )
                {
                    m_missingUniformWarnings.push_back( key );
                    Log().WriteEventf( "shader_contract_resource_set_with_uniform_api shader=%s resource=%s slot=%d setter=%s",
                                       m_contract->baseName,
                                       name,
                                       resource.slot,
                                       setterName );
                }
                return;
            }
        }

        const std::string key = std::string( name ) + ":" + setterName + ":not_in_contract";
        if ( !ContainsWarningKey( m_missingUniformWarnings, key ) )
        {
            m_missingUniformWarnings.push_back( key );
            Log().WriteEventf( "shader_contract_stale_uniform shader=%s uniform=%s setter=%s reason=not_in_contract",
                               m_contract->baseName,
                               name,
                               setterName );
        }
        return;
    }

    const ShaderValueType setterType = ShaderValueTypeForSetter( setterName );
    if ( uniform->type != setterType )
    {
        const std::string key = std::string( name ) + ":" + setterName + ":type_mismatch";
        if ( !ContainsWarningKey( m_typeMismatchWarnings, key ) )
        {
            m_typeMismatchWarnings.push_back( key );
            Log().WriteEventf( "shader_contract_uniform_type_mismatch shader=%s uniform=%s setter=%s expected=%s",
                               m_contract->baseName,
                               name,
                               setterName,
                               ShaderValueTypeName( uniform->type ) );
        }
    }

    if ( uniformIndex < m_contractUniformsSet.size() )
    {
        m_contractUniformsSet[uniformIndex] = static_cast<uint8_t>( 1 );
    }
}


void ShaderDX12::ReportMissingRequiredContractUniforms() const
{
    if ( !m_contract || m_contractUniformsSet.size() != m_contract->uniformCount )
    {
        return;
    }

    if ( m_contractMissingRequiredLogged.size() != m_contract->uniformCount )
    {
        m_contractMissingRequiredLogged.assign( m_contract->uniformCount, static_cast<uint8_t>( 0 ) );
    }

    for ( size_t i = 0; i < m_contract->uniformCount; ++i )
    {
        const ShaderUniformDecl& uniform = m_contract->uniforms[i];
        if ( uniform.required && m_contractUniformsSet[i] == static_cast<uint8_t>( 0 ) && m_contractMissingRequiredLogged[i] == static_cast<uint8_t>( 0 ) )
        {
            m_contractMissingRequiredLogged[i] = static_cast<uint8_t>( 1 );
            Log().WriteEventf( "shader_contract_required_uniform_not_set shader=%s uniform=%s pass=%s",
                               m_contract->baseName,
                               uniform.name,
                               m_contract->passCategory ? m_contract->passCategory : "unknown" );
        }
    }
}


void ShaderDX12::ReportContractReflectionMismatch() const
{
    if ( !m_contract )
    {
        return;
    }

    for ( size_t i = 0; i < m_contract->uniformCount; ++i )
    {
        const ShaderUniformDecl& uniform = m_contract->uniforms[i];
        if ( uniform.required && m_uniformMap.find( uniform.name ) == m_uniformMap.end() )
        {
            Log().WriteEventf( "shader_contract_required_uniform_not_reflected shader=%s uniform=%s expected=%s",
                               m_contract->baseName,
                               uniform.name,
                               ShaderValueTypeName( uniform.type ) );
        }
    }
}


void ShaderDX12::ReportUniformNotReflected( const char* name, const char* setterName ) const
{
    if ( !m_contract )
    {
        return;
    }
    if ( !FindShaderUniformDecl( *m_contract, name ) )
    {
        return;
    }
    const char* shaderName = m_contract ? m_contract->baseName : "<unmanifested>";
    const std::string key = std::string( shaderName ) + ":" + ( name ? name : "" ) + ":" + setterName + ":not_reflected";
    if ( ContainsWarningKey( m_missingUniformWarnings, key ) )
    {
        return;
    }

    m_missingUniformWarnings.push_back( key );
    Log().WriteEventf( "shader_uniform_not_reflected shader=%s uniform=%s setter=%s",
                       shaderName,
                       name ? name : "<null>",
                       setterName );
}
#endif


void ShaderDX12::SetInt( const char* name, int value ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetInt" );
#endif
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetInt" );
#endif
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( int ) );
    m_cbDirty = true;
}


void ShaderDX12::SetFloat( const char* name, float value ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetFloat" );
#endif
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetFloat" );
#endif
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( float ) );
    m_cbDirty = true;
}


void ShaderDX12::SetVec3( const char* name, float x, float y, float z ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetVec3" );
#endif
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetVec3" );
#endif
        return;
    }
    float v[3] = { x, y, z };
    memcpy( m_cbData.data() + it->second.offset, v, sizeof( v ) );
    m_cbDirty = true;
}


void ShaderDX12::SetVec3( const char* name, const Vector3& v ) const
{
    SetVec3( name, v.x, v.y, v.z );
}


void ShaderDX12::SetVec4( const char* name, float x, float y, float z, float w ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetVec4" );
#endif
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetVec4" );
#endif
        return;
    }
    float v[4] = { x, y, z, w };
    memcpy( m_cbData.data() + it->second.offset, v, sizeof( v ) );
    m_cbDirty = true;
}


void ShaderDX12::SetMat4( const char* name, const Matrix4& m ) const
{
#ifdef _DEBUG
    MarkContractUniformSet( name, "SetMat4" );
#endif
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
#ifdef _DEBUG
        ReportUniformNotReflected( name, "SetMat4" );
#endif
        return;
    }
    // HLSL uses #pragma pack_matrix(column_major) — send data as-is
    memcpy( m_cbData.data() + it->second.offset, m.Data(), 64 );
    m_cbDirty = true;
}


D3D12_GPU_VIRTUAL_ADDRESS ShaderDX12::FlushCB() const
{
    if ( m_cbSize == 0 )
    {
        return 0;
    }

#ifdef _DEBUG
    ReportMissingRequiredContractUniforms();
#endif

    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return 0;
    }

    // Constant buffers must be 256-byte aligned in DX12. ReserveUpload probes
    // with that same alignment and flushes/resets the upload arena if needed,
    // instead of letting a busy frame throw after the arena fills up.
    D3D12_GPU_VIRTUAL_ADDRESS addr = backend->ReserveUpload( m_cbSize, 256 );
    memcpy( backend->GetUploadPtr( addr ), m_cbData.data(), m_cbSize );
    m_cbDirty = false;
    return addr;
}


const void* ShaderDX12::GetVSBytecode() const
{
    return m_vsBlob ? m_vsBlob->GetBufferPointer() : nullptr;
}


SIZE_T ShaderDX12::GetVSBytecodeSize() const
{
    return m_vsBlob ? m_vsBlob->GetBufferSize() : 0;
}


const void* ShaderDX12::GetPSBytecode() const
{
    return m_psBlob ? m_psBlob->GetBufferPointer() : nullptr;
}


SIZE_T ShaderDX12::GetPSBytecodeSize() const
{
    return m_psBlob ? m_psBlob->GetBufferSize() : 0;
}

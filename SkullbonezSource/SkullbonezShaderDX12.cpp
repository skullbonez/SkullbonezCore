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
#include <d3d11shader.h>
#include <stdexcept>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <d3dcompiler.h>
#include <wrl/client.h>


using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using Microsoft::WRL::ComPtr;


ShaderDX12::ShaderDX12()
    : m_cbSize( 0 ), m_cbDirty( false )
{
}


ShaderDX12::~ShaderDX12() = default;


bool ShaderDX12::Compile( const char* hlslPath )
{
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
}


void ShaderDX12::SetInt( const char* name, int value ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( int ) );
    m_cbDirty = true;
}


void ShaderDX12::SetFloat( const char* name, float value ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( float ) );
    m_cbDirty = true;
}


void ShaderDX12::SetVec3( const char* name, float x, float y, float z ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
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
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    float v[4] = { x, y, z, w };
    memcpy( m_cbData.data() + it->second.offset, v, sizeof( v ) );
    m_cbDirty = true;
}


void ShaderDX12::SetMat4( const char* name, const Matrix4& m ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
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

// --- Includes ---
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


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;


ShaderDX12::ShaderDX12()
    : m_vsBlob( nullptr ), m_psBlob( nullptr ), m_cbSize( 0 ), m_cbDirty( false )
{
}


ShaderDX12::~ShaderDX12()
{
    if ( m_vsBlob )
    {
        m_vsBlob->Release();
    }
    if ( m_psBlob )
    {
        m_psBlob->Release();
    }
}


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

    // Compile VS
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile( source.c_str(), source.size(), hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_vs", "vs_5_0", flags, 0, &m_vsBlob, &errors );
    if ( FAILED( hr ) )
    {
        std::string msg = "VS compile failed: ";
        if ( errors )
        {
            msg += (const char*)errors->GetBufferPointer();
            errors->Release();
        }
        throw std::runtime_error( msg );
    }
    if ( errors )
    {
        errors->Release();
        errors = nullptr;
    }

    // Compile PS
    hr = D3DCompile( source.c_str(), source.size(), hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_ps", "ps_5_0", flags, 0, &m_psBlob, &errors );
    if ( FAILED( hr ) )
    {
        std::string msg = "PS compile failed: ";
        if ( errors )
        {
            msg += (const char*)errors->GetBufferPointer();
            errors->Release();
        }
        throw std::runtime_error( msg );
    }
    if ( errors )
    {
        errors->Release();
    }

    // Reflect VS cbuffer
    ReflectCB( m_vsBlob );

    return true;
}


void ShaderDX12::ReflectCB( ID3DBlob* blob )
{
    ID3D11ShaderReflection* reflect = nullptr;
    D3DReflect( blob->GetBufferPointer(), blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflect );

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

    reflect->Release();

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

    D3D12_GPU_VIRTUAL_ADDRESS addr = backend->SubAllocateUpload( m_cbSize, 256 );
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

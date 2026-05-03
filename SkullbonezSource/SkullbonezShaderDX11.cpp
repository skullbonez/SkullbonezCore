// --- Includes ---
#include "SkullbonezShaderDX11.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezVector3.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "dxguid.lib" )


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


ShaderDX11::ShaderDX11( ID3D11Device* device, ID3D11DeviceContext* context )
    : m_device( device ), m_context( context ), m_vs( nullptr ), m_ps( nullptr ), m_cbuffer( nullptr ), m_cbSize( 0 ), m_cbDirty( false )
{
}


ShaderDX11::~ShaderDX11()
{
    if ( m_cbuffer )
    {
        m_cbuffer->Release();
    }
    if ( m_ps )
    {
        m_ps->Release();
    }
    if ( m_vs )
    {
        m_vs->Release();
    }
}


bool ShaderDX11::Compile( const char* hlslPath )
{
    // Read HLSL source
    std::ifstream file( hlslPath, std::ios::binary );
    if ( !file.is_open() )
    {
        throw std::runtime_error( std::string( "Failed to open HLSL: " ) + hlslPath );
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    // Compile vertex ShaderGL
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errBlob = nullptr;
    HRESULT hr = D3DCompile( source.c_str(),
                             source.size(),
                             hlslPath,
                             nullptr,
                             nullptr,
                             "main_vs",
                             "vs_5_0",
                             D3DCOMPILE_ENABLE_STRICTNESS,
                             0,
                             &vsBlob,
                             &errBlob );
    if ( FAILED( hr ) )
    {
        std::string err = errBlob ? (const char*)errBlob->GetBufferPointer() : "Unknown error";
        if ( errBlob )
        {
            errBlob->Release();
        }
        throw std::runtime_error( "VS compile failed: " + err );
    }
    if ( errBlob )
    {
        errBlob->Release();
    }

    m_vsBytecode.assign( (uint8_t*)vsBlob->GetBufferPointer(),
                         (uint8_t*)vsBlob->GetBufferPointer() + vsBlob->GetBufferSize() );

    hr = m_device->CreateVertexShader( vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs );
    if ( FAILED( hr ) )
    {
        vsBlob->Release();
        throw std::runtime_error( "CreateVertexShader failed" );
    }

    // Reflect VS to build uniform map
    ID3D11ShaderReflection* reflection = nullptr;
    hr = D3DReflect( vsBlob->GetBufferPointer(),
                     vsBlob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     (void**)&reflection );
    if ( SUCCEEDED( hr ) )
    {
        D3D11_SHADER_DESC shaderDesc;
        reflection->GetDesc( &shaderDesc );

        for ( UINT cb = 0; cb < shaderDesc.ConstantBuffers; ++cb )
        {
            ID3D11ShaderReflectionConstantBuffer* cbRef = reflection->GetConstantBufferByIndex( cb );
            D3D11_SHADER_BUFFER_DESC cbDesc;
            cbRef->GetDesc( &cbDesc );

            if ( cbDesc.Size > m_cbSize )
            {
                m_cbSize = cbDesc.Size;
            }

            for ( UINT v = 0; v < cbDesc.Variables; ++v )
            {
                ID3D11ShaderReflectionVariable* var = cbRef->GetVariableByIndex( v );
                D3D11_SHADER_VARIABLE_DESC varDesc;
                var->GetDesc( &varDesc );
                m_uniformMap[varDesc.Name] = { varDesc.StartOffset, varDesc.Size };
            }
        }
        reflection->Release();
    }
    vsBlob->Release();

    // Compile pixel ShaderGL
    ID3DBlob* psBlob = nullptr;
    errBlob = nullptr;
    hr = D3DCompile( source.c_str(),
                     source.size(),
                     hlslPath,
                     nullptr,
                     nullptr,
                     "main_ps",
                     "ps_5_0",
                     D3DCOMPILE_ENABLE_STRICTNESS,
                     0,
                     &psBlob,
                     &errBlob );
    if ( FAILED( hr ) )
    {
        std::string err = errBlob ? (const char*)errBlob->GetBufferPointer() : "Unknown error";
        if ( errBlob )
        {
            errBlob->Release();
        }
        throw std::runtime_error( "PS compile failed: " + err );
    }
    if ( errBlob )
    {
        errBlob->Release();
    }

    // Also reflect PS to capture PS-only uniforms
    reflection = nullptr;
    hr = D3DReflect( psBlob->GetBufferPointer(),
                     psBlob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     (void**)&reflection );
    if ( SUCCEEDED( hr ) )
    {
        D3D11_SHADER_DESC shaderDesc;
        reflection->GetDesc( &shaderDesc );

        for ( UINT cb = 0; cb < shaderDesc.ConstantBuffers; ++cb )
        {
            ID3D11ShaderReflectionConstantBuffer* cbRef = reflection->GetConstantBufferByIndex( cb );
            D3D11_SHADER_BUFFER_DESC cbDesc;
            cbRef->GetDesc( &cbDesc );

            if ( cbDesc.Size > m_cbSize )
            {
                m_cbSize = cbDesc.Size;
            }

            for ( UINT v = 0; v < cbDesc.Variables; ++v )
            {
                ID3D11ShaderReflectionVariable* var = cbRef->GetVariableByIndex( v );
                D3D11_SHADER_VARIABLE_DESC varDesc;
                var->GetDesc( &varDesc );
                if ( m_uniformMap.find( varDesc.Name ) == m_uniformMap.end() )
                {
                    m_uniformMap[varDesc.Name] = { varDesc.StartOffset, varDesc.Size };
                }
            }
        }
        reflection->Release();
    }

    hr = m_device->CreatePixelShader( psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps );
    psBlob->Release();
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "CreatePixelShader failed" );
    }

    // Create constant buffer
    if ( m_cbSize > 0 )
    {
        m_cbData.resize( m_cbSize, 0 );

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = m_cbSize;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_device->CreateBuffer( &bd, nullptr, &m_cbuffer );
        if ( FAILED( hr ) )
        {
            throw std::runtime_error( "CreateBuffer (CB) failed" );
        }
    }

    return true;
}


void ShaderDX11::Use() const
{
    m_context->VSSetShader( m_vs, nullptr, 0 );
    m_context->PSSetShader( m_ps, nullptr, 0 );
    RenderBackendDX11::Get()->SetActiveShader( const_cast<ShaderDX11*>( this ) );
}


void ShaderDX11::FlushCB() const
{
    if ( !m_cbDirty || !m_cbuffer )
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map( m_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mapped.pData, m_cbData.data(), m_cbData.size() );
        m_context->Unmap( m_cbuffer, 0 );
    }

    m_context->VSSetConstantBuffers( 0, 1, &m_cbuffer );
    m_context->PSSetConstantBuffers( 0, 1, &m_cbuffer );
    m_cbDirty = false;
}


void ShaderDX11::SetInt( const char* name, int value ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( int ) );
    m_cbDirty = true;
}


void ShaderDX11::SetFloat( const char* name, float value ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, &value, sizeof( float ) );
    m_cbDirty = true;
}


void ShaderDX11::SetVec3( const char* name, float x, float y, float z ) const
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


void ShaderDX11::SetVec4( const char* name, float x, float y, float z, float w ) const
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


void ShaderDX11::SetMat4( const char* name, const Matrix4& mat ) const
{
    auto it = m_uniformMap.find( name );
    if ( it == m_uniformMap.end() )
    {
        return;
    }
    memcpy( m_cbData.data() + it->second.offset, mat.Data(), 16 * sizeof( float ) );
    m_cbDirty = true;
}


void ShaderDX11::SetVec3( const char* name, const Vector3& v ) const
{
    SetVec3( name, v.x, v.y, v.z );
}


const void* ShaderDX11::GetVSBytecode() const
{
    return m_vsBytecode.data();
}


size_t ShaderDX11::GetVSBytecodeSize() const
{
    return m_vsBytecode.size();
}
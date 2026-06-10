// --- Includes ---
#include "SkullbonezShaderDX11.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezVector3.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <wrl/client.h>

#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "dxguid.lib" )


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using Microsoft::WRL::ComPtr;


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
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> errBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef _DEBUG
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    // Compile HLSL source code into GPU bytecode for the vertex shader. D3DCompile takes the raw
    // HLSL text, the entry point function name ("main_vs"), and the shader model target ("vs_5_0"
    // = Vertex Shader Model 5.0). Returns a blob of compiled bytecode or an error message blob.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
    HRESULT hr = D3DCompile( source.c_str(),
                             source.size(),
                             hlslPath,
                             nullptr,
                             nullptr,
                             "main_vs",
                             "vs_5_0",
                             compileFlags,
                             0,
                             vsBlob.GetAddressOf(),
                             errBlob.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        std::string err = errBlob ? static_cast<const char*>( errBlob->GetBufferPointer() ) : "Unknown error";
        throw std::runtime_error( "VS compile failed: " + err );
    }
    errBlob.Reset();

    m_vsBytecode.assign( static_cast<uint8_t*>( vsBlob->GetBufferPointer() ),
                         static_cast<uint8_t*>( vsBlob->GetBufferPointer() ) + vsBlob->GetBufferSize() );

    // Create a vertex shader object from compiled bytecode. The Device (GPU abstraction) takes the
    // bytecode blob and produces a shader object that can be bound to the pipeline. The bytecode
    // is also retained for input layout creation (which must validate against the VS signature).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createvertexshader
    hr = m_device->CreateVertexShader( vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "CreateVertexShader failed" );
    }

    // Reflect VS to build uniform map
    ComPtr<ID3D11ShaderReflection> reflection;
    // Use D3DReflect to inspect the compiled vertex shader bytecode. Reflection lets us discover
    // constant buffer layouts (names, offsets, sizes of variables) at runtime without hardcoding
    // them. This builds our uniform map so SetFloat/SetMat4/etc. know where to write data.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
    hr = D3DReflect( vsBlob->GetBufferPointer(),
                     vsBlob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     reinterpret_cast<void**>( reflection.GetAddressOf() ) );
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
    }

    // Compile pixel ShaderGL
    ComPtr<ID3DBlob> psBlob;
    errBlob.Reset();
    // Compile the pixel (fragment) shader from the same HLSL file. The entry point is "main_ps"
    // targeting "ps_5_0" (Pixel Shader Model 5.0). The pixel shader runs once per pixel-fragment
    // and determines the final color output for each covered pixel.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
    hr = D3DCompile( source.c_str(),
                     source.size(),
                     hlslPath,
                     nullptr,
                     nullptr,
                     "main_ps",
                     "ps_5_0",
                     compileFlags,
                     0,
                     psBlob.GetAddressOf(),
                     errBlob.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        std::string err = errBlob ? static_cast<const char*>( errBlob->GetBufferPointer() ) : "Unknown error";
        throw std::runtime_error( "PS compile failed: " + err );
    }
    errBlob.Reset();

    // Also reflect PS to capture PS-only uniforms
    reflection.Reset();
    // Reflect the pixel shader to discover any constant buffer variables that are only referenced
    // by the pixel shader (not the vertex shader). Merges them into our shared uniform map.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
    hr = D3DReflect( psBlob->GetBufferPointer(),
                     psBlob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     reinterpret_cast<void**>( reflection.GetAddressOf() ) );
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
    }

    // Create a pixel shader object from the compiled bytecode. Similar to CreateVertexShader but
    // for the pixel/fragment stage of the pipeline.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createpixelshader
    hr = m_device->CreatePixelShader( psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps );
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

        // Create a constant buffer (DX11's equivalent of OpenGL uniform buffers). This is a small
        // GPU buffer that holds shader parameters (matrices, colors, floats). DYNAMIC + CPU_WRITE
        // means the CPU can update it every frame via Map/Unmap without GPU stalls.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
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
    // Bind the vertex shader to the pipeline. All subsequent draw calls will run vertices through
    // this VS until a different one is bound. nullptr/0 = no class instances (advanced feature).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetshader
    m_context->VSSetShader( m_vs, nullptr, 0 );

    // Bind the pixel shader to the pipeline. All subsequent draw calls will run pixel fragments
    // through this PS until a different one is bound.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshader
    m_context->PSSetShader( m_ps, nullptr, 0 );
    RenderBackendDX11::Get()->SetActiveShader( const_cast<ShaderDX11*>( this ) );
}


void ShaderDX11::FlushCB() const
{
    if ( !m_cbDirty || !m_cbuffer )
    {
        return;
    }

    // Map the constant buffer for CPU write access. WRITE_DISCARD tells the GPU "I'm replacing
    // all the data" which avoids stalls -- the GPU gives us a fresh memory region while the old
    // one may still be in-flight. We memcpy our shadow copy of uniform data into GPU memory.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map( m_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mapped.pData, m_cbData.data(), m_cbData.size() );

        // Unmap releases our CPU pointer and signals the GPU that the data is ready to use.
        // After Unmap, the mapped.pData pointer is invalid.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap
        m_context->Unmap( m_cbuffer, 0 );
    }

    // Bind the constant buffer to slot 0 of the vertex shader stage. The VS can now read the
    // uniforms (matrices, vectors, floats) that we just uploaded.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetconstantbuffers
    m_context->VSSetConstantBuffers( 0, 1, &m_cbuffer );

    // Bind the same constant buffer to slot 0 of the pixel shader stage so the PS can also
    // read shared uniforms (e.g. light direction, fog color, material properties).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetconstantbuffers
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

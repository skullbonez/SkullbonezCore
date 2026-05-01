// --- Includes ---
#include "SkullbonezShaderDX.h"
#include "SkullbonezWindow.h"


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Basics;


char* Shader::LoadShaderSource( const char* path )
{
    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "rb" );
    if ( err != 0 || !file )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open shader file: %s  (Shader::LoadShaderSource)", path );
        throw std::runtime_error( msg );
    }

    fseek( file, 0, SEEK_END );
    long length = ftell( file );
    fseek( file, 0, SEEK_SET );

    char* source = new char[length + 1];
    fread( source, 1, static_cast<size_t>( length ), file );
    source[length] = '\0';
    fclose( file );

    return source;
}


HRESULT Shader::CompileShader( const char* hlslPath, const char* entryPoint, const char* target, ID3DBlob** blob )
{
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompileFromFile(
        SkullbonezCore::StringToWide( hlslPath ).c_str(),
        nullptr,
        nullptr,
        entryPoint,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        blob,
        &errorBlob );

    if ( FAILED( hr ) )
    {
        if ( errorBlob )
        {
            char msg[2048];
            sprintf_s( msg, sizeof( msg ), "Shader compilation failed (%s -> %s):\n%s",
                       hlslPath, entryPoint, (const char*)errorBlob->GetBufferPointer() );
            errorBlob->Release();
            throw std::runtime_error( msg );
        }
        else
        {
            char msg[512];
            sprintf_s( msg, sizeof( msg ), "Shader compilation failed (%s -> %s): 0x%08X",
                       hlslPath, entryPoint, hr );
            throw std::runtime_error( msg );
        }
    }

    return hr;
}

void Shader::CreateConstantBuffer( UINT size, UINT slot )
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = size;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    HRESULT hr = m_device->CreateBuffer( &bufferDesc, nullptr, m_constantBuffers[slot].GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create constant buffer" );
    }
}


Shader::Shader( const char* hlslPath )
{
    m_device = SkullbonezWindow::Instance()->m_d3dDevice;
    m_context = SkullbonezWindow::Instance()->m_d3dContext;

    if ( !m_device || !m_context )
    {
        throw std::runtime_error( "DirectX device not initialized when creating shader" );
    }

    ComPtr<ID3DBlob> vsBlob;
    HRESULT hr = CompileShader( hlslPath, "main_vs", "vs_5_0", &vsBlob );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to compile vertex shader" );
    }

    hr = m_device->CreateVertexShader( vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create vertex shader object" );
    }

    ComPtr<ID3DBlob> psBlob;
    hr = CompileShader( hlslPath, "main_ps", "ps_5_0", &psBlob );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to compile pixel shader" );
    }

    hr = m_device->CreatePixelShader( psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create pixel shader object" );
    }

    // Determine input layout based on shader filename
    D3D11_INPUT_ELEMENT_DESC layout[8];
    UINT layoutCount = 0;

    const char* filename = hlslPath;
    const char* lastSlash = strrchr( hlslPath, '\\' );
    if ( lastSlash )
        filename = lastSlash + 1;
    const char* lastSlash2 = strrchr( filename, '/' );
    if ( lastSlash2 )
        filename = lastSlash2 + 1;

    // Position is always present
    layout[layoutCount++] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };

    // Add input elements based on shader type
    if ( strstr( filename, "lit_textured" ) )
    {
        layout[layoutCount++] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        layout[layoutCount++] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    }
    else if ( strstr( filename, "text" ) )
    {
        layout[layoutCount++] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    }
    else if ( strstr( filename, "shadow" ) )
    {
        layout[layoutCount++] = { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        layout[layoutCount++] = { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        layout[layoutCount++] = { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        layout[layoutCount++] = { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        layout[layoutCount++] = { "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    }
    else
    {
        // unlit_textured, water - position and texcoord
        layout[layoutCount++] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    }

    hr = m_device->CreateInputLayout( layout, layoutCount, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create input layout" );
    }
}


void Shader::Use() const
{
    m_context->VSSetShader( m_vertexShader.Get(), nullptr, 0 );
    m_context->PSSetShader( m_pixelShader.Get(), nullptr, 0 );
    m_context->IASetInputLayout( m_inputLayout.Get() );
}


void Shader::SetInt( const char* name, int value ) const
{
    // Note: DirectX uses constant buffers, not immediate uniforms
    // This is a stub - real implementation uses constant buffer updates
}


void Shader::SetFloat( const char* name, float value ) const
{
    // Stub - use constant buffers in real implementation
}


void Shader::SetVec3( const char* name, const Vector3& v ) const
{
    // Stub - use constant buffers in real implementation
}


void Shader::SetVec3( const char* name, float x, float y, float z ) const
{
    // Stub - use constant buffers in real implementation
}


void Shader::SetVec4( const char* name, float x, float y, float z, float w ) const
{
    // Stub - use constant buffers in real implementation
}


void Shader::SetMat4( const char* name, const Matrix4& mat ) const
{
    // Stub - use constant buffers in real implementation
}

void Shader::UpdateConstantBuffer( UINT slot, const void* data, UINT size ) const
{
    if ( slot >= 4 || !m_constantBuffers[slot] )
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map( m_constantBuffers[slot].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    if ( SUCCEEDED( hr ) )
    {
        memcpy( mapped.pData, data, size );
        m_context->Unmap( m_constantBuffers[slot].Get(), 0 );
        m_context->VSSetConstantBuffers( slot, 1, m_constantBuffers[slot].GetAddressOf() );
        m_context->PSSetConstantBuffers( slot, 1, m_constantBuffers[slot].GetAddressOf() );
    }
}

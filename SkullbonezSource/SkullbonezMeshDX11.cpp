// --- Includes ---
#include "SkullbonezMeshDX11.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezShaderDX11.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


static D3D11_INPUT_ELEMENT_DESC s_layoutPos3[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos3Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos3Norm3Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos2Tex2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static D3D11_INPUT_ELEMENT_DESC s_layoutPos2[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};


MeshDX11::MeshDX11( ID3D11Device* device, ID3D11DeviceContext* context )
    : m_device( device ), m_context( context ), m_vb( nullptr ), m_inputLayout( nullptr ), m_vertexCount( 0 ), m_stride( 0 ), m_format( VertexFormatDX::Pos3 ), m_lastVSBytecode( nullptr )
{
}


MeshDX11::~MeshDX11()
{
    if ( m_inputLayout )
    {
        m_inputLayout->Release();
    }
    if ( m_vb )
    {
        m_vb->Release();
    }
}


bool MeshDX11::Create( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    int floatsPerVert = 3;
    if ( hasNormals )
    {
        floatsPerVert += 3;
    }
    if ( hasTexCoords )
    {
        floatsPerVert += 2;
    }

    m_stride = floatsPerVert * sizeof( float );
    m_vertexCount = vertexCount;

    if ( hasNormals && hasTexCoords )
    {
        m_format = VertexFormatDX::Pos3_Norm3_Tex2;
    }
    else if ( hasTexCoords )
    {
        m_format = VertexFormatDX::Pos3_Tex2;
    }
    else
    {
        m_format = VertexFormatDX::Pos3;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)( vertexCount * m_stride );
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    HRESULT hr = m_device->CreateBuffer( &bd, &initData, &m_vb );
    return SUCCEEDED( hr );
}


void MeshDX11::EnsureInputLayout() const
{
    auto* backend = RenderBackendDX11::Get();
    auto* ShaderGL = backend->GetActiveShader();
    if ( !ShaderGL )
    {
        return;
    }

    const void* vsBytecode = ShaderGL->GetVSBytecode();
    if ( vsBytecode == m_lastVSBytecode && m_inputLayout )
    {
        return;
    }

    if ( m_inputLayout )
    {
        m_inputLayout->Release();
        m_inputLayout = nullptr;
    }

    D3D11_INPUT_ELEMENT_DESC* elements = nullptr;
    UINT numElements = 0;

    switch ( m_format )
    {
    case VertexFormatDX::Pos3:
        elements = s_layoutPos3;
        numElements = 1;
        break;
    case VertexFormatDX::Pos3_Tex2:
        elements = s_layoutPos3Tex2;
        numElements = 2;
        break;
    case VertexFormatDX::Pos3_Norm3_Tex2:
        elements = s_layoutPos3Norm3Tex2;
        numElements = 3;
        break;
    case VertexFormatDX::Pos2_Tex2:
        elements = s_layoutPos2Tex2;
        numElements = 2;
        break;
    case VertexFormatDX::Pos2:
        elements = s_layoutPos2;
        numElements = 1;
        break;
    }

    m_device->CreateInputLayout( elements,
                                 numElements,
                                 ShaderGL->GetVSBytecode(),
                                 ShaderGL->GetVSBytecodeSize(),
                                 &m_inputLayout );
    m_lastVSBytecode = vsBytecode;
}


void MeshDX11::Draw() const
{
    EnsureInputLayout();

    auto* backend = RenderBackendDX11::Get();
    auto* ShaderGL = backend->GetActiveShader();
    if ( ShaderGL )
    {
        ShaderGL->FlushCB();
    }

    m_context->IASetInputLayout( m_inputLayout );
    UINT stride = (UINT)m_stride;
    UINT offset = 0;
    m_context->IASetVertexBuffers( 0, 1, &m_vb, &stride, &offset );
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_context->Draw( (UINT)m_vertexCount, 0 );
}


void MeshDX11::DrawInstanced( int instanceCount ) const
{
    EnsureInputLayout();

    auto* backend = RenderBackendDX11::Get();
    auto* ShaderGL = backend->GetActiveShader();
    if ( ShaderGL )
    {
        ShaderGL->FlushCB();
    }

    m_context->IASetInputLayout( m_inputLayout );
    UINT stride = (UINT)m_stride;
    UINT offset = 0;
    m_context->IASetVertexBuffers( 0, 1, &m_vb, &stride, &offset );
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_context->DrawInstanced( (UINT)m_vertexCount, (UINT)instanceCount, 0, 0 );
}
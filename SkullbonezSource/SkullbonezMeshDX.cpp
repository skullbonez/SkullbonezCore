// --- Includes ---
#include "SkullbonezMeshDX.h"
#include "SkullbonezWindow.h"
#include "SkullbonezShader.h"


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Basics;


Mesh::Mesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords, int primitiveMode )
    : m_vertexCount( vertexCount )
{
    m_device = SkullbonezWindow::Instance()->m_d3dDevice;
    m_context = SkullbonezWindow::Instance()->m_d3dContext;

    if ( !m_device || !m_context )
    {
        throw std::runtime_error( "DirectX device not initialized when creating mesh" );
    }

    // Calculate stride
    int floatsPerVertex = 3;
    if ( hasNormals )
    {
        floatsPerVertex += 3;
    }
    if ( hasTexCoords )
    {
        floatsPerVertex += 2;
    }
    m_stride = floatsPerVertex * static_cast<int>( sizeof( float ) );

    // Create vertex buffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = m_vertexCount * m_stride;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    HRESULT hr = m_device->CreateBuffer( &bufferDesc, &initData, &m_vertexBuffer );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create DirectX vertex buffer" );
    }
}


void Mesh::Draw() const
{
    UINT offset = 0;
    m_context->IASetVertexBuffers( 0, 1, m_vertexBuffer.GetAddressOf(), (UINT*)&m_stride, &offset );
    m_context->Draw( m_vertexCount, 0 );
}


int Mesh::GetVertexCount() const
{
    return m_vertexCount;
}

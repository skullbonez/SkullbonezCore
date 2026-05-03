// --- Includes ---
#include "SkullbonezMeshDX12.h"
#include "SkullbonezRenderBackendDX12.h"
#include <stdexcept>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


MeshDX12::MeshDX12()
    : m_vertexBuffer( nullptr ), m_vertexCount( 0 ), m_stride( 0 ), m_format( VertexFormat12::Pos3 )
{
    m_vbView = {};
}


MeshDX12::~MeshDX12()
{
    if ( m_vertexBuffer )
    {
        m_vertexBuffer->Release();
    }
}


void MeshDX12::Create( ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const float* data, int vertexCount, int floatsPerVert, VertexFormat12 format, D3D12_GPU_VIRTUAL_ADDRESS uploadAddr, uint8_t* uploadPtr )
{
    m_vertexCount = vertexCount;
    m_stride = floatsPerVert * (int)sizeof( float );
    m_format = format;

    UINT64 dataSize = (UINT64)vertexCount * m_stride;

    // Create committed vertex buffer on default heap
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = dataSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_vertexBuffer ) );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "MeshDX12: CreateCommittedResource failed" );
    }

    // Copy vertex data to upload buffer
    memcpy( uploadPtr, data, (size_t)dataSize );

    // Record copy from upload buffer to vertex buffer
    auto* backend = RenderBackendDX12::Get();
    UINT64 uploadOffset = uploadAddr - backend->GetUploadBuffer()->GetGPUVirtualAddress();
    cmdList->CopyBufferRegion( m_vertexBuffer, 0, backend->GetUploadBuffer(), uploadOffset, dataSize );

    // Transition to vertex buffer state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_vertexBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier( 1, &barrier );

    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.SizeInBytes = (UINT)dataSize;
    m_vbView.StrideInBytes = (UINT)m_stride;
}


void MeshDX12::Draw() const
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return;
    }
    backend->PrepareDraw( m_format );
    backend->GetCommandList()->IASetVertexBuffers( 0, 1, &m_vbView );
    backend->GetCommandList()->DrawInstanced( (UINT)m_vertexCount, 1, 0, 0 );
}


void MeshDX12::DrawInstanced( int instanceCount ) const
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return;
    }
    backend->PrepareDraw( m_format );
    backend->GetCommandList()->IASetVertexBuffers( 0, 1, &m_vbView );
    backend->GetCommandList()->DrawInstanced( (UINT)m_vertexCount, (UINT)instanceCount, 0, 0 );
}


void MeshDX12::ResetResources()
{
    if ( m_vertexBuffer )
    {
        m_vertexBuffer->Release();
        m_vertexBuffer = nullptr;
    }
}

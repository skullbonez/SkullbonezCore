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

    // Allocate GPU memory and create a committed resource for the vertex buffer on the DEFAULT heap.
    // The Default Heap is fast GPU-only memory (not CPU-accessible). Data must be copied here from
    // an upload buffer. Initial state is COPY_DEST because we'll immediately copy vertex data into it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
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
    // Record a GPU-side copy command: transfer vertex data from the upload buffer (CPU-visible staging
    // memory) to the vertex buffer (fast GPU-only memory). This happens asynchronously on the GPU —
    // the CPU just records the command, the actual copy happens when the command list is executed.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
    cmdList->CopyBufferRegion( m_vertexBuffer, 0, backend->GetUploadBuffer(), uploadOffset, dataSize );

    // Transition the vertex buffer from COPY_DEST to VERTEX_AND_CONSTANT_BUFFER state.
    // In DX12, you MUST explicitly tell the GPU when a resource changes usage. After the copy
    // finishes, the buffer needs to be in the correct state before it can be used for drawing.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
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
    // Bind the vertex buffer to input slot 0 of the Input Assembler (IA) stage.
    // The IA is the very first stage of the GPU pipeline — it reads vertex data and feeds it
    // to the vertex shader. The view tells the GPU where the buffer is, how big it is, and the
    // stride (bytes per vertex).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    backend->GetCommandList()->IASetVertexBuffers( 0, 1, &m_vbView );

    // Issue a non-indexed draw call. DrawInstanced draws all vertices sequentially from the bound
    // vertex buffer. Parameters: (vertexCount, instanceCount=1, startVertex=0, startInstance=0).
    // "Instanced" here means you *could* draw multiple copies, but we pass 1 for a single mesh.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
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
    // Bind vertex buffer and draw multiple instances of this mesh in one call.
    // Instanced drawing is how we efficiently render many copies of the same geometry (e.g. 300 balls)
    // with different per-instance data (position, color, etc.) — all in a single GPU draw call.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    backend->GetCommandList()->IASetVertexBuffers( 0, 1, &m_vbView );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
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

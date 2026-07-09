/*
File: SkullbonezSource/Rendering/DX12/MeshDX12.cpp
Purpose:
  Implements mesh buffers, upload flow, and draw binding for the DX12 renderer.

Mental model:
  MeshDX12.cpp implements mesh buffers, upload flow, and draw binding for the
  DX12 renderer. As an implementation unit, keep edits anchored on DX12
  ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Mesh uploads borrow the current frame upload arena from RenderBackendDX12;
    a missing upload buffer means the backend frame resources were not
    initialized before mesh creation.

Related:
  - SkullbonezSource/Rendering/DX12/MeshDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "MeshDX12.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "RenderBackendDX12.h"
#include <cstring>


using namespace SkullbonezCore::Rendering;


MeshDX12::MeshDX12( RenderBackendDX12& backend )
    : m_backend( backend ), m_vertexBuffer( nullptr ), m_vertexCount( 0 ), m_stride( 0 ),
      m_format( VertexFormat12::Pos3 )
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


bool MeshDX12::Create( ID3D12Device* device,
                       ID3D12GraphicsCommandList* cmdList,
                       const float* data,
                       int vertexCount,
                       int floatsPerVert,
                       VertexFormat12 format,
                       D3D12_GPU_VIRTUAL_ADDRESS uploadAddr,
                       uint8_t* uploadPtr )
{
    m_vertexCount = vertexCount;
    m_stride = floatsPerVert * (int)sizeof( float );
    m_format = format;

    UINT64 dataSize = (UINT64)vertexCount * m_stride;

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

    // Allocate GPU memory for the vertex buffer on the DEFAULT heap.
    // The Default Heap is fast GPU-only memory (not CPU-accessible). Data must be copied here from
    // an upload buffer. D3D12 always creates buffers in COMMON state regardless of what is specified —
    // specifying COPY_DEST fires warning #1328 (CREATERESOURCE_STATE_IGNORED). Use COMMON explicitly;
    // CopyBufferRegion promotes the buffer to COPY_DEST implicitly within the command list.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    HRESULT hr = device->CreateCommittedResource( &defaultHeap,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &bufDesc,
                                                  D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr,
                                                  IID_PPV_ARGS( &m_vertexBuffer ) );
    if ( FAILED( hr ) )
    {
        // Lane R: mesh buffers are backend resources. Factory callers receive
        // a null mesh and skip the dependent draw path while the DX12 gate keeps
        // the HRESULT visible.
        Log().WriteEventf( "dx12_mesh_vertex_buffer_create_failed hresult=0x%08X vertices=%d stride=%d bytes=%llu",
                           static_cast<unsigned int>( hr ),
                           vertexCount,
                           m_stride,
                           static_cast<unsigned long long>( dataSize ) );
        Log().FlushAll();
        return false;
    }
    NameDx12Object( m_vertexBuffer, L"Skullbonez DX12 Mesh Vertex Buffer" );

    memcpy( uploadPtr, data, (size_t)dataSize );

    ID3D12Resource* uploadBuffer = m_backend.GetUploadBuffer();
    // Invariant: ReserveUpload and GetUploadPtr above are only valid when the
    // frame upload system owns a backing resource for the current frame.
    if ( !uploadBuffer )
    {
        SB_FATAL( "MeshDX12", "Create requires a DX12 upload buffer." );
    }
    UINT64 uploadOffset = uploadAddr - uploadBuffer->GetGPUVirtualAddress();
    // Record a GPU-side copy command: transfer vertex data from the upload buffer (CPU-visible staging
    // memory) to the vertex buffer (fast GPU-only memory). This happens asynchronously on the GPU —
    // the CPU just records the command, the actual copy happens when the command list is executed.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
    cmdList->CopyBufferRegion( m_vertexBuffer, 0, uploadBuffer, uploadOffset, dataSize );

    // Transition the vertex buffer from COMMON (implicitly promoted to COPY_DEST by CopyBufferRegion)
    // to a combined read state that covers both normal drawing (VERTEX_AND_CONSTANT_BUFFER) and raytracing
    // acceleration structure builds (NON_PIXEL_SHADER_RESOURCE). Both are read-only states so they
    // can be combined per D3D12 spec.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states
    m_backend.ExecuteGraphTransition( "MeshVertexUploadFinal",
                                      "MeshVertexBuffer",
                                      m_vertexBuffer,
                                      RenderGraphResourceAccess::CopyDest,
                                      RenderGraphResourceAccess::VertexAndNonPixelShaderResource );

    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.SizeInBytes = (UINT)dataSize;
    m_vbView.StrideInBytes = (UINT)m_stride;
    return true;
}


void MeshDX12::Draw() const
{
    ID3D12GraphicsCommandList* commandList = m_backend.GetCommandList();
    if ( !commandList )
    {
        return;
    }
    if ( !m_backend.PrepareDraw( m_format ) )
    {
        return;
    }
    // Bind the vertex buffer to input slot 0 of the Input Assembler (IA) stage.
    // The IA is the very first stage of the GPU pipeline — it reads vertex data and feeds it
    // to the vertex shader. The view tells the GPU where the buffer is, how big it is, and the
    // stride (bytes per vertex).
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    commandList->IASetVertexBuffers( 0, 1, &m_vbView );

    // Issue a non-indexed draw call. DrawInstanced draws all vertices sequentially from the bound
    // vertex buffer. Parameters: (vertexCount, instanceCount=1, startVertex=0, startInstance=0).
    // "Instanced" here means you *could* draw multiple copies, but we pass 1 for a single mesh.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    m_backend.RecordDrawCall( { DrawCallKind::Mesh, "Mesh", m_vertexCount, 1 } );
    commandList->DrawInstanced( (UINT)m_vertexCount, 1, 0, 0 );
}


void MeshDX12::DrawInstanced( int instanceCount ) const
{
    ID3D12GraphicsCommandList* commandList = m_backend.GetCommandList();
    if ( !commandList )
    {
        return;
    }
    if ( !m_backend.PrepareDraw( m_format ) )
    {
        return;
    }
    // Instanced drawing renders many copies of one mesh with different
    // per-instance data in a single GPU draw call.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    commandList->IASetVertexBuffers( 0, 1, &m_vbView );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    m_backend.RecordDrawCall( { DrawCallKind::Mesh, "MeshInstanced", m_vertexCount, instanceCount } );
    commandList->DrawInstanced( (UINT)m_vertexCount, (UINT)instanceCount, 0, 0 );
}


void MeshDX12::ResetResources()
{
    if ( m_vertexBuffer )
    {
        m_vertexBuffer->Release();
        m_vertexBuffer = nullptr;
    }
}

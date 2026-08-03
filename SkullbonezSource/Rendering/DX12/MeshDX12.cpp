/*
File: SkullbonezSource/Rendering/DX12/MeshDX12.cpp
Purpose:
  Implements mesh buffers, upload flow, and draw binding for the DX12 renderer.

Summary:
  Mesh creation copies cold asset data into frame upload storage and publishes a
  default-heap vertex buffer. Draws pass through the frame gate and record their
  evidence through the concrete diagnostics owner before native submission.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Mesh creation receives the current frame upload resource as an operation
    value; a missing upload buffer means the device frame resources were not
    initialized before mesh creation.
  - An upload reservation failure returns before memcpy or GPU command
    recording; address zero is the failure sentinel.
  - Draw diagnostics are recorded only after the draw gate accepts the command.

Related:
  - SkullbonezSource/Rendering/DX12/MeshDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/engine-glossary.md
*/
#include "MeshDX12.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "RenderBackendDX12.h"
#include "../RenderGraph.h"
#include <cstring>


using namespace SkullbonezCore::Rendering;


MeshDX12::MeshDX12( Dx12RenderDevice& device, Dx12DrawGate& drawGate, Dx12Diagnostics& diagnostics )
    : m_device( device ), m_drawGate( drawGate ), m_diagnostics( diagnostics ), m_vertexCount( 0 ), m_stride( 0 ),
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


bool MeshDX12::Create( ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const float* data, int vertexCount,
                       int floatsPerVert, VertexFormat12 format, D3D12_GPU_VIRTUAL_ADDRESS uploadAddr, uint8_t* uploadPtr,
                       ID3D12Resource* uploadBuffer )
{

    if ( uploadAddr == 0 || !uploadPtr )
    {
        return false;
    }

    m_vertexCount = vertexCount;
    m_stride = floatsPerVert * static_cast<int>( sizeof( float ) );
    m_format = format;

    UINT64 dataSize = static_cast<UINT64>( vertexCount ) * m_stride;

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
    HRESULT hr = device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr, IID_PPV_ARGS( &m_vertexBuffer ) );

    if ( FAILED( hr ) )
    {

        // Lane R: mesh buffers are backend resources. Factory callers receive
        // a null mesh and skip the dependent draw path while the DX12 gate keeps
        // the HRESULT visible.
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_mesh_vertex_buffer_create_failed hresult=0x%08X vertices=%d stride=%d bytes=%llu",
                          static_cast<unsigned int>( hr ), vertexCount, m_stride,
                          static_cast<unsigned long long>( dataSize ) );

        SkullbonezCore::Core::Log().FlushAll();
        return false;
    }

    NameDx12Object( m_vertexBuffer, L"Skullbonez DX12 Mesh Vertex Buffer" );

    memcpy( uploadPtr, data, static_cast<size_t>( dataSize ) );

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
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_vertexBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    if ( !m_drawGate.CanRecord() )
    {
        return false;
    }

    cmdList->ResourceBarrier( 1, &barrier );

    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.SizeInBytes = static_cast<UINT>( dataSize );
    m_vbView.StrideInBytes = static_cast<UINT>( m_stride );
    return true;
}


bool MeshDX12::PrecompileRasterState( const PassRasterStateBucket& bucket ) const
{
    return m_drawGate.PrecompilePipelineDraw( m_format, false, nullptr, nullptr, bucket.raster );
}


void MeshDX12::Draw( const PassRasterStateBucket& bucket ) const
{
    ID3D12GraphicsCommandList* commandList = m_device.CommandList();

    if ( !commandList )
    {
        return;
    }

    // Invariant: the pass bucket reaches PSO selection on the same call that
    // submits the mesh. No ambient setter state participates in this draw.

    if ( !m_drawGate.PreparePipelineDraw( m_format, false, nullptr, nullptr, bucket.raster ) )
    {
        return;
    }

    commandList->IASetVertexBuffers( 0, 1, &m_vbView );
    m_diagnostics.RecordDrawCall( { DrawCallKind::Mesh, "MeshDeclaredRaster", m_vertexCount, 1 } );
    commandList->DrawInstanced( static_cast<UINT>( m_vertexCount ), 1, 0, 0 );
}

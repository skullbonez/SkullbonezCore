/*
File: SkullbonezSource/SkullbonezRenderBackendDX12.DynamicGeometry.cpp
Purpose:
  Draws transient DX12 geometry such as debug lines and UI-style batches.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

// --- RenderBackendDX12 DynamicGeometry methods ---


uint32_t RenderBackendDX12::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    DynamicVBDX12 dvb = {};
    dvb.numAttribs = numAttribs;
    dvb.maxVertices = maxVertices;
    int totalFloats = 0;
    for ( int i = 0; i < numAttribs && i < 8; ++i )
    {
        dvb.attribComponents[i] = attribComponents[i];
        totalFloats += attribComponents[i];
    }
    dvb.floatsPerVertex = totalFloats;
    dvb.stride = totalFloats * (int)sizeof( float );
    m_dynamicVBs.push_back( dvb );
    return (uint32_t)m_dynamicVBs.size(); // 1-based
}


void RenderBackendDX12::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() || vertexCount <= 0 )
    {
        return;
    }
    DynamicVBDX12& dvb = m_dynamicVBs[handle - 1];

    EnsureCommandListOpen();

    // ReserveUpload is intentionally used instead of raw SubAllocateUpload().
    // It probes and flushes with the same alignment used for allocation, so a
    // burst of dynamic UI/debug vertices can recover by submitting current work
    // instead of throwing "DX12 upload buffer exhausted."
    UINT64 dataSize = (UINT64)vertexCount * dvb.stride;
    D3D12_GPU_VIRTUAL_ADDRESS vbAddr = ReserveUpload( dataSize, 4 );
    memcpy( GetUploadPtr( vbAddr ), data, (size_t)dataSize );

    // Determine vertex format
    VertexFormat12 fmt = VertexFormat12::Pos2_Tex2;
    if ( dvb.numAttribs == 2 && dvb.attribComponents[0] == 2 && dvb.attribComponents[1] == 2 )
    {
        fmt = VertexFormat12::Pos2_Tex2;
    }

    PrepareDraw( fmt, false, nullptr, &dvb );

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vbAddr;
    vbv.SizeInBytes = (UINT)dataSize;
    vbv.StrideInBytes = (UINT)dvb.stride;
    // Bind and draw the dynamic vertex buffer directly from upload heap memory.
    // Dynamic VBs (for example text quads) change every frame, so they are
    // drawn from upload memory without copying to a default-heap buffer. That
    // is simpler but slightly slower for large batches.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    m_commandList->IASetVertexBuffers( 0, 1, &vbv );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    RecordDrawCall( { DrawCallKind::DynamicVertexBuffer, "DynamicVB", vertexCount, 1 } );
    m_commandList->DrawInstanced( (UINT)vertexCount, 1, 0, 0 );
}


void RenderBackendDX12::DestroyDynamicVB( uint32_t /*handle*/ )
{
    // No GPU resources to release; upload memory is shared by the frame arena.
}


void RenderBackendDX12::DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
{
    if ( vertCount <= 0 )
    {
        return;
    }

    EnsureCommandListOpen();

    // Lazy-init shader and LINE_LIST PSO
    if ( !m_gridLineShader )
    {
        m_gridLineShader = CreateShader( "shaders/grid_line" );
    }
    if ( !m_gridLinePSO )
    {
        ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );

        // Input layout: POSITION (float3) + TEXCOORD0 (float3)
        D3D12_INPUT_ELEMENT_DESC elements[2] = {};
        elements[0].SemanticName = "POSITION";
        elements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        elements[0].AlignedByteOffset = 0;
        elements[1].SemanticName = "TEXCOORD";
        elements[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        elements[1].AlignedByteOffset = 12;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout.pInputElementDescs = elements;
        psoDesc.InputLayout.NumElements = 2;
        psoDesc.pRootSignature = m_rootSignature;
        psoDesc.VS.pShaderBytecode = shader->GetVSBytecode();
        psoDesc.VS.BytecodeLength = shader->GetVSBytecodeSize();
        psoDesc.PS.pShaderBytecode = shader->GetPSBytecode();
        psoDesc.PS.BytecodeLength = shader->GetPSBytecodeSize();
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        m_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_gridLinePSO ) );
        NameDx12Object( m_gridLinePSO, L"Skullbonez DX12 Debug Line PSO" );
    }

    // Upload vertex data to the shared upload buffer. Debug-line vertex data is
    // read as vertex-buffer bytes, so 4-byte alignment is sufficient here; the
    // important part is that the probe and final allocation use the same value.
    UINT64 dataSize = (UINT64)vertCount * 6 * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS vbAddress = ReserveUpload( dataSize, 4 );
    memcpy( GetUploadPtr( vbAddress ), data, (size_t)dataSize );

    // Set pipeline state and draw
    m_commandList->SetPipelineState( m_gridLinePSO );
    m_commandList->SetGraphicsRootSignature( m_rootSignature );
    m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINELIST );

    // Set the viewProj matrix via root constants or CB slot 0
    ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );
    m_activeShader = shader;
    m_psoDirty = true; // Force PSO rebind on next normal draw

    Matrix4 vpMat( viewProjMatrix16 );
    shader->SetMat4( "uViewProj", vpMat );
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr = shader->FlushCB();
    if ( cbAddr )
    {
        m_commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
    }

    // Bind vertex buffer view
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vbAddress;
    vbView.SizeInBytes = (UINT)dataSize;
    vbView.StrideInBytes = 6 * sizeof( float );
    m_commandList->IASetVertexBuffers( 0, 1, &vbView );

    // Bind render targets (depth disabled in PSO)
    m_commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
    m_commandList->RSSetViewports( 1, &m_viewport );
    m_commandList->RSSetScissorRects( 1, &m_scissorRect );

    RecordDrawCall( { DrawCallKind::DebugLines, "DebugLines", vertCount, 1 } );
    m_commandList->DrawInstanced( (UINT)vertCount, 1, 0, 0 );
}


uint32_t RenderBackendDX12::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int /*maxInstances*/, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes, int numStaticAttribs )
{
    EnsureCommandListOpen();

    InstancedMeshDX12 im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.staticStride = staticFloatsPerVert * (int)sizeof( float );
    im.instanceFloats = instanceFloats;
    im.instanceStride = instanceFloats * (int)sizeof( float );
    im.instanceStartAttrib = instanceStartAttrib;
    im.numInstanceAttribs = numInstanceAttribs;
    im.numStaticAttribs = numStaticAttribs;
    for ( int i = 0; i < numInstanceAttribs && i < 8; ++i )
    {
        im.instanceAttribSizes[i] = instanceAttribSizes[i];
    }
    for ( int i = 0; i < numStaticAttribs && i < 8; ++i )
    {
        im.staticAttribSizes[i] = staticAttribSizes[i];
    }

    // Create the static (shared) vertex buffer on the GPU-only default heap.
    // This holds geometry that does not change, such as sphere or box mesh
    // vertices. It is uploaded once and reused across instance batches.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    // Create static VB on default heap
    UINT64 dataSize = (UINT64)staticVertCount * staticFloatsPerVert * sizeof( float );

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

    // Buffers are always created in COMMON state in D3D12 regardless of what is specified here.
    // Specifying COPY_DEST fires warning #1328 (CREATERESOURCE_STATE_IGNORED). Use COMMON
    // explicitly, then rely on implicit promotion to COPY_DEST when CopyBufferRegion executes.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
    ThrowIfFailed( m_device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &im.staticVB ) ),
                   "CreateCommittedResource (instanced static vertex buffer) failed" );
    NameDx12ObjectIndexed( im.staticVB, L"Skullbonez DX12 Instanced Static Vertex Buffer", static_cast<UINT>( m_instancedMeshes.size() + 1 ) );

    // Upload static vertex data from CPU to GPU via the upload buffer, then transition to VB state.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = ReserveUpload( dataSize, 4 );
    memcpy( GetUploadPtr( uploadAddr ), staticData, (size_t)dataSize );
    m_commandList->CopyBufferRegion( im.staticVB, 0, m_uploadSystem.Resource( m_allocatorIndex ), m_uploadSystem.OffsetFromAddress( m_allocatorIndex, uploadAddr ), dataSize );
    // Transition from COPY_DEST (implicit promotion after CopyBufferRegion) to the
    // combined read state used for both vertex fetch and DXR BLAS build SRV access.
    TransitionBarrier( im.staticVB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );

    im.staticVBV.BufferLocation = im.staticVB->GetGPUVirtualAddress();
    im.staticVBV.SizeInBytes = (UINT)dataSize;
    im.staticVBV.StrideInBytes = (UINT)im.staticStride;

    m_instancedMeshes.push_back( im );
    return (uint32_t)m_instancedMeshes.size(); // 1-based
}


void RenderBackendDX12::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || floatCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];

    EnsureCommandListOpen();

    UINT64 dataSize = (UINT64)floatCount * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS addr = ReserveUpload( dataSize, 4 );
    memcpy( GetUploadPtr( addr ), data, (size_t)dataSize );

    im.instanceDataAddr = addr;
    im.instanceDataSize = (UINT)dataSize;
}


void RenderBackendDX12::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || instanceCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];

    if ( im.instanceDataAddr == 0 )
    {
        return; // No instance data uploaded yet
    }

    PrepareDraw( VertexFormat12::Pos3, true, &im, nullptr );

    // Slot 0: static geometry, Slot 1: per-instance data
    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
    vbvs[0] = im.staticVBV;
    vbvs[1].BufferLocation = im.instanceDataAddr;
    vbvs[1].SizeInBytes = im.instanceDataSize;
    vbvs[1].StrideInBytes = (UINT)im.instanceStride;

    // Bind two vertex buffer slots: slot 0 has the shared geometry (sphere mesh), slot 1 has
    // per-instance data (position, color for each ball). The GPU reads slot 0 once per vertex
    // and slot 1 once per instance, combining them in the vertex shader.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    m_commandList->IASetVertexBuffers( 0, 2, vbvs );

    // Draw all instances in one call. This renders staticVertCount vertices
    // multiplied by instanceCount copies.
    // This is the key optimization: 300 balls drawn in a single GPU dispatch.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    RecordDrawCall( { DrawCallKind::InstancedMesh, "InstancedMesh", staticVertCount, instanceCount } );
    m_commandList->DrawInstanced( (UINT)staticVertCount, (UINT)instanceCount, 0, 0 );
}


void RenderBackendDX12::DestroyInstancedMesh( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    auto& im = m_instancedMeshes[handle - 1];
    if ( im.staticVB )
    {
        im.staticVB->Release();
        im.staticVB = nullptr;
    }
}

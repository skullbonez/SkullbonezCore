/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp
Purpose:
  Owns bounded dynamic/instanced geometry registries, warmed overlay pipelines,
  and their DX12 create/upload/draw/destroy lifecycle.

Mental model:
  Dx12GeometryOwner retains geometry handles and warmed overlay resources.
  RenderBackendDX12 establishes a valid command epoch and lends per-operation
  values; the owner records geometry work without retaining the coordinator.

Glossary:
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Dynamic line PSOs are stored in a fixed cache by render-target format; cache
    exhaustion is a renderer capacity invariant, not a recoverable shader/device
    failure.
  - GeometryOwner stores no backend pointer, callback, or polymorphic service;
    every frame dependency is explicit at the operation boundary.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include <cstddef>
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

namespace
{
std::size_t TransientTriangleStyleIndex( TransientTriangleStyle style )
{
    switch ( style )
    {
    case TransientTriangleStyle::TrajectoryRibbonDepthHint:
        return 3;
    case TransientTriangleStyle::TrajectoryRibbon:
        return 2;
    case TransientTriangleStyle::SoftAdditiveRibbon:
        return 1;
    case TransientTriangleStyle::Color:
    default:
        return 0;
    }
}

const char* TransientTriangleShaderBaseName( TransientTriangleStyle style )
{
    switch ( style )
    {
    case TransientTriangleStyle::TrajectoryRibbonDepthHint:
    case TransientTriangleStyle::TrajectoryRibbon:
        return "shaders/trajectory_ribbon";
    case TransientTriangleStyle::SoftAdditiveRibbon:
        return "shaders/soft_additive_ribbon";
    case TransientTriangleStyle::Color:
    default:
        return "shaders/tornado_fx";
    }
}

const char* TransientTriangleTraceLabel( TransientTriangleStyle style )
{
    switch ( style )
    {
    case TransientTriangleStyle::TrajectoryRibbonDepthHint:
        return "TrajectoryRibbonDepthHint";
    case TransientTriangleStyle::TrajectoryRibbon:
        return "TrajectoryRibbon";
    case TransientTriangleStyle::SoftAdditiveRibbon:
        return "SoftAdditiveRibbon";
    case TransientTriangleStyle::Color:
    default:
        return "TornadoVisual";
    }
}

bool IsTrajectoryRibbonStyle( TransientTriangleStyle style )
{
    return style == TransientTriangleStyle::TrajectoryRibbon ||
           style == TransientTriangleStyle::TrajectoryRibbonDepthHint;
}
} // namespace

// --- Dx12GeometryOwner methods ---


void Dx12GeometryOwner::AdoptGridLineShader( std::unique_ptr<IShader> shader )
{
    m_gridLineShader = std::move( shader );
}


bool Dx12GeometryOwner::EnsureGridLinePipeline( ID3D12Device* device,
                                                Dx12PipelineOwner& pipeline,
                                                DXGI_FORMAT rtvFormat )
{
    // Runtime allocation policy: PSO cache misses are legal only during
    // backend/resource warm-up. DrawLinesColored calls this too so an unexpected
    // future RTV format fails visibly under the allocation guard.
    if ( !m_gridLineShader )
    {
        return false;
    }

    for ( size_t i = 0; i < m_gridLinePSOCount; ++i )
    {
        if ( m_gridLinePSOs[i].format == rtvFormat )
        {
            return true;
        }
    }

    ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );

    // Input layout: POSITION (float3) + TEXCOORD0 (float3)
    D3D12_INPUT_ELEMENT_DESC elements[2] = {};
    elements[0].SemanticName = "POSITION";
    elements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elements[0].AlignedByteOffset = 0;
    elements[1].SemanticName = "TEXCOORD";
    elements[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elements[1].AlignedByteOffset = 12;

    std::string inputContractError;
    if ( !shader->ValidateInputLayout( elements, 2, inputContractError ) )
    {
        Log().WriteEventf( "dx12_shader_input_contract_rejected owner=Dx12GeometryOwner reason=%s",
                           inputContractError.c_str() );
        Log().FlushAll();
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout.pInputElementDescs = elements;
    psoDesc.InputLayout.NumElements = 2;
    psoDesc.pRootSignature = pipeline.RootSignature();
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
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* gridLinePSO = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &gridLinePSO ) );
    if ( FAILED( hr ) || !gridLinePSO )
    {
        // Lane R: debug-line rendering is diagnostic overlay work. A failed
        // line PSO should drop this overlay draw and report the device result,
        // not unwind the frame; cache capacity failures below remain fatal.
        Log().WriteEventf( "dx12_debug_line_pso_create_failed hresult=0x%08X rtv_format=%u",
                           static_cast<unsigned int>( FAILED( hr ) ? hr : E_FAIL ),
                           static_cast<unsigned int>( rtvFormat ) );
        Log().FlushAll();
        if ( gridLinePSO )
        {
            gridLinePSO->Release();
        }
        return false;
    }
    NameDx12Object( gridLinePSO, L"Skullbonez DX12 Debug Line PSO" );
    // Invariant: grid-line PSO variants are bounded by the fixed cache in the
    // backend. A new RTV format should be added deliberately with cache budget,
    // not by growing during draw-line submission.
    if ( m_gridLinePSOCount >= m_gridLinePSOs.size() )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 grid-line PSO cache exhausted. capacity=%zu format=%u",
                  m_gridLinePSOs.size(),
                  static_cast<unsigned int>( rtvFormat ) );
    }
    m_gridLinePSOs[m_gridLinePSOCount].format = rtvFormat;
    m_gridLinePSOs[m_gridLinePSOCount].pso = gridLinePSO;
    ++m_gridLinePSOCount;
    return true;
}


uint32_t Dx12GeometryOwner::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    DynamicVBDX12 dvb = {};
    dvb.numAttribs = numAttribs;
    dvb.maxVertices = maxVertices;
    int totalFloats = 0;
    for ( int i = 0; i < numAttribs && i < 12; ++i )
    {
        dvb.attribComponents[i] = attribComponents[i];
        totalFloats += attribComponents[i];
    }
    dvb.floatsPerVertex = totalFloats;
    dvb.stride = totalFloats * (int)sizeof( float );
    m_dynamicVBs.push_back( dvb );
    return (uint32_t)m_dynamicVBs.size(); // 1-based
}


void Dx12GeometryOwner::UploadAndDrawDynamicVB( uint32_t handle,
                                                const float* data,
                                                int vertexCount,
                                                D3D12_GPU_VIRTUAL_ADDRESS vbAddr,
                                                uint8_t* uploadPointer,
                                                ID3D12GraphicsCommandList* commandList,
                                                Dx12DrawGate& drawGate,
                                                DrawCallTrace& drawTrace,
                                                int& drawCount )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() || vertexCount <= 0 )
    {
        return;
    }
    DynamicVBDX12& dvb = m_dynamicVBs[handle - 1];

    // ReserveUpload is intentionally used instead of raw SubAllocateUpload().
    // It probes and flushes with the same alignment used for allocation, so a
    // burst of dynamic UI/debug vertices can recover by submitting current work
    // instead of throwing "DX12 upload buffer exhausted."
    UINT64 dataSize = (UINT64)vertexCount * dvb.stride;
    if ( vbAddr == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, data, (size_t)dataSize );

    // Determine vertex format
    VertexFormat12 fmt = VertexFormat12::Pos2_Tex2;
    if ( dvb.numAttribs == 2 && dvb.attribComponents[0] == 2 && dvb.attribComponents[1] == 2 )
    {
        fmt = VertexFormat12::Pos2_Tex2;
    }

    if ( !drawGate.PreparePipelineDraw( fmt, false, nullptr, &dvb ) )
    {
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vbAddr;
    vbv.SizeInBytes = (UINT)dataSize;
    vbv.StrideInBytes = (UINT)dvb.stride;
    // Dynamic vertex buffers, such as text quads, change every frame and are
    // drawn from upload memory without copying to a default-heap buffer. That
    // is simpler but slightly slower for large batches.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    commandList->IASetVertexBuffers( 0, 1, &vbv );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    ++drawCount;
    drawTrace.RecordDrawCall( { DrawCallKind::DynamicVertexBuffer, "DynamicVB", vertexCount, 1 } );
    commandList->DrawInstanced( (UINT)vertexCount, 1, 0, 0 );
}


void Dx12GeometryOwner::DestroyDynamicVB( uint32_t /*handle*/ )
{
    // No GPU resources to release; upload memory is shared by the frame arena.
}


void Dx12GeometryOwner::DrawLinesColored( const float* data,
                                          int vertCount,
                                          const float* viewProjMatrix16,
                                          D3D12_GPU_VIRTUAL_ADDRESS vbAddress,
                                          uint8_t* uploadPointer,
                                          ID3D12GraphicsCommandList* commandList,
                                          Dx12PipelineOwner& pipeline,
                                          Dx12DrawGate& drawGate,
                                          DrawCallTrace& drawTrace,
                                          int& drawCount )
{
    if ( vertCount <= 0 )
    {
        return;
    }

    ID3D12PipelineState* gridLinePSO = nullptr;
    for ( size_t i = 0; i < m_gridLinePSOCount; ++i )
    {
        if ( m_gridLinePSOs[i].format == pipeline.RenderTargetFormat() )
        {
            gridLinePSO = m_gridLinePSOs[i].pso;
            break;
        }
    }
    if ( !gridLinePSO || !uploadPointer )
    {
        return;
    }

    // Upload vertex data to the shared upload buffer. Debug-line vertex data is
    // read as vertex-buffer bytes, so 4-byte alignment is sufficient here; the
    // important part is that the probe and final allocation use the same value.
    UINT64 dataSize = (UINT64)vertCount * 6 * sizeof( float );
    if ( vbAddress == 0 )
    {
        return;
    }
    memcpy( uploadPointer, data, (size_t)dataSize );

    commandList->SetPipelineState( gridLinePSO );
    commandList->SetGraphicsRootSignature( pipeline.RootSignature() );
    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINELIST );

    // Grid lines use the same constant-buffer slot as ordinary shader constants
    // so the debug path can share the renderer root-signature contract.
    ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );
    pipeline.SetActiveShader( shader );
    pipeline.InvalidateCommandState(); // Force PSO rebind on next normal draw.

    Matrix4 vpMat( viewProjMatrix16 );
    shader->SetMat4( "uViewProj", vpMat );
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr = shader->FlushCB();
    if ( !drawGate.CanRecord() )
    {
        return;
    }
    if ( cbAddr )
    {
        commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
    }

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vbAddress;
    vbView.SizeInBytes = (UINT)dataSize;
    vbView.StrideInBytes = 6 * sizeof( float );
    commandList->IASetVertexBuffers( 0, 1, &vbView );

    // Bind render targets (depth disabled in PSO)
    pipeline.BindCurrentOutputs( commandList );

    ++drawCount;
    drawTrace.RecordDrawCall( { DrawCallKind::DebugLines, "DebugLines", vertCount, 1 } );
    commandList->DrawInstanced( (UINT)vertCount, 1, 0, 0 );
}


void Dx12GeometryOwner::DrawTransientColoredTriangles( const float* data,
                                                       int vertexCount,
                                                       const float* viewProjMatrix16,
                                                       TransientTriangleStyle style,
                                                       int viewportWidth,
                                                       int viewportHeight,
                                                       D3D12_GPU_VIRTUAL_ADDRESS vbAddress,
                                                       uint8_t* uploadPointer,
                                                       ID3D12GraphicsCommandList* commandList,
                                                       Dx12DrawGate& drawGate,
                                                       DrawCallTrace& drawTrace,
                                                       int& drawCount )
{
    if ( vertexCount <= 0 || !data || !viewProjMatrix16 )
    {
        return;
    }

    IShader* transientShader = m_transientTriangleShaders[TransientTriangleStyleIndex( style )].get();
    if ( !transientShader )
    {
        return;
    }
    ShaderDX12* shader = static_cast<ShaderDX12*>( transientShader );
    shader->Use();
    shader->SetMat4( "uViewProj", Matrix4( viewProjMatrix16 ) );
    if ( IsTrajectoryRibbonStyle( style ) )
    {
        // Concept: the trajectory shader expands segment payloads in clip space.
        // The viewport lets it translate pixel width into stable NDC offsets.
        shader->SetVec4( "uViewportPixels",
                         static_cast<float>( viewportWidth ),
                         static_cast<float>( viewportHeight ),
                         0.0f,
                         0.0f );
        const bool depthHint = style == TransientTriangleStyle::TrajectoryRibbonDepthHint;
        shader->SetVec4( "uRibbonStyle",
                         depthHint ? 0.20f : 1.0f,
                         depthHint ? 0.36f : 1.0f,
                         depthHint ? 1.25f : 1.0f,
                         0.0f );
    }

    DynamicVBDX12 vertexLayout = {};
    vertexLayout.numAttribs = IsTrajectoryRibbonStyle( style ) ? 4 : 3;
    vertexLayout.attribComponents[0] = 3;
    vertexLayout.attribComponents[1] = 4;
    vertexLayout.attribComponents[2] = 4;
    if ( IsTrajectoryRibbonStyle( style ) )
    {
        vertexLayout.attribComponents[3] = 2;
        vertexLayout.floatsPerVertex = 13;
    }
    else
    {
        vertexLayout.floatsPerVertex = 11;
    }
    vertexLayout.stride = vertexLayout.floatsPerVertex * static_cast<int>( sizeof( float ) );

    const UINT64 dataSize = static_cast<UINT64>( vertexCount ) * static_cast<UINT64>( vertexLayout.stride );
    if ( vbAddress == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, data, static_cast<size_t>( dataSize ) );

    if ( !drawGate.PreparePipelineDraw( VertexFormat12::Pos3, false, nullptr, &vertexLayout ) )
    {
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vbAddress;
    vbView.SizeInBytes = static_cast<UINT>( dataSize );
    vbView.StrideInBytes = static_cast<UINT>( vertexLayout.stride );
    commandList->IASetVertexBuffers( 0, 1, &vbView );

    ++drawCount;
    drawTrace.RecordDrawCall(
        { DrawCallKind::DynamicVertexBuffer, TransientTriangleTraceLabel( style ), vertexCount, 1 } );
    commandList->DrawInstanced( static_cast<UINT>( vertexCount ), 1, 0, 0 );
}


void Dx12GeometryOwner::AdoptTransientTriangleShader( TransientTriangleStyle style, std::unique_ptr<IShader> shader )
{
    const std::size_t index = TransientTriangleStyleIndex( style );
    m_transientTriangleShaders[index] = std::move( shader );
}


bool Dx12GeometryOwner::HasTransientTriangleShader( TransientTriangleStyle style ) const
{
    return m_transientTriangleShaders[TransientTriangleStyleIndex( style )] != nullptr;
}


UINT Dx12GeometryOwner::GridLineConstantBytes() const
{
    const ShaderDX12* shader = static_cast<const ShaderDX12*>( m_gridLineShader.get() );
    return shader ? shader->ConstantBufferUploadSize() : 0;
}


UINT Dx12GeometryOwner::TransientConstantBytes( TransientTriangleStyle style ) const
{
    const ShaderDX12* shader =
        static_cast<const ShaderDX12*>( m_transientTriangleShaders[TransientTriangleStyleIndex( style )].get() );
    return shader ? shader->ConstantBufferUploadSize() : 0;
}


const char* Dx12GeometryOwner::TransientShaderBaseName( TransientTriangleStyle style )
{
    return TransientTriangleShaderBaseName( style );
}


uint32_t Dx12GeometryOwner::CreateInstancedMesh( const float* staticData,
                                                 int staticVertCount,
                                                 int staticFloatsPerVert,
                                                 int instanceFloats,
                                                 int instanceStartAttrib,
                                                 const int* instanceAttribSizes,
                                                 int numInstanceAttribs,
                                                 const int* staticAttribSizes,
                                                 int numStaticAttribs,
                                                 ID3D12Device* device,
                                                 ID3D12GraphicsCommandList* commandList,
                                                 ID3D12Resource* uploadResource,
                                                 D3D12_GPU_VIRTUAL_ADDRESS uploadAddr,
                                                 uint8_t* uploadPointer )
{
    if ( !device || !commandList || !uploadResource || uploadAddr == 0 || !uploadPointer )
    {
        return 0;
    }

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
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
    const HRESULT staticBufferResult = device->CreateCommittedResource( &defaultHeap,
                                                                        D3D12_HEAP_FLAG_NONE,
                                                                        &bufDesc,
                                                                        D3D12_RESOURCE_STATE_COMMON,
                                                                        nullptr,
                                                                        IID_PPV_ARGS( &im.staticVB ) );
    if ( FAILED( staticBufferResult ) || !im.staticVB )
    {
        // Lane R: instanced mesh handles already use 0 as "no backend mesh".
        // Callers route uploads and draws through that handle, so creation can
        // fail as a logged result without leaving a partially registered mesh.
        Log().WriteEventf( "dx12_instanced_static_vertex_buffer_create_failed hresult=0x%08X vertices=%d stride=%d",
                           static_cast<unsigned int>( FAILED( staticBufferResult ) ? staticBufferResult : E_FAIL ),
                           staticVertCount,
                           im.staticStride );
        Log().FlushAll();
        if ( im.staticVB )
        {
            im.staticVB->Release();
            im.staticVB = nullptr;
        }
        return 0;
    }
    NameDx12ObjectIndexed( im.staticVB,
                           L"Skullbonez DX12 Instanced Static Vertex Buffer",
                           static_cast<UINT>( m_instancedMeshes.size() + 1 ) );

    // Upload static vertex data from CPU to GPU via the upload buffer, then transition to VB state.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
    memcpy( uploadPointer, staticData, (size_t)dataSize );
    commandList->CopyBufferRegion( im.staticVB,
                                   0,
                                   uploadResource,
                                   uploadAddr - uploadResource->GetGPUVirtualAddress(),
                                   dataSize );
    // Transition from COPY_DEST (implicit promotion after CopyBufferRegion) to the
    // combined read state used for both vertex fetch and raytracing BLAS build SRV access.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = im.staticVB;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier( 1, &barrier );

    im.staticVBV.BufferLocation = im.staticVB->GetGPUVirtualAddress();
    im.staticVBV.SizeInBytes = (UINT)dataSize;
    im.staticVBV.StrideInBytes = (UINT)im.staticStride;

    m_instancedMeshes.push_back( im );
    return (uint32_t)m_instancedMeshes.size(); // 1-based
}


void Dx12GeometryOwner::UploadInstanceData( uint32_t handle,
                                            const float* data,
                                            int floatCount,
                                            D3D12_GPU_VIRTUAL_ADDRESS addr,
                                            uint8_t* uploadPointer )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || floatCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];

    UINT64 dataSize = (UINT64)floatCount * sizeof( float );
    if ( addr == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, data, (size_t)dataSize );

    im.instanceDataAddr = addr;
    im.instanceDataSize = (UINT)dataSize;
}


void Dx12GeometryOwner::DrawInstancedMesh( uint32_t handle,
                                           int staticVertCount,
                                           int instanceCount,
                                           ID3D12GraphicsCommandList* commandList,
                                           Dx12DrawGate& drawGate,
                                           DrawCallTrace& drawTrace,
                                           int& drawCount )
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

    if ( !drawGate.PreparePipelineDraw( VertexFormat12::Pos3, true, &im, nullptr ) )
    {
        return;
    }

    // Slot 0: static geometry, Slot 1: per-instance data
    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
    vbvs[0] = im.staticVBV;
    vbvs[1].BufferLocation = im.instanceDataAddr;
    vbvs[1].SizeInBytes = im.instanceDataSize;
    vbvs[1].StrideInBytes = (UINT)im.instanceStride;

    // Bind two vertex buffer slots: slot 0 has the shared geometry (sphere mesh), slot 1 has
    // per-instance data (position, color for each ball). The GPU reads slot 0 once per vertex
    // and slot 1 once per instance, combining them in the vertex shader.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    commandList->IASetVertexBuffers( 0, 2, vbvs );

    // Draw all instances in one call. This renders staticVertCount vertices
    // multiplied by instanceCount copies.
    // This is the key optimization: 300 balls drawn in a single GPU dispatch.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    ++drawCount;
    drawTrace.RecordDrawCall( { DrawCallKind::InstancedMesh, "InstancedMesh", staticVertCount, instanceCount } );
    commandList->DrawInstanced( (UINT)staticVertCount, (UINT)instanceCount, 0, 0 );
}


void Dx12GeometryOwner::DestroyInstancedMesh( uint32_t handle )
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


uint64_t Dx12GeometryOwner::StaticVertexBufferAddress( uint32_t handle ) const
{
    return ( handle > 0 && handle <= m_instancedMeshes.size() && m_instancedMeshes[handle - 1].staticVB )
               ? m_instancedMeshes[handle - 1].staticVB->GetGPUVirtualAddress()
               : 0;
}


int Dx12GeometryOwner::StaticVertexStride( uint32_t handle ) const
{
    return ( handle > 0 && handle <= m_instancedMeshes.size() ) ? m_instancedMeshes[handle - 1].staticStride : 0;
}


size_t Dx12GeometryOwner::DynamicCount() const
{
    return m_dynamicVBs.size();
}
size_t Dx12GeometryOwner::DynamicCapacity() const
{
    return m_dynamicVBs.capacity();
}
UINT64 Dx12GeometryOwner::DynamicUploadBytes( uint32_t handle, int vertexCount ) const
{
    return ( handle > 0 && handle <= m_dynamicVBs.size() && vertexCount > 0 )
               ? static_cast<UINT64>( vertexCount ) * static_cast<UINT64>( m_dynamicVBs[handle - 1].stride )
               : 0;
}
size_t Dx12GeometryOwner::InstancedCount() const
{
    return m_instancedMeshes.size();
}
size_t Dx12GeometryOwner::InstancedCapacity() const
{
    return m_instancedMeshes.capacity();
}


void Dx12GeometryOwner::Shutdown()
{
    for ( GridLinePSODX12& entry : m_gridLinePSOs )
    {
        if ( entry.pso )
        {
            entry.pso->Release();
            entry = {};
        }
    }
    m_gridLinePSOCount = 0;
    m_gridLineShader.reset();
    for ( std::unique_ptr<IShader>& shader : m_transientTriangleShaders )
    {
        shader.reset();
    }
    for ( InstancedMeshDX12& mesh : m_instancedMeshes )
    {
        if ( mesh.staticVB )
        {
            mesh.staticVB->Release();
            mesh.staticVB = nullptr;
        }
    }
    m_instancedMeshes.clear();
    m_dynamicVBs.clear();
}


// --- RenderBackendDX12 geometry facet coordination ---

uint32_t RenderBackendDX12::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    return m_geometryOwner.CreateDynamicVB( attribComponents, numAttribs, maxVertices );
}


void RenderBackendDX12::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
    if ( !data || !m_frameOwner.DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = m_geometryOwner.DynamicUploadBytes( handle, vertexCount );
    const ShaderDX12* shader = m_pipelineOwner.ActiveShader();
    const UINT64 constantBytes = shader ? shader->ConstantBufferUploadSize() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        bytes > 0 ? m_frameOwner.UploadReservations().ReserveGeometryUpload( bytes, constantBytes ) : 0;
    m_geometryOwner.UploadAndDrawDynamicVB( handle,
                                            data,
                                            vertexCount,
                                            address,
                                            address ? GetUploadPtr( address ) : nullptr,
                                            CommandList(),
                                            m_frameOwner.DrawGate(),
                                            m_drawCallTrace,
                                            m_frameDrawCallCount );
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
}


void RenderBackendDX12::DestroyDynamicVB( uint32_t handle )
{
    m_geometryOwner.DestroyDynamicVB( handle );
}


void RenderBackendDX12::DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
{
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
    if ( !data || !viewProjMatrix16 || vertCount <= 0 || !m_frameOwner.DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = static_cast<UINT64>( vertCount ) * 6u * sizeof( float );
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_frameOwner.UploadReservations().ReserveGeometryUpload( bytes, m_geometryOwner.GridLineConstantBytes() );
    m_geometryOwner.DrawLinesColored( data,
                                      vertCount,
                                      viewProjMatrix16,
                                      address,
                                      address ? GetUploadPtr( address ) : nullptr,
                                      CommandList(),
                                      m_pipelineOwner,
                                      m_frameOwner.DrawGate(),
                                      m_drawCallTrace,
                                      m_frameDrawCallCount );
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
}


void RenderBackendDX12::DrawTransientColoredTriangles( const float* data,
                                                       int vertexCount,
                                                       const float* viewProjMatrix16,
                                                       TransientTriangleStyle style )
{
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
    if ( !data || !viewProjMatrix16 || vertexCount <= 0 || !m_frameOwner.DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 floatsPerVertex = IsTrajectoryRibbonStyle( style ) ? 13u : 11u;
    const UINT64 bytes = static_cast<UINT64>( vertexCount ) * floatsPerVertex * sizeof( float );
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_frameOwner.UploadReservations().ReserveGeometryUpload( bytes,
                                                                 m_geometryOwner.TransientConstantBytes( style ) );
    m_geometryOwner.DrawTransientColoredTriangles( data,
                                                   vertexCount,
                                                   viewProjMatrix16,
                                                   style,
                                                   m_width,
                                                   m_height,
                                                   address,
                                                   address ? GetUploadPtr( address ) : nullptr,
                                                   CommandList(),
                                                   m_frameOwner.DrawGate(),
                                                   m_drawCallTrace,
                                                   m_frameDrawCallCount );
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
}


uint32_t RenderBackendDX12::CreateInstancedMesh( const float* staticData,
                                                 int staticVertCount,
                                                 int staticFloatsPerVert,
                                                 int /*maxInstances*/,
                                                 int instanceFloats,
                                                 int instanceStartAttrib,
                                                 const int* instanceAttribSizes,
                                                 int numInstanceAttribs,
                                                 const int* staticAttribSizes,
                                                 int numStaticAttribs )
{
    if ( !EnsureCommandListOpen().ok )
    {
        return 0;
    }
    const UINT64 bytes = static_cast<UINT64>( staticVertCount ) * staticFloatsPerVert * sizeof( float );
    const D3D12_GPU_VIRTUAL_ADDRESS address = ReserveUpload( bytes, 4 );
    return m_geometryOwner.CreateInstancedMesh( staticData,
                                                staticVertCount,
                                                staticFloatsPerVert,
                                                instanceFloats,
                                                instanceStartAttrib,
                                                instanceAttribSizes,
                                                numInstanceAttribs,
                                                staticAttribSizes,
                                                numStaticAttribs,
                                                Device(),
                                                CommandList(),
                                                m_frameOwner.Uploads().Resource( m_frameOwner.AllocatorIndex() ),
                                                address,
                                                address ? GetUploadPtr( address ) : nullptr );
}


void RenderBackendDX12::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
    if ( !data || m_geometryOwner.StaticVertexStride( handle ) <= 0 || floatCount <= 0 ||
         !m_frameOwner.DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = static_cast<UINT64>( floatCount ) * sizeof( float );
    // Invariant: the engine command contract pairs UploadInstanceData with the
    // immediately following DrawInstancedMesh under the same active shader. The
    // coordinator reserves that shader's CB plus instance bytes atomically, so
    // the draw cannot flush between the two published addresses.
    const ShaderDX12* shader = m_pipelineOwner.ActiveShader();
    const UINT64 constantBytes = shader ? shader->ConstantBufferUploadSize() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_frameOwner.UploadReservations().ReserveGeometryUpload( bytes, constantBytes );
    m_geometryOwner.UploadInstanceData( handle,
                                        data,
                                        floatCount,
                                        address,
                                        address ? GetUploadPtr( address ) : nullptr );
}


void RenderBackendDX12::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( !m_frameOwner.DrawGate().PrepareDraw() )
    {
        m_frameOwner.UploadReservations().CancelPendingConstantUpload();
        return;
    }
    m_geometryOwner.DrawInstancedMesh( handle,
                                       staticVertCount,
                                       instanceCount,
                                       CommandList(),
                                       m_frameOwner.DrawGate(),
                                       m_drawCallTrace,
                                       m_frameDrawCallCount );
    m_frameOwner.UploadReservations().CancelPendingConstantUpload();
}


void RenderBackendDX12::DestroyInstancedMesh( uint32_t handle )
{
    m_geometryOwner.DestroyInstancedMesh( handle );
}

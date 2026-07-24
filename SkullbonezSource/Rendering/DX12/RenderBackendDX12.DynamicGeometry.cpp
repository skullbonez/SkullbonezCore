/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp
Purpose:
  Owns bounded dynamic/instanced geometry registries, warmed overlay pipelines,
  and their DX12 create/upload/draw/destroy lifecycle.

Summary:
  Dx12GeometryOwner retains geometry handles and warmed overlay resources.
  Backend startup binds stable device, frame, pipeline, and diagnostics owners;
  later draws carry only operation values. Geometry records draw evidence
  without retaining the aggregate backend or raw diagnostic fields.

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
  - Dynamic, transient, and instanced draws pass their raster recipe directly
    into PSO preparation; geometry owners retain no ambient raster state.
  - GeometryOwner stores no backend pointer, callback, or polymorphic service;
    its startup-bound owner borrows remain valid for the device lifetime.
  - One accepted native geometry draw records one row through Dx12Diagnostics.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
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
    SkullbonezCore::Core::Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u",
                                             name,
                                             nextIndex,
                                             capacity );
    SkullbonezCore::Core::Log().FlushAll();
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
        return "shaders/transient_colored_triangles";
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
        return "TransientColoredTriangles";
    }
}

bool IsTrajectoryRibbonStyle( TransientTriangleStyle style )
{
    return style == TransientTriangleStyle::TrajectoryRibbon ||
           style == TransientTriangleStyle::TrajectoryRibbonDepthHint;
}

bool IsGridLineRasterState( const RasterStateDesc& raster )
{
    return !raster.depthTest && !raster.depthWrite && !raster.blendEnabled && raster.cullMode == CullMode::None &&
           !raster.depthBias.enabled;
}
} // namespace

// --- Dx12GeometryOwner methods ---


void Dx12GeometryOwner::AdoptGridLineShader( std::unique_ptr<ShaderDX12> shader )
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

    const char* inputContractError = nullptr;
    if ( !shader->ValidateInputLayout( elements, 2, inputContractError ) )
    {
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_shader_input_contract_rejected owner=Dx12GeometryOwner reason=%s",
            inputContractError );
        SkullbonezCore::Core::Log().FlushAll();
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
        SkullbonezCore::Core::Log().WriteEventf( "dx12_debug_line_pso_create_failed hresult=0x%08X rtv_format=%u",
                                                 static_cast<unsigned int>( FAILED( hr ) ? hr : E_FAIL ),
                                                 static_cast<unsigned int>( rtvFormat ) );
        SkullbonezCore::Core::Log().FlushAll();
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


Dx12GeometryOwner::Dx12GeometryOwner()
{
    // Runtime allocation policy: text, overlays, primitive batches, and tools
    // acquire stable one-based handles from this bounded registry. Reserve the
    // complete handle budget before any first-use render path can request one.
    m_dynamicVBs.reserve( MAX_DYNAMIC_VERTEX_BUFFERS );
}


uint32_t Dx12GeometryOwner::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    if ( m_dynamicVBs.size() >= MAX_DYNAMIC_VERTEX_BUFFERS )
    {
        SB_FATAL( "Rendering/Dx12GeometryOwner",
                  "Dynamic vertex-buffer handle capacity exceeded. requested=%zu hard_capacity=%zu",
                  m_dynamicVBs.size() + 1u,
                  MAX_DYNAMIC_VERTEX_BUFFERS );
    }
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
                                                std::span<const float> packedVertices,
                                                D3D12_GPU_VIRTUAL_ADDRESS vbAddr,
                                                uint8_t* uploadPointer,
                                                ID3D12GraphicsCommandList* commandList,
                                                Dx12DrawGate& drawGate,
                                                Dx12Diagnostics& diagnostics,
                                                const RasterStateDesc& rasterState )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() || packedVertices.empty() )
    {
        return;
    }
    DynamicVBDX12& dvb = m_dynamicVBs[handle - 1];
    if ( dvb.floatsPerVertex <= 0 || packedVertices.size() % static_cast<size_t>( dvb.floatsPerVertex ) != 0 )
    {
        return;
    }
    const int vertexCount = static_cast<int>( packedVertices.size() / dvb.floatsPerVertex );

    // The phase-aware reservation is intentionally used instead of raw arena
    // allocation. A steady UI/debug burst rejects this draw without submitting
    // and waiting in the middle of the frame.
    const UINT64 dataSize = static_cast<UINT64>( packedVertices.size_bytes() );
    if ( vbAddr == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, packedVertices.data(), (size_t)dataSize );

    // Determine vertex format
    VertexFormat12 fmt = VertexFormat12::Pos2_Tex2;
    if ( dvb.numAttribs == 2 && dvb.attribComponents[0] == 2 && dvb.attribComponents[1] == 2 )
    {
        fmt = VertexFormat12::Pos2_Tex2;
    }

    if ( !drawGate.PreparePipelineDraw( fmt, false, nullptr, &dvb, rasterState ) )
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
    diagnostics.RecordDrawCall( { DrawCallKind::DynamicVertexBuffer, "DynamicVB", vertexCount, 1 } );
    commandList->DrawInstanced( (UINT)vertexCount, 1, 0, 0 );
}


bool Dx12GeometryOwner::PrecompileDynamicVBRasterState( uint32_t handle,
                                                        Dx12DrawGate& drawGate,
                                                        const RasterStateDesc& declaredRasterState )
{
    if ( handle == 0 || handle > static_cast<uint32_t>( m_dynamicVBs.size() ) )
    {
        return false;
    }
    const DynamicVBDX12& dynamicVertexBuffer = m_dynamicVBs[handle - 1];
    return drawGate.PrecompilePipelineDraw( VertexFormat12::Pos2_Tex2,
                                            false,
                                            nullptr,
                                            &dynamicVertexBuffer,
                                            declaredRasterState );
}


void Dx12GeometryOwner::DestroyDynamicVB( uint32_t /*handle*/ )
{
    // No GPU resources to release; upload memory is shared by the frame arena.
}


void Dx12GeometryOwner::DrawLinesColored( std::span<const float> packedVertices,
                                          const Math::Transformation::Matrix4& viewProjection,
                                          D3D12_GPU_VIRTUAL_ADDRESS vbAddress,
                                          uint8_t* uploadPointer,
                                          ID3D12GraphicsCommandList* commandList,
                                          Dx12PipelineOwner& pipeline,
                                          Dx12DrawGate& drawGate,
                                          Dx12Diagnostics& diagnostics,
                                          const RasterStateDesc& rasterState )
{
    // Invariant: the specialized line-topology PSO is immutable. Declared
    // callers must select its depth-disabled, unblended, two-sided recipe.
    if ( packedVertices.empty() || packedVertices.size() % 6 != 0 || !IsGridLineRasterState( rasterState ) )
    {
        return;
    }
    if ( vbAddress == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, packedVertices.data(), packedVertices.size_bytes() );
    DrawLinesColoredFromBuffer( packedVertices.size(),
                                viewProjection,
                                vbAddress,
                                commandList,
                                pipeline,
                                drawGate,
                                diagnostics,
                                rasterState );
}


void Dx12GeometryOwner::DrawLinesColoredFromBuffer( std::size_t packedFloatCount,
                                                    const Math::Transformation::Matrix4& viewProjection,
                                                    D3D12_GPU_VIRTUAL_ADDRESS vertexAddress,
                                                    ID3D12GraphicsCommandList* commandList,
                                                    Dx12PipelineOwner& pipeline,
                                                    Dx12DrawGate& drawGate,
                                                    Dx12Diagnostics& diagnostics,
                                                    const RasterStateDesc& rasterState )
{
    if ( packedFloatCount == 0u || packedFloatCount % 6u != 0u || vertexAddress == 0 ||
         !IsGridLineRasterState( rasterState ) )
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
    if ( !gridLinePSO )
    {
        return;
    }

    // Upload vertex data to the shared upload buffer. Debug-line vertex data is
    // read as vertex-buffer bytes, so 4-byte alignment is sufficient here; the
    // important part is that the probe and final allocation use the same value.
    const int vertCount = static_cast<int>( packedFloatCount / 6u );
    const UINT64 dataSize = static_cast<UINT64>( packedFloatCount * sizeof( float ) );

    commandList->SetPipelineState( gridLinePSO );
    commandList->SetGraphicsRootSignature( pipeline.RootSignature() );
    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINELIST );

    // Grid lines use the same constant-buffer slot as ordinary shader constants
    // so the debug path can share the renderer root-signature contract.
    ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );
    pipeline.SetActiveShader( shader );
    pipeline.InvalidateCommandState(); // Force PSO rebind on next normal draw.

    shader->SetMat4( "uViewProj", viewProjection );
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr = shader->FlushCB();
    if ( !drawGate.CanRecord() )
    {
        return;
    }
    if ( shader->ConstantBufferUploadSize() > 0 && cbAddr == 0 )
    {
        return;
    }
    if ( cbAddr )
    {
        commandList->SetGraphicsRootConstantBufferView( UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS,
                                                        cbAddr );
    }

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vertexAddress;
    vbView.SizeInBytes = (UINT)dataSize;
    vbView.StrideInBytes = 6 * sizeof( float );
    commandList->IASetVertexBuffers( 0, 1, &vbView );

    // Bind render targets (depth disabled in PSO)
    pipeline.BindCurrentOutputs( commandList );

    diagnostics.RecordDrawCall( { DrawCallKind::DebugLines, "DebugLines", vertCount, 1 } );
    commandList->DrawInstanced( (UINT)vertCount, 1, 0, 0 );
}


void Dx12GeometryOwner::DrawTransientColoredTriangles( std::span<const float> packedVertices,
                                                       const Math::Transformation::Matrix4& viewProjection,
                                                       TransientTriangleStyle style,
                                                       int viewportWidth,
                                                       int viewportHeight,
                                                       D3D12_GPU_VIRTUAL_ADDRESS vbAddress,
                                                       uint8_t* uploadPointer,
                                                       ID3D12GraphicsCommandList* commandList,
                                                       Dx12DrawGate& drawGate,
                                                       Dx12Diagnostics& diagnostics,
                                                       const RasterStateDesc& rasterState )
{
    if ( packedVertices.empty() || vbAddress == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, packedVertices.data(), packedVertices.size_bytes() );
    DrawColoredTrianglesFromBuffer( packedVertices.size(),
                                    viewProjection,
                                    style,
                                    viewportWidth,
                                    viewportHeight,
                                    false,
                                    0u,
                                    vbAddress,
                                    commandList,
                                    drawGate,
                                    diagnostics,
                                    rasterState );
}


void Dx12GeometryOwner::DrawColoredTrianglesFromBuffer( std::size_t packedFloatCount,
                                                        const Math::Transformation::Matrix4& viewProjection,
                                                        TransientTriangleStyle style,
                                                        int viewportWidth,
                                                        int viewportHeight,
                                                        bool compactTrajectoryInstances,
                                                        UINT startInstance,
                                                        D3D12_GPU_VIRTUAL_ADDRESS vbAddress,
                                                        ID3D12GraphicsCommandList* commandList,
                                                        Dx12DrawGate& drawGate,
                                                        Dx12Diagnostics& diagnostics,
                                                        const RasterStateDesc& rasterState )
{
    if ( packedFloatCount == 0u || vbAddress == 0 )
    {
        return;
    }
    ShaderDX12* transientShader = m_transientTriangleShaders[TransientTriangleStyleIndex( style )].get();
    if ( !transientShader )
    {
        return;
    }
    ShaderDX12* shader = static_cast<ShaderDX12*>( transientShader );
    shader->Use();
    shader->SetMat4( "uViewProj", viewProjection );
    if ( IsTrajectoryRibbonStyle( style ) )
    {
        // Concept: the trajectory shader expands each compact segment into a
        // screen-space vector spline. The viewport converts the authored full
        // width and analytic anti-aliasing overhang from pixels to
        // normalized-device-coordinate offsets.
        shader->SetVec4( "uViewportPixels",
                         static_cast<float>( viewportWidth ),
                         static_cast<float>( viewportHeight ),
                         0.0f,
                         0.0f );
        const bool depthHint = style == TransientTriangleStyle::TrajectoryRibbonDepthHint;
        shader->SetVec4( "uRibbonStyle", depthHint ? 0.16f : 1.0f, depthHint ? 0.70f : 1.0f, 1.0f, 0.0f );
    }

    DynamicVBDX12 vertexLayout = {};
    vertexLayout.numAttribs = IsTrajectoryRibbonStyle( style ) ? 6 : 3;
    vertexLayout.attribComponents[0] = 3;
    vertexLayout.attribComponents[1] = 4;
    vertexLayout.attribComponents[2] = 4;
    if ( IsTrajectoryRibbonStyle( style ) )
    {
        vertexLayout.attribComponents[3] = 2;
        vertexLayout.attribComponents[4] = 3;
        vertexLayout.attribComponents[5] = 3;
        vertexLayout.floatsPerVertex = 19;
    }
    else
    {
        vertexLayout.floatsPerVertex = 11;
    }
    vertexLayout.stride = vertexLayout.floatsPerVertex * static_cast<int>( sizeof( float ) );
    vertexLayout.perInstance = compactTrajectoryInstances;
    if ( packedFloatCount % static_cast<size_t>( vertexLayout.floatsPerVertex ) != 0 )
    {
        return;
    }

    const int recordCount = static_cast<int>( packedFloatCount / vertexLayout.floatsPerVertex );
    const int vertexCount = compactTrajectoryInstances ? 6 : recordCount;
    const int instanceCount = compactTrajectoryInstances ? recordCount : 1;
    const UINT64 dataSize = static_cast<UINT64>( packedFloatCount * sizeof( float ) );

    if ( !drawGate.PreparePipelineDraw( VertexFormat12::Pos3, false, nullptr, &vertexLayout, rasterState ) )
    {
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vbAddress;
    vbView.SizeInBytes = static_cast<UINT>( dataSize );
    vbView.StrideInBytes = static_cast<UINT>( vertexLayout.stride );
    commandList->IASetVertexBuffers( 0, 1, &vbView );

    diagnostics.RecordDrawCall(
        { DrawCallKind::DynamicVertexBuffer, TransientTriangleTraceLabel( style ), vertexCount, instanceCount } );
    commandList->DrawInstanced( static_cast<UINT>( vertexCount ),
                                static_cast<UINT>( instanceCount ),
                                0,
                                startInstance );
}


void Dx12GeometryOwner::AdoptTransientTriangleShader( TransientTriangleStyle style, std::unique_ptr<ShaderDX12> shader )
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


uint32_t Dx12GeometryOwner::CreateInstancedMesh( const float* staticVertices,
                                                 int staticVertexCount,
                                                 int staticFloatsPerVertex,
                                                 int instanceFloats,
                                                 int instanceStartAttribute,
                                                 std::span<const int> instanceAttributeSizes,
                                                 std::span<const int> staticAttributeSizes,
                                                 ID3D12Device* device,
                                                 ID3D12GraphicsCommandList* commandList,
                                                 ID3D12Resource* uploadResource,
                                                 D3D12_GPU_VIRTUAL_ADDRESS uploadAddress,
                                                 uint8_t* uploadPointer )
{
    if ( !device || !commandList || !uploadResource || uploadAddress == 0 || !uploadPointer )
    {
        return 0;
    }

    InstancedMeshDX12 im = {};
    im.staticFloatsPerVert = staticFloatsPerVertex;
    im.staticStride = staticFloatsPerVertex * (int)sizeof( float );
    im.instanceFloats = instanceFloats;
    im.instanceStride = instanceFloats * (int)sizeof( float );
    im.instanceStartAttrib = instanceStartAttribute;
    im.numInstanceAttribs = static_cast<int>( instanceAttributeSizes.size() );
    im.numStaticAttribs = static_cast<int>( staticAttributeSizes.size() );
    for ( int i = 0; i < im.numInstanceAttribs && i < 8; ++i )
    {
        im.instanceAttribSizes[i] = instanceAttributeSizes[static_cast<std::size_t>( i )];
    }
    for ( int i = 0; i < im.numStaticAttribs && i < 8; ++i )
    {
        im.staticAttribSizes[i] = staticAttributeSizes[static_cast<std::size_t>( i )];
    }

    // Create the static (shared) vertex buffer on the GPU-only default heap.
    // This holds geometry that does not change, such as sphere or box mesh
    // vertices. It is uploaded once and reused across instance batches.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    UINT64 dataSize = (UINT64)staticVertexCount * staticFloatsPerVertex * sizeof( float );

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
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_instanced_static_vertex_buffer_create_failed hresult=0x%08X vertices=%d stride=%d",
            static_cast<unsigned int>( FAILED( staticBufferResult ) ? staticBufferResult : E_FAIL ),
            staticVertexCount,
            im.staticStride );
        SkullbonezCore::Core::Log().FlushAll();
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
    memcpy( uploadPointer, staticVertices, (size_t)dataSize );
    commandList->CopyBufferRegion( im.staticVB,
                                   0,
                                   uploadResource,
                                   uploadAddress - uploadResource->GetGPUVirtualAddress(),
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
                                            std::span<const float> packedInstances,
                                            D3D12_GPU_VIRTUAL_ADDRESS addr,
                                            uint8_t* uploadPointer )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || packedInstances.empty() )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];
    // Hazard: a rejected steady-frame reservation must invalidate the prior
    // frame's upload address before DrawInstancedMesh can observe it.
    im.instanceDataAddr = 0;
    im.instanceDataSize = 0;

    const UINT64 dataSize = static_cast<UINT64>( packedInstances.size_bytes() );
    if ( addr == 0 || !uploadPointer )
    {
        return;
    }
    memcpy( uploadPointer, packedInstances.data(), (size_t)dataSize );

    im.instanceDataAddr = addr;
    im.instanceDataSize = (UINT)dataSize;
}


void Dx12GeometryOwner::DrawInstancedMesh( const InstancedMeshDrawDesc& draw,
                                           ID3D12GraphicsCommandList* commandList,
                                           Dx12DrawGate& drawGate,
                                           Dx12Diagnostics& diagnostics )
{
    if ( draw.handle == 0 || draw.handle > (uint32_t)m_instancedMeshes.size() || draw.instanceCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[draw.handle - 1];

    if ( im.instanceDataAddr == 0 )
    {
        return; // No instance data uploaded yet
    }

    if ( !drawGate.PreparePipelineDraw( VertexFormat12::Pos3, true, &im, nullptr, draw.rasterState.raster ) )
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
    diagnostics.RecordDrawCall(
        { DrawCallKind::InstancedMesh, "InstancedMesh", draw.staticVertexCount, draw.instanceCount } );
    commandList->DrawInstanced( (UINT)draw.staticVertexCount, (UINT)draw.instanceCount, 0, 0 );
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
UINT64 Dx12GeometryOwner::DynamicUploadBytes( uint32_t handle, std::span<const float> packedVertices ) const
{
    if ( handle == 0 || handle > m_dynamicVBs.size() || packedVertices.empty() )
    {
        return 0;
    }
    const int floatsPerVertex = m_dynamicVBs[handle - 1].floatsPerVertex;
    return floatsPerVertex > 0 && packedVertices.size() % static_cast<size_t>( floatsPerVertex ) == 0
               ? static_cast<UINT64>( packedVertices.size_bytes() )
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


void Dx12GeometryOwner::InvalidateGridLinePipelinesForShaderReload()
{
    // Lifetime: the caller has drained the GPU. Grid-line PSOs bypass the main
    // raster cache, so they must be released explicitly before the registered
    // grid-line ShaderDX12 adopts new bytecode.
    for ( GridLinePSODX12& entry : m_gridLinePSOs )
    {
        if ( entry.pso )
        {
            entry.pso->Release();
        }
        entry = {};
    }
    m_gridLinePSOCount = 0;
}


void Dx12GeometryOwner::Shutdown()
{
    InvalidateGridLinePipelinesForShaderReload();
    m_gridLineShader.reset();
    for ( std::unique_ptr<ShaderDX12>& shader : m_transientTriangleShaders )
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
    m_retainedTrajectoryBuffers = {};
    m_retainedTrajectoryCommandSignature.Reset();
}


// --- RenderBackendDX12 draw coordination ---


bool Dx12GeometryOwner::PrecompileDynamicVBRasterState( uint32_t handle, const PassRasterStateBucket& bucket )
{
    assert( m_resourceFrame );
    return PrecompileDynamicVBRasterState( handle, m_resourceFrame->DrawGate(), bucket.raster );
}


void Dx12GeometryOwner::UploadAndDrawDynamicVB( uint32_t handle,
                                                std::span<const float> packedVertices,
                                                const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionPipeline && m_submissionDiagnostics );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
    if ( packedVertices.empty() || !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = DynamicUploadBytes( handle, packedVertices );
    const ShaderDX12* shader = m_submissionPipeline->ActiveShader();
    const UINT64 constantBytes = shader ? shader->ConstantBufferUploadSize() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        bytes > 0 ? m_resourceFrame->UploadReservations().ReserveGeometryUpload( bytes,
                                                                                 constantBytes,
                                                                                 RenderUploadCategory::DynamicVertex )
                  : 0;
    UploadAndDrawDynamicVB( handle,
                            packedVertices,
                            address,
                            address ? m_resourceFrame->UploadReservations().UploadPointer( address ) : nullptr,
                            m_resourceDevice->CommandList(),
                            m_resourceFrame->DrawGate(),
                            *m_submissionDiagnostics,
                            bucket.raster );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
}


void Dx12GeometryOwner::DrawLinesColored( std::span<const float> packedVertices,
                                          const Math::Transformation::Matrix4& viewProjection,
                                          const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionPipeline && m_submissionDiagnostics );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
    if ( packedVertices.empty() || packedVertices.size() % 6 != 0 || !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = static_cast<UINT64>( packedVertices.size_bytes() );
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_resourceFrame->UploadReservations().ReserveGeometryUpload( bytes,
                                                                     GridLineConstantBytes(),
                                                                     RenderUploadCategory::DebugPredictionOverlay );
    DrawLinesColored( packedVertices,
                      viewProjection,
                      address,
                      address ? m_resourceFrame->UploadReservations().UploadPointer( address ) : nullptr,
                      m_resourceDevice->CommandList(),
                      *m_submissionPipeline,
                      m_resourceFrame->DrawGate(),
                      *m_submissionDiagnostics,
                      bucket.raster );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
}


void Dx12GeometryOwner::DrawTransientColoredTriangles( std::span<const float> packedVertices,
                                                       const Math::Transformation::Matrix4& viewProjection,
                                                       TransientTriangleStyle style,
                                                       const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionDiagnostics );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
    const UINT64 floatsPerVertex = IsTrajectoryRibbonStyle( style ) ? 19u : 11u;
    if ( packedVertices.empty() || packedVertices.size() % floatsPerVertex != 0 ||
         !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = static_cast<UINT64>( packedVertices.size_bytes() );
    const RenderUploadCategory category = IsTrajectoryRibbonStyle( style )
                                              ? RenderUploadCategory::DebugPredictionOverlay
                                              : RenderUploadCategory::DynamicVertex;
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_resourceFrame->UploadReservations().ReserveGeometryUpload( bytes, TransientConstantBytes( style ), category );
    DrawTransientColoredTriangles( packedVertices,
                                   viewProjection,
                                   style,
                                   m_resourceDevice->Width(),
                                   m_resourceDevice->Height(),
                                   address,
                                   address ? m_resourceFrame->UploadReservations().UploadPointer( address ) : nullptr,
                                   m_resourceDevice->CommandList(),
                                   m_resourceFrame->DrawGate(),
                                   *m_submissionDiagnostics,
                                   bucket.raster );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
}


void Dx12GeometryOwner::DrawRetainedTrajectoryRibbon( std::span<const float> packedVertices,
                                                      uint64_t streamId,
                                                      uint64_t revision,
                                                      bool priorityLane,
                                                      const Math::Transformation::Matrix4& viewProjection,
                                                      TransientTriangleStyle style,
                                                      const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionDiagnostics );
    const std::size_t laneIndex = priorityLane ? 1u : 0u;
    constexpr std::size_t floatsPerRecord = 19u;
    constexpr std::size_t verticesPerSegment = 6u;
    constexpr std::size_t floatsPerExpandedSegment = verticesPerSegment * floatsPerRecord;
    const std::size_t laneCapacity =
        priorityLane ? RETAINED_TRAJECTORY_PRIORITY_FLOAT_CAPACITY : RETAINED_TRAJECTORY_ORDINARY_FLOAT_CAPACITY;
    const std::size_t laneOffset = priorityLane ? RETAINED_TRAJECTORY_ORDINARY_FLOAT_CAPACITY : 0u;
    const std::size_t segmentCount = packedVertices.size() / floatsPerExpandedSegment;
    if ( packedVertices.empty() || packedVertices.size() % floatsPerExpandedSegment != 0u ||
         segmentCount * floatsPerRecord > laneCapacity || !IsTrajectoryRibbonStyle( style ) ||
         !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }

    const UINT frameIndex = m_resourceFrame->FrameIndex();
    RetainedTrajectoryBufferDX12& buffer = m_retainedTrajectoryBuffers[frameIndex];
    uint8_t* retainedBytes = m_resourceFrame->Uploads().PersistentTailPointer( frameIndex );
    const D3D12_GPU_VIRTUAL_ADDRESS retainedAddress = m_resourceFrame->Uploads().PersistentTailAddress( frameIndex );
    const RetainedTrajectoryUploadPlanDX12 uploadPlan =
        BuildRetainedTrajectoryUploadPlanDX12( buffer.streamIds[laneIndex],
                                               buffer.revisions[laneIndex],
                                               buffer.uploadedFloatCounts[laneIndex],
                                               streamId,
                                               revision,
                                               segmentCount,
                                               true );
    if ( uploadPlan.uploadRequired )
    {
        // The expanded CPU cache repeats each segment payload six times only
        // for the transient submission contract. Retained GPU storage keeps one
        // per-instance record and patches only the formerly open adjacency tail
        // plus the appended suffix.
        for ( std::size_t segment = uploadPlan.firstChangedUnit; segment < segmentCount; ++segment )
        {
            memcpy( retainedBytes + ( laneOffset + segment * floatsPerRecord ) * sizeof( float ),
                    packedVertices.data() + segment * floatsPerExpandedSegment,
                    floatsPerRecord * sizeof( float ) );
        }
        buffer.streamIds[laneIndex] = streamId;
        buffer.revisions[laneIndex] = revision;
        buffer.uploadedFloatCounts[laneIndex] = segmentCount;
    }

    DrawColoredTrianglesFromBuffer( segmentCount * floatsPerRecord,
                                    viewProjection,
                                    style,
                                    m_resourceDevice->Width(),
                                    m_resourceDevice->Height(),
                                    true,
                                    0u,
                                    retainedAddress + laneOffset * sizeof( float ),
                                    m_resourceDevice->CommandList(),
                                    m_resourceFrame->DrawGate(),
                                    *m_submissionDiagnostics,
                                    bucket.raster );
}


void Dx12GeometryOwner::DrawRetainedTrajectoryRanges( std::span<const float> compactRecords,
                                                      std::span<const RetainedTrajectoryDrawRange> ranges,
                                                      uint64_t streamId,
                                                      uint64_t revision,
                                                      const Math::Transformation::Matrix4& viewProjection,
                                                      TransientTriangleStyle style,
                                                      const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionDiagnostics );
    if ( ranges.empty() || ranges.size() > RETAINED_TRAJECTORY_MAX_DRAW_RANGES ||
         compactRecords.size() <
             ( RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY + RETAINED_TRAJECTORY_PRIORITY_SEGMENT_CAPACITY ) *
                 RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT ||
         !IsTrajectoryRibbonStyle( style ) || !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }

    const UINT frameIndex = m_resourceFrame->FrameIndex();
    RetainedTrajectoryBufferDX12& buffer = m_retainedTrajectoryBuffers[frameIndex];
    uint8_t* retainedBytes = m_resourceFrame->Uploads().PersistentTailPointer( frameIndex );
    const D3D12_GPU_VIRTUAL_ADDRESS retainedAddress = m_resourceFrame->Uploads().PersistentTailAddress( frameIndex );
    constexpr std::size_t compactFloatOffset = RETAINED_TRAJECTORY_FLOAT_CAPACITY;
    constexpr std::size_t indirectByteOffset =
        ( RETAINED_TRAJECTORY_FLOAT_CAPACITY + RETAINED_TRAJECTORY_COMPACT_FLOAT_CAPACITY ) * sizeof( float );
    auto* indirectArguments = reinterpret_cast<D3D12_DRAW_ARGUMENTS*>( retainedBytes + indirectByteOffset );
    const D3D12_GPU_VIRTUAL_ADDRESS indirectAddress = retainedAddress + indirectByteOffset;
    const bool streamChanged = buffer.rangeStreamId != streamId;
    const bool revisionChanged = streamChanged || buffer.rangeRevision != revision;
    if ( streamChanged )
    {
        buffer.rangeIdentities.fill( 0 );
        buffer.rangeSourceVersions.fill( 0 );
        buffer.rangeSegmentCounts.fill( 0 );
    }

    if ( revisionChanged )
    {
        uint32_t totalSegmentCount = 0;
        for ( std::size_t rangeIndex = 0; rangeIndex < ranges.size(); ++rangeIndex )
        {
            const RetainedTrajectoryDrawRange& range = ranges[rangeIndex];
            const std::size_t cacheIndex = range.cacheSlot;
            const std::size_t firstSegment = range.firstSegment;
            const std::size_t segmentCapacity = range.segmentCapacity;
            const std::size_t segmentCount = range.segmentCount;
            const std::size_t laneBegin = range.priority ? RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY : 0u;
            const std::size_t laneEnd = range.priority ? RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY +
                                                             RETAINED_TRAJECTORY_PRIORITY_SEGMENT_CAPACITY
                                                       : RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY;
            if ( cacheIndex >= RETAINED_TRAJECTORY_MAX_DRAW_RANGES || segmentCount > segmentCapacity ||
                 firstSegment < laneBegin || firstSegment + segmentCapacity > laneEnd )
            {
                indirectArguments[rangeIndex] = {};
                continue;
            }

            const RetainedTrajectoryUploadPlanDX12 uploadPlan =
                BuildRetainedTrajectoryRangeUploadPlanDX12( buffer.rangeIdentities[cacheIndex],
                                                            buffer.rangeSourceVersions[cacheIndex],
                                                            buffer.rangeSegmentCounts[cacheIndex],
                                                            range.identity,
                                                            range.sourceVersion,
                                                            segmentCount );
            if ( uploadPlan.uploadRequired && uploadPlan.firstChangedUnit < segmentCount )
            {
                const std::size_t sourceFloat =
                    ( firstSegment + uploadPlan.firstChangedUnit ) * RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
                const std::size_t changedFloatCount =
                    ( segmentCount - uploadPlan.firstChangedUnit ) * RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
                memcpy( retainedBytes + ( compactFloatOffset + sourceFloat ) * sizeof( float ),
                        compactRecords.data() + sourceFloat,
                        changedFloatCount * sizeof( float ) );
            }
            buffer.rangeIdentities[cacheIndex] = range.identity;
            buffer.rangeSourceVersions[cacheIndex] = range.sourceVersion;
            buffer.rangeSegmentCounts[cacheIndex] = static_cast<uint32_t>( segmentCount );
            indirectArguments[rangeIndex] = { 6u,
                                              static_cast<UINT>( segmentCount ),
                                              0u,
                                              static_cast<UINT>( firstSegment ) };
            totalSegmentCount += static_cast<uint32_t>( segmentCount );
        }
        buffer.rangeStreamId = streamId;
        buffer.rangeRevision = revision;
        buffer.rangeTotalSegmentCount = totalSegmentCount;
    }

    ShaderDX12* transientShader = m_transientTriangleShaders[TransientTriangleStyleIndex( style )].get();
    if ( !transientShader || !m_retainedTrajectoryCommandSignature )
    {
        return;
    }
    transientShader->Use();
    transientShader->SetMat4( "uViewProj", viewProjection );
    transientShader->SetVec4( "uViewportPixels",
                              static_cast<float>( m_resourceDevice->Width() ),
                              static_cast<float>( m_resourceDevice->Height() ),
                              0.0f,
                              0.0f );
    const bool depthHint = style == TransientTriangleStyle::TrajectoryRibbonDepthHint;
    transientShader->SetVec4( "uRibbonStyle", depthHint ? 0.16f : 1.0f, depthHint ? 0.70f : 1.0f, 1.0f, 0.0f );

    DynamicVBDX12 vertexLayout = {};
    vertexLayout.numAttribs = 6;
    vertexLayout.attribComponents[0] = 3;
    vertexLayout.attribComponents[1] = 4;
    vertexLayout.attribComponents[2] = 4;
    vertexLayout.attribComponents[3] = 2;
    vertexLayout.attribComponents[4] = 3;
    vertexLayout.attribComponents[5] = 3;
    vertexLayout.floatsPerVertex = static_cast<int>( RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT );
    vertexLayout.stride = vertexLayout.floatsPerVertex * static_cast<int>( sizeof( float ) );
    vertexLayout.perInstance = true;
    if ( !m_resourceFrame->DrawGate()
              .PreparePipelineDraw( VertexFormat12::Pos3, false, nullptr, &vertexLayout, bucket.raster ) )
    {
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = retainedAddress + compactFloatOffset * sizeof( float );
    vbView.SizeInBytes = static_cast<UINT>(
        ( RETAINED_TRAJECTORY_ORDINARY_SEGMENT_CAPACITY + RETAINED_TRAJECTORY_PRIORITY_SEGMENT_CAPACITY ) *
        RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT * sizeof( float ) );
    vbView.StrideInBytes = static_cast<UINT>( vertexLayout.stride );
    m_resourceDevice->CommandList()->IASetVertexBuffers( 0, 1, &vbView );
    m_submissionDiagnostics->RecordDrawCall( { DrawCallKind::DynamicVertexBuffer,
                                               TransientTriangleTraceLabel( style ),
                                               6,
                                               static_cast<int>( buffer.rangeTotalSegmentCount ) } );
    // Stable frames reach this call without visiting a range or copying a byte.
    // The GPU consumes the retained command table in canonical range order.
    m_resourceDevice->CommandList()->ExecuteIndirect(
        m_retainedTrajectoryCommandSignature.Get(),
        static_cast<UINT>( ranges.size() ),
        m_resourceFrame->Uploads().Resource( frameIndex ),
        indirectAddress - m_resourceFrame->Uploads().Resource( frameIndex )->GetGPUVirtualAddress(),
        nullptr,
        0u );
}


void Dx12GeometryOwner::DrawRetainedLinesColored( std::span<const float> packedVertices,
                                                  uint64_t streamId,
                                                  uint64_t revision,
                                                  bool priorityLane,
                                                  const Math::Transformation::Matrix4& viewProjection,
                                                  const PassRasterStateBucket& bucket )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionPipeline && m_submissionDiagnostics );
    const std::size_t channelIndex = priorityLane ? 3u : 2u;
    const std::size_t ribbonFloatCapacity =
        RETAINED_TRAJECTORY_ORDINARY_FLOAT_CAPACITY + RETAINED_TRAJECTORY_PRIORITY_FLOAT_CAPACITY;
    const std::size_t laneCapacity = priorityLane ? RETAINED_TRAJECTORY_PRIORITY_LINE_FLOAT_CAPACITY
                                                  : RETAINED_TRAJECTORY_ORDINARY_LINE_FLOAT_CAPACITY;
    const std::size_t laneOffset =
        ribbonFloatCapacity + ( priorityLane ? RETAINED_TRAJECTORY_ORDINARY_LINE_FLOAT_CAPACITY : 0u );
    if ( packedVertices.empty() || packedVertices.size() % 6u != 0u || packedVertices.size() > laneCapacity ||
         !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }

    const UINT frameIndex = m_resourceFrame->FrameIndex();
    RetainedTrajectoryBufferDX12& buffer = m_retainedTrajectoryBuffers[frameIndex];
    uint8_t* retainedBytes = m_resourceFrame->Uploads().PersistentTailPointer( frameIndex );
    const D3D12_GPU_VIRTUAL_ADDRESS retainedAddress = m_resourceFrame->Uploads().PersistentTailAddress( frameIndex );
    const RetainedTrajectoryUploadPlanDX12 uploadPlan =
        BuildRetainedTrajectoryUploadPlanDX12( buffer.streamIds[channelIndex],
                                               buffer.revisions[channelIndex],
                                               buffer.uploadedFloatCounts[channelIndex],
                                               streamId,
                                               revision,
                                               packedVertices.size(),
                                               false );
    if ( uploadPlan.uploadRequired )
    {
        memcpy( retainedBytes + ( laneOffset + uploadPlan.firstChangedUnit ) * sizeof( float ),
                packedVertices.data() + uploadPlan.firstChangedUnit,
                ( packedVertices.size() - uploadPlan.firstChangedUnit ) * sizeof( float ) );
        buffer.streamIds[channelIndex] = streamId;
        buffer.revisions[channelIndex] = revision;
        buffer.uploadedFloatCounts[channelIndex] = packedVertices.size();
    }
    DrawLinesColoredFromBuffer( packedVertices.size(),
                                viewProjection,
                                retainedAddress + laneOffset * sizeof( float ),
                                m_resourceDevice->CommandList(),
                                *m_submissionPipeline,
                                m_resourceFrame->DrawGate(),
                                *m_submissionDiagnostics,
                                bucket.raster );
}


void Dx12GeometryOwner::BindResourceOwners( Dx12RenderDevice& device,
                                            Dx12FrameOwner& frame,
                                            Dx12PipelineOwner& pipeline,
                                            Dx12Diagnostics& diagnostics )
{
    m_resourceDevice = &device;
    m_resourceFrame = &frame;
    m_submissionPipeline = &pipeline;
    m_submissionDiagnostics = &diagnostics;
}


bool Dx12GeometryOwner::InitializeRetainedTrajectoryCommands( ID3D12Device* device )
{
    m_retainedTrajectoryCommandSignature.Reset();
    if ( !device )
    {
        return false;
    }
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature = {};
    signature.ByteStride = sizeof( D3D12_DRAW_ARGUMENTS );
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    return SUCCEEDED(
        device->CreateCommandSignature( &signature, nullptr, IID_PPV_ARGS( &m_retainedTrajectoryCommandSignature ) ) );
}


uint32_t Dx12GeometryOwner::CreateInstancedMesh( const float* staticVertices,
                                                 int staticVertexCount,
                                                 int staticFloatsPerVertex,
                                                 int instanceFloats,
                                                 int instanceStartAttribute,
                                                 std::span<const int> instanceAttributeSizes,
                                                 std::span<const int> staticAttributeSizes )
{
    assert( m_resourceDevice && m_resourceFrame );
    if ( !m_resourceFrame->EnsureOpen().ok )
    {
        return 0;
    }
    const UINT64 bytes = static_cast<UINT64>( staticVertexCount ) * staticFloatsPerVertex * sizeof( float );
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_resourceFrame->UploadReservations().ReserveUpload( bytes, 4, RenderUploadCategory::DynamicVertex );
    return CreateInstancedMesh( staticVertices,
                                staticVertexCount,
                                staticFloatsPerVertex,
                                instanceFloats,
                                instanceStartAttribute,
                                instanceAttributeSizes,
                                staticAttributeSizes,
                                m_resourceDevice->Device(),
                                m_resourceDevice->CommandList(),
                                m_resourceFrame->Uploads().Resource( m_resourceFrame->AllocatorIndex() ),
                                address,
                                address ? m_resourceFrame->UploadReservations().UploadPointer( address ) : nullptr );
}


void Dx12GeometryOwner::UploadInstanceData( uint32_t handle, std::span<const float> packedInstances )
{
    assert( m_resourceFrame && m_submissionPipeline );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
    if ( packedInstances.empty() || StaticVertexStride( handle ) <= 0 || !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        return;
    }
    const UINT64 bytes = static_cast<UINT64>( packedInstances.size_bytes() );
    // Invariant: the engine command contract pairs UploadInstanceData with the
    // immediately following DrawInstancedMesh under the same active shader. The
    // coordinator reserves that shader's CB plus instance bytes atomically, so
    // the draw cannot flush between the two published addresses.
    const ShaderDX12* shader = m_submissionPipeline->ActiveShader();
    const UINT64 constantBytes = shader ? shader->ConstantBufferUploadSize() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        m_resourceFrame->UploadReservations().ReserveGeometryUpload( bytes,
                                                                     constantBytes,
                                                                     RenderUploadCategory::InstanceData );
    UploadInstanceData( handle,
                        packedInstances,
                        address,
                        address ? m_resourceFrame->UploadReservations().UploadPointer( address ) : nullptr );
}


void Dx12GeometryOwner::DrawInstancedMesh( const InstancedMeshDrawDesc& draw )
{
    assert( m_resourceDevice && m_resourceFrame && m_submissionDiagnostics );
    if ( !m_resourceFrame->DrawGate().PrepareDraw() )
    {
        m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
        return;
    }
    DrawInstancedMesh( draw, m_resourceDevice->CommandList(), m_resourceFrame->DrawGate(), *m_submissionDiagnostics );
    m_resourceFrame->UploadReservations().CancelPendingConstantUpload();
}

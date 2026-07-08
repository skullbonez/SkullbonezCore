/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
Purpose:
  Builds and binds DX12 pipeline state, render targets, and descriptors.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  PIX: Microsoft GPU debugger/profiler that can read engine markers and DX12
  object names.
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
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
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

// --- RenderBackendDX12 Pipeline methods ---


static D3D12_BLEND MapBlendFactor( BlendFactor f )
{
    switch ( f )
    {
    case BlendFactor::Zero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::One:
        return D3D12_BLEND_ONE;
    case BlendFactor::SrcAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    default:
        return D3D12_BLEND_ONE;
    }
}


static INT TranslatePolygonOffsetDepthBiasDX12( float units )
{
    return static_cast<INT>( units );
}


static float TranslatePolygonOffsetSlopeBiasDX12( float factor )
{
    return factor;
}


size_t RenderBackendDX12::HashPSOKey( const PSOKey12& key )
{
    size_t h = 0;
    auto hashCombine = []( size_t& seed, size_t val ) { seed ^= val + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ); };
    auto hashFloatBits = []( float value )
    {
        uint32_t bits = 0;
        memcpy( &bits, &value, sizeof( bits ) );
        return static_cast<size_t>( bits );
    };
    hashCombine( h, (size_t)key.rootSignature );
    hashCombine( h, key.shaderVSHash );
    hashCombine( h, key.shaderPSHash );
    hashCombine( h, (size_t)key.format );
    hashCombine( h, (size_t)key.isInstanced );
    hashCombine( h, (size_t)key.blendEnabled );
    hashCombine( h, (size_t)key.blendSrc );
    hashCombine( h, (size_t)key.blendDst );
    hashCombine( h, (size_t)key.depthEnabled );
    hashCombine( h, (size_t)key.depthWriteEnabled );
    hashCombine( h, (size_t)key.cullEnabled );
    hashCombine( h, (size_t)key.polyOffsetEnabled );
    hashCombine( h, (size_t)key.polyOffsetDepthBias );
    hashCombine( h, hashFloatBits( key.polyOffsetSlopeScaledDepthBias ) );
    hashCombine( h, (size_t)key.rtvFormat );
    return h;
}


void RenderBackendDX12::BuildInputLayout( VertexFormat12 format, D3D12_INPUT_ELEMENT_DESC* out, UINT& count )
{
    count = 0;
    switch ( format )
    {
    case VertexFormat12::Pos3:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 1;
        break;
    case VertexFormat12::Pos3_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 2;
        break;
    case VertexFormat12::Pos3_Norm3_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[2] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 3;
        break;
    case VertexFormat12::Pos2_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 2;
        break;
    case VertexFormat12::Pos2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 1;
        break;
    }
}


void RenderBackendDX12::BuildInstancedInputLayout( const InstancedMeshDX12& im,
                                                   D3D12_INPUT_ELEMENT_DESC* out,
                                                   UINT& count )
{
    count = 0;

    // Slot 0: static vertex data
    if ( im.numStaticAttribs > 0 )
    {
        // Multi-attribute layout (e.g. POSITION + NORMAL + TEXCOORD)
        static const char* staticSemantics[] = { "POSITION", "NORMAL", "TEXCOORD" };
        UINT staticOffset = 0;
        for ( int i = 0; i < im.numStaticAttribs; ++i )
        {
            DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
            if ( im.staticAttribSizes[i] == 2 )
            {
                fmt = DXGI_FORMAT_R32G32_FLOAT;
            }
            else if ( im.staticAttribSizes[i] == 3 )
            {
                fmt = DXGI_FORMAT_R32G32B32_FLOAT;
            }
            else if ( im.staticAttribSizes[i] == 4 )
            {
                fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
            }

            out[count].SemanticName = staticSemantics[i < 3 ? i : 2];
            out[count].SemanticIndex = ( i >= 2 ) ? (UINT)( i - 2 ) : 0;
            out[count].Format = fmt;
            out[count].InputSlot = 0;
            out[count].AlignedByteOffset = staticOffset;
            out[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            out[count].InstanceDataStepRate = 0;
            ++count;
            staticOffset += (UINT)im.staticAttribSizes[i] * sizeof( float );
        }
    }
    else
    {
        // Legacy: single POSITION attribute
        out[count++] =
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    }

    // Slot 1: per-instance attributes
    UINT instOffset = 0;
    for ( int i = 0; i < im.numInstanceAttribs; ++i )
    {
        DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
        if ( im.instanceAttribSizes[i] == 2 )
        {
            fmt = DXGI_FORMAT_R32G32_FLOAT;
        }
        else if ( im.instanceAttribSizes[i] == 3 )
        {
            fmt = DXGI_FORMAT_R32G32B32_FLOAT;
        }
        else if ( im.instanceAttribSizes[i] == 4 )
        {
            fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        out[count].SemanticName = "TEXCOORD";
        out[count].SemanticIndex = (UINT)( im.instanceStartAttrib + i - 2 );
        out[count].Format = fmt;
        out[count].InputSlot = 1;
        out[count].AlignedByteOffset = instOffset;
        out[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        out[count].InstanceDataStepRate = 1;
        ++count;
        instOffset += (UINT)im.instanceAttribSizes[i] * sizeof( float );
    }
}


void RenderBackendDX12::BuildDynamicVBInputLayout( const DynamicVBDX12& dvb,
                                                   D3D12_INPUT_ELEMENT_DESC* out,
                                                   UINT& count )
{
    count = 0;
    UINT offset = 0;
    const bool hasNormal = dvb.numAttribs >= 2 && dvb.attribComponents[0] == 3 && dvb.attribComponents[1] == 3;
    const bool hasUvAfterNormal = hasNormal && dvb.numAttribs >= 3 && dvb.attribComponents[2] == 2;
    for ( int i = 0; i < dvb.numAttribs; ++i )
    {
        DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
        if ( dvb.attribComponents[i] == 2 )
        {
            fmt = DXGI_FORMAT_R32G32_FLOAT;
        }
        else if ( dvb.attribComponents[i] == 3 )
        {
            fmt = DXGI_FORMAT_R32G32B32_FLOAT;
        }
        else if ( dvb.attribComponents[i] == 4 )
        {
            fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        if ( i == 0 )
        {
            out[count] = { "POSITION", 0, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        else if ( hasNormal && i == 1 )
        {
            out[count] = { "NORMAL", 0, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        else if ( hasUvAfterNormal && i == 2 )
        {
            out[count] = { "TEXCOORD", 0, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        else
        {
            const UINT semanticIndex =
                hasNormal ? static_cast<UINT>( hasUvAfterNormal ? i - 2 : i - 1 ) : static_cast<UINT>( i - 1 );
            out[count] = { "TEXCOORD", semanticIndex, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        ++count;
        offset += (UINT)dvb.attribComponents[i] * sizeof( float );
    }
}


ID3D12PipelineState* RenderBackendDX12::CreatePSO( VertexFormat12 format,
                                                   bool instanced,
                                                   const InstancedMeshDX12* im,
                                                   const DynamicVBDX12* dvb )
{
    D3D12_INPUT_ELEMENT_DESC elements[16] = {};
    UINT numElements = 0;

    if ( instanced && im )
    {
        BuildInstancedInputLayout( *im, elements, numElements );
    }
    else if ( dvb )
    {
        BuildDynamicVBInputLayout( *dvb, elements, numElements );
    }
    else
    {
        BuildInputLayout( format, elements, numElements );
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = { m_activeShader->GetVSBytecode(), m_activeShader->GetVSBytecodeSize() };
    psoDesc.PS = { m_activeShader->GetPSBytecode(), m_activeShader->GetPSBytecodeSize() };
    psoDesc.InputLayout = { elements, numElements };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // Rasterizer
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = m_cullEnabled ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    if ( m_polyOffsetEnabled )
    {
        psoDesc.RasterizerState.DepthBias = TranslatePolygonOffsetDepthBiasDX12( m_polyOffsetUnits );
        psoDesc.RasterizerState.SlopeScaledDepthBias = TranslatePolygonOffsetSlopeBiasDX12( m_polyOffsetFactor );
    }
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // Depth stencil
    psoDesc.DepthStencilState.DepthEnable = m_depthTestEnabled ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask =
        m_depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // Blend
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if ( m_blendEnabled )
    {
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = MapBlendFactor( m_blendSrc );
        psoDesc.BlendState.RenderTarget[0].DestBlend = MapBlendFactor( m_blendDst );
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_currentRTVFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    // Create a Graphics Pipeline State Object (PSO). In DX12, all render state
    // is compiled into one monolithic object: shaders, input layout, rasterizer
    // settings, blend mode, depth test, and render-target formats. The PSO is
    // expensive to create but fast to bind, so we cache them by hash and reuse
    // across frames.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-creategraphicspipelinestate
    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = Device()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "CreateGraphicsPipelineState failed" );
    }
    // A graphics PSO is the compiled bundle of shaders plus fixed GPU state
    // such as blend, depth, rasterizer, render-target format, and vertex layout.
    // Naming cached PSOs makes PIX and debug-layer output identify the object as
    // a Skullbonez graphics pipeline instead of an anonymous D3D12 pointer.
    NameDx12Object( pso, L"Skullbonez DX12 Cached Graphics PSO" );
    return pso;
}


void RenderBackendDX12::PrepareDraw( VertexFormat12 format,
                                     bool instanced,
                                     const InstancedMeshDX12* im,
                                     const DynamicVBDX12* dvb )
{
    EnsureCommandListOpen();

    if ( !m_renderingToFBO && !m_backBufferIsRT )
    {
        ExecuteGraphTransition( "PrepareDrawBackbuffer",
                                "SwapchainBackbuffer",
                                m_renderTargets[m_frameIndex],
                                RenderGraphResourceAccess::Present,
                                RenderGraphResourceAccess::RenderTarget );
        m_backBufferIsRT = true;
        m_targetsDirty = true;
    }

    // Concept: the PSO cache key is the complete "shape" of a draw pipeline.
    //
    // DX12 cannot cheaply toggle individual pieces of fixed-function state the
    // way old immediate renderers did. The vertex layout, stable shader bytecode
    // hashes, blend/depth/cull state, polygon offset, instancing mode, and
    // render-target format all participate in the Pipeline State Object. If any
    // of those values changes, the cached PSO may no longer describe the draw correctly.
    // Include the root signature too: today ordinary raster draws share one
    // signature, but future fullscreen, material-table, or graph-local resource
    // signatures must not accidentally reuse an incompatible cached PSO.
    PSOKey12 key = {};
    key.rootSignature = m_rootSignature;
    key.shaderVSHash = m_activeShader->GetVSBytecodeHash();
    key.shaderPSHash = m_activeShader->GetPSBytecodeHash();
    key.format = format;
    key.isInstanced = instanced;
    key.blendEnabled = m_blendEnabled;
    key.blendSrc = m_blendSrc;
    key.blendDst = m_blendDst;
    key.depthEnabled = m_depthTestEnabled;
    key.depthWriteEnabled = m_depthWriteEnabled;
    key.cullEnabled = m_cullEnabled;
    key.polyOffsetEnabled = m_polyOffsetEnabled;
    key.polyOffsetDepthBias = m_polyOffsetEnabled ? TranslatePolygonOffsetDepthBiasDX12( m_polyOffsetUnits ) : 0;
    key.polyOffsetSlopeScaledDepthBias =
        m_polyOffsetEnabled ? TranslatePolygonOffsetSlopeBiasDX12( m_polyOffsetFactor ) : 0.0f;
    key.rtvFormat = m_currentRTVFormat;

    size_t psoHash = HashPSOKey( key );
    if ( dvb )
    {
        for ( int i = 0; i < dvb->numAttribs; ++i )
        {
            psoHash ^= ( (size_t)dvb->attribComponents[i] << ( i * 4 ) );
        }
    }

    // Fast path: if PSO, texture descriptor bindings, and render targets are
    // unchanged, the only per-draw work left is uploading the constant buffer.
    // This is the common path for many objects sharing the same mesh/shader
    // shape, such as generated balls or boxes.
    bool psoChanged = m_psoDirty || ( psoHash != m_lastPSOHash );

    if ( !psoChanged && !m_texBindingsDirty && !m_targetsDirty )
    {
        // Only the constant buffer has changed (e.g. model matrix per ball)
        if ( m_activeShader )
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
            if ( cbAddr )
            {
                CommandList()->SetGraphicsRootConstantBufferView( 0, cbAddr );
            }
        }
        return;
    }

    // Full state setup path: at least one expensive binding category changed,
    // so rebuild/reuse the PSO, rebind the root signature, refresh constants,
    // copy texture descriptors, and update output targets.
    if ( psoChanged )
    {
        ID3D12PipelineState* pso = nullptr;
        for ( size_t i = 0; i < m_psoCacheCount; ++i )
        {
            if ( m_psoCache[i].hash == psoHash )
            {
                pso = m_psoCache[i].pso;
                break;
            }
        }
        if ( !pso )
        {
            // Diagnostics: a cache miss means the renderer discovered a new
            // pipeline shape. That is expected during warm-up, but unexpected
            // misses in validation/perf runs can reveal root-signature churn,
            // render-target format drift, or state toggles happening in hot
            // loops.
            Log().WriteEventf( "dx12_pso_cache_miss hash=%llu cache_size=%llu root_signature=%p vs_hash=%llu "
                               "ps_hash=%llu format=%u instanced=%d blend=%d depth=%d depth_write=%d cull=%d "
                               "rtv_format=%u",
                               static_cast<unsigned long long>( psoHash ),
                               static_cast<unsigned long long>( m_psoCacheCount ),
                               key.rootSignature,
                               static_cast<unsigned long long>( key.shaderVSHash ),
                               static_cast<unsigned long long>( key.shaderPSHash ),
                               static_cast<unsigned int>( key.format ),
                               key.isInstanced ? 1 : 0,
                               key.blendEnabled ? 1 : 0,
                               key.depthEnabled ? 1 : 0,
                               key.depthWriteEnabled ? 1 : 0,
                               key.cullEnabled ? 1 : 0,
                               static_cast<unsigned int>( key.rtvFormat ) );
            if ( m_psoCacheCount >= m_psoCache.size() )
            {
                fprintf( stderr,
                         "FATAL: DX12 graphics PSO cache exhausted (capacity=%zu hash=%llu format=%u instanced=%d)\n",
                         m_psoCache.size(),
                         static_cast<unsigned long long>( psoHash ),
                         static_cast<unsigned int>( key.format ),
                         key.isInstanced ? 1 : 0 );
                fprintf( stdout,
                         "FATAL: DX12 graphics PSO cache exhausted (capacity=%zu hash=%llu format=%u instanced=%d)\n",
                         m_psoCache.size(),
                         static_cast<unsigned long long>( psoHash ),
                         static_cast<unsigned int>( key.format ),
                         key.isInstanced ? 1 : 0 );
                fflush( stderr );
                fflush( stdout );
                throw std::runtime_error( "DX12 graphics PSO cache exhausted" );
            }
            pso = CreatePSO( format, instanced, im, dvb );
            m_psoCache[m_psoCacheCount].hash = psoHash;
            m_psoCache[m_psoCacheCount].pso = pso;
            ++m_psoCacheCount;
        }

        // Bind the PSO. This sets the whole GPU pipeline recipe (shaders,
        // blend, depth, rasterizer, render-target formats) in one call.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpipelinestate
        CommandList()->SetPipelineState( pso );

        // Re-bind root signature after PSO change (required by DX12 spec).
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootsignature
        CommandList()->SetGraphicsRootSignature( m_rootSignature );
        m_lastPSOHash = psoHash;
    }

    // Flush constant buffer data and bind it at root parameter [0] — this is where per-draw
    // shader uniforms (MVP matrix, colors, time, etc.) are uploaded to the GPU each draw call.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootconstantbufferview
    if ( m_activeShader )
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
        if ( cbAddr )
        {
            CommandList()->SetGraphicsRootConstantBufferView( ROOT_PARAMETER_FRAME_CONSTANTS, cbAddr );
        }
    }

    // Bind textures by copying their SRV descriptors to the shader-visible heap
    // and pointing the root descriptor table at them. Root params [1..5] map to
    // texture slots t0..t4. The object pass uses t4 for the material table.
    //
    // Plain-language flow:
    //
    // 1. Game/render code chooses a texture handle.
    // 2. The texture registry resolves that to a persistent SRV descriptor row
    //    in the CPU-only staging heap.
    // 3. This draw gets a transient row in the shader-visible heap.
    // 4. The persistent descriptor is copied into the transient row.
    // 5. The command list binds the transient row's GPU handle.
    // 6. When the pixel shader samples t0/t1/t2/t3/t4, the GPU follows that handle.
    //
    // DX12 does not bind "the C++ texture object" directly. It binds a descriptor
    // table row that describes how the shader should read that texture.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootdescriptortable
    if ( m_texBindingsDirty )
    {
        for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
        {
            UINT srcIdx = m_boundTexSlot[slot];
            if ( srcIdx == UINT_MAX )
            {
                srcIdx = m_nullTextureSRVIndex;
            }
            if ( srcIdx != UINT_MAX )
            {
                // The texture's persistent descriptor lives in the CPU-only
                // staging heap. For this draw, copy that descriptor into a
                // per-frame shader-visible row and bind the GPU handle to that
                // row. This copy looks redundant at first, but it is the safety
                // mechanism: transient rows are reset only after the frame fence
                // proves the GPU is done with them.
                UINT transient = AllocateTransientSRV();
                D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = m_srvDescriptors.ShaderVisibleCpuHandle( transient );
                Device()->CopyDescriptorsSimple( 1,
                                                 dstHandle,
                                                 GetSRVStagingCpuHandle( srcIdx ),
                                                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                CommandList()->SetGraphicsRootDescriptorTable( ROOT_PARAMETER_FIRST_TEXTURE + static_cast<UINT>( slot ),
                                                               GetSRVGpuHandle( transient ) );
            }
        }
        m_texBindingsDirty = false;
    }

    // Avoid redundant OM/RS binds; target changes are tracked explicitly.
    if ( m_targetsDirty )
    {
        CommandList()->RSSetViewports( 1, &m_viewport );
        CommandList()->RSSetScissorRects( 1, &m_scissorRect );
        CommandList()->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
        m_targetsDirty = false;
    }

    // Primitive topology tells the Input Assembler how to interpret vertex data.
    // TRIANGLELIST means every 3 vertices form an independent triangle.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetprimitivetopology
    CommandList()->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_psoDirty = false;
}


void RenderBackendDX12::SetActiveShader( ShaderDX12* shader )
{
    m_activeShader = shader;
    m_psoDirty = true;
}


void RenderBackendDX12::SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv )
{
    m_currentRTV = rtv;
    m_currentDSV = dsv;
    m_targetsDirty = true;
}


void RenderBackendDX12::SetRenderingToFBO( bool rendering,
                                           UINT fboSrvIndex,
                                           UINT fboDepthSrvIndex,
                                           DXGI_FORMAT rtvFormat )
{
    m_renderingToFBO = rendering;
    m_currentRTVFormat = rendering ? rtvFormat : DXGI_FORMAT_R8G8B8A8_UNORM;
    m_psoDirty = true;
    if ( rendering )
    {
        // Hazard: an FBO texture cannot be sampled while it is also the active
        // render target.
        //
        // The same image can have an RTV view for writing and an SRV view for
        // reading, but not at the same time in this pass. Clear any texture slot
        // still pointing at the FBO color/depth SRV before the resource is used
        // as a render target again.
        ClearBoundTextureSlotsForSrv( fboSrvIndex );
        ClearBoundTextureSlotsForSrv( fboDepthSrvIndex );
    }
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateRTV()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_rtvDescriptors.GetStats();
    if ( stats.used >= stats.capacity )
    {
        ReportDX12DescriptorHeapExhausted( stats.heapName, stats.used, stats.capacity );
        throw std::runtime_error( "DX12 RTV heap exhausted" );
    }
    return m_rtvDescriptors.Allocate().cpuHandle;
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateDSV()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_dsvDescriptors.GetStats();
    if ( stats.used >= stats.capacity )
    {
        ReportDX12DescriptorHeapExhausted( stats.heapName, stats.used, stats.capacity );
        throw std::runtime_error( "DX12 DSV heap exhausted" );
    }
    return m_dsvDescriptors.Allocate().cpuHandle;
}

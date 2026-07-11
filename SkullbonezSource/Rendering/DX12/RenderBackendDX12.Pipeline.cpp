/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
Purpose:
  Implements Dx12PipelineOwner's root signature, fixed-state recipe, and cache.

Mental model:
  Dx12PipelineOwner turns the active shader, vertex layout, raster/depth/blend
  choices, and output format into a bounded cached PSO. It owns the dirty-state
  fast path and receives only the device, command list, recording state,
  descriptor allocator, and texture bindings required for one draw.

Glossary:
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads/writes depth
  and stencil values during depth testing.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  FBO (Framebuffer Object): Engine term for an off-screen color/depth target
  that can later be sampled as a texture.
  PIX: Microsoft GPU debugger/profiler that can read engine markers and DX12
  object names.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - The graphics PSO cache and framebuffer descriptor heaps are fixed backend
    capacity. Exhaustion means renderer capacity planning failed; do not grow
    them during draw submission or render-target allocation.

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
#include <stdexcept>
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


size_t Dx12PipelineOwner::HashPSOKey( const PSOKey12& key )
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


void Dx12PipelineOwner::BuildInputLayout( VertexFormat12 format, D3D12_INPUT_ELEMENT_DESC* out, UINT& count )
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


void Dx12PipelineOwner::BuildInstancedInputLayout( const InstancedMeshDX12& im,
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


void Dx12PipelineOwner::BuildDynamicVBInputLayout( const DynamicVBDX12& dvb,
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


ID3D12PipelineState* Dx12PipelineOwner::CreatePSO( ID3D12Device* device,
                                                   VertexFormat12 format,
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

    std::string inputContractError;
    if ( !m_activeShader->ValidateInputLayout( elements, numElements, inputContractError ) )
    {
        // Lane R: mesh/layout selection is startup-owned pipeline input. A
        // reflected mismatch skips PSO publication and names the owning path.
        Log().WriteEventf( "dx12_shader_input_contract_rejected owner=Dx12PipelineOwner reason=%s",
                           inputContractError.c_str() );
        Log().FlushAll();
        return nullptr;
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
    HRESULT hr = device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );
    if ( FAILED( hr ) || !pso )
    {
        // Lane R: a graphics PSO can fail because the active shader/input layout
        // or device state is invalid. The draw path can skip this submission and
        // keep the renderer alive; fixed cache-cap exhaustion above remains fatal.
        Log().WriteEventf( "dx12_graphics_pso_create_failed hresult=0x%08X format=%u instanced=%d rtv_format=%u",
                           static_cast<unsigned int>( FAILED( hr ) ? hr : E_FAIL ),
                           static_cast<unsigned int>( format ),
                           instanced ? 1 : 0,
                           static_cast<unsigned int>( m_currentRTVFormat ) );
        Log().FlushAll();
        if ( pso )
        {
            pso->Release();
        }
        return nullptr;
    }
    // A graphics PSO is the compiled bundle of shaders plus fixed GPU state
    // such as blend, depth, rasterizer, render-target format, and vertex layout.
    // Naming cached PSOs makes PIX and debug-layer output identify the object as
    // a Skullbonez graphics pipeline instead of an anonymous D3D12 pointer.
    NameDx12Object( pso, L"Skullbonez DX12 Cached Graphics PSO" );
    return pso;
}


bool Dx12PipelineOwner::PrepareDraw( ID3D12Device* device,
                                     ID3D12GraphicsCommandList* commandList,
                                     Dx12CommandRecordingState& recording,
                                     Dx12TextureOwner& textures,
                                     Dx12DescriptorAllocator& descriptors,
                                     VertexFormat12 format,
                                     bool instanced,
                                     const InstancedMeshDX12* im,
                                     const DynamicVBDX12* dvb )
{
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

    if ( !psoChanged && !textures.BindingsDirty() && !m_targetsDirty )
    {
        // Only the constant buffer has changed (e.g. model matrix per ball)
        if ( m_activeShader )
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
            if ( recording.HasFailure() )
            {
                return false;
            }
            if ( cbAddr )
            {
                commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
            }
        }
        return true;
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
                // Invariant: PSO variants are bounded by the fixed cache in the
                // backend. A new draw-state family needs an intentional cache
                // budget, not growth from the per-draw binding path.
                SB_FATAL( "RenderBackendDX12",
                          "DX12 graphics PSO cache exhausted. capacity=%zu hash=%llu format=%u instanced=%d",
                          m_psoCache.size(),
                          static_cast<unsigned long long>( psoHash ),
                          static_cast<unsigned int>( key.format ),
                          key.isInstanced ? 1 : 0 );
            }
            pso = CreatePSO( device, format, instanced, im, dvb );
            if ( !pso )
            {
                return false;
            }
            m_psoCache[m_psoCacheCount].hash = psoHash;
            m_psoCache[m_psoCacheCount].pso = pso;
            ++m_psoCacheCount;
        }

        // Bind the PSO. This sets the whole GPU pipeline recipe (shaders,
        // blend, depth, rasterizer, render-target formats) in one call.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpipelinestate
        commandList->SetPipelineState( pso );

        // Re-bind root signature after PSO change (required by DX12 spec).
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootsignature
        commandList->SetGraphicsRootSignature( m_rootSignature );
        m_lastPSOHash = psoHash;
    }

    // Flush constant buffer data and bind it at root parameter [0] — this is where per-draw
    // shader uniforms (MVP matrix, colors, time, etc.) are uploaded to the GPU each draw call.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootconstantbufferview
    if ( m_activeShader )
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
        if ( recording.HasFailure() )
        {
            return false;
        }
        if ( cbAddr )
        {
            commandList->SetGraphicsRootConstantBufferView( ROOT_PARAMETER_FRAME_CONSTANTS, cbAddr );
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
    if ( textures.BindingsDirty() )
    {
        for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
        {
            UINT srcIdx = textures.ResolveBoundSrv( slot );
            if ( srcIdx != UINT_MAX )
            {
                // The texture's persistent descriptor lives in the CPU-only
                // staging heap. For this draw, copy that descriptor into a
                // per-frame shader-visible row and bind the GPU handle to that
                // row. This copy looks redundant at first, but it is the safety
                // mechanism: transient rows are reset only after the frame fence
                // proves the GPU is done with them.
                UINT transient = descriptors.AllocateTransient();
                D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = descriptors.ShaderVisibleCpuHandle( transient );
                device->CopyDescriptorsSimple( 1,
                                               dstHandle,
                                               descriptors.StagingCpuHandle( srcIdx ),
                                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                commandList->SetGraphicsRootDescriptorTable( ROOT_PARAMETER_FIRST_TEXTURE + static_cast<UINT>( slot ),
                                                             descriptors.ShaderVisibleGpuHandle( transient ) );
            }
        }
        textures.MarkBindingsClean();
    }

    // Avoid redundant OM/RS binds; target changes are tracked explicitly.
    if ( m_targetsDirty )
    {
        commandList->RSSetViewports( 1, &m_viewport );
        commandList->RSSetScissorRects( 1, &m_scissorRect );
        commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
        m_targetsDirty = false;
    }

    // Primitive topology tells the Input Assembler how to interpret vertex data.
    // TRIANGLELIST means every 3 vertices form an independent triangle.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetprimitivetopology
    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_psoDirty = false;
    return true;
}


void Dx12PipelineOwner::SetActiveShader( ShaderDX12* shader )
{
    m_activeShader = shader;
    m_psoDirty = true;
}


void Dx12PipelineOwner::SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv )
{
    m_currentRTV = rtv;
    m_currentDSV = dsv;
    m_targetsDirty = true;
}


void Dx12PipelineOwner::SetRenderingToFBO( bool rendering, DXGI_FORMAT rtvFormat )
{
    m_renderingToFBO = rendering;
    m_currentRTVFormat = rendering ? rtvFormat : DXGI_FORMAT_R8G8B8A8_UNORM;
    m_psoDirty = true;
}


ShaderDX12* Dx12PipelineOwner::ActiveShader() const
{
    return m_activeShader;
}


void Dx12PipelineOwner::SetViewport( const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor )
{
    m_viewport = viewport;
    m_scissorRect = scissor;
    m_targetsDirty = true;
}


void Dx12PipelineOwner::InvalidateCommandState()
{
    m_lastPSOHash = 0;
    m_psoDirty = true;
    m_targetsDirty = true;
}


void Dx12PipelineOwner::InvalidateTargets()
{
    m_targetsDirty = true;
}


DXGI_FORMAT Dx12PipelineOwner::RenderTargetFormat() const
{
    return m_currentRTVFormat;
}


ID3D12RootSignature* Dx12PipelineOwner::RootSignature() const
{
    return m_rootSignature;
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12PipelineOwner::CurrentRTV() const
{
    return m_currentRTV;
}


D3D12_CPU_DESCRIPTOR_HANDLE Dx12PipelineOwner::CurrentDSV() const
{
    return m_currentDSV;
}


bool Dx12PipelineOwner::RenderingToFramebuffer() const
{
    return m_renderingToFBO;
}


void Dx12PipelineOwner::RestoreRenderTargetFormat( DXGI_FORMAT format )
{
    m_currentRTVFormat = format;
    m_psoDirty = true;
}


void Dx12PipelineOwner::SetCurrentColorTarget( D3D12_CPU_DESCRIPTOR_HANDLE rtv )
{
    SetCurrentTargets( rtv, m_currentDSV );
}


void Dx12PipelineOwner::BindCurrentOutputs( ID3D12GraphicsCommandList* commandList ) const
{
    // Invariant: target and viewport/scissor state are one draw-output recipe.
    // Special draw paths use this operation rather than reaching into owner
    // fields and accidentally publishing only half of that recipe.
    commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
    commandList->RSSetViewports( 1, &m_viewport );
    commandList->RSSetScissorRects( 1, &m_scissorRect );
}


void Dx12PipelineOwner::ClearCurrentColor( ID3D12GraphicsCommandList* commandList, const float color[4] ) const
{
    commandList->ClearRenderTargetView( m_currentRTV, color, 0, nullptr );
}


void Dx12PipelineOwner::ClearCurrentDepth( ID3D12GraphicsCommandList* commandList, float depth ) const
{
    commandList->ClearDepthStencilView( m_currentDSV, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr );
}


size_t Dx12PipelineOwner::CacheCount() const
{
    return m_psoCacheCount;
}


void Dx12PipelineOwner::Shutdown()
{
    for ( size_t index = 0; index < m_psoCacheCount; ++index )
    {
        if ( m_psoCache[index].pso )
        {
            m_psoCache[index].pso->Release();
            m_psoCache[index].pso = nullptr;
        }
    }
    m_psoCacheCount = 0;
    if ( m_rootSignature )
    {
        m_rootSignature->Release();
        m_rootSignature = nullptr;
    }
    m_activeShader = nullptr;
    ResetDesiredState();
}


void Dx12PipelineOwner::ResetDesiredState()
{
    Dx12PipelineDesiredState defaults;
    defaults.Reset();
    m_activeShader = defaults.m_activeShader;
    m_viewport = defaults.m_viewport;
    m_scissorRect = defaults.m_scissorRect;
    m_currentRTV = defaults.m_currentRTV;
    m_currentDSV = defaults.m_currentDSV;
    m_currentRTVFormat = defaults.m_currentRTVFormat;
    m_depthTestEnabled = defaults.m_depthTestEnabled;
    m_depthWriteEnabled = defaults.m_depthWriteEnabled;
    m_blendEnabled = defaults.m_blendEnabled;
    m_blendSrc = defaults.m_blendSrc;
    m_blendDst = defaults.m_blendDst;
    m_cullEnabled = defaults.m_cullEnabled;
    m_polyOffsetEnabled = defaults.m_polyOffsetEnabled;
    m_polyOffsetFactor = defaults.m_polyOffsetFactor;
    m_polyOffsetUnits = defaults.m_polyOffsetUnits;
    m_renderingToFBO = defaults.m_renderingToFBO;
    m_lastPSOHash = defaults.m_lastPSOHash;
    m_psoDirty = defaults.m_psoDirty;
    m_targetsDirty = defaults.m_targetsDirty;
}


void Dx12PipelineOwner::SetDepthTest( bool enabled )
{
    if ( m_depthTestEnabled != enabled )
    {
        m_depthTestEnabled = enabled;
        m_psoDirty = true;
    }
}


void Dx12PipelineOwner::SetDepthWrite( bool enabled )
{
    if ( m_depthWriteEnabled != enabled )
    {
        m_depthWriteEnabled = enabled;
        m_psoDirty = true;
    }
}


void Dx12PipelineOwner::SetBlend( bool enabled )
{
    if ( m_blendEnabled != enabled )
    {
        m_blendEnabled = enabled;
        m_psoDirty = true;
    }
}


void Dx12PipelineOwner::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    if ( m_blendSrc != src || m_blendDst != dst )
    {
        m_blendSrc = src;
        m_blendDst = dst;
        m_psoDirty = true;
    }
}


void Dx12PipelineOwner::SetCullFace( bool enabled )
{
    if ( m_cullEnabled != enabled )
    {
        m_cullEnabled = enabled;
        m_psoDirty = true;
    }
}


void Dx12PipelineOwner::SetPolygonOffset( bool enabled, float factor, float units )
{
    if ( m_polyOffsetEnabled != enabled || m_polyOffsetFactor != factor || m_polyOffsetUnits != units )
    {
        m_polyOffsetEnabled = enabled;
        m_polyOffsetFactor = factor;
        m_polyOffsetUnits = units;
        m_psoDirty = true;
    }
}


bool Dx12PipelineOwner::DepthTestEnabled() const
{
    return m_depthTestEnabled;
}
bool Dx12PipelineOwner::DepthWriteEnabled() const
{
    return m_depthWriteEnabled;
}
bool Dx12PipelineOwner::BlendEnabled() const
{
    return m_blendEnabled;
}
bool Dx12PipelineOwner::CullEnabled() const
{
    return m_cullEnabled;
}


void Dx12PipelineOwner::GetBlendFunc( BlendFactor& src, BlendFactor& dst ) const
{
    src = m_blendSrc;
    dst = m_blendDst;
}


bool RenderBackendDX12::PrepareDraw( VertexFormat12 format,
                                     bool instanced,
                                     const InstancedMeshDX12* instancedMesh,
                                     const DynamicVBDX12* dynamicVertexBuffer )
{
    return m_frameOwner.DrawGate().PreparePipelineDraw( format, instanced, instancedMesh, dynamicVertexBuffer );
}


void RenderBackendDX12::SetActiveShader( ShaderDX12* shader )
{
    m_pipelineOwner.SetActiveShader( shader );
}


void RenderBackendDX12::SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv )
{
    m_pipelineOwner.SetCurrentTargets( rtv, dsv );
}


void RenderBackendDX12::SetRenderingToFBO( bool rendering,
                                           UINT fboSrvIndex,
                                           UINT fboDepthSrvIndex,
                                           DXGI_FORMAT rtvFormat )
{
    m_pipelineOwner.SetRenderingToFBO( rendering, rtvFormat );
    if ( rendering )
    {
        // Hazard: a render-target resource cannot remain sampled through a
        // texture slot while the output-merger writes it.
        m_textureOwner.ClearBoundSlotsForSrv( fboSrvIndex );
        m_textureOwner.ClearBoundSlotsForSrv( fboDepthSrvIndex );
    }
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateRTV()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_rtvDescriptors.GetStats();
    if ( stats.used >= stats.capacity )
    {
        // Invariant: RTV rows are fixed backend capacity and must be budgeted
        // before render-target creation starts consuming them.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 RTV heap exhausted. heap=%s used=%u capacity=%u",
                  stats.heapName ? stats.heapName : "unknown",
                  stats.used,
                  stats.capacity );
    }
    return m_rtvDescriptors.Allocate().cpuHandle;
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateDSV()
{
    const Dx12CpuDescriptorAllocatorStats stats = m_dsvDescriptors.GetStats();
    if ( stats.used >= stats.capacity )
    {
        // Invariant: DSV rows are fixed backend capacity and must be budgeted
        // before depth-target creation starts consuming them.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 DSV heap exhausted. heap=%s used=%u capacity=%u",
                  stats.heapName ? stats.heapName : "unknown",
                  stats.used,
                  stats.capacity );
    }
    return m_dsvDescriptors.Allocate().cpuHandle;
}

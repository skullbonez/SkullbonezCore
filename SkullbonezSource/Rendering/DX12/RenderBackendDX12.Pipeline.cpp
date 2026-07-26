/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
Purpose:
  Implements Dx12PipelineOwner's root signature, fixed-state recipe, and cache.

Summary:
  Dx12PipelineOwner turns the active shader, vertex layout, raster/depth/blend
  choices, and output format into a bounded cached PSO. Legacy draws source
  those choices from tracked setters; migrated draws and pass precompile source
  them from a declared value. The owner keeps one cache and one key path for
  both during migration.

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
  - Shader-development code may release this owner's PSOs only after its fixed
    candidate registry is complete and the composition root has drained the GPU.
  - Pass precompile and draw-time lookup share the exact key-builder/cache path;
    precompile never binds command-list state.

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

    hashCombine( h, static_cast<size_t>( key.rootSignatureIdentity ) );
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
        out[count++] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
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
    const D3D12_INPUT_CLASSIFICATION inputClass = dvb.perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                                                  : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    const UINT instanceStepRate = dvb.perInstance ? 1u : 0u;
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
            out[count] = { "POSITION", 0, fmt, 0, offset, inputClass, instanceStepRate };
        }
        else if ( hasNormal && i == 1 )
        {
            out[count] = { "NORMAL", 0, fmt, 0, offset, inputClass, instanceStepRate };
        }
        else if ( hasUvAfterNormal && i == 2 )
        {
            out[count] = { "TEXCOORD", 0, fmt, 0, offset, inputClass, instanceStepRate };
        }
        else
        {
            const UINT semanticIndex = hasNormal ? static_cast<UINT>( hasUvAfterNormal ? i - 2 : i - 1 )
                                                 : static_cast<UINT>( i - 1 );

            out[count] = { "TEXCOORD", semanticIndex, fmt, 0, offset, inputClass, instanceStepRate };
        }

        ++count;
        offset += (UINT)dvb.attribComponents[i] * sizeof( float );
    }
}


PSOKey12
Dx12PipelineOwner::BuildPSOKey( VertexFormat12 format, bool instanced, const RasterStateDesc& rasterState ) const
{
    PSOKey12 key = {};
    key.rootSignatureIdentity = m_rootSignatureIdentity;
    key.shaderVSHash = m_activeShader->GetVSBytecodeHash();
    key.shaderPSHash = m_activeShader->GetPSBytecodeHash();
    key.format = format;
    key.isInstanced = instanced;
    key.blendEnabled = rasterState.blendEnabled;
    key.blendSrc = rasterState.sourceBlend;
    key.blendDst = rasterState.destinationBlend;
    key.depthEnabled = rasterState.depthTest;
    key.depthWriteEnabled = rasterState.depthWrite;
    key.cullEnabled = rasterState.cullMode == CullMode::Back;
    key.polyOffsetEnabled = rasterState.depthBias.enabled;
    key.polyOffsetDepthBias = rasterState.depthBias.enabled
                                  ? TranslatePolygonOffsetDepthBiasDX12( rasterState.depthBias.constant )
                                  : 0;

    key.polyOffsetSlopeScaledDepthBias = rasterState.depthBias.enabled
                                             ? TranslatePolygonOffsetSlopeBiasDX12( rasterState.depthBias.slopeScaled )
                                             : 0.0f;

    key.rtvFormat = m_currentRTVFormat;
    return key;
}


size_t Dx12PipelineOwner::BuildPSOHash( const PSOKey12& key, const DynamicVBDX12* dynamicVertexBuffer )
{
    size_t psoHash = HashPSOKey( key );
    if ( dynamicVertexBuffer )
    {
        for ( int i = 0; i < dynamicVertexBuffer->numAttribs; ++i )
        {
            psoHash ^= ( (size_t)dynamicVertexBuffer->attribComponents[i] << ( i * 4 ) );
        }

        // Per-instance and per-vertex layouts can carry identical semantic
        // widths but are different PSOs; keep that classification in identity.
        psoHash ^= dynamicVertexBuffer->perInstance ? ( size_t { 1 } << 63u ) : 0u;
    }

    return psoHash;
}


ID3D12PipelineState* Dx12PipelineOwner::FindOrCreatePSO( ID3D12Device* device,
                                                         const PSOKey12& key,
                                                         size_t psoHash,
                                                         VertexFormat12 format,
                                                         bool instanced,
                                                         const InstancedMeshDX12* instancedMesh,
                                                         const DynamicVBDX12* dynamicVertexBuffer,
                                                         const RasterStateDesc& rasterState,
                                                         bool precompile )
{
    for ( size_t i = 0; i < m_psoCacheCount; ++i )
    {
        if ( m_psoCache[i].hash == psoHash )
        {
            ++m_psoCacheHitCount;
            return m_psoCache[i].pso;
        }
    }

    ++m_psoCacheMissCount;
    // Diagnostics: a cache miss means the renderer discovered a new pipeline
    // shape. The declared flag separates pass preparation from draw-time
    // discovery without creating a second cache or identity path.
    SkullbonezCore::Core::Log().WriteEventf(
        "dx12_pso_cache_miss hash=%llu cache_size=%llu root_signature_identity=%llu vs_hash=%llu "
        "ps_hash=%llu format=%u instanced=%d blend=%d depth=%d depth_write=%d cull=%d rtv_format=%u "
        "precompile=%d",
        static_cast<unsigned long long>( psoHash ),
        static_cast<unsigned long long>( m_psoCacheCount ),
        static_cast<unsigned long long>( key.rootSignatureIdentity ),
        static_cast<unsigned long long>( key.shaderVSHash ),
        static_cast<unsigned long long>( key.shaderPSHash ),
        static_cast<unsigned int>( key.format ),
        key.isInstanced ? 1 : 0,
        key.blendEnabled ? 1 : 0,
        key.depthEnabled ? 1 : 0,
        key.depthWriteEnabled ? 1 : 0,
        key.cullEnabled ? 1 : 0,
        static_cast<unsigned int>( key.rtvFormat ),
        precompile ? 1 : 0 );

    if ( m_psoCacheCount >= m_psoCache.size() )
    {
        // Invariant: PSO variants are bounded by the fixed cache in the
        // backend. A new draw-state family needs an intentional cache budget,
        // not growth from pass preparation or draw submission.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 graphics PSO cache exhausted. capacity=%zu hash=%llu format=%u instanced=%d",
                  m_psoCache.size(),
                  static_cast<unsigned long long>( psoHash ),
                  static_cast<unsigned int>( key.format ),
                  key.isInstanced ? 1 : 0 );
    }

    ID3D12PipelineState* pso = CreatePSO( device, format, instanced, instancedMesh, dynamicVertexBuffer, rasterState );
    if ( !pso )
    {
        return nullptr;
    }

    m_psoCache[m_psoCacheCount].hash = psoHash;
    m_psoCache[m_psoCacheCount].pso = pso;
    ++m_psoCacheCount;
    if ( precompile )
    {
        ++m_precompiledPsoCount;
    }

    return pso;
}


ID3D12PipelineState* Dx12PipelineOwner::CreatePSO( ID3D12Device* device,
                                                   VertexFormat12 format,
                                                   bool instanced,
                                                   const InstancedMeshDX12* im,
                                                   const DynamicVBDX12* dvb,
                                                   const RasterStateDesc& rasterState )
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

    const char* inputContractError = nullptr;
    if ( !m_activeShader->ValidateInputLayout( elements, numElements, inputContractError ) )
    {
        // Lane R: mesh/layout selection is startup-owned pipeline input. A
        // reflected mismatch skips PSO publication and names the owning path.
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_shader_input_contract_rejected owner=Dx12PipelineOwner reason=%s",
            inputContractError );

        SkullbonezCore::Core::Log().FlushAll();
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
    psoDesc.RasterizerState.CullMode = rasterState.cullMode == CullMode::Back ? D3D12_CULL_MODE_BACK
                                                                              : D3D12_CULL_MODE_NONE;

    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    if ( rasterState.depthBias.enabled )
    {
        psoDesc.RasterizerState.DepthBias = TranslatePolygonOffsetDepthBiasDX12( rasterState.depthBias.constant );
        psoDesc.RasterizerState.SlopeScaledDepthBias = TranslatePolygonOffsetSlopeBiasDX12( rasterState.depthBias.slopeScaled );
    }

    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // Depth stencil
    psoDesc.DepthStencilState.DepthEnable = rasterState.depthTest ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = rasterState.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                                                      : D3D12_DEPTH_WRITE_MASK_ZERO;

    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // Blend
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if ( rasterState.blendEnabled )
    {
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = MapBlendFactor( rasterState.sourceBlend );
        psoDesc.BlendState.RenderTarget[0].DestBlend = MapBlendFactor( rasterState.destinationBlend );
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
    const bool attachedCachedBlob = m_persistentPsoCache.Attach( psoDesc );
    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );
    if ( FAILED( hr ) && attachedCachedBlob )
    {
        // Lane R: cached bytes are external driver-specific cold-start input.
        // Retry the exact recipe once without them and evict the rejected row.
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_pso_disk_cache_rejected owner=Dx12PipelineOwner hresult=0x%08X bytes=%llu",
            static_cast<unsigned int>( hr ),
            static_cast<unsigned long long>( psoDesc.CachedPSO.CachedBlobSizeInBytes ) );

        m_persistentPsoCache.RejectAttached( psoDesc );
        hr = device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );
    }

    if ( FAILED( hr ) || !pso )
    {
        // Lane R: a graphics PSO can fail because the active shader/input layout
        // or device state is invalid. The draw path can skip this submission and
        // keep the renderer alive; fixed cache-cap exhaustion above remains fatal.
        SkullbonezCore::Core::Log().WriteEventf(
            "dx12_graphics_pso_create_failed hresult=0x%08X format=%u instanced=%d rtv_format=%u",
            static_cast<unsigned int>( FAILED( hr ) ? hr : E_FAIL ),
            static_cast<unsigned int>( format ),
            instanced ? 1 : 0,
            static_cast<unsigned int>( m_currentRTVFormat ) );

        SkullbonezCore::Core::Log().FlushAll();
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
    // Retain only a borrowed pointer in the fixed cold-cache accounting table.
    // Shutdown asks each still-live PSO for its driver cache blob before release.
    m_persistentPsoCache.Store( psoDesc, pso );
    return pso;
}


bool Dx12PipelineOwner::PrecompileDraw( ID3D12Device* device,
                                        VertexFormat12 format,
                                        bool instanced,
                                        const InstancedMeshDX12* instancedMesh,
                                        const DynamicVBDX12* dynamicVertexBuffer,
                                        const RasterStateDesc& declaredRasterState )
{
    if ( !device || !m_activeShader || declaredRasterState.targets.sampleCount != 1 )
    {
        return false;
    }

    const PSOKey12 key = BuildPSOKey( format, instanced, declaredRasterState );
    const size_t psoHash = BuildPSOHash( key, dynamicVertexBuffer );
    return FindOrCreatePSO( device,
                            key,
                            psoHash,
                            format,
                            instanced,
                            instancedMesh,
                            dynamicVertexBuffer,
                            declaredRasterState,
                            true ) != nullptr;
}


bool Dx12PipelineOwner::PrepareDraw( ID3D12Device* device,
                                     ID3D12GraphicsCommandList* commandList,
                                     Dx12CommandRecordingState& recording,
                                     Dx12TextureOwner& textures,
                                     VertexFormat12 format,
                                     bool instanced,
                                     const InstancedMeshDX12* im,
                                     const DynamicVBDX12* dvb,
                                     const RasterStateDesc& rasterState )
{
    // Concept: the PSO cache key is the complete "shape" of a draw pipeline.
    //
    // DX12 cannot cheaply toggle individual pieces of fixed-function state the
    // way old immediate renderers did. The vertex layout, stable shader bytecode
    // hashes, blend/depth/cull state, polygon offset, instancing mode, and
    // render-target format all participate in the Pipeline State Object. If any
    // of those values changes, the cached PSO may no longer describe the draw correctly.
    // Include the root-signature identity too: today raster draws share
    // UnifiedRaster, but future graph-local signatures must not accidentally
    // reuse an incompatible cached PSO. The owner-issued epoch survives COM
    // address recycling and therefore represents recipe identity, not storage.
    const PSOKey12 key = BuildPSOKey( format, instanced, rasterState );
    const size_t psoHash = BuildPSOHash( key, dvb );

    // Fast path: if PSO, texture descriptor bindings, and render targets are
    // unchanged, the only per-draw work left is uploading the constant buffer.
    // This is the common path for many objects sharing the same mesh/shader
    // shape, such as generated balls or boxes.
    bool psoChanged = m_pipelineBindingDirty || ( psoHash != m_lastPSOHash );

    if ( !psoChanged && !textures.BindingsDirty() && !m_targetsDirty )
    {
        // The last bound PSO is a cache reuse even though the fast path avoids
        // walking the fixed array.
        ++m_psoCacheHitCount;
        // Only the constant buffer has changed (e.g. model matrix per ball)
        if ( m_activeShader )
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
            if ( recording.HasFailure() )
            {
                return false;
            }

            // Hazard: zero means the phase policy rejected this upload. Drawing
            // would reuse the prior root constant address, so fail the caller.
            if ( m_activeShader->ConstantBufferUploadSize() > 0 && cbAddr == 0 )
            {
                return false;
            }

            if ( cbAddr )
            {
                commandList->SetGraphicsRootConstantBufferView(
                    UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS,
                    cbAddr );
            }
        }

        return true;
    }

    // Full state setup path: at least one expensive binding category changed,
    // so rebuild/reuse the PSO, rebind the root signature, refresh constants,
    // publish texture indices, and update output targets.
    if ( psoChanged )
    {
        ID3D12PipelineState*
            pso = FindOrCreatePSO( device, key, psoHash, format, instanced, im, dvb, rasterState, false );

        if ( !pso )
        {
            return false;
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

        // Hazard: do not publish a draw after the phase policy rejected its
        // constants; the command list may still contain an older root address.
        if ( m_activeShader->ConstantBufferUploadSize() > 0 && cbAddr == 0 )
        {
            return false;
        }

        if ( cbAddr )
        {
            commandList->SetGraphicsRootConstantBufferView( UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS,
                                                            cbAddr );
        }
    }

    // Owner ruling: Dx12PipelineOwner carries six native descriptor indices in
    // b1 root constants. Engine instance rows retain material-domain values;
    // putting DX12 heap identity there would leak the backend boundary and
    // overwrite packed material flags. The matching descriptors already occupy
    // stable shader-visible rows, so this draw loop allocates or copies nothing.
    if ( psoChanged || textures.BindingsDirty() )
    {
        UINT textureIndices[TEXTURE_SLOT_COUNT] = {};
        for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
        {
            textureIndices[slot] = textures.ResolveBoundSrv( slot );
        }

        commandList->SetGraphicsRoot32BitConstants( UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES,
                                                    TEXTURE_SLOT_COUNT,
                                                    textureIndices,
                                                    0 );

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
    m_pipelineBindingDirty = false;
    return true;
}


void Dx12PipelineOwner::SetActiveShader( const ShaderDX12* shader )
{
    m_activeShader = shader;
    m_pipelineBindingDirty = true;
}


void Dx12PipelineOwner::ReleaseShaderPipelinesForReload()
{
    // Lifetime: Dx12ShaderDevelopment proved the GPU drain and staged every
    // replacement. Persist old blobs while their source PSOs remain live.
    m_persistentPsoCache.Shutdown();
    for ( size_t index = 0; index < m_psoCacheCount; ++index )
    {
        if ( m_psoCache[index].pso )
        {
            m_psoCache[index].pso->Release();
        }

        m_psoCache[index] = {};
    }

    m_psoCacheCount = 0;
}


void Dx12PipelineOwner::RestoreShaderPipelinesAfterReload()
{
    // Invariant: bytecode adoption is complete and cannot fail. Reopen the
    // persistent cache against the new manifest before the next PSO lookup.
    m_lastPSOHash = 0;
    m_pipelineBindingDirty = true;
    m_targetsDirty = true;
    m_persistentPsoCache.Initialize( { m_rootSignatureSerialized.data(), m_rootSignatureSerializedSize } );
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
    m_pipelineBindingDirty = true;
}


const ShaderDX12* Dx12PipelineOwner::ActiveShader() const
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
    m_pipelineBindingDirty = true;
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
    m_pipelineBindingDirty = true;
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


uint64_t Dx12PipelineOwner::CacheHitCount() const
{
    return m_psoCacheHitCount;
}


uint64_t Dx12PipelineOwner::CacheMissCount() const
{
    return m_psoCacheMissCount;
}


uint64_t Dx12PipelineOwner::PrecompiledPsoCount() const
{
    return m_precompiledPsoCount;
}


void Dx12PipelineOwner::Shutdown()
{
    // Lifetime: serialize while the blob store and source PSOs are still live.
    // This is bounded cold shutdown I/O, never per-frame cache growth.
    m_persistentPsoCache.Shutdown();
    for ( size_t index = 0; index < m_psoCacheCount; ++index )
    {
        if ( m_psoCache[index].pso )
        {
            m_psoCache[index].pso->Release();
            m_psoCache[index].pso = nullptr;
        }
    }

    m_psoCacheCount = 0;
    m_psoCacheHitCount = 0;
    m_psoCacheMissCount = 0;
    m_precompiledPsoCount = 0;
    if ( m_rootSignature )
    {
        m_rootSignature->Release();
        m_rootSignature = nullptr;
    }

    // Lifetime: every dependent PSO is gone before the active signature epoch
    // is invalidated. Keep the next value monotonic across owner reuse.
    m_rootSignatureIdentity = 0;
    m_activeShader = nullptr;
    m_rootSignatureSerialized = {};
    m_rootSignatureSerializedSize = 0;
    ResetCommandState();
}


void Dx12PipelineOwner::ResetCommandState()
{
    Dx12PipelineCommandState defaults;
    defaults.Reset();
    m_activeShader = defaults.m_activeShader;
    m_viewport = defaults.m_viewport;
    m_scissorRect = defaults.m_scissorRect;
    m_currentRTV = defaults.m_currentRTV;
    m_currentDSV = defaults.m_currentDSV;
    m_currentRTVFormat = defaults.m_currentRTVFormat;
    m_renderingToFBO = defaults.m_renderingToFBO;
    m_lastPSOHash = defaults.m_lastPSOHash;
    m_pipelineBindingDirty = defaults.m_pipelineBindingDirty;
    m_targetsDirty = defaults.m_targetsDirty;
}

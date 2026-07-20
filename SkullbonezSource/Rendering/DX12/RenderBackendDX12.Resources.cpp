/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
Purpose:
  Creates, transitions, and names DX12 resources and delegates the opt-in
  offline-DXC hot-reload transaction.

Summary:
  RenderBackendDX12.Resources.cpp creates, transitions, and names DX12
  resources used by the renderer. The shader-development interface remains a
  composition-root sequence here: ask its concrete owner to bake, drain the
  frame owner, then let that owner stage and publish the verified generation.

Glossary:
  Upload arena: Frame-scoped CPU-visible staging memory used to seed default
  heap resources before their copy commands execute.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Hot reload: Developer-only rebake and transactional replacement of live
    shader bytecode and dependent pipelines.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Mesh resource creation stops before pointer access when command reopening
    or upload reservation has latched a failure.
  - Meshes borrow Dx12Diagnostics as one owner, never separate counter/trace aliases.
  - Dx12ShaderDevelopment rejects reload without the exact launch token and
    never mutates live bytecode before every replacement contract passes.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "../../Core/WindowConstants.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
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

// --- RenderBackendDX12 Resources methods ---


std::unique_ptr<IShader> RenderBackendDX12::CreateShader( const char* baseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";
    if ( !Device() )
    {
        return nullptr;
    }
    auto shader = std::make_unique<ShaderDX12>( m_renderDevice,
                                                m_pipelineOwner,
                                                m_shaderDevelopment,
                                                m_frameOwner.UploadReservations() );
    if ( !shader->Compile( hlslPath.c_str() ) )
    {
        // Lane R: shader files and compiler output are external inputs. Return
        // a null shader so setup/render owners can skip the dependent draw while
        // the DX12 validation log names the missing program.
        SkullbonezCore::Core::Log().WriteEventf( "dx12_shader_create_failed path=%s", hlslPath.c_str() );
        SkullbonezCore::Core::Log().FlushAll();
        return nullptr;
    }
    return shader;
}


bool RenderBackendDX12::ShaderHotReloadEnabled() const
{
    return m_shaderDevelopment.Enabled();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::ReloadShadersFromSource()
{
    const SkullbonezCore::Core::SbResult bakeResult = m_shaderDevelopment.BakeSourceGeneration();
    if ( !bakeResult.ok )
    {
        return bakeResult;
    }

    // Lifetime: all current PSOs may still be referenced by submitted command
    // lists. Drain before the transactional owner releases either main-cache or
    // grid-line PSOs and publishes replacement bytecode.
    const SkullbonezCore::Core::SbResult drainResult = Finish();
    if ( !drainResult.ok )
    {
        return drainResult;
    }
    return m_shaderDevelopment.ReloadBakedGeneration( Device() );
}


std::unique_ptr<IMesh>
RenderBackendDX12::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    if ( !Device() )
    {
        return nullptr;
    }
    VertexFormat12 format;
    int floatsPerVert;
    if ( hasNormals && hasTexCoords )
    {
        format = VertexFormat12::Pos3_Norm3_Tex2;
        floatsPerVert = 8;
    }
    else if ( hasTexCoords )
    {
        format = VertexFormat12::Pos3_Tex2;
        floatsPerVert = 5;
    }
    else
    {
        format = VertexFormat12::Pos3;
        floatsPerVert = 3;
    }

    if ( !m_frameOwner.EnsureOpen().ok )
    {
        return nullptr;
    }
    UINT64 dataSize = (UINT64)vertexCount * floatsPerVert * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr =
        m_frameOwner.UploadReservations().ReserveUpload( dataSize, 4, RenderUploadCategory::DynamicVertex );
    if ( uploadAddr == 0 )
    {
        return nullptr;
    }
    uint8_t* uploadPtr = m_frameOwner.UploadReservations().UploadPointer( uploadAddr );

    auto mesh = std::make_unique<MeshDX12>( m_renderDevice, m_frameOwner.DrawGate(), m_diagnostics );
    if ( !mesh->Create( Device(),
                        CommandList(),
                        data,
                        vertexCount,
                        floatsPerVert,
                        format,
                        uploadAddr,
                        uploadPtr,
                        m_frameOwner.Uploads().Resource( m_frameOwner.AllocatorIndex() ) ) )
    {
        return nullptr;
    }
    return mesh;
}


std::unique_ptr<IFramebuffer>
RenderBackendDX12::CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat )
{
    if ( !Device() )
    {
        return nullptr;
    }
    auto fbo = std::make_unique<FramebufferDX12>( m_renderDevice,
                                                  m_pipelineOwner,
                                                  m_textureOwner,
                                                  m_descriptorHeaps,
                                                  m_frameOwner.DrawGate(),
                                                  m_frameOwner.ResourceRelease(),
                                                  colorFormat );
    if ( !fbo->Create( width, height ) )
    {
        return nullptr;
    }
    return fbo;
}

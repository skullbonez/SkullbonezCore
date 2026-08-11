/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
Purpose:
  Creates, transitions, and names DX12 resources.

Summary:
  Creates, transitions, and names DX12 resources used by the
  renderer. The concrete shader-development owner now
  contains its cold bake, drain, stage, and publication transaction.

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
  - Agentic/Reference/engine-glossary.md
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
    SkullbonezCore::Core::Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex,
                                             capacity );

    SkullbonezCore::Core::Log().FlushAll();
}

// --- Dx12ResourceBuilder methods ---


std::unique_ptr<ShaderDX12> Dx12ResourceBuilder::CreateShader( const char* baseName, const char* contractBaseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";

    if ( !m_device.Device() )
    {
        return nullptr;
    }

    auto shader = std::make_unique<ShaderDX12>( m_device, m_pipeline, m_shaderDevelopment, m_frame.UploadReservations() );

    if ( !shader->Compile( hlslPath.c_str(), contractBaseName ) )
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


std::unique_ptr<MeshDX12> Dx12ResourceBuilder::CreateMesh( const float* data, int vertexCount, bool hasNormals,
                                                           bool hasTexCoords )
{
    if ( !m_device.Device() )
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

    if ( !m_frame.EnsureOpen().Ok() )
    {
        return nullptr;
    }

    UINT64 dataSize = static_cast<UINT64>( vertexCount ) * floatsPerVert * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = m_frame.UploadReservations().ReserveUpload( dataSize, 4,
                                                                                       RenderUploadCategory::DynamicVertex );

    if ( uploadAddr == 0 )
    {
        return nullptr;
    }

    uint8_t* uploadPtr = m_frame.UploadReservations().UploadPointer( uploadAddr );

    auto mesh = std::make_unique<MeshDX12>( m_device, m_frame.DrawGate(), m_diagnostics );

    if ( !mesh->Create( m_device.Device(), m_device.CommandList(), data, vertexCount, floatsPerVert, format, uploadAddr,
                        uploadPtr, m_frame.Uploads().Resource( m_frame.AllocatorIndex() ) ) )
    {
        return nullptr;
    }

    return mesh;
}


std::unique_ptr<FramebufferDX12> Dx12ResourceBuilder::CreateFramebuffer( int width, int height,
                                                                         FramebufferColorFormat colorFormat )
{
    if ( !m_device.Device() )
    {
        return nullptr;
    }

    auto fbo = std::make_unique<FramebufferDX12>( m_device, m_pipeline, m_textures, m_descriptors, m_frame.DrawGate(),
                                                  m_frame.ResourceRelease(), colorFormat );

    if ( !fbo->Create( width, height ) )
    {
        return nullptr;
    }

    return fbo;
}

/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
Purpose:
  Creates, transitions, and names DX12 resources used by the renderer.

Mental model:
  RenderBackendDX12.Resources.cpp creates, transitions, and names DX12
  resources used by the renderer. As an implementation unit, keep edits
  anchored on DX12 ownership, descriptors, resources, and command submission
  and on the glossary/invariants below.

Glossary:
  Upload arena: Frame-scoped CPU-visible staging memory used to seed default
  heap resources before their copy commands execute.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Mesh resource creation stops before pointer access when command reopening
    or upload reservation has latched a failure.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "../../Runtime/WindowConstants.h"
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

// --- RenderBackendDX12 Resources methods ---


std::unique_ptr<IShader> RenderBackendDX12::CreateShader( const char* baseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";
    auto shader = std::make_unique<ShaderDX12>( *this );
    if ( !shader->Compile( hlslPath.c_str() ) )
    {
        // Lane R: shader files and compiler output are external inputs. Return
        // a null shader so setup/render owners can skip the dependent draw while
        // the DX12 validation log names the missing program.
        Log().WriteEventf( "dx12_shader_create_failed path=%s", hlslPath.c_str() );
        Log().FlushAll();
        return nullptr;
    }
    return shader;
}


std::unique_ptr<IMesh>
RenderBackendDX12::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
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

    if ( !EnsureCommandListOpen().ok )
    {
        return nullptr;
    }
    UINT64 dataSize = (UINT64)vertexCount * floatsPerVert * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = ReserveUpload( dataSize, 4 );
    if ( uploadAddr == 0 )
    {
        return nullptr;
    }
    uint8_t* uploadPtr = GetUploadPtr( uploadAddr );

    auto mesh = std::make_unique<MeshDX12>( *this );
    if ( !mesh->Create( Device(), CommandList(), data, vertexCount, floatsPerVert, format, uploadAddr, uploadPtr ) )
    {
        return nullptr;
    }
    return mesh;
}


std::unique_ptr<IFramebuffer>
RenderBackendDX12::CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat )
{
    auto fbo = std::make_unique<FramebufferDX12>( *this, colorFormat );
    if ( !fbo->Create( width, height ) )
    {
        return nullptr;
    }
    return fbo;
}

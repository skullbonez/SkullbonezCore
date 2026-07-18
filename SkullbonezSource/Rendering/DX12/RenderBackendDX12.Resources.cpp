/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
Purpose:
  Creates, transitions, and names DX12 resources and owns the opt-in offline-DXC
  hot-reload transaction.

Summary:
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
  Hot reload: Developer-only rebake and transactional replacement of live
    shader bytecode and dependent pipelines.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Mesh resource creation stops before pointer access when command reopening
    or upload reservation has latched a failure.
  - Meshes borrow Dx12Diagnostics as one owner, never separate counter/trace aliases.
  - Shader reload is disabled without the exact launch token and never mutates
    live bytecode before the complete bake and replacement contracts pass.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "../../Runtime/WindowConstants.h"
#include "ShaderDX12.h"
#include "ShaderBytecodeManifest.h"
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
    auto shader = std::make_unique<ShaderDX12>( m_renderDevice, m_pipelineOwner, m_frameOwner.UploadReservations() );
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
    return DevShaderHotReloadEnabled();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::ReloadShadersFromSource()
{
    if ( !ShaderHotReloadEnabled() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Shader hot reload requires --dev-shader-hot-reload" );
    }
    constexpr char BAKE_PATH[] = "tools\\bake_shaders.bat";
    if ( GetFileAttributesA( BAKE_PATH ) == INVALID_FILE_ATTRIBUTES )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Shader bake tool is unavailable from this working directory" );
    }

    // Cold utility action: invoke the same pinned DXC bake used by validation.
    // The process is synchronous so the manifest and all stage files become one
    // complete generation before any live shader attempts to read them.
    char commandLine[] = "cmd.exe /d /c tools\\bake_shaders.bat";
    STARTUPINFOA startup = {};
    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};
    fprintf( stdout, "[shader-hot-reload] bake begin\n" );
    fflush( stdout );
    if ( !CreateProcessA( nullptr,
                          commandLine,
                          nullptr,
                          nullptr,
                          FALSE,
                          CREATE_NO_WINDOW,
                          nullptr,
                          nullptr,
                          &startup,
                          &process ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Shader bake process failed to start (error=%lu)",
                                                        GetLastError() );
    }
    const DWORD waitResult = WaitForSingleObject( process.hProcess, INFINITE );
    DWORD exitCode = ERROR_PROCESS_ABORTED;
    const bool exited = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess( process.hProcess, &exitCode );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    if ( !exited || exitCode != 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "Shader bake failed (wait=%lu exit=%lu)",
                                                        waitResult,
                                                        exitCode );
    }

    // Lifetime: all current PSOs may still be referenced by submitted command
    // lists. Drain before the transactional owner releases either main-cache or
    // grid-line PSOs and publishes replacement bytecode.
    const SkullbonezCore::Core::SbResult drainResult = Finish();
    if ( !drainResult.ok )
    {
        return drainResult;
    }
    ID3D12PipelineState* generateMipsCandidate = nullptr;
    Dx12TextureCommands textureCommands( m_renderDevice, m_frameOwner );
    const SkullbonezCore::Core::SbResult computeReloadResult =
        m_textureOwner.PrepareGenerateMipsShaderReload( textureCommands, generateMipsCandidate );
    if ( !computeReloadResult.ok )
    {
        return computeReloadResult;
    }
    const SkullbonezCore::Core::SbResult reloadResult = m_pipelineOwner.ReloadShadersFromBakedAssets();
    if ( !reloadResult.ok )
    {
        generateMipsCandidate->Release();
        return reloadResult;
    }
    m_textureOwner.AdoptGenerateMipsShaderReload( generateMipsCandidate );
    m_geometryOwner.InvalidateGridLinePipelinesForShaderReload();
    m_pipelineOwner.InvalidateCommandState();
    fprintf( stdout, "[shader-hot-reload] committed\n" );
    fflush( stdout );
    SkullbonezCore::Core::Log().WriteEventf( "dx12_shader_hot_reload_complete owner=RenderBackendDX12" );
    return SkullbonezCore::Core::SbResult::Success();
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

    if ( !EnsureCommandListOpen().ok )
    {
        return nullptr;
    }
    UINT64 dataSize = (UINT64)vertexCount * floatsPerVert * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = ReserveUpload( dataSize, 4, RenderUploadCategory::DynamicVertex );
    if ( uploadAddr == 0 )
    {
        return nullptr;
    }
    uint8_t* uploadPtr = GetUploadPtr( uploadAddr );

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

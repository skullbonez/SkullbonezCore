/*
File: SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp
Purpose:
  Implements the cold DX12 shader rebake and all-or-nothing adoption transaction.

Summary:
  The owner launches the repository's pinned bake tool, validates replacement
  bytecode for every registered raster shader, stages the generate-mips compute
  PSO, drains the frame owner, and then performs a no-fail publication.

Glossary:
  Offline DXC bake: Repository tool invocation that creates one pinned manifest
    and its complete shader-stage generation.
  Adoption: No-fail swap from staged bytecode/PSOs to the live generation.
  Dependent PSO: Native pipeline whose recipe embeds a shader bytecode identity.

Invariants:
  - Candidate preparation completes for every shader before any live object changes.
  - The commit contains no recoverable operation after old PSOs are released.
  - Registered rows are fixed-capacity and contain no backend or command-list pointer.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
  - tools/bake_shaders.bat
*/
#include "Dx12ShaderDevelopment.h"
#include "../../Core/SbDiagnosticStore.h"

#include "RenderBackendDX12.h"
#include "ShaderBytecodeManifest.h"
#include "ShaderDX12.h"
#include "../../Core/Log.h"
#include "../../Core/FatalError.h"

#include <array>
#include <cstdio>

using namespace SkullbonezCore::Rendering;


Dx12ShaderDevelopment::Dx12ShaderDevelopment( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                              Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures,
                                              Dx12GeometryOwner& geometry, Dx12RenderDevice& device, Dx12FrameOwner& frame,
                                              Dx12Diagnostics& diagnostics )
    : m_resultDiagnostics( resultDiagnostics ), m_pipeline( pipeline ), m_textures( textures ), m_geometry( geometry ),
      m_device( device ), m_frame( frame ), m_diagnostics( diagnostics )
{
}


bool Dx12ShaderDevelopment::Enabled() const
{
    return DevShaderHotReloadEnabled();
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::ReloadShadersFromSource()
{
    const SkullbonezCore::Core::SbResult bakeResult = BakeSourceGeneration();

    if ( !bakeResult.Ok() )
    {
        return bakeResult;
    }

    // Lifetime: every current PSO may still be referenced by submitted command
    // lists. The shader owner publishes no replacement until the frame owner
    // proves those lists complete and reopens the recording epoch.
    const SkullbonezCore::Core::SbResult drainResult = m_frame.FinishAndReopen( m_diagnostics );

    if ( !drainResult.Ok() )
    {
        return drainResult;
    }

    return ReloadBakedGeneration( m_device.Device() );
}


void Dx12ShaderDevelopment::RegisterShader( ShaderDX12* shader )
{
    if ( !shader )
    {
        return;
    }

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] == shader )
        {
            return;
        }
    }

    if ( m_liveShaderCount >= m_liveShaders.size() )
    {
        // Fatal invariant: the registry is a fixed backend-lifetime contract. Growing it
        // during resource creation would violate the runtime allocation policy.
        SB_FATAL( "Dx12ShaderDevelopment", "Live raster shader registry exhausted. capacity=%zu", m_liveShaders.size() );
    }

    m_liveShaders[m_liveShaderCount++] = shader;
}


void Dx12ShaderDevelopment::UnregisterShader( ShaderDX12* shader )
{
    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] != shader )
        {
            continue;
        }

        m_liveShaders[index] = m_liveShaders[m_liveShaderCount - 1];
        m_liveShaders[m_liveShaderCount - 1] = nullptr;
        --m_liveShaderCount;
        break;
    }

    if ( m_pipeline.ActiveShader() == shader )
    {
        // Invariant: deleting a live shader cannot leave pipeline desired state
        // borrowing its identity for the next draw.
        m_pipeline.SetActiveShader( nullptr );
    }
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::BakeSourceGeneration() const
{
    if ( !Enabled() )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader hot reload requires --dev-shader-hot-reload" );
    }

    constexpr char BAKE_PATH[] = "tools\\bake_shaders.bat";

    if ( GetFileAttributesA( BAKE_PATH ) == INVALID_FILE_ATTRIBUTES )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12",
                                            "Shader bake tool is unavailable from this working directory" );
    }

    // Cold utility action: invoke the same pinned DXC bake used by validation.
    // Synchronous completion makes the manifest and stage files one generation.
    char commandLine[] = "cmd.exe /d /c tools\\bake_shaders.bat";
    STARTUPINFOA startup = {};

    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};

    fprintf( stdout, "[shader-hot-reload] bake begin\n" );
    fflush( stdout );

    if ( !CreateProcessA( nullptr, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &process ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader bake process failed to start (error=%lu)",
                                            GetLastError() );
    }

    const DWORD waitResult = WaitForSingleObject( process.hProcess, INFINITE );
    DWORD exitCode = ERROR_PROCESS_ABORTED;
    const bool exited = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess( process.hProcess, &exitCode );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );

    if ( !exited || exitCode != 0 )
    {
        // Recoverable error: the external bake can fail without changing the live generation.
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader bake failed (wait=%lu exit=%lu)", waitResult,
                                            exitCode );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::ReloadBakedGeneration( ID3D12Device* device )
{
    // Runtime allocation policy: candidate reflection containers may allocate only in
    // the explicit BackendInit developer scope held by the F9 caller.
    ID3D12PipelineState* generateMipsCandidate = nullptr;
    const SkullbonezCore::Core::SbResult computeResult = m_textures.PrepareGenerateMipsShaderReload( device,
                                                                                                     generateMipsCandidate );

    if ( !computeResult.Ok() )
    {
        return computeResult;
    }

    std::array<ShaderDX12ReloadPayload, LIVE_SHADER_CAPACITY> candidates;

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        ShaderDX12* live = m_liveShaders[index];

        if ( live && !live->PrepareReload( candidates[index] ) )
        {
            generateMipsCandidate->Release();

            // Recoverable error: a changed shader interface needs a rebuilt executable;
            // every live shader and PSO still names the previous generation.
            return m_resultDiagnostics.Failure( "Rendering/DX12",
                                                "Shader hot reload rejected changed or invalid bytecode contract" );
        }
    }

    // Lifetime: the composition root drained the GPU before this method. From
    // here through publication every operation is a bounded no-fail release/swap.
    m_pipeline.ReleaseShaderPipelinesForReload();

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] )
        {
            m_liveShaders[index]->AdoptReload( candidates[index] );
        }
    }

    m_pipeline.RestoreShaderPipelinesAfterReload();
    m_textures.AdoptGenerateMipsShaderReload( generateMipsCandidate );
    m_geometry.InvalidateGridLinePipelinesForShaderReload();
    m_pipeline.InvalidateCommandState();

    fprintf( stdout, "[shader-hot-reload] committed\n" );
    fflush( stdout );
    SkullbonezCore::Core::Log().WriteEventf( "dx12_shader_hot_reload_complete owner=Dx12ShaderDevelopment shaders=%llu",
                                             static_cast<unsigned long long>( m_liveShaderCount ) );

    return SkullbonezCore::Core::SbResult::Success();
}


void Dx12ShaderDevelopment::ResetAfterShutdown()
{
    // Lifetime: runtime and geometry resource owners must destroy every shader
    // before the renderer tears down pipeline/device state. A remaining row
    // would become a dangling owner reference after backend destruction.
    if ( m_liveShaderCount != 0 )
    {
        SB_FATAL( "Dx12ShaderDevelopment", "Shader registry remained live at backend shutdown. count=%zu capacity=%zu",
                  m_liveShaderCount, m_liveShaders.size() );
    }

    m_liveShaders = {};
}
